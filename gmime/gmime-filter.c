/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2001 Ximian, Inc. (www.ximian.com)
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


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h> /* for memcpy */

#include "gmime-filter.h"


struct _GMimeFilterPrivate {
	char *inbuf;
	size_t inlen;
};

#define PRE_HEAD (64)
#define BACK_HEAD (64)
#define _PRIVATE(o) (((GMimeFilter *)(o))->priv)

static void g_mime_filter_class_init (GMimeFilterClass *klass);
static void g_mime_filter_init (GMimeFilter *filter, GMimeFilterClass *klass);
static void g_mime_filter_finalize (GObject *object);

static GMimeFilter *filter_copy (GMimeFilter *filter);
static void filter_filter (GMimeFilter *filter, char *in, size_t len, size_t prespace,
			   char **out, size_t *outlen, size_t *outprespace);
static void filter_complete (GMimeFilter *filter, char *in, size_t len, size_t prespace,
			     char **out, size_t *outlen, size_t *outprespace);
static void filter_reset (GMimeFilter *filter);


static GObjectClass *parent_class = NULL;


GType
g_mime_filter_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (GMimeFilterClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) g_mime_filter_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (GMimeFilter),
			0,    /* n_preallocs */
			(GInstanceInitFunc) g_mime_filter_init,
		};
		
		type = g_type_register_static (G_TYPE_OBJECT, "GMimeFilter", &info, 0);
	}
	
	return type;
}


static void
g_mime_filter_class_init (GMimeFilterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (G_TYPE_OBJECT);
	
	object_class->finalize = g_mime_filter_finalize;
	
	klass->copy = filter_copy;
	klass->filter = filter_filter;
	klass->complete = filter_complete;
	klass->reset = filter_reset;
}

static void
g_mime_filter_init (GMimeFilter *filter, GMimeFilterClass *klass)
{
	filter->priv = g_new0 (struct _GMimeFilterPrivate, 1);
	filter->outptr = NULL;
	filter->outreal = NULL;
	filter->outbuf = NULL;
	filter->outsize = 0;
	
	filter->backbuf = NULL;
	filter->backsize = 0;
	filter->backlen = 0;
}

