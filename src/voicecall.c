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
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <glib.h>
#include <gdbus.h>

#include "ofono.h"

#include "common.h"
#include "simutil.h"
#include "smsutil.h"

#define VOICECALLS_FLAG_MULTI_RELEASE 0x1

#define MAX_VOICE_CALLS 16

GSList *g_drivers = NULL;

struct ofono_voicecall {
	GSList *call_list;
	GSList *release_list;
	GSList *multiparty_list;
	GSList *en_list;  /* emergency number list */
	GSList *new_en_list; /* Emergency numbers being read from SIM */
	int flags;
	DBusMessage *pending;
	gint emit_calls_source;
	gint emit_multi_source;
	struct ofono_sim *sim;
	unsigned int sim_watch;
	unsigned int sim_state_watch;
	const struct ofono_voicecall_driver *driver;
	void *driver_data;
	struct ofono_atom *atom;
};

struct voicecall {
	struct ofono_call *call;
	struct ofono_voicecall *vc;
	time_t start_time;
	time_t detect_time;
};

static const char *default_en_list[] = { "911", "112", NULL };
static const char *default_en_list_no_sim[] = { "119", "118", "999", "110",
						"08", "000", NULL };

static void generic_callback(const struct ofono_error *error, void *data);
static void multirelease_callback(const struct ofono_error *err, void *data);

static gint call_compare_by_id(gconstpointer a, gconstpointer b)
{
	const struct ofono_call *call = ((struct voicecall *)a)->call;
	unsigned int id = GPOINTER_TO_UINT(b);

	if (id < call->id)
		return -1;

	if (id > call->id)
		return 1;

	return 0;
}

static gint call_compare(gconstpointer a, gconstpointer b)
{
	const struct voicecall *ca = a;
	const struct voicecall *cb = b;

	if (ca->call->id < cb->call->id)
		return -1;

	if (ca->call->id > cb->call->id)
		return 1;

	return 0;
}

static void add_to_en_list(GSList **l, const char **list)
{
	int i = 0;
	while (list[i])
		*l = g_slist_prepend(*l, g_strdup(list[i++]));
}

static const char *disconnect_reason_to_string(enum ofono_disconnect_reason r)
{
	switch (r) {
	case OFONO_DISCONNECT_REASON_LOCAL_HANGUP:
		return "local";
	case OFONO_DISCONNECT_REASON_REMOTE_HANGUP:
		return "remote";
	default:
		return "network";
	}
}

static const char *call_status_to_string(int status)
{
	switch (status) {
	case CALL_STATUS_ACTIVE:
		return "active";
	case CALL_STATUS_HELD:
		return "held";
	case CALL_STATUS_DIALING:
		return "dialing";
	case CALL_STATUS_ALERTING:
		return "alerting";
	case CALL_STATUS_INCOMING:
		return "incoming";
	case CALL_STATUS_WAITING:
		return "waiting";
	default:
		return "disconnected";
	}
}

static const char *phone_and_clip_to_string(const struct ofono_phone_number *n,
						int clip_validity)
{
	if (clip_validity == CLIP_VALIDITY_WITHHELD && !strlen(n->number))
		return "withheld";

	if (clip_validity == CLIP_VALIDITY_NOT_AVAILABLE)
		return "";

	return phone_number_to_string(n);
}

static const char *time_to_str(const time_t *t)
{
	static char buf[128];
	struct tm tm;

	strftime(buf, 127, "%Y-%m-%dT%H:%M:%S%z", localtime_r(t, &tm));
	buf[127] = '\0';

	return buf;
}

static DBusMessage *voicecall_get_properties(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	struct voicecall *v = data;
	struct ofono_call *call = v->call;
	DBusMessage *reply;
	DBusMessageIter iter;
	DBusMessageIter dict;
	const char *status;
	const char *callerid;
	const char *timestr;

	reply = dbus_message_new_method_return(msg);

	if (!reply)
		return NULL;

	status = call_status_to_string(call->status);
	callerid = phone_number_to_string(&call->phone_number);

	dbus_message_iter_init_append(reply, &iter);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					OFONO_PROPERTIES_ARRAY_SIGNATURE,
					&dict);

	ofono_dbus_dict_append(&dict, "State", DBUS_TYPE_STRING, &status);

	ofono_dbus_dict_append(&dict, "LineIdentification",
				DBUS_TYPE_STRING, &callerid);

	if (call->status == CALL_STATUS_ACTIVE ||
			call->status == CALL_STATUS_HELD ||
			(call->status == CALL_STATUS_DISCONNECTED &&
				v->start_time != 0)) {
		timestr = time_to_str(&v->start_time);

		ofono_dbus_dict_append(&dict, "StartTime", DBUS_TYPE_STRING,
					&timestr);
	}

	dbus_message_iter_close_container(&iter, &dict);

	return reply;
}

static DBusMessage *voicecall_deflect(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct voicecall *v = data;
	struct ofono_voicecall *vc = v->vc;
	struct ofono_call *call = v->call;

	struct ofono_phone_number ph;
	const char *number;

	if (call->status != CALL_STATUS_INCOMING &&
		call->status != CALL_STATUS_WAITING)
		return __ofono_error_failed(msg);

	if (!vc->driver->deflect)
		return __ofono_error_not_implemented(msg);

	if (vc->pending)
		return __ofono_error_busy(msg);

	if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &number,
					DBUS_TYPE_INVALID) == FALSE)
		return __ofono_error_invalid_args(msg);

	if (!valid_phone_number_format(number))
		return __ofono_error_invalid_format(msg);

	vc->pending = dbus_message_ref(msg);

	string_to_phone_number(number, &ph);

	vc->driver->deflect(vc, &ph, generic_callback, vc);

	return NULL;
}

static DBusMessage *voicecall_hangup(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct voicecall *v = data;
	struct ofono_voicecall *vc = v->vc;
	struct ofono_call *call = v->call;
	int num_calls;

	if (call->status == CALL_STATUS_DISCONNECTED)
		return __ofono_error_failed(msg);

	if (vc->pending)
		return __ofono_error_busy(msg);

	/* According to various specs, other than 27.007, +CHUP is used
	 * to reject an incoming call
	 */
	if (call->status == CALL_STATUS_INCOMING) {
		if (vc->driver->hangup == NULL)
			return __ofono_error_not_implemented(msg);

		vc->pending = dbus_message_ref(msg);
		vc->driver->hangup(vc, generic_callback, vc);

		return NULL;
	}

	if (call->status == CALL_STATUS_WAITING) {
		if (vc->driver->set_udub == NULL)
			return __ofono_error_not_implemented(msg);

		vc->pending = dbus_message_ref(msg);
		vc->driver->set_udub(vc, generic_callback, vc);

		return NULL;
	}

	num_calls = g_slist_length(vc->call_list);

	if (num_calls == 1 && vc->driver->hangup &&
			(call->status == CALL_STATUS_ACTIVE ||
				call->status == CALL_STATUS_DIALING ||
				call->status == CALL_STATUS_ALERTING)) {
		vc->pending = dbus_message_ref(msg);
		vc->driver->hangup(vc, generic_callback, vc);

		return NULL;
	}

	if (num_calls == 1 && vc->driver->release_all_held &&
			call->status == CALL_STATUS_HELD) {
		vc->pending = dbus_message_ref(msg);
		vc->driver->release_all_held(vc, generic_callback, vc);

		return NULL;
	}

	if (vc->driver->release_specific == NULL)
		return __ofono_error_not_implemented(msg);

	vc->pending = dbus_message_ref(msg);
	vc->driver->release_specific(vc, call->id,
					generic_callback, vc);

	return NULL;
}

static DBusMessage *voicecall_answer(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct voicecall *v = data;
	struct ofono_voicecall *vc = v->vc;
	struct ofono_call *call = v->call;

	if (call->status != CALL_STATUS_INCOMING)
		return __ofono_error_failed(msg);

	if (!vc->driver->answer)
		return __ofono_error_not_implemented(msg);

	if (vc->pending)
		return __ofono_error_busy(msg);

	vc->pending = dbus_message_ref(msg);

	vc->driver->answer(vc, generic_callback, vc);

	return NULL;
}

static GDBusMethodTable voicecall_methods[] = {
	{ "GetProperties",	"",	"a{sv}",	voicecall_get_properties },
	{ "Deflect",		"s",	"",		voicecall_deflect,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ "Hangup",		"",	"",		voicecall_hangup,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ "Answer",		"",	"",		voicecall_answer,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ }
};

static GDBusSignalTable voicecall_signals[] = {
	{ "PropertyChanged",	"sv" },
	{ "DisconnectReason",	"s" },
	{ }
};

static struct voicecall *voicecall_create(struct ofono_voicecall *vc,
						struct ofono_call *call)
{
	struct voicecall *v;

	v = g_try_new0(struct voicecall, 1);

	if (!v)
		return NULL;

	v->call = call;
	v->vc = vc;

	return v;
}

static void voicecall_destroy(gpointer userdata)
{
	struct voicecall *voicecall = (struct voicecall *)userdata;

	g_free(voicecall->call);

	g_free(voicecall);
}

static const char *voicecall_build_path(struct ofono_voicecall *vc,
					const struct ofono_call *call)
{
	static char path[256];

	snprintf(path, sizeof(path), "%s/voicecall%02d",
			__ofono_atom_get_path(vc->atom), call->id);

	return path;
}

static void voicecall_emit_disconnect_reason(struct voicecall *call,
					enum ofono_disconnect_reason reason)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path;
	const char *reason_str;

	reason_str = disconnect_reason_to_string(reason);
	path = voicecall_build_path(call->vc, call->call);

	g_dbus_emit_signal(conn, path, OFONO_VOICECALL_INTERFACE,
				"DisconnectReason",
				DBUS_TYPE_STRING, &reason_str,
				DBUS_TYPE_INVALID);
}

static void voicecall_set_call_status(struct voicecall *call,
					int status)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path;
	const char *status_str;
	int old_status;

	if (call->call->status == status)
		return;

	old_status = call->call->status;

	call->call->status = status;

	status_str = call_status_to_string(status);
	path = voicecall_build_path(call->vc, call->call);

	ofono_dbus_signal_property_changed(conn, path,
						OFONO_VOICECALL_INTERFACE,
						"State", DBUS_TYPE_STRING,
						&status_str);

	if (status == CALL_STATUS_ACTIVE &&
		(old_status == CALL_STATUS_INCOMING ||
			old_status == CALL_STATUS_DIALING ||
			old_status == CALL_STATUS_ALERTING ||
			old_status == CALL_STATUS_WAITING)) {
		const char *timestr;

		call->start_time = time(NULL);
		timestr = time_to_str(&call->start_time);

		ofono_dbus_signal_property_changed(conn, path,
						OFONO_VOICECALL_INTERFACE,
						"StartTime", DBUS_TYPE_STRING,
						&timestr);
	}
}

static void voicecall_set_call_lineid(struct voicecall *v,
					const struct ofono_phone_number *ph,
					int clip_validity)
{
	struct ofono_call *call = v->call;
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path;
	const char *lineid_str;

	if (!strcmp(call->phone_number.number, ph->number) &&
		call->phone_number.type == ph->type &&
		call->clip_validity == clip_validity)
		return;

	/* Two cases: We get an incoming call with CLIP factored in, or
	 * CLIP comes in later as a separate event
	 * For COLP only the phone number should be checked, it can come
	 * in with the initial call event or later as a separate event */

	/* For plugins that don't keep state, ignore */
	if (call->clip_validity == CLIP_VALIDITY_VALID &&
		clip_validity == CLIP_VALIDITY_NOT_AVAILABLE)
		return;

	strcpy(call->phone_number.number, ph->number);
	call->clip_validity = clip_validity;
	call->phone_number.type = ph->type;

	path = voicecall_build_path(v->vc, call);

	if (call->direction == CALL_DIRECTION_MOBILE_TERMINATED)
		lineid_str = phone_and_clip_to_string(ph, clip_validity);
	else
		lineid_str = phone_number_to_string(ph);

	ofono_dbus_signal_property_changed(conn, path,
						OFONO_VOICECALL_INTERFACE,
						"LineIdentification",
						DBUS_TYPE_STRING, &lineid_str);
}

static gboolean voicecall_dbus_register(struct voicecall *v)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path;

	if (!v)
		return FALSE;

	path = voicecall_build_path(v->vc, v->call);

	if (!g_dbus_register_interface(conn, path, OFONO_VOICECALL_INTERFACE,
					voicecall_methods,
					voicecall_signals,
					NULL, v, voicecall_destroy)) {
		ofono_error("Could not register VoiceCall %s", path);
		voicecall_destroy(v);

		return FALSE;
	}

	return TRUE;
}

static gboolean voicecall_dbus_unregister(struct ofono_voicecall *vc,
						struct voicecall *v)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path = voicecall_build_path(vc, v->call);

	return g_dbus_unregister_interface(conn, path,
						OFONO_VOICECALL_INTERFACE);
}


static int voicecalls_path_list(struct ofono_voicecall *vc, GSList *call_list,
				char ***objlist)
{
	GSList *l;
	int i;
	struct voicecall *v;

	*objlist = g_new0(char *, g_slist_length(call_list) + 1);

	if (*objlist == NULL)
		return -1;

	for (i = 0, l = call_list; l; l = l->next, i++) {
		v = l->data;
		(*objlist)[i] = g_strdup(voicecall_build_path(vc, v->call));
	}

	return 0;
}

static gboolean voicecalls_have_active(struct ofono_voicecall *vc)
{
	GSList *l;
	struct voicecall *v;

	for (l = vc->call_list; l; l = l->next) {
		v = l->data;

		if (v->call->status == CALL_STATUS_ACTIVE ||
			v->call->status == CALL_STATUS_DIALING ||
			v->call->status == CALL_STATUS_ALERTING)
			return TRUE;
	}

	return FALSE;
}

static gboolean voicecalls_can_dtmf(struct ofono_voicecall *vc)
{
	GSList *l;
	struct voicecall *v;

	for (l = vc->call_list; l; l = l->next) {
		v = l->data;

		if (v->call->status == CALL_STATUS_ACTIVE)
			return TRUE;

		/* Connected for 2nd stage dialing */
		if (v->call->status == CALL_STATUS_ALERTING)
			return TRUE;
	}

	return FALSE;
}

static gboolean voicecalls_have_with_status(struct ofono_voicecall *vc, int status)
{
	GSList *l;
	struct voicecall *v;

	for (l = vc->call_list; l; l = l->next) {
		v = l->data;

		if (v->call->status == status)
			return TRUE;
	}

	return FALSE;
}

static gboolean voicecalls_have_held(struct ofono_voicecall *vc)
{
	return voicecalls_have_with_status(vc, CALL_STATUS_HELD);
}

static int voicecalls_num_with_status(struct ofono_voicecall *vc,
					int status)
{
	GSList *l;
	struct voicecall *v;
	int num = 0;

	for (l = vc->call_list; l; l = l->next) {
		v = l->data;

		if (v->call->status == status)
			num += 1;
	}

	return num;
}

static int voicecalls_num_active(struct ofono_voicecall *vc)
{
	return voicecalls_num_with_status(vc, CALL_STATUS_ACTIVE);
}

static int voicecalls_num_held(struct ofono_voicecall *vc)
{
	return voicecalls_num_with_status(vc, CALL_STATUS_HELD);
}

static int voicecalls_num_connecting(struct ofono_voicecall *vc)
{
	int r = 0;

	r += voicecalls_num_with_status(vc, CALL_STATUS_DIALING);
	r += voicecalls_num_with_status(vc, CALL_STATUS_ALERTING);

	return r;
}

static GSList *voicecalls_held_list(struct ofono_voicecall *vc)
{
	GSList *l;
	GSList *r = NULL;
	struct voicecall *v;

	for (l = vc->call_list; l; l = l->next) {
		v = l->data;

		if (v->call->status == CALL_STATUS_HELD)
			r = g_slist_prepend(r, v);
	}

	if (r)
		r = g_slist_reverse(r);

	return r;
}

/* Intended to be used for multiparty, which cannot be incoming,
 * alerting or dialing */
static GSList *voicecalls_active_list(struct ofono_voicecall *vc)
{
	GSList *l;
	GSList *r = NULL;
	struct voicecall *v;

	for (l = vc->call_list; l; l = l->next) {
		v = l->data;

		if (v->call->status == CALL_STATUS_ACTIVE)
			r = g_slist_prepend(r, v);
	}

	if (r)
		r = g_slist_reverse(r);

	return r;
}

static gboolean voicecalls_have_waiting(struct ofono_voicecall *vc)
{
	return voicecalls_have_with_status(vc, CALL_STATUS_WAITING);
}

static gboolean voicecalls_have_incoming(struct ofono_voicecall *vc)
{
	return voicecalls_have_with_status(vc, CALL_STATUS_INCOMING);
}

static gboolean real_emit_call_list_changed(void *data)
{
	struct ofono_voicecall *vc = data;
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path = __ofono_atom_get_path(vc->atom);
	char **objpath_list;

	voicecalls_path_list(vc, vc->call_list, &objpath_list);

	ofono_dbus_signal_array_property_changed(conn, path,
					OFONO_VOICECALL_MANAGER_INTERFACE,
					"Calls", DBUS_TYPE_OBJECT_PATH,
					&objpath_list);

	g_strfreev(objpath_list);

	vc->emit_calls_source = 0;

	return FALSE;
}

static void emit_call_list_changed(struct ofono_voicecall *vc)
{
	if (vc->emit_calls_source == 0)
		vc->emit_calls_source =
			g_timeout_add(0, real_emit_call_list_changed, vc);
}

static gboolean real_emit_multiparty_call_list_changed(void *data)
{
	struct ofono_voicecall *vc = data;
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path = __ofono_atom_get_path(vc->atom);
	char **objpath_list;

	voicecalls_path_list(vc, vc->multiparty_list, &objpath_list);

	ofono_dbus_signal_array_property_changed(conn, path,
					OFONO_VOICECALL_MANAGER_INTERFACE,
					"MultipartyCalls",
					DBUS_TYPE_OBJECT_PATH, &objpath_list);

	g_strfreev(objpath_list);

	vc->emit_multi_source = 0;

	return FALSE;
}

static void emit_multiparty_call_list_changed(struct ofono_voicecall *vc)
{
	if (vc->emit_multi_source == 0)
		vc->emit_multi_source = g_timeout_add(0,
				real_emit_multiparty_call_list_changed, vc);
}

static void voicecalls_release_queue(struct ofono_voicecall *vc, GSList *calls)
{
	GSList *l;

	g_slist_free(vc->release_list);
	vc->release_list = NULL;

	for (l = calls; l; l = l->next)
		vc->release_list = g_slist_prepend(vc->release_list, l->data);
}

static void voicecalls_release_next(struct ofono_voicecall *vc)
{
	struct voicecall *call;

	if (!vc->release_list)
		return;

	call = vc->release_list->data;

	vc->release_list = g_slist_remove(vc->release_list, call);

	vc->driver->release_specific(vc, call->call->id,
						multirelease_callback, vc);
}

