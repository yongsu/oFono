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

typedef struct _GIsiPEP GIsiPEP;
typedef void (*GIsiPEPCallback)(GIsiPEP *pep, void *opaque);

GIsiPEP *g_isi_pep_create(GIsiModem *modem, GIsiPEPCallback, void *);
void g_isi_pep_destroy(GIsiPEP *pep);
uint16_t g_isi_pep_get_object(const GIsiPEP *pep);
unsigned g_isi_pep_get_ifindex(const GIsiPEP *pep);
char *g_isi_pep_get_ifname(const GIsiPEP *pep, char *ifname);
