/*
 *
 *  oFono - Open Source Telephony
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

#ifndef __OFONO_VOICECALL_H
#define __OFONO_VOICECALL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ofono/types.h>

struct ofono_voicecall;

typedef void (*ofono_voicecall_cb_t)(const struct ofono_error *error,
					void *data);

typedef void (*ofono_call_list_cb_t)(const struct ofono_error *error,
					int numcalls,
					const struct ofono_call *call_list,
					void *data);

/* Voice call related functionality, including ATD, ATA, +CHLD, CTFR, CLCC
 * and VTS.
 *
 * It is up to the plugin to implement polling of CLCC if the modem does
 * not support vendor extensions for call progress indication.
 */
struct ofono_voicecall_driver {
	const char *name;
	int (*probe)(struct ofono_voicecall *vc, unsigned int vendor,
			void *data);
	void (*remove)(struct ofono_voicecall *vc);

	/* According to 22.030 the dial is expected to do the following:
	 * - If an there is an existing active call(s), and the dial is
	 *   successful, the active calls are automatically put on hold.
	 *   Driver must take special care to put the call on hold before
	 *   returning from atd call.
	 *
	 * - The dial has no affect on the state of the waiting call,
	 *   if the hardware does not support this, then it is better
	 *   to return an error here.  No special handling of the
	 *   waiting call is performed by the core
	 */
	void (*dial)(struct ofono_voicecall *vc,
			const struct ofono_phone_number *number,
			enum ofono_clir_option clir, enum ofono_cug_option cug,
			ofono_voicecall_cb_t cb, void *data);
	void (*answer)(struct ofono_voicecall *vc,
			ofono_voicecall_cb_t cb, void *data);
	void (*hangup)(struct ofono_voicecall *vc,
			ofono_voicecall_cb_t cb, void *data);
	void (*hold_all_active)(struct ofono_voicecall *vc,
			ofono_voicecall_cb_t cb, void *data);
	void (*release_all_held)(struct ofono_voicecall *vc,
			ofono_voicecall_cb_t cb, void *data);
	void (*set_udub)(struct ofono_voicecall *vc,
			ofono_voicecall_cb_t cb, void *data);
	void (*release_all_active)(struct ofono_voicecall *vc,
			ofono_voicecall_cb_t cb, void *data);
	void (*release_specific)(struct ofono_voicecall *vc, int id,
			ofono_voicecall_cb_t cb, void *data);
	void (*private_chat)(struct ofono_voicecall *vc, int id,
			ofono_voicecall_cb_t cb, void *data);
	void (*create_multiparty)(struct ofono_voicecall *vc,
			ofono_voicecall_cb_t cb, void *data);
	void (*transfer)(struct ofono_voicecall *vc,
			ofono_voicecall_cb_t cb, void *data);
	void (*deflect)(struct ofono_voicecall *vc,
			const struct ofono_phone_number *ph,
			ofono_voicecall_cb_t cb, void *data);
	void (*swap_without_accept)(struct ofono_voicecall *vc,
			ofono_voicecall_cb_t cb, void *data);
	void (*send_tones)(struct ofono_voicecall *vc, const char *tones,
			ofono_voicecall_cb_t cb, void *data);
};

void ofono_voicecall_notify(struct ofono_voicecall *vc,
				const struct ofono_call *call);
void ofono_voicecall_disconnected(struct ofono_voicecall *vc, int id,
				enum ofono_disconnect_reason reason,
				const struct ofono_error *error);

int ofono_voicecall_driver_register(const struct ofono_voicecall_driver *d);
void ofono_voicecall_driver_unregister(const struct ofono_voicecall_driver *d);

struct ofono_voicecall *ofono_voicecall_create(struct ofono_modem *modem,
					unsigned int vendor,
					const char *driver, void *data);

void ofono_voicecall_register(struct ofono_voicecall *vc);
void ofono_voicecall_remove(struct ofono_voicecall *vc);

void ofono_voicecall_set_data(struct ofono_voicecall *vc, void *data);
void *ofono_voicecall_get_data(struct ofono_voicecall *vc);
int ofono_voicecall_get_next_callid(struct ofono_voicecall *vc);

#ifdef __cplusplus
}
#endif

#endif /* __OFONO_VOICECALL_H */
