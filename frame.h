/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * edfinfo - read information from electricity meter (France)
 *
 * Copyright (C) 2022, Cédric Le Goater <clg@kaod.org>
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef EDFINFO_FRAME_H
#define EDFINFO_FRAME_H

#include <time.h>

/*
 * Frame info (etiquette) indexes. These are also used as bit numbers
 * to check that a frame is valid.
 */
enum frame_info_index {
	FRAME_INFO_ADCO,
	FRAME_INFO_OPTARIF,
	FRAME_INFO_ISOUSC,
	FRAME_INFO_BASE,
	FRAME_INFO_HCHC,
	FRAME_INFO_HCHP,
	FRAME_INFO_EJPHN,
	FRAME_INFO_EJPHPM,
	FRAME_INFO_BBRHCJB,
	FRAME_INFO_BBRHPJB,

	FRAME_INFO_BBRHCJW,
	FRAME_INFO_BBRHPJW,
	FRAME_INFO_BBRHCJR,
	FRAME_INFO_BBRHPJR,
	FRAME_INFO_PEJP,
	FRAME_INFO_PTEC,
	FRAME_INFO_DEMAIN,
	FRAME_INFO_IINST1,
	FRAME_INFO_IINST2,
	FRAME_INFO_IINST3,

	FRAME_INFO_IINST,
	FRAME_INFO_IMAX1,
	FRAME_INFO_IMAX2,
	FRAME_INFO_IMAX3,
	FRAME_INFO_IMAX,
	FRAME_INFO_PMAX,
	FRAME_INFO_PAPP,
	FRAME_INFO_HHPHC,
	FRAME_INFO_MOTDETAT,
	FRAME_INFO_PPOT,

	FRAME_INFO_ADPS,

	FRAME_INFO_MAX
};

struct frame_info {
	const char *label;
	const char *value;
	enum frame_info_index index;
	unsigned char csum;
};

#define MAX_FRAME_LENGTH	512 /* should be enough for one frame */

struct frame {
	unsigned int num;
	unsigned int ninfos;
	struct frame_info *infos[FRAME_INFO_MAX];
	unsigned long infos_bitmap;
	time_t timestamp;	/* seconds is enough */
	unsigned int power;	/* Watt */
	unsigned int energy;	/* Watt x h */
	struct frame *next;
	size_t len;
	char buffer[/* len */];
};

extern void frame_destroy(struct frame *frame);
extern struct frame *frame_new(const char *buffer, size_t len);
extern void frame_log(const struct frame *frame);
extern int frame_print(const struct frame *frame, char *buffer, size_t len);
extern const char *frame_get_info(struct frame *frame, const char *label);
extern int frame_info_set_default(const char *label, const char *value);

extern struct frame *frame_stack;

#define frame_stack_top() frame_stack
extern int frame_stack_add(struct frame *frame);
extern int frame_stack_average(time_t seconds);
extern int frame_stack_clear(struct frame *frame);

extern int energy_print(char *buffer, size_t len);

#endif
