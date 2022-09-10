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
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <limits.h>

#include "log.h"
#include "edfinfo.h"
#include "frame.h"
#include "stats.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static unsigned int myatoi(const char *str, unsigned int count,
			   unsigned int base)
{
	unsigned int result = 0;

	while (count--) {
		result *= base;
		if ((*str >= '0') && (*str <= '9'))
			result += *str - '0';
		else if ((*str >= 'A') && (*str <= 'F'))
			result += 10 + *str - 'A';
		else if ((*str >= 'a') && (*str <= 'f'))
			result += 10 + *str - 'a';
		str++;
	}
	return result;
}

struct edfinfo;

static int motdetat_validate(const struct edfinfo *einfo __unused,
			     const char *value)
{
	unsigned char byte[3];

	byte[0] = myatoi(&value[0], 2, 16);
	byte[1] = myatoi(&value[2], 2, 16);
	byte[2] = myatoi(&value[4], 2, 16);

	/*
	 * TODO: decode MOTDETAT bits. Depends on electricity meter.
	 */
	if ((byte[0] & 0xff) || (byte[1] & 0xff) || (byte[2] & 0xff))
		return -1;

	return 0;
}

/*
 * These labels match the mysql table definition
 */
static struct edfinfo {
	enum frame_info_index index;
	const char *label;
	size_t len;
	const char *default_value; /* set by configuration */
	int (*validate)(const struct edfinfo *einfo, const char *value);
} edfinfos[] = {
	/* Adresse du compteur */
	{ FRAME_INFO_ADCO,	"ADCO",		12,	NULL,	NULL	},
	/* Option tarifaire choisie */
	{ FRAME_INFO_OPTARIF,	"OPTARIF",	4,	NULL,	NULL	},

	/* Intensité souscrite (A) */
	{ FRAME_INFO_ISOUSC,	"ISOUSC",	2,	NULL,	NULL	},
	/* Index option Base (Wh) */
	{ FRAME_INFO_BASE,	"BASE",		9,	NULL,	NULL	},

	/* Index option Heures Creuses (Wh) */
	{ FRAME_INFO_HCHC,	"HCHC",		9,	NULL,	NULL	},
	{ FRAME_INFO_HCHP,	"HCHP",		9,	NULL,	NULL	},

	/* Index option EJP (Wh) */
	{ FRAME_INFO_EJPHN,	"EJPHN",	9,	NULL,	NULL	},
	{ FRAME_INFO_EJPHPM,	"EJPHPM",	9,	NULL,	NULL	},

	/* Index option Tempo (Wh) */
	{ FRAME_INFO_BBRHCJB,	"BBRHCJB",	9,	NULL,	NULL	},
	{ FRAME_INFO_BBRHPJB,	"BBRHPJB",	9,	NULL,	NULL	},
	{ FRAME_INFO_BBRHCJW,	"BBRHCJW",	9,	NULL,	NULL	},
	{ FRAME_INFO_BBRHPJW,	"BBRHPJW",	9,	NULL,	NULL	},
	{ FRAME_INFO_BBRHCJR,	"BBRHCJR",	9,	NULL,	NULL	},
	{ FRAME_INFO_BBRHPJR,	"BBRHPJR",	9,	NULL,	NULL	},

	/* Préavis Début EJP (30 min) */
	{ FRAME_INFO_PEJP,	"PEJP",		2,	NULL,	NULL	},
	/* Période Tarifaire en cours */
	{ FRAME_INFO_PTEC,	"PTEC",		4,	NULL,	NULL	},
	 /* Couleur du lendemain */
	{ FRAME_INFO_DEMAIN,	"DEMAIN",	4,	NULL,	NULL	},

	/* Intensité Instantanée (A) */
	{ FRAME_INFO_IINST1,	"IINST1",	3,	NULL,	NULL	},
	{ FRAME_INFO_IINST2,	"IINST2",	3,	NULL,	NULL	},
	{ FRAME_INFO_IINST3,	"IINST3",	3,	NULL,	NULL	},
	/* mono phase */
	{ FRAME_INFO_IINST,	"IINST",	3,	NULL,	NULL	},

	/* Intensité maximale (A) */
	{ FRAME_INFO_IMAX1,	"IMAX1",	3,	NULL,	NULL	},
	{ FRAME_INFO_IMAX2,	"IMAX2",	3,	NULL,	NULL	},
	{ FRAME_INFO_IMAX3,	"IMAX3",	3,	NULL,	NULL	},
	/* mono phase */
	{ FRAME_INFO_IMAX,	"IMAX",		3,	NULL,	NULL	},

	/* Puissance maximale triphasée atteinte (W)  */
	{ FRAME_INFO_PMAX,	"PMAX",		5,	NULL,	NULL	},
	/* Puissance apparente (VA) */
	{ FRAME_INFO_PAPP,	"PAPP",		5,	NULL,	NULL	},
	/* Horaire Heures Pleines Heures Creuses */
	{ FRAME_INFO_HHPHC,	"HHPHC",	1,	NULL,	NULL	},

	/* Mot d'état compteur */
	{ FRAME_INFO_MOTDETAT,	"MOTDETAT",	6,	NULL,
	  motdetat_validate },

	 /* Présence des potentiels */
	{ FRAME_INFO_PPOT,	"PPOT",		2,	NULL,	NULL	},

	/* Avertissement de Dépassement De Puissance Souscrite */
	{ FRAME_INFO_ADPS,	"ADPS",		3,	NULL,	NULL	},

	{ FRAME_INFO_MAX,	NULL,		0,	NULL,	NULL	},
};

