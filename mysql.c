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
#include <unistd.h>
#include <sys/time.h>
#include <mysql.h>

#include "log.h"
#include "edfinfo.h"
#include "frame.h"
#include "backend.h"
#include "stats.h"

static struct mysql_config {
	const char	*host;
	const char	*db;
	const char	*table;
	const char	*user;
	const char	*password;
	int             ratelimit;
} mysql_config = {
	.host		= "localhost",
	.db		= "home",
	.table		= "edfinfo",
	.user		= "edfinfo",
	.password	= "edfinfo",
	.ratelimit	= 900,
};

#define MATCH(n) (strcmp(name, n) == 0)

static int mysql_configure(const char *name, const char *value)
{
	if (MATCH("host")) {
		mysql_config.host = strdup(value);
	} else if (MATCH("db")) {
		mysql_config.db = strdup(value);
	} else if (MATCH("table")) {
		mysql_config.table = strdup(value);
	} else if (MATCH("user")) {
		mysql_config.user = strdup(value);
	} else if (MATCH("password")) {
		mysql_config.password = strdup(value);
	} else if (MATCH("ratelimit")) {
		mysql_config.ratelimit = atoi(value);
	} else {
		fprintf(stderr, "unknown config name mysql/%s\n", name);
		return 0;  /* unknown section/name, error */
	}

	/* success */
	return 1;
}

static MYSQL my;
static int mysql_retries;

static int mysql_myinit(void)
{
	my_bool my_true = 0;

	if (!mysql_init(&my)) {
		ERROR("MySQL init failed !");
		return -1;
	}

	if (mysql_options(&my, MYSQL_OPT_RECONNECT, &my_true)) {
		ERROR("MySQL options failed: %s ", mysql_error(&my));
		return -1;
	}

	if (!mysql_real_connect(&my, mysql_config.host, mysql_config.user,
				mysql_config.password, mysql_config.db, 0, NULL, 0)) {
		if (!mysql_retries)
			ERROR("MySQL connect failed: %s ", mysql_error(&my));
		mysql_retries++;
		goto out;
	}

	mysql_retries = 0;
	NOTICE("MySQL: connected to server %s", mysql_config.host);
out:
	/*
	 * return success whatever happens
	 */
	return 0;
}

static void mysql_myfini(void)
{
	mysql_close(&my);
}

static int mysql_check(void)
{
	if (!mysql_ping(&my))
		return 0;

	if (!mysql_retries)
		ERROR("MySQL ping failed : %s", mysql_error(&my));

	mysql_myfini();
	mysql_myinit();

	return mysql_retries;
}

/* this conversion is needed to match the mysql table definition which
 * also supports "triphasé".
 *
 *	IINST	-> IINST1
 *	IMAX	-> IMAX1
 */
static inline const char *get_triphase_suffix(const char *label)
{
	return (!strcmp(label, "IINST") || !strcmp(label, "IMAX")) ? "1" : "";
}

static int build_mysql_query(const struct frame *frame, char *query, size_t len)
{
	unsigned int i;
	int n;

	n = snprintf(query, len, "INSERT INTO %s (DATE", mysql_config.table);
	for (i = 0; i < frame->ninfos; i++) {
		n += snprintf(query + n, len - n, ",%s%s",
			      frame->infos[i]->label,
			      get_triphase_suffix(frame->infos[i]->label));
	}

	n += snprintf(query + n, len - n, ") VALUES (NOW()");
	for (i = 0; i < frame->ninfos; i++) {
		n += snprintf(query + n, len - n, ",'%s'",
			      frame->infos[i]->value);
	}

	n += snprintf(query + n, len - n, ");");
	return n;
}

static int check_ratelimit(int ratelimit)
{
	static struct timeval prev;
	struct timeval now;

	gettimeofday(&now, NULL);
	if (now.tv_sec - prev.tv_sec < ratelimit)
		return 0;

	prev = now;
	return 1;
}

static int mysql_push(const struct frame *frame)
{
	char query[512];
	unsigned int ret = 0;

	if (!check_ratelimit(mysql_config.ratelimit))
		return ret;

	/* something went wrong last time a push was done. try to
	 * reconnect
	 */
	if (mysql_check())
		return 0;

	ret = build_mysql_query(frame, query, sizeof(query));
	if (ret >= sizeof(query)) {
		ERROR("MySQL query buffer is too small : %d bytes needed", ret);
		return -1;
	}

	INFO("MySQL query #%d: \"%s\"", ret, query);

	ret = mysql_query(&my, query);
	if (ret) {
		ERROR("MySQL query failed : %s", mysql_error(&my));
		stats.mysql_error++;
		mysql_myfini();
	} else {
		stats.mysql_pushed++;
	}

	return ret;
}

static struct backend_ops mysql_ops = {
	.configure = mysql_configure,
	.init = mysql_myinit,
	.push = mysql_push,
	.fini = mysql_myfini
};

backend_register("mysql", &mysql_ops)
