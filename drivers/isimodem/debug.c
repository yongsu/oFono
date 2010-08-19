/*
 * This file is part of oFono - Open Source Telephony
 *
 * Copyright (C) 2009-2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <glib.h>

#define OFONO_API_SUBJECT_TO_CHANGE
#include <ofono/log.h>

#include "debug.h"

#define _(X) case X: return #X

const char *pn_resource_name(int value)
{
	switch (value) {
		_(PN_NETWORK);
		_(PN_PHONE_INFO);
		_(PN_SS);
		_(PN_CALL);
		_(PN_SMS);
		_(PN_SIM);
		_(PN_MTC);
		_(PN_GSS);
		_(PN_GPDS);
	}
	return "PN_<UNKNOWN>";
}

const char *ss_message_id_name(enum ss_message_id value)
{
	switch (value) {
		_(SS_SERVICE_REQ);
		_(SS_SERVICE_COMPLETED_RESP);
		_(SS_SERVICE_FAILED_RESP);
		_(SS_GSM_USSD_SEND_REQ);
		_(SS_GSM_USSD_SEND_RESP);
		_(SS_GSM_USSD_RECEIVE_IND);
		_(SS_STATUS_IND);
		_(SS_COMMON_MESSAGE);
	}
	return "SS_<UNKNOWN>";
}

const char *ss_subblock_name(enum ss_subblock value)
{
	switch (value) {
		_(SS_FORWARDING);
		_(SS_STATUS_RESULT);
		_(SS_GSM_PASSWORD);
		_(SS_GSM_FORWARDING_INFO);
		_(SS_GSM_FORWARDING_FEATURE);
		_(SS_GSM_DATA);
		_(SS_GSM_BSC_INFO);
		_(SS_GSM_PASSWORD_INFO);
		_(SS_GSM_INDICATE_PASSWORD_ERROR);
		_(SS_GSM_INDICATE_ERROR);
		_(SS_GSM_ADDITIONAL_INFO);
		_(SS_GSM_USSD_STRING);
	}
	return "SS_<UNKNOWN>";
}

const char *mtc_isi_cause_name(enum mtc_isi_cause value)
{
	switch (value) {
		_(MTC_OK);
		_(MTC_FAIL);
		_(MTC_NOT_ALLOWED);
		_(MTC_STATE_TRANSITION_GOING_ON);
		_(MTC_ALREADY_ACTIVE);
		_(MTC_SERVICE_DISABLED);
		_(MTC_NOT_READY_YET);
		_(MTC_NOT_SUPPORTED);
		_(MTC_TRANSITION_ONGOING);
		_(MTC_RESET_REQUIRED);
	}
	return "MTC_<UNKNOWN>";
}

const char *mtc_message_id_name(enum mtc_message_id value)
{
	switch (value) {
		_(MTC_STATE_REQ);
		_(MTC_STATE_QUERY_REQ);
		_(MTC_POWER_OFF_REQ);
		_(MTC_POWER_ON_REQ);
		_(MTC_STARTUP_SYNQ_REQ);
		_(MTC_SHUTDOWN_SYNC_REQ);
		_(MTC_STATE_RESP);
		_(MTC_STATE_QUERY_RESP);
		_(MTC_POWER_OFF_RESP);
		_(MTC_POWER_ON_RESP);
		_(MTC_STARTUP_SYNQ_RESP);
		_(MTC_SHUTDOWN_SYNC_RESP);
		_(MTC_STATE_INFO_IND);
		_(MTC_COMMON_MESSAGE);
	}
	return "MTC_<UNKNOWN>";
}

const char *mtc_modem_state_name(enum mtc_modem_state value)
{
	switch (value) {
		_(MTC_POWER_OFF);
		_(MTC_NORMAL);
		_(MTC_CHARGING);
		_(MTC_ALARM);
		_(MTC_TEST);
		_(MTC_LOCAL);
		_(MTC_WARRANTY);
		_(MTC_RELIABILITY);
		_(MTC_SELFTEST_FAIL);
		_(MTC_SWDL);
		_(MTC_RF_INACTIVE);
		_(MTC_ID_WRITE);
		_(MTC_DISCHARGING);
		_(MTC_DISK_WIPE);
		_(MTC_SW_RESET);
		_(MTC_CMT_ONLY_MODE);
		_(MTC_STATE_NONE);
	}
	return "MTC_<UNKNOWN>";
}

const char *sms_isi_cause_name(enum sms_isi_cause value)
{
	switch (value) {
		_(SMS_OK);
		_(SMS_ERR_ROUTING_RELEASED);
		_(SMS_ERR_INVALID_PARAMETER);
		_(SMS_ERR_DEVICE_FAILURE);
		_(SMS_ERR_PP_RESERVED);
		_(SMS_ERR_ROUTE_NOT_AVAILABLE);
		_(SMS_ERR_ROUTE_NOT_ALLOWED);
		_(SMS_ERR_SERVICE_RESERVED);
		_(SMS_ERR_INVALID_LOCATION);
		_(SMS_ERR_NO_SIM);
		_(SMS_ERR_SIM_NOT_READY);
		_(SMS_ERR_NO_NETW_RESPONSE);
		_(SMS_ERR_DEST_ADDR_FDN_RESTRICTED);
		_(SMS_ERR_SMSC_ADDR_FDN_RESTRICTED);
		_(SMS_ERR_RESEND_ALREADY_DONE);
		_(SMS_ERR_SMSC_ADDR_NOT_AVAILABLE);
		_(SMS_ERR_ROUTING_FAILED);
		_(SMS_ERR_CS_INACTIVE);
		_(SMS_ERR_SAT_MO_CONTROL_MODIFIED);
		_(SMS_ERR_SAT_MO_CONTROL_REJECT);
		_(SMS_ERR_TRACFONE_FAILED);
	}
	return "SMS_<UNKNOWN>";
}

const char *sms_gsm_cause_name(enum sms_gsm_cause value)
{
	switch (value) {
		_(SMS_GSM_ERR_UNASSIGNED_NUMBER);
		_(SMS_GSM_ERR_OPER_DETERMINED_BARR);
		_(SMS_GSM_ERR_CALL_BARRED);
		_(SMS_GSM_ERR_RESERVED);
		_(SMS_GSM_ERR_MSG_TRANSFER_REJ);
		_(SMS_GSM_ERR_MEMORY_CAPACITY_EXC);
		_(SMS_GSM_ERR_DEST_OUT_OF_ORDER);
		_(SMS_GSM_ERR_UNDEFINED_SUBSCRIBER);
		_(SMS_GSM_ERR_FACILITY_REJECTED);
		_(SMS_GSM_ERR_UNKNOWN_SUBSCRIBER);
		_(SMS_GSM_ERR_NETW_OUT_OF_ORDER);
		_(SMS_GSM_ERR_TEMPORARY_FAILURE);
		_(SMS_GSM_ERR_CONGESTION);
		_(SMS_GSM_ERR_RESOURCE_UNAVAILABLE);
		_(SMS_GSM_ERR_REQ_FACILITY_NOT_SUB);
		_(SMS_GSM_ERR_REQ_FACILITY_NOT_IMP);
		_(SMS_GSM_ERR_INVALID_REFERENCE);
		_(SMS_GSM_ERR_INCORRECT_MESSAGE);
		_(SMS_GSM_ERR_INVALID_MAND_INFO);
		_(SMS_GSM_ERR_INVALID_MSG_TYPE);
		_(SMS_GSM_ERR_MSG_NOT_COMP_WITH_ST);
		_(SMS_GSM_ERR_INVALID_INFO_ELEMENT);
		_(SMS_GSM_ERR_PROTOCOL_ERROR);
		_(SMS_GSM_ERR_INTERWORKING);
		_(SMS_GSM_ERR_NO_CAUSE);
		_(SMS_GSM_ERR_IMSI_UNKNOWN_HLR);
		_(SMS_GSM_ERR_ILLEGAL_MS);
		_(SMS_GSM_ERR_IMSI_UNKNOWN_VLR);
		_(SMS_GSM_ERR_IMEI_NOT_ACCEPTED);
		_(SMS_GSM_ERR_ILLEGAL_ME);
		_(SMS_GSM_ERR_PLMN_NOT_ALLOWED);
		_(SMS_GSM_ERR_LA_NOT_ALLOWED);
		_(SMS_GSM_ERR_ROAM_NOT_ALLOWED_LA);
		_(SMS_GSM_ERR_NO_SUITABLE_CELLS_LA);
		_(SMS_GSM_ERR_NETWORK_FAILURE);
		_(SMS_GSM_ERR_MAC_FAILURE);
		_(SMS_GSM_ERR_SYNC_FAILURE);
		_(SMS_GSM_ERR_LOW_LAYER_CONGESTION);
		_(SMS_GSM_ERR_AUTH_UNACCEPTABLE);
		_(SMS_GSM_ERR_SERV_OPT_NOT_SUPPORTED);
		_(SMS_GSM_ERR_SERV_OPT_NOT_SUBSCRIBED);
		_(SMS_GSM_ERR_SERV_OPT_TEMP_OUT_OF_ORDER);
		_(SMS_GSM_ERR_CALL_CANNOT_BE_IDENTIFIED);
		_(SMS_GSM_ERR_SEMANTICALLY_INCORR_MSG);
		_(SMS_GSM_ERR_LOW_LAYER_INVALID_MAND_INFO);
		_(SMS_GSM_ERR_LOW_LAYER_INVALID_MSG_TYPE);
		_(SMS_GSM_ERR_LOW_LAYER_MSG_TYPE_NOT_COMP_WITH_ST);
		_(SMS_GSM_ERR_LOW_LAYER_INVALID_INFO_ELEMENT);
		_(SMS_GSM_ERR_CONDITIONAL_IE_ERROR);
		_(SMS_GSM_ERR_LOW_LAYER_MSG_NOT_COMP_WITH_ST);
		_(SMS_GSM_ERR_CS_BARRED);
		_(SMS_GSM_ERR_LOW_LAYER_PROTOCOL_ERROR);
	}
	return "SMS_<UNKNOWN>";
}

const char *sms_message_id_name(enum sms_message_id value)
{
	switch (value) {
		_(SMS_MESSAGE_SEND_REQ);
		_(SMS_MESSAGE_SEND_RESP);
		_(SMS_PP_ROUTING_REQ);
		_(SMS_PP_ROUTING_RESP);
		_(SMS_PP_ROUTING_NTF);
		_(SMS_GSM_RECEIVED_PP_REPORT_REQ);
		_(SMS_GSM_RECEIVED_PP_REPORT_RESP);
		_(SMS_GSM_CB_ROUTING_REQ);
		_(SMS_GSM_CB_ROUTING_RESP);
		_(SMS_GSM_CB_ROUTING_NTF);
		_(SMS_MESSAGE_SEND_STATUS_IND);
		_(SMS_COMMON_MESSAGE);
	}
	return "SMS_<UNKNOWN>";
}

const char *sms_subblock_name(enum sms_subblock value)
{
	switch (value) {
		_(SMS_GSM_DELIVER);
		_(SMS_GSM_STATUS_REPORT);
		_(SMS_GSM_SUBMIT);
		_(SMS_GSM_COMMAND);
		_(SMS_GSM_DELIVER_REPORT);
		_(SMS_GSM_REPORT);
		_(SMS_GSM_ROUTING);
		_(SMS_GSM_TPDU);
		_(SMS_COMMON_DATA);
		_(SMS_ADDRESS);
	}
	return "SMS_<UNKNOWN>";
}

const char *sim_isi_cause_name(enum sim_isi_cause value)
{
	switch (value) {
		_(SIM_SERV_NOT_AVAIL);
		_(SIM_SERV_OK);
		_(SIM_SERV_PIN_VERIFY_REQUIRED);
		_(SIM_SERV_PIN_REQUIRED);
		_(SIM_SERV_SIM_BLOCKED);
		_(SIM_SERV_SIM_PERMANENTLY_BLOCKED);
		_(SIM_SERV_SIM_DISCONNECTED);
		_(SIM_SERV_SIM_REJECTED);
		_(SIM_SERV_LOCK_ACTIVE);
		_(SIM_SERV_AUTOLOCK_CLOSED);
		_(SIM_SERV_AUTOLOCK_ERROR);
		_(SIM_SERV_INIT_OK);
		_(SIM_SERV_INIT_NOT_OK);
		_(SIM_SERV_WRONG_OLD_PIN);
		_(SIM_SERV_PIN_DISABLED);
		_(SIM_SERV_COMMUNICATION_ERROR);
		_(SIM_SERV_UPDATE_IMPOSSIBLE);
		_(SIM_SERV_NO_SECRET_CODE_IN_SIM);
		_(SIM_SERV_PIN_ENABLE_OK);
		_(SIM_SERV_PIN_DISABLE_OK);
		_(SIM_SERV_WRONG_UNBLOCKING_KEY);
		_(SIM_SERV_ILLEGAL_NUMBER);
		_(SIM_SERV_NOT_OK);
		_(SIM_SERV_PN_LIST_ENABLE_OK);
		_(SIM_SERV_PN_LIST_DISABLE_OK);
		_(SIM_SERV_NO_PIN);
		_(SIM_SERV_PIN_VERIFY_OK);
		_(SIM_SERV_PIN_BLOCKED);
		_(SIM_SERV_PIN_PERM_BLOCKED);
		_(SIM_SERV_DATA_NOT_AVAIL);
		_(SIM_SERV_IN_HOME_ZONE);
		_(SIM_SERV_STATE_CHANGED);
		_(SIM_SERV_INF_NBR_READ_OK);
		_(SIM_SERV_INF_NBR_READ_NOT_OK);
		_(SIM_SERV_IMSI_EQUAL);
		_(SIM_SERV_IMSI_NOT_EQUAL);
		_(SIM_SERV_INVALID_LOCATION);
		_(SIM_SERV_STA_SIM_REMOVED);
		_(SIM_SERV_SECOND_SIM_REMOVED_CS);
		_(SIM_SERV_CONNECTED_INDICATION_CS);
		_(SIM_SERV_SECOND_SIM_CONNECTED_CS);
		_(SIM_SERV_PIN_RIGHTS_LOST_IND_CS);
		_(SIM_SERV_PIN_RIGHTS_GRANTED_IND_CS);
		_(SIM_SERV_INIT_OK_CS);
		_(SIM_SERV_INIT_NOT_OK_CS);
		_(SIM_FDN_ENABLED);
		_(SIM_FDN_DISABLED);
		_(SIM_SERV_INVALID_FILE);
		_(SIM_SERV_DATA_AVAIL);
		_(SIM_SERV_ICC_EQUAL);
		_(SIM_SERV_ICC_NOT_EQUAL);
		_(SIM_SERV_SIM_NOT_INITIALISED);
		_(SIM_SERV_SERVICE_NOT_AVAIL);
		_(SIM_SERV_FDN_STATUS_ERROR);
		_(SIM_SERV_FDN_CHECK_PASSED);
		_(SIM_SERV_FDN_CHECK_FAILED);
		_(SIM_SERV_FDN_CHECK_DISABLED);
		_(SIM_SERV_FDN_CHECK_NO_FDN_SIM);
		_(SIM_STA_ISIM_AVAILEBLE_PIN_REQUIRED);
		_(SIM_STA_ISIM_AVAILEBLE);
		_(SIM_STA_USIM_AVAILEBLE);
		_(SIM_STA_SIM_AVAILEBLE);
		_(SIM_STA_ISIM_NOT_INITIALIZED);
		_(SIM_STA_IMS_READY);
		_(SIM_STA_APP_DATA_READ_OK);
		_(SIM_STA_APP_ACTIVATE_OK);
		_(SIM_STA_APP_ACTIVATE_NOT_OK);
		_(SIM_SERV_NOT_DEFINED);
		_(SIM_SERV_NOSERVICE);
		_(SIM_SERV_NOTREADY);
		_(SIM_SERV_ERROR);
		_(SIM_SERV_CIPHERING_INDICATOR_DISPLAY_REQUIRED);
		_(SIM_SERV_CIPHERING_INDICATOR_DISPLAY_NOT_REQUIRED);
		_(SIM_SERV_FILE_NOT_AVAILABLE);
	}
	return "SIM_<UNKNOWN>";
}

const char *sim_message_id_name(enum sim_message_id value)
{
	switch (value) {
		_(SIM_NETWORK_INFO_REQ);
		_(SIM_NETWORK_INFO_RESP);
		_(SIM_IMSI_REQ_READ_IMSI);
		_(SIM_IMSI_RESP_READ_IMSI);
		_(SIM_SERV_PROV_NAME_REQ);
		_(SIM_SERV_PROV_NAME_RESP);
		_(SIM_READ_FIELD_REQ);
		_(SIM_READ_FIELD_RESP);
		_(SIM_SMS_REQ);
		_(SIM_SMS_RESP);
		_(SIM_PB_REQ_SIM_PB_READ);
		_(SIM_PB_RESP_SIM_PB_READ);
		_(SIM_IND);
		_(SIM_COMMON_MESSAGE);
	}
	return "SIM_<UNKNOWN>";
}

const char *sim_subblock_name(enum sim_subblock value)
{
	switch (value) {
		_(SIM_PB_INFO_REQUEST);
		_(SIM_PB_STATUS);
		_(SIM_PB_LOCATION);
		_(SIM_PB_LOCATION_SEARCH);
	}
	return "SIM_<UNKNOWN>";
}

const char *info_isi_cause_name(enum info_isi_cause value)
{
	switch (value) {
		_(INFO_OK);
		_(INFO_FAIL);
		_(INFO_NO_NUMBER);
		_(INFO_NOT_SUPPORTED);
	}
	return "INFO_<UNKNOWN>";
}

const char *info_message_id_name(enum info_message_id value)
{
	switch (value) {
		_(INFO_SERIAL_NUMBER_READ_REQ);
		_(INFO_SERIAL_NUMBER_READ_RESP);
		_(INFO_VERSION_READ_REQ);
		_(INFO_VERSION_READ_RESP);
		_(INFO_PRODUCT_INFO_READ_REQ);
		_(INFO_PRODUCT_INFO_READ_RESP);
		_(INFO_COMMON_MESSAGE);
	}
	return "INFO_<UNKNOWN>";
}

const char *info_subblock_name(enum info_subblock value)
{
	switch (value) {
		_(INFO_SB_PRODUCT_INFO_NAME);
		_(INFO_SB_PRODUCT_INFO_MANUFACTURER);
		_(INFO_SB_SN_IMEI_PLAIN);
		_(INFO_SB_SN_IMEI_SV_TO_NET);
		_(INFO_SB_MCUSW_VERSION);
	}
	return "INFO_<UNKNOWN>";
}

const char *call_status_name(enum call_status value)
{
	switch (value) {
		_(CALL_STATUS_IDLE);
		_(CALL_STATUS_CREATE);
		_(CALL_STATUS_COMING);
		_(CALL_STATUS_PROCEEDING);
		_(CALL_STATUS_MO_ALERTING);
		_(CALL_STATUS_MT_ALERTING);
		_(CALL_STATUS_WAITING);
		_(CALL_STATUS_ANSWERED);
		_(CALL_STATUS_ACTIVE);
		_(CALL_STATUS_MO_RELEASE);
		_(CALL_STATUS_MT_RELEASE);
		_(CALL_STATUS_HOLD_INITIATED);
		_(CALL_STATUS_HOLD);
		_(CALL_STATUS_RETRIEVE_INITIATED);
		_(CALL_STATUS_RECONNECT_PENDING);
		_(CALL_STATUS_TERMINATED);
		_(CALL_STATUS_SWAP_INITIATED);
	}
	return "CALL_<UNKNOWN>";
}

char const *call_message_id_name(enum call_message_id value)
{
	switch (value) {
		_(CALL_CREATE_REQ);
		_(CALL_CREATE_RESP);
		_(CALL_COMING_IND);
		_(CALL_MO_ALERT_IND);
		_(CALL_MT_ALERT_IND);
		_(CALL_WAITING_IND);
		_(CALL_ANSWER_REQ);
		_(CALL_ANSWER_RESP);
		_(CALL_RELEASE_REQ);
		_(CALL_RELEASE_RESP);
		_(CALL_RELEASE_IND);
		_(CALL_TERMINATED_IND);
		_(CALL_STATUS_REQ);
		_(CALL_STATUS_RESP);
		_(CALL_STATUS_IND);
		_(CALL_SERVER_STATUS_IND);
		_(CALL_CONTROL_REQ);
		_(CALL_CONTROL_RESP);
		_(CALL_CONTROL_IND);
		_(CALL_MODE_SWITCH_REQ);
		_(CALL_MODE_SWITCH_RESP);
		_(CALL_MODE_SWITCH_IND);
		_(CALL_DTMF_SEND_REQ);
		_(CALL_DTMF_SEND_RESP);
		_(CALL_DTMF_STOP_REQ);
		_(CALL_DTMF_STOP_RESP);
		_(CALL_DTMF_STATUS_IND);
		_(CALL_DTMF_TONE_IND);
		_(CALL_RECONNECT_IND);
		_(CALL_PROPERTY_GET_REQ);
		_(CALL_PROPERTY_GET_RESP);
		_(CALL_PROPERTY_SET_REQ);
		_(CALL_PROPERTY_SET_RESP);
		_(CALL_PROPERTY_SET_IND);
		_(CALL_EMERGENCY_NBR_CHECK_REQ);
		_(CALL_EMERGENCY_NBR_CHECK_RESP);
		_(CALL_EMERGENCY_NBR_GET_REQ);
		_(CALL_EMERGENCY_NBR_GET_RESP);
		_(CALL_EMERGENCY_NBR_MODIFY_REQ);
		_(CALL_EMERGENCY_NBR_MODIFY_RESP);
		_(CALL_GSM_NOTIFICATION_IND);
		_(CALL_GSM_USER_TO_USER_REQ);
		_(CALL_GSM_USER_TO_USER_RESP);
		_(CALL_GSM_USER_TO_USER_IND);
		_(CALL_GSM_BLACKLIST_CLEAR_REQ);
		_(CALL_GSM_BLACKLIST_CLEAR_RESP);
		_(CALL_GSM_BLACKLIST_TIMER_IND);
		_(CALL_GSM_DATA_CH_INFO_IND);
		_(CALL_GSM_CCP_GET_REQ);
		_(CALL_GSM_CCP_GET_RESP);
		_(CALL_GSM_CCP_CHECK_REQ);
		_(CALL_GSM_CCP_CHECK_RESP);
		_(CALL_GSM_COMING_REJ_IND);
		_(CALL_GSM_RAB_IND);
		_(CALL_GSM_IMMEDIATE_MODIFY_IND);
		_(CALL_CREATE_NO_SIMATK_REQ);
		_(CALL_GSM_SS_DATA_IND);
		_(CALL_TIMER_REQ);
		_(CALL_TIMER_RESP);
		_(CALL_TIMER_NTF);
		_(CALL_TIMER_IND);
		_(CALL_TIMER_RESET_REQ);
		_(CALL_TIMER_RESET_RESP);
		_(CALL_EMERGENCY_NBR_IND);
		_(CALL_SERVICE_DENIED_IND);
		_(CALL_RELEASE_END_REQ);
		_(CALL_RELEASE_END_RESP);
		_(CALL_USER_CONNECT_IND);
		_(CALL_AUDIO_CONNECT_IND);
		_(CALL_KODIAK_ALLOW_CTRL_REQ);
		_(CALL_KODIAK_ALLOW_CTRL_RESP);
		_(CALL_SERVICE_ACTIVATE_IND);
		_(CALL_SERVICE_ACTIVATE_REQ);
		_(CALL_SERVICE_ACTIVATE_RESP);
		_(CALL_SIM_ATK_IND);
		_(CALL_CONTROL_OPER_IND);
		_(CALL_TEST_CALL_STATUS_IND);
		_(CALL_SIM_ATK_INFO_IND);
		_(CALL_SECURITY_IND);
		_(CALL_MEDIA_HANDLE_REQ);
		_(CALL_MEDIA_HANDLE_RESP);
		_(CALL_COMMON_MESSAGE);
	}
	return "CALL_<UNKNOWN>";
}

char const *call_isi_cause_name(enum call_isi_cause value)
{
	switch (value) {
		_(CALL_CAUSE_NO_CAUSE);
		_(CALL_CAUSE_NO_CALL);
		_(CALL_CAUSE_TIMEOUT);
		_(CALL_CAUSE_RELEASE_BY_USER);
		_(CALL_CAUSE_BUSY_USER_REQUEST);
		_(CALL_CAUSE_ERROR_REQUEST);
		_(CALL_CAUSE_COST_LIMIT_REACHED);
		_(CALL_CAUSE_CALL_ACTIVE);
		_(CALL_CAUSE_NO_CALL_ACTIVE);
		_(CALL_CAUSE_INVALID_CALL_MODE);
		_(CALL_CAUSE_SIGNALLING_FAILURE);
		_(CALL_CAUSE_TOO_LONG_ADDRESS);
		_(CALL_CAUSE_INVALID_ADDRESS);
		_(CALL_CAUSE_EMERGENCY);
		_(CALL_CAUSE_NO_TRAFFIC_CHANNEL);
		_(CALL_CAUSE_NO_COVERAGE);
		_(CALL_CAUSE_CODE_REQUIRED);
		_(CALL_CAUSE_NOT_ALLOWED);
		_(CALL_CAUSE_NO_DTMF);
		_(CALL_CAUSE_CHANNEL_LOSS);
		_(CALL_CAUSE_FDN_NOT_OK);
		_(CALL_CAUSE_USER_TERMINATED);
		_(CALL_CAUSE_BLACKLIST_BLOCKED);
		_(CALL_CAUSE_BLACKLIST_DELAYED);
		_(CALL_CAUSE_NUMBER_NOT_FOUND);
		_(CALL_CAUSE_NUMBER_CANNOT_REMOVE);
		_(CALL_CAUSE_EMERGENCY_FAILURE);
		_(CALL_CAUSE_CS_SUSPENDED);
		_(CALL_CAUSE_DCM_DRIVE_MODE);
		_(CALL_CAUSE_MULTIMEDIA_NOT_ALLOWED);
		_(CALL_CAUSE_SIM_REJECTED);
		_(CALL_CAUSE_NO_SIM);
		_(CALL_CAUSE_SIM_LOCK_OPERATIVE);
		_(CALL_CAUSE_SIMATKCC_REJECTED);
		_(CALL_CAUSE_SIMATKCC_MODIFIED);
		_(CALL_CAUSE_DTMF_INVALID_DIGIT);
		_(CALL_CAUSE_DTMF_SEND_ONGOING);
		_(CALL_CAUSE_CS_INACTIVE);
		_(CALL_CAUSE_SECURITY_MODE);
		_(CALL_CAUSE_TRACFONE_FAILED);
		_(CALL_CAUSE_TRACFONE_WAIT_FAILED);
		_(CALL_CAUSE_TRACFONE_CONF_FAILED);
		_(CALL_CAUSE_TEMPERATURE_LIMIT);
		_(CALL_CAUSE_KODIAK_POC_FAILED);
		_(CALL_CAUSE_NOT_REGISTERED);
		_(CALL_CAUSE_CS_CALLS_ONLY);
		_(CALL_CAUSE_VOIP_CALLS_ONLY);
		_(CALL_CAUSE_LIMITED_CALL_ACTIVE);
		_(CALL_CAUSE_LIMITED_CALL_NOT_ALLOWED);
		_(CALL_CAUSE_SECURE_CALL_NOT_POSSIBLE);
		_(CALL_CAUSE_INTERCEPT);
	}
	return "CALL_<UNKNOWN>";
}

char const *call_gsm_cause_name(enum call_gsm_cause value)
{
	switch (value) {
		_(CALL_GSM_CAUSE_UNASSIGNED_NUMBER);
		_(CALL_GSM_CAUSE_NO_ROUTE);
		_(CALL_GSM_CAUSE_CH_UNACCEPTABLE);
		_(CALL_GSM_CAUSE_OPER_BARRING);
		_(CALL_GSM_CAUSE_NORMAL);
		_(CALL_GSM_CAUSE_USER_BUSY);
		_(CALL_GSM_CAUSE_NO_USER_RESPONSE);
		_(CALL_GSM_CAUSE_ALERT_NO_ANSWER);
		_(CALL_GSM_CAUSE_CALL_REJECTED);
		_(CALL_GSM_CAUSE_NUMBER_CHANGED);
		_(CALL_GSM_CAUSE_NON_SELECT_CLEAR);
		_(CALL_GSM_CAUSE_DEST_OUT_OF_ORDER);
		_(CALL_GSM_CAUSE_INVALID_NUMBER);
		_(CALL_GSM_CAUSE_FACILITY_REJECTED);
		_(CALL_GSM_CAUSE_RESP_TO_STATUS);
		_(CALL_GSM_CAUSE_NORMAL_UNSPECIFIED);
		_(CALL_GSM_CAUSE_NO_CHANNEL);
		_(CALL_GSM_CAUSE_NETW_OUT_OF_ORDER);
		_(CALL_GSM_CAUSE_TEMPORARY_FAILURE);
		_(CALL_GSM_CAUSE_CONGESTION);
		_(CALL_GSM_CAUSE_ACCESS_INFO_DISC);
		_(CALL_GSM_CAUSE_CHANNEL_NA);
		_(CALL_GSM_CAUSE_RESOURCES_NA);
		_(CALL_GSM_CAUSE_QOS_NA);
		_(CALL_GSM_CAUSE_FACILITY_UNSUBS);
		_(CALL_GSM_CAUSE_COMING_BARRED_CUG);
		_(CALL_GSM_CAUSE_BC_UNAUTHORIZED);
		_(CALL_GSM_CAUSE_BC_NA);
		_(CALL_GSM_CAUSE_SERVICE_NA);
		_(CALL_GSM_CAUSE_BEARER_NOT_IMPL);
		_(CALL_GSM_CAUSE_ACM_MAX);
		_(CALL_GSM_CAUSE_FACILITY_NOT_IMPL);
		_(CALL_GSM_CAUSE_ONLY_RDI_BC);
		_(CALL_GSM_CAUSE_SERVICE_NOT_IMPL);
		_(CALL_GSM_CAUSE_INVALID_TI);
		_(CALL_GSM_CAUSE_NOT_IN_CUG);
		_(CALL_GSM_CAUSE_INCOMPATIBLE_DEST);
		_(CALL_GSM_CAUSE_INV_TRANS_NET_SEL);
		_(CALL_GSM_CAUSE_SEMANTICAL_ERR);
		_(CALL_GSM_CAUSE_INVALID_MANDATORY);
		_(CALL_GSM_CAUSE_MSG_TYPE_INEXIST);
		_(CALL_GSM_CAUSE_MSG_TYPE_INCOMPAT);
		_(CALL_GSM_CAUSE_IE_NON_EXISTENT);
		_(CALL_GSM_CAUSE_COND_IE_ERROR);
		_(CALL_GSM_CAUSE_MSG_INCOMPATIBLE);
		_(CALL_GSM_CAUSE_TIMER_EXPIRY);
		_(CALL_GSM_CAUSE_PROTOCOL_ERROR);
		_(CALL_GSM_CAUSE_INTERWORKING);
	}
	return "CALL_<UNKNOWN>";
}

const char *net_gsm_cause_name(enum net_gsm_cause value)
{
	switch (value) {
		_(NET_GSM_IMSI_UNKNOWN_IN_HLR);
		_(NET_GSM_ILLEGAL_MS);
		_(NET_GSM_IMSI_UNKNOWN_IN_VLR);
		_(NET_GSM_IMEI_NOT_ACCEPTED);
		_(NET_GSM_ILLEGAL_ME);
		_(NET_GSM_GPRS_SERVICES_NOT_ALLOWED);
		_(NET_GSM_GPRS_AND_NON_GPRS_NA);
		_(NET_GSM_MS_ID_CANNOT_BE_DERIVED);
		_(NET_GSM_IMPLICITLY_DETACHED);
		_(NET_GSM_PLMN_NOT_ALLOWED);
		_(NET_GSM_LA_NOT_ALLOWED);
		_(NET_GSM_ROAMING_NOT_IN_THIS_LA);
		_(NET_GSM_GPRS_SERV_NA_IN_THIS_PLMN);
		_(NET_GSM_NO_SUITABLE_CELLS_IN_LA);
		_(NET_GSM_MSC_TEMP_NOT_REACHABLE);
		_(NET_GSM_NETWORK_FAILURE);
		_(NET_GSM_MAC_FAILURE);
		_(NET_GSM_SYNCH_FAILURE);
		_(NET_GSM_CONGESTION);
		_(NET_GSM_AUTH_UNACCEPTABLE);
		_(NET_GSM_SERV_OPT_NOT_SUPPORTED);
		_(NET_GSM_SERV_OPT_NOT_SUBSCRIBED);
		_(NET_GSM_SERV_TEMP_OUT_OF_ORDER);
		_(NET_GSM_RETRY_ENTRY_NEW_CELL_LOW);
		_(NET_GSM_RETRY_ENTRY_NEW_CELL_HIGH);
		_(NET_GSM_SEMANTICALLY_INCORRECT);
		_(NET_GSM_INVALID_MANDATORY_INFO);
		_(NET_GSM_MSG_TYPE_NONEXISTENT);
		_(NET_GSM_CONDITIONAL_IE_ERROR);
		_(NET_GSM_MSG_TYPE_WRONG_STATE);
		_(NET_GSM_PROTOCOL_ERROR_UNSPECIFIED);
	}
	return "NET_<UNKNOWN>";
}

const char *net_isi_cause_name(enum net_isi_cause value)
{
	switch (value) {
		_(NET_CAUSE_OK);
		_(NET_CAUSE_COMMUNICATION_ERROR);
		_(NET_CAUSE_INVALID_PARAMETER);
		_(NET_CAUSE_NO_SIM);
		_(NET_CAUSE_SIM_NOT_YET_READY);
		_(NET_CAUSE_NET_NOT_FOUND);
		_(NET_CAUSE_REQUEST_NOT_ALLOWED);
		_(NET_CAUSE_CALL_ACTIVE);
		_(NET_CAUSE_SERVER_BUSY);
		_(NET_CAUSE_SECURITY_CODE_REQUIRED);
		_(NET_CAUSE_NOTHING_TO_CANCEL);
		_(NET_CAUSE_UNABLE_TO_CANCEL);
		_(NET_CAUSE_NETWORK_FORBIDDEN);
		_(NET_CAUSE_REQUEST_REJECTED);
		_(NET_CAUSE_CS_NOT_SUPPORTED);
		_(NET_CAUSE_PAR_INFO_NOT_AVAILABLE);
		_(NET_CAUSE_NOT_DONE);
		_(NET_CAUSE_NO_SELECTED_NETWORK);
		_(NET_CAUSE_REQUEST_INTERRUPTED);
		_(NET_CAUSE_TOO_BIG_INDEX);
		_(NET_CAUSE_MEMORY_FULL);
		_(NET_CAUSE_SERVICE_NOT_ALLOWED);
		_(NET_CAUSE_NOT_SUPPORTED_IN_TECH);
	}
	return "NET_<UNKNOWN>";
}

const char *net_status_name(enum net_reg_status value)
{
	switch (value) {
		_(NET_REG_STATUS_HOME);
		_(NET_REG_STATUS_ROAM);
		_(NET_REG_STATUS_ROAM_BLINK);
		_(NET_REG_STATUS_NOSERV);
		_(NET_REG_STATUS_NOSERV_SEARCHING);
		_(NET_REG_STATUS_NOSERV_NOTSEARCHING);
		_(NET_REG_STATUS_NOSERV_NOSIM);
		_(NET_REG_STATUS_POWER_OFF);
		_(NET_REG_STATUS_NSPS);
		_(NET_REG_STATUS_NSPS_NO_COVERAGE);
		_(NET_REG_STATUS_NOSERV_SIM_REJECTED_BY_NW);
	}
	return "NET_<UNKNOWN>";
}

const char *net_message_id_name(enum net_message_id value)
{
	switch (value) {
		_(NET_SET_REQ);
		_(NET_SET_RESP);
		_(NET_RSSI_GET_REQ);
		_(NET_RSSI_GET_RESP);
		_(NET_RSSI_IND);
		_(NET_TIME_IND);
		_(NET_RAT_IND);
		_(NET_RAT_REQ);
		_(NET_RAT_RESP);
		_(NET_REG_STATUS_GET_REQ);
		_(NET_REG_STATUS_GET_RESP);
		_(NET_REG_STATUS_IND);
		_(NET_AVAILABLE_GET_REQ);
		_(NET_AVAILABLE_GET_RESP);
		_(NET_OPER_NAME_READ_REQ);
		_(NET_OPER_NAME_READ_RESP);
		_(NET_COMMON_MESSAGE);
	}
	return "NET_<UNKNOWN>";
}

const char *net_subblock_name(enum net_subblock value)
{
	switch (value) {
		_(NET_REG_INFO_COMMON);
		_(NET_OPERATOR_INFO_COMMON);
		_(NET_RSSI_CURRENT);
		_(NET_GSM_REG_INFO);
		_(NET_DETAILED_NETWORK_INFO);
		_(NET_GSM_OPERATOR_INFO);
		_(NET_TIME_INFO);
		_(NET_GSM_BAND_INFO);
		_(NET_RAT_INFO);
		_(NET_AVAIL_NETWORK_INFO_COMMON);
		_(NET_OPER_NAME_INFO);
	}
	return "NET_<UNKNOWN>";
}

const char *gss_message_id_name(enum gss_message_id value)
{
	switch (value) {
		_(GSS_CS_SERVICE_REQ);
		_(GSS_CS_SERVICE_RESP);
		_(GSS_CS_SERVICE_FAIL_RESP);
	}
	return "GSS_<UNKNOWN>";
}

const char *gss_subblock_name(enum gss_subblock value)
{
	switch (value) {
		_(GSS_RAT_INFO);
	}
	return "GSS_<UNKNOWN>";
}

const char *gpds_message_id_name(enum gpds_message_id value)
{
	switch (value) {
		_(GPDS_LL_CONFIGURE_REQ);
		_(GPDS_LL_CONFIGURE_RESP);
		_(GPDS_CONTEXT_ID_CREATE_REQ);
		_(GPDS_CONTEXT_ID_CREATE_RESP);
		_(GPDS_CONTEXT_ID_CREATE_IND);
		_(GPDS_CONTEXT_ID_DELETE_IND);
		_(GPDS_CONTEXT_CONFIGURE_REQ);
		_(GPDS_CONTEXT_CONFIGURE_RESP);
		_(GPDS_CONTEXT_ACTIVATE_REQ);
		_(GPDS_CONTEXT_ACTIVATE_RESP);
		_(GPDS_CONTEXT_ACTIVATE_IND);
		_(GPDS_CONTEXT_DEACTIVATE_REQ);
		_(GPDS_CONTEXT_DEACTIVATE_RESP);
		_(GPDS_CONTEXT_DEACTIVATE_IND);
		_(GPDS_CONTEXT_MWI_ACT_REQUEST_IND);
		_(GPDS_CONTEXT_NWI_ACT_REJECT_REQ);
		_(GPDS_CONTEXT_NWI_ACT_REJECT_RESP);
		_(GPDS_CONFIGURE_REQ);
		_(GPDS_CONFIGURE_RESP);
		_(GPDS_ATTACH_REQ);
		_(GPDS_ATTACH_RESP);
		_(GPDS_ATTACH_IND);
		_(GPDS_DETACH_REQ);
		_(GPDS_DETACH_RESP);
		_(GPDS_DETACH_IND);
		_(GPDS_STATUS_REQ);
		_(GPDS_STATUS_RESP);
		_(GPDS_SMS_PDU_SEND_REQ);
		_(GPDS_SMS_PDU_SEND_RESP);
		_(GPDS_SMS_PDU_RECEIVE_IND);
		_(GPDS_TRANSFER_STATUS_IND);
		_(GPDS_CONTEXT_ACTIVATE_FAIL_IND);
		_(GPDS_LL_BIND_REQ);
		_(GPDS_LL_BIND_RESP);
		_(GPDS_CONTEXT_STATUS_REQ);
		_(GPDS_CONTEXT_STATUS_RESP);
		_(GPDS_CONTEXT_STATUS_IND);
		_(GPDS_CONTEXT_ACTIVATING_IND);
		_(GPDS_CONTEXT_MODIFY_REQ);
		_(GPDS_CONTEXT_MODIFY_RESP);
		_(GPDS_CONTEXT_MODIFY_IND);
		_(GPDS_ATTACH_FAIL_IND);
		_(GPDS_CONTEXT_DEACTIVATING_IND);
		_(GPDS_CONFIGURATION_INFO_REQ);
		_(GPDS_CONFIGURATION_INFO_RESP);
		_(GPDS_CONFIGURATION_INFO_IND);
		_(GPDS_CONTEXT_AUTH_REQ);
		_(GPDS_CONTEXT_AUTH_RESP);
		_(GPDS_TEST_MODE_REQ);
		_(GPDS_TEST_MODE_RESP);
		_(GPDS_RADIO_ACTIVITY_IND);
		_(GPDS_FORCED_READY_STATE_REQ);
		_(GPDS_FORCED_READY_STATE_RESP);
		_(GPDS_CONTEXTS_CLEAR_REQ);
		_(GPDS_CONTEXTS_CLEAR_RESP);
		_(GPDS_MBMS_SERVICE_SELECTION_REQ);
		_(GPDS_MBMS_SERVICE_SELECTION_RESP);
		_(GPDS_MBMS_STATUS_IND);
		_(GPDS_MBMS_CONTEXT_CREATE_REQ);
		_(GPDS_MBMS_CONTEXT_CREATE_RESP);
		_(GPDS_MBMS_CONTEXT_ACTIVATE_REQ);
		_(GPDS_MBMS_CONTEXT_ACTIVATE_RESP);
		_(GPDS_MBMS_CONTEXT_DELETE_REQ);
		_(GPDS_MBMS_CONTEXT_DELETE_RESP);
		_(GPDS_MBMS_CONTEXT_DELETE_IND);
		_(GPDS_MBMS_SERVICE_SELECTION_IND);
		_(GPDS_MBMS_SERVICE_AVAILABLE_IND);
		_(GPDS_TEST_REQ);
		_(GPDS_TEST_RESP);
	}
	return "GPSD_<UNKNOWN>";
}

const char *gpds_subblock_name(enum gpds_subblock value)
{
	switch (value) {
		_(GPDS_COMP_INFO);
		_(GPDS_QOS_REQ_INFO);
		_(GPDS_QOS_MIN_INFO);
		_(GPDS_QOS_NEG_INFO);
		_(GPDS_PDP_ADDRESS_INFO);
		_(GPDS_APN_INFO);
		_(GPDS_QOS99_REQ_INFO);
		_(GPDS_QOS99_MIN_INFO);
		_(GPDS_QOS99_NEG_INFO);
		_(GPDS_TFT_INFO);
		_(GPDS_TFT_FILTER_INFO);
		_(GPDS_USER_NAME_INFO);
		_(GPDS_PASSWORD_INFO);
		_(GPDS_PDNS_ADDRESS_INFO);
		_(GPDS_SDNS_ADDRESS_INFO);
		_(GPDS_CHALLENGE_INFO);
		_(GPDS_DNS_ADDRESS_REQ_INFO);
		_(GPDS_COMMON_MESSAGE);
	}
	return "GPDS_<UNKNOWN>";
}

const char *gpds_status_name(enum gpds_status value)
{
	switch (value) {
		_(GPDS_ERROR);
		_(GPDS_OK);
		_(GPDS_FAIL);
	}
	return "GPDS_<UNKNOWN>";
}

const char *gpds_isi_cause_name(enum gpds_isi_cause value)
{
	switch (value) {
		_(GPDS_CAUSE_UNKNOWN);
		_(GPDS_CAUSE_IMSI);
		_(GPDS_CAUSE_MS_ILLEGAL);
		_(GPDS_CAUSE_ME_ILLEGAL);
		_(GPDS_CAUSE_GPRS_NOT_ALLOWED);
		_(GPDS_NOT_ALLOWED);
		_(GPDS_CAUSE_MS_IDENTITY);
		_(GPDS_CAUSE_DETACH);
		_(GPDS_PLMN_NOT_ALLOWED);
		_(GPDS_LA_NOT_ALLOWED);
		_(GPDS_ROAMING_NOT_ALLOWED);
		_(GPDS_CAUSE_GPRS_NOT_ALLOWED_IN_PLMN);
		_(GPDS_CAUSE_MSC_NOT_REACH);
		_(GPDS_CAUSE_PLMN_FAIL);
		_(GPDS_CAUSE_NETWORK_CONGESTION);
		_(GPDS_CAUSE_MBMS_BEARER_CAPABILITY_INSUFFICIENT);
		_(GPDS_CAUSE_LLC_SNDCP_FAILURE);
		_(GPDS_CAUSE_RESOURCE_INSUFF);
		_(GPDS_CAUSE_APN);
		_(GPDS_CAUSE_PDP_UNKNOWN);
		_(GPDS_CAUSE_AUTHENTICATION);
		_(GPDS_CAUSE_ACT_REJECT_GGSN);
		_(GPDS_CAUSE_ACT_REJECT);
		_(GPDS_CAUSE_SERV_OPT_NOT_SUPPORTED);
		_(GPDS_CAUSE_SERV_OPT_NOT_SUBSCRIBED);
		_(GPDS_CAUSE_SERV_OPT_OUT_OF_ORDER);
		_(GPDS_CAUSE_NSAPI_ALREADY_USED);
		_(GPDS_CAUSE_DEACT_REGULAR);
		_(GPDS_CAUSE_QOS);
		_(GPDS_CAUSE_NETWORK_FAIL);
		_(GPDS_CAUSE_REACTIVATION_REQ);
		_(GPDS_CAUSE_FEAT_NOT_SUPPORTED);
		_(GPDS_CAUSE_TFT_SEMANTIC_ERROR);
		_(GPDS_CAUSE_TFT_SYNTAX_ERROR);
		_(GPDS_CAUSE_CONTEXT_UNKNOWN);
		_(GPDS_CAUSE_FILTER_SEMANTIC_ERROR);
		_(GPDS_CAUSE_FILTER_SYNTAX_ERROR);
		_(GPDS_CAUSE_CONT_WITHOUT_TFT);
		_(GPDS_CAUSE_MULTICAST_MEMBERSHIP_TIMEOUT);
		_(GPDS_CAUSE_INVALID_MANDATORY_INFO);
		_(GPDS_CAUSE_MSG_TYPE_NON_EXISTENTOR_NOT_IMPLTD);
		_(GPDS_CAUSE_MSG_TYPE_NOT_COMPATIBLE_WITH_PROTOCOL_STATE);
		_(GPDS_CAUSE_IE_NON_EXISTENT_OR_NOT_IMPLEMENTED);
		_(GPDS_CAUSE_CONDITIONAL_IE_ERROR);
		_(GPDS_CUASEMSG_NOT_COMPATIBLE_WITH_PROTOCOL_STATE);
		_(GPDS_CAUSE_UNSPECIFIED);
		_(GPDS_CAUSE_APN_INCOMPATIBLE_WITH_CURR_CTXT);
		_(GPDS_CAUSE_FDN);
		_(GPDS_CAUSE_USER_ABORT);
		_(GPDS_CAUSE_CS_INACTIVE);
		_(GPDS_CAUSE_CSD_OVERRIDE);
		_(GPDS_CAUSE_APN_CONTROL);
		_(GPDS_CAUSE_CALL_CONTROL);
		_(GPDS_CAUSE_TEMPERATURE_LIMIT);
		_(GPDS_CAUSE_RETRY_COUNTER_EXPIRED);
		_(GPDS_CAUSE_NO_CONNECTION);
		_(GPDS_CAUSE_DETACHED);
		_(GPDS_CAUSE_NO_SERVICE_POWER_SAVE);
		_(GPDS_CAUSE_SIM_REMOVED);
		_(GPDS_CAUSE_POWER_OFF);
		_(GPDS_CAUSE_LAI_FORBIDDEN_NATIONAL_ROAM_LIST);
		_(GPDS_CAUSE_LAI_FORBIDDEN_REG_PROVISION_LIST);
		_(GPDS_CAUSE_ACCESS_BARRED);
		_(GPDS_CAUSE_FATAL_FAILURE);
		_(GPDS_CAUSE_AUT_FAILURE);
	}
	return "GPDS_<UNKNOWN>";
}

#undef _

static void hex_dump(const char *name, const uint8_t m[], size_t len)
{
	char hex[3 * 16 + 1];
	char ascii[16 + 1];
	size_t i, j, k;

	ofono_debug("%s [%s=0x%02X len=%zu]:", name,
			"message_id", m[0], len);

	strcpy(hex, " **"), j = 3;
	strcpy(ascii, "."), k = 1;

	for (i = 0; i < len; i++) {
		sprintf(hex + j, " %02X", m[i]), j += 3;
		ascii[k++] = g_ascii_isgraph(m[i]) ? m[i] : '.';

		if ((j & 48) == 48) {
			ofono_debug("    *%-48s : %.*s", hex, (int)k, ascii);
			j = 0, k = 0;
		}
	}

	if (j)
		ofono_debug("    *%-48s : %.*s", hex, (int)k, ascii);
}

void ss_debug(const void *restrict buf, size_t len, void *data)
{
	const uint8_t *m = buf;
	hex_dump(ss_message_id_name(m[0]), m, len);
}

void mtc_debug(const void *restrict buf, size_t len, void *data)
{
	const uint8_t *m = buf;
	hex_dump(mtc_message_id_name(m[0]), m, len);
}

void sms_debug(const void *restrict buf, size_t len, void *data)
{
	const uint8_t *m = buf;
	hex_dump(sms_message_id_name(m[0]), m, len);
}

void sim_debug(const void *restrict buf, size_t len, void *data)
{
	const uint8_t *m = buf;
	hex_dump(sim_message_id_name(m[0]), m, len);
}

void info_debug(const void *restrict buf, size_t len, void *data)
{
	const uint8_t *m = buf;
	hex_dump(info_message_id_name(m[0]), m, len);
}

void call_debug(const void *restrict buf, size_t len, void *data)
{
	const uint8_t *m = buf;
	hex_dump(call_message_id_name(m[0]), m, len);
}

void net_debug(const void *restrict buf, size_t len, void *data)
{
	const uint8_t *m = buf;
	hex_dump(net_message_id_name(m[0]), m, len);
}

void gss_debug(const void *restrict buf, size_t len, void *data)
{
	const uint8_t *m = buf;
	hex_dump(gss_message_id_name(m[0]), m, len);
}

void gpds_debug(const void *restrict buf, size_t len, void *data)
{
	const uint8_t *m = buf;
	hex_dump(gpds_message_id_name(m[0]), m, len);
}