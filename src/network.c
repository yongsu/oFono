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
#include <errno.h>

#include <glib.h>
#include <gdbus.h>

#include "ofono.h"

#include "common.h"
#include "simutil.h"
#include "util.h"
#include "storage.h"

#define NETWORK_REGISTRATION_FLAG_HOME_SHOW_PLMN 0x1
#define NETWORK_REGISTRATION_FLAG_ROAMING_SHOW_SPN 0x2

enum network_registration_mode {
	NETWORK_REGISTRATION_MODE_AUTO = 0,
	NETWORK_REGISTRATION_MODE_MANUAL = 1,
	NETWORK_REGISTRATION_MODE_OFF = 2,
	NETWORK_REGISTRATION_MODE_MANUAL_AUTO = 4
};

#define SETTINGS_STORE "netreg"
#define SETTINGS_GROUP "Settings"

static GSList *g_drivers = NULL;

/* 27.007 Section 7.3 <stat> */
enum operator_status {
	OPERATOR_STATUS_UNKNOWN = 0,
	OPERATOR_STATUS_AVAILABLE = 1,
	OPERATOR_STATUS_CURRENT = 2,
	OPERATOR_STATUS_FORBIDDEN = 3
};

struct ofono_netreg {
	int status;
	int location;
	int cellid;
	int technology;
	int mode;
	char *base_station;
	struct network_operator_data *current_operator;
	GSList *operator_list;
	struct ofono_network_registration_ops *ops;
	int flags;
	DBusMessage *pending;
	int signal_strength;
	char *spname;
	struct sim_spdi *spdi;
	struct sim_eons *eons;
	struct ofono_sim *sim;
	GKeyFile *settings;
	char *imsi;
	struct ofono_watchlist *status_watches;
	const struct ofono_netreg_driver *driver;
	void *driver_data;
	struct ofono_atom *atom;
};

static void operator_list_callback(const struct ofono_error *error, int total,
				const struct ofono_network_operator *list,
				void *data);

static void current_operator_callback(const struct ofono_error *error,
				const struct ofono_network_operator *current,
				void *data);

static void signal_strength_callback(const struct ofono_error *error,
					int strength, void *data);

static void registration_status_callback(const struct ofono_error *error,
					int status, int lac, int ci, int tech,
					void *data);

struct network_operator_data {
	char name[OFONO_MAX_OPERATOR_NAME_LENGTH + 1];
	char mcc[OFONO_MAX_MCC_LENGTH + 1];
	char mnc[OFONO_MAX_MNC_LENGTH + 1];
	int status;
	unsigned int techs;
	const struct sim_eons_operator_info *eons_info;
	struct ofono_netreg *netreg;
};

static const char *registration_mode_to_string(int mode)
{
	switch (mode) {
	case NETWORK_REGISTRATION_MODE_AUTO:
		return "auto";
	case NETWORK_REGISTRATION_MODE_MANUAL:
		return "manual";
	case NETWORK_REGISTRATION_MODE_OFF:
		return "off";
	default:
		return "unknown";
	}
}

static inline const char *network_operator_status_to_string(int status)
{
	switch (status) {
	case OPERATOR_STATUS_AVAILABLE:
		return "available";
	case OPERATOR_STATUS_CURRENT:
		return "current";
	case OPERATOR_STATUS_FORBIDDEN:
		return "forbidden";
	}

	return "unknown";
}

static char **network_operator_technologies(struct network_operator_data *opd)
{
	unsigned int ntechs = 0;
	char **techs;
	unsigned int i;

	for (i = 0; i < sizeof(opd->techs); i++) {
		if (opd->techs & (1 << i))
			ntechs += 1;
	}

	techs = g_new0(char *, ntechs + 1);
	ntechs = 0;

	for (i = 0; i < sizeof(opd->techs); i++) {
		if (!(opd->techs & (1 << i)))
			continue;

		techs[ntechs++] = g_strdup(registration_tech_to_string(i));
	}

	return techs;
}

static void set_registration_mode(struct ofono_netreg *netreg, int mode)
{
	DBusConnection *conn;
	const char *strmode;
	const char *path;

	if (netreg->mode == mode)
		return;

	netreg->mode = mode;

	if (netreg->settings) {
		g_key_file_set_integer(netreg->settings, SETTINGS_GROUP,
					"Mode", netreg->mode);
		storage_sync(netreg->imsi, SETTINGS_STORE, netreg->settings);
	}

	strmode = registration_mode_to_string(mode);

	conn = ofono_dbus_get_connection();
	path = __ofono_atom_get_path(netreg->atom);

	ofono_dbus_signal_property_changed(conn, path,
					OFONO_NETWORK_REGISTRATION_INTERFACE,
					"Mode", DBUS_TYPE_STRING, &strmode);
}

static void register_callback(const struct ofono_error *error, void *data)
{
	struct ofono_netreg *netreg = data;
	DBusConnection *conn = ofono_dbus_get_connection();
	DBusMessage *reply;

	if (!netreg->pending)
		goto out;

	if (error->type == OFONO_ERROR_TYPE_NO_ERROR)
		reply = dbus_message_new_method_return(netreg->pending);
	else
		reply = __ofono_error_failed(netreg->pending);

	g_dbus_send_message(conn, reply);

	dbus_message_unref(netreg->pending);
	netreg->pending = NULL;

out:
	if (netreg->driver->registration_status)
		netreg->driver->registration_status(netreg,
					registration_status_callback, netreg);
}

static void init_register(const struct ofono_error *error, void *data)
{
	struct ofono_netreg *netreg = data;

	if (netreg->driver->registration_status)
		netreg->driver->registration_status(netreg,
					registration_status_callback, netreg);
}

