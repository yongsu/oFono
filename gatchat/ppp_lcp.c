/*
 *
 *  PPP library with GLib integration
 *
 *  Copyright (C) 2009-2010  Intel Corporation. All rights reserved.
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

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <glib.h>
#include <arpa/inet.h>

#include "gatppp.h"
#include "ppp.h"

#define LCP_SUPPORTED_CODES	((1 << PPPCP_CODE_TYPE_CONFIGURE_REQUEST) | \
				(1 << PPPCP_CODE_TYPE_CONFIGURE_ACK) | \
				(1 << PPPCP_CODE_TYPE_CONFIGURE_NAK) | \
				(1 << PPPCP_CODE_TYPE_CONFIGURE_REJECT) | \
				(1 << PPPCP_CODE_TYPE_TERMINATE_REQUEST) | \
				(1 << PPPCP_CODE_TYPE_TERMINATE_ACK) | \
				(1 << PPPCP_CODE_TYPE_CODE_REJECT) | \
				(1 << PPPCP_CODE_TYPE_PROTOCOL_REJECT) | \
				(1 << PPPCP_CODE_TYPE_ECHO_REQUEST) | \
				(1 << PPPCP_CODE_TYPE_ECHO_REPLY) | \
				(1 << PPPCP_CODE_TYPE_DISCARD_REQUEST))

enum lcp_options {
	RESERVED 		= 0,
	MRU			= 1,
	ACCM			= 2,
	AUTH_PROTO		= 3,
	QUAL_PROTO		= 4,
	MAGIC_NUMBER		= 5,
	DEPRECATED_QUAL_PROTO	= 6,
	PFC			= 7,
	ACFC			= 8,
};

/* Maximum size of all options, we only ever request ACCM and MRU */ 
#define MAX_CONFIG_OPTION_SIZE 10

#define REQ_OPTION_ACCM	0x1
#define REQ_OPTION_MRU	0x2

struct lcp_data {
	guint8 options[MAX_CONFIG_OPTION_SIZE];
	guint16 options_len;
	guint8 req_options;
	guint32 accm;			/* ACCM value */
	guint16 mru;
};

static void lcp_generate_config_options(struct lcp_data *lcp)
{
	guint16 len = 0;

	if (lcp->req_options & REQ_OPTION_ACCM) {
		guint32 accm;

		accm = htonl(lcp->accm);

		lcp->options[len] = ACCM;
		lcp->options[len + 1] = 6;
		memcpy(lcp->options + len + 2, &accm, sizeof(accm));

		len += 6;
	}

	if (lcp->req_options & REQ_OPTION_MRU) {
		guint16 mru;

		mru = htons(lcp->mru);

		lcp->options[len] = MRU;
		lcp->options[len + 1] = 4;
		memcpy(lcp->options + len + 2, &mru, sizeof(mru));

		len += 4;
	}

	lcp->options_len = len;
}

static void lcp_reset_config_options(struct lcp_data *lcp)
{
	lcp->req_options = REQ_OPTION_ACCM;
	lcp->accm = 0;

	lcp_generate_config_options(lcp);
}

/*
 * signal the Up event to the NCP
 */
static void lcp_up(struct pppcp_data *pppcp)
{
	ppp_lcp_up_notify(pppcp_get_ppp(pppcp));
}

/*
 * signal the Down event to the NCP
 */
static void lcp_down(struct pppcp_data *pppcp)
{
	struct lcp_data *lcp = pppcp_get_data(pppcp);

	lcp_reset_config_options(lcp);
	pppcp_set_local_options(pppcp, lcp->options, lcp->options_len);
	ppp_lcp_down_notify(pppcp_get_ppp(pppcp));
}

/*
 * Indicate that the lower layer is not needed
 * Should trigger Down event
 */
static void lcp_finished(struct pppcp_data *pppcp)
{
	ppp_lcp_finished_notify(pppcp_get_ppp(pppcp));
}

static void lcp_rca(struct pppcp_data *pppcp, const struct pppcp_packet *packet)
{
	struct ppp_option_iter iter;

	ppp_option_iter_init(&iter, packet);

	while (ppp_option_iter_next(&iter) == TRUE) {
		switch (ppp_option_iter_get_type(&iter)) {
		case ACCM:
			ppp_set_xmit_accm(pppcp_get_ppp(pppcp), 0);
			break;
		default:
			break;
		}
	}
}

