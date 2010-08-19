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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib.h>

#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/devinfo.h>

#include "gatchat.h"
#include "gatresult.h"

#include "atmodem.h"

static const char *fixup_return(const char *line, const char *prefix)
{
	if (g_str_has_prefix(line, prefix) == FALSE)
		return line;

	line = line + strlen(prefix);

	while (line[0] == ' ')
		line++;

	return line;
}

static void attr_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_devinfo_query_cb_t cb = cbd->cb;
	const char *prefix = cbd->user;
	struct ofono_error error;
	int numlines = g_at_result_num_response_lines(result);
	GAtResultIter iter;
	const char *line;
	int i;

	decode_at_error(&error, g_at_result_final_response(result));

	if (!ok) {
		cb(&error, NULL, cbd->data);
		return;
	}

	if (numlines == 0) {
		CALLBACK_WITH_FAILURE(cb, NULL, cbd->data);
		return;
	}

	g_at_result_iter_init(&iter, result);

	/* We have to be careful here, sometimes a stray unsolicited
	 * notification will appear as part of the response and we
	 * cannot rely on having a prefix to recognize the actual
	 * response line.  So use the last line only as the response
	 */
	for (i = 0; i < numlines; i++)
		g_at_result_iter_next(&iter, NULL);

	line = g_at_result_iter_raw_line(&iter);

	cb(&error, fixup_return(line, prefix), cbd->data);
}

static void at_query_manufacturer(struct ofono_devinfo *info,
				ofono_devinfo_query_cb_t cb, void *data)
{
	struct cb_data *cbd = cb_data_new(cb, data);
	GAtChat *chat = ofono_devinfo_get_data(info);

	if (!cbd)
		goto error;

	cbd->user = "+CGMI:";

	if (g_at_chat_send(chat, "AT+CGMI", NULL,
				attr_cb, cbd, g_free) > 0)
		return;

error:
	if (cbd)
		g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, NULL, data);
}

static void at_query_model(struct ofono_devinfo *info,
				ofono_devinfo_query_cb_t cb, void *data)
{
	struct cb_data *cbd = cb_data_new(cb, data);
	GAtChat *chat = ofono_devinfo_get_data(info);

	if (!cbd)
		goto error;

	cbd->user = "+CGMM:";

	if (g_at_chat_send(chat, "AT+CGMM", NULL,
				attr_cb, cbd, g_free) > 0)
		return;

error:
	if (cbd)
		g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, NULL, data);
}

static void at_query_revision(struct ofono_devinfo *info,
				ofono_devinfo_query_cb_t cb, void *data)
{
	struct cb_data *cbd = cb_data_new(cb, data);
	GAtChat *chat = ofono_devinfo_get_data(info);

	if (!cbd)
		goto error;

	cbd->user = "+CGMR:";

	if (g_at_chat_send(chat, "AT+CGMR", NULL,
				attr_cb, cbd, g_free) > 0)
		return;

error:
	if (cbd)
		g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, NULL, data);
}

static void at_query_serial(struct ofono_devinfo *info,
				ofono_devinfo_query_cb_t cb, void *data)
{
	struct cb_data *cbd = cb_data_new(cb, data);
	GAtChat *chat = ofono_devinfo_get_data(info);

	if (!cbd)
		goto error;

	cbd->user = "+CGSN:";

	if (g_at_chat_send(chat, "AT+CGSN", NULL,
				attr_cb, cbd, g_free) > 0)
		return;

error:
	if (cbd)
		g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, NULL, data);
}

static gboolean at_devinfo_register(gpointer user_data)
{
	struct ofono_devinfo *info = user_data;

	ofono_devinfo_register(info);

	return FALSE;
}

static int at_devinfo_probe(struct ofono_devinfo *info, unsigned int vendor,
				void *data)
{
	GAtChat *chat = data;

	ofono_devinfo_set_data(info, chat);
	g_idle_add(at_devinfo_register, info);

	return 0;
}

static void at_devinfo_remove(struct ofono_devinfo *info)
{
}

static struct ofono_devinfo_driver driver = {
	.name = "atmodem",
	.probe = at_devinfo_probe,
	.remove = at_devinfo_remove,
	.query_manufacturer = at_query_manufacturer,
	.query_model = at_query_model,
	.query_revision = at_query_revision,
	.query_serial = at_query_serial
};

void at_devinfo_init(void)
{
	ofono_devinfo_driver_register(&driver);
}

void at_devinfo_exit(void)
{
	ofono_devinfo_driver_unregister(&driver);
}
