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

#include <glib.h>
#include <gdbus.h>

#include "ofono.h"

#define DBUS_GSM_ERROR_INTERFACE "org.ofono.Error"

static DBusConnection *g_connection;

static void append_variant(DBusMessageIter *iter,
				int type, void *value)
{
	char sig[2];
	DBusMessageIter valueiter;

	sig[0] = type;
	sig[1] = 0;

	dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
						sig, &valueiter);

	dbus_message_iter_append_basic(&valueiter, type, value);

	dbus_message_iter_close_container(iter, &valueiter);
}

void ofono_dbus_dict_append(DBusMessageIter *dict,
			const char *key, int type, void *value)
{
	DBusMessageIter keyiter;

	if (type == DBUS_TYPE_STRING) {
		const char *str = *((const char **) value);
		if (str == NULL)
			return;
	}

	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY,
							NULL, &keyiter);

	dbus_message_iter_append_basic(&keyiter, DBUS_TYPE_STRING, &key);

	append_variant(&keyiter, type, value);

	dbus_message_iter_close_container(dict, &keyiter);
}

static void append_array_variant(DBusMessageIter *iter, int type, void *val)
{
	DBusMessageIter variant, array;
	char typesig[2];
	char arraysig[3];
	const char **str_array = *(const char ***)val;
	int i;

	arraysig[0] = DBUS_TYPE_ARRAY;
	arraysig[1] = typesig[0] = type;
	arraysig[2] = typesig[1] = '\0';

	dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
						arraysig, &variant);

	dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY,
						typesig, &array);

	for (i = 0; str_array[i]; i++)
		dbus_message_iter_append_basic(&array, type,
						&(str_array[i]));

	dbus_message_iter_close_container(&variant, &array);

	dbus_message_iter_close_container(iter, &variant);
}

void ofono_dbus_dict_append_array(DBusMessageIter *dict, const char *key,
				int type, void *val)
{
	DBusMessageIter entry;

	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY,
						NULL, &entry);

	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);

	append_array_variant(&entry, type, val);

	dbus_message_iter_close_container(dict, &entry);
}

static void append_dict_variant(DBusMessageIter *iter, int type, void *val)
{
	DBusMessageIter variant, array, entry;
	char typesig[5];
	char arraysig[6];
	const void **val_array = *(const void ***)val;
	int i;

	arraysig[0] = DBUS_TYPE_ARRAY;
	arraysig[1] = typesig[0] = DBUS_DICT_ENTRY_BEGIN_CHAR;
	arraysig[2] = typesig[1] = DBUS_TYPE_STRING;
	arraysig[3] = typesig[2] = type;
	arraysig[4] = typesig[3] = DBUS_DICT_ENTRY_END_CHAR;
	arraysig[5] = typesig[4] = '\0';

	dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
						arraysig, &variant);

	dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY,
						typesig, &array);

	for (i = 0; val_array[i]; i += 2) {
		dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY,
							NULL, &entry);

		dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING,
						&(val_array[i + 0]));
		dbus_message_iter_append_basic(&entry, type,
						&(val_array[i + 1]));

		dbus_message_iter_close_container(&array, &entry);
	}

	dbus_message_iter_close_container(&variant, &array);

	dbus_message_iter_close_container(iter, &variant);
}

void ofono_dbus_dict_append_dict(DBusMessageIter *dict, const char *key,
				int type, void *val)
{
	DBusMessageIter entry;

	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY,
						NULL, &entry);

	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);

	append_dict_variant(&entry, type, val);

	dbus_message_iter_close_container(dict, &entry);
}

int ofono_dbus_signal_property_changed(DBusConnection *conn,
					const char *path,
					const char *interface,
					const char *name,
					int type, void *value)
{
	DBusMessage *signal;
	DBusMessageIter iter;

	signal = dbus_message_new_signal(path, interface, "PropertyChanged");

	if (!signal) {
		ofono_error("Unable to allocate new %s.PropertyChanged signal",
				interface);
		return -1;
	}

	dbus_message_iter_init_append(signal, &iter);

	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &name);

	append_variant(&iter, type, value);

	return g_dbus_send_message(conn, signal);
}

int ofono_dbus_signal_array_property_changed(DBusConnection *conn,
						const char *path,
						const char *interface,
						const char *name,
						int type, void *value)

{
	DBusMessage *signal;
	DBusMessageIter iter;

	signal = dbus_message_new_signal(path, interface, "PropertyChanged");

	if (!signal) {
		ofono_error("Unable to allocate new %s.PropertyChanged signal",
				interface);
		return -1;
	}

	dbus_message_iter_init_append(signal, &iter);

	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &name);

	append_array_variant(&iter, type, value);

	return g_dbus_send_message(conn, signal);
}

int ofono_dbus_signal_dict_property_changed(DBusConnection *conn,
						const char *path,
						const char *interface,
						const char *name,
						int type, void *value)

{
	DBusMessage *signal;
	DBusMessageIter iter;

	signal = dbus_message_new_signal(path, interface, "PropertyChanged");

	if (!signal) {
		ofono_error("Unable to allocate new %s.PropertyChanged signal",
				interface);
		return -1;
	}

	dbus_message_iter_init_append(signal, &iter);

	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &name);

	append_dict_variant(&iter, type, value);

	return g_dbus_send_message(conn, signal);
}