/* Must use g_strfreev on network_operators */
static void network_operator_populate_registered(struct ofono_netreg *netreg,
						char ***network_operators)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	char **children;
	int i;
	int prefix_len;
	int num_children;
	GSList *l;
	char path[256];
	char mnc[OFONO_MAX_MNC_LENGTH + 1];
	char mcc[OFONO_MAX_MCC_LENGTH + 1];
	int op_path_len;

	prefix_len = snprintf(path, sizeof(path), "%s/operator",
				__ofono_atom_get_path(netreg->atom));

	if (!dbus_connection_list_registered(conn, path, &children)) {
		DBG("Unable to obtain registered NetworkOperator(s)");
		*network_operators = g_try_new0(char *, 1);
		return;
	}

	for (i = 0; children[i]; i++)
		;

	num_children = i;

	*network_operators = g_try_new0(char *, num_children + 1);

	/* Enough to store '/' + MCC + MNC + null */
	op_path_len = prefix_len;
	op_path_len += OFONO_MAX_MCC_LENGTH + OFONO_MAX_MNC_LENGTH + 2;

	/* Quoting 27.007: "The list of operators shall be in order: home
	 * network, networks referenced in SIM or active application in the
	 * UICC (GSM or USIM) in the following order: HPLMN selector, User
	 * controlled PLMN selector, Operator controlled PLMN selector and
	 * PLMN selector (in the SIM or GSM application), and other networks."
	 * Thus we must make sure we return the list in the same order,
	 * if possible.  Luckily the operator_list is stored in order already
	 */
	i = 0;
	for (l = netreg->operator_list; l; l = l->next) {
		struct network_operator_data *opd = l->data;
		int j;

		for (j = 0; children[j]; j++) {
			sscanf(children[j], "%3[0-9]%[0-9]", mcc, mnc);
			if (!strcmp(opd->mcc, mcc) && !strcmp(opd->mnc, mnc)) {
				(*network_operators)[i] =
					g_try_new(char, op_path_len);
				snprintf((*network_operators)[i], op_path_len,
						"%s/%s", path, children[j]);
				++i;
			}
		}
	}

	dbus_free_string_array(children);
}

static struct network_operator_data *
	network_operator_create(const struct ofono_network_operator *op)
{
	struct network_operator_data *opd;

	opd = g_new0(struct network_operator_data, 1);

	memcpy(&opd->name, op->name, sizeof(opd->name));
	memcpy(&opd->mcc, op->mcc, sizeof(opd->mcc));
	memcpy(&opd->mnc, op->mnc, sizeof(opd->mnc));

	opd->status = op->status;

	if (op->tech != -1)
		opd->techs |= 1 << op->tech;

	return opd;
}

static void network_operator_destroy(gpointer userdata)
{
	struct network_operator_data *op = userdata;

	g_free(op);
}

static gint network_operator_compare(gconstpointer a, gconstpointer b)
{
	const struct network_operator_data *opda = a;
	const struct ofono_network_operator *opb = b;

	int comp1;
	int comp2;

	comp1 = strcmp(opda->mcc, opb->mcc);
	comp2 = strcmp(opda->mnc, opb->mnc);

	return comp1 != 0 ? comp1 : comp2;
}

static gint network_operator_data_compare(gconstpointer a, gconstpointer b)
{
	const struct network_operator_data *opa = a;
	const struct network_operator_data *opb = b;

	int comp1;
	int comp2;

	comp1 = strcmp(opa->mcc, opb->mcc);
	comp2 = strcmp(opa->mnc, opb->mnc);

	return comp1 != 0 ? comp1 : comp2;
}

static inline const char *network_operator_build_path(struct ofono_netreg *netreg,
							const char *mcc,
							const char *mnc)
{
	static char path[256];

	snprintf(path, sizeof(path), "%s/operator/%s%s",
			__ofono_atom_get_path(netreg->atom),
			mcc, mnc);

	return path;
}

static void network_operator_emit_available_operators(struct ofono_netreg *netreg)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path = __ofono_atom_get_path(netreg->atom);
	char **network_operators;

	network_operator_populate_registered(netreg, &network_operators);

	ofono_dbus_signal_array_property_changed(conn, path,
					OFONO_NETWORK_REGISTRATION_INTERFACE,
					"Operators",
					DBUS_TYPE_OBJECT_PATH,
					&network_operators);

	g_strfreev(network_operators);
}

static void set_network_operator_status(struct network_operator_data *opd,
					int status)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	struct ofono_netreg *netreg = opd->netreg;
	const char *status_str;
	const char *path;

	if (opd->status == status)
		return;

	opd->status = status;

	/* Don't emit for the case where only operator name is reported */
	if (opd->mcc[0] == '\0' && opd->mnc[0] == '\0')
		return;

	status_str = network_operator_status_to_string(status);
	path = network_operator_build_path(netreg, opd->mcc, opd->mnc);

	ofono_dbus_signal_property_changed(conn, path,
					OFONO_NETWORK_OPERATOR_INTERFACE,
					"Status", DBUS_TYPE_STRING,
					&status_str);
}

static void set_network_operator_techs(struct network_operator_data *opd,
					unsigned int techs)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	struct ofono_netreg *netreg = opd->netreg;
	char **technologies;
	const char *path;

	if (opd->techs == techs)
		return;

	opd->techs = techs;
	technologies = network_operator_technologies(opd);
	path = network_operator_build_path(netreg, opd->mcc, opd->mnc);

	ofono_dbus_signal_array_property_changed(conn, path,
					OFONO_NETWORK_REGISTRATION_INTERFACE,
					"Technologies", DBUS_TYPE_STRING,
					&technologies);
	g_strfreev(technologies);
}

static char *get_operator_display_name(struct ofono_netreg *netreg)
{
	struct network_operator_data *opd = netreg->current_operator;
	const char *plmn;
	static char name[1024];
	static char mccmnc[OFONO_MAX_MCC_LENGTH + OFONO_MAX_MNC_LENGTH + 1];
	int len = sizeof(name);
	int home_or_spdi;

	/* The name displayed to user depends on whether we're in a home
	 * PLMN or roaming and on configuration bits from the SIM, all
	 * together there are four cases to consider.  */

	if (!opd) {
		g_strlcpy(name, "", len);
		return name;
	}

	plmn = opd->name;

	/*
	 * This is a fallback on some really broken hardware which do not
	 * report the COPS name
	 */
	if (plmn[0] == '\0') {
		snprintf(mccmnc, sizeof(mccmnc), "%s%s", opd->mcc, opd->mnc);
		plmn = mccmnc;
	}

	if (opd->eons_info && opd->eons_info->longname)
		plmn = opd->eons_info->longname;

	if (!netreg->spname || strlen(netreg->spname) == 0) {
		g_strlcpy(name, plmn, len);
		return name;
	}

	if (netreg->status == NETWORK_REGISTRATION_STATUS_REGISTERED)
		home_or_spdi = TRUE;
	else
		home_or_spdi = sim_spdi_lookup(netreg->spdi,
							opd->mcc, opd->mnc);

	if (home_or_spdi)
		if (netreg->flags & NETWORK_REGISTRATION_FLAG_HOME_SHOW_PLMN)
			/* Case 1 */
			snprintf(name, len, "%s (%s)", netreg->spname, plmn);
		else
			/* Case 2 */
			snprintf(name, len, "%s", netreg->spname);
	else
		if (netreg->flags & NETWORK_REGISTRATION_FLAG_ROAMING_SHOW_SPN)
			/* Case 3 */
			snprintf(name, len, "%s (%s)", netreg->spname, plmn);
		else
			/* Case 4 */
			snprintf(name, len, "%s", plmn);

	return name;
}

