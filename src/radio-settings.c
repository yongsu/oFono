/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
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

#define RADIO_SETTINGS_MODE_CACHED 0x1

static GSList *g_drivers = NULL;

struct ofono_radio_settings {
	DBusMessage *pending;
	int flags;
	int mode;
	int pending_mode;
	const struct ofono_radio_settings_driver *driver;
	void *driver_data;
	struct ofono_atom *atom;
};

static const char *radio_access_mode_to_string(enum ofono_radio_access_mode mode)
{
	switch (mode) {
	case OFONO_RADIO_ACCESS_MODE_ANY:
		return "any";
	case OFONO_RADIO_ACCESS_MODE_GSM:
		return "gsm";
	case OFONO_RADIO_ACCESS_MODE_UMTS:
		return "umts";
	case OFONO_RADIO_ACCESS_MODE_LTE:
		return "lte";
	default:
		return "";
	}
}

static int string_to_radio_access_mode(const char *mode)

{
	if (g_strcmp0(mode, "any") == 0)
		return OFONO_RADIO_ACCESS_MODE_ANY;
	if (g_strcmp0(mode, "gsm") == 0)
		return OFONO_RADIO_ACCESS_MODE_GSM;
	if (g_strcmp0(mode, "umts") == 0)
		return OFONO_RADIO_ACCESS_MODE_UMTS;
	if (g_strcmp0(mode, "lte") == 0)
		return OFONO_RADIO_ACCESS_MODE_LTE;
	return -1;
}

static DBusMessage *radio_get_properties_reply(DBusMessage *msg,
						struct ofono_radio_settings *rs)
{
	DBusMessage *reply;
	DBusMessageIter iter;
	DBusMessageIter dict;

	const char *mode = radio_access_mode_to_string(rs->mode);

	reply = dbus_message_new_method_return(msg);
	if (!reply)
		return NULL;

	dbus_message_iter_init_append(reply, &iter);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					OFONO_PROPERTIES_ARRAY_SIGNATURE,
					&dict);

	ofono_dbus_dict_append(&dict, "TechnologyPreference", DBUS_TYPE_STRING,
				&mode);

	dbus_message_iter_close_container(&iter, &dict);

	return reply;
}

static void radio_set_rat_mode(struct ofono_radio_settings *rs,
				enum ofono_radio_access_mode mode)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path;
	const char *str_mode;

	if (rs->mode == (int)mode)
		return;

	rs->mode = mode;
	rs->flags |= RADIO_SETTINGS_MODE_CACHED;

	path = __ofono_atom_get_path(rs->atom);
	str_mode = radio_access_mode_to_string(rs->mode);

	ofono_dbus_signal_property_changed(conn, path,
					OFONO_RADIO_SETTINGS_INTERFACE,
					"TechnologyPreference", DBUS_TYPE_STRING,
					&str_mode);
}

static void radio_mode_set_callback(const struct ofono_error *error, void *data)
{
	struct ofono_radio_settings *rs = data;
	DBusMessage *reply;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Error setting radio access mode");
		rs->pending_mode = rs->mode;
		reply = __ofono_error_failed(rs->pending);
		__ofono_dbus_pending_reply(&rs->pending, reply);
		return;
	}

	reply = dbus_message_new_method_return(rs->pending);
	__ofono_dbus_pending_reply(&rs->pending, reply);

	radio_set_rat_mode(rs, rs->pending_mode);
}

static void radio_rat_mode_query_callback(const struct ofono_error *error,
					enum ofono_radio_access_mode mode,
					void *data)
{
	struct ofono_radio_settings *rs = data;
	DBusMessage *reply;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Error during radio access mode query");
		reply = __ofono_error_failed(rs->pending);
		__ofono_dbus_pending_reply(&rs->pending, reply);
		return;
	}

	radio_set_rat_mode(rs, mode);

	reply = radio_get_properties_reply(rs->pending, rs);
	__ofono_dbus_pending_reply(&rs->pending, reply);
}

static DBusMessage *radio_get_properties(DBusConnection *conn, DBusMessage *msg,
					void *data)
{
	struct ofono_radio_settings *rs = data;

	if (rs->flags & RADIO_SETTINGS_MODE_CACHED)
		return radio_get_properties_reply(msg, rs);

	if (!rs->driver->query_rat_mode)
		return __ofono_error_not_implemented(msg);

	if (rs->pending)
		return __ofono_error_busy(msg);

	rs->pending = dbus_message_ref(msg);
	rs->driver->query_rat_mode(rs, radio_rat_mode_query_callback, rs);

	return NULL;
}

