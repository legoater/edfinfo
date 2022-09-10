/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * edfinfo - read information from electricity meter (France)
 *
 * Copyright (C) 2022, Cédric Le Goater <clg@kaod.org>
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef EDFINFO_SERIAL_H
#define EDFINFO_SERIAL_H

/*
 * for select() in seconds
 */
#define SERIAL_TIMEOUT	config.serial_timeout

extern void serial_close(int fd);
extern int serial_open(const char *port);
extern int serial_read(int fd, void (*cb)(const char *buffer, size_t len));
extern int serial_open_lograw(const char *filename);

#endif