static void set_network_operator_name(struct network_operator_data *opd,
					const char *name)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	struct ofono_netreg *netreg = opd->netreg;
	const char *path;
	const char *operator;

	if (name[0] == '\0')
		return;

	if (!strncmp(opd->name, name, OFONO_MAX_OPERATOR_NAME_LENGTH))
		return;

	strncpy(opd->name, name, OFONO_MAX_OPERATOR_NAME_LENGTH);
	opd->name[OFONO_MAX_OPERATOR_NAME_LENGTH] = '\0';

	/* If we have Enhanced Operator Name info on the SIM, we always use
	 * that, so do not need to emit the signal here
	 */
	if (opd->eons_info && opd->eons_info->longname)
		return;

	if (opd == netreg->current_operator) {
		const char *path = __ofono_atom_get_path(netreg->atom);

		operator = get_operator_display_name(netreg);

		ofono_dbus_signal_property_changed(conn, path,
					OFONO_NETWORK_REGISTRATION_INTERFACE,
					"Name", DBUS_TYPE_STRING,
					&operator);
	}

	/* Don't emit when only operator name is reported */
	if (opd->mcc[0] == '\0' && opd->mnc[0] == '\0')
		return;

	path = network_operator_build_path(netreg, opd->mcc, opd->mnc);

	ofono_dbus_signal_property_changed(conn, path,
					OFONO_NETWORK_OPERATOR_INTERFACE,
					"Name", DBUS_TYPE_STRING, &name);
}

static void set_network_operator_eons_info(struct network_operator_data *opd,
				const struct sim_eons_operator_info *eons_info)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	struct ofono_netreg *netreg = opd->netreg;
	const struct sim_eons_operator_info *old_eons_info = opd->eons_info;
	const char *path;
	const char *oldname;
	const char *newname;
	const char *oldinfo;
	const char *newinfo;

	if (!old_eons_info && !eons_info)
		return;

	path = network_operator_build_path(netreg, opd->mcc, opd->mnc);
	opd->eons_info = eons_info;

	if (old_eons_info && old_eons_info->longname)
		oldname = old_eons_info->longname;
	else
		oldname = opd->name;

	if (eons_info && eons_info->longname)
		newname = eons_info->longname;
	else
		newname = opd->name;

	if (oldname != newname && strcmp(oldname, newname)) {
		ofono_dbus_signal_property_changed(conn, path,
					OFONO_NETWORK_OPERATOR_INTERFACE,
					"Name", DBUS_TYPE_STRING, &newname);

		if (opd == netreg->current_operator) {
			const char *npath = __ofono_atom_get_path(netreg->atom);
			const char *operator = get_operator_display_name(netreg);

			ofono_dbus_signal_property_changed(conn, npath,
					OFONO_NETWORK_REGISTRATION_INTERFACE,
					"Name", DBUS_TYPE_STRING,
					&operator);
		}
	}

	if (old_eons_info && old_eons_info->info)
		oldinfo = old_eons_info->info;
	else
		oldinfo = "";

	if (eons_info && eons_info->info)
		newinfo = eons_info->info;
	else
		newinfo = "";

	if (oldinfo != newinfo && strcmp(oldinfo, newinfo))
		ofono_dbus_signal_property_changed(conn, path,
					OFONO_NETWORK_OPERATOR_INTERFACE,
					"AdditionalInformation",
					DBUS_TYPE_STRING, &newinfo);
}

static DBusMessage *network_operator_get_properties(DBusConnection *conn,
							DBusMessage *msg,
							void *data)
{
	struct network_operator_data *opd = data;
	DBusMessage *reply;
	DBusMessageIter iter;
	DBusMessageIter dict;
	char mccmnc[OFONO_MAX_MCC_LENGTH + OFONO_MAX_MNC_LENGTH + 1];

	const char *name = opd->name;
	const char *status =
		network_operator_status_to_string(opd->status);

	if (opd->eons_info && opd->eons_info->longname)
		name = opd->eons_info->longname;

	reply = dbus_message_new_method_return(msg);
	if (!reply)
		return NULL;

	dbus_message_iter_init_append(reply, &iter);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					OFONO_PROPERTIES_ARRAY_SIGNATURE,
					&dict);

	if (name[0] == '\0') {
		snprintf(mccmnc, sizeof(mccmnc), "%s%s", opd->mcc, opd->mnc);
		name = mccmnc;
	}

	ofono_dbus_dict_append(&dict, "Name", DBUS_TYPE_STRING, &name);

	ofono_dbus_dict_append(&dict, "Status", DBUS_TYPE_STRING, &status);

	if (*opd->mcc != '\0') {
		const char *mcc = opd->mcc;
		ofono_dbus_dict_append(&dict, "MobileCountryCode",
					DBUS_TYPE_STRING, &mcc);
	}

	if (*opd->mnc != '\0') {
		const char *mnc = opd->mnc;
		ofono_dbus_dict_append(&dict, "MobileNetworkCode",
					DBUS_TYPE_STRING, &mnc);
	}

	if (opd->techs != 0) {
		char **technologies = network_operator_technologies(opd);

		ofono_dbus_dict_append_array(&dict, "Technologies",
						DBUS_TYPE_STRING,
						&technologies);

		g_strfreev(technologies);
	}

	if (opd->eons_info && opd->eons_info->info) {
		const char *additional = opd->eons_info->info;

		ofono_dbus_dict_append(&dict, "AdditionalInformation",
					DBUS_TYPE_STRING, &additional);
	}

	dbus_message_iter_close_container(&iter, &dict);

	return reply;
}

static DBusMessage *network_operator_register(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	struct network_operator_data *opd = data;
	struct ofono_netreg *netreg = opd->netreg;

	if (netreg->pending)
		return __ofono_error_busy(msg);

	if (netreg->driver->register_manual == NULL)
		return __ofono_error_not_implemented(msg);

	netreg->pending = dbus_message_ref(msg);

	netreg->driver->register_manual(netreg, opd->mcc, opd->mnc,
					register_callback, netreg);

	set_registration_mode(netreg, NETWORK_REGISTRATION_MODE_MANUAL);

	return NULL;
}

static GDBusMethodTable network_operator_methods[] = {
	{ "GetProperties",	"",	"a{sv}",	network_operator_get_properties },
	{ "Register",		"",	"",		network_operator_register,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ }
};

