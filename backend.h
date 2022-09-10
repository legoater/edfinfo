/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * edfinfo - read information from electricity meter (France)
 *
 * Copyright (C) 2022, Cédric Le Goater <clg@kaod.org>
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef EDFINFO_BACKEND_H
#define EDFINFO_BACKEND_H

struct frame;

struct backend_ops {
	int (*configure)(const char *name, const char *value);
	int (*init)(void);
	int (*push)(const struct frame *frame);
	void (*fini)(void);
};

struct backend {
	const char *name;
	int enable;
	struct backend_ops *ops;
};

#define backend_register(bname, bops)					\
static struct backend __backend_ ## function = {			\
	.name = bname,							\
	.enable = 0,							\
	.ops = bops							\
};									\
									\
static void __attribute__((constructor)) __backend_init_ ## function(void) \
{									\
	__backend_register(&__backend_ ## function);			\
}

void __backend_register(struct backend *backend);
struct backend *backend_get(const char *name);

int backend_configure(struct backend *backend, const char *name,
		      const char *value);
int backend_init(void);
int backend_push(const struct frame *frame);
void backend_fini(void);

#endif
