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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <glib.h>

#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/voicecall.h>

#include "gatchat.h"
#include "gatresult.h"

#include "atmodem.h"

/* Amount of ms we wait between CLCC calls */
#define POLL_CLCC_INTERVAL 500

 /* Amount of time we give for CLIP to arrive before we commence CLCC poll */
#define CLIP_INTERVAL 200

static const char *clcc_prefix[] = { "+CLCC:", NULL };
static const char *none_prefix[] = { NULL };

/* According to 27.007 COLP is an intermediate status for ATD */
static const char *atd_prefix[] = { "+COLP:", NULL };

struct voicecall_data {
	GSList *calls;
	unsigned int local_release;
	unsigned int clcc_source;
	GAtChat *chat;
};

struct release_id_req {
	struct ofono_voicecall *vc;
	ofono_voicecall_cb_t cb;
	void *data;
	int id;
};

struct change_state_req {
	struct ofono_voicecall *vc;
	ofono_voicecall_cb_t cb;
	void *data;
	int affected_types;
};

static gboolean poll_clcc(gpointer user_data);

static int class_to_call_type(int cls)
{
	switch (cls) {
	case 1:
		return 0;
	case 4:
		return 2;
	case 8:
		return 9;
	default:
		return 1;
	}
}

static struct ofono_call *create_call(struct ofono_voicecall *vc, int type,
					int direction, int status,
					const char *num, int num_type, int clip)
{
	struct voicecall_data *d = ofono_voicecall_get_data(vc);
	struct ofono_call *call;

	/* Generate a call structure for the waiting call */
	call = g_try_new0(struct ofono_call, 1);

	if (!call)
		return NULL;

	call->id = ofono_voicecall_get_next_callid(vc);
	call->type = type;
	call->direction = direction;
	call->status = status;

	if (clip != 2) {
		strncpy(call->phone_number.number, num,
			OFONO_MAX_PHONE_NUMBER_LENGTH);
		call->phone_number.type = num_type;
	}

	call->clip_validity = clip;

	d->calls = g_slist_insert_sorted(d->calls, call, at_util_call_compare);

	return call;
}

static void clcc_poll_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct ofono_voicecall *vc = user_data;
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);
	GSList *calls;
	GSList *n, *o;
	struct ofono_call *nc, *oc;
	gboolean poll_again = FALSE;

	if (!ok) {
		ofono_error("We are polling CLCC and received an error");
		ofono_error("All bets are off for call management");
		return;
	}

	calls = at_util_parse_clcc(result);

	n = calls;
	o = vd->calls;

	while (n || o) {
		nc = n ? n->data : NULL;
		oc = o ? o->data : NULL;

		if (nc && nc->status >= 2 && nc->status <= 5)
			poll_again = TRUE;

		if (oc && (!nc || (nc->id > oc->id))) {
			enum ofono_disconnect_reason reason;

			if (vd->local_release & (0x1 << oc->id))
				reason = OFONO_DISCONNECT_REASON_LOCAL_HANGUP;
			else
				reason = OFONO_DISCONNECT_REASON_REMOTE_HANGUP;

			if (!oc->type)
				ofono_voicecall_disconnected(vc, oc->id,
								reason, NULL);

			o = o->next;
		} else if (nc && (!oc || (nc->id < oc->id))) {
			/* new call, signal it */
			if (nc->type == 0)
				ofono_voicecall_notify(vc, nc);

			n = n->next;
		} else {
			/* Always use the clip_validity from old call
			 * the only place this is truly told to us is
			 * in the CLIP notify, the rest are fudged
			 * anyway.  Useful when RING, CLIP is used,
			 * and we're forced to use CLCC and clip_validity
			 * is 1
			 */
			nc->clip_validity = oc->clip_validity;

			if (memcmp(nc, oc, sizeof(struct ofono_call)) &&
					!nc->type)
				ofono_voicecall_notify(vc, nc);

			n = n->next;
			o = o->next;
		}
	}

	g_slist_foreach(vd->calls, (GFunc) g_free, NULL);
	g_slist_free(vd->calls);

	vd->calls = calls;

	vd->local_release = 0;

	if (poll_again && !vd->clcc_source)
		vd->clcc_source = g_timeout_add(POLL_CLCC_INTERVAL,
						poll_clcc, vc);
}