static DBusMessage *manager_get_properties(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	struct ofono_voicecall *vc = data;
	DBusMessage *reply;
	DBusMessageIter iter;
	DBusMessageIter dict;
	int i;
	GSList *l;
	char **callobj_list;
	char **list;

	reply = dbus_message_new_method_return(msg);

	if (!reply)
		return NULL;

	dbus_message_iter_init_append(reply, &iter);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					OFONO_PROPERTIES_ARRAY_SIGNATURE,
					&dict);

	voicecalls_path_list(vc, vc->call_list, &callobj_list);

	ofono_dbus_dict_append_array(&dict, "Calls", DBUS_TYPE_OBJECT_PATH,
				&callobj_list);

	g_strfreev(callobj_list);

	voicecalls_path_list(vc, vc->multiparty_list, &callobj_list);

	ofono_dbus_dict_append_array(&dict, "MultipartyCalls",
					DBUS_TYPE_OBJECT_PATH, &callobj_list);

	g_strfreev(callobj_list);

	/* property EmergencyNumbers */
	list = g_new0(char *, g_slist_length(vc->en_list) + 1);

	for (i = 0, l = vc->en_list; l; l = l->next, i++)
		list[i] = g_strdup(l->data);

	ofono_dbus_dict_append_array(&dict, "EmergencyNumbers",
					DBUS_TYPE_STRING, &list);
	g_strfreev(list);

	dbus_message_iter_close_container(&iter, &dict);

	return reply;
}

static ofono_bool_t clir_string_to_clir(const char *clirstr,
					enum ofono_clir_option *clir)
{
	if (strlen(clirstr) == 0 || !strcmp(clirstr, "default")) {
		*clir = OFONO_CLIR_OPTION_DEFAULT;
		return TRUE;
	} else if (!strcmp(clirstr, "disabled")) {
		*clir = OFONO_CLIR_OPTION_SUPPRESSION;
		return TRUE;
	} else if (!strcmp(clirstr, "enabled")) {
		*clir = OFONO_CLIR_OPTION_INVOCATION;
		return TRUE;
	} else {
		return FALSE;
	}
}

static struct ofono_call *synthesize_outgoing_call(struct ofono_voicecall *vc,
						DBusMessage *msg)
{
	struct ofono_modem *modem = __ofono_atom_get_modem(vc->atom);
	const char *number;
	struct ofono_call *call;

	call = g_try_new0(struct ofono_call, 1);

	if (!call)
		return call;

	call->id = __ofono_modem_callid_next(modem);

	if (call->id == 0) {
		ofono_error("Failed to alloc callid, too many calls");
		g_free(call);
		return NULL;
	}

	__ofono_modem_callid_hold(modem, call->id);

	if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &number,
				DBUS_TYPE_INVALID) == FALSE)
		number = "";
	else
		string_to_phone_number(number, &call->phone_number);

	call->direction = CALL_DIRECTION_MOBILE_ORIGINATED;
	call->status = CALL_STATUS_DIALING;
	call->clip_validity = CLIP_VALIDITY_VALID;

	return call;
}

static void dial_callback(const struct ofono_error *error, void *data)
{
	struct ofono_voicecall *vc = data;
	DBusMessage *reply;
	GSList *l;
	struct ofono_call *call;
	const char *path;
	gboolean need_to_emit = FALSE;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Dial callback returned error: %s",
			telephony_error_to_str(error));
		reply = __ofono_error_failed(vc->pending);
		__ofono_dbus_pending_reply(&vc->pending, reply);
		return;
	}

	/* Two things can happen, the call notification arrived before dial
	 * callback or dial callback was first.	Handle here */
	for (l = vc->call_list; l; l = l->next) {
		struct voicecall *v = l->data;

		if (v->call->status == CALL_STATUS_DIALING ||
				v->call->status == CALL_STATUS_ALERTING ||
				v->call->status == CALL_STATUS_ACTIVE)
			break;
	}

	if (!l) {
		struct voicecall *v;
		call = synthesize_outgoing_call(vc, vc->pending);

		if (!call) {
			__ofono_dbus_pending_reply(&vc->pending,
				__ofono_error_failed(vc->pending));
			return;
		}

		v = voicecall_create(vc, call);

		if (!v) {
			__ofono_dbus_pending_reply(&vc->pending,
				__ofono_error_failed(vc->pending));
			return;
		}

		v->detect_time = time(NULL);

		DBG("Registering new call: %d", call->id);
		voicecall_dbus_register(v);

		vc->call_list = g_slist_insert_sorted(vc->call_list, v,
					call_compare);

		need_to_emit = TRUE;
	} else {
		struct voicecall *v = l->data;

		call = v->call;
	}

	reply = dbus_message_new_method_return(vc->pending);
	path = voicecall_build_path(vc, call);
	dbus_message_append_args(reply, DBUS_TYPE_OBJECT_PATH, &path,
					DBUS_TYPE_INVALID);
	__ofono_dbus_pending_reply(&vc->pending, reply);

	if (need_to_emit)
		emit_call_list_changed(vc);
}

static DBusMessage *manager_dial(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct ofono_voicecall *vc = data;
	const char *number;
	struct ofono_phone_number ph;
	const char *clirstr;
	enum ofono_clir_option clir;

	if (vc->pending)
		return __ofono_error_busy(msg);

	if (g_slist_length(vc->call_list) >= MAX_VOICE_CALLS)
		return __ofono_error_failed(msg);

	if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &number,
					DBUS_TYPE_STRING, &clirstr,
					DBUS_TYPE_INVALID) == FALSE)
		return __ofono_error_invalid_args(msg);

	if (!valid_phone_number_format(number))
		return __ofono_error_invalid_format(msg);

	if (clir_string_to_clir(clirstr, &clir) == FALSE)
		return __ofono_error_invalid_format(msg);

	if (!vc->driver->dial)
		return __ofono_error_not_implemented(msg);

	if (voicecalls_have_incoming(vc))
		return __ofono_error_failed(msg);

	/* We can't have two dialing/alerting calls, reject outright */
	if (voicecalls_num_connecting(vc) > 0)
		return __ofono_error_failed(msg);

	if (voicecalls_have_active(vc) && voicecalls_have_held(vc))
		return __ofono_error_failed(msg);

	vc->pending = dbus_message_ref(msg);

	string_to_phone_number(number, &ph);

	vc->driver->dial(vc, &ph, clir, OFONO_CUG_OPTION_DEFAULT,
				dial_callback, vc);

	return NULL;
}

static DBusMessage *manager_transfer(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct ofono_voicecall *vc = data;
	int numactive;
	int numheld;

	if (vc->pending)
		return __ofono_error_busy(msg);

	numactive = voicecalls_num_active(vc);

	/* According to 22.091 section 5.8, the network has the option of
	 * implementing the call transfer operation for a call that is
	 * still dialing/alerting.
	 */
	numactive += voicecalls_num_connecting(vc);

	numheld = voicecalls_num_held(vc);

	if ((numactive != 1) && (numheld != 1))
		return __ofono_error_failed(msg);

	if (!vc->driver->transfer)
		return __ofono_error_not_implemented(msg);

	vc->pending = dbus_message_ref(msg);

	vc->driver->transfer(vc, generic_callback, vc);

	return NULL;
}