static GDBusSignalTable network_operator_signals[] = {
	{ "PropertyChanged",	"sv" },
	{ }
};

static gboolean network_operator_dbus_register(struct ofono_netreg *netreg,
					struct network_operator_data *opd)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path;

	path = network_operator_build_path(netreg, opd->mcc, opd->mnc);

	if (!g_dbus_register_interface(conn, path,
					OFONO_NETWORK_OPERATOR_INTERFACE,
					network_operator_methods,
					network_operator_signals,
					NULL, opd,
					network_operator_destroy)) {
		ofono_error("Could not register NetworkOperator %s", path);
		return FALSE;
	}

	opd->netreg = netreg;
	opd->eons_info = NULL;

	if (netreg->eons)
		opd->eons_info = sim_eons_lookup(netreg->eons,
							opd->mcc, opd->mnc);

	return TRUE;
}

static gboolean network_operator_dbus_unregister(struct ofono_netreg *netreg,
						struct network_operator_data *opd)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path;

	path = network_operator_build_path(netreg, opd->mcc, opd->mnc);

	return g_dbus_unregister_interface(conn, path,
					OFONO_NETWORK_OPERATOR_INTERFACE);
}

static DBusMessage *network_get_properties(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	struct ofono_netreg *netreg = data;
	DBusMessage *reply;
	DBusMessageIter iter;
	DBusMessageIter dict;

	const char *status = registration_status_to_string(netreg->status);
	const char *operator;
	const char *mode = registration_mode_to_string(netreg->mode);

	char **network_operators;

	reply = dbus_message_new_method_return(msg);
	if (!reply)
		return NULL;

	dbus_message_iter_init_append(reply, &iter);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					OFONO_PROPERTIES_ARRAY_SIGNATURE,
					&dict);

	ofono_dbus_dict_append(&dict, "Status", DBUS_TYPE_STRING, &status);
	ofono_dbus_dict_append(&dict, "Mode", DBUS_TYPE_STRING, &mode);

	if (netreg->location != -1) {
		dbus_uint16_t location = netreg->location;
		ofono_dbus_dict_append(&dict, "LocationAreaCode",
					DBUS_TYPE_UINT16, &location);
	}

	if (netreg->cellid != -1) {
		dbus_uint32_t cellid = netreg->cellid;
		ofono_dbus_dict_append(&dict, "CellId",
					DBUS_TYPE_UINT32, &cellid);
	}

	if (netreg->technology != -1) {
		const char *technology =
			registration_tech_to_string(netreg->technology);

		ofono_dbus_dict_append(&dict, "Technology", DBUS_TYPE_STRING,
					&technology);
	}

	if (netreg->current_operator) {
		if (netreg->current_operator->mcc[0] != '\0') {
			const char *mcc = netreg->current_operator->mcc;
			ofono_dbus_dict_append(&dict, "MobileCountryCode",
						DBUS_TYPE_STRING, &mcc);
		}

		if (netreg->current_operator->mnc[0] != '\0') {
			const char *mnc = netreg->current_operator->mnc;
			ofono_dbus_dict_append(&dict, "MobileNetworkCode",
						DBUS_TYPE_STRING, &mnc);
		}
	}

	operator = get_operator_display_name(netreg);
	ofono_dbus_dict_append(&dict, "Name", DBUS_TYPE_STRING, &operator);

	network_operator_populate_registered(netreg, &network_operators);

	ofono_dbus_dict_append_array(&dict, "Operators",
					DBUS_TYPE_OBJECT_PATH,
					&network_operators);

	g_strfreev(network_operators);

	if (netreg->signal_strength != -1) {
		dbus_uint16_t strength = netreg->signal_strength;
		ofono_dbus_dict_append(&dict, "Strength", DBUS_TYPE_UINT16,
					&strength);
	}

	if (netreg->base_station)
		ofono_dbus_dict_append(&dict, "BaseStation", DBUS_TYPE_STRING,
					&netreg->base_station);

	dbus_message_iter_close_container(&iter, &dict);

	return reply;
}

static DBusMessage *network_register(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct ofono_netreg *netreg = data;

	if (netreg->pending)
		return __ofono_error_busy(msg);

	if (netreg->driver->register_auto == NULL)
		return __ofono_error_not_implemented(msg);

	netreg->pending = dbus_message_ref(msg);

	netreg->driver->register_auto(netreg, register_callback, netreg);

	set_registration_mode(netreg, NETWORK_REGISTRATION_MODE_AUTO);

	return NULL;
}

static DBusMessage *network_deregister(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct ofono_netreg *netreg = data;

	if (netreg->pending)
		return __ofono_error_busy(msg);

	if (netreg->driver->deregister == NULL)
		return __ofono_error_not_implemented(msg);

	netreg->pending = dbus_message_ref(msg);

	netreg->driver->deregister(netreg, register_callback, netreg);

	set_registration_mode(netreg, NETWORK_REGISTRATION_MODE_OFF);

	return NULL;
}

static DBusMessage *network_propose_scan(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	struct ofono_netreg *netreg = data;

	if (netreg->pending)
		return __ofono_error_busy(msg);

	if (netreg->driver->list_operators == NULL)
		return __ofono_error_not_implemented(msg);

	netreg->pending = dbus_message_ref(msg);

	netreg->driver->list_operators(netreg, operator_list_callback, netreg);

	return NULL;
}

static GDBusMethodTable network_registration_methods[] = {
	{ "GetProperties",	"",	"a{sv}",	network_get_properties	},
	{ "Register",		"",	"",		network_register,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ "Deregister",		"",	"",		network_deregister,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ "ProposeScan",	"",	"ao",		network_propose_scan,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ }
};

static GDBusSignalTable network_registration_signals[] = {
	{ "PropertyChanged",	"sv" },
	{ }
};

static void set_registration_status(struct ofono_netreg *netreg, int status)
{
	const char *str_status = registration_status_to_string(status);
	const char *path = __ofono_atom_get_path(netreg->atom);
	DBusConnection *conn = ofono_dbus_get_connection();

	netreg->status = status;

	ofono_dbus_signal_property_changed(conn, path,
					OFONO_NETWORK_REGISTRATION_INTERFACE,
					"Status", DBUS_TYPE_STRING,
					&str_status);
}

static void set_registration_location(struct ofono_netreg *netreg, int lac)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path = __ofono_atom_get_path(netreg->atom);
	dbus_uint16_t dbus_lac = lac;

	if (lac > 0xffff)
		return;

	netreg->location = lac;

	if (netreg->location == -1)
		return;

	ofono_dbus_signal_property_changed(conn, path,
					OFONO_NETWORK_REGISTRATION_INTERFACE,
					"LocationAreaCode",
					DBUS_TYPE_UINT16, &dbus_lac);
}