static void
g_mime_filter_finalize (GObject *object)
{
	GMimeFilter *filter = (GMimeFilter *) object;
	
	g_free (filter->priv->inbuf);
	g_free (filter->priv);
	g_free (filter->outreal);
	g_free (filter->backbuf);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static GMimeFilter *
filter_copy (GMimeFilter *filter)
{
	return NULL;
}


/**
 * g_mime_filter_copy:
 * @filter: filter
 *
 * Copies @filter into a new GMimeFilter object.
 *
 * Returns a duplicate of @filter.
 **/
GMimeFilter *
g_mime_filter_copy (GMimeFilter *filter)
{
	g_return_val_if_fail (GMIME_IS_FILTER (filter), NULL);
	
	return GMIME_FILTER_GET_CLASS (filter)->copy (filter);
}


static void
filter_run (GMimeFilter *filter, char *in, size_t len, size_t prespace,
	    char **out, size_t *outlen, size_t *outprespace,
	    void (*filterfunc) (GMimeFilter *filter,
				char *in, size_t len, size_t prespace,
				char **out, size_t *outlen, size_t *outprespace))
{
	/* here we take a performance hit, if the input buffer doesn't
	   have the pre-space required.  We make a buffer that does... */
	if (prespace < filter->backlen) {
		struct _GMimeFilterPrivate *p = _PRIVATE (filter);
		size_t newlen = len + prespace + filter->backlen;
		
		if (p->inlen < newlen) {
			/* NOTE: g_realloc copies data, we dont need that (slower) */
			g_free (p->inbuf);
			p->inbuf = g_malloc (newlen + PRE_HEAD);
			p->inlen = newlen + PRE_HEAD;
		}
		
		/* copy to end of structure */
		memcpy (p->inbuf + p->inlen - len, in, len);
		in = p->inbuf + p->inlen - len;
		prespace = p->inlen - len;
	}
	
	/* preload any backed up data */
	if (filter->backlen > 0) {
		memcpy (in - filter->backlen, filter->backbuf, filter->backlen);
		in -= filter->backlen;
		len += filter->backlen;
		prespace -= filter->backlen;
		filter->backlen = 0;
	}
	
	filterfunc (filter, in, len, prespace, out, outlen, outprespace);
}


static void
filter_filter (GMimeFilter *filter, char *in, size_t len, size_t prespace,
	       char **out, size_t *outlen, size_t *outprespace)
{
	/* no-op */
}


/**
 * g_mime_filter_filter:
 * @filter: filter
 * @in: input buffer
 * @len: input buffer length
 * @prespace: prespace buffer length
 * @out: pointer to output buffer
 * @outlen: pointer to output length
 * @outprespace: pointer to output prespace buffer length
 *
 * Filters the input data and writes it to @out.
 **/
void
g_mime_filter_filter (GMimeFilter *filter, char *in, size_t len, size_t prespace,
		      char **out, size_t *outlen, size_t *outprespace)
{
	g_return_if_fail (GMIME_IS_FILTER (filter));
	
	filter_run (filter, in, len, prespace, out, outlen, outprespace,
		    GMIME_FILTER_GET_CLASS (filter)->filter);
}


static void
filter_complete (GMimeFilter *filter, char *in, size_t len, size_t prespace,
		 char **out, size_t *outlen, size_t *outprespace)
{
	/* no-op */
}


/**
 * g_mime_filter_complete:
 * @filter: filter
 * @in: input buffer
 * @len: input length
 * @prespace: prespace buffer length
 * @out: pointer to output buffer
 * @outlen: pointer to output length
 * @outprespace: pointer to output prespace buffer length
 *
 * Completes the filtering.
 **/
void
g_mime_filter_complete (GMimeFilter *filter,
			char *in, size_t len, size_t prespace,
			char **out, size_t *outlen, size_t *outprespace)
{
	g_return_if_fail (GMIME_IS_FILTER (filter));
	
	filter_run (filter, in, len, prespace, out, outlen, outprespace,
		    GMIME_FILTER_GET_CLASS (filter)->complete);
}


static void
filter_reset (GMimeFilter *filter)
{
	/* no-op */
}


/**
 * g_mime_filter_reset:
 * @filter:
 *
 * Resets the filter.
 **/
void
g_mime_filter_reset (GMimeFilter *filter)
{
	g_return_if_fail (GMIME_IS_FILTER (filter));
	
	GMIME_FILTER_GET_CLASS (filter)->reset (filter);
	
	/* could free some buffers, if they are really big? */
	filter->backlen = 0;
}


/**
 * g_mime_filter_backup:
 * @filter: filter
 * @data: 
 * @length: 
 *
 * Sets number of bytes backed up on the input, new calls replace
 * previous ones
 **/
void
g_mime_filter_backup (GMimeFilter *filter, const char *data, size_t length)
{
	g_return_if_fail (GMIME_IS_FILTER (filter));
	
	if (filter->backsize < length) {
		/* g_realloc copies data, unnecessary overhead */
		g_free (filter->backbuf);
		filter->backbuf = g_malloc (length + BACK_HEAD);
		filter->backsize = length + BACK_HEAD;
	}
	
	filter->backlen = length;
	memcpy (filter->backbuf, data, length);
}


/**
 * g_mime_filter_set_size:
 * @filter: filter
 * @size: 
 * @keep: 
 *
 * Ensure this much size available for filter output (if required)
 **/
void
g_mime_filter_set_size (GMimeFilter *filter, size_t size, gboolean keep)
{
	g_return_if_fail (GMIME_IS_FILTER (filter));
	
	if (filter->outsize < size) {
		size_t offset = filter->outptr - filter->outreal;
		
		if (keep) {
			filter->outreal = g_realloc (filter->outreal, size + PRE_HEAD * 4);
		} else {
			g_free (filter->outreal);
			filter->outreal = g_malloc (size + PRE_HEAD * 4);
		}
		
		filter->outptr = filter->outreal + offset;
		filter->outbuf = filter->outreal + PRE_HEAD * 4;
		filter->outsize = size;
		
		/* this could be offset from the end of the structure, but 
		   this should be good enough */
		
		filter->outpre = PRE_HEAD * 4;
	}
}