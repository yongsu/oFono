/*
 * This file is part of oFono - Open Source Telephony
 *
 * Copyright (C) 2010  Nokia Corporation and/or its subsidary(-ies).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include <glib.h>

#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/gprs.h>
#include <gisi/client.h>

#include "isimodem.h"
#include "isiutil.h"
#include "gpds.h"
#include "debug.h"

struct gprs_data {
	GIsiClient *client;
};

static void detach_ind_cb(GIsiClient *client,
				const void *restrict data, size_t len,
				uint16_t object, void *opaque)
{
	/*struct ofono_gprs *gprs = opaque;*/
	const unsigned char *msg = data;

	if (!msg || len < 3 || msg[0] != GPDS_DETACH_IND)
		return;

	DBG("detached: %s (0x%02"PRIx8")",
		gpds_isi_cause_name(msg[1]), msg[1]);

	/* TODO: Don't report this to core, it won't ever reattach */
	/*ofono_gprs_detached_notify(gprs);*/
}

static gboolean isi_gprs_register(gpointer user)
{
	struct ofono_gprs *gprs = user;
	struct gprs_data *gd = ofono_gprs_get_data(gprs);

	const char *debug = getenv("OFONO_ISI_DEBUG");

	if (debug && (strcmp(debug, "all") == 0 || strcmp(debug, "gpds") == 0))
		g_isi_client_set_debug(gd->client, gpds_debug, NULL);

	g_isi_subscribe(gd->client, GPDS_DETACH_IND, detach_ind_cb, gprs);

	ofono_gprs_register(user);

	return FALSE;
}

static void gpds_reachable_cb(GIsiClient *client,
				gboolean alive, uint16_t object,
				void *opaque)
{
	struct ofono_gprs *gprs = opaque;

	if (!alive) {
		DBG("unable to bootsrap gprs driver");
		return;
	}

	DBG("%s (v%03d.%03d)",
		pn_resource_name(g_isi_client_resource(client)),
		g_isi_version_major(client),
		g_isi_version_minor(client));

	g_idle_add(isi_gprs_register, gprs);
}

static int isi_gprs_probe(struct ofono_gprs *gprs,
				unsigned int vendor, void *user)
{
	GIsiModem *idx = user;
	struct gprs_data *gd = g_try_new0(struct gprs_data, 1);

	if (!gd)
		return -ENOMEM;

	gd->client = g_isi_client_create(idx, PN_GPDS);
	if (!gd->client) {
		g_free(gd);
		return -ENOMEM;
	}

	ofono_gprs_set_data(gprs, gd);

	ofono_gprs_set_cid_range(gprs, 1, GPDS_MAX_CONTEXT_COUNT + 1);

	g_isi_verify(gd->client, gpds_reachable_cb, gprs);

	return 0;
}

static void isi_gprs_remove(struct ofono_gprs *gprs)
{
	struct gprs_data *data = ofono_gprs_get_data(gprs);

	if (!data)
		return;

	ofono_gprs_set_data(gprs, NULL);
	g_isi_client_destroy(data->client);
	g_free(data);
}

static gboolean attach_resp_cb(GIsiClient *client,
				const void *restrict data, size_t len,
				uint16_t object, void *opaque)
{
	const unsigned char *msg = data;
	struct isi_cb_data *cbd = opaque;
	ofono_gprs_cb_t cb = cbd->cb;

	if (!msg) {
		DBG("ISI client error: %d", g_isi_client_error(client));
		goto error;
	}

	if (len != 4 || msg[0] != GPDS_ATTACH_RESP)
		return FALSE;

	if (msg[1] != GPDS_OK) {
		DBG("attach failed: %s", gpds_status_name(msg[1]));
		goto error;
	}

	CALLBACK_WITH_SUCCESS(cb, cbd->data);
	goto out;

error:
	CALLBACK_WITH_FAILURE(cb, cbd->data);

out:
	g_free(cbd);
	return TRUE;
}

