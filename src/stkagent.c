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

#define _GNU_SOURCE
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <glib.h>
#include <gdbus.h>

#include "ofono.h"

#include "stkagent.h"

enum allowed_error {
	ALLOWED_ERROR_GO_BACK	= 0x1,
	ALLOWED_ERROR_TERMINATE	= 0x2,
};

struct stk_agent {
	char *path;				/* Agent Path */
	char *bus;				/* Agent bus */
	guint disconnect_watch;			/* DBus disconnect watch */
	ofono_bool_t remove_on_terminate;
	ofono_destroy_func removed_cb;
	void *removed_data;
	DBusMessage *msg;
	DBusPendingCall *call;
	void *user_cb;
	void *user_data;
	ofono_destroy_func user_destroy;

	const struct stk_menu *request_selection_menu;
};

#define ERROR_PREFIX OFONO_SERVICE ".Error"
#define GOBACK_ERROR ERROR_PREFIX ".GoBack"
#define TERMINATE_ERROR ERROR_PREFIX ".EndSession"

static void stk_agent_send_noreply(struct stk_agent *agent, const char *method)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	DBusMessage *message;

	message = dbus_message_new_method_call(agent->bus, agent->path,
						OFONO_SIM_APP_INTERFACE,
						method);
	if (message == NULL)
		return;

	dbus_message_set_no_reply(message, TRUE);

	g_dbus_send_message(conn, message);
}

static inline void stk_agent_send_release(struct stk_agent *agent)
{
	stk_agent_send_noreply(agent, "Release");
}

static inline void stk_agent_send_cancel(struct stk_agent *agent)
{
	stk_agent_send_noreply(agent, "Cancel");
}

static void stk_agent_request_end(struct stk_agent *agent)
{
	if (agent->msg) {
		dbus_message_unref(agent->msg);
		agent->msg = NULL;
	}

	if (agent->call) {
		dbus_pending_call_unref(agent->call);
		agent->call = NULL;
	}

	if (agent->user_destroy)
		agent->user_destroy(agent->user_data);

	agent->user_destroy = NULL;
	agent->user_data = NULL;
	agent->user_cb = NULL;
}

ofono_bool_t stk_agent_matches(struct stk_agent *agent,
				const char *path, const char *sender)
{
	return !strcmp(agent->path, path) && !strcmp(agent->bus, sender);
}

void stk_agent_set_removed_notify(struct stk_agent *agent,
					ofono_destroy_func destroy,
					void *user_data)
{
	agent->removed_cb = destroy;
	agent->removed_data = user_data;
}

void stk_agent_request_cancel(struct stk_agent *agent)
{
	if (agent->call == NULL)
		return;

	dbus_pending_call_cancel(agent->call);
	stk_agent_send_cancel(agent);
	stk_agent_request_end(agent);
}

void stk_agent_free(struct stk_agent *agent)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	gboolean busy = agent->call != NULL;

	if (agent->disconnect_watch) {
		if (busy)
			stk_agent_send_cancel(agent);

		stk_agent_send_release(agent);

		g_dbus_remove_watch(conn, agent->disconnect_watch);
		agent->disconnect_watch = 0;
	}

	if (agent->removed_cb)
		agent->removed_cb(agent->removed_data);

	g_free(agent->path);
	g_free(agent->bus);
	g_free(agent);
}

static int check_error(struct stk_agent *agent, DBusMessage *reply,
				int allowed_errors,
				enum stk_agent_result *out_result)
{
	DBusError err;
	int result = 0;

	dbus_error_init(&err);

	if (dbus_set_error_from_message(&err, reply) == FALSE) {
		*out_result = STK_AGENT_RESULT_OK;
		return 0;
	}

	ofono_debug("SimToolkitAgent %s replied with error %s, %s",
			agent->path, err.name, err.message);

	/* Timeout is always valid */
	if (g_str_equal(err.name, DBUS_ERROR_NO_REPLY)) {
		/* Send a Cancel() to the agent since its taking too long */
		stk_agent_send_cancel(agent);
		*out_result = STK_AGENT_RESULT_TIMEOUT;
		goto out;
	}

	if ((allowed_errors & ALLOWED_ERROR_GO_BACK) &&
			g_str_equal(err.name, GOBACK_ERROR)) {
		*out_result = STK_AGENT_RESULT_BACK;
		goto out;
	}

	if ((allowed_errors & ALLOWED_ERROR_TERMINATE) &&
			g_str_equal(err.name, TERMINATE_ERROR)) {
		*out_result = STK_AGENT_RESULT_TERMINATE;
		goto out;
	}

	result = -EINVAL;

out:
	dbus_error_free(&err);
	return result;
}

static void stk_agent_disconnect_cb(DBusConnection *conn, void *user_data)
{
	struct stk_agent *agent = user_data;

	ofono_debug("Agent exited without calling Unregister");

	agent->disconnect_watch = 0;

	stk_agent_free(agent);
}

struct stk_agent *stk_agent_new(const char *path, const char *sender,
				ofono_bool_t remove_on_terminate)
{
	struct stk_agent *agent = g_try_new0(struct stk_agent, 1);
	DBusConnection *conn = ofono_dbus_get_connection();

	if (!agent)
		return NULL;

	agent->path = g_strdup(path);
	agent->bus = g_strdup(sender);
	agent->remove_on_terminate = remove_on_terminate;

	agent->disconnect_watch = g_dbus_add_disconnect_watch(conn, sender,
							stk_agent_disconnect_cb,
							agent, NULL);

	return agent;
}

