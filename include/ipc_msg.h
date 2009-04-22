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

#include <corosync/ipc_gen.h>

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
	MESSAGE_REQ_MSG_QUEUECAPACITYTHRESHOLDSET = 21,
	MESSAGE_REQ_MSG_QUEUECAPACITYTHRESHOLDGET = 22,
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
	MESSAGE_RES_MSG_QUEUECAPACITYTHRESHOLDSET = 21,
	MESSAGE_RES_MSG_QUEUECAPACITYTHRESHOLDGET = 22,
	MESSAGE_RES_MSG_METADATASIZEGET = 23,
	MESSAGE_RES_MSG_LIMITGET = 24,
	MESSAGE_RES_MSG_QUEUEOPEN_CALLBACK = 25,
	MESSAGE_RES_MSG_QUEUEGROUPTRACK_CALLBACK = 26,
	MESSAGE_RES_MSG_MESSAGEDELIVERED_CALLBACK = 27,
	MESSAGE_RES_MSG_MESSAGERECEIVED_CALLBACK = 28
};

struct req_lib_msg_queueopen {
	mar_req_header_t header;
	SaMsgQueueHandleT queue_handle;
	SaNameT queue_name;
	SaUint8T create_attrs_flag;
	SaMsgQueueCreationAttributesT create_attrs;
	SaMsgQueueOpenFlagsT open_flags;
	SaTimeT timeout;
};

struct res_lib_msg_queueopen {
	mar_res_header_t header;
	SaUint32T queue_id;
};

struct req_lib_msg_queueopenasync {
	mar_req_header_t header;
	SaMsgQueueHandleT queue_handle;
	SaNameT queue_name;
	SaUint8T create_attrs_flag;
	SaMsgQueueCreationAttributesT create_attrs;
	SaMsgQueueOpenFlagsT open_flags;
	SaInvocationT invocation;
};

struct res_lib_msg_queueopenasync {
	mar_res_header_t header;
	SaUint32T queue_id;
};

struct req_lib_msg_queueclose {
	mar_req_header_t header;
	SaNameT queue_name;
	SaUint32T queue_id;
};

struct res_lib_msg_queueclose {
	mar_res_header_t header;
};

struct req_lib_msg_queuestatusget {
	mar_req_header_t header;
	SaNameT queue_name;
};

struct res_lib_msg_queuestatusget {
	mar_res_header_t header;
	SaMsgQueueStatusT queue_status;
};

struct req_lib_msg_queueretentiontimeset {
	mar_req_header_t header;
	SaNameT queue_name;
	SaUint32T queue_id;
	SaTimeT retention_time;
};

struct res_lib_msg_queueretentiontimeset {
	mar_res_header_t header;
};

struct req_lib_msg_queueunlink {
	mar_req_header_t header;
	SaNameT queue_name;
};

struct res_lib_msg_queueunlink {
	mar_res_header_t header;
};

struct req_lib_msg_queuegroupcreate {
	mar_req_header_t header;
	SaNameT group_name;
	SaMsgQueueGroupPolicyT policy;
};

struct res_lib_msg_queuegroupcreate {
	mar_res_header_t header;
};

struct req_lib_msg_queuegroupinsert {
	mar_req_header_t header;
	SaNameT group_name;
	SaNameT queue_name;
};

struct res_lib_msg_queuegroupinsert {
	mar_res_header_t header;
};

struct req_lib_msg_queuegroupremove {
	mar_req_header_t header;
	SaNameT group_name;
	SaNameT queue_name;
};

struct res_lib_msg_queuegroupremove {
	mar_res_header_t header;
};

struct req_lib_msg_queuegroupdelete {
	mar_req_header_t header;
	SaNameT group_name;
};

struct res_lib_msg_queuegroupdelete {
	mar_res_header_t header;
};

struct req_lib_msg_queuegrouptrack {
	mar_req_header_t header;
	SaNameT group_name;
	SaUint8T track_flags;
	SaUint8T buffer_flag;
};

struct res_lib_msg_queuegrouptrack {
	mar_res_header_t header;
	SaMsgQueueGroupNotificationBufferT buffer;
};

