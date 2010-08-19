/*
 *
 *  AT chat library with GLib integration
 *
 *  Copyright (C) 2008-2010  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <ctype.h>

#include <glib.h>

#include "gatutil.h"

void g_at_util_debug_chat(gboolean in, const char *str, gsize len,
				GAtDebugFunc debugf, gpointer user_data)
{
	char type = in ? '<' : '>';
	gsize escaped = 2; /* Enough for '<', ' ' */
	char *escaped_str;
	const char *esc = "<ESC>";
	gsize esc_size = strlen(esc);
	const char *ctrlz = "<CtrlZ>";
	gsize ctrlz_size = strlen(ctrlz);
	gsize i;

	if (!debugf || !len)
		return;

	for (i = 0; i < len; i++) {
		char c = str[i];

		if (isprint(c))
			escaped += 1;
		else if (c == '\r' || c == '\t' || c == '\n')
			escaped += 2;
		else if (c == 26)
			escaped += ctrlz_size;
		else if (c == 25)
			escaped += esc_size;
		else
			escaped += 4;
	}

	escaped_str = g_try_malloc(escaped + 1);
	if (escaped_str == NULL)
		return;

	escaped_str[0] = type;
	escaped_str[1] = ' ';
	escaped_str[2] = '\0';
	escaped_str[escaped] = '\0';

	for (escaped = 2, i = 0; i < len; i++) {
		char c = str[i];

		switch (c) {
		case '\r':
			escaped_str[escaped++] = '\\';
			escaped_str[escaped++] = 'r';
			break;
		case '\t':
			escaped_str[escaped++] = '\\';
			escaped_str[escaped++] = 't';
			break;
		case '\n':
			escaped_str[escaped++] = '\\';
			escaped_str[escaped++] = 'n';
			break;
		case 26:
			strncpy(&escaped_str[escaped], ctrlz, ctrlz_size);
			escaped += ctrlz_size;
			break;
		case 25:
			strncpy(&escaped_str[escaped], esc, esc_size);
			escaped += esc_size;
			break;
		default:
			if (isprint(c))
				escaped_str[escaped++] = c;
			else {
				escaped_str[escaped++] = '\\';
				escaped_str[escaped++] = '0' + ((c >> 6) & 07);
				escaped_str[escaped++] = '0' + ((c >> 3) & 07);
				escaped_str[escaped++] = '0' + (c & 07);
			}
		}
	}

	debugf(escaped_str, user_data);
	g_free(escaped_str);
}

void g_at_util_debug_dump(gboolean in, const unsigned char *buf, gsize len,
				GAtDebugFunc debugf, gpointer user_data)
{
	char type = in ? '<' : '>';
	GString *str;
	gsize i;

	if (!debugf || !len)
		return;

	str = g_string_sized_new(1 + (len * 2));
	if (!str)
		return;

	g_string_append_c(str, type);

	for (i = 0; i < len; i++)
		g_string_append_printf(str, " %02x", buf[i]);

	debugf(str->str, user_data);
	g_string_free(str, TRUE);
}

gboolean g_at_util_setup_io(GIOChannel *io, GIOFlags flags)
{
	GIOFlags io_flags;

	if (g_io_channel_set_encoding(io, NULL, NULL) !=
			G_IO_STATUS_NORMAL)
		return FALSE;

	if (flags & G_IO_FLAG_SET_MASK) {
		io_flags = g_io_channel_get_flags(io);

		io_flags |= (flags & G_IO_FLAG_SET_MASK);

		if (g_io_channel_set_flags(io, io_flags, NULL) !=
				G_IO_STATUS_NORMAL)
			return FALSE;
	}

	g_io_channel_set_close_on_unref(io, TRUE);

	return TRUE;
}
