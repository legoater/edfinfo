/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * edfinfo - read information from electricity meter (France)
 *
 * Copyright (C) 2022, Cédric Le Goater <clg@kaod.org>
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef EDFINFO_STATS_H
#define EDFINFO_STATS_H

extern struct stats {
	unsigned int	frame_stack;
	unsigned int	frame_stack_max;
	unsigned long	frame_pushed;
	unsigned long	frame_error;
	unsigned long	frame_dup;
	size_t		frame_maxlen;
	unsigned long	badchecksum;

	unsigned long	mysql_pushed;
	unsigned long	mysql_error;
	unsigned long	mqtt_pushed;
	unsigned long	mqtt_dropped;
	unsigned long	control_requests;

	unsigned int	power_min;
	unsigned int	power_max;

	/* serial */
	useconds_t	serial_data_loss;
	unsigned long	serial_rx_errors;
	ssize_t		serial_rx_bytes_max;
	useconds_t	min_timeout;
} stats;

extern void stats_update_min_timeout(struct stats *s, struct timeval *tv);
extern int stats_print(struct stats *stats, char *buffer, size_t len);
extern void stats_log(struct stats *stats);

#endif
