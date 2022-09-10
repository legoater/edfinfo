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

#include "config.h"
#include "stats.h"
#include "log.h"
#include "frame.h"
#include "serial.h"

#define USEC_PER_SEC	1000000

struct stats stats = {
	.power_min = 999999,
	.min_timeout = 100 * USEC_PER_SEC,
};

void stats_update_min_timeout(struct stats *s, struct timeval *tv)
{
	useconds_t timeout = tv->tv_sec * USEC_PER_SEC + tv->tv_usec;

	if (timeout < s->min_timeout)
		s->min_timeout = timeout;
}

int stats_print(struct stats *s, char *buffer, size_t len)
{
	struct frame *top = frame_stack_top();
	int avg_one  = frame_stack_average(1 * 60);
	int avg_five = frame_stack_average(5 * 60);
	int avg_thirty = frame_stack_average(30 * 60);
	int n;

	n = snprintf(buffer, len,
		     "Frames\n"
		     "    pushed            : %ld\n"
		     "    duplicate         : %ld\n"
		     "    error             : %ld\n"
		     "    checksum errors   : %ld\n",
		     s->frame_pushed,
		     s->frame_dup,
		     s->frame_error,
		     s->badchecksum);

	n += snprintf(buffer + n, len - n,
		      "    max len           : %zd\n"
		      "    stack\n"
		      "        count         : %d\n"
		      "        max           : %d\n",
		      s->frame_maxlen,
		      s->frame_stack,
		      s->frame_stack_max);

	n += snprintf(buffer + n, len - n,
		      "MySQL\n"
		      "    pushed            : %ld\n"
		      "    errors            : %ld\n"
		      "MQTT\n"
		      "    pushed            : %ld\n"
		      "    dropped           : %ld\n"
		      "Controller\n"
		      "    requests          : %ld\n",
		      s->mysql_pushed,
		      s->mysql_error,
		      s->mqtt_pushed,
		      s->mqtt_dropped,
		      s->control_requests);

	n += snprintf(buffer + n, len - n,
		      "Serial\n"
		      "    errors            : %ld\n"
		      "    data loss         : %d secs\n"
		      "    max read bytes    : %zd\n"
		      "    timeout           : %d/%d us\n",
		      s->serial_rx_errors,
		      s->serial_data_loss,
		      s->serial_rx_bytes_max,
		      s->min_timeout, SERIAL_TIMEOUT * USEC_PER_SEC);

	n += snprintf(buffer + n, len - n,
		      "Power (Watt)\n"
		      "    current           : %d\n"
		      "    min/max           : %d/%d\n"
		      "    averages 1/5/30   : %d/%d/%d\n",
		      top ? top->power : 0,
		      s->power_min,
		      s->power_max,
		      avg_one, avg_five, avg_thirty);

	return n;
}

void stats_log(struct stats *s)
{
	char buffer[1024];
	char *line = buffer;
	char *ptr = buffer;

	stats_print(s, buffer, sizeof(buffer));

	/*
	 * syslog does not like multiline, so we parse the buffer and
	 * log each line one by one.
	 */
	while (*ptr) {
		if (*ptr == '\n') {
			*ptr = '\0';
			NOTICE(line);
			line = ptr + 1;
		}
		ptr++;
	}
}
