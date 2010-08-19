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
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>

#include <glib.h>
#include <gdbus.h>

#include "ofono.h"

#include "common.h"
#include "storage.h"
#include "idmap.h"

#define GPRS_FLAG_ATTACHING 0x1
#define GPRS_FLAG_RECHECK 0x2

#define SETTINGS_STORE "gprs"
#define SETTINGS_GROUP "Settings"
#define MAX_CONTEXT_NAME_LENGTH 127
#define MAX_CONTEXTS 256

static GSList *g_drivers = NULL;
static GSList *g_context_drivers = NULL;

enum gprs_context_type {
	GPRS_CONTEXT_TYPE_INTERNET = 0,
	GPRS_CONTEXT_TYPE_MMS,
	GPRS_CONTEXT_TYPE_WAP,
	GPRS_CONTEXT_TYPE_INVALID,
};

struct ofono_gprs {
	GSList *contexts;
	ofono_bool_t attached;
	ofono_bool_t driver_attached;
	ofono_bool_t roaming_allowed;
	ofono_bool_t powered;
	int status;
	int flags;
	struct idmap *pid_map;
	unsigned int last_context_id;
	struct idmap *cid_map;
	int netreg_status;
	struct ofono_netreg *netreg;
	unsigned int netreg_watch;
	unsigned int status_watch;
	GKeyFile *settings;
	char *imsi;
	DBusMessage *pending;
	struct ofono_gprs_context *context_driver;
	const struct ofono_gprs_driver *driver;
	void *driver_data;
	struct ofono_atom *atom;
};

struct ofono_gprs_context {
	struct ofono_gprs *gprs;
	DBusMessage *pending;
	const struct ofono_gprs_context_driver *driver;
	void *driver_data;
	struct ofono_atom *atom;
};

struct context_settings {
	char *interface;
	gboolean static_ip;
	char *ip;
	char *netmask;
	char *gateway;
	char **dns;
};

struct pri_context {
	ofono_bool_t active;
	enum gprs_context_type type;
	char name[MAX_CONTEXT_NAME_LENGTH + 1];
	unsigned int id;
	char *path;
	char *key;
	struct context_settings *settings;
	struct ofono_gprs_primary_context context;
	struct ofono_gprs *gprs;
};

static void gprs_netreg_update(struct ofono_gprs *gprs);

static const char *gprs_context_type_to_string(int type)
{
	switch (type) {
	case GPRS_CONTEXT_TYPE_INTERNET:
		return "internet";
	case GPRS_CONTEXT_TYPE_MMS:
		return "mms";
	case GPRS_CONTEXT_TYPE_WAP:
		return "wap";
	}

	return NULL;
}

static enum gprs_context_type gprs_context_string_to_type(const char *str)
{
	if (g_str_equal(str, "internet"))
		return GPRS_CONTEXT_TYPE_INTERNET;
	else if (g_str_equal(str, "wap"))
		return GPRS_CONTEXT_TYPE_WAP;
	else if (g_str_equal(str, "mms"))
		return GPRS_CONTEXT_TYPE_MMS;

	return GPRS_CONTEXT_TYPE_INVALID;
}

static const char *gprs_proto_to_string(enum ofono_gprs_proto proto)
{
	switch (proto) {
	case OFONO_GPRS_PROTO_IP:
		return "ip";
	case OFONO_GPRS_PROTO_IPV6:
		return "ipv6";
	};

	return NULL;
}

static gboolean gprs_proto_from_string(const char *str,
					enum ofono_gprs_proto *proto)
{
	if (g_str_equal(str, "ip")) {
		*proto = OFONO_GPRS_PROTO_IP;
		return TRUE;
	} else if (g_str_equal(str, "ipv6")) {
		*proto = OFONO_GPRS_PROTO_IPV6;
		return TRUE;
	}

	return FALSE;
}

static unsigned int gprs_cid_alloc(struct ofono_gprs *gprs)
{
	return idmap_alloc(gprs->cid_map);
}

static void gprs_cid_release(struct ofono_gprs *gprs, unsigned int id)
{
	idmap_put(gprs->cid_map, id);
}

static struct pri_context *gprs_context_by_path(struct ofono_gprs *gprs,
						const char *ctx_path)
{
	GSList *l;

	for (l = gprs->contexts; l; l = l->next) {
		struct pri_context *ctx = l->data;

		if (g_str_equal(ctx_path, ctx->path))
			return ctx;
	}

	return NULL;
}

static void context_settings_free(struct context_settings *settings)
{
	g_free(settings->interface);
	g_free(settings->ip);
	g_free(settings->netmask);
	g_free(settings->gateway);
	g_strfreev(settings->dns);

	g_free(settings);
}

static void context_settings_append_variant(struct context_settings *settings,
						DBusMessageIter *iter)
{
	DBusMessageIter variant;
	DBusMessageIter array;
	char typesig[5];
	char arraysig[6];
	const char *method;

	arraysig[0] = DBUS_TYPE_ARRAY;
	arraysig[1] = typesig[0] = DBUS_DICT_ENTRY_BEGIN_CHAR;
	arraysig[2] = typesig[1] = DBUS_TYPE_STRING;
	arraysig[3] = typesig[2] = DBUS_TYPE_VARIANT;
	arraysig[4] = typesig[3] = DBUS_DICT_ENTRY_END_CHAR;
	arraysig[5] = typesig[4] = '\0';

	dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
						arraysig, &variant);

	dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY,
						typesig, &array);
	if (settings == NULL)
		goto end;

	ofono_dbus_dict_append(&array, "Interface",
				DBUS_TYPE_STRING, &settings->interface);

	if (settings->static_ip == TRUE)
		method = "static";
	else
		method = "dhcp";

	ofono_dbus_dict_append(&array, "Method", DBUS_TYPE_STRING, &method);

	if (settings->ip)
		ofono_dbus_dict_append(&array, "Address", DBUS_TYPE_STRING,
					&settings->ip);

	if (settings->netmask)
		ofono_dbus_dict_append(&array, "Netmask", DBUS_TYPE_STRING,
					&settings->netmask);

	if (settings->gateway)
		ofono_dbus_dict_append(&array, "Gateway", DBUS_TYPE_STRING,
					&settings->gateway);

	if (settings->dns)
		ofono_dbus_dict_append_array(&array, "DomainNameServers",
						DBUS_TYPE_STRING,
						&settings->dns);

end:
	dbus_message_iter_close_container(&variant, &array);

	dbus_message_iter_close_container(iter, &variant);
}

static void context_settings_append_dict(struct context_settings *settings,
						DBusMessageIter *dict)
{
	DBusMessageIter entry;
	const char *key = "Settings";

	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY,
						NULL, &entry);

	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);

	context_settings_append_variant(settings, &entry);

	dbus_message_iter_close_container(dict, &entry);
}