static void set_registration_cellid(struct ofono_netreg *netreg, int ci)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path = __ofono_atom_get_path(netreg->atom);
	dbus_uint32_t dbus_ci = ci;

	netreg->cellid = ci;

	if (netreg->cellid == -1)
		return;

	ofono_dbus_signal_property_changed(conn, path,
					OFONO_NETWORK_REGISTRATION_INTERFACE,
					"CellId", DBUS_TYPE_UINT32, &dbus_ci);
}

static void set_registration_technology(struct ofono_netreg *netreg, int tech)
{
	const char *tech_str = registration_tech_to_string(tech);
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path = __ofono_atom_get_path(netreg->atom);

	netreg->technology = tech;

	if (netreg->technology == -1)
		return;

	ofono_dbus_signal_property_changed(conn, path,
					OFONO_NETWORK_REGISTRATION_INTERFACE,
					"Technology", DBUS_TYPE_STRING,
					&tech_str);
}

void __ofono_netreg_set_base_station_name(struct ofono_netreg *netreg,
						const char *name)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path = __ofono_atom_get_path(netreg->atom);
	const char *base_station = name ? name : "";

	/* Cell ID changed, but we don't have a cell name, nothing to do */
	if (netreg->base_station == NULL && name == NULL)
		return;

	if (netreg->base_station)
		g_free(netreg->base_station);

	if (name == NULL) {
		netreg->base_station = NULL;

		/* We just got unregistered, set name to NULL
		 * but don't emit signal */
		if (netreg->current_operator == NULL)
			return;
	} else {
		netreg->base_station = g_strdup(name);
	}

	ofono_dbus_signal_property_changed(conn, path,
					OFONO_NETWORK_REGISTRATION_INTERFACE,
					"BaseStation", DBUS_TYPE_STRING,
					&base_station);
}

unsigned int __ofono_netreg_add_status_watch(struct ofono_netreg *netreg,
				ofono_netreg_status_notify_cb_t notify,
				void *data, ofono_destroy_func destroy)
{
	struct ofono_watchlist_item *item;

	DBG("%p", netreg);

	if (netreg == NULL)
		return 0;

	if (notify == NULL)
		return 0;

	item = g_new0(struct ofono_watchlist_item, 1);

	item->notify = notify;
	item->destroy = destroy;
	item->notify_data = data;

	return __ofono_watchlist_add_item(netreg->status_watches, item);
}

gboolean __ofono_netreg_remove_status_watch(struct ofono_netreg *netreg,
						unsigned int id)
{
	DBG("%p", netreg);

	return __ofono_watchlist_remove_item(netreg->status_watches, id);
}

static void notify_status_watches(struct ofono_netreg *netreg)
{
	struct ofono_watchlist_item *item;
	GSList *l;
	ofono_netreg_status_notify_cb_t notify;
	const char *mcc = NULL;
	const char *mnc = NULL;

	if (netreg->current_operator) {
		mcc = netreg->current_operator->mcc;
		mnc = netreg->current_operator->mnc;
	}

	for (l = netreg->status_watches->items; l; l = l->next) {
		item = l->data;
		notify = item->notify;

		notify(netreg->status, netreg->location, netreg->cellid,
			netreg->technology, mcc, mnc, item->notify_data);
	}
}

void ofono_netreg_status_notify(struct ofono_netreg *netreg, int status,
			int lac, int ci, int tech)
{
	if (!netreg)
		return;

	if (netreg->status != status)
		set_registration_status(netreg, status);

	if (netreg->location != lac)
		set_registration_location(netreg, lac);

	if (netreg->cellid != ci)
		set_registration_cellid(netreg, ci);

	if (netreg->technology != tech)
		set_registration_technology(netreg, tech);

	if (netreg->status == NETWORK_REGISTRATION_STATUS_REGISTERED ||
		netreg->status == NETWORK_REGISTRATION_STATUS_ROAMING) {
		if (netreg->driver->current_operator)
			netreg->driver->current_operator(netreg,
					current_operator_callback, netreg);
	} else {
		struct ofono_error error;

		error.type = OFONO_ERROR_TYPE_NO_ERROR;
		error.error = 0;

		current_operator_callback(&error, NULL, netreg);
		__ofono_netreg_set_base_station_name(netreg, NULL);

		netreg->signal_strength = -1;
	}

	notify_status_watches(netreg);
}

void ofono_netreg_time_notify(struct ofono_netreg *netreg,
				struct ofono_network_time *info)
{
	struct ofono_modem *modem = __ofono_atom_get_modem(netreg->atom);

	if (!info)
		return;

	__ofono_nettime_info_received(modem, info);
}

static GSList *compress_operator_list(const struct ofono_network_operator *list,
					int total)
{
	GSList *oplist = 0;
	GSList *o;
	int i;
	struct network_operator_data *opd;

	for (i = 0; i < total; i++) {
		o = NULL;

		if (oplist)
			o = g_slist_find_custom(oplist, &list[i],
						network_operator_compare);

		if (!o) {
			opd = network_operator_create(&list[i]);
			oplist = g_slist_prepend(oplist, opd);
		} else if (o && list[i].tech != -1) {
			opd = o->data;
			opd->techs |= 1 << list[i].tech;
		}
	}

	if (oplist)
		oplist = g_slist_reverse(oplist);

	return oplist;
}

static gboolean update_operator_list(struct ofono_netreg *netreg, int total,
				const struct ofono_network_operator *list)
{
	GSList *n = NULL;
	GSList *o;
	GSList *compressed;
	GSList *c;
	gboolean changed = FALSE;

	compressed = compress_operator_list(list, total);

	for (c = compressed; c; c = c->next) {
		struct network_operator_data *copd = c->data;

		o = g_slist_find_custom(netreg->operator_list, copd,
					network_operator_data_compare);

		if (o) { /* Update and move to a new list */
			set_network_operator_status(o->data, copd->status);
			set_network_operator_techs(o->data, copd->techs);
			set_network_operator_name(o->data, copd->name);

			n = g_slist_prepend(n, o->data);
			netreg->operator_list =
				g_slist_remove(netreg->operator_list, o->data);
		} else {
			/* New operator */
			struct network_operator_data *opd;

			opd = g_memdup(copd,
					sizeof(struct network_operator_data));

			if (!network_operator_dbus_register(netreg, opd)) {
				g_free(opd);
				continue;
			}

			n = g_slist_prepend(n, opd);
			changed = TRUE;
		}
	}

	g_slist_foreach(compressed, (GFunc)g_free, NULL);
	g_slist_free(compressed);

	if (n)
		n = g_slist_reverse(n);

	if (netreg->operator_list)
		changed = TRUE;

	for (o = netreg->operator_list; o; o = o->next)
		network_operator_dbus_unregister(netreg, o->data);

	g_slist_free(netreg->operator_list);

	netreg->operator_list = n;

	return changed;
}

