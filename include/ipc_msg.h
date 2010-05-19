/*
 * Copyright (c) 2005 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
 *
 * This software licensed under BSD license, the text of which follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef IPC_MSG_H_DEFINED
#define IPC_MSG_H_DEFINED

#include "saAis.h"
#include "saMsg.h"
#include <corosync/hdb.h>
#include "mar_msg.h"

enum req_lib_msg_queue_types {
	MESSAGE_REQ_MSG_QUEUEOPEN = 0,
	MESSAGE_REQ_MSG_QUEUEOPENASYNC = 1,
	MESSAGE_REQ_MSG_QUEUECLOSE = 2,
	MESSAGE_REQ_MSG_QUEUESTATUSGET = 3,
	MESSAGE_REQ_MSG_QUEUERETENTIONTIMESET = 4,
	MESSAGE_REQ_MSG_QUEUEUNLINK = 5,
	MESSAGE_REQ_MSG_QUEUEGROUPCREATE = 6,
	MESSAGE_REQ_MSG_QUEUEGROUPINSERT = 7,
	MESSAGE_REQ_MSG_QUEUEGROUPREMOVE = 8,
	MESSAGE_REQ_MSG_QUEUEGROUPDELETE = 9,
	MESSAGE_REQ_MSG_QUEUEGROUPTRACK = 10,
	MESSAGE_REQ_MSG_QUEUEGROUPTRACKSTOP = 11,
	MESSAGE_REQ_MSG_QUEUEGROUPNOTIFICATIONFREE = 12,
	MESSAGE_REQ_MSG_MESSAGESEND = 13,
	MESSAGE_REQ_MSG_MESSAGESENDASYNC = 14,
	MESSAGE_REQ_MSG_MESSAGEGET = 15,
	MESSAGE_REQ_MSG_MESSAGEDATAFREE = 16,
	MESSAGE_REQ_MSG_MESSAGECANCEL = 17,
	MESSAGE_REQ_MSG_MESSAGESENDRECEIVE = 18,
	MESSAGE_REQ_MSG_MESSAGEREPLY = 19,
	MESSAGE_REQ_MSG_MESSAGEREPLYASYNC = 20,
	MESSAGE_REQ_MSG_QUEUECAPACITYTHRESHOLDSSET = 21,
	MESSAGE_REQ_MSG_QUEUECAPACITYTHRESHOLDSGET = 22,
	MESSAGE_REQ_MSG_METADATASIZEGET = 23,
	MESSAGE_REQ_MSG_LIMITGET = 24
};

enum res_lib_msg_queue_types {
	MESSAGE_RES_MSG_QUEUEOPEN = 0,
	MESSAGE_RES_MSG_QUEUEOPENASYNC = 1,
	MESSAGE_RES_MSG_QUEUECLOSE = 2,
	MESSAGE_RES_MSG_QUEUESTATUSGET = 3,
	MESSAGE_RES_MSG_QUEUERETENTIONTIMESET = 4,
	MESSAGE_RES_MSG_QUEUEUNLINK = 5,
	MESSAGE_RES_MSG_QUEUEGROUPCREATE = 6,
	MESSAGE_RES_MSG_QUEUEGROUPINSERT = 7,
	MESSAGE_RES_MSG_QUEUEGROUPREMOVE = 8,
	MESSAGE_RES_MSG_QUEUEGROUPDELETE = 9,
	MESSAGE_RES_MSG_QUEUEGROUPTRACK = 10,
	MESSAGE_RES_MSG_QUEUEGROUPTRACKSTOP = 11,
	MESSAGE_RES_MSG_QUEUEGROUPNOTIFICATIONFREE = 12,
	MESSAGE_RES_MSG_MESSAGESEND = 13,
	MESSAGE_RES_MSG_MESSAGESENDASYNC = 14,
	MESSAGE_RES_MSG_MESSAGEGET = 15,
	MESSAGE_RES_MSG_MESSAGEDATAFREE = 16,
	MESSAGE_RES_MSG_MESSAGECANCEL = 17,
	MESSAGE_RES_MSG_MESSAGESENDRECEIVE = 18,
	MESSAGE_RES_MSG_MESSAGEREPLY = 19,
	MESSAGE_RES_MSG_MESSAGEREPLYASYNC = 20,
	MESSAGE_RES_MSG_QUEUECAPACITYTHRESHOLDSSET = 21,
	MESSAGE_RES_MSG_QUEUECAPACITYTHRESHOLDSGET = 22,
	MESSAGE_RES_MSG_METADATASIZEGET = 23,
	MESSAGE_RES_MSG_LIMITGET = 24,
	MESSAGE_RES_MSG_QUEUEOPEN_CALLBACK = 25,
	MESSAGE_RES_MSG_QUEUEGROUPTRACK_CALLBACK = 26,
	MESSAGE_RES_MSG_MESSAGEDELIVERED_CALLBACK = 27,
	MESSAGE_RES_MSG_MESSAGERECEIVED_CALLBACK = 28
};

/*
 * Define the limits for the message service.
 * These limits are implementation specific and
 * can be obtained via the library call saMsgLimitGet
 * by passing the appropriate limitId (see saMsg.h).
 */