static void pri_context_signal_settings(struct pri_context *ctx)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path = ctx->path;
	DBusMessage *signal;
	DBusMessageIter iter;
	const char *prop = "Settings";

	signal = dbus_message_new_signal(path, OFONO_DATA_CONTEXT_INTERFACE,
						"PropertyChanged");

	if (!signal)
		return;

	dbus_message_iter_init_append(signal, &iter);

	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &prop);

	context_settings_append_variant(ctx->settings, &iter);

	g_dbus_send_message(conn, signal);
}

static void pri_ifupdown(const char *interface, ofono_bool_t active)
{
	struct ifreq ifr;
	int sk;

	if (!interface)
		return;

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return;

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, interface, IFNAMSIZ);

	if (ioctl(sk, SIOCGIFFLAGS, &ifr) < 0)
		goto done;

	if (active == TRUE) {
		if (ifr.ifr_flags & IFF_UP)
			goto done;
		ifr.ifr_flags |= IFF_UP;
	} else {
		if (!(ifr.ifr_flags & IFF_UP))
			goto done;
		ifr.ifr_flags &= ~IFF_UP;
	}

	if (ioctl(sk, SIOCSIFFLAGS, &ifr) < 0)
		ofono_error("Failed to change interface flags");

done:
	close(sk);
}

static void pri_reset_context_settings(struct pri_context *ctx)
{
	char *interface;

	if (ctx->settings == NULL)
		return;

	interface = ctx->settings->interface;
	ctx->settings->interface = NULL;

	context_settings_free(ctx->settings);
	ctx->settings = NULL;

	pri_context_signal_settings(ctx);

	pri_ifupdown(interface, FALSE);

	g_free(interface);
}

static void pri_update_context_settings(struct pri_context *ctx,
					const char *interface,
					ofono_bool_t static_ip,
					const char *ip, const char *netmask,
					const char *gateway, const char **dns)
{
	if (ctx->settings)
		context_settings_free(ctx->settings);

	ctx->settings = g_new0(struct context_settings, 1);

	ctx->settings->interface = g_strdup(interface);
	ctx->settings->static_ip = static_ip;
	ctx->settings->ip = g_strdup(ip);
	ctx->settings->netmask = g_strdup(netmask);
	ctx->settings->gateway = g_strdup(gateway);
	ctx->settings->dns = g_strdupv((char **)dns);

	pri_ifupdown(interface, TRUE);

	pri_context_signal_settings(ctx);
}

static DBusMessage *pri_get_properties(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct pri_context *ctx = data;
	DBusMessage *reply;
	DBusMessageIter iter;
	DBusMessageIter dict;
	dbus_bool_t value;
	const char *type = gprs_context_type_to_string(ctx->type);
	const char *proto = gprs_proto_to_string(ctx->context.proto);
	const char *name = ctx->name;
	const char *strvalue;

	reply = dbus_message_new_method_return(msg);
	if (!reply)
		return NULL;

	dbus_message_iter_init_append(reply, &iter);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					OFONO_PROPERTIES_ARRAY_SIGNATURE,
					&dict);

	ofono_dbus_dict_append(&dict, "Name", DBUS_TYPE_STRING, &name);

	value = ctx->active;
	ofono_dbus_dict_append(&dict, "Active", DBUS_TYPE_BOOLEAN, &value);

	ofono_dbus_dict_append(&dict, "Type", DBUS_TYPE_STRING, &type);

	ofono_dbus_dict_append(&dict, "Protocol", DBUS_TYPE_STRING, &proto);

	strvalue = ctx->context.apn;
	ofono_dbus_dict_append(&dict, "AccessPointName", DBUS_TYPE_STRING,
				&strvalue);

	strvalue = ctx->context.username;
	ofono_dbus_dict_append(&dict, "Username", DBUS_TYPE_STRING,
				&strvalue);

	strvalue = ctx->context.password;
	ofono_dbus_dict_append(&dict, "Password", DBUS_TYPE_STRING,
				&strvalue);

	context_settings_append_dict(ctx->settings, &dict);

	dbus_message_iter_close_container(&iter, &dict);

	return reply;
}

static void pri_activate_callback(const struct ofono_error *error,
					const char *interface, ofono_bool_t static_ip,
					const char *ip, const char *netmask,
					const char *gateway, const char **dns,
					void *data)
{
	struct pri_context *ctx = data;
	struct ofono_gprs_context *gc = ctx->gprs->context_driver;
	DBusConnection *conn = ofono_dbus_get_connection();
	dbus_bool_t value;

	DBG("%p %s", ctx, interface);

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Activating context failed with error: %s",
				telephony_error_to_str(error));
		__ofono_dbus_pending_reply(&gc->pending,
					__ofono_error_failed(gc->pending));

		gprs_cid_release(ctx->gprs, ctx->context.cid);
		ctx->context.cid = 0;

		return;
	}

	ctx->active = TRUE;
	__ofono_dbus_pending_reply(&gc->pending,
				dbus_message_new_method_return(gc->pending));

	/* If we don't have the interface, don't bother emitting any settings,
	 * as nobody can make use of them
	 */
	if (interface != NULL)
		pri_update_context_settings(ctx, interface, static_ip,
						ip, netmask, gateway, dns);

	value = ctx->active;
	ofono_dbus_signal_property_changed(conn, ctx->path,
						OFONO_DATA_CONTEXT_INTERFACE,
						"Active", DBUS_TYPE_BOOLEAN,
						&value);
}

static void pri_deactivate_callback(const struct ofono_error *error, void *data)
{
	struct pri_context *ctx = data;
	struct ofono_gprs_context *gc = ctx->gprs->context_driver;
	DBusConnection *conn = ofono_dbus_get_connection();
	dbus_bool_t value;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Deactivating context failed with error: %s",
				telephony_error_to_str(error));
		__ofono_dbus_pending_reply(&gc->pending,
					__ofono_error_failed(gc->pending));
		return;
	}

	gprs_cid_release(ctx->gprs, ctx->context.cid);
	ctx->context.cid = 0;

	ctx->active = FALSE;
	__ofono_dbus_pending_reply(&gc->pending,
				dbus_message_new_method_return(gc->pending));

	pri_reset_context_settings(ctx);

	value = ctx->active;
	ofono_dbus_signal_property_changed(conn, ctx->path,
						OFONO_DATA_CONTEXT_INTERFACE,
						"Active", DBUS_TYPE_BOOLEAN,
						&value);
}