static struct edfinfo *find_einfo(const char *label)
{
	struct edfinfo *einfo = edfinfos;

	while (einfo->label) {
		if (!strcmp(label, einfo->label))
			return einfo;
		einfo++;
	}

	return NULL;
}

int frame_info_set_default(const char *label, const char *value)
{
	struct edfinfo *ei;

	ei = find_einfo(label);
	if (!ei) {
		WARN("unknown info label: '%s'", label);
		return -1;
	}

	ei->default_value = strdup(value);
	return 0;
}

void frame_log(const struct frame *frame)
{
	unsigned int i;

	INFO("frame %08d size:%ld bitmap:%08x", frame->num, frame->len,
	     frame->infos_bitmap);
	for (i = 0; i < frame->ninfos; i++)
		DEBUG("\t%s: '%s'", frame->infos[i]->label,
		      frame->infos[i]->value);
}

int frame_print(const struct frame *frame, char *buffer, size_t len)
{
	unsigned int i;
	int n = 0;

	for (i = 0; i < frame->ninfos; i++)
		n += snprintf(buffer + n, len - n, "%s:%s\n",
			      frame->infos[i]->label, frame->infos[i]->value);
	return n;
}

const char *frame_get_info(struct frame *frame, const char *label)
{
	unsigned int i;

	for (i = 0; i < frame->ninfos; i++)
		if (!strcmp(label, frame->infos[i]->label))
			return frame->infos[i]->value;
	return NULL;
}

/*
 * Une trame est constituée de 3 parties :
 *   - le caractère de début de trame "Start Text" STX (02h)
 *   - le corps de la trame, composé d'un ou de plusieurs groupes
 *     d'information
 *   - le caractère de fin de trame "End Text" ETX ( 03h)
 *
 * Une trame peut être interrompue, auquel cas le caractère "End Of
 * Text" EOT (04h) est transmis avant l'interruption.
 *
 * Chaque groupe d'information forme un ensemble cohérent avec une
 * étiquette et une valeur associée. La composition d'un groupe
 * d'information est la suivante :
 *
 *   - le caractère de début de groupe "Line Feed" LF (0Ah)
 *   - le champ étiquette dont la longueur est comprise entre 4 et 8
 *     caractères
 *   - un séparateur "Space" SP (20h)
 *   - le champ données dont la longueur est comprise entre 1 et 12
 *     caractères
 *   - un séparateur "Space" SP (20h)
 *   - un champ de contrôle (checksum), composé d'un caractère
 *   - le caractère de fin de groupe "Carriage Return" CR (0Ch)
 *
 * Le checksum est calculé sur l'ensemble des caractères allant du
 * champ étiquette à la fin du champ données, caractère SP inclus. On
 * fait tout d'abord la somme des codes ASCII de tous ces
 * caractères. Pour éviter d'introduire des caractères ASCII pouvant
 * être non imprimables, on ne conserve que les six bits de poids
 * faible du résultat obtenu. Enfin, on ajoute 20h.
 */
