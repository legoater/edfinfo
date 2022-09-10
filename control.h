/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * edfinfo - read information from electricity meter (France)
 *
 * Copyright (C) 2022, Cédric Le Goater <clg@kaod.org>
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef EDFINFO_CTL_H
#define EDFINFO_CTL_H

extern int control_fd;

int control_open(void);
int control_read(int sd);
int control_close(int sd);

#endif