static DBusMessage *pri_set_apn(struct pri_context *ctx, DBusConnection *conn,
				DBusMessage *msg, const char *apn)
{
	GKeyFile *settings = ctx->gprs->settings;

	if (strlen(apn) > OFONO_GPRS_MAX_APN_LENGTH)
		return __ofono_error_invalid_format(msg);

	if (g_str_equal(apn, ctx->context.apn))
		return dbus_message_new_method_return(msg);

	if (is_valid_apn(apn) == FALSE)
		return __ofono_error_invalid_format(msg);

	strcpy(ctx->context.apn, apn);

	if (settings) {
		g_key_file_set_string(settings, ctx->key,
					"AccessPointName", apn);
		storage_sync(ctx->gprs->imsi, SETTINGS_STORE, settings);
	}

	g_dbus_send_reply(conn, msg, DBUS_TYPE_INVALID);

	ofono_dbus_signal_property_changed(conn, ctx->path,
						OFONO_DATA_CONTEXT_INTERFACE,
						"AccessPointName",
						DBUS_TYPE_STRING, &apn);

	return NULL;
}

static DBusMessage *pri_set_username(struct pri_context *ctx,
					DBusConnection *conn, DBusMessage *msg,
					const char *username)
{
	GKeyFile *settings = ctx->gprs->settings;

	if (strlen(username) > OFONO_GPRS_MAX_USERNAME_LENGTH)
		return __ofono_error_invalid_format(msg);

	if (g_str_equal(username, ctx->context.username))
		return dbus_message_new_method_return(msg);

	strcpy(ctx->context.username, username);

	if (settings) {
		g_key_file_set_string(settings, ctx->key,
					"Username", username);
		storage_sync(ctx->gprs->imsi, SETTINGS_STORE, settings);
	}

	g_dbus_send_reply(conn, msg, DBUS_TYPE_INVALID);

	ofono_dbus_signal_property_changed(conn, ctx->path,
						OFONO_DATA_CONTEXT_INTERFACE,
						"Username",
						DBUS_TYPE_STRING, &username);

	return NULL;
}

static DBusMessage *pri_set_password(struct pri_context *ctx,
					DBusConnection *conn, DBusMessage *msg,
					const char *password)
{
	GKeyFile *settings = ctx->gprs->settings;

	if (strlen(password) > OFONO_GPRS_MAX_PASSWORD_LENGTH)
		return __ofono_error_invalid_format(msg);

	if (g_str_equal(password, ctx->context.password))
		return dbus_message_new_method_return(msg);

	strcpy(ctx->context.password, password);

	if (settings) {
		g_key_file_set_string(settings, ctx->key,
					"Password", password);
		storage_sync(ctx->gprs->imsi, SETTINGS_STORE, settings);
	}

	g_dbus_send_reply(conn, msg, DBUS_TYPE_INVALID);

	ofono_dbus_signal_property_changed(conn, ctx->path,
						OFONO_DATA_CONTEXT_INTERFACE,
						"Password",
						DBUS_TYPE_STRING, &password);

	return NULL;
}

static DBusMessage *pri_set_type(struct pri_context *ctx, DBusConnection *conn,
					DBusMessage *msg, const char *type)
{
	GKeyFile *settings = ctx->gprs->settings;
	enum gprs_context_type context_type;

	context_type = gprs_context_string_to_type(type);

	if (context_type == GPRS_CONTEXT_TYPE_INVALID)
		return __ofono_error_invalid_format(msg);

	if (ctx->type == context_type)
		return dbus_message_new_method_return(msg);

	ctx->type = context_type;

	if (settings) {
		g_key_file_set_string(settings, ctx->key, "Type", type);
		storage_sync(ctx->gprs->imsi, SETTINGS_STORE, settings);
	}

	g_dbus_send_reply(conn, msg, DBUS_TYPE_INVALID);

	ofono_dbus_signal_property_changed(conn, ctx->path,
						OFONO_DATA_CONTEXT_INTERFACE,
						"Type", DBUS_TYPE_STRING,
						&type);

	return NULL;
}

static DBusMessage *pri_set_proto(struct pri_context *ctx,
					DBusConnection *conn,
					DBusMessage *msg, const char *str)
{
	GKeyFile *settings = ctx->gprs->settings;
	enum ofono_gprs_proto proto;

	if (gprs_proto_from_string(str, &proto) == FALSE)
		return __ofono_error_invalid_format(msg);

	if (ctx->context.proto == proto)
		return dbus_message_new_method_return(msg);

	ctx->context.proto = proto;

	if (settings) {
		g_key_file_set_string(settings, ctx->key, "Protocol", str);
		storage_sync(ctx->gprs->imsi, SETTINGS_STORE, settings);
	}

	g_dbus_send_reply(conn, msg, DBUS_TYPE_INVALID);

	ofono_dbus_signal_property_changed(conn, ctx->path,
						OFONO_DATA_CONTEXT_INTERFACE,
						"Protocol", DBUS_TYPE_STRING,
						&str);

	return NULL;
}

static DBusMessage *pri_set_name(struct pri_context *ctx, DBusConnection *conn,
					DBusMessage *msg, const char *name)
{
	GKeyFile *settings = ctx->gprs->settings;

	if (strlen(name) > MAX_CONTEXT_NAME_LENGTH)
		return __ofono_error_invalid_format(msg);

	if (ctx->name && g_str_equal(ctx->name, name))
		return dbus_message_new_method_return(msg);

	strcpy(ctx->name, name);

	if (settings) {
		g_key_file_set_string(settings, ctx->key, "Name", ctx->name);
		storage_sync(ctx->gprs->imsi, SETTINGS_STORE, settings);
	}

	g_dbus_send_reply(conn, msg, DBUS_TYPE_INVALID);

	ofono_dbus_signal_property_changed(conn, ctx->path,
						OFONO_DATA_CONTEXT_INTERFACE,
						"Name", DBUS_TYPE_STRING,
						&name);

	return NULL;
}