static void lcp_rcn_nak(struct pppcp_data *pppcp,
				const struct pppcp_packet *packet)
{
	struct lcp_data *lcp = pppcp_get_data(pppcp);
	struct ppp_option_iter iter;

	ppp_option_iter_init(&iter, packet);

	while (ppp_option_iter_next(&iter) == TRUE) {
		const guint8 *data = ppp_option_iter_get_data(&iter);

		switch (ppp_option_iter_get_type(&iter)) {
		case MRU:
		{
			guint16 mru = get_host_short(data);

			if (mru < 2048) {
				g_print("Setting peer's suggested mru: %hd\n",
						mru);
				lcp->mru = get_host_short(data);
				lcp->req_options |= REQ_OPTION_MRU;
			}

			break;
		}
		default:
			break;
		}
	}

	lcp_generate_config_options(lcp);
	pppcp_set_local_options(pppcp, lcp->options, lcp->options_len);
}

static void lcp_rcn_rej(struct pppcp_data *pppcp,
				const struct pppcp_packet *packet)
{

}

static enum rcr_result lcp_rcr(struct pppcp_data *pppcp,
					const struct pppcp_packet *packet,
					guint8 **new_options, guint16 *new_len)
{
	GAtPPP *ppp = pppcp_get_ppp(pppcp);
	struct ppp_option_iter iter;

	ppp_option_iter_init(&iter, packet);

	while (ppp_option_iter_next(&iter) == TRUE) {
		switch (ppp_option_iter_get_type(&iter)) {
		case AUTH_PROTO:
		{
			const guint8 *option_data =
				ppp_option_iter_get_data(&iter);
			guint16 proto = get_host_short(option_data);
			guint8 method = option_data[2];
			guint8 *option;

			if ((proto == CHAP_PROTOCOL) && (method == MD5))
				break;

			/*
			 * try to suggest CHAP & MD5.  If we are out
			 * of memory, just reject.
			 */

			option = g_try_malloc0(5);
			if (!option)
				return RCR_REJECT;

			option[0] = AUTH_PROTO;
			option[1] = 5;
			put_network_short(&option[2], CHAP_PROTOCOL);
			option[4] = MD5;
			*new_options = option;
			*new_len = 5;
			return RCR_NAK;
		}
		case ACCM:
		case PFC:
		case ACFC:
		case MRU:
			break;

		case MAGIC_NUMBER:
		{
			guint32 magic =
				get_host_long(ppp_option_iter_get_data(&iter));

			if (magic == 0)
				return RCR_REJECT;

			break;
		}
		default:
			return RCR_REJECT;
		}
	}

	/* All options were found acceptable, apply them here and return */
	ppp_option_iter_init(&iter, packet);

	while (ppp_option_iter_next(&iter) == TRUE) {
		switch (ppp_option_iter_get_type(&iter)) {
		case ACCM:
			ppp_set_recv_accm(ppp,
				get_host_long(ppp_option_iter_get_data(&iter)));
			break;
		case AUTH_PROTO:
			ppp_set_auth(ppp, ppp_option_iter_get_data(&iter));
			break;
		case MRU:
			ppp_set_mtu(ppp, ppp_option_iter_get_data(&iter));
			break;
		case MAGIC_NUMBER:
		case PFC:
		case ACFC:
			/* don't care */
			break;
		}
	}

	return RCR_ACCEPT;
}

struct pppcp_proto lcp_proto = {
	.proto			= LCP_PROTOCOL,
	.name			= "lcp",
	.supported_codes	= LCP_SUPPORTED_CODES,
	.this_layer_up		= lcp_up,
	.this_layer_down	= lcp_down,
	.this_layer_finished	= lcp_finished,
	.rca			= lcp_rca,
	.rcn_nak		= lcp_rcn_nak,
	.rcn_rej		= lcp_rcn_rej,
	.rcr			= lcp_rcr,
};

void lcp_free(struct pppcp_data *pppcp)
{
	struct lcp_data *lcp = pppcp_get_data(pppcp);

	g_free(lcp);
	pppcp_free(pppcp);
}

struct pppcp_data *lcp_new(GAtPPP *ppp, gboolean is_server)
{
	struct pppcp_data *pppcp;
	struct lcp_data *lcp;

	lcp = g_try_new0(struct lcp_data, 1);
	if (!lcp)
		return NULL;

	pppcp = pppcp_new(ppp, &lcp_proto, is_server, 0);
	if (!pppcp) {
		g_free(lcp);
		return NULL;
	}

	pppcp_set_data(pppcp, lcp);

	lcp_reset_config_options(lcp);
	pppcp_set_local_options(pppcp, lcp->options, lcp->options_len);

	return pppcp;
}
