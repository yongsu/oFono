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

#ifndef __OFONO_GPRS_CONTEXT_H
#define __OFONO_GPRS_CONTEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ofono/types.h>

struct ofono_gprs_context;

#define OFONO_GPRS_MAX_APN_LENGTH 127
#define OFONO_GPRS_MAX_USERNAME_LENGTH 63
#define OFONO_GPRS_MAX_PASSWORD_LENGTH 255

enum ofono_gprs_proto {
	OFONO_GPRS_PROTO_IP = 0,
	OFONO_GPRS_PROTO_IPV6,
};

struct ofono_gprs_primary_context {
	unsigned int cid;
	int direction;
	char apn[OFONO_GPRS_MAX_APN_LENGTH + 1];
	char username[OFONO_GPRS_MAX_USERNAME_LENGTH + 1];
	char password[OFONO_GPRS_MAX_PASSWORD_LENGTH + 1];
	enum ofono_gprs_proto proto;
};

typedef void (*ofono_gprs_context_cb_t)(const struct ofono_error *error,
					void *data);
typedef void (*ofono_gprs_context_up_cb_t)(const struct ofono_error *error,
				const char *interface, ofono_bool_t static_ip,
				const char *address, const char *netmask,
				const char *gw, const char **dns, void *data);

struct ofono_gprs_context_driver {
	const char *name;
	int (*probe)(struct ofono_gprs_context *gc, unsigned int vendor,
			void *data);
	void (*remove)(struct ofono_gprs_context *gc);
	void (*activate_primary)(struct ofono_gprs_context *gc,
				const struct ofono_gprs_primary_context *ctx,
				ofono_gprs_context_up_cb_t cb, void *data);
	void (*deactivate_primary)(struct ofono_gprs_context *gc,
					unsigned int id,
					ofono_gprs_context_cb_t cb, void *data);
};

void ofono_gprs_context_deactivated(struct ofono_gprs_context *gc,
					unsigned int id);

int ofono_gprs_context_driver_register(const struct ofono_gprs_context_driver *d);
void ofono_gprs_context_driver_unregister(const struct ofono_gprs_context_driver *d);

struct ofono_gprs_context *ofono_gprs_context_create(struct ofono_modem *modem,
						unsigned int vendor,
						const char *driver, void *data);
void ofono_gprs_context_remove(struct ofono_gprs_context *gc);

void ofono_gprs_context_set_data(struct ofono_gprs_context *gc, void *data);
void *ofono_gprs_context_get_data(struct ofono_gprs_context *gc);

struct ofono_modem *ofono_gprs_context_get_modem(struct ofono_gprs_context *gc);

#ifdef __cplusplus
}
#endif

#endif /* __OFONO_GPRS_CONTEXT_H */