static gboolean poll_clcc(gpointer user_data)
{
	struct ofono_voicecall *vc = user_data;
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);

	g_at_chat_send(vd->chat, "AT+CLCC", clcc_prefix,
				clcc_poll_cb, vc, NULL);

	vd->clcc_source = 0;

	return FALSE;
}

static void generic_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct change_state_req *req = user_data;
	struct voicecall_data *vd = ofono_voicecall_get_data(req->vc);
	struct ofono_error error;

	decode_at_error(&error, g_at_result_final_response(result));

	if (ok && req->affected_types) {
		GSList *l;
		struct ofono_call *call;

		for (l = vd->calls; l; l = l->next) {
			call = l->data;

			if (req->affected_types & (0x1 << call->status))
				vd->local_release |= (0x1 << call->id);
		}
	}

	g_at_chat_send(vd->chat, "AT+CLCC", clcc_prefix,
			clcc_poll_cb, req->vc, NULL);

	/* We have to callback after we schedule a poll if required */
	req->cb(&error, req->data);
}

static void release_id_cb(gboolean ok, GAtResult *result,
				gpointer user_data)
{
	struct release_id_req *req = user_data;
	struct voicecall_data *vd = ofono_voicecall_get_data(req->vc);
	struct ofono_error error;

	decode_at_error(&error, g_at_result_final_response(result));

	if (ok)
		vd->local_release = 0x1 << req->id;

	g_at_chat_send(vd->chat, "AT+CLCC", clcc_prefix,
			clcc_poll_cb, req->vc, NULL);

	/* We have to callback after we schedule a poll if required */
	req->cb(&error, req->data);
}

static void atd_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	struct ofono_voicecall *vc = cbd->user;
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);
	ofono_voicecall_cb_t cb = cbd->cb;
	GAtResultIter iter;
	const char *num;
	int type = 128;
	int validity = 2;
	struct ofono_error error;
	struct ofono_call *call;
	GSList *l;

	decode_at_error(&error, g_at_result_final_response(result));

	if (!ok)
		goto out;

	/* On a success, make sure to put all active calls on hold */
	for (l = vd->calls; l; l = l->next) {
		call = l->data;

		if (call->status != 0)
			continue;

		call->status = 1;
		ofono_voicecall_notify(vc, call);
	}

	g_at_result_iter_init(&iter, result);

	if (g_at_result_iter_next(&iter, "+COLP:")) {
		g_at_result_iter_next_string(&iter, &num);
		g_at_result_iter_next_number(&iter, &type);

		if (strlen(num) > 0)
			validity = 0;
		else
			validity = 2;

		DBG("colp_notify: %s %d %d", num, type, validity);
	}

	/* Generate a voice call that was just dialed, we guess the ID */
	call = create_call(vc, 0, 0, 2, num, type, validity);

	if (!call) {
		ofono_error("Unable to malloc, call tracking will fail!");
		return;
	}

	/* Telephonyd will generate a call with the dialed number
	 * inside its dial callback.  Unless we got COLP information
	 * we do not need to communicate that a call is being
	 * dialed
	 */
	if (validity != 2)
		ofono_voicecall_notify(vc, call);

	if (!vd->clcc_source)
		vd->clcc_source = g_timeout_add(POLL_CLCC_INTERVAL,
						poll_clcc, vc);

out:
	cb(&error, cbd->data);
}