static DBusMessage *pri_set_property(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct pri_context *ctx = data;
	DBusMessageIter iter;
	DBusMessageIter var;
	const char *property;
	dbus_bool_t value;
	const char *str;

	if (!dbus_message_iter_init(msg, &iter))
		return __ofono_error_invalid_args(msg);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
		return __ofono_error_invalid_args(msg);

	dbus_message_iter_get_basic(&iter, &property);
	dbus_message_iter_next(&iter);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT)
		return __ofono_error_invalid_args(msg);

	dbus_message_iter_recurse(&iter, &var);

	if (g_str_equal(property, "Active")) {
		struct ofono_gprs_context *gc = ctx->gprs->context_driver;

		if (gc == NULL || gc->driver->activate_primary == NULL ||
				gc->driver->deactivate_primary == NULL ||
				ctx->gprs->cid_map == NULL)
			return __ofono_error_not_implemented(msg);

		if (gc->pending)
			return __ofono_error_busy(msg);

		if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_BOOLEAN)
			return __ofono_error_invalid_args(msg);

		dbus_message_iter_get_basic(&var, &value);

		if (ctx->active == (ofono_bool_t) value)
			return dbus_message_new_method_return(msg);

		if (value && !ctx->gprs->attached)
			return __ofono_error_not_attached(msg);

		if (ctx->gprs->flags & GPRS_FLAG_ATTACHING)
			return __ofono_error_attach_in_progress(msg);

		if (value) {
			ctx->context.cid = gprs_cid_alloc(ctx->gprs);

			if (ctx->context.cid == 0)
				return __ofono_error_failed(msg);

			if (ctx->context.cid !=
					idmap_get_min(ctx->gprs->cid_map)) {
				ofono_error("Multiple active contexts are"
						" not yet supported");

				gprs_cid_release(ctx->gprs, ctx->context.cid);
				ctx->context.cid = 0;

				return __ofono_error_failed(msg);
			}
		}

		gc->pending = dbus_message_ref(msg);

		if (value)
			gc->driver->activate_primary(gc, &ctx->context,
						pri_activate_callback, ctx);
		else
			gc->driver->deactivate_primary(gc, ctx->context.cid,
						pri_deactivate_callback, ctx);

		return NULL;
	}

	/* All other properties are read-only when context is active */
	if (ctx->active == TRUE)
		return __ofono_error_in_use(msg);

	if (!strcmp(property, "AccessPointName")) {
		if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING)
			return __ofono_error_invalid_args(msg);

		dbus_message_iter_get_basic(&var, &str);

		return pri_set_apn(ctx, conn, msg, str);
	} else if (!strcmp(property, "Type")) {
		if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING)
			return __ofono_error_invalid_args(msg);

		dbus_message_iter_get_basic(&var, &str);

		return pri_set_type(ctx, conn, msg, str);
	} else if (!strcmp(property, "Protocol")) {
		if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING)
			return __ofono_error_invalid_args(msg);

		dbus_message_iter_get_basic(&var, &str);

		return pri_set_proto(ctx, conn, msg, str);
	} else if (!strcmp(property, "Username")) {
		if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING)
			return __ofono_error_invalid_args(msg);

		dbus_message_iter_get_basic(&var, &str);

		return pri_set_username(ctx, conn, msg, str);
	} else if (!strcmp(property, "Password")) {
		if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING)
			return __ofono_error_invalid_args(msg);

		dbus_message_iter_get_basic(&var, &str);

		return pri_set_password(ctx, conn, msg, str);
	} else if (!strcmp(property, "Name")) {
		if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING)
			return __ofono_error_invalid_args(msg);

		dbus_message_iter_get_basic(&var, &str);

		return pri_set_name(ctx, conn, msg, str);
	}

	return __ofono_error_invalid_args(msg);
}

static GDBusMethodTable context_methods[] = {
	{ "GetProperties",	"",	"a{sv}",	pri_get_properties },
	{ "SetProperty",	"sv",	"",		pri_set_property,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ }
};

static GDBusSignalTable context_signals[] = {
	{ "PropertyChanged",	"sv" },
	{ }
};

static struct pri_context *pri_context_create(struct ofono_gprs *gprs,
						const char *name,
						enum gprs_context_type type)
{
	struct pri_context *context = g_try_new0(struct pri_context, 1);

	if (!context)
		return NULL;

	context->gprs = gprs;
	strcpy(context->name, name);
	context->type = type;

	return context;
}

static void pri_context_destroy(gpointer userdata)
{
	struct pri_context *ctx = userdata;

	if (ctx->settings) {
		context_settings_free(ctx->settings);
		ctx->settings = NULL;
	}

	g_free(ctx->path);

	g_free(ctx);
}

static gboolean context_dbus_register(struct pri_context *ctx)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	char path[256];
	const char *basepath;

	basepath = __ofono_atom_get_path(ctx->gprs->atom);

	snprintf(path, sizeof(path), "%s/primarycontext%u", basepath, ctx->id);

	if (!g_dbus_register_interface(conn, path, OFONO_DATA_CONTEXT_INTERFACE,
					context_methods, context_signals,
					NULL, ctx, pri_context_destroy)) {
		ofono_error("Could not register PrimaryContext %s", path);
		idmap_put(ctx->gprs->pid_map, ctx->id);
		pri_context_destroy(ctx);

		return FALSE;
	}

	ctx->path = g_strdup(path);
	ctx->key = ctx->path + strlen(basepath) + 1;

	return TRUE;
}

static gboolean context_dbus_unregister(struct pri_context *ctx)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	char path[256];

	strcpy(path, ctx->path);
	idmap_put(ctx->gprs->pid_map, ctx->id);

	return g_dbus_unregister_interface(conn, path,
						OFONO_DATA_CONTEXT_INTERFACE);
}

static char **gprs_contexts_path_list(GSList *context_list)
{
	GSList *l;
	char **i;
	struct pri_context *ctx;
	char **objlist = g_new0(char *, g_slist_length(context_list) + 1);

	if (!objlist)
		return NULL;

	for (i = objlist, l = context_list; l; l = l->next) {
		ctx = l->data;
		*i++ = g_strdup(ctx->path);
	}

	return objlist;
}

static void gprs_attached_update(struct ofono_gprs *gprs)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path;
	ofono_bool_t attached;
	dbus_bool_t value;

	attached = gprs->driver_attached &&
		(gprs->status == NETWORK_REGISTRATION_STATUS_REGISTERED ||
			gprs->status == NETWORK_REGISTRATION_STATUS_ROAMING);

	if (attached == gprs->attached)
		return;

	gprs->attached = attached;

	if (gprs->attached == FALSE) {
		GSList *l;
		struct pri_context *ctx;

		for (l = gprs->contexts; l; l = l->next) {
			ctx = l->data;

			if (ctx->active == FALSE)
				continue;

			gprs_cid_release(gprs, ctx->context.cid);
			ctx->context.cid = 0;

			ctx->active = FALSE;
			pri_reset_context_settings(ctx);

			value = FALSE;
			ofono_dbus_signal_property_changed(conn, ctx->path,
						OFONO_DATA_CONTEXT_INTERFACE,
						"Active", DBUS_TYPE_BOOLEAN,
						&value);
		}
	}

	path = __ofono_atom_get_path(gprs->atom);
	value = attached;
	ofono_dbus_signal_property_changed(conn, path,
				OFONO_DATA_CONNECTION_MANAGER_INTERFACE,
				"Attached", DBUS_TYPE_BOOLEAN, &value);
}

