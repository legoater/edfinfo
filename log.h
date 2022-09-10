/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * edfinfo - read information from electricity meter (France)
 *
 * Copyright (C) 2022, Cédric Le Goater <clg@kaod.org>
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef EDFINFO_LOG_H
#define EDFINFO_LOG_H

#include <syslog.h>

#include "config.h"

#define ERROR(format, ...) do {				\
	if (config.logpriority >= LOG_ERR)		\
		__log(LOG_ERR, format, ##__VA_ARGS__);	\
	} while (0)

#define WARN(format, ...) do {					\
	if (config.logpriority >= LOG_WARNING)			\
		__log(LOG_WARNING, format, ##__VA_ARGS__);	\
	} while (0)

#define NOTICE(format, ...) do {				\
	if (config.logpriority >= LOG_NOTICE)			\
		__log(LOG_NOTICE, format, ##__VA_ARGS__);	\
	} while (0)

#define INFO(format, ...) do {				\
	if (config.logpriority >= LOG_INFO)		\
		__log(LOG_INFO, format, ##__VA_ARGS__);	\
	} while (0)

#define DEBUG(format, ...) do {					\
	if (config.logpriority >= LOG_DEBUG)			\
		__log(LOG_DEBUG, format, ##__VA_ARGS__);	\
	} while (0)

extern int log_fd;

extern void __log(int priority, const  char *format, ...);
extern int log_open(const char *name);
extern int log_name_to_priority(const char *name);
extern const char *log_priority_to_name(int priority);

#endif
