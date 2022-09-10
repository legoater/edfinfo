/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * edfinfo - read information from electricity meter (France)
 *
 * Copyright (C) 2022, Cédric Le Goater <clg@kaod.org>
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef EDFINFO_CONFIG_H
#define EDFINFO_CONFIG_H

extern struct config {
	const char	*logfile;
	int		logpriority;
	int		daemonize;
	int		debug;

	const char	*serial_port;
	int		serial_timeout;

	int		control_port;

	/* file to record raw data */
	const char	*serial_lograw;
} config;

extern int config_init(const char *file);

#endif