static void registration_status_cb(const struct ofono_error *error,
					int status, void *data)
{
	struct ofono_gprs *gprs = data;

	if (error->type == OFONO_ERROR_TYPE_NO_ERROR)
		ofono_gprs_status_notify(gprs, status);

	if (gprs->flags & GPRS_FLAG_RECHECK) {
		gprs->flags &= ~GPRS_FLAG_RECHECK;
		gprs_netreg_update(gprs);
	}
}

static void gprs_attach_callback(const struct ofono_error *error, void *data)
{
	struct ofono_gprs *gprs = data;

	gprs->flags &= ~GPRS_FLAG_ATTACHING;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR)
		gprs->driver_attached = !gprs->driver_attached;

	if (gprs->driver->attached_status) {
		gprs->driver->attached_status(gprs, registration_status_cb,
						gprs);
		return;
	}

	gprs_attached_update(gprs);

	if (gprs->flags & GPRS_FLAG_RECHECK) {
		gprs->flags &= ~GPRS_FLAG_RECHECK;
		gprs_netreg_update(gprs);
	}
}

static void gprs_netreg_update(struct ofono_gprs *gprs)
{
	ofono_bool_t attach;

	attach = gprs->netreg_status == NETWORK_REGISTRATION_STATUS_REGISTERED;

	attach = attach || (gprs->roaming_allowed &&
		gprs->netreg_status == NETWORK_REGISTRATION_STATUS_ROAMING);

	attach = attach && gprs->powered;

	if (gprs->driver_attached == attach)
		return;

	if (gprs->flags & GPRS_FLAG_ATTACHING) {
		gprs->flags |= GPRS_FLAG_RECHECK;
		return;
	}

	gprs->flags |= GPRS_FLAG_ATTACHING;

	gprs->driver->set_attached(gprs, attach, gprs_attach_callback, gprs);
	gprs->driver_attached = attach;
}

static void netreg_status_changed(int status, int lac, int ci, int tech,
					const char *mcc, const char *mnc,
					void *data)
{
	struct ofono_gprs *gprs = data;

	DBG("%d", status);

	if (gprs->netreg_status == status)
		return;

	gprs->netreg_status = status;

	gprs_netreg_update(gprs);
}

static DBusMessage *gprs_get_properties(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct ofono_gprs *gprs = data;
	DBusMessage *reply;
	DBusMessageIter iter;
	DBusMessageIter dict;
	char **objpath_list;
	dbus_bool_t value;

	reply = dbus_message_new_method_return(msg);
	if (!reply)
		return NULL;

	dbus_message_iter_init_append(reply, &iter);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					OFONO_PROPERTIES_ARRAY_SIGNATURE,
					&dict);

	objpath_list = gprs_contexts_path_list(gprs->contexts);
	if (!objpath_list)
		return NULL;

	ofono_dbus_dict_append_array(&dict, "PrimaryContexts",
					DBUS_TYPE_OBJECT_PATH, &objpath_list);

	g_strfreev(objpath_list);

	value = gprs->attached;
	ofono_dbus_dict_append(&dict, "Attached", DBUS_TYPE_BOOLEAN, &value);

	value = gprs->roaming_allowed;
	ofono_dbus_dict_append(&dict, "RoamingAllowed",
				DBUS_TYPE_BOOLEAN, &value);

	value = gprs->powered;
	ofono_dbus_dict_append(&dict, "Powered", DBUS_TYPE_BOOLEAN, &value);

	dbus_message_iter_close_container(&iter, &dict);

	return reply;
}

static DBusMessage *gprs_set_property(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct ofono_gprs *gprs = data;
	DBusMessageIter iter;
	DBusMessageIter var;
	const char *property;
	dbus_bool_t value;
	const char *path;

	if (gprs->pending)
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

	if (!strcmp(property, "RoamingAllowed")) {
		if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_BOOLEAN)
			return __ofono_error_invalid_args(msg);

		dbus_message_iter_get_basic(&var, &value);

		if (gprs->roaming_allowed == (ofono_bool_t) value)
			return dbus_message_new_method_return(msg);

		gprs->roaming_allowed = value;

		if (gprs->settings) {
			g_key_file_set_integer(gprs->settings, SETTINGS_GROUP,
						"RoamingAllowed",
						gprs->roaming_allowed);
			storage_sync(gprs->imsi, SETTINGS_STORE,
					gprs->settings);
		}

		gprs_netreg_update(gprs);
	} else if (!strcmp(property, "Powered")) {
		if (!gprs->driver->set_attached)
			return __ofono_error_not_implemented(msg);

		if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_BOOLEAN)
			return __ofono_error_invalid_args(msg);

		dbus_message_iter_get_basic(&var, &value);

		if (gprs->powered == (ofono_bool_t) value)
			return dbus_message_new_method_return(msg);

		gprs->powered = value;

		if (gprs->settings) {
			g_key_file_set_integer(gprs->settings, SETTINGS_GROUP,
						"Powered", gprs->powered);
			storage_sync(gprs->imsi, SETTINGS_STORE,
					gprs->settings);
		}

		gprs_netreg_update(gprs);
	} else {
		return __ofono_error_invalid_args(msg);
	}

	path = __ofono_atom_get_path(gprs->atom);
	ofono_dbus_signal_property_changed(conn, path,
					OFONO_DATA_CONNECTION_MANAGER_INTERFACE,
					property, DBUS_TYPE_BOOLEAN, &value);

	return dbus_message_new_method_return(msg);
}