static DBusMessage *manager_swap_without_accept(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	struct ofono_voicecall *vc = data;

	if (vc->pending)
		return __ofono_error_busy(msg);

	vc->pending = dbus_message_ref(msg);

	vc->driver->swap_without_accept(vc, generic_callback, vc);

	return NULL;
}


static DBusMessage *manager_swap_calls(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct ofono_voicecall *vc = data;

	if (vc->driver->swap_without_accept)
		return manager_swap_without_accept(conn, msg, data);

	if (vc->pending)
		return __ofono_error_busy(msg);

	if (voicecalls_have_waiting(vc))
		return __ofono_error_failed(msg);

	if (!vc->driver->hold_all_active)
		return __ofono_error_not_implemented(msg);

	vc->pending = dbus_message_ref(msg);

	vc->driver->hold_all_active(vc, generic_callback, vc);

	return NULL;
}

static DBusMessage *manager_release_and_answer(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	struct ofono_voicecall *vc = data;

	if (vc->pending)
		return __ofono_error_busy(msg);

	if (!voicecalls_have_waiting(vc))
		return __ofono_error_failed(msg);

	if (!vc->driver->release_all_active)
		return __ofono_error_not_implemented(msg);

	vc->pending = dbus_message_ref(msg);

	vc->driver->release_all_active(vc, generic_callback, vc);

	return NULL;
}

static DBusMessage *manager_hold_and_answer(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	struct ofono_voicecall *vc = data;

	if (vc->pending)
		return __ofono_error_busy(msg);

	if (voicecalls_have_waiting(vc) == FALSE)
		return __ofono_error_failed(msg);

	/* We have waiting call and both an active and held call.  According
	 * to 22.030 we cannot use CHLD=2 in this situation.
	 */
	if (voicecalls_have_active(vc) && voicecalls_have_held(vc))
		return __ofono_error_failed(msg);

	if (!vc->driver->hold_all_active)
		return __ofono_error_not_implemented(msg);

	vc->pending = dbus_message_ref(msg);

	vc->driver->hold_all_active(vc, generic_callback, vc);

	return NULL;
}

static DBusMessage *manager_hangup_all(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct ofono_voicecall *vc = data;

	if (vc->pending)
		return __ofono_error_busy(msg);

	if (!vc->driver->release_specific)
		return __ofono_error_not_implemented(msg);

	if (vc->call_list == NULL) {
		DBusMessage *reply = dbus_message_new_method_return(msg);
		return reply;
	}

	vc->flags |= VOICECALLS_FLAG_MULTI_RELEASE;

	vc->pending = dbus_message_ref(msg);

	voicecalls_release_queue(vc, vc->call_list);
	voicecalls_release_next(vc);

	return NULL;
}

static void multiparty_callback_common(struct ofono_voicecall *vc,
					DBusMessage *reply)
{
	DBusMessageIter iter;
	DBusMessageIter array_iter;
	char **objpath_list;
	int i;

	voicecalls_path_list(vc, vc->multiparty_list, &objpath_list);

	dbus_message_iter_init_append(reply, &iter);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
		DBUS_TYPE_OBJECT_PATH_AS_STRING, &array_iter);

	for (i = 0; objpath_list[i]; i++)
		dbus_message_iter_append_basic(&array_iter,
			DBUS_TYPE_OBJECT_PATH, &objpath_list[i]);

	dbus_message_iter_close_container(&iter, &array_iter);
}

static void private_chat_callback(const struct ofono_error *error, void *data)
{
	struct ofono_voicecall *vc = data;
	DBusMessage *reply;
	const char *callpath;
	const char *c;
	int id;
	GSList *l;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("command failed with error: %s",
				telephony_error_to_str(error));
		__ofono_dbus_pending_reply(&vc->pending,
					__ofono_error_failed(vc->pending));
		return;
	}

	dbus_message_get_args(vc->pending, NULL,
				DBUS_TYPE_OBJECT_PATH, &callpath,
				DBUS_TYPE_INVALID);

	c = strrchr(callpath, '/');
	sscanf(c, "/voicecall%2u", &id);

	l = g_slist_find_custom(vc->multiparty_list, GINT_TO_POINTER(id),
				call_compare_by_id);

	if (l) {
		vc->multiparty_list =
			g_slist_remove(vc->multiparty_list, l->data);

		if (g_slist_length(vc->multiparty_list) < 2) {
			g_slist_free(vc->multiparty_list);
			vc->multiparty_list = 0;
		}
	}

	reply = dbus_message_new_method_return(vc->pending);
	multiparty_callback_common(vc, reply);
	__ofono_dbus_pending_reply(&vc->pending, reply);

	emit_multiparty_call_list_changed(vc);
}

static DBusMessage *multiparty_private_chat(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	struct ofono_voicecall *vc = data;
	const char *path = __ofono_atom_get_path(vc->atom);
	const char *callpath;
	const char *c;
	unsigned int id;
	GSList *l;

	if (vc->pending)
		return __ofono_error_busy(msg);

	if (dbus_message_get_args(msg, NULL, DBUS_TYPE_OBJECT_PATH, &callpath,
					DBUS_TYPE_INVALID) == FALSE)
		return __ofono_error_invalid_args(msg);

	if (strlen(callpath) == 0)
		return __ofono_error_invalid_format(msg);

	c = strrchr(callpath, '/');

	if (!c || strncmp(path, callpath, c-callpath))
		return __ofono_error_not_found(msg);

	if (!sscanf(c, "/voicecall%2u", &id))
		return __ofono_error_not_found(msg);

	for (l = vc->multiparty_list; l; l = l->next) {
		struct voicecall *v = l->data;
		if (v->call->id == id)
			break;
	}

	if (!l)
		return __ofono_error_not_found(msg);

	/* If we found id on the list of multiparty calls, then by definition
	 * the multiparty call exists.	Only thing to check is whether we have
	 * held calls
	 */
	if (voicecalls_have_held(vc))
		return __ofono_error_failed(msg);

	if (!vc->driver->private_chat)
		return __ofono_error_not_implemented(msg);

	vc->pending = dbus_message_ref(msg);

	vc->driver->private_chat(vc, id, private_chat_callback, vc);

	return NULL;
}

