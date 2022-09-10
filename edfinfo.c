// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * edfinfo - read information from electricity meter (France)
 *
 * Copyright (C) 2022, Cédric Le Goater <clg@kaod.org>
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <sys/signalfd.h>

#include "serial.h"
#include "log.h"
#include "edfinfo.h"
#include "control.h"
#include "frame.h"
#include "backend.h"
#include "stats.h"

const char progname[]	= "edfinfod";
const char version[]	= VERSION;

static int sig_fd = -1;
static int serial_fd = -1;

static void push_frame(const char *buffer, size_t len)
{
	struct frame *frame;

	frame = frame_new(buffer, len);
	if (!frame) {
		ERROR("dropping frame");
		stats.frame_error++;
		return;
	}

	if (frame->len > stats.frame_maxlen)
		stats.frame_maxlen = frame->len;
	if (frame->power > stats.power_max)
		stats.power_max = frame->power;
	if (frame->power < stats.power_min)
		stats.power_min = frame->power;

	frame_log(frame);

	stats.frame_stack = frame_stack_add(frame);
	if (stats.frame_stack > stats.frame_stack_max)
		stats.frame_stack_max = stats.frame_stack;

	stats.frame_pushed++;

	/*
	 * Backend will most likely use network and it could be
	 * slow. We will need a workqueue to push frames while reading
	 * the serial line.
	 */
	backend_push(frame);
}

static int read_signal(int sfd)
{
	struct signalfd_siginfo fdsi;

	if (read(sfd, &fdsi, sizeof(fdsi)) != sizeof(fdsi)) {
		ERROR("read() failed: %s", strerror(errno));
		return -1;
	}

	NOTICE("caught signal %d", fdsi.ssi_signo);

	switch (fdsi.ssi_signo) {
	case SIGUSR1:
		stats_log(&stats);
		return 0;
	case SIGINT:
	case SIGQUIT:
	case SIGTERM:
	default:
		return -1;
	}
}

static void print_help(int code)
{
	fprintf(stderr, "\
Usage: %s %s [-vhd]  [-c <FILE>] [-p <PRIORITY>] [-o <FILE>] [-f <TTY>]...\n\
\n\
  -v, --version			display version\n\
  -?, --help			give this help list\n\
      --usage			give a short usage message\n\
  -c, --config <FILE>		use <FILE> to load configuration\n\
  -p, --logpriority <PRIORITY>	use log priority <PRIORITY>. between [0-7]\n\
  -o, --logfile <FILE>		send logs to <FILE>, else use syslog\n\
\n\
  -f, --tty <TTY>		read EDF info from serial port device <TTY>\n\
  -r, --raw <RAW>		record raw data in file <RAW>\n\
  -d, --daemon			daemonize program\n\
\n\
See the %s man page for further information.\n",
		progname, version, progname);

	exit(code);
}

static struct option long_options[] = {
	{ "help",		no_argument, NULL, 'h' },
	{ "usage",		no_argument, NULL, 'h' },
	{ "version",		no_argument, NULL, 'v' },

	{ "config",		required_argument, NULL, 'c' },
	{ "logfile",		required_argument, NULL, 'o' },
	{ "logpriority",	required_argument, NULL, 'p' },
	{ "daemon",		no_argument, NULL, 'd' },
	{ "debug",		no_argument, NULL, 'g' },
	{ "raw",		required_argument, NULL, 'r' },

	{ "tty",		required_argument, NULL, 'f' },

	{ 0,			0,	     NULL,  0 }
};

static const char short_options[] = "hvc:o:p:dgr:f:";

static void print_version(void)
{
	printf("%s %s\n", progname, version);
	exit(0);
}

static void cleanup(int dumpstats)
{
	WARN("exiting...");

	backend_fini();

	if (dumpstats)
		stats_log(&stats);

	frame_stack_clear(frame_stack);

	if (serial_fd != -1)
		serial_close(serial_fd);
	if (control_fd != -1)
		control_close(control_fd);
	if (log_fd != -1)
		close(log_fd);
	if (sig_fd != -1)
		close(sig_fd);
}

