/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2002 Ximian, Inc. (www.ximian.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */

/*
 * For the hell of it, I built gmime's uuencode with gcc -O2 on my
 * redhat 7.2 box and compared it with the system's uuencode:
 *
 * file: gmime/tests/large.eml
 * size: 38657516 bytes
 *
 * uuencode - GNU sharutils 4.2.1
 * real	0m9.066s
 * user	0m8.930s
 * sys  0m0.220s
 *
 * uuencode - GMime 1.90.4
 * real	0m3.563s
 * user	0m3.240s
 * sys 	0m0.200s
 *
 * And here are the results of base64 encoding the same file:
 *
 * uuencode - GNU sharutils 4.2.1
 * real	0m8.920s
 * user	0m8.750s
 * sys	0m0.160s
 *
 * uuencode - GMime 1.90.4
 * real	0m1.173s
 * user	0m0.940s
 * sys	0m0.180s
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "getopt.h"

#include <gmime/gmime.h>


static struct option longopts[] = {
	{ "help",        no_argument,       NULL, 'h' },
	{ "version",     no_argument,       NULL, 'v' },
	{ "base64",      no_argument,       NULL, 'm' },
	{ NULL,          no_argument,       NULL,  0  }
};


static int
uuopen (const char *filename, int flags, mode_t mode)
{
	int fd, err;
	
	if (strcmp (filename, "-") != 0)
		return open (filename, flags, mode);
	
	if (flags & O_RDONLY == O_RDONLY)
		return dup (0);
	else
		return dup (1);
}

static void
usage (const char *progname)
{
	printf ("Usage: %s [options] [ file ] name\n\n", progname);
	printf ("Options:\n");
	printf ("  -h, --help               display help and exit\n");
	printf ("  -v, --version            display version and exit\n");
	printf ("  -m, --base64             use RFC1521 base64 encoding\n");
}

static void
version (const char *progname)
{
	printf ("%s - GMime %s\n", progname, VERSION);
}

static void
uuencode (const char *progname, int argc, char **argv)
{
	GMimeStream *istream, *ostream, *fstream;
	const char *filename, *name, *param;
	GMimeFilterBasicType encoding;
	GMimeFilter *filter;
	gboolean base64;
	struct stat st;
	int fd, opt;
	
	base64 = FALSE;
	encoding = GMIME_FILTER_BASIC_UU_ENC;
	while ((opt = getopt_long (argc, argv, "hvm", longopts, NULL)) != -1) {
		switch (opt) {
		case 'h':
			usage (progname);
			exit (0);
			break;
		case 'v':
			version (progname);
			exit (0);
			break;
		case 'm':
			base64 = TRUE;
			encoding = GMIME_FILTER_BASIC_BASE64_ENC;
			break;
		default:
			printf ("Try `%s --help' for more information.\n", progname);
			exit (1);
		}
	}
	
	if (optind >= argc) {
		printf ("Try `%s --help' for more information.\n", progname);
		exit (1);
	}
	
	if ((fd = dup (1)) == -1) {
		fprintf (stderr, "%s: cannot open stdout: %s\n",
			 progname, strerror (errno));
		exit (1);
	}
	
	ostream = g_mime_stream_fs_new (fd);
	
	if (optind + 1 < argc)
		filename = argv[optind++];
	else
		filename = NULL;
	
	name = argv[optind];
	
	if ((fd = filename ? open (filename, O_RDONLY) : dup (0)) == -1) {
		fprintf (stderr, "%s: %s: %s\n", progname,
			 filename ? filename : "stdin",
			 strerror (errno));
		g_object_unref (ostream);
		exit (1);
	}
	
	if (fstat (fd, &st) == -1) {
		fprintf (stderr, "%s: %s: %s\n", progname,
			 filename ? filename : "stdin",
			 strerror (errno));
		g_object_unref (ostream);
		exit (1);
	}
	
	if (g_mime_stream_printf (ostream, "begin%s %0.3o %s\n",
				  base64 ? "-base64" : "", st.st_mode & 0777, name) == -1) {
		fprintf (stderr, "%s: %s\n", progname, strerror (errno));
		g_object_unref (ostream);
		exit (1);
	}
	
	istream = g_mime_stream_fs_new (fd);
	
	fstream = g_mime_stream_filter_new_with_stream (ostream);
	
	filter = g_mime_filter_basic_new_type (encoding);
	g_mime_stream_filter_add ((GMimeStreamFilter *) fstream, filter);
	g_object_unref (filter);
	
	if (g_mime_stream_write_to_stream (istream, fstream) == -1) {
		fprintf (stderr, "%s: %s\n", progname, strerror (errno));
		g_object_unref (fstream);
		g_object_unref (istream);
		g_object_unref (ostream);
		exit (1);
	}
	
	g_mime_stream_flush (fstream);
	g_object_unref (fstream);
	g_object_unref (istream);
	
	if (g_mime_stream_write_string (ostream, base64 ? "====\n" : "end\n") == -1) {
		fprintf (stderr, "%s: %s\n", progname, strerror (errno));
		g_object_unref (ostream);
		exit (1);
	}
	
	g_object_unref (ostream);
}

int main (int argc, char **argv)
{
	const char *progname;
	
	g_type_init ();
	
	if (!(progname = strrchr (argv[0], '/')))
		progname = argv[0];
	else
		progname++;
	
	uuencode (progname, argc, argv);
	
	return 0;
}