// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * edfinfo - read information from electricity meter (France)
 *
 * Copyright (C) 2022, Cédric Le Goater <clg@kaod.org>
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include <string.h>
#include <stdlib.h>

#include "backend.h"

#define BACKEND_MAX 10

static struct backend *backends[BACKEND_MAX];

static unsigned int backend_count;

void __backend_register(struct backend *backend)
{
	if (backend_count < BACKEND_MAX)
		backends[backend_count++] = backend;
}

struct backend *backend_get(const char *name)
{
	unsigned int i;

	for (i = 0; i < backend_count; i++) {
		if (!strcmp(backends[i]->name, name))
			return backends[i];
	}
	return NULL;
}

int backend_push(const struct frame *frame)
{
	unsigned int i;
	int ret = 0;

	for (i = 0; i < backend_count; i++) {
		if (backends[i]->enable && backends[i]->ops->push)
			ret |= backends[i]->ops->push(frame);
	}
	return ret;
}

int backend_init(void)
{
	unsigned int i;
	int ret = 0;

	for (i = 0; i < backend_count; i++) {
		if (backends[i]->enable && backends[i]->ops->init)
			ret |= backends[i]->ops->init();
	}
	return ret;
}

void backend_fini(void)
{
	unsigned int i;

	for (i = 0; i < backend_count; i++) {
		if (backends[i]->enable && backends[i]->ops->fini)
			backends[i]->ops->fini();
	}
}

#define MATCH(n) (strcmp(name, n) == 0)

int backend_configure(struct backend *b, const char *name, const char *value)
{
	if (MATCH("enable")) {
		b->enable = atoi(value);
		return 1;
	}

	if (b->ops->configure)
		return b->ops->configure(name, value);

	/* fail if no configure ? */
	return 0;
}