static void operator_list_callback(const struct ofono_error *error, int total,
				const struct ofono_network_operator *list,
				void *data)
{
	struct ofono_netreg *netreg = data;
	DBusMessage *reply;
	char **network_operators;
	DBusMessageIter iter;
	DBusMessageIter array_iter;
	int i;
	gboolean need_to_emit;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Error occurred during operator list");
		__ofono_dbus_pending_reply(&netreg->pending,
					__ofono_error_failed(netreg->pending));
		return;
	}

	need_to_emit = update_operator_list(netreg, total, list);

	reply = dbus_message_new_method_return(netreg->pending);
	network_operator_populate_registered(netreg, &network_operators);

	dbus_message_iter_init_append(reply, &iter);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
		DBUS_TYPE_OBJECT_PATH_AS_STRING, &array_iter);

	for (i = 0; network_operators[i]; i++)
		dbus_message_iter_append_basic(&array_iter,
			DBUS_TYPE_OBJECT_PATH, &network_operators[i]);

	dbus_message_iter_close_container(&iter, &array_iter);
	__ofono_dbus_pending_reply(&netreg->pending, reply);

	g_strfreev(network_operators);

	if (need_to_emit)
		network_operator_emit_available_operators(netreg);
}

static void current_operator_callback(const struct ofono_error *error,
				const struct ofono_network_operator *current,
				void *data)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	struct ofono_netreg *netreg = data;
	const char *path = __ofono_atom_get_path(netreg->atom);
	GSList *op = NULL;
	const char *operator;

	DBG("%p, %p", netreg, netreg->current_operator);

	/*
	 * Sometimes we try to query COPS right when we roam off the cell,
	 * in which case the operator information frequently comes in bogus.
	 * We ignore it here
	 */
	if (netreg->status != NETWORK_REGISTRATION_STATUS_REGISTERED &&
			netreg->status != NETWORK_REGISTRATION_STATUS_ROAMING)
		current = NULL;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Error during current operator");
		return;
	}

	if (!netreg->current_operator && !current)
		return;

	/* We got a new network operator, reset the previous one's status */
	/* It will be updated properly later */
	if (netreg->current_operator &&
		(!current ||
			network_operator_compare(netreg->current_operator, current)))
		set_network_operator_status(netreg->current_operator,
						OPERATOR_STATUS_AVAILABLE);

	if (current)
		op = g_slist_find_custom(netreg->operator_list, current,
					network_operator_compare);

	if (op) {
		struct network_operator_data *opd = op->data;
		unsigned int techs = opd->techs;

		if (current->tech != -1) {
			techs |= 1 << current->tech;
			set_network_operator_techs(opd, techs);
		}

		set_network_operator_status(opd, OPERATOR_STATUS_CURRENT);
		set_network_operator_name(opd, current->name);

		if (netreg->current_operator == op->data)
			return;

		netreg->current_operator = op->data;
		goto emit;
	}

	if (current) {
		struct network_operator_data *opd;

		opd = network_operator_create(current);

		if (opd->mcc[0] != '\0' && opd->mnc[0] != '\0' &&
				!network_operator_dbus_register(netreg, opd)) {
			g_free(opd);
			return;
		}

		netreg->current_operator = opd;
		netreg->operator_list = g_slist_append(netreg->operator_list,
							opd);

		if (opd->mcc[0] != '\0' && opd->mnc[0] != '\0')
			network_operator_emit_available_operators(netreg);
	} else {
		/* We don't free this here because operator is registered */
		/* Taken care of elsewhere */
		netreg->current_operator = NULL;
	}

emit:
	operator = get_operator_display_name(netreg);

	ofono_dbus_signal_property_changed(conn, path,
					OFONO_NETWORK_REGISTRATION_INTERFACE,
					"Name", DBUS_TYPE_STRING,
					&operator);

	if (netreg->current_operator) {
		if (netreg->current_operator->mcc[0] != '\0') {
			const char *mcc = netreg->current_operator->mcc;
			ofono_dbus_signal_property_changed(conn, path,
					OFONO_NETWORK_REGISTRATION_INTERFACE,
					"MobileCountryCode",
					DBUS_TYPE_STRING, &mcc);
		}

		if (netreg->current_operator->mnc[0] != '\0') {
			const char *mnc = netreg->current_operator->mnc;
			ofono_dbus_signal_property_changed(conn, path,
					OFONO_NETWORK_REGISTRATION_INTERFACE,
					"MobileNetworkCode",
					DBUS_TYPE_STRING, &mnc);
		}
	}

	notify_status_watches(netreg);
}

static void registration_status_callback(const struct ofono_error *error,
					int status, int lac, int ci, int tech,
					void *data)
{
	struct ofono_netreg *netreg = data;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Error during registration status query");
		return;
	}

	ofono_netreg_status_notify(netreg, status, lac, ci, tech);
}

static void init_registration_status(const struct ofono_error *error,
					int status, int lac, int ci, int tech,
					void *data)
{
	struct ofono_netreg *netreg = data;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Error during registration status query");
		return;
	}

	ofono_netreg_status_notify(netreg, status, lac, ci, tech);

	/* Bootstrap our signal strength value without waiting for the
	 * stack to report it
	 */
	if (netreg->status == NETWORK_REGISTRATION_STATUS_REGISTERED ||
		netreg->status == NETWORK_REGISTRATION_STATUS_ROAMING) {
		if (netreg->driver->strength)
			netreg->driver->strength(netreg,
					signal_strength_callback, netreg);
	}

	if (netreg->mode == NETWORK_REGISTRATION_MODE_AUTO &&
		(status == NETWORK_REGISTRATION_STATUS_NOT_REGISTERED ||
			status == NETWORK_REGISTRATION_STATUS_DENIED ||
			status == NETWORK_REGISTRATION_STATUS_UNKNOWN)) {
		if (netreg->driver->register_auto)
			netreg->driver->register_auto(netreg, init_register,
							netreg);
	}
}