static void at_dial(struct ofono_voicecall *vc,
			const struct ofono_phone_number *ph,
			enum ofono_clir_option clir, enum ofono_cug_option cug,
			ofono_voicecall_cb_t cb, void *data)
{
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);
	struct cb_data *cbd = cb_data_new(cb, data);
	char buf[256];

	if (!cbd)
		goto error;

	cbd->user = vc;

	if (ph->type == 145)
		snprintf(buf, sizeof(buf), "ATD+%s", ph->number);
	else
		snprintf(buf, sizeof(buf), "ATD%s", ph->number);

	switch (clir) {
	case OFONO_CLIR_OPTION_INVOCATION:
		strcat(buf, "I");
		break;
	case OFONO_CLIR_OPTION_SUPPRESSION:
		strcat(buf, "i");
		break;
	default:
		break;
	}

	switch (cug) {
	case OFONO_CUG_OPTION_INVOCATION:
		strcat(buf, "G");
		break;
	default:
		break;
	}

	strcat(buf, ";");

	if (g_at_chat_send(vd->chat, buf, atd_prefix,
				atd_cb, cbd, g_free) > 0)
		return;

error:
	if (cbd)
		g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, data);
}

static void at_template(const char *cmd, struct ofono_voicecall *vc,
			GAtResultFunc result_cb, unsigned int affected_types,
			ofono_voicecall_cb_t cb, void *data)
{
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);
	struct change_state_req *req = g_try_new0(struct change_state_req, 1);

	if (!req)
		goto error;

	req->vc = vc;
	req->cb = cb;
	req->data = data;
	req->affected_types = affected_types;

	if (g_at_chat_send(vd->chat, cmd, none_prefix,
				result_cb, req, g_free) > 0)
		return;

error:
	if (req)
		g_free(req);

	CALLBACK_WITH_FAILURE(cb, data);
}

static void at_answer(struct ofono_voicecall *vc,
			ofono_voicecall_cb_t cb, void *data)
{
	at_template("ATA", vc, generic_cb, 0, cb, data);
}

static void at_hangup(struct ofono_voicecall *vc,
			ofono_voicecall_cb_t cb, void *data)
{
	/* Hangup all calls */
	at_template("AT+CHUP", vc, generic_cb, 0x3f, cb, data);
}

static void clcc_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct ofono_voicecall *vc = user_data;
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);
	GSList *l;

	if (!ok)
		return;

	vd->calls = at_util_parse_clcc(result);

	for (l = vd->calls; l; l = l->next)
		ofono_voicecall_notify(vc, l->data);
}

static void at_hold_all_active(struct ofono_voicecall *vc,
				ofono_voicecall_cb_t cb, void *data)
{
	at_template("AT+CHLD=2", vc, generic_cb, 0, cb, data);
}

static void at_release_all_held(struct ofono_voicecall *vc,
				ofono_voicecall_cb_t cb, void *data)
{
	unsigned int held_status = 0x1 << 1;
	at_template("AT+CHLD=0", vc, generic_cb, held_status, cb, data);
}

static void at_set_udub(struct ofono_voicecall *vc,
			ofono_voicecall_cb_t cb, void *data)
{
	unsigned int incoming_or_waiting = (0x1 << 4) | (0x1 << 5);
	at_template("AT+CHLD=0", vc, generic_cb, incoming_or_waiting,
			cb, data);
}

static void at_release_all_active(struct ofono_voicecall *vc,
					ofono_voicecall_cb_t cb, void *data)
{
	at_template("AT+CHLD=1", vc, generic_cb, 0x1, cb, data);
}

static void at_release_specific(struct ofono_voicecall *vc, int id,
				ofono_voicecall_cb_t cb, void *data)
{
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);
	struct release_id_req *req = g_try_new0(struct release_id_req, 1);
	char buf[32];

	if (!req)
		goto error;

	req->vc = vc;
	req->cb = cb;
	req->data = data;
	req->id = id;

	snprintf(buf, sizeof(buf), "AT+CHLD=1%d", id);

	if (g_at_chat_send(vd->chat, buf, none_prefix,
				release_id_cb, req, g_free) > 0)
		return;

error:
	if (req)
		g_free(req);

	CALLBACK_WITH_FAILURE(cb, data);
}

static void at_private_chat(struct ofono_voicecall *vc, int id,
				ofono_voicecall_cb_t cb, void *data)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "AT+CHLD=2%d", id);
	at_template(buf, vc, generic_cb, 0, cb, data);
}

static void at_create_multiparty(struct ofono_voicecall *vc,
					ofono_voicecall_cb_t cb, void *data)
{
	at_template("AT+CHLD=3", vc, generic_cb, 0, cb, data);
}