static gboolean detach_resp_cb(GIsiClient *client,
				const void *restrict data, size_t len,
				uint16_t object, void *opaque)
{
	const unsigned char *msg = data;
	struct isi_cb_data *cbd = opaque;
	ofono_gprs_cb_t cb = cbd->cb;

	if (!msg) {
		DBG("ISI client error: %d", g_isi_client_error(client));
		goto error;
	}

	if (len != 3 || msg[0] != GPDS_DETACH_RESP)
		return FALSE;

	if (msg[1] != GPDS_OK) {
		DBG("detach failed: %s", gpds_status_name(msg[1]));
		goto error;
	}

	CALLBACK_WITH_SUCCESS(cb, cbd->data);
	goto out;

error:
	CALLBACK_WITH_FAILURE(cb, cbd->data);

out:
	g_free(cbd);
	return TRUE;
}

static GIsiRequest *attach_request_make(GIsiClient *client, void *data)
{
	const unsigned char msg[] = {
		GPDS_ATTACH_REQ,
		GPDS_FOLLOW_OFF
	};

	return g_isi_request_make(client, msg, sizeof(msg), GPDS_TIMEOUT,
					attach_resp_cb, data);
}

static GIsiRequest *detach_request_make(GIsiClient *client, void *data)
{
	const unsigned char msg[] = {
		GPDS_DETACH_REQ,
		0x00, /* filler */
		0x00  /* sub-blocks */
	};

	return g_isi_request_make(client, msg, sizeof(msg), GPDS_TIMEOUT,
					detach_resp_cb, data);
}

static void isi_gprs_set_attached(struct ofono_gprs *gprs, int attached,
					ofono_gprs_cb_t cb, void *data)
{
	struct gprs_data *gd = ofono_gprs_get_data(gprs);
	struct isi_cb_data *cbd = isi_cb_data_new(NULL, cb, data);

	GIsiRequest *req;

	if (!cbd)
		goto error;

	if (attached)
		req = attach_request_make(gd->client, cbd);
	else
		req = detach_request_make(gd->client, cbd);

	if (req)
		return;

error:
	CALLBACK_WITH_FAILURE(cb, data);
	g_free(cbd);
}

static gboolean status_resp_cb(GIsiClient *client,
				const void *restrict data, size_t len,
				uint16_t object, void *opaque)
{
	const unsigned char *msg = data;
	struct isi_cb_data *cbd = opaque;
	ofono_gprs_status_cb_t cb = cbd->cb;

	if (!msg) {
		DBG("ISI client error: %d", g_isi_client_error(client));
		goto error;
	}

	if (len < 2 || msg[0] != GPDS_STATUS_RESP)
		return FALSE;

	/* FIXME: the core still expects reg status, and not a boolean
	 * attached status here.*/

	/* CALLBACK_WITH_SUCCESS(cb, msg[1] == GPDS_ATTACHED, cbd->data); */
	CALLBACK_WITH_SUCCESS(cb, 1, cbd->data);

	goto out;

error:
	CALLBACK_WITH_FAILURE(cb, -1, cbd->data);

out:
	g_free(cbd);
	return TRUE;
}

static void isi_gprs_attached_status(struct ofono_gprs *gprs,
						ofono_gprs_status_cb_t cb,
						void *data)
{
	struct gprs_data *gd = ofono_gprs_get_data(gprs);
	struct isi_cb_data *cbd = isi_cb_data_new(NULL, cb, data);

	const unsigned char msg[] = {
		GPDS_STATUS_REQ,
	};

	if (!cbd)
		goto error;

	if (g_isi_request_make(gd->client, msg, sizeof(msg), GPDS_TIMEOUT,
					status_resp_cb, cbd))
		return;

error:
	CALLBACK_WITH_FAILURE(cb, -1, data);
	g_free(cbd);

}

static struct ofono_gprs_driver driver = {
	.name			= "isimodem",
	.probe			= isi_gprs_probe,
	.remove			= isi_gprs_remove,
	.set_attached		= isi_gprs_set_attached,
	.attached_status	= isi_gprs_attached_status,
};

void isi_gprs_init(void)
{
	ofono_gprs_driver_register(&driver);
}

void isi_gprs_exit(void)
{
	ofono_gprs_driver_unregister(&driver);
}