static void multiparty_create_callback(const struct ofono_error *error, void *data)
{
	struct ofono_voicecall *vc = data;
	DBusMessage *reply;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("command failed with error: %s",
				telephony_error_to_str(error));
		__ofono_dbus_pending_reply(&vc->pending,
					__ofono_error_failed(vc->pending));
		return;
	}

	/* We just created a multiparty call, gather all held
	 * active calls and add them to the multiparty list
	 */
	if (vc->multiparty_list) {
		g_slist_free(vc->multiparty_list);
		vc->multiparty_list = 0;
	}

	vc->multiparty_list = g_slist_concat(vc->multiparty_list,
						voicecalls_held_list(vc));

	vc->multiparty_list = g_slist_concat(vc->multiparty_list,
						voicecalls_active_list(vc));

	vc->multiparty_list = g_slist_sort(vc->multiparty_list,
						call_compare);

	if (g_slist_length(vc->multiparty_list) < 2) {
		ofono_error("Created multiparty call, but size is less than 2"
				" panic!");

		__ofono_dbus_pending_reply(&vc->pending,
					__ofono_error_failed(vc->pending));
		return;
	}

	reply = dbus_message_new_method_return(vc->pending);
	multiparty_callback_common(vc, reply);
	__ofono_dbus_pending_reply(&vc->pending, reply);

	emit_multiparty_call_list_changed(vc);
}

static DBusMessage *multiparty_create(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	struct ofono_voicecall *vc = data;

	if (vc->pending)
		return __ofono_error_busy(msg);

	if (!voicecalls_have_held(vc) || !voicecalls_have_active(vc))
		return __ofono_error_failed(msg);

	if (!vc->driver->create_multiparty)
		return __ofono_error_not_implemented(msg);

	vc->pending = dbus_message_ref(msg);

	vc->driver->create_multiparty(vc, multiparty_create_callback, vc);

	return NULL;
}

static DBusMessage *multiparty_hangup(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	struct ofono_voicecall *vc = data;

	if (vc->pending)
		return __ofono_error_busy(msg);

	if (!vc->driver->release_specific)
		return __ofono_error_not_implemented(msg);

	if (!vc->driver->release_all_held)
		return __ofono_error_not_implemented(msg);

	if (!vc->driver->release_all_active)
		return __ofono_error_not_implemented(msg);

	if (vc->multiparty_list == NULL) {
		DBusMessage *reply = dbus_message_new_method_return(msg);
		return reply;
	}

	vc->pending = dbus_message_ref(msg);

	/* We don't have waiting calls, as we can't use +CHLD to release */
	if (!voicecalls_have_waiting(vc)) {
		struct voicecall *v = vc->multiparty_list->data;

		if (v->call->status == CALL_STATUS_HELD) {
			vc->driver->release_all_held(vc, generic_callback,
							vc);
			goto out;
		}

		/* Multiparty is currently active, if we have held calls
		 * we shouldn't use release_all_active here since this also
		 * has the side-effect of activating held calls
		 */
		if (!voicecalls_have_held(vc)) {
			vc->driver->release_all_active(vc, generic_callback,
								vc);
			goto out;
		}
	}

	/* Fall back to the old-fashioned way */
	vc->flags |= VOICECALLS_FLAG_MULTI_RELEASE;
	voicecalls_release_queue(vc, vc->multiparty_list);
	voicecalls_release_next(vc);

out:
	return NULL;
}

static DBusMessage *manager_tone(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct ofono_voicecall *vc = data;
	const char *in_tones;
	char *tones;
	int i, len;

	if (vc->pending)
		return __ofono_error_busy(msg);

	if (!vc->driver->send_tones)
		return __ofono_error_not_implemented(msg);

	/* Send DTMFs only if we have at least one connected call */
	if (!voicecalls_can_dtmf(vc))
		return __ofono_error_failed(msg);

	if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &in_tones,
					DBUS_TYPE_INVALID) == FALSE)
		return __ofono_error_invalid_args(msg);

	len = strlen(in_tones);

	if (len == 0)
		return __ofono_error_invalid_format(msg);

	tones = g_ascii_strup(in_tones, len);

	/* Tones can be 0-9, *, #, A-D according to 27.007 C.2.11 */
	for (i = 0; i < len; i++) {
		if (g_ascii_isdigit(tones[i]) ||
			tones[i] == '*' || tones[i] == '#' ||
				(tones[i] >= 'A' && tones[i] <= 'D'))
			continue;

		g_free(tones);
		return __ofono_error_invalid_format(msg);
	}

	vc->pending = dbus_message_ref(msg);

	vc->driver->send_tones(vc, tones, generic_callback, vc);

	g_free(tones);

	return NULL;
}

static GDBusMethodTable manager_methods[] = {
	{ "GetProperties",	"",	"a{sv}",	manager_get_properties },
	{ "Dial",		"ss",	"o",		manager_dial,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ "Transfer",		"",	"",		manager_transfer,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ "SwapCalls",		"",	"",		manager_swap_calls,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ "ReleaseAndAnswer",	"",	"",		manager_release_and_answer,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ "HoldAndAnswer",	"",	"",		manager_hold_and_answer,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ "HangupAll",		"",	"",		manager_hangup_all,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ "PrivateChat",	"o",	"ao",		multiparty_private_chat,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ "CreateMultiparty",	"",	"ao",		multiparty_create,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ "HangupMultiparty",	"",	"",		multiparty_hangup,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ "SendTones",		"s",	"",		manager_tone,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ }
};

static GDBusSignalTable manager_signals[] = {
	{ "PropertyChanged",	"sv" },
	{ }
};

void ofono_voicecall_disconnected(struct ofono_voicecall *vc, int id,
				enum ofono_disconnect_reason reason,
				const struct ofono_error *error)
{
	struct ofono_modem *modem = __ofono_atom_get_modem(vc->atom);
	GSList *l;
	struct voicecall *call;
	time_t ts;
	enum call_status prev_status;

	DBG("Got disconnection event for id: %d, reason: %d", id, reason);

	__ofono_modem_callid_release(modem, id);

	l = g_slist_find_custom(vc->call_list, GUINT_TO_POINTER(id),
				call_compare_by_id);

	if (!l) {
		ofono_error("Plugin notified us of call disconnect for"
				" unknown call");
		return;
	}

	call = l->data;

	ts = time(NULL);
	prev_status = call->call->status;

	l = g_slist_find_custom(vc->multiparty_list, GUINT_TO_POINTER(id),
				call_compare_by_id);

	if (l) {
		vc->multiparty_list =
			g_slist_remove(vc->multiparty_list, call);

		if (vc->multiparty_list->next == NULL) { /* Size == 1 */
			g_slist_free(vc->multiparty_list);
			vc->multiparty_list = 0;
		}

		emit_multiparty_call_list_changed(vc);
	}

	vc->release_list = g_slist_remove(vc->release_list, call);

	if (reason != OFONO_DISCONNECT_REASON_UNKNOWN)
		voicecall_emit_disconnect_reason(call, reason);

	voicecall_set_call_status(call, CALL_STATUS_DISCONNECTED);

	if (prev_status == CALL_STATUS_INCOMING ||
			prev_status == CALL_STATUS_WAITING)
		__ofono_history_call_missed(modem, call->call, ts);
	else
		__ofono_history_call_ended(modem, call->call,
						call->detect_time, ts);

	voicecall_dbus_unregister(vc, call);

	vc->call_list = g_slist_remove(vc->call_list, call);

	emit_call_list_changed(vc);
}