static void at_transfer(struct ofono_voicecall *vc,
			ofono_voicecall_cb_t cb, void *data)
{
	/* Held & Active */
	unsigned int transfer = 0x1 | 0x2;

	/* Transfer can puts held & active calls together and disconnects
	 * from both.  However, some networks support transfering of
	 * dialing/ringing calls as well.
	 */
	transfer |= 0x4 | 0x8;

	at_template("AT+CHLD=4", vc, generic_cb, transfer, cb, data);
}

static void at_deflect(struct ofono_voicecall *vc,
			const struct ofono_phone_number *ph,
			ofono_voicecall_cb_t cb, void *data)
{
	char buf[128];
	unsigned int incoming_or_waiting = (0x1 << 4) | (0x1 << 5);

	snprintf(buf, sizeof(buf), "AT+CTFR=%s,%d", ph->number, ph->type);
	at_template(buf, vc, generic_cb, incoming_or_waiting, cb, data);
}

static void vts_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_voicecall_cb_t cb = cbd->cb;
	struct ofono_error error;

	decode_at_error(&error, g_at_result_final_response(result));
	cb(&error, cbd->data);
}

static void at_send_dtmf(struct ofono_voicecall *vc, const char *dtmf,
			ofono_voicecall_cb_t cb, void *data)
{
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);
	struct cb_data *cbd = cb_data_new(cb, data);
	int len = strlen(dtmf);
	int s;
	int i;
	char *buf;

	if (!cbd)
		goto error;

	/* strlen("+VTS=\"T\";") = 9 + initial AT + null */
	buf = g_try_new(char, len * 9 + 3);

	if (!buf)
		goto error;

	s = sprintf(buf, "AT+VTS=\"%c\"", dtmf[0]);

	for (i = 1; i < len; i++)
		s += sprintf(buf + s, ";+VTS=\"%c\"", dtmf[i]);

	s = g_at_chat_send(vd->chat, buf, none_prefix,
				vts_cb, cbd, g_free);

	g_free(buf);

	if (s > 0)
		return;

error:
	if (cbd)
		g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, data);
}

static void ring_notify(GAtResult *result, gpointer user_data)
{
	struct ofono_voicecall *vc = user_data;
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);
	struct ofono_call *call;

	/* See comment in CRING */
	if (g_slist_find_custom(vd->calls, GINT_TO_POINTER(5),
				at_util_call_compare_by_status))
		return;

	/* RING can repeat, ignore if we already have an incoming call */
	if (g_slist_find_custom(vd->calls, GINT_TO_POINTER(4),
				at_util_call_compare_by_status))
		return;

	/* Generate an incoming call of unknown type */
	call = create_call(vc, 9, 1, 4, NULL, 128, 2);

	if (!call) {
		ofono_error("Couldn't create call, call management is fubar!");
		return;
	}

	/* We don't know the call type, we must run clcc */
	vd->clcc_source = g_timeout_add(CLIP_INTERVAL, poll_clcc, vc);
}

static void cring_notify(GAtResult *result, gpointer user_data)
{
	struct ofono_voicecall *vc = user_data;
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);
	GAtResultIter iter;
	const char *line;
	int type;

	/* Handle the following situation:
	 * Active Call + Waiting Call.  Active Call is Released.  The Waiting
	 * call becomes Incoming and RING/CRING indications are signaled.
	 * Sometimes these arrive before we managed to poll CLCC to find about
	 * the stage change.  If this happens, simply ignore the RING/CRING
	 * when a waiting call exists (cannot have waiting + incoming in GSM)
	 */
	if (g_slist_find_custom(vd->calls, GINT_TO_POINTER(5),
				at_util_call_compare_by_status))
		return;

	/* CRING can repeat, ignore if we already have an incoming call */
	if (g_slist_find_custom(vd->calls, GINT_TO_POINTER(4),
				at_util_call_compare_by_status))
		return;

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+CRING:"))
		return;

	line = g_at_result_iter_raw_line(&iter);

	if (line == NULL)
		return;

	/* Ignore everything that is not voice for now */
	if (!strcasecmp(line, "VOICE"))
		type = 0;
	else
		type = 9;

	/* Generate an incoming call */
	create_call(vc, type, 1, 4, NULL, 128, 2);

	/* We have a call, and call type but don't know the number and
	 * must wait for the CLIP to arrive before announcing the call.
	 * So we wait, and schedule the clcc call.  If the CLIP arrives
	 * earlier, we announce the call there
	 */
	vd->clcc_source = g_timeout_add(CLIP_INTERVAL, poll_clcc, vc);

	DBG("cring_notify");
}