static DBusMessage *gprs_create_context(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct ofono_gprs *gprs = data;
	struct pri_context *context;
	const char *name;
	const char *typestr;
	const char *path;
	enum gprs_context_type type;
	char **objpath_list;
	unsigned int id;

	if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &name,
					DBUS_TYPE_STRING, &typestr,
					DBUS_TYPE_INVALID))
		return __ofono_error_invalid_args(msg);

	if (strlen(name) == 0 || strlen(name) > MAX_CONTEXT_NAME_LENGTH)
		return __ofono_error_invalid_format(msg);

	type = gprs_context_string_to_type(typestr);

	if (type == GPRS_CONTEXT_TYPE_INVALID)
		return __ofono_error_invalid_format(msg);

	if (gprs->last_context_id)
		id = idmap_alloc_next(gprs->pid_map, gprs->last_context_id);
	else
		id = idmap_alloc(gprs->pid_map);

	if (id > idmap_get_max(gprs->pid_map))
		return __ofono_error_not_supported(msg);

	context = pri_context_create(gprs, name, type);
	context->id = id;

	if (!context) {
		ofono_error("Unable to allocate context struct");
		return __ofono_error_failed(msg);
	}

	DBG("Registering new context");

	if (!context_dbus_register(context)) {
		ofono_error("Unable to register primary context");
		return __ofono_error_failed(msg);
	}

	gprs->last_context_id = id;

	if (gprs->settings) {
		g_key_file_set_string(gprs->settings, context->key,
					"Name", context->name);
		g_key_file_set_string(gprs->settings, context->key,
					"AccessPointName",
					context->context.apn);
		g_key_file_set_string(gprs->settings, context->key,
					"Username", context->context.username);
		g_key_file_set_string(gprs->settings, context->key,
					"Password", context->context.password);
		g_key_file_set_string(gprs->settings, context->key, "Type",
				gprs_context_type_to_string(context->type));
		g_key_file_set_string(gprs->settings, context->key, "Protocol",
				gprs_proto_to_string(context->context.proto));
		storage_sync(gprs->imsi, SETTINGS_STORE, gprs->settings);
	}

	gprs->contexts = g_slist_append(gprs->contexts, context);

	objpath_list = gprs_contexts_path_list(gprs->contexts);

	if (objpath_list) {
		path = __ofono_atom_get_path(gprs->atom);
		ofono_dbus_signal_array_property_changed(conn, path,
					OFONO_DATA_CONNECTION_MANAGER_INTERFACE,
					"PrimaryContexts",
					DBUS_TYPE_OBJECT_PATH, &objpath_list);

		g_strfreev(objpath_list);
	}

	path = context->path;

	return g_dbus_create_reply(msg, DBUS_TYPE_OBJECT_PATH, &path,
					DBUS_TYPE_INVALID);
}

static void gprs_deactivate_for_remove(const struct ofono_error *error,
						void *data)
{
	struct pri_context *ctx = data;
	struct ofono_gprs *gprs = ctx->gprs;
	char **objpath_list;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Removing context failed with error: %s",
				telephony_error_to_str(error));

		__ofono_dbus_pending_reply(&gprs->pending,
					__ofono_error_failed(gprs->pending));
		return;
	}

	if (gprs->settings) {
		g_key_file_remove_group(gprs->settings, ctx->key, NULL);
		storage_sync(gprs->imsi, SETTINGS_STORE, gprs->settings);
	}

	context_dbus_unregister(ctx);
	gprs->contexts = g_slist_remove(gprs->contexts, ctx);

	__ofono_dbus_pending_reply(&gprs->pending,
				dbus_message_new_method_return(gprs->pending));

	objpath_list = gprs_contexts_path_list(gprs->contexts);

	if (objpath_list) {
		const char *path = __ofono_atom_get_path(gprs->atom);
		DBusConnection *conn = ofono_dbus_get_connection();

		ofono_dbus_signal_array_property_changed(conn, path,
					OFONO_DATA_CONNECTION_MANAGER_INTERFACE,
					"PrimaryContexts",
					DBUS_TYPE_OBJECT_PATH, &objpath_list);
		g_strfreev(objpath_list);
	}
}

static DBusMessage *gprs_remove_context(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct ofono_gprs *gprs = data;
	struct pri_context *ctx;
	const char *path;
	char **objpath_list;

	if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_OBJECT_PATH, &path,
					DBUS_TYPE_INVALID))
		return __ofono_error_invalid_args(msg);

	if (path[0] == '\0')
		return __ofono_error_invalid_format(msg);

	ctx = gprs_context_by_path(gprs, path);
	if (!ctx)
		return __ofono_error_not_found(msg);

	if (ctx->active) {
		struct ofono_gprs_context *gc = gprs->context_driver;

		gprs->pending = dbus_message_ref(msg);
		gc->driver->deactivate_primary(gc, ctx->context.cid,
					gprs_deactivate_for_remove, ctx);
		return NULL;
	}

	if (gprs->settings) {
		g_key_file_remove_group(gprs->settings, ctx->key, NULL);
		storage_sync(gprs->imsi, SETTINGS_STORE, gprs->settings);
	}

	DBG("Unregistering context: %s\n", ctx->path);
	context_dbus_unregister(ctx);
	gprs->contexts = g_slist_remove(gprs->contexts, ctx);

	g_dbus_send_reply(conn, msg, DBUS_TYPE_INVALID);

	objpath_list = gprs_contexts_path_list(gprs->contexts);

	if (objpath_list) {
		path = __ofono_atom_get_path(gprs->atom);
		ofono_dbus_signal_array_property_changed(conn, path,
					OFONO_DATA_CONNECTION_MANAGER_INTERFACE,
					"PrimaryContexts",
					DBUS_TYPE_OBJECT_PATH, &objpath_list);
		g_strfreev(objpath_list);
	}

	return NULL;
}

static DBusMessage *gprs_deactivate_all(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct ofono_gprs *gprs = data;

	if (gprs->pending)
		return __ofono_error_busy(msg);

	if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INVALID))
		return __ofono_error_invalid_args(msg);

	return __ofono_error_not_implemented(msg);
}

static GDBusMethodTable manager_methods[] = {
	{ "GetProperties",	"",	"a{sv}",	gprs_get_properties },
	{ "SetProperty",	"sv",	"",		gprs_set_property },
	{ "CreateContext",	"ss",	"o",		gprs_create_context },
	{ "RemoveContext",	"o",	"",		gprs_remove_context,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ "DeactivateAll",	"",	"",		gprs_deactivate_all,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ }
};

static GDBusSignalTable manager_signals[] = {
	{ "PropertyChanged",	"sv" },
	{ }
};

void ofono_gprs_detached_notify(struct ofono_gprs *gprs)
{
	if (gprs->driver_attached == FALSE)
		return;

	gprs->driver_attached = FALSE;

	gprs_attached_update(gprs);

	/* TODO: The network forced a detach, we should wait for some time
	 * and try to re-attach
	 */
}

void ofono_gprs_status_notify(struct ofono_gprs *gprs, int status)
{
	/* If we are not attached and haven't tried to attach, ignore */
	if (gprs->driver_attached == FALSE)
		return;

	gprs->status = status;
	gprs_attached_update(gprs);
}

void ofono_gprs_set_cid_range(struct ofono_gprs *gprs,
				unsigned int min, unsigned int max)
{
	if (gprs == NULL)
		return;

	if (gprs->cid_map)
		idmap_free(gprs->cid_map);

	gprs->cid_map = idmap_new_from_range(min, max);
}

static void gprs_context_unregister(struct ofono_atom *atom)
{
	struct ofono_gprs_context *gc = __ofono_atom_get_data(atom);

	if (gc->gprs)
		gc->gprs->context_driver = NULL;

	gc->gprs = NULL;
}

void ofono_gprs_add_context(struct ofono_gprs *gprs,
				struct ofono_gprs_context *gc)
{
	gprs->context_driver = gc;
	gc->gprs = gprs;