void ofono_netreg_strength_notify(struct ofono_netreg *netreg, int strength)
{
	DBusConnection *conn = ofono_dbus_get_connection();

	if (netreg->signal_strength == strength)
		return;

	/* Theoretically we can get signal strength even when not registered
	 * to any network.  However, what do we do with it in that case?
	 */
	if (netreg->status != NETWORK_REGISTRATION_STATUS_REGISTERED &&
		netreg->status != NETWORK_REGISTRATION_STATUS_ROAMING)
		return;

	netreg->signal_strength = strength;

	if (strength != -1) {
		const char *path = __ofono_atom_get_path(netreg->atom);
		dbus_uint16_t strength = netreg->signal_strength;

		ofono_dbus_signal_property_changed(conn, path,
					OFONO_NETWORK_REGISTRATION_INTERFACE,
					"Strength", DBUS_TYPE_UINT16,
					&strength);
	}
}

static void signal_strength_callback(const struct ofono_error *error,
					int strength, void *data)
{
	struct ofono_netreg *netreg = data;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Error during signal strength query");
		return;
	}

	ofono_netreg_strength_notify(netreg, strength);
}

static void sim_opl_read_cb(int ok, int length, int record,
				const unsigned char *data,
				int record_length, void *userdata)
{
	struct ofono_netreg *netreg = userdata;
	int total;
	GSList *l;

	if (!ok) {
		if (record > 0)
			goto optimize;

		return;
	}

	if (record_length < 8 || length < record_length)
		return;

	total = length / record_length;

	sim_eons_add_opl_record(netreg->eons, data, record_length);

	if (record != total)
		return;

optimize:
	sim_eons_optimize(netreg->eons);

	for (l = netreg->operator_list; l; l = l->next) {
		struct network_operator_data *opd = l->data;
		const struct sim_eons_operator_info *eons_info;

		eons_info = sim_eons_lookup(netreg->eons, opd->mcc, opd->mnc);

		set_network_operator_eons_info(opd, eons_info);
	}
}

static void sim_pnn_read_cb(int ok, int length, int record,
				const unsigned char *data,
				int record_length, void *userdata)
{
	struct ofono_netreg *netreg = userdata;
	int total;

	if (!ok)
		goto check;

	if (length < 3 || record_length < 3 || length < record_length)
		return;

	total = length / record_length;

	if (!netreg->eons)
		netreg->eons = sim_eons_new(total);

	sim_eons_add_pnn_record(netreg->eons, record, data, record_length);

	if (record != total)
		return;

check:
	/* If PNN is not present then OPL is not useful, don't
	 * retrieve it.  If OPL is not there then PNN[1] will
	 * still be used for the HPLMN and/or EHPLMN, if PNN
	 * is present.  */
	if (netreg->eons && !sim_eons_pnn_is_empty(netreg->eons))
		ofono_sim_read(netreg->sim, SIM_EFOPL_FILEID,
				OFONO_SIM_FILE_STRUCTURE_FIXED,
				sim_opl_read_cb, netreg);
}

static void sim_spdi_read_cb(int ok, int length, int record,
				const unsigned char *data,
				int record_length, void *userdata)
{
	struct ofono_netreg *netreg = userdata;
	struct network_operator_data *current = netreg->current_operator;

	if (!ok)
		return;

	netreg->spdi = sim_spdi_new(data, length);

	if (!current)
		return;

	if (netreg->status == NETWORK_REGISTRATION_STATUS_ROAMING) {
		DBusConnection *conn = ofono_dbus_get_connection();
		const char *path = __ofono_atom_get_path(netreg->atom);
		const char *operator;

		if (!sim_spdi_lookup(netreg->spdi,
					current->mcc, current->mnc))
			return;

		operator = get_operator_display_name(netreg);

		ofono_dbus_signal_property_changed(conn, path,
					OFONO_NETWORK_REGISTRATION_INTERFACE,
					"Name", DBUS_TYPE_STRING,
					&operator);
	}
}

static void sim_spn_read_cb(int ok, int length, int record,
				const unsigned char *data,
				int record_length, void *userdata)
{
	struct ofono_netreg *netreg = userdata;
	unsigned char dcbyte;
	char *spn;

	if (!ok)
		return;

	dcbyte = data[0];

	/* TS 31.102 says:
	 *
	 * the string shall use:
	 *
	 * - either the SMS default 7-bit coded alphabet as defined in
	 *   TS 23.038 [5] with bit 8 set to 0. The string shall be left
	 *   justified. Unused bytes shall be set to 'FF'.
	 *
	 * - or one of the UCS2 code options defined in the annex of TS
	 *   31.101 [11].
	 *
	 * 31.101 has no such annex though.  51.101 refers to Annex B of
	 * itself which is not there either.  11.11 contains the same
	 * paragraph as 51.101 and has an Annex B which we implement.
	 */
	spn = sim_string_to_utf8(data + 1, length - 1);

	if (!spn) {
		ofono_error("EFspn read successfully, but couldn't parse");
		return;
	}

	if (strlen(spn) == 0) {
		g_free(spn);
		return;
	}

	netreg->spname = spn;
	ofono_sim_read(netreg->sim, SIM_EFSPDI_FILEID,
			OFONO_SIM_FILE_STRUCTURE_TRANSPARENT,
			sim_spdi_read_cb, netreg);

	if (dcbyte & SIM_EFSPN_DC_HOME_PLMN_BIT)
		netreg->flags |= NETWORK_REGISTRATION_FLAG_HOME_SHOW_PLMN;

	if (!(dcbyte & SIM_EFSPN_DC_ROAMING_SPN_BIT))
		netreg->flags |= NETWORK_REGISTRATION_FLAG_ROAMING_SHOW_SPN;

	if (netreg->current_operator) {
		DBusConnection *conn = ofono_dbus_get_connection();
		const char *path = __ofono_atom_get_path(netreg->atom);
		const char *operator;

		operator = get_operator_display_name(netreg);

		ofono_dbus_signal_property_changed(conn, path,
					OFONO_NETWORK_REGISTRATION_INTERFACE,
					"Name", DBUS_TYPE_STRING,
					&operator);
	}
}

int ofono_netreg_get_location(struct ofono_netreg *netreg)
{
	if (netreg == NULL)
		return -1;

	return netreg->location;
}

int ofono_netreg_get_cellid(struct ofono_netreg *netreg)
{
	if (netreg == NULL)
		return -1;

	return netreg->cellid;
}

int ofono_netreg_get_status(struct ofono_netreg *netreg)
{
	if (netreg == NULL)
		return -1;

	return netreg->status;
}

int ofono_netreg_get_technology(struct ofono_netreg *netreg)
{
	if (netreg == NULL)
		return -1;

	return netreg->technology;
}