#define MSG_MAX_PRIORITY_AREA_SIZE 128000
#define MSG_MAX_QUEUE_SIZE         512000
#define MSG_MAX_NUM_QUEUES            32
#define MSG_MAX_NUM_QUEUE_GROUPS      16
#define MSG_MAX_NUM_QUEUES_PER_GROUP  16
#define MSG_MAX_MESSAGE_SIZE          32
#define MSG_MAX_REPLY_SIZE            32

struct req_lib_msg_queueopen {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_msg_queue_handle_t queue_handle __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
	mar_uint8_t create_attrs_flag __attribute__((aligned(8)));
	mar_msg_queue_creation_attributes_t create_attrs __attribute__((aligned(8)));
	mar_msg_queue_open_flags_t open_flags __attribute__((aligned(8)));
	mar_time_t timeout __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_queueopen {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint32_t queue_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_queueopenasync {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_msg_queue_handle_t queue_handle __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
	mar_uint8_t create_attrs_flag __attribute__((aligned(8)));
	mar_msg_queue_creation_attributes_t create_attrs __attribute__((aligned(8)));
	mar_msg_queue_open_flags_t open_flags __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_queueopenasync {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint32_t queue_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_queueclose {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
	mar_uint32_t queue_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_queueclose {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_queuestatusget {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_queuestatusget {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_msg_queue_status_t queue_status __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_queueretentiontimeset {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
	mar_uint32_t queue_id __attribute__((aligned(8)));
	mar_time_t retention_time __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_queueretentiontimeset {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_queueunlink {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_queueunlink {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_queuegroupcreate {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t group_name __attribute__((aligned(8)));
	mar_msg_queue_group_policy_t policy __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_queuegroupcreate {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_queuegroupinsert {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t group_name __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_queuegroupinsert {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_queuegroupremove {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t group_name __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_queuegroupremove {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_queuegroupdelete {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t group_name __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_queuegroupdelete {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_queuegrouptrack {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t group_name __attribute__((aligned(8)));
	mar_uint8_t track_flags __attribute__((aligned(8)));
	mar_uint8_t buffer_flag __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_queuegrouptrack {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_name_t group_name __attribute__((aligned(8)));
	mar_uint32_t member_count __attribute__((aligned(8)));
	mar_uint32_t number_of_items __attribute__((aligned(8)));
	mar_msg_queue_group_policy_t queue_group_policy __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_queuegrouptrackstop {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t group_name __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_queuegrouptrackstop {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_queuegroupnotificationfree {
	coroipc_request_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_queuegroupnotificationfree {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_messagesend {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t destination __attribute__((aligned(8)));
	mar_time_t timeout __attribute__((aligned(8)));
	mar_msg_message_t message __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_messagesend {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_messagesendasync {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t destination __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
	mar_msg_message_t message __attribute__((aligned(8)));
	mar_msg_ack_flags_t ack_flags __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_messagesendasync {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_messageget {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
	mar_uint32_t queue_id __attribute__((aligned(8)));
	mar_time_t timeout __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_messageget {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_time_t send_time __attribute__((aligned(8)));
	mar_msg_sender_id_t sender_id __attribute__((aligned(8)));
	mar_msg_message_t message __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_messagedatafree {
	coroipc_request_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_messagedatafree {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_messagecancel {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
	mar_uint32_t queue_id __attribute__((aligned(8)));
	mar_uint32_t pid __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_messagecancel {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_messagesendreceive {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t destination __attribute__((aligned(8)));
	mar_time_t timeout __attribute__((aligned(8)));
	mar_size_t reply_size __attribute__((aligned(8)));
	mar_msg_message_t message __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_messagesendreceive {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_time_t reply_time __attribute__((aligned(8)));
	mar_msg_message_t message __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_messagereply {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_msg_message_t reply_message __attribute__((aligned(8)));
	mar_msg_sender_id_t sender_id __attribute__((aligned(8)));
	mar_time_t timeout __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_messagereply {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_messagereplyasync {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_msg_message_t reply_message __attribute__((aligned(8)));
	mar_msg_sender_id_t sender_id __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
	mar_msg_ack_flags_t ack_flags __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_messagereplyasync {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_queuecapacitythresholdsset {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
	mar_uint32_t queue_id __attribute__((aligned(8)));
	mar_msg_queue_thresholds_t thresholds __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_queuecapacitythresholdsset {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_queuecapacitythresholdsget {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
	mar_uint32_t queue_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_queuecapacitythresholdsget {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_msg_queue_thresholds_t thresholds __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_metadatasizeget {
	coroipc_request_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_metadatasizeget {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_msg_limitget {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_msg_limit_id_t limit_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_limitget {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint64_t value __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_queueopen_callback {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_msg_queue_handle_t queue_handle __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_queuegrouptrack_callback {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_name_t group_name __attribute__((aligned(8)));
	mar_uint32_t member_count __attribute__((aligned(8)));
	mar_uint32_t number_of_items __attribute__((aligned(8)));
	mar_msg_queue_group_policy_t queue_group_policy __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_messagedelivered_callback {
	coroipc_response_header_t header __attribute__((aligned(8)));
/* 	mar_msg_queue_handle_t queue_handle __attribute__((aligned(8))); */
	mar_invocation_t invocation __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_msg_messagereceived_callback {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_msg_queue_handle_t queue_handle __attribute__((aligned(8)));
} __attribute__((aligned(8)));

#endif /* IPC_MSG_H_DEFINED */