	__ofono_atom_register(gc->atom, gprs_context_unregister);
}

void ofono_gprs_context_deactivated(struct ofono_gprs_context *gc,
					unsigned int cid)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	GSList *l;
	struct pri_context *ctx;
	dbus_bool_t value;

	for (l = gc->gprs->contexts; l; l = l->next) {
		ctx = l->data;

		if (ctx->active == FALSE)
			continue;

		if (ctx->context.cid != cid)
			continue;

		gprs_cid_release(ctx->gprs, ctx->context.cid);
		ctx->context.cid = 0;

		ctx->active = FALSE;
		pri_reset_context_settings(ctx);

		value = FALSE;
		ofono_dbus_signal_property_changed(conn, ctx->path,
						OFONO_DATA_CONTEXT_INTERFACE,
						"Active", DBUS_TYPE_BOOLEAN,
						&value);
	}
}

int ofono_gprs_context_driver_register(const struct ofono_gprs_context_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	if (d->probe == NULL)
		return -EINVAL;

	g_context_drivers = g_slist_prepend(g_context_drivers, (void *)d);

	return 0;
}

void ofono_gprs_context_driver_unregister(const struct ofono_gprs_context_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	g_context_drivers = g_slist_remove(g_context_drivers, (void *)d);
}

static void gprs_context_remove(struct ofono_atom *atom)
{
	struct ofono_gprs_context *gc = __ofono_atom_get_data(atom);

	DBG("atom: %p", atom);

	if (gc == NULL)
		return;

	if (gc->driver && gc->driver->remove)
		gc->driver->remove(gc);

	g_free(gc);
}

struct ofono_gprs_context *ofono_gprs_context_create(struct ofono_modem *modem,
						unsigned int vendor,
						const char *driver, void *data)
{
	struct ofono_gprs_context *gc;
	GSList *l;

	if (driver == NULL)
		return NULL;

	gc = g_try_new0(struct ofono_gprs_context, 1);

	if (gc == NULL)
		return NULL;

	gc->atom = __ofono_modem_add_atom(modem, OFONO_ATOM_TYPE_GPRS_CONTEXT,
						gprs_context_remove, gc);

	for (l = g_context_drivers; l; l = l->next) {
		const struct ofono_gprs_context_driver *drv = l->data;

		if (g_strcmp0(drv->name, driver))
			continue;

		if (drv->probe(gc, vendor, data) < 0)
			continue;

		gc->driver = drv;
		break;
	}

	return gc;
}

void ofono_gprs_context_remove(struct ofono_gprs_context *gc)
{
	__ofono_atom_free(gc->atom);
}

void ofono_gprs_context_set_data(struct ofono_gprs_context *gc, void *data)
{
	gc->driver_data = data;
}

void *ofono_gprs_context_get_data(struct ofono_gprs_context *gc)
{
	return gc->driver_data;
}

struct ofono_modem *ofono_gprs_context_get_modem(struct ofono_gprs_context *gc)
{
	return __ofono_atom_get_modem(gc->atom);
}

int ofono_gprs_driver_register(const struct ofono_gprs_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	if (d->probe == NULL)
		return -EINVAL;

	g_drivers = g_slist_prepend(g_drivers, (void *)d);

	return 0;
}

void ofono_gprs_driver_unregister(const struct ofono_gprs_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	g_drivers = g_slist_remove(g_drivers, (void *)d);
}

static void gprs_unregister(struct ofono_atom *atom)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	struct ofono_gprs *gprs = __ofono_atom_get_data(atom);
	struct ofono_modem *modem = __ofono_atom_get_modem(atom);
	const char *path = __ofono_atom_get_path(atom);
	GSList *l;

	if (gprs->settings) {
		storage_close(gprs->imsi, SETTINGS_STORE,
				gprs->settings, TRUE);

		g_free(gprs->imsi);
		gprs->imsi = NULL;
		gprs->settings = NULL;
	}

	for (l = gprs->contexts; l; l = l->next) {
		struct pri_context *context = l->data;

		context_dbus_unregister(context);
	}

	g_slist_free(gprs->contexts);

	if (gprs->cid_map) {
		idmap_free(gprs->cid_map);
		gprs->cid_map = NULL;
	}

	if (gprs->netreg_watch) {
		if (gprs->status_watch) {
			__ofono_netreg_remove_status_watch(gprs->netreg,
							gprs->status_watch);
			gprs->status_watch = 0;
		}

		__ofono_modem_remove_atom_watch(modem, gprs->netreg_watch);
		gprs->netreg_watch = 0;
		gprs->netreg = NULL;
	}

	ofono_modem_remove_interface(modem,
					OFONO_DATA_CONNECTION_MANAGER_INTERFACE);
	g_dbus_unregister_interface(conn, path,
					OFONO_DATA_CONNECTION_MANAGER_INTERFACE);
}

static void gprs_remove(struct ofono_atom *atom)
{
	struct ofono_gprs *gprs = __ofono_atom_get_data(atom);

	DBG("atom: %p", atom);

	if (gprs == NULL)
		return;

	if (gprs->pid_map) {
		idmap_free(gprs->pid_map);
		gprs->pid_map = NULL;
	}

	if (gprs->context_driver) {
		gprs->context_driver->gprs = NULL;
		gprs->context_driver = NULL;
	}

	if (gprs->driver && gprs->driver->remove)
		gprs->driver->remove(gprs);

	g_free(gprs);
}

struct ofono_gprs *ofono_gprs_create(struct ofono_modem *modem,
					unsigned int vendor,
					const char *driver, void *data)
{
	struct ofono_gprs *gprs;
	GSList *l;

	if (driver == NULL)
		return NULL;

	gprs = g_try_new0(struct ofono_gprs, 1);

	if (gprs == NULL)
		return NULL;

	gprs->atom = __ofono_modem_add_atom(modem, OFONO_ATOM_TYPE_GPRS,
						gprs_remove, gprs);

	for (l = g_drivers; l; l = l->next) {
		const struct ofono_gprs_driver *drv = l->data;

		if (g_strcmp0(drv->name, driver))
			continue;

		if (drv->probe(gprs, vendor, data) < 0)
			continue;

		gprs->driver = drv;
		break;
	}

	gprs->status = NETWORK_REGISTRATION_STATUS_UNKNOWN;
	gprs->netreg_status = NETWORK_REGISTRATION_STATUS_UNKNOWN;
	gprs->pid_map = idmap_new(MAX_CONTEXTS);

	return gprs;
}