static DBusMessage *radio_set_property(DBusConnection *conn, DBusMessage *msg,
					void *data)
{
	struct ofono_radio_settings *rs = data;
	DBusMessageIter iter;
	DBusMessageIter var;
	const char *property;

	if (rs->pending)
		return __ofono_error_busy(msg);

	if (!dbus_message_iter_init(msg, &iter))
		return __ofono_error_invalid_args(msg);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
		return __ofono_error_invalid_args(msg);

	dbus_message_iter_get_basic(&iter, &property);
	dbus_message_iter_next(&iter);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT)
		return __ofono_error_invalid_args(msg);

	dbus_message_iter_recurse(&iter, &var);

	if (g_strcmp0(property, "TechnologyPreference") == 0) {
		const char *value;
		int mode = -1;

		if (!rs->driver->set_rat_mode)
			return __ofono_error_not_implemented(msg);

		if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING)
			return __ofono_error_invalid_args(msg);

		dbus_message_iter_get_basic(&var, &value);
		mode = string_to_radio_access_mode(value);

		if (mode == -1)
			return __ofono_error_invalid_args(msg);

		if (rs->mode == mode)
			return dbus_message_new_method_return(msg);

		rs->pending = dbus_message_ref(msg);
		rs->pending_mode = mode;

		rs->driver->set_rat_mode(rs, mode, radio_mode_set_callback, rs);

		return NULL;
	}

	return __ofono_error_invalid_args(msg);
}

static GDBusMethodTable radio_methods[] = {
	{ "GetProperties",	"",	"a{sv}",	radio_get_properties,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ "SetProperty",	"sv",	"",		radio_set_property,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ }
};

static GDBusSignalTable radio_signals[] = {
	{ "PropertyChanged",	"sv" },
	{ }
};

int ofono_radio_settings_driver_register(const struct ofono_radio_settings_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	if (!d || !d->probe)
		return -EINVAL;

	g_drivers = g_slist_prepend(g_drivers, (void *)d);

	return 0;
}

void ofono_radio_settings_driver_unregister(const struct ofono_radio_settings_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	if (!d)
		return;

	g_drivers = g_slist_remove(g_drivers, (void *)d);
}

static void radio_settings_unregister(struct ofono_atom *atom)
{
	struct ofono_radio_settings *rs = __ofono_atom_get_data(atom);
	const char *path = __ofono_atom_get_path(rs->atom);
	DBusConnection *conn = ofono_dbus_get_connection();
	struct ofono_modem *modem = __ofono_atom_get_modem(rs->atom);

	ofono_modem_remove_interface(modem, OFONO_RADIO_SETTINGS_INTERFACE);
	g_dbus_unregister_interface(conn, path, OFONO_RADIO_SETTINGS_INTERFACE);
}

static void radio_settings_remove(struct ofono_atom *atom)
{
	struct ofono_radio_settings *rs = __ofono_atom_get_data(atom);

	DBG("atom: %p", atom);

	if (!rs)
		return;

	if (rs->driver && rs->driver->remove)
		rs->driver->remove(rs);

	g_free(rs);
}

struct ofono_radio_settings *ofono_radio_settings_create(struct ofono_modem *modem,
							unsigned int vendor,
							const char *driver,
							void *data)
{
	struct ofono_radio_settings *rs;
	GSList *l;

	if (!driver)
		return NULL;

	rs = g_try_new0(struct ofono_radio_settings, 1);
	if (!rs)
		return NULL;

	rs->mode = -1;

	rs->atom = __ofono_modem_add_atom(modem, OFONO_ATOM_TYPE_RADIO_SETTINGS,
						radio_settings_remove, rs);

	for (l = g_drivers; l; l = l->next) {
		const struct ofono_radio_settings_driver *drv = l->data;

		if (g_strcmp0(drv->name, driver) != 0)
			continue;

		if (drv->probe(rs, vendor, data) < 0)
			continue;

		rs->driver = drv;
		break;
	}

	return rs;
}

void ofono_radio_settings_register(struct ofono_radio_settings *rs)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	struct ofono_modem *modem = __ofono_atom_get_modem(rs->atom);
	const char *path = __ofono_atom_get_path(rs->atom);

	if (!g_dbus_register_interface(conn, path,
					OFONO_RADIO_SETTINGS_INTERFACE,
					radio_methods, radio_signals,
					NULL, rs, NULL)) {
		ofono_error("Could not create %s interface",
				OFONO_RADIO_SETTINGS_INTERFACE);

		return;
	}

	ofono_modem_add_interface(modem, OFONO_RADIO_SETTINGS_INTERFACE);
	__ofono_atom_register(rs->atom, radio_settings_unregister);
}

void ofono_radio_settings_remove(struct ofono_radio_settings *rs)
{
	__ofono_atom_free(rs->atom);
}

void ofono_radio_settings_set_data(struct ofono_radio_settings *rs,
					void *data)
{
	rs->driver_data = data;
}

void *ofono_radio_settings_get_data(struct ofono_radio_settings *rs)
{
	return rs->driver_data;
}
