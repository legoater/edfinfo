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
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

#include "log.h"
#include "edfinfo.h"
#include "frame.h"
#include "stats.h"
#include "control.h"

int control_fd = -1;

struct command;

typedef	int (*command_handler)(char *buffer, size_t n, void *data);

struct command {
	const char *key;
	command_handler handler;
	const char *help;
};

static int handle_help(char *buffer, size_t len, void *data);
static int handle_last(char *buffer, size_t len, void *data);
static int handle_stats(char *buffer, size_t len, void *data);
static int handle_average(char *buffer, size_t len, void *data);
static int handle_energy(char *buffer, size_t len, void *data);
static int handle_priority(char *buffer, size_t len, void *data);

static const struct command commands[] = {
	{ "help",	handle_help,	 "this message"			},
	{ "last",	handle_last,	 "last frame received"		},
	{ "stats",	handle_stats,	 "current statistics"		},
	{ "average",	handle_average,
	  "power averages over the last 1/5/30 minutes"			},
	{ "energy",	handle_energy,
	  "energy consumption over the last days and hours"		},
	{ "priority",	handle_priority, "change the logging priority"  },

	{ NULL,		NULL,		 NULL }
};

static const struct command *command_get(const char *str)
{
	const struct command *c = commands;

	while (c->key) {
		if (!strncmp(c->key, str, strlen(c->key)))
			return c;
		c++;
	}
	return NULL;
}

static int handle_help(char *buffer, size_t len, void *data __unused)
{
	int n = 0;
	const struct command *cmd = commands;

	while (cmd->key) {
		n += snprintf(buffer + n, len - n, "%-12s %s\n",
			      cmd->key, cmd->help);
		cmd++;
	}
	return n;
}

static int handle_last(char *buffer, size_t len, void *data __unused)
{
	struct frame *top = frame_stack_top();

	if (top)
		return frame_print(top, buffer, len);
	else
		return snprintf(buffer, len, "no last frame\n");
}

static int handle_stats(char *buffer, size_t len, void *data __unused)
{
	return stats_print(&stats, buffer, len);
}

static int handle_average(char *buffer, size_t len, void *data __unused)
{
	int avg_one    = frame_stack_average(1 * 60);
	int avg_five   = frame_stack_average(5 * 60);
	int avg_thirty = frame_stack_average(30 * 60);

	return snprintf(buffer, len, "averages 1/5/30: %d/%d/%d\n",
			avg_one, avg_five, avg_thirty);
}

static int handle_energy(char *buffer, size_t len, void *data __unused)
{
	return energy_print(buffer, len);
}

static int handle_priority(char *buffer, size_t len, void *data __unused)
{
	char *newpriority;

	if (sscanf(buffer, "priority %m[a-z]", &newpriority) != 1)
		return -1;

	config.logpriority = log_name_to_priority(newpriority);
	free(newpriority);
	return snprintf(buffer, len, "New priority is : %s\n",
			log_priority_to_name(config.logpriority));
}

int control_open(void)
{
	int sd;
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);

	sd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sd < 0) {
		ERROR("socket() failed: %s", strerror(errno));
		return -1;
	}

	memset(&addr, 0, addrlen);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(config.control_port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(sd, (struct sockaddr *)&addr, addrlen) < 0) {
		ERROR("bind() failed : %s", strerror(errno));
		close(sd);
		return -1;
	}

	control_fd = sd;
	NOTICE("listening on UDP port ':%d'", config.control_port);
	return sd;
}

int control_read(int sd)
{
	int ret;
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);
	char buffer[1024];
	int n = 0;
	const struct command *cmd = commands;

	memset(buffer, 0, sizeof(buffer));

	ret = recvfrom(sd, &buffer, sizeof(buffer), 0,
		       (struct sockaddr *)&addr, &addrlen);
	if (ret < 0) {
		ERROR("recvfrom() failed: %s", strerror(errno));
		return -1;
	}

	INFO("received %d bytes from %s:%d", ret, inet_ntoa(addr.sin_addr),
	     ntohs(addr.sin_port));

	cmd = command_get(buffer);
	if (!cmd)
		n = snprintf(buffer, sizeof(buffer), "unknown command\n");
	else
		n = cmd->handler(buffer, sizeof(buffer), &addr);

	if (n == -1)
		n = snprintf(buffer, sizeof(buffer), "command '%s' failed\n",
			     cmd->key);

	ret = sendto(sd, buffer, n, 0, (struct sockaddr *)&addr, addrlen);
	if (ret < 0) {
		ERROR("sendto(%s) failed: %s", inet_ntoa(addr.sin_addr),
		      strerror(errno));
		return -1;
	}

	return 0;
}

int control_close(int sd)
{
	control_fd = -1;
	return close(sd);
}
