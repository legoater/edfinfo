// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * edfinfo - read information from electricity meter (France)
 *
 * Copyright (C) 2022, Cédric Le Goater <clg@kaod.org>
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <sys/signalfd.h>

#include "control.h"
#include "log.h"

const char progname[] = "edfctl";
const char version[]  = VERSION;

static int sig_fd = -1;
static int sock_fd = -1;

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
Usage: %s %s [-vhw] [-p <PORT>] [-H <HOSTNAME>] [<COMMAND>]\n\
\n\
  -v, --version			display version\n\
  -?, --help			give this help list\n\
      --usage			give a short usage message\n\
\n\
  -H, --host <HOST>		connect to <HOST> for UDP requests \n\
				default is \"localhost\"\n\
  -p, --port <PORT>		connect to <PORT> for UDP requests \n\
				default port is \":%d\"\n\
\n\
See the %s man page for further information.\n",
		progname, version,
		config.control_port,
		progname);

	exit(code);
}

static struct option long_options[] = {
	{ "help",		no_argument, NULL, 'h' },
	{ "usage",		no_argument, NULL, 'h' },
	{ "version",		no_argument, NULL, 'v' },

	{ "port",		required_argument, NULL, 'p' },
	{ "host",		required_argument, NULL, 'H' },

	{ 0,			0,	     NULL,  0 }
};

static const char short_options[] = "vhp:H:";

static void print_version(void)
{
	printf("%s %s\n", progname, version);
	exit(0);
}

static int read_sock(int fd)
{
	int ret;
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);
	char buffer[1024];

	memset(buffer, 0, sizeof(buffer));

	ret = recvfrom(fd, buffer, sizeof(buffer), 0,
		       (struct sockaddr *)&addr, &addrlen);
	if (ret < 0) {
		perror("recvfrom()");
		return -1;
	}

	printf("%s", buffer);

	/*
	 * Subscription expired
	 */
	if (!strncmp(buffer, "EOS", 3))
		return -1;

	return 0;
}

static int max_fd(void)
{
	int max = 0;

	if (sig_fd > max)
		max = sig_fd;
	if (sock_fd > max)
		max = sock_fd;
	return max;
}

int main(int argc, char *argv[])
{
	int ret;
	struct sockaddr_in addr;
	struct hostent *hostp;
	char buffer[512] = "stats";
	const char *host = "localhost";
	int wait = 0;
	int c;
	int n = 0;
	int i;
	sigset_t mask;

	while (1) {
		int index = 0;

		c = getopt_long(argc, argv, short_options, long_options,
				&index);
		if (c == -1)
			break;

		switch (c) {
		case 'p':
			config.control_port = atoi(optarg);
			break;
		case 'H':
			host = optarg;
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
	argc -= optind - 1;
	argv += optind - 1;

	log_fd = 2;

	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGQUIT);
	sigaddset(&mask, SIGTERM);

	if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
		ERROR("sigprocmask() failed: %s", strerror(errno));
		exit(1);
	}

	sig_fd = signalfd(-1, &mask, 0);
	if (sig_fd == -1) {
		ERROR("signalfd() failed: %s", strerror(errno));
		exit(1);
	}

	sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock_fd < 0) {
		perror("socket()");
		close(sock_fd);
		exit(1);
	}

	memset(&addr, 0, sizeof(struct sockaddr_in));

	addr.sin_family = AF_INET;
	addr.sin_port = htons(config.control_port);

	hostp = gethostbyname(host);
	if (hostp == (struct hostent *)NULL) {
		fprintf(stderr, "gethostbyname(%s) failed: %d\n",
			host, h_errno);
		ret = 1;
		goto out;
	}
	memcpy(&addr.sin_addr, hostp->h_addr, sizeof(addr.sin_addr));

	for (i = 1; i < argc; i++)
		n += snprintf(buffer + n, sizeof(buffer) - n, "%s ", argv[i]);

	if (!strncmp(buffer, "sub", 3))
		wait = 1;

	printf("Sending '%s' request to '%s:%d' ...\n", buffer, host,
	       config.control_port);

	ret = sendto(sock_fd, buffer, strlen(buffer), 0,
		     (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		perror("sendto()");
		goto out;
	}

#define TIMEOUT 3

	do {
		fd_set rfds;
		struct timeval tv = { TIMEOUT, 0 };

		FD_ZERO(&rfds);
		FD_SET(sig_fd, &rfds);
		FD_SET(sock_fd, &rfds);

		ret = TEMP_FAILURE_RETRY(select(max_fd() + 1, &rfds,
						NULL, NULL, &tv));
		if (ret == -1) {
			ERROR("select() failed: %s", strerror(errno));
			goto out;
		}

		if (!ret && !wait) {
			ERROR("no data within %d seconds ?", TIMEOUT);
			goto out;
		}

		if (FD_ISSET(sig_fd, &rfds))
			ret = read_signal(sig_fd);
		
		if (FD_ISSET(sock_fd, &rfds))
			ret = read_sock(sock_fd);

		if (ret)
			goto out;
	} while (wait);

out:
	close(sig_fd);
	close(sock_fd);
	exit(ret != 0);
}