static void netreg_watch(struct ofono_atom *atom,
				enum ofono_atom_watch_condition cond,
				void *data)
{
	struct ofono_gprs *gprs = data;

	if (cond == OFONO_ATOM_WATCH_CONDITION_UNREGISTERED) {
		gprs->status_watch = 0;
		gprs->netreg = NULL;
		return;
	}

	gprs->netreg = __ofono_atom_get_data(atom);
	gprs->netreg_status = ofono_netreg_get_status(gprs->netreg);
	gprs->status_watch = __ofono_netreg_add_status_watch(gprs->netreg,
					netreg_status_changed, gprs, NULL);

	gprs_netreg_update(gprs);
}

static gboolean load_context(struct ofono_gprs *gprs, const char *group)
{
	char *name = NULL;
	char *typestr = NULL;
	char *protostr = NULL;
	char *username = NULL;
	char *password = NULL;
	char *apn = NULL;
	gboolean ret = FALSE;
	struct pri_context *context;
	enum gprs_context_type type;
	enum ofono_gprs_proto proto;
	unsigned int id;

	if (sscanf(group, "primarycontext%d", &id) != 1)
		goto error;

	if (id < 1 || id > MAX_CONTEXTS)
		goto error;

	if ((name = g_key_file_get_string(gprs->settings, group,
					"Name", NULL)) == NULL)
		goto error;

	if ((typestr = g_key_file_get_string(gprs->settings, group,
					"Type", NULL)) == NULL)
		goto error;

	type = gprs_context_string_to_type(typestr);
	if (type == GPRS_CONTEXT_TYPE_INVALID)
		goto error;

	if ((protostr = g_key_file_get_string(gprs->settings, group,
						"Protocol", NULL)) == NULL)
		protostr = g_strdup("ip");

	if (gprs_proto_from_string(protostr, &proto) == FALSE)
		goto error;

	username = g_key_file_get_string(gprs->settings, group,
						"Username", NULL);
	if (!username)
		goto error;

	if (strlen(username) > OFONO_GPRS_MAX_USERNAME_LENGTH)
		goto error;

	password = g_key_file_get_string(gprs->settings, group,
						"Password", NULL);

	if (!password)
		goto error;

	if (strlen(password) > OFONO_GPRS_MAX_PASSWORD_LENGTH)
		goto error;

	apn = g_key_file_get_string(gprs->settings, group,
					"AccessPointName", NULL);

	if (!apn)
		goto error;

	if (strlen(apn) > OFONO_GPRS_MAX_APN_LENGTH)
		goto error;

	/* Accept empty (just created) APNs, but don't allow other
	 * invalid ones */
	if (apn[0] != '\0' && is_valid_apn(apn) == FALSE)
		goto error;

	if ((context = pri_context_create(gprs, name, type)) == NULL)
		goto error;

	idmap_take(gprs->pid_map, id);
	context->id = id;
	strcpy(context->context.username, username);
	strcpy(context->context.password, password);
	strcpy(context->context.apn, apn);
	context->context.proto = proto;

	if (context_dbus_register(context) == FALSE)
		goto error;

	gprs->last_context_id = id;

	gprs->contexts = g_slist_append(gprs->contexts, context);
	ret = TRUE;

error:
	g_free(name);
	g_free(typestr);
	g_free(protostr);
	g_free(username);
	g_free(password);
	g_free(apn);

	return ret;
}

static void gprs_load_settings(struct ofono_gprs *gprs, const char *imsi)
{
	GError *error = NULL;
	char **groups;
	int i;

	gprs->settings = storage_open(imsi, SETTINGS_STORE);

	if (gprs->settings == NULL)
		return;

	gprs->imsi = g_strdup(imsi);

	gprs->powered = g_key_file_get_boolean(gprs->settings, SETTINGS_GROUP,
						"Powered", &error);

	/*
	 * If any error occurs, simply switch to defaults.
	 * Default to Powered = True
	 * and RoamingAllowed = True
	 */
	if (error) {
		gprs->powered = TRUE;
		g_key_file_set_boolean(gprs->settings, SETTINGS_GROUP,
					"Powered", gprs->powered);
	}

	gprs->roaming_allowed = g_key_file_get_boolean(gprs->settings,
							SETTINGS_GROUP,
							"RoamingAllowed", NULL);

	if (error) {
		gprs->roaming_allowed = TRUE;
		g_key_file_set_boolean(gprs->settings, SETTINGS_GROUP,
					"RoamingAllowed",
					gprs->roaming_allowed);
	}

	groups = g_key_file_get_groups(gprs->settings, NULL);

	for (i = 0; groups[i]; i++) {

		if (g_str_equal(groups[i], SETTINGS_GROUP))
			continue;

		if (!g_str_has_prefix(groups[i], "primarycontext"))
			goto remove;

		if (load_context(gprs, groups[i]) == TRUE)
			continue;

remove:
		g_key_file_remove_group(gprs->settings, groups[i], NULL);
	}

	g_strfreev(groups);
}

void ofono_gprs_register(struct ofono_gprs *gprs)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	struct ofono_modem *modem = __ofono_atom_get_modem(gprs->atom);
	const char *path = __ofono_atom_get_path(gprs->atom);
	struct ofono_atom *netreg_atom;
	struct ofono_atom *sim_atom;

	if (!g_dbus_register_interface(conn, path,
					OFONO_DATA_CONNECTION_MANAGER_INTERFACE,
					manager_methods, manager_signals, NULL,
					gprs, NULL)) {
		ofono_error("Could not create %s interface",
				OFONO_DATA_CONNECTION_MANAGER_INTERFACE);

		return;
	}

	ofono_modem_add_interface(modem,
				OFONO_DATA_CONNECTION_MANAGER_INTERFACE);

	sim_atom = __ofono_modem_find_atom(modem, OFONO_ATOM_TYPE_SIM);

	if (sim_atom) {
		struct ofono_sim *sim = __ofono_atom_get_data(sim_atom);
		const char *imsi = ofono_sim_get_imsi(sim);

		gprs_load_settings(gprs, imsi);
	}

	gprs->netreg_watch = __ofono_modem_add_atom_watch(modem,
					OFONO_ATOM_TYPE_NETREG,
					netreg_watch, gprs, NULL);

	netreg_atom = __ofono_modem_find_atom(modem, OFONO_ATOM_TYPE_NETREG);

	if (netreg_atom && __ofono_atom_get_registered(netreg_atom))
		netreg_watch(netreg_atom,
				OFONO_ATOM_WATCH_CONDITION_REGISTERED, gprs);

	__ofono_atom_register(gprs->atom, gprs_unregister);
}

void ofono_gprs_remove(struct ofono_gprs *gprs)
{
	__ofono_atom_free(gprs->atom);
}

void ofono_gprs_set_data(struct ofono_gprs *gprs, void *data)
{
	gprs->driver_data = data;
}

void *ofono_gprs_get_data(struct ofono_gprs *gprs)
{
	return gprs->driver_data;
}