static unsigned char calc_checksum(const char *buffer)
{
	unsigned int csum = 0;

	while (*buffer)
		csum += *buffer++;
	return (csum & 0x3f) + 0x20;
}

/*
 */
static const struct edfinfo *frame_info_validate(const char *label,
						 const char *value)
{
	const struct edfinfo *ei;
	size_t len;

	ei = find_einfo(label);
	if (!ei) {
		ERROR("unknown info label: '%s'", label);
		return NULL;
	}

	/* and check length, default value and format if possible */
	len = strlen(value);
	if (len != ei->len) {
		ERROR("info[%s] has an invalid length value: %ld", label, len);
		return NULL;
	}

	if (ei->default_value && strcmp(ei->default_value, value)) {
		ERROR("info[%s] has an invalid default value : %s", label,
		      value);
		return NULL;
	}

	if (ei->validate && ei->validate(ei, value)) {
		ERROR("info[%s] is bogus : %s", label, value);
		return NULL;
	}

	return ei;
}

static int frame_info_parse(char *buffer, char **tokens, int ntokens)
{
	const char delims[] = " |";
	int i;
	char *saveptr;

	for (i = 0; i < ntokens; i++, buffer = NULL) {
		tokens[i] = strtok_r(buffer, delims, &saveptr);
		if (!tokens[i])
			break;
	}

	return i;
}

/* Format d'un groupe d'information attendu :
 *
 *   LF
 *       LABEL[4..8] SPACE DATA[1..12] SPACE CSUM
 *   CR
 */
static struct frame_info *frame_info_new(char *buffer, size_t len)
{
	unsigned char csum;
	char *tokens[3];
	const struct edfinfo *ei;
	struct frame_info *frame_info;

	DEBUG("frame info: '%s' #%d", buffer, len);

	csum = buffer[len - 1];
	buffer[len - 2] = '\0';

	if (csum != calc_checksum(buffer)) {
		stats.badchecksum++;
		ERROR("frame info has an invalid checksum: '%s'", buffer);
		return NULL;
	}

	/* extract label and value */
	if (frame_info_parse(buffer, tokens, ARRAY_SIZE(tokens)) < 2) {
		ERROR("frame info has invalid format: '%s'", buffer);
		return NULL;
	}

	ei = frame_info_validate(tokens[0], tokens[1]);
	if (!ei)
		return NULL;

	frame_info = calloc(1, sizeof(*frame_info));
	if (!frame_info) {
		ERROR("could not allocate frame_info : %s", strerror(errno));
		return NULL;
	}
	frame_info->label = tokens[0];
	frame_info->value = tokens[1];
	frame_info->index = ei->index;
	frame_info->csum = csum;
	return frame_info;
}

#define BIT(nr)                 (1UL << (nr))

static void frame_info_add(struct frame *frame, struct frame_info *finfo)
{
	unsigned int i = 0;

	/* Check for duplicate frame infos (never occurred in real
	 * life)
	 */
	for (i = 0; i < frame->ninfos; i++)
		if (frame->infos[i]->index == finfo->index)
			break;

	if (frame->infos[i]) {
		WARN("replacing frame info '%s'", finfo->label);
		free(frame->infos[i]);
	}

	frame->infos[i] = finfo;

	/* if this is not a replacement, increase number of allocated
	 * frame infos for next round
	 */
	if (i == frame->ninfos)
		frame->ninfos++;

	/* update bitmap of collected frame infos. For the moment, we
	 * are lucky enough to have indexes with values < 31 ...
	 */
	frame->infos_bitmap |= BIT(finfo->index);

	/* keep some infos for later use. calculations depends on
	 * it.
	 */
	if (finfo->index == FRAME_INFO_PAPP)
		frame->power = atoi(finfo->value);
	if (finfo->index == FRAME_INFO_BASE)
		frame->energy = atoi(finfo->value);
}

