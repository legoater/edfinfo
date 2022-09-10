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
#include <string.h>
#include <errno.h>
#include <ini.h>

#include "log.h"
#include "config.h"
#include "frame.h"
#include "backend.h"

struct config config	= {
	.logfile	= "",
	.logpriority	= LOG_NOTICE,
	.daemonize	= 0,
	.debug		= 0,

	.serial_port	= "/dev/ttyAMA0",
	.serial_timeout	= 3,
	.serial_lograw	= NULL,

	.control_port   = 54345
};

#define MATCH_SECTION(s) (strcmp(section, s) == 0)
#define MATCH(s, n) (strcmp(section, s) == 0 && strcmp(name, n) == 0)

static int handler(void *user, const char *section, const char *name,
		   const char *value)
{
	struct backend *backend;
	struct config *pconfig = (struct config *)user;

	if (MATCH("", "logfile")) {
		pconfig->logfile = strdup(value);
	} else if (MATCH("", "logpriority")) {
		pconfig->logpriority = log_name_to_priority(value);
	} else if (MATCH("", "daemonize")) {
		pconfig->daemonize = atoi(value);

	} else if (MATCH("serial", "port")) {
		pconfig->serial_port = strdup(value);
	} else if (MATCH("serial", "timeout")) {
		pconfig->serial_timeout = atoi(value);
	} else if (MATCH("serial", "lograw")) {
		pconfig->serial_lograw = strdup(value);

	} else if (MATCH("control", "port")) {
		pconfig->control_port = atoi(value);

	/* default frame info values */
	} else if (MATCH_SECTION("edfinfo")) {
		return frame_info_set_default(name, value) == 0;

	/* backend configuration */
	} else {
		backend = backend_get(section);
		if (backend)
			return backend_configure(backend, name, value);

		fprintf(stderr, "unknown section/name, %s/%s\n",
			section, name);
		return 0;  /* unknown section/name, error */
	}

	/* success */
	return 1;
}

int config_init(const char *filename)
{
	FILE *file;
	int error;

	if (!filename)
		filename = getenv("EDFINFO_CONF") ?
			getenv("EDFINFO_CONF") : EDFINFO_CONF;

	file = fopen(filename, "r");
	if (!file) {
		ERROR("failed to open config file '%s' : %s",
		      filename, strerror(errno));
		return -1;
	}

	error = ini_parse_file(file, handler, &config);
	if (error)
		ERROR("bogus configuration line at %s[%d]",
		      filename, error);

	fclose(file);

	/*
	 * valgrind: config values are not freed
	 */
	NOTICE("Config loaded from '%s'\n", filename);
	return error ? -1 : 0;
}
