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

#ifndef __GISI_ITER_H
#define __GISI_ITER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct _GIsiSubBlockIter {
	uint8_t *start;
	uint8_t *end;
	uint16_t longhdr;
	uint16_t sub_blocks;
};

typedef struct _GIsiSubBlockIter GIsiSubBlockIter;

void g_isi_sb_iter_init(GIsiSubBlockIter *iter,
			const void *restrict data,
			size_t len, size_t used);
void g_isi_sb_iter_init_full(GIsiSubBlockIter *iter,
				const void *restrict data,
				size_t len, size_t used,
				gboolean longhdr,
				uint16_t sub_blocks);
gboolean g_isi_sb_iter_is_valid(const GIsiSubBlockIter *iter);

gboolean g_isi_sb_iter_next(GIsiSubBlockIter *iter);

int g_isi_sb_iter_get_id(const GIsiSubBlockIter *iter);
size_t g_isi_sb_iter_get_len(const GIsiSubBlockIter *iter);

gboolean g_isi_sb_iter_get_data(const GIsiSubBlockIter *restrict iter,
				void **data, unsigned pos);
gboolean g_isi_sb_iter_get_byte(const GIsiSubBlockIter *restrict iter,
				uint8_t *byte, unsigned pos);
gboolean g_isi_sb_iter_get_word(const GIsiSubBlockIter *restrict iter,
				uint16_t *word, unsigned pos);
gboolean g_isi_sb_iter_get_dword(const GIsiSubBlockIter *restrict iter,
					uint32_t *dword, unsigned pos);
gboolean g_isi_sb_iter_get_oper_code(const GIsiSubBlockIter *restrict iter,
					char *mcc, char *mnc, unsigned pos);
gboolean g_isi_sb_iter_get_alpha_tag(const GIsiSubBlockIter *restrict iter,
					char **utf8, size_t len, unsigned pos);
gboolean g_isi_sb_iter_get_latin_tag(const GIsiSubBlockIter *restrict iter,
					char **ascii, size_t len, unsigned pos);

#ifdef __cplusplus
}
#endif

#endif /* __GISI_ITER_H */