void frame_destroy(struct frame *frame)
{
	unsigned int i;

	if (!frame)
		return;

	for (i = 0; i < frame->ninfos; i++)
		free(frame->infos[i]);

	free(frame);
}

/*
 * Set of frame infos that should be present in a frame
 */
#define FRAME_INFO_COMMON_MASK (					\
		BIT(FRAME_INFO_ADCO)   | BIT(FRAME_INFO_OPTARIF) |	\
		BIT(FRAME_INFO_ISOUSC) | BIT(FRAME_INFO_BASE)	 |	\
		BIT(FRAME_INFO_PTEC)   | BIT(FRAME_INFO_PAPP)	 |	\
		BIT(FRAME_INFO_MOTDETAT))

#define FRAME_INFO_MONO_MASK (						\
		FRAME_INFO_COMMON_MASK | BIT(FRAME_INFO_IINST)	 |	\
		BIT(FRAME_INFO_IMAX))

#define FRAME_INFO_TRI_MASK (						\
		FRAME_INFO_COMMON_MASK | BIT(FRAME_INFO_IINST1)	 |	\
		BIT(FRAME_INFO_IINST2) | BIT(FRAME_INFO_IINST3)	 |	\
		BIT(FRAME_INFO_IMAX1)  | BIT(FRAME_INFO_IMAX2)	 |	\
		BIT(FRAME_INFO_IMAX3))

/*
 * validate the frame by checking that enough frame infos have been
 * collected. Should use a config option ?
 */
static int frame_validate(struct frame *frame)
{
	unsigned long bitmask = FRAME_INFO_MONO_MASK;

	if ((frame->infos_bitmap & bitmask) != bitmask) {
		ERROR("invalid frame info bitmap : %08x", frame->infos_bitmap);
		return -1;
	}

	return 0;
}

struct frame *frame_new(const char *buffer, size_t len)
{
	static int frame_num;

	struct frame *frame;
	unsigned int i;
	unsigned int start_info = 0;

	if (len > MAX_FRAME_LENGTH) {
		ERROR("frame buffer is too large: %d", len);
		return NULL;
	}

	if (buffer[0] != '\n' || buffer[len - 1] != '\r') {
		ERROR("frame buffer is not correctly formatted");
		return NULL;
	}

	frame = calloc(1, sizeof(*frame) + len);
	if (!frame) {
		ERROR("could not allocate frame : %s", strerror(errno));
		return NULL;
	}

	memcpy(frame->buffer, buffer, len);
	frame->len = len;

	/* now loop on the frame buffer and try to decode and collect
	 * frame infos
	 */
	for (i = 0; i < len; i++) {
		struct frame_info *finfo;

		if (frame->ninfos == FRAME_INFO_MAX) {
			WARN("max frame info reached. dropping %d bytes",
			     len - i);
			break;
		}

		switch (frame->buffer[i]) {
		case '\n':
			start_info = i + 1;
			break;
		case '\r':
			frame->buffer[i] = '\0';

			finfo = frame_info_new(&frame->buffer[start_info],
					       i - start_info);
			if (!finfo) {
				frame_destroy(frame);
				return NULL;
			}

			frame_info_add(frame, finfo);
			break;

		default:
			break;
		}
	}

	if (frame_validate(frame)) {
		frame_destroy(frame);
		return NULL;
	}

	/*
	 * timestamp will be udpated if frame is added to the stack
	 */
	frame->timestamp = 0;
	frame->num = frame_num++;
	return frame;
}

static int energy_days[7];
static int energy_hours[24];

static inline int mod(int a, int b)
{
	int ret = a % b;

	return (ret < 0) ? ret + b : ret;
}

static void energy_update(struct frame *frame)
{
	struct tm tm;

	localtime_r(&frame->timestamp, &tm);

	energy_days[mod(tm.tm_wday, ARRAY_SIZE(energy_days))] =
		    frame->energy;
	energy_hours[mod(tm.tm_hour, ARRAY_SIZE(energy_hours))] =
		     frame->energy;
}