void ofono_voicecall_notify(struct ofono_voicecall *vc,
				const struct ofono_call *call)
{
	struct ofono_modem *modem = __ofono_atom_get_modem(vc->atom);
	GSList *l;
	struct voicecall *v = NULL;
	struct ofono_call *newcall;

	DBG("Got a voicecall event, status: %d, id: %u, number: %s",
			call->status, call->id, call->phone_number.number);

	l = g_slist_find_custom(vc->call_list, GUINT_TO_POINTER(call->id),
				call_compare_by_id);

	if (l) {
		DBG("Found call with id: %d\n", call->id);
		voicecall_set_call_status(l->data, call->status);
		voicecall_set_call_lineid(l->data, &call->phone_number,
						call->clip_validity);

		return;
	}

	DBG("Did not find a call with id: %d\n", call->id);

	__ofono_modem_callid_hold(modem, call->id);

	newcall = g_memdup(call, sizeof(struct ofono_call));

	if (!newcall) {
		ofono_error("Unable to allocate call");
		goto error;
	}

	v = voicecall_create(vc, newcall);

	if (!v) {
		ofono_error("Unable to allocate voicecall_data");
		goto error;
	}

	v->detect_time = time(NULL);

	if (!voicecall_dbus_register(v)) {
		ofono_error("Unable to register voice call");
		goto error;
	}

	vc->call_list = g_slist_insert_sorted(vc->call_list, v, call_compare);

	emit_call_list_changed(vc);

	return;

error:
	if (newcall)
		g_free(newcall);

	if (v)
		g_free(v);
}

static void generic_callback(const struct ofono_error *error, void *data)
{
	struct ofono_voicecall *vc = data;
	DBusMessage *reply;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR)
		DBG("command failed with error: %s",
				telephony_error_to_str(error));

	if (error->type == OFONO_ERROR_TYPE_NO_ERROR)
		reply = dbus_message_new_method_return(vc->pending);
	else
		reply = __ofono_error_failed(vc->pending);

	__ofono_dbus_pending_reply(&vc->pending, reply);
}

static void multirelease_callback(const struct ofono_error *error, void *data)
{
	struct ofono_voicecall *vc = data;
	DBusMessage *reply;

	if (vc->release_list != NULL) {
		voicecalls_release_next(vc);
		return;
	}

	vc->flags &= ~VOICECALLS_FLAG_MULTI_RELEASE;

	reply = dbus_message_new_method_return(vc->pending);
	__ofono_dbus_pending_reply(&vc->pending, reply);
}

static void emit_en_list_changed(struct ofono_voicecall *vc)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path = __ofono_atom_get_path(vc->atom);
	char **list;
	GSList *l;
	int i;

	list = g_new0(char *, g_slist_length(vc->en_list) + 1);
	for (i = 0, l = vc->en_list; l; l = l->next, i++)
		list[i] = g_strdup(l->data);

	ofono_dbus_signal_array_property_changed(conn, path,
				OFONO_VOICECALL_MANAGER_INTERFACE,
				"EmergencyNumbers", DBUS_TYPE_STRING, &list);

	g_strfreev(list);
}

static void set_new_ecc(struct ofono_voicecall *vc)
{
	int i = 0;

	g_slist_foreach(vc->en_list, (GFunc)g_free, NULL);
	g_slist_free(vc->en_list);
	vc->en_list = NULL;

	vc->en_list = vc->new_en_list;
	vc->new_en_list = NULL;

	while (default_en_list[i]) {
		GSList *l;

		for (l = vc->en_list; l; l = l->next)
			if (!strcmp(l->data, default_en_list[i]))
				break;

		if (l == NULL)
			vc->en_list = g_slist_prepend(vc->en_list,
						g_strdup(default_en_list[i]));

		i++;
	}

	vc->en_list = g_slist_reverse(vc->en_list);
	emit_en_list_changed(vc);
}

static void ecc_g2_read_cb(int ok, int total_length, int record,
				const unsigned char *data,
				int record_length, void *userdata)
{
	struct ofono_voicecall *vc = userdata;
	char en[7];

	DBG("%d", ok);

	if (!ok)
		return;

	if (total_length < 3) {
		ofono_error("Unable to read emergency numbers from SIM");
		return;
	}

	total_length /= 3;
	while (total_length--) {
		extract_bcd_number(data, 3, en);
		data += 3;

		if (en[0] != '\0')
			vc->new_en_list = g_slist_prepend(vc->new_en_list,
								g_strdup(en));
	}

	if (vc->new_en_list == NULL)
		return;

	set_new_ecc(vc);
}

static void ecc_g3_read_cb(int ok, int total_length, int record,
				const unsigned char *data,
				int record_length, void *userdata)
{
	struct ofono_voicecall *vc = userdata;
	int total;
	char en[7];

	DBG("%d", ok);

	if (!ok)
		goto check;

	if (record_length < 4 || total_length < record_length) {
		ofono_error("Unable to read emergency numbers from SIM");
		return;
	}

	total = total_length / record_length;
	extract_bcd_number(data, 3, en);

	if (en[0] != '\0')
		vc->new_en_list = g_slist_prepend(vc->new_en_list,
							g_strdup(en));

	if (record != total)
		return;

check:
	if (vc->new_en_list == NULL)
		return;

	set_new_ecc(vc);
}

int ofono_voicecall_driver_register(const struct ofono_voicecall_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	if (d->probe == NULL)
		return -EINVAL;

	g_drivers = g_slist_prepend(g_drivers, (void *)d);

	return 0;
}

void ofono_voicecall_driver_unregister(const struct ofono_voicecall_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	g_drivers = g_slist_remove(g_drivers, (void *)d);
}

