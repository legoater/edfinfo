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
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <termios.h>

#include "log.h"
#include "edfinfo.h"
#include "frame.h"
#include "serial.h"
#include "stats.h"

/*
 * serial line speed is 1200 bps, which is approximately 150 B/s,
 * close to 1 frame/s.
 */
#define SERIAL_MIN_CHAR		150 /* wake up  every 7 frames */

#define SERIAL_BUFFER_SIZE	256 /* depends on SERIAL_MIN_CHAR */

static struct termios oldtermios;

static int lograw = -1;

void serial_close(int fd)
{
	tcsetattr(fd, TCSANOW | TCSAFLUSH, &oldtermios);
	close(fd);
	if (lograw != -1)
		close(lograw);
}

int serial_open_lograw(const char *filename)
{
	lograw = open(filename, O_CREAT | O_WRONLY | O_APPEND | O_CLOEXEC, 0666);
	if (lograw < 0) {
		ERROR("open(%s): %s", filename, strerror(errno));
		return -1;
	}

	return lograw;
}

int serial_open(const char *port)
{
	int fd;
	struct termios termios;

	fd = open(port, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		ERROR("open(%s): %s", port, strerror(errno));
		return -1;
	}

	if (tcgetattr(fd, &oldtermios) == -1) {
		perror("tcgetattr");
		return -1;
	}

	memcpy(&termios, &oldtermios, sizeof(termios));

	/* raw mode */
	cfmakeraw(&termios);

	/* 1200 bps */
	if (cfsetospeed(&termios, B1200) < 0 ||
	    cfsetispeed(&termios, B1200) < 0) {
		ERROR("cannot set serial speed to 1200 bps: %s",
		      strerror(errno));
		return -1;
	}

	/* 7E1 */
	termios.c_cflag |= PARENB;
	termios.c_cflag &= ~PARODD;
	termios.c_cflag &= ~CSTOPB;
	termios.c_cflag &= ~CSIZE;
	termios.c_cflag |= CS7;

	/* No Flow Control */
	termios.c_cflag &= ~(CRTSCTS);
	termios.c_iflag &= ~(IXON | IXOFF | IXANY);
	termios.c_cflag |= CLOCAL;

	/* VMIN: wait for enough bytes to be queued in the driver
	 * before waking up read(), or select() in our case.
	 *
	 * VTIME: no interbyte timer. It will be handled by the
	 * select() timeout.
	 */
	termios.c_cc[VMIN]  = SERIAL_MIN_CHAR;
	termios.c_cc[VTIME] = 0;

	if (tcsetattr(fd, TCSANOW | TCSAFLUSH, &termios) == -1) {
		ERROR("tcsetattr() failed: %s",	strerror(errno));
		return -1;
	}

	return fd;
}

static int check_duplicate(const char *buffer, size_t len)
{
	static char prev_buffer[MAX_FRAME_LENGTH];
	static size_t prev_len;

	if (len == prev_len && memcmp(prev_buffer, buffer, len) == 0) {
		INFO("dropping duplicate frame");
		stats.frame_dup++;
		return 1;
	}
	prev_len = len;
	memcpy(prev_buffer, buffer, sizeof(prev_buffer));
	return 0;
}

#define STX 0x02 /* start frame */
#define ETX 0x03 /* end frame */
#define EOT 0x04 /* frame interrupt for out of band data */

static int read_buffer(unsigned char c,
		       void (*cb)(const char *buffer, size_t len))
{
	static char buffer[MAX_FRAME_LENGTH];
	static size_t len;
	static int fillbuffer;

	switch (c) {
	case STX:
		fillbuffer = 1;
		len = 0;
		memset(&buffer, 0, sizeof(buffer));
		break;
	case ETX:
		if (!fillbuffer)
			break;
		fillbuffer = 0;

		if (!check_duplicate(buffer, sizeof(buffer)))
			cb(buffer, len);
		break;
	case EOT:
		fillbuffer = 0;
		ERROR("received an interrupt !? dropping frame");
		break;
	default:
		if (!fillbuffer)
			break;

		if (len < sizeof(buffer)) {
			buffer[len++] = c;
		} else {
			ERROR("max buffer len reached : %d. dropping frame",
			      len);
			fillbuffer = 0;
			return -1;
		}
		break;
	}
	return 0;
}

int serial_read(int fd, void (*cb)(const char *buffer, size_t len))
{
	char buffer[SERIAL_BUFFER_SIZE];
	ssize_t n;
	int i;

	n = read(fd, buffer, sizeof(buffer));
	if (n < 0) {
		ERROR("read() failed: %s", strerror(errno));
		return -1;
	}

	if (!n) {
		WARN("nothing to read !?");
		return -1;
	}

	DEBUG("read %d bytes", n);
	if (n > stats.serial_rx_bytes_max)
		stats.serial_rx_bytes_max = n;

	for (i = 0; i < n; i++)
		read_buffer(buffer[i], cb);

	if (lograw != -1)
		if (write(lograw, buffer, n) < 0)
			ERROR("write() failed: %s", strerror(errno));

	return n;
}