static int max_fd(void)
{
	int max = 0;

	if (sig_fd > max)
		max = sig_fd;
	if (control_fd > max)
		max = control_fd;
	if (serial_fd > max)
		max = serial_fd;
	return max;
}

void load_config_file_first(int argc, char *const argv[])
{
	int c;

	while (1) {
		c = getopt_long(argc, argv, short_options, long_options, NULL);
		if (c == -1 || c == 'c')
			break;
	}

	config_init(optarg);
	optind = 1;
}

int main(int argc, char *const argv[])
{
	sigset_t mask;
	int c;
	int receiving_data = 0;

	load_config_file_first(argc, argv);

	while (1) {
		c = getopt_long(argc, argv, short_options, long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'c':
			break; /* already done. ignore */
		case 'g':
			config.debug = 1;
			break;
		case 'r':
			config.serial_lograw = optarg;
			break;
		case 'd':
			config.daemonize = 1;
			break;
		case 'f':
			config.serial_port = optarg;
			break;

		case 'o':
			config.logfile = optarg;
			break;
		case 'p':
			config.logpriority = log_name_to_priority(optarg);
			break;
		case 'v':
			print_version();
			break;
		case 'h':
			print_help(0);
			break;
		default:
			print_help(1);
		}
	}

	log_fd = log_open(config.logfile);

	WARN("%s %s starting", progname, version);

	/* skip serial initialization and use stdin when testing */
	serial_fd = (config.debug) ? 0 : serial_open(config.serial_port);

	if (serial_fd < 0)
		goto out;

	NOTICE("opened serial port '%s'", config.serial_port);

	if (config.serial_lograw)
		serial_open_lograw(config.serial_lograw);

	control_fd = control_open();
	if (control_fd < 0)
		goto out;

	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGQUIT);
	sigaddset(&mask, SIGTERM);

	if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
		ERROR("sigprocmask() failed: %s", strerror(errno));
		goto out;
	}

	sig_fd = signalfd(-1, &mask, 0);
	if (sig_fd == -1) {
		ERROR("signalfd() failed: %s", strerror(errno));
		goto out;
	}

	if (config.daemonize)
		daemon(0, 0);

	if (backend_init()) {
		ERROR("backend initialization failed");
		goto out;
	}

	while (1) {
		int ret;
		fd_set rfds;
		struct timeval tv = { SERIAL_TIMEOUT, 0 };

		FD_ZERO(&rfds);
		FD_SET(sig_fd, &rfds);
		FD_SET(serial_fd, &rfds);
		FD_SET(control_fd, &rfds);

		ret = TEMP_FAILURE_RETRY(select(max_fd() + 1, &rfds,
						NULL, NULL, &tv));
		if (ret == -1) {
			ERROR("select() failed: %s", strerror(errno));
			goto out;
		}

		if (!ret) {
			if (receiving_data) {
				WARN("no data within %d seconds. Signal lost ?",
				     SERIAL_TIMEOUT);
				receiving_data = 0;
				stats.serial_rx_errors++;
			}
			stats.serial_data_loss += SERIAL_TIMEOUT;
			continue;
		}

		stats_update_min_timeout(&stats, &tv);

		if (FD_ISSET(sig_fd, &rfds)) {
			if (read_signal(sig_fd))
				goto out;
		}

		if (FD_ISSET(serial_fd, &rfds)) {
			if (!receiving_data) {
				NOTICE("receiving data");
				receiving_data = 1;
			}
			ret = serial_read(serial_fd, push_frame);
			if (ret == -1 && config.debug)
				goto out;
		}

		if (FD_ISSET(control_fd, &rfds)) {
			control_read(control_fd);
			stats.control_requests++;
		}
	}

out:
	cleanup(1);
	return !config.debug;
}