const char *ofono_netreg_get_mcc(struct ofono_netreg *netreg)
{
	if (netreg == NULL)
		return NULL;

	if (netreg->current_operator == NULL)
		return NULL;

	return netreg->current_operator->mcc;
}

const char *ofono_netreg_get_mnc(struct ofono_netreg *netreg)
{
	if (netreg == NULL)
		return NULL;

	if (netreg->current_operator == NULL)
		return NULL;

	return netreg->current_operator->mnc;
}

int ofono_netreg_driver_register(const struct ofono_netreg_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	if (d->probe == NULL)
		return -EINVAL;

	g_drivers = g_slist_prepend(g_drivers, (void *)d);

	return 0;
}

void ofono_netreg_driver_unregister(const struct ofono_netreg_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	g_drivers = g_slist_remove(g_drivers, (void *)d);
}

static void netreg_unregister(struct ofono_atom *atom)
{
	struct ofono_netreg *netreg = __ofono_atom_get_data(atom);
	DBusConnection *conn = ofono_dbus_get_connection();
	struct ofono_modem *modem = __ofono_atom_get_modem(atom);
	const char *path = __ofono_atom_get_path(atom);
	GSList *l;

	__ofono_watchlist_free(netreg->status_watches);
	netreg->status_watches = NULL;

	for (l = netreg->operator_list; l; l = l->next) {
		struct network_operator_data *opd = l->data;

		if (opd->mcc[0] == '\0' && opd->mnc[0] == '\0')
			continue;

		network_operator_dbus_unregister(netreg, l->data);
	}

	g_slist_free(netreg->operator_list);
	netreg->operator_list = NULL;

	if (netreg->base_station) {
		g_free(netreg->base_station);
		netreg->base_station = NULL;
	}

	if (netreg->settings) {
		storage_close(netreg->imsi, SETTINGS_STORE,
				netreg->settings, TRUE);

		g_free(netreg->imsi);
		netreg->imsi = NULL;
		netreg->settings = NULL;
	}

	g_dbus_unregister_interface(conn, path,
					OFONO_NETWORK_REGISTRATION_INTERFACE);
	ofono_modem_remove_interface(modem,
					OFONO_NETWORK_REGISTRATION_INTERFACE);
}

static void netreg_remove(struct ofono_atom *atom)
{
	struct ofono_netreg *netreg = __ofono_atom_get_data(atom);

	DBG("atom: %p", atom);

	if (netreg == NULL)
		return;

	if (netreg->driver && netreg->driver->remove)
		netreg->driver->remove(netreg);

	if (netreg->eons)
		sim_eons_free(netreg->eons);

	if (netreg->spdi)
		sim_spdi_free(netreg->spdi);

	if (netreg->spname)
		g_free(netreg->spname);

	g_free(netreg);
}

struct ofono_netreg *ofono_netreg_create(struct ofono_modem *modem,
					unsigned int vendor,
					const char *driver,
					void *data)
{
	struct ofono_netreg *netreg;
	GSList *l;

	if (driver == NULL)
		return NULL;

	netreg = g_try_new0(struct ofono_netreg, 1);

	if (netreg == NULL)
		return NULL;

	netreg->status = NETWORK_REGISTRATION_STATUS_UNKNOWN;
	netreg->location = -1;
	netreg->cellid = -1;
	netreg->technology = -1;
	netreg->signal_strength = -1;

	netreg->atom = __ofono_modem_add_atom(modem, OFONO_ATOM_TYPE_NETREG,
						netreg_remove, netreg);

	for (l = g_drivers; l; l = l->next) {
		const struct ofono_netreg_driver *drv = l->data;

		if (g_strcmp0(drv->name, driver))
			continue;

		if (drv->probe(netreg, vendor, data) < 0)
			continue;

		netreg->driver = drv;
		break;
	}

	return netreg;
}

static void netreg_load_settings(struct ofono_netreg *netreg)
{
	const char *imsi;
	int mode;

	imsi = ofono_sim_get_imsi(netreg->sim);

	if (!imsi)
		return;

	netreg->settings = storage_open(imsi, SETTINGS_STORE);

	if (netreg->settings == NULL)
		return;

	netreg->imsi = g_strdup(imsi);

	mode = g_key_file_get_integer(netreg->settings, SETTINGS_GROUP,
					"Mode", NULL);

	if (mode >= 0 && mode <= 2)
		netreg->mode = mode;

	g_key_file_set_integer(netreg->settings, SETTINGS_GROUP,
				"Mode", netreg->mode);
}

void ofono_netreg_register(struct ofono_netreg *netreg)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	struct ofono_modem *modem = __ofono_atom_get_modem(netreg->atom);
	const char *path = __ofono_atom_get_path(netreg->atom);
	struct ofono_atom *sim_atom;

	if (!g_dbus_register_interface(conn, path,
					OFONO_NETWORK_REGISTRATION_INTERFACE,
					network_registration_methods,
					network_registration_signals,
					NULL, netreg, NULL)) {
		ofono_error("Could not create %s interface",
				OFONO_NETWORK_REGISTRATION_INTERFACE);

		return;
	}

	netreg->status_watches = __ofono_watchlist_new(g_free);

	ofono_modem_add_interface(modem, OFONO_NETWORK_REGISTRATION_INTERFACE);

	if (netreg->driver->registration_status)
		netreg->driver->registration_status(netreg,
					init_registration_status, netreg);

	sim_atom = __ofono_modem_find_atom(modem, OFONO_ATOM_TYPE_SIM);

	if (sim_atom) {
		/* Assume that if sim atom exists, it is ready */
		netreg->sim = __ofono_atom_get_data(sim_atom);

		netreg_load_settings(netreg);

		ofono_sim_read(netreg->sim, SIM_EFPNN_FILEID,
				OFONO_SIM_FILE_STRUCTURE_FIXED,
				sim_pnn_read_cb, netreg);
		ofono_sim_read(netreg->sim, SIM_EFSPN_FILEID,
				OFONO_SIM_FILE_STRUCTURE_TRANSPARENT,
				sim_spn_read_cb, netreg);
	}

	__ofono_atom_register(netreg->atom, netreg_unregister);
}

void ofono_netreg_remove(struct ofono_netreg *netreg)
{
	__ofono_atom_free(netreg->atom);
}

void ofono_netreg_set_data(struct ofono_netreg *netreg, void *data)
{
	netreg->driver_data = data;
}

void *ofono_netreg_get_data(struct ofono_netreg *netreg)
{
	return netreg->driver_data;
}
