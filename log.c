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
#define SYSLOG_NAMES
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include <asm/unistd.h>

#include "log.h"
#include "edfinfo.h"

int log_fd = -1;

const char *log_priority_to_name(int priority)
{
	switch (priority) {
	case LOG_EMERG:		return "emerg";
	case LOG_ALERT:		return "alert";
	case LOG_CRIT:		return "crit";
	case LOG_ERR:		return "error";
	case LOG_WARNING:	return "warning";
	case LOG_NOTICE:	return "notice";
	case LOG_INFO:		return "info";
	case LOG_DEBUG:		return "debug";
	default:
				return "unknown";
	}
}

int log_name_to_priority(const char *name)
{
	CODE *c = prioritynames;

	while (c->c_name) {
		if (!strcmp(c->c_name, name))
			break;
		c++;
	}

	return c->c_val != -1 ? c->c_val : LOG_WARNING;
}

void __log(int priority, const  char *format, ...)
{
	char buffer[512];
	va_list args;
	size_t n;

	if (log_fd == -1)  {
		if (config.daemonize) {
			openlog(progname, LOG_PID | LOG_CONS, LOG_USER);
			va_start(args, format);
			vsyslog(priority, format, args);
			va_end(args);
			closelog();
		}
		return;
	}

	n = snprintf(buffer, sizeof(buffer), "%s %d: %s - ", progname,
		     getpid(), log_priority_to_name(priority));

	va_start(args, format);
	n += vsnprintf(buffer + n, sizeof(buffer) - n, format, args);
	va_end(args);

	if (n >= sizeof(buffer) - 1) {
		WARN("truncated next event from %d to %zd bytes", n,
		     sizeof(buffer));
		n = sizeof(buffer) - 1;
	}

	buffer[n] = '\n';
	write(log_fd, buffer, n + 1);
}

int log_open(const char *filename)
{
	int fd;
	int newfd;

	if (!filename || *filename == '\0')
		return -1;

	fd = open(filename, O_CREAT | O_WRONLY | O_APPEND | O_CLOEXEC, 0666);
	if (fd == -1) {
		fprintf(stderr, "failed to open log file \"%s\" : %s\n",
			filename, strerror(errno));
		return -1;
	}

	if (fd > 2)
		return fd;

	newfd = fcntl(fd, F_DUPFD_CLOEXEC, 3);
	if (newfd == -1)
		fprintf(stderr, "failed to dup log fd %d : %s\n", fd,
			strerror(errno));

	close(fd);
	return newfd;
}