static void clip_notify(GAtResult *result, gpointer user_data)
{
	struct ofono_voicecall *vc = user_data;
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);
	GAtResultIter iter;
	const char *num;
	int type, validity;
	GSList *l;
	struct ofono_call *call;

	l = g_slist_find_custom(vd->calls, GINT_TO_POINTER(4),
				at_util_call_compare_by_status);

	if (l == NULL) {
		ofono_error("CLIP for unknown call");
		return;
	}

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+CLIP:"))
		return;

	if (!g_at_result_iter_next_string(&iter, &num))
		return;

	if (!g_at_result_iter_next_number(&iter, &type))
		return;

	if (strlen(num) > 0)
		validity = 0;
	else
		validity = 2;

	/* Skip subaddr, satype and alpha */
	g_at_result_iter_skip_next(&iter);
	g_at_result_iter_skip_next(&iter);
	g_at_result_iter_skip_next(&iter);

	/* If we have CLI validity field, override our guessed value */
	g_at_result_iter_next_number(&iter, &validity);

	DBG("clip_notify: %s %d %d", num, type, validity);

	call = l->data;

	strncpy(call->phone_number.number, num,
		OFONO_MAX_PHONE_NUMBER_LENGTH);
	call->phone_number.number[OFONO_MAX_PHONE_NUMBER_LENGTH] = '\0';
	call->phone_number.type = type;
	call->clip_validity = validity;

	if (call->type == 0)
		ofono_voicecall_notify(vc, call);

	/* We started a CLCC, but the CLIP arrived and the call type
	 * is known.  If we don't need to poll, cancel the GSource
	 */
	if (call->type != 9 && vd->clcc_source) {
		g_source_remove(vd->clcc_source);
		vd->clcc_source = 0;
	}
}

static void ccwa_notify(GAtResult *result, gpointer user_data)
{
	struct ofono_voicecall *vc = user_data;
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);
	GAtResultIter iter;
	const char *num;
	int num_type, validity, cls;
	struct ofono_call *call;

	/* Some modems resend CCWA, ignore it the second time around */
	if (g_slist_find_custom(vd->calls, GINT_TO_POINTER(5),
				at_util_call_compare_by_status))
		return;

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+CCWA:"))
		return;

	if (!g_at_result_iter_next_string(&iter, &num))
		return;

	if (!g_at_result_iter_next_number(&iter, &num_type))
		return;

	if (!g_at_result_iter_next_number(&iter, &cls))
		return;

	/* Skip alpha field */
	g_at_result_iter_skip_next(&iter);

	if (strlen(num) > 0)
		validity = 0;
	else
		validity = 2;

	/* If we have CLI validity field, override our guessed value */
	g_at_result_iter_next_number(&iter, &validity);

	DBG("ccwa_notify: %s %d %d %d", num, num_type, cls, validity);

	call = create_call(vc, class_to_call_type(cls), 1, 5,
				num, num_type, validity);

	if (!call) {
		ofono_error("Unable to malloc. Call management is fubar");
		return;
	}

	if (call->type == 0) /* Only notify voice calls */
		ofono_voicecall_notify(vc, call);

	if (vd->clcc_source == 0)
		vd->clcc_source = g_timeout_add(POLL_CLCC_INTERVAL,
						poll_clcc, vc);
}

static void no_carrier_notify(GAtResult *result, gpointer user_data)
{
	struct ofono_voicecall *vc = user_data;
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);

	g_at_chat_send(vd->chat, "AT+CLCC", clcc_prefix,
			clcc_poll_cb, vc, NULL);
}