static void append_menu_items(DBusMessageIter *iter,
				const struct stk_menu_item *item)
{
	DBusMessageIter array, entry;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
						"(sy)", &array);

	for (; item->text; item++) {
		dbus_message_iter_open_container(&array, DBUS_TYPE_STRUCT,
							NULL, &entry);

		dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING,
						&item->text);
		dbus_message_iter_append_basic(&entry, DBUS_TYPE_BYTE,
						&item->icon_id);

		dbus_message_iter_close_container(&array, &entry);
	}

	dbus_message_iter_close_container(iter, &array);
}

void append_menu_items_variant(DBusMessageIter *iter,
				const struct stk_menu_item *items)
{
	DBusMessageIter variant;

	dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
						"a(sy)", &variant);

	append_menu_items(&variant, items);

	dbus_message_iter_close_container(iter, &variant);
}

static void request_selection_cb(DBusPendingCall *call, void *data)
{
	struct stk_agent *agent = data;
	const struct stk_menu *menu = agent->request_selection_menu;
	stk_agent_selection_cb cb = (stk_agent_selection_cb) agent->user_cb;
	DBusMessage *reply = dbus_pending_call_steal_reply(call);
	unsigned char selection, i;
	enum stk_agent_result result;
	gboolean remove_agent;

	if (check_error(agent, reply,
			ALLOWED_ERROR_GO_BACK | ALLOWED_ERROR_TERMINATE,
			&result) == -EINVAL) {
		remove_agent = TRUE;
		goto error;
	}

	if (result != STK_AGENT_RESULT_OK) {
		cb(result, 0, agent->user_data);
		goto done;
	}

	if (dbus_message_get_args(reply, NULL,
					DBUS_TYPE_BYTE, &selection,
					DBUS_TYPE_INVALID) == FALSE) {
		ofono_error("Can't parse the reply to RequestSelection()");
		remove_agent = TRUE;
		goto error;
	}

	for (i = 0; i < selection && menu->items[i].text; i++);

	if (i != selection) {
		ofono_error("Invalid item selected");
		remove_agent = TRUE;
		goto error;
	}

	cb(result, menu->items[selection].item_id, agent->user_data);

done:
	if (result == STK_AGENT_RESULT_TERMINATE && agent->remove_on_terminate)
		remove_agent = TRUE;
	else
		remove_agent = FALSE;

error:
	stk_agent_request_end(agent);
	dbus_message_unref(reply);

	if (remove_agent)
		stk_agent_free(agent);
}

int stk_agent_request_selection(struct stk_agent *agent,
				const struct stk_menu *menu,
				stk_agent_selection_cb cb,
				void *user_data, ofono_destroy_func destroy,
				int timeout)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	dbus_int16_t default_item = menu->default_item;
	DBusMessageIter iter;

	agent->msg = dbus_message_new_method_call(agent->bus, agent->path,
							OFONO_SIM_APP_INTERFACE,
							"RequestSelection");
	if (agent->msg == NULL)
		return -ENOMEM;

	dbus_message_iter_init_append(agent->msg, &iter);

	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &menu->title);
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_BYTE, &menu->icon_id);
	append_menu_items(&iter, menu->items);
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT16, &default_item);

	if (dbus_connection_send_with_reply(conn, agent->msg, &agent->call,
						timeout) == FALSE ||
			agent->call == NULL)
		return -EIO;

	agent->user_cb = cb;
	agent->user_data = user_data;
	agent->user_destroy = destroy;

	agent->request_selection_menu = menu;

	dbus_pending_call_set_notify(agent->call, request_selection_cb,
					agent, NULL);

	return 0;
}

static void display_text_cb(DBusPendingCall *call, void *data)
{
	struct stk_agent *agent = data;
	stk_agent_display_text_cb cb = agent->user_cb;
	DBusMessage *reply = dbus_pending_call_steal_reply(call);
	enum stk_agent_result result;
	gboolean remove_agent;

	if (check_error(agent, reply,
			ALLOWED_ERROR_GO_BACK | ALLOWED_ERROR_TERMINATE,
			&result) == -EINVAL) {
		remove_agent = TRUE;
		goto error;
	}

	if (result == STK_AGENT_RESULT_OK && dbus_message_get_args(
				reply, NULL, DBUS_TYPE_INVALID) == FALSE) {
		ofono_error("Can't parse the reply to DisplayText()");
		remove_agent = TRUE;
		goto error;
	}

	cb(result, agent->user_data);

	if (result == STK_AGENT_RESULT_TERMINATE && agent->remove_on_terminate)
		remove_agent = TRUE;
	else
		remove_agent = FALSE;

error:
	stk_agent_request_end(agent);
	dbus_message_unref(reply);

	if (remove_agent)
		stk_agent_free(agent);
}

int stk_agent_display_text(struct stk_agent *agent, const char *text,
				uint8_t icon_id, ofono_bool_t urgent,
				stk_agent_display_text_cb cb,
				void *user_data, ofono_destroy_func destroy,
				int timeout)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	dbus_bool_t priority = urgent;

	agent->msg = dbus_message_new_method_call(agent->bus, agent->path,
							OFONO_SIM_APP_INTERFACE,
							"DisplayText");
	if (agent->msg == NULL)
		return -ENOMEM;

	dbus_message_append_args(agent->msg,
					DBUS_TYPE_STRING, &text,
					DBUS_TYPE_BYTE, &icon_id,
					DBUS_TYPE_BOOLEAN, &priority,
					DBUS_TYPE_INVALID);

	if (dbus_connection_send_with_reply(conn, agent->msg, &agent->call,
						timeout) == FALSE ||
			agent->call == NULL)
		return -EIO;

	agent->user_cb = cb;
	agent->user_data = user_data;
	agent->user_destroy = destroy;

	dbus_pending_call_set_notify(agent->call, display_text_cb,
					agent, NULL);

	return 0;
}
