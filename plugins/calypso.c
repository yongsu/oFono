/*
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

#include <stdlib.h>
#include <errno.h>
#include <termios.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <glib.h>
#include <gatchat.h>
#include <gattty.h>
#include <gatmux.h>

#define OFONO_API_SUBJECT_TO_CHANGE
#include <ofono/plugin.h>
#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/call-barring.h>
#include <ofono/call-forwarding.h>
#include <ofono/call-meter.h>
#include <ofono/call-settings.h>
#include <ofono/call-volume.h>
#include <ofono/devinfo.h>
#include <ofono/message-waiting.h>
#include <ofono/netreg.h>
#include <ofono/phonebook.h>
#include <ofono/sim.h>
#include <ofono/sms.h>
#include <ofono/ssn.h>
#include <ofono/ussd.h>
#include <ofono/voicecall.h>

#include <drivers/atmodem/vendor.h>

#define CALYPSO_POWER_PATH "/sys/bus/platform/devices/neo1973-pm-gsm.0/power_on"
#define CALYPSO_RESET_PATH "/sys/bus/platform/devices/neo1973-pm-gsm.0/reset"

enum powercycle_state {
	POWERCYCLE_STATE_POWER0 = 0,
	POWERCYCLE_STATE_RESET0,
	POWERCYCLE_STATE_POWER1,
	POWERCYCLE_STATE_RESET1,
	POWERCYCLE_STATE_FINISHED,
};

#define NUM_DLC 4

#define VOICE_DLC 0
#define NETREG_DLC 1
#define SMS_DLC 2
#define AUX_DLC 3
#define SETUP_DLC 3

struct calypso_data {
	GAtMux *mux;
	GAtChat *dlcs[NUM_DLC];
	enum powercycle_state state;
	gboolean phonebook_added;
	gboolean sms_added;
};

static void calypso_debug(const char *str, void *data)
{
	guint dlc = GPOINTER_TO_UINT(data);

	ofono_info("DLC%u: %s", dlc, str);
}

static void calypso_mux_debug(const char *str, void *data)
{
	ofono_info("MUX: %s", str);
}

static void calypso_setup_debug(const char *str, void *data)
{
	ofono_info("Setup: %s", str);
}

static int calypso_probe(struct ofono_modem *modem)
{
	const char *device;
	struct calypso_data *data;

	DBG("");

	device = ofono_modem_get_string(modem, "Device");
	if (device == NULL)
		return -EINVAL;

	DBG("%s", device);

	data = g_new0(struct calypso_data, 1);

	ofono_modem_set_data(modem, data);

	return 0;
}

static void calypso_remove(struct ofono_modem *modem)
{
	struct calypso_data *data = ofono_modem_get_data(modem);

	DBG("");

	g_free(data);
}

static void cstat_notify(GAtResult *result, gpointer user_data)
{
	struct ofono_modem *modem = user_data;
	struct calypso_data *data = ofono_modem_get_data(modem);
	GAtResultIter iter;
	const char *stat;
	int enabled;

	DBG("phonebook: %d, sms: %d", data->phonebook_added, data->sms_added);

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "%CSTAT:"))
		return;

	if (!g_at_result_iter_next_unquoted_string(&iter, &stat))
		return;

	if (!g_at_result_iter_next_number(&iter, &enabled))
		return;

	DBG("stat: %s, enabled: %d", stat, enabled);

	if (!g_strcmp0(stat, "PHB") && enabled == 1 && !data->phonebook_added) {
		data->phonebook_added = TRUE;
		ofono_phonebook_create(modem, 0, "atmodem",
					data->dlcs[AUX_DLC]);
	}

	if (!g_strcmp0(stat, "SMS") && enabled == 1 && !data->sms_added) {
		data->sms_added = TRUE;
		ofono_sms_create(modem, 0, "atmodem", data->dlcs[SMS_DLC]);
	}
}

static void setup_modem(struct ofono_modem *modem)
{
	struct calypso_data *data = ofono_modem_get_data(modem);
	int i;

	/* Generate unsolicited notifications as soon as they're generated */
	for (i = 0; i < NUM_DLC; i++) {
		g_at_chat_send(data->dlcs[i], "ATE0", NULL, NULL, NULL, NULL);
		g_at_chat_send(data->dlcs[i], "AT%CUNS=0",
				NULL, NULL, NULL, NULL);
	}

	/* CSTAT tells us when SMS & Phonebook are ready to be used */
	g_at_chat_register(data->dlcs[SETUP_DLC], "%CSTAT:", cstat_notify,
				FALSE, modem, NULL);
	g_at_chat_send(data->dlcs[SETUP_DLC], "AT%CSTAT=1", NULL,
				NULL, NULL, NULL);

	/* audio side tone: set to minimum */
	g_at_chat_send(data->dlcs[SETUP_DLC], "AT@ST=\"-26\"", NULL,
			NULL, NULL, NULL);

	/* Disable deep sleep */
	g_at_chat_send(data->dlcs[SETUP_DLC], "AT%SLEEP=2", NULL,
			NULL, NULL, NULL);
}