static void no_answer_notify(GAtResult *result, gpointer user_data)
{
	struct ofono_voicecall *vc = user_data;
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);

	g_at_chat_send(vd->chat, "AT+CLCC", clcc_prefix,
			clcc_poll_cb, vc, NULL);
}

static void busy_notify(GAtResult *result, gpointer user_data)
{
	struct ofono_voicecall *vc = user_data;
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);

	/* Call was rejected, most likely due to network congestion
	 * or UDUB on the other side
	 * TODO: Handle UDUB or other conditions somehow
	 */
	g_at_chat_send(vd->chat, "AT+CLCC", clcc_prefix,
			clcc_poll_cb, vc, NULL);
}

static void at_voicecall_initialized(gboolean ok, GAtResult *result,
					gpointer user_data)
{
	struct ofono_voicecall *vc = user_data;
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);

	DBG("voicecall_init: registering to notifications");

	g_at_chat_register(vd->chat, "RING", ring_notify, FALSE, vc, NULL);
	g_at_chat_register(vd->chat, "+CRING:", cring_notify, FALSE, vc, NULL);
	g_at_chat_register(vd->chat, "+CLIP:", clip_notify, FALSE, vc, NULL);
	g_at_chat_register(vd->chat, "+CCWA:", ccwa_notify, FALSE, vc, NULL);

	/* Modems with 'better' call progress indicators should
	 * probably not even bother registering to these
	 */
	g_at_chat_register(vd->chat, "NO CARRIER",
				no_carrier_notify, FALSE, vc, NULL);
	g_at_chat_register(vd->chat, "NO ANSWER",
				no_answer_notify, FALSE, vc, NULL);
	g_at_chat_register(vd->chat, "BUSY", busy_notify, FALSE, vc, NULL);

	ofono_voicecall_register(vc);

	/* Populate the call list */
	g_at_chat_send(vd->chat, "AT+CLCC", clcc_prefix, clcc_cb, vc, NULL);
}

static int at_voicecall_probe(struct ofono_voicecall *vc, unsigned int vendor,
				void *data)
{
	GAtChat *chat = data;
	struct voicecall_data *vd;

	vd = g_new0(struct voicecall_data, 1);
	vd->chat = chat;

	ofono_voicecall_set_data(vc, vd);

	g_at_chat_send(chat, "AT+CRC=1", NULL, NULL, NULL, NULL);
	g_at_chat_send(chat, "AT+CLIP=1", NULL, NULL, NULL, NULL);
	g_at_chat_send(chat, "AT+COLP=1", NULL, NULL, NULL, NULL);
	g_at_chat_send(chat, "AT+CCWA=1", NULL,
				at_voicecall_initialized, vc, NULL);
	return 0;
}

static void at_voicecall_remove(struct ofono_voicecall *vc)
{
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);

	if (vd->clcc_source)
		g_source_remove(vd->clcc_source);

	g_slist_foreach(vd->calls, (GFunc) g_free, NULL);
	g_slist_free(vd->calls);

	ofono_voicecall_set_data(vc, NULL);

	g_free(vd);
}

static struct ofono_voicecall_driver driver = {
	.name			= "atmodem",
	.probe			= at_voicecall_probe,
	.remove			= at_voicecall_remove,
	.dial			= at_dial,
	.answer			= at_answer,
	.hangup			= at_hangup,
	.hold_all_active	= at_hold_all_active,
	.release_all_held	= at_release_all_held,
	.set_udub		= at_set_udub,
	.release_all_active	= at_release_all_active,
	.release_specific	= at_release_specific,
	.private_chat		= at_private_chat,
	.create_multiparty	= at_create_multiparty,
	.transfer		= at_transfer,
	.deflect		= at_deflect,
	.swap_without_accept	= NULL,
	.send_tones		= at_send_dtmf
};

void at_voicecall_init()
{
	ofono_voicecall_driver_register(&driver);
}

void at_voicecall_exit()
{
	ofono_voicecall_driver_unregister(&driver);
}