struct req_lib_msg_queuegrouptrackstop {
	mar_req_header_t header;
	SaNameT group_name;
};

struct res_lib_msg_queuegrouptrackstop {
	mar_res_header_t header;
};

struct req_lib_msg_queuegroupnotificationfree {
	mar_req_header_t header;
};

struct res_lib_msg_queuegroupnotificationfree {
	mar_res_header_t header;
};

struct req_lib_msg_messagesend {
	mar_req_header_t header;
	SaNameT destination;
	SaTimeT timeout;
	SaMsgMessageT message;
};

struct res_lib_msg_messagesend {
	mar_res_header_t header;
};

struct req_lib_msg_messagesendasync {
	mar_req_header_t header;
	SaNameT destination;
	SaInvocationT invocation;
	SaMsgMessageT message;
};

struct res_lib_msg_messagesendasync {
	mar_res_header_t header;
};

struct req_lib_msg_messageget {
	mar_req_header_t header;
	SaNameT queue_name;
	SaUint32T queue_id;
	SaTimeT timeout;
};

struct res_lib_msg_messageget {
	mar_res_header_t header;
	SaTimeT send_time;
	SaMsgSenderIdT sender_id;
	SaMsgMessageT message;
};

struct req_lib_msg_messagedatafree {
	mar_req_header_t header;
};

struct res_lib_msg_messagedatafree {
	mar_res_header_t header;
};

struct req_lib_msg_messagecancel {
	mar_req_header_t header;
	SaNameT queue_name;
	SaUint32T queue_id;
};

struct res_lib_msg_messagecancel {
	mar_res_header_t header;
};

struct req_lib_msg_messagesendreceive {
	mar_req_header_t header;
	SaNameT destination;
	SaTimeT timeout;
	SaMsgMessageT message;
};

struct res_lib_msg_messagesendreceive {
	mar_res_header_t header;
};

struct req_lib_msg_messagereply {
	mar_req_header_t header;
	SaMsgMessageT reply_message;
	SaMsgSenderIdT sender_id;
	SaTimeT timeout;
};

struct res_lib_msg_messagereply {
	mar_res_header_t header;
}
;
struct req_lib_msg_messagereplyasync {
	mar_req_header_t header;
	SaMsgMessageT reply_message;
	SaMsgSenderIdT sender_id;
	SaInvocationT invocation;
};

struct res_lib_msg_messagereplyasync {
	mar_res_header_t header;
};

struct req_lib_msg_queuecapacitythresholdset {
	mar_req_header_t header;
	SaNameT queue_name;
	SaUint32T queue_id;
};

struct res_lib_msg_queuecapacitythresholdset {
	mar_res_header_t header;
};

struct req_lib_msg_queuecapacitythresholdget {
	mar_req_header_t header;
	SaNameT queue_name;
	SaUint32T queue_id;
};

struct res_lib_msg_queuecapacitythresholdget {
	mar_res_header_t header;
};

struct req_lib_msg_metadatasizeget {
	mar_req_header_t header;
};

struct res_lib_msg_metadatasizeget {
	mar_res_header_t header;
};

struct req_lib_msg_limitget {
	mar_req_header_t header;
	SaMsgLimitIdT limit_id;
};

struct res_lib_msg_limitget {
	mar_res_header_t header;
	SaUint64T value;
};

struct res_lib_msg_queueopen_callback {
	mar_res_header_t header;
	SaMsgQueueHandleT queue_handle;
	SaInvocationT invocation;
};

struct res_lib_msg_queuegrouptrack_callback {
	mar_res_header_t header;
	SaNameT group_name;
	SaMsgQueueGroupNotificationBufferT buffer;
	SaUint32T member_count;
};

struct res_lib_msg_messagedelivered_callback {
	mar_res_header_t header;
	SaInvocationT invocation;
};

struct res_lib_msg_messagereceived_callback {
	mar_res_header_t header;
	SaMsgQueueHandleT queue_handle;
};

#endif /* IPC_MSG_H_DEFINED */