static void cfun_set_on_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct ofono_modem *modem = user_data;
	struct calypso_data *data = ofono_modem_get_data(modem);

	DBG("");

	if (ok == FALSE) {
		int i;

		for (i = 0; i < NUM_DLC; i++) {
			g_at_chat_unref(data->dlcs[i]);
			data->dlcs[i] = NULL;
		}

		g_at_mux_shutdown(data->mux);
		g_at_mux_unref(data->mux);
		data->mux = NULL;
	} else {
		setup_modem(modem);
	}

	ofono_modem_set_powered(modem, ok);
}

static void mux_setup(GAtMux *mux, gpointer user_data)
{
	struct ofono_modem *modem = user_data;
	struct calypso_data *data = ofono_modem_get_data(modem);
	GIOChannel *io;
	GAtSyntax *syntax;
	int i;

	DBG("%p", mux);

	if (!mux) {
		ofono_modem_set_powered(modem, FALSE);
		return;
	}

	data->mux = mux;

	if (getenv("OFONO_AT_DEBUG"))
		g_at_mux_set_debug(data->mux, calypso_mux_debug, NULL);

	g_at_mux_start(mux);

	for (i = 0; i < NUM_DLC; i++) {
		io = g_at_mux_create_channel(mux);

		syntax = g_at_syntax_new_gsm_permissive();
		data->dlcs[i] = g_at_chat_new(io, syntax);
		g_at_syntax_unref(syntax);
		g_io_channel_unref(io);

		if (getenv("OFONO_AT_DEBUG"))
			g_at_chat_set_debug(data->dlcs[i], calypso_debug,
						GUINT_TO_POINTER(i));

		g_at_chat_set_wakeup_command(data->dlcs[i], "AT\r", 500, 5000);
	}

	g_at_chat_send(data->dlcs[SETUP_DLC], "AT+CFUN=1", NULL,
					cfun_set_on_cb, modem, NULL);
}

static void modem_initialize(struct ofono_modem *modem)
{
	GAtSyntax *syntax;
	GAtChat *chat;
	const char *device;
	GIOChannel *io;
	GHashTable *options;

	DBG("");

	device = ofono_modem_get_string(modem, "Device");

	options = g_hash_table_new(g_str_hash, g_str_equal);
	if (options == NULL)
		goto error;

	g_hash_table_insert(options, "Baud", "115200");
	g_hash_table_insert(options, "Parity", "none");
	g_hash_table_insert(options, "StopBits", "1");
	g_hash_table_insert(options, "DataBits", "8");
	g_hash_table_insert(options, "XonXoff", "on");
	g_hash_table_insert(options, "Local", "on");
	g_hash_table_insert(options, "RtsCts", "on");

	io = g_at_tty_open(device, options);
	g_hash_table_destroy(options);

	if (io == NULL)
		goto error;

	/* Calypso is normally compliant to 27.007, except the vendor-specific
	 * notifications (like %CSTAT) are not prefixed by \r\n
	 */
	syntax = g_at_syntax_new_gsm_permissive();
	chat = g_at_chat_new(io, syntax);
	g_at_syntax_unref(syntax);
	g_io_channel_unref(io);

	if (chat == NULL)
		goto error;

	if (getenv("OFONO_AT_DEBUG") != NULL)
		g_at_chat_set_debug(chat, calypso_setup_debug, NULL);

	g_at_chat_set_wakeup_command(chat, "AT\r", 500, 5000);

	g_at_chat_send(chat, "ATE0", NULL, NULL, NULL, NULL);

	g_at_mux_setup_gsm0710(chat, mux_setup, modem, NULL);
	g_at_chat_unref(chat);

	return;

error:
	ofono_modem_set_powered(modem, FALSE);
}

static gboolean write_file(const char *file, gboolean on)
{
	int fd;
	int r;

	fd = open(file, O_WRONLY);

	if (fd == -1)
		return FALSE;

	DBG("%s, %s", file, on ? "1" : "0");

	if (on)
		r = write(fd, "1\n", 2);
	else
		r = write(fd, "0\n", 2);

	close(fd);

	return r > 0 ? TRUE : FALSE;
}