DBusMessage *__ofono_error_invalid_args(DBusMessage *msg)
{
	return g_dbus_create_error(msg, DBUS_GSM_ERROR_INTERFACE
					".InvalidArguments",
					"Invalid arguments in method call");
}

DBusMessage *__ofono_error_invalid_format(DBusMessage *msg)
{
	return g_dbus_create_error(msg, DBUS_GSM_ERROR_INTERFACE
					".InvalidFormat",
					"Argument format is not recognized");
}

DBusMessage *__ofono_error_not_implemented(DBusMessage *msg)
{
	return g_dbus_create_error(msg, DBUS_GSM_ERROR_INTERFACE
					".NotImplemented",
					"Implementation not provided");
}

DBusMessage *__ofono_error_failed(DBusMessage *msg)
{
	return g_dbus_create_error(msg, DBUS_GSM_ERROR_INTERFACE ".Failed",
					"Operation failed");
}

DBusMessage *__ofono_error_busy(DBusMessage *msg)
{
	return g_dbus_create_error(msg, DBUS_GSM_ERROR_INTERFACE ".InProgress",
					"Operation already in progress");
}

DBusMessage *__ofono_error_not_found(DBusMessage *msg)
{
	return g_dbus_create_error(msg, DBUS_GSM_ERROR_INTERFACE ".NotFound",
			"Object is not found or not valid for this operation");
}

DBusMessage *__ofono_error_not_active(DBusMessage *msg)
{
	return g_dbus_create_error(msg, DBUS_GSM_ERROR_INTERFACE ".NotActive",
			"Operation is not active or in progress");
}

DBusMessage *__ofono_error_not_supported(DBusMessage *msg)
{
	return g_dbus_create_error(msg, DBUS_GSM_ERROR_INTERFACE
					".NotSupported",
					"Operation is not supported by the"
					" network / modem");
}

DBusMessage *__ofono_error_not_available(DBusMessage *msg)
{
	return g_dbus_create_error(msg, DBUS_GSM_ERROR_INTERFACE,
					".NotAvailable",
					"Operation currently not available");
}

DBusMessage *__ofono_error_timed_out(DBusMessage *msg)
{
	return g_dbus_create_error(msg, DBUS_GSM_ERROR_INTERFACE ".Timedout",
			"Operation failure due to timeout");
}

DBusMessage *__ofono_error_sim_not_ready(DBusMessage *msg)
{
	return g_dbus_create_error(msg, DBUS_GSM_ERROR_INTERFACE ".SimNotReady",
			"SIM is not ready or not inserted");
}

DBusMessage *__ofono_error_in_use(DBusMessage *msg)
{
	return g_dbus_create_error(msg, DBUS_GSM_ERROR_INTERFACE ".InUse",
			"The resource is currently in use");
}

DBusMessage *__ofono_error_not_attached(DBusMessage *msg)
{
	return g_dbus_create_error(msg, DBUS_GSM_ERROR_INTERFACE ".NotAttached",
			"GPRS is not attached");
}

DBusMessage *__ofono_error_attach_in_progress(DBusMessage *msg)
{
	return g_dbus_create_error(msg,
				DBUS_GSM_ERROR_INTERFACE ".AttachInProgress",
				"GPRS Attach is in progress");
}

void __ofono_dbus_pending_reply(DBusMessage **msg, DBusMessage *reply)
{
	DBusConnection *conn = ofono_dbus_get_connection();

	g_dbus_send_message(conn, reply);

	dbus_message_unref(*msg);
	*msg = NULL;
}

gboolean __ofono_dbus_valid_object_path(const char *path)
{
	unsigned int i;
	char c = '\0';

	if (path == NULL)
		return FALSE;

	if (path[0] == '\0')
		return FALSE;

	if (path[0] && !path[1] && path[0] == '/')
		return TRUE;

	if (path[0] != '/')
		return FALSE;

	for (i = 0; path[i]; i++) {
		if (path[i] == '/' && c == '/')
			return FALSE;

		c = path[i];

		if (path[i] >= 'a' && path[i] <= 'z')
			continue;

		if (path[i] >= 'A' && path[i] <= 'Z')
			continue;

		if (path[i] >= '0' && path[i] <= '9')
			continue;

		if (path[i] == '_' || path[i] == '/')
			continue;

		return FALSE;
	}

	if (path[i-1] == '/')
		return FALSE;

	return TRUE;
}

DBusConnection *ofono_dbus_get_connection()
{
	return g_connection;
}

static void dbus_gsm_set_connection(DBusConnection *conn)
{
	if (conn && g_connection != NULL)
		ofono_error("Setting a connection when it is not NULL");

	g_connection = conn;
}

int __ofono_dbus_init(DBusConnection *conn)
{
	dbus_gsm_set_connection(conn);

	return 0;
}

void __ofono_dbus_cleanup(void)
{
	DBusConnection *conn = ofono_dbus_get_connection();

	if (!conn || !dbus_connection_get_is_connected(conn))
		return;

	dbus_gsm_set_connection(NULL);
}
