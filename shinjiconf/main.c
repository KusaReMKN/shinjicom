#
/*-
 * SPDX-License-Identifier: BSD 2-Clause License
 *
 * Copyright (c) 2026, KusaReMKN
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#define BUFLEN	256

static void usage(void);

int
main(int argc, char *argv[])
{
	struct termios term;
	size_t i, len;
	ssize_t nread;
	int c, cflag, fd;
	char buf[BUFLEN];

	cflag = 0;
	while ((c = getopt(argc, argv, "c")) != -1)
		switch (c) {
		case 'c':
			cflag = 1;
			break;
		case '?':
		default:
			usage();
			/* NOTREACHED */
		}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	fd = open(argv[0], O_RDWR | O_NOCTTY);
	if (fd == -1)
		err(EXIT_FAILURE, "%s", argv[0]);

	if (tcgetattr(fd, &term) == -1)
		err(EXIT_FAILURE, "tcgetattr");
	cfmakeraw(&term);
	term.c_cc[VMIN] = 0;
	term.c_cc[VTIME] = 3;
	if (tcsetattr(fd, TCSANOW, &term) == -1)	/* XXX */
		err(EXIT_FAILURE, "tcgetattr");

	len = 0;
	while ((nread = read(STDIN_FILENO, buf+len, sizeof(buf)-len-1)) > 0) {
		len += nread;
		if (len >= BUFLEN-1)
			break;
	}
	if (nread == -1)
		err(EXIT_FAILURE, "stdin");

	if (cflag) {
		buf[len] = 0;
		for (i = 0; i < len; i++)
			buf[len] ^= buf[i];
		len++;
	}

	(void)fprintf(stderr, "to LoRa");
	for (i = 0; i < len; i++) {
		if ((i & 0x0F) == 0)
			(void)fprintf(stderr, "\n%#04zx:\t", i);
		(void)fprintf(stderr, "%02x ", (unsigned)buf[i] & 0xFF);
	}
	(void)fprintf(stderr, "\n%#04zx (%zd)\n", len, len);

	if (write(fd, buf, len) == -1)
		err(EXIT_FAILURE, "%s", argv[0]);

	len = 0;
	while ((nread = read(fd, buf+len, sizeof(buf)-len-1)) > 0) {
		len += nread;
		if (len >= BUFLEN-1)
			break;
	}
	if (nread == -1)
		err(EXIT_FAILURE, "%s", argv[0]);

	(void)fprintf(stderr, "from LoRa");
	for (i = 0; i < len; i++) {
		if ((i & 0x0F) == 0)
			(void)fprintf(stderr, "\n%#04zx:\t", i);
		(void)fprintf(stderr, "%02x ", (unsigned)buf[i] & 0xFF);
	}
	(void)fprintf(stderr, "\n%#04zx (%zd)\n", len, len);

	(void)close(fd);

	return 0;
}

static void
usage(void)
{
	fprintf(stderr, "usage: shinjiconf [-c] <LoRa>\n");
	exit(EXIT_FAILURE);
}