static gboolean poweron_cycle(gpointer user_data)
{
	struct ofono_modem *modem = user_data;
	struct calypso_data *data = ofono_modem_get_data(modem);

	switch (data->state) {
	case POWERCYCLE_STATE_POWER0:
		if (write_file(CALYPSO_RESET_PATH, FALSE)) {
			data->state = POWERCYCLE_STATE_RESET0;
			return TRUE;
		}

		break;

	case POWERCYCLE_STATE_RESET0:
		if (write_file(CALYPSO_POWER_PATH, TRUE)) {
			data->state = POWERCYCLE_STATE_POWER1;
			return TRUE;
		}

		break;

	case POWERCYCLE_STATE_POWER1:
		if (write_file(CALYPSO_RESET_PATH, TRUE)) {
			data->state = POWERCYCLE_STATE_RESET1;
			return TRUE;
		}

		break;

	case POWERCYCLE_STATE_RESET1:
		if (write_file(CALYPSO_RESET_PATH, FALSE)) {
			data->state = POWERCYCLE_STATE_FINISHED;
			return TRUE;
		}

		break;

	case POWERCYCLE_STATE_FINISHED:
		modem_initialize(modem);
		return FALSE;

	default:
		break;
	};

	ofono_modem_set_powered(modem, FALSE);
	return FALSE;
}

/* power up hardware */
static int calypso_enable(struct ofono_modem *modem)
{
	struct calypso_data *data = ofono_modem_get_data(modem);

	if (write_file(CALYPSO_POWER_PATH, FALSE) == FALSE)
		return -EINVAL;

	data->state = POWERCYCLE_STATE_POWER0;
	g_timeout_add_seconds(1, poweron_cycle, modem);

	return -EINPROGRESS;
}

static int calypso_disable(struct ofono_modem *modem)
{
	struct calypso_data *data = ofono_modem_get_data(modem);
	int i;

	DBG("");

	for (i = 0; i < NUM_DLC; i++) {
		g_at_chat_unref(data->dlcs[i]);
		data->dlcs[i] = NULL;
	}

	g_at_mux_shutdown(data->mux);
	g_at_mux_unref(data->mux);
	data->mux = NULL;

	data->phonebook_added = FALSE;
	data->sms_added = FALSE;

	if (write_file(CALYPSO_POWER_PATH, FALSE))
		return 0;

	return -EINVAL;
}

static void calypso_pre_sim(struct ofono_modem *modem)
{
	struct calypso_data *data = ofono_modem_get_data(modem);
	struct ofono_sim *sim;

	DBG("");

	ofono_devinfo_create(modem, 0, "atmodem", data->dlcs[AUX_DLC]);
	sim = ofono_sim_create(modem, 0, "atmodem", data->dlcs[AUX_DLC]);
	ofono_voicecall_create(modem, 0, "calypsomodem", data->dlcs[VOICE_DLC]);

	if (sim)
		ofono_sim_inserted_notify(sim, TRUE);
}

static void calypso_post_sim(struct ofono_modem *modem)
{
	struct calypso_data *data = ofono_modem_get_data(modem);
	struct ofono_message_waiting *mw;

	DBG("");

	ofono_ussd_create(modem, 0, "atmodem", data->dlcs[AUX_DLC]);
	ofono_call_forwarding_create(modem, 0, "atmodem", data->dlcs[AUX_DLC]);
	ofono_call_settings_create(modem, 0, "atmodem", data->dlcs[AUX_DLC]);
	ofono_netreg_create(modem, OFONO_VENDOR_CALYPSO, "atmodem",
				data->dlcs[NETREG_DLC]);
	ofono_call_meter_create(modem, 0, "atmodem", data->dlcs[AUX_DLC]);
	ofono_call_barring_create(modem, 0, "atmodem", data->dlcs[AUX_DLC]);
	ofono_ssn_create(modem, 0, "atmodem", data->dlcs[AUX_DLC]);
	ofono_call_volume_create(modem, 0, "atmodem", data->dlcs[AUX_DLC]);

	mw = ofono_message_waiting_create(modem);
	if (mw)
		ofono_message_waiting_register(mw);
}

static struct ofono_modem_driver calypso_driver = {
	.name		= "calypso",
	.probe		= calypso_probe,
	.remove		= calypso_remove,
	.enable		= calypso_enable,
	.disable	= calypso_disable,
	.pre_sim	= calypso_pre_sim,
	.post_sim	= calypso_post_sim,
};

static int calypso_init(void)
{
	return ofono_modem_driver_register(&calypso_driver);
}

static void calypso_exit(void)
{
	ofono_modem_driver_unregister(&calypso_driver);
}

OFONO_PLUGIN_DEFINE(calypso, "TI Calypso modem driver", VERSION,
			OFONO_PLUGIN_PRIORITY_DEFAULT,
			calypso_init, calypso_exit)