static double energy_deltas(const int *array, size_t size, double *deltas)
{
	unsigned int i;
	double total = 0;

	for (i = 0; i < size; i++) {
		unsigned int current = array[i];
		unsigned int    prev = array[mod(i - 1, size)];

		if (prev && current > prev) {
			deltas[i] = (double)(current - prev) / 1000;
			total += deltas[i];
		} else {
			deltas[i] = 0;
		}
	}
	return total;
}

int energy_print(char *buffer, size_t len)
{
	unsigned int i;
	int n = 0;
	double total = 0;
	double deltas[ARRAY_SIZE(energy_hours)];

	total = energy_deltas(energy_days, ARRAY_SIZE(energy_days), deltas);
	n += snprintf(buffer + n, len - n, "days [%06.3f]: ", total);
	for (i = 0; i < ARRAY_SIZE(energy_days); i++)
		n += snprintf(buffer + n, len - n, "%06.3f ", deltas[i]);
	n += snprintf(buffer + n, len - n, "\n");

	total = energy_deltas(energy_hours, ARRAY_SIZE(energy_hours),
			      deltas);
	n += snprintf(buffer + n, len - n, "hours[%06.3f]: ", total);
	for (i = 0; i < ARRAY_SIZE(energy_hours); i++)
		n += snprintf(buffer + n, len - n, "%06.3f ", deltas[i]);
	n += snprintf(buffer + n, len - n, "\n");

	return n;
}

#define FRAME_STACK_DEPTH (60 * 60) /* seconds */

struct frame *frame_stack;

int frame_stack_clear(struct frame *frame)
{
	unsigned int count = 0;

	if (!frame)
		return 0;

	if (frame == frame_stack)
		frame_stack = NULL;

	while (frame) {
		struct frame *frame_next = frame->next;

		frame_destroy(frame);
		frame = frame_next;
		count++;
	}

	INFO("cleared %d frames", count);
	return count;
}

static struct power_average {
	time_t window;
	time_t elapsed;
	unsigned int power;
	unsigned int nframes;
} averages[] = {
	{ .window =  1 * 60 },
	{ .window =  5 * 60 },
	{ .window = 30 * 60 }
};

static void power_average_init(struct frame *frame)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(averages); i++) {
		averages[i].power = frame->power;
		averages[i].elapsed = 1;
		averages[i].nframes = 1;
	}
}

/*
 * Accumulate power mesures and extrapolate values in missing time
 * ranges
 */
static void power_average_update(unsigned int power, time_t delta)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(averages); i++) {
		time_t d = delta;

		if (d + averages[i].elapsed > averages[i].window)
			d = averages[i].window - averages[i].elapsed;

		if (averages[i].elapsed >= averages[i].window)
			continue;

		averages[i].power += power * d;
		averages[i].elapsed += d;
		averages[i].nframes++;
	}
}

int frame_stack_add(struct frame *frame)
{
	struct timeval now;
	unsigned int count = 0;
	struct frame *frame_prev = NULL;

	gettimeofday(&now, NULL);
	frame->timestamp = now.tv_sec;

	frame->next = frame_stack;
	frame_stack = frame;

	power_average_init(frame);
	energy_update(frame);

	/* check for aging frames */
	while (frame) {
		if (frame_prev)
			power_average_update(frame->power,
					     frame_prev->timestamp - frame->timestamp);

		if (frame_stack->timestamp - frame->timestamp >
		    FRAME_STACK_DEPTH)
			break;
		count++;
		frame_prev = frame;
		frame = frame->next;
	}

	frame_stack_clear(frame);
	if (frame_prev)
		frame_prev->next = NULL;
	return count;
}

int frame_stack_average(time_t window)
{
	unsigned int i;
	struct power_average *avg = NULL;
	int result;

	for (i = 0; i < ARRAY_SIZE(averages); i++) {
		if (averages[i].window == window)
			avg = &averages[i];
	}

	if (!avg)
		return -1;

	result = avg->elapsed ? avg->power / avg->elapsed : 0;
	DEBUG("%d seconds average: %d Watts (%d frames)", avg->window,
	      result, avg->nframes);
	return result;
}
