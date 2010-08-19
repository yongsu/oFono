/*
 * This file is part of oFono - Open Source Telephony
 *
 * Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Rémi Denis-Courmont <remi.denis-courmont@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

typedef struct _GIsiPipe GIsiPipe;

GIsiPipe *g_isi_pipe_create(GIsiModem *, void (*cb)(GIsiPipe *),
				uint16_t obj1, uint16_t obj2,
				uint8_t type1, uint8_t type2);
void g_isi_pipe_destroy(GIsiPipe *pipe);

void g_isi_pipe_set_error_handler(GIsiPipe *pipe, void (*cb)(GIsiPipe *));
int g_isi_pipe_get_error(const GIsiPipe *pipe);
void *g_isi_pipe_set_userdata(GIsiPipe *pipe, void *data);
void *g_isi_pipe_get_userdata(GIsiPipe *pipe);
uint8_t g_isi_pipe_get_handle(GIsiPipe *pipe);

int g_isi_pipe_start(GIsiPipe *pipe);