static void voicecall_unregister(struct ofono_atom *atom)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	struct ofono_voicecall *vc = __ofono_atom_get_data(atom);
	struct ofono_modem *modem = __ofono_atom_get_modem(atom);
	const char *path = __ofono_atom_get_path(atom);
	GSList *l;

	if (vc->sim_watch) {
		__ofono_modem_remove_atom_watch(modem, vc->sim_watch);
		vc->sim_watch = 0;
	}

	if (vc->emit_calls_source) {
		g_source_remove(vc->emit_calls_source);
		vc->emit_calls_source = 0;
	}

	if (vc->emit_multi_source) {
		g_source_remove(vc->emit_multi_source);
		vc->emit_multi_source = 0;
	}

	for (l = vc->call_list; l; l = l->next)
		voicecall_dbus_unregister(vc, l->data);

	g_slist_free(vc->call_list);

	ofono_modem_remove_interface(modem, OFONO_VOICECALL_MANAGER_INTERFACE);
	g_dbus_unregister_interface(conn, path,
					OFONO_VOICECALL_MANAGER_INTERFACE);
}

static void voicecall_remove(struct ofono_atom *atom)
{
	struct ofono_voicecall *vc = __ofono_atom_get_data(atom);

	DBG("atom: %p", atom);

	if (vc == NULL)
		return;

	if (vc->driver && vc->driver->remove)
		vc->driver->remove(vc);

	if (vc->en_list) {
		g_slist_foreach(vc->en_list, (GFunc)g_free, NULL);
		g_slist_free(vc->en_list);
		vc->en_list = NULL;
	}

	if (vc->new_en_list) {
		g_slist_foreach(vc->new_en_list, (GFunc)g_free, NULL);
		g_slist_free(vc->new_en_list);
		vc->new_en_list = NULL;
	}

	if (vc->sim_state_watch) {
		ofono_sim_remove_state_watch(vc->sim, vc->sim_state_watch);
		vc->sim_state_watch = 0;
		vc->sim = NULL;
	}

	g_free(vc);
}

struct ofono_voicecall *ofono_voicecall_create(struct ofono_modem *modem,
						unsigned int vendor,
						const char *driver,
						void *data)
{
	struct ofono_voicecall *vc;
	GSList *l;

	if (driver == NULL)
		return NULL;

	vc = g_try_new0(struct ofono_voicecall, 1);

	if (vc == NULL)
		return NULL;

	vc->atom = __ofono_modem_add_atom(modem, OFONO_ATOM_TYPE_VOICECALL,
						voicecall_remove, vc);

	for (l = g_drivers; l; l = l->next) {
		const struct ofono_voicecall_driver *drv = l->data;

		if (g_strcmp0(drv->name, driver))
			continue;

		if (drv->probe(vc, vendor, data) < 0)
			continue;

		vc->driver = drv;
		break;
	}

	return vc;
}

static void sim_state_watch(void *user, enum ofono_sim_state new_state)
{
	struct ofono_voicecall *vc = user;

	switch (new_state) {
	case OFONO_SIM_STATE_INSERTED:
		/* Try both formats, only one or none will work */
		ofono_sim_read(vc->sim, SIM_EFECC_FILEID,
				OFONO_SIM_FILE_STRUCTURE_TRANSPARENT,
				ecc_g2_read_cb, vc);
		ofono_sim_read(vc->sim, SIM_EFECC_FILEID,
				OFONO_SIM_FILE_STRUCTURE_FIXED,
				ecc_g3_read_cb, vc);
		break;
	case OFONO_SIM_STATE_NOT_PRESENT:
		/* TODO: Must release all non-emergency calls */

		/*
		 * Free the currently being read EN list, just in case the
		 * SIM is removed when we're still reading them
		 */
		if (vc->new_en_list) {
			g_slist_foreach(vc->new_en_list, (GFunc) g_free, NULL);
			g_slist_free(vc->new_en_list);
			vc->new_en_list = NULL;
		}

		add_to_en_list(&vc->new_en_list, default_en_list_no_sim);
		set_new_ecc(vc);
	default:
		break;
	}
}

static void sim_watch(struct ofono_atom *atom,
			enum ofono_atom_watch_condition cond, void *data)
{
	struct ofono_voicecall *vc = data;
	struct ofono_sim *sim = __ofono_atom_get_data(atom);

	if (cond == OFONO_ATOM_WATCH_CONDITION_UNREGISTERED) {
		vc->sim_state_watch = 0;
		vc->sim = NULL;
		return;
	}

	vc->sim = sim;
	vc->sim_state_watch = ofono_sim_add_state_watch(sim,
							sim_state_watch,
							vc, NULL);

	sim_state_watch(vc, ofono_sim_get_state(sim));
}

void ofono_voicecall_register(struct ofono_voicecall *vc)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	struct ofono_modem *modem = __ofono_atom_get_modem(vc->atom);
	const char *path = __ofono_atom_get_path(vc->atom);
	struct ofono_atom *sim_atom;

	if (!g_dbus_register_interface(conn, path,
					OFONO_VOICECALL_MANAGER_INTERFACE,
					manager_methods, manager_signals, NULL,
					vc, NULL)) {
		ofono_error("Could not create %s interface",
				OFONO_VOICECALL_MANAGER_INTERFACE);

		return;
	}

	ofono_modem_add_interface(modem, OFONO_VOICECALL_MANAGER_INTERFACE);

	/* Start out with the 22.101 mandated numbers, if we have a SIM and
	 * the SIM contains EFecc, then we update the list once we've read them
	 */
	add_to_en_list(&vc->en_list, default_en_list_no_sim);
	add_to_en_list(&vc->en_list, default_en_list);

	vc->sim_watch = __ofono_modem_add_atom_watch(modem,
						OFONO_ATOM_TYPE_SIM,
						sim_watch, vc, NULL);

	sim_atom = __ofono_modem_find_atom(modem, OFONO_ATOM_TYPE_SIM);

	if (sim_atom && __ofono_atom_get_registered(sim_atom))
		sim_watch(sim_atom, OFONO_ATOM_WATCH_CONDITION_REGISTERED, vc);

	__ofono_atom_register(vc->atom, voicecall_unregister);
}

void ofono_voicecall_remove(struct ofono_voicecall *vc)
{
	__ofono_atom_free(vc->atom);
}

void ofono_voicecall_set_data(struct ofono_voicecall *vc, void *data)
{
	vc->driver_data = data;
}

void *ofono_voicecall_get_data(struct ofono_voicecall *vc)
{
	return vc->driver_data;
}

int ofono_voicecall_get_next_callid(struct ofono_voicecall *vc)
{
	struct ofono_modem *modem;
	if (vc == NULL || vc->atom == NULL)
		return 0;

	modem = __ofono_atom_get_modem(vc->atom);

	return __ofono_modem_callid_next(modem);
}
