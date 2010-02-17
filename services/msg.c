/*
 * Copyright (c) 2006 MontaVista Software, Inc.
 * Copyright (c) 2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Authors: Steven Dake (sdake@redhat.com), Ryan O'Hara (rohara@redhat.com)
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

#include <config.h>

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>

#include <assert.h>
#include <inttypes.h>

#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include <corosync/coroipcc.h>
#include <corosync/corodefs.h>
#include <corosync/mar_gen.h>
#include <corosync/swab.h>
#include <corosync/list.h>
#include <corosync/hdb.h>
#include <corosync/engine/coroapi.h>
#include <corosync/engine/logsys.h>
#include <corosync/lcr/lcr_comp.h>

#include "../include/saAis.h"
#include "../include/saMsg.h"
#include "../include/ipc_msg.h"

LOGSYS_DECLARE_SUBSYS ("MSG");

enum msg_exec_message_req_types {
	MESSAGE_REQ_EXEC_MSG_QUEUEOPEN = 0,
	MESSAGE_REQ_EXEC_MSG_QUEUEOPENASYNC = 1,
	MESSAGE_REQ_EXEC_MSG_QUEUECLOSE = 2,
	MESSAGE_REQ_EXEC_MSG_QUEUESTATUSGET = 3,
	MESSAGE_REQ_EXEC_MSG_QUEUERETENTIONTIMESET = 4,
	MESSAGE_REQ_EXEC_MSG_QUEUEUNLINK = 5,
	MESSAGE_REQ_EXEC_MSG_QUEUEGROUPCREATE = 6,
	MESSAGE_REQ_EXEC_MSG_QUEUEGROUPINSERT = 7,
	MESSAGE_REQ_EXEC_MSG_QUEUEGROUPREMOVE = 8,
	MESSAGE_REQ_EXEC_MSG_QUEUEGROUPDELETE = 9,
	MESSAGE_REQ_EXEC_MSG_QUEUEGROUPTRACK = 10,
	MESSAGE_REQ_EXEC_MSG_QUEUEGROUPTRACKSTOP = 11,
	MESSAGE_REQ_EXEC_MSG_QUEUEGROUPNOTIFICATIONFREE = 12,
	MESSAGE_REQ_EXEC_MSG_MESSAGESEND = 13,
	MESSAGE_REQ_EXEC_MSG_MESSAGESENDASYNC = 14,
	MESSAGE_REQ_EXEC_MSG_MESSAGEGET = 15,
	MESSAGE_REQ_EXEC_MSG_MESSAGEDATAFREE = 16,
	MESSAGE_REQ_EXEC_MSG_MESSAGECANCEL = 17,
	MESSAGE_REQ_EXEC_MSG_MESSAGESENDRECEIVE = 18,
	MESSAGE_REQ_EXEC_MSG_MESSAGEREPLY = 19,
	MESSAGE_REQ_EXEC_MSG_MESSAGEREPLYASYNC = 20,
	MESSAGE_REQ_EXEC_MSG_QUEUECAPACITYTHRESHOLDSSET = 21,
	MESSAGE_REQ_EXEC_MSG_QUEUECAPACITYTHRESHOLDSGET = 22,
	MESSAGE_REQ_EXEC_MSG_METADATASIZEGET = 23,
	MESSAGE_REQ_EXEC_MSG_LIMITGET = 24,
	MESSAGE_REQ_EXEC_MSG_SYNC_QUEUE = 25,
	MESSAGE_REQ_EXEC_MSG_SYNC_QUEUE_MESSAGE = 26,
	MESSAGE_REQ_EXEC_MSG_SYNC_QUEUE_REFCOUNT = 27,
	MESSAGE_REQ_EXEC_MSG_SYNC_GROUP = 28,
	MESSAGE_REQ_EXEC_MSG_SYNC_GROUP_MEMBER = 29,
	MESSAGE_REQ_EXEC_MSG_SYNC_REPLY = 30,
	MESSAGE_REQ_EXEC_MSG_QUEUE_TIMEOUT = 31,
	MESSAGE_REQ_EXEC_MSG_MESSAGEGET_TIMEOUT = 32,
	MESSAGE_REQ_EXEC_MSG_SENDRECEIVE_TIMEOUT = 33,
};

enum msg_sync_state {
	MSG_SYNC_STATE_NOT_STARTED,
	MSG_SYNC_STATE_STARTED,
	MSG_SYNC_STATE_QUEUE,
	MSG_SYNC_STATE_GROUP,
	MSG_SYNC_STATE_REPLY,
};

enum msg_sync_iteration_state {
	MSG_SYNC_ITERATION_STATE_QUEUE,
	MSG_SYNC_ITERATION_STATE_QUEUE_REFCOUNT,
	MSG_SYNC_ITERATION_STATE_QUEUE_MESSAGE,
	MSG_SYNC_ITERATION_STATE_GROUP,
	MSG_SYNC_ITERATION_STATE_GROUP_MEMBER,
	MSG_SYNC_ITERATION_STATE_REPLY
};

struct refcount_set {
	unsigned int refcount;
	unsigned int nodeid;
};

typedef struct {
	unsigned int refcount __attribute__((aligned(8)));
	unsigned int nodeid __attribute__((aligned(8)));
} mar_refcount_set_t;

struct message_entry {
	mar_time_t send_time;
	mar_msg_sender_id_t sender_id;
	mar_msg_message_t message;
	struct list_head queue_list;
	struct list_head message_list;
};

struct reply_entry {
	mar_msg_sender_id_t sender_id;
	mar_size_t reply_size;
	mar_message_source_t source;
	corosync_timer_handle_t timer_handle;
	struct list_head reply_list;
};

struct track_entry {
	mar_name_t group_name;
	mar_uint8_t track_flags;
	mar_message_source_t source;
	struct list_head track_list;
};

struct cleanup_entry {
	mar_name_t queue_name;
	mar_uint32_t queue_id;
	mar_msg_queue_handle_t queue_handle; /* ? */
	struct list_head cleanup_list;
};

struct priority_area {
	mar_size_t queue_size;
	mar_size_t queue_used;
	mar_size_t capacity_reached;
	mar_size_t capacity_available;
	mar_uint32_t number_of_messages;
	struct list_head message_head;
};

struct pending_entry {
	mar_name_t queue_name;
	mar_message_source_t source;
	corosync_timer_handle_t timer_handle;
	struct list_head pending_list;
};

struct queue_entry {
	mar_name_t queue_name;
	mar_time_t close_time;
	mar_uint32_t refcount;
	mar_uint32_t queue_id;
	mar_uint8_t unlink_flag;
	mar_message_source_t source;
	mar_msg_queue_open_flags_t open_flags;
	mar_msg_queue_group_changes_t change_flag;
	mar_msg_queue_creation_attributes_t create_attrs;
	mar_msg_queue_handle_t queue_handle;
	corosync_timer_handle_t timer_handle;
	struct group_entry *group;
	struct list_head queue_list;
	struct list_head group_list;
	struct list_head message_head;
	struct list_head pending_head;
	struct priority_area priority[SA_MSG_MESSAGE_LOWEST_PRIORITY+1];
	struct refcount_set refcount_set[PROCESSOR_COUNT_MAX];
};

struct group_entry {
	mar_name_t group_name;
	mar_uint32_t change_count;
	mar_uint32_t member_count;
	mar_msg_queue_group_policy_t policy;
	struct queue_entry *next_queue;
	struct list_head group_list;
	struct list_head queue_head;
};

DECLARE_LIST_INIT(queue_list_head);
DECLARE_LIST_INIT(group_list_head);
DECLARE_LIST_INIT(track_list_head);
DECLARE_LIST_INIT(reply_list_head);

DECLARE_LIST_INIT(sync_queue_list_head);
DECLARE_LIST_INIT(sync_group_list_head);
DECLARE_LIST_INIT(sync_reply_list_head);

static struct corosync_api_v1 *api;

static mar_uint32_t global_queue_id = 0;
static mar_uint32_t global_sender_id = 0;
static mar_uint32_t global_queue_count = 0;
static mar_uint32_t global_group_count = 0;
static mar_uint32_t sync_queue_count = 0;
static mar_uint32_t sync_group_count = 0;

static void msg_exec_dump_fn (void);

static int msg_exec_init_fn (struct corosync_api_v1 *);
static int msg_lib_init_fn (void *conn);
static int msg_lib_exit_fn (void *conn);

static void message_handler_req_exec_msg_queueopen (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queueopenasync (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queueclose (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuestatusget (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queueretentiontimeset (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queueunlink (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegroupcreate (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegroupinsert (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegroupremove (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegroupdelete (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegrouptrack (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegrouptrackstop (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegroupnotificationfree (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagesend (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagesendasync (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messageget (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagedatafree (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagecancel (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagesendreceive (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagereply (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagereplyasync (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuecapacitythresholdsset (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuecapacitythresholdsget (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_metadatasizeget (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_limitget (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_sync_queue (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_sync_queue_message (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_sync_queue_refcount (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_sync_group (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_sync_group_member (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_sync_reply (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queue_timeout (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messageget_timeout (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_exec_msg_sendreceive_timeout (
	const void *msg,
	unsigned int nodeid);

static void message_handler_req_lib_msg_queueopen (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_queueopenasync (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_queueclose (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_queuestatusget (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_queueretentiontimeset (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_queueunlink (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_queuegroupcreate (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_queuegroupinsert (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_queuegroupremove (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_queuegroupdelete (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_queuegrouptrack (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_queuegrouptrackstop (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_queuegroupnotificationfree (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_messagesend (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_messagesendasync (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_messageget (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_messagedatafree (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_messagecancel (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_messagesendreceive (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_messagereply (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_messagereplyasync (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_queuecapacitythresholdsset (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_queuecapacitythresholdsget (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_metadatasizeget (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_limitget (
	void *conn,
	const void *msg);

static void exec_msg_queueopen_endian_convert (void *msg);
static void exec_msg_queueopenasync_endian_convert (void *msg);
static void exec_msg_queueclose_endian_convert (void *msg);
static void exec_msg_queuestatusget_endian_convert (void *msg);
static void exec_msg_queueretentiontimeset_endian_convert (void *msg);
static void exec_msg_queueunlink_endian_convert (void *msg);
static void exec_msg_queuegroupcreate_endian_convert (void *msg);
static void exec_msg_queuegroupinsert_endian_convert (void *msg);
static void exec_msg_queuegroupremove_endian_convert (void *msg);
static void exec_msg_queuegroupdelete_endian_convert (void *msg);
static void exec_msg_queuegrouptrack_endian_convert (void *msg);
static void exec_msg_queuegrouptrackstop_endian_convert (void *msg);
static void exec_msg_queuegroupnotificationfree_endian_convert (void *msg);
static void exec_msg_messagesend_endian_convert (void *msg);
static void exec_msg_messagesendasync_endian_convert (void *msg);
static void exec_msg_messageget_endian_convert (void *msg);
static void exec_msg_messagedatafree_endian_convert (void *msg);
static void exec_msg_messagecancel_endian_convert (void *msg);
static void exec_msg_messagesendreceive_endian_convert (void *msg);
static void exec_msg_messagereply_endian_convert (void *msg);
static void exec_msg_messagereplyasync_endian_convert (void *msg);
static void exec_msg_queuecapacitythresholdsset_endian_convert (void *msg);
static void exec_msg_queuecapacitythresholdsget_endian_convert (void *msg);
static void exec_msg_metadatasizeget_endian_convert (void *msg);
static void exec_msg_limitget_endian_convert (void *msg);
static void exec_msg_sync_queue_endian_convert (void *msg);
static void exec_msg_sync_queue_message_endian_convert (void *msg);
static void exec_msg_sync_queue_refcount_endian_convert (void *msg);
static void exec_msg_sync_group_endian_convert (void *msg);
static void exec_msg_sync_group_member_endian_convert (void *msg);
static void exec_msg_sync_reply_endian_convert (void *msg);
static void exec_msg_queue_timeout_endian_convert (void *msg);
static void exec_msg_messageget_timeout_endian_convert (void *msg);
static void exec_msg_sendreceive_timeout_endian_convert (void *msg);

static enum msg_sync_state msg_sync_state = MSG_SYNC_STATE_NOT_STARTED;
static enum msg_sync_iteration_state msg_sync_iteration_state;

static struct list_head *msg_sync_iteration_queue;
static struct list_head *msg_sync_iteration_queue_message;
static struct list_head *msg_sync_iteration_group;
static struct list_head *msg_sync_iteration_group_member;
static struct list_head *msg_sync_iteration_reply;

static void msg_sync_init (
	const unsigned int *member_list,
	size_t member_list_entries,
	const struct memb_ring_id *ring_id);

static int  msg_sync_process (void);
static void msg_sync_activate (void);
static void msg_sync_abort (void);

static void msg_sync_refcount_increment (
	struct queue_entry *queue, unsigned int nodeid);
static void msg_sync_refcount_decrement (
	struct queue_entry *queue, unsigned int nodeid);
static void msg_sync_refcount_calculate (
	struct queue_entry *queue);

static unsigned int msg_member_list[PROCESSOR_COUNT_MAX];
static unsigned int msg_member_list_entries = 0;
static unsigned int lowest_nodeid = 0;

static struct memb_ring_id saved_ring_id;

static int msg_find_member_nodeid (unsigned int nodeid);

static void msg_queue_timeout (void *data);
static void msg_messageget_timeout (void *data);
static void msg_sendreceive_timeout (void *data);

static void msg_queue_release (struct queue_entry *queue);
static void msg_group_release (struct group_entry *group);
static void msg_reply_release (struct reply_entry *reply);

static void msg_confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id);

struct msg_pd {
	struct list_head queue_list;
	struct list_head queue_cleanup_list;
};

struct corosync_lib_handler msg_lib_engine[] =
{
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queueopen,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queueopenasync,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queueclose,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queuestatusget,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queueretentiontimeset,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queueunlink,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queuegroupcreate,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queuegroupinsert,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queuegroupremove,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queuegroupdelete,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queuegrouptrack,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queuegrouptrackstop,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queuegroupnotificationfree,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_messagesend,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_messagesendasync,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_messageget,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_messagedatafree,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_messagecancel,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_messagesendreceive,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_messagereply,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_messagereplyasync,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queuecapacitythresholdsset,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queuecapacitythresholdsget,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_metadatasizeget,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_limitget,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
};

static struct corosync_exec_handler msg_exec_engine[] =
{
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queueopen,
		.exec_endian_convert_fn = exec_msg_queueopen_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queueopenasync,
		.exec_endian_convert_fn = exec_msg_queueopenasync_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queueclose,
		.exec_endian_convert_fn = exec_msg_queueclose_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queuestatusget,
		.exec_endian_convert_fn = exec_msg_queuestatusget_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queueretentiontimeset,
		.exec_endian_convert_fn = exec_msg_queueretentiontimeset_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queueunlink,
		.exec_endian_convert_fn = exec_msg_queueunlink_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queuegroupcreate,
		.exec_endian_convert_fn = exec_msg_queuegroupcreate_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queuegroupinsert,
		.exec_endian_convert_fn = exec_msg_queuegroupinsert_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queuegroupremove,
		.exec_endian_convert_fn = exec_msg_queuegroupremove_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queuegroupdelete,
		.exec_endian_convert_fn = exec_msg_queuegroupdelete_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queuegrouptrack,
		.exec_endian_convert_fn = exec_msg_queuegrouptrack_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queuegrouptrackstop,
		.exec_endian_convert_fn = exec_msg_queuegrouptrackstop_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queuegroupnotificationfree,
		.exec_endian_convert_fn = exec_msg_queuegroupnotificationfree_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_messagesend,
		.exec_endian_convert_fn = exec_msg_messagesend_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_messagesendasync,
		.exec_endian_convert_fn = exec_msg_messagesendasync_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_messageget,
		.exec_endian_convert_fn = exec_msg_messageget_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_messagedatafree,
		.exec_endian_convert_fn = exec_msg_messagedatafree_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_messagecancel,
		.exec_endian_convert_fn = exec_msg_messagecancel_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_messagesendreceive,
		.exec_endian_convert_fn = exec_msg_messagesendreceive_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_messagereply,
		.exec_endian_convert_fn = exec_msg_messagereply_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_messagereplyasync,
		.exec_endian_convert_fn = exec_msg_messagereplyasync_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queuecapacitythresholdsset,
		.exec_endian_convert_fn = exec_msg_queuecapacitythresholdsset_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queuecapacitythresholdsget,
		.exec_endian_convert_fn = exec_msg_queuecapacitythresholdsget_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_metadatasizeget,
		.exec_endian_convert_fn = exec_msg_metadatasizeget_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_limitget,
		.exec_endian_convert_fn = exec_msg_limitget_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_sync_queue,
		.exec_endian_convert_fn = exec_msg_sync_queue_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_sync_queue_message,
		.exec_endian_convert_fn = exec_msg_sync_queue_message_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_sync_queue_refcount,
		.exec_endian_convert_fn = exec_msg_sync_queue_refcount_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_sync_group,
		.exec_endian_convert_fn = exec_msg_sync_group_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_sync_group_member,
		.exec_endian_convert_fn = exec_msg_sync_group_member_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_sync_reply,
		.exec_endian_convert_fn = exec_msg_sync_reply_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queue_timeout,
		.exec_endian_convert_fn = exec_msg_queue_timeout_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_messageget_timeout,
		.exec_endian_convert_fn = exec_msg_messageget_timeout_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_sendreceive_timeout,
		.exec_endian_convert_fn = exec_msg_sendreceive_timeout_endian_convert
	},
};

struct corosync_service_engine msg_service_engine = {
	.name			= "openais message service B.03.01",
	.id			= MSG_SERVICE,
	.private_data_size	= sizeof (struct msg_pd),
	.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED,
	.lib_init_fn		= msg_lib_init_fn,
	.lib_exit_fn		= msg_lib_exit_fn,
	.lib_engine		= msg_lib_engine,
	.lib_engine_count	= sizeof (msg_lib_engine) / sizeof (struct corosync_lib_handler),
	.exec_init_fn		= msg_exec_init_fn,
	.exec_dump_fn		= msg_exec_dump_fn,
	.exec_engine		= msg_exec_engine,
	.exec_engine_count	= sizeof (msg_exec_engine)/ sizeof (struct corosync_exec_handler),
	.confchg_fn		= msg_confchg_fn,
	.sync_mode		= CS_SYNC_V2,
	.sync_init		= msg_sync_init,
	.sync_process		= msg_sync_process,
	.sync_activate		= msg_sync_activate,
	.sync_abort		= msg_sync_abort,
};

static struct corosync_service_engine *msg_get_engine_ver0 (void);

static struct corosync_service_engine_iface_ver0 msg_service_engine_iface = {
	.corosync_get_service_engine_ver0 = msg_get_engine_ver0
};

static struct lcr_iface openais_msg_ver0[1] = {
	{
		.name			= "openais_msg",
		.version		= 0,
		.versions_replace	= 0,
		.versions_replace_count	= 0,
		.dependencies		= 0,
		.dependency_count	= 0,
		.constructor		= NULL,
		.destructor		= NULL,
		.interfaces		= NULL,
	}
};

static struct lcr_comp msg_comp_ver0 = {
	.iface_count	= 1,
	.ifaces		= openais_msg_ver0
};

static struct corosync_service_engine *msg_get_engine_ver0 (void)
{
	return (&msg_service_engine);
}

#ifdef OPENAIS_SOLARIS
void corosync_lcr_component_register (void);

void corosync_lcr_component_register (void) {
#else
__attribute__ ((constructor)) static void corosync_lcr_component_register (void)
{
#endif
	lcr_interfaces_set (&openais_msg_ver0[0], &msg_service_engine_iface);
	lcr_component_register (&msg_comp_ver0);
}

struct req_exec_msg_queueopen {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
	mar_msg_queue_handle_t queue_handle __attribute__((aligned(8)));
	mar_uint8_t create_attrs_flag __attribute__((aligned(8)));
	mar_msg_queue_creation_attributes_t create_attrs __attribute__((aligned(8)));
	mar_msg_queue_open_flags_t open_flags __attribute__((aligned(8)));
	mar_time_t timeout __attribute__((aligned(8)));
};

struct req_exec_msg_queueopenasync {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
	mar_msg_queue_handle_t queue_handle __attribute__((aligned(8)));
	mar_uint8_t create_attrs_flag __attribute__((aligned(8)));
	mar_msg_queue_creation_attributes_t create_attrs __attribute__((aligned(8)));
	mar_msg_queue_open_flags_t open_flags __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
};

struct req_exec_msg_queueclose {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
	mar_uint32_t queue_id __attribute__((aligned(8)));
};

struct req_exec_msg_queuestatusget {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
};

struct req_exec_msg_queueretentiontimeset {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
	mar_uint32_t queue_id __attribute__((aligned(8)));
	mar_time_t retention_time __attribute__((aligned(8)));
};

struct req_exec_msg_queueunlink {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
};

struct req_exec_msg_queuegroupcreate {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t group_name __attribute__((aligned(8)));
	mar_msg_queue_group_policy_t policy __attribute__((aligned(8)));
};

struct req_exec_msg_queuegroupinsert {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t group_name __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
};

struct req_exec_msg_queuegroupremove {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t group_name __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
};

struct req_exec_msg_queuegroupdelete {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t group_name __attribute__((aligned(8)));
};

struct req_exec_msg_queuegrouptrack {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t group_name __attribute__((aligned(8)));
	mar_uint8_t buffer_flag __attribute__((aligned(8)));
};

struct req_exec_msg_queuegrouptrackstop {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t group_name __attribute__((aligned(8)));
};

struct req_exec_msg_queuegroupnotificationfree {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
};

struct req_exec_msg_messagesend {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t destination __attribute__((aligned(8)));
	mar_time_t timeout __attribute__((aligned(8)));
	mar_msg_message_t message __attribute__((aligned(8)));
};

struct req_exec_msg_messagesendasync {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t destination __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
	mar_msg_message_t message __attribute__((aligned(8)));
	mar_msg_ack_flags_t ack_flags __attribute__((aligned(8)));
};

struct req_exec_msg_messageget {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
	mar_uint32_t queue_id __attribute__((aligned(8)));
	mar_time_t timeout __attribute__((aligned(8)));
};

struct req_exec_msg_messagedatafree {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
};

struct req_exec_msg_messagecancel {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
	mar_uint32_t queue_id __attribute__((aligned(8)));
};

struct req_exec_msg_messagesendreceive {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t destination __attribute__((aligned(8)));
	mar_time_t timeout __attribute__((aligned(8)));
	mar_size_t reply_size __attribute__((aligned(8)));
	mar_msg_message_t message __attribute__((aligned(8)));
	mar_msg_sender_id_t sender_id __attribute__((aligned(8)));
};

struct req_exec_msg_messagereply {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_msg_message_t reply_message __attribute__((aligned(8)));
	mar_msg_sender_id_t sender_id __attribute__((aligned(8)));
	mar_time_t timeout __attribute__((aligned(8)));
};

struct req_exec_msg_messagereplyasync {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_msg_message_t reply_message __attribute__((aligned(8)));
	mar_msg_sender_id_t sender_id __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
	mar_msg_ack_flags_t ack_flags __attribute__((aligned(8)));
};

struct req_exec_msg_queuecapacitythresholdsset {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
	mar_uint32_t queue_id __attribute__((aligned(8)));
	mar_msg_queue_thresholds_t thresholds __attribute__((aligned(8)));
};

struct req_exec_msg_queuecapacitythresholdsget {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
	mar_uint32_t queue_id __attribute__((aligned(8)));
};

struct req_exec_msg_metadatasizeget {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
};

struct req_exec_msg_limitget {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_msg_limit_id_t limit_id __attribute__((aligned(8)));
};

struct req_exec_msg_sync_queue {
	coroipc_request_header_t header __attribute__((aligned(8)));
	struct memb_ring_id ring_id __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
	mar_uint32_t queue_id __attribute__((aligned(8)));
	mar_time_t close_time __attribute__((aligned(8)));
	mar_size_t capacity_available[SA_MSG_MESSAGE_LOWEST_PRIORITY+1] __attribute__((aligned(8)));
	mar_size_t capacity_reached[SA_MSG_MESSAGE_LOWEST_PRIORITY+1] __attribute__((aligned(8)));
	mar_uint8_t unlink_flag __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_msg_queue_handle_t queue_handle __attribute__((aligned(8)));
	mar_msg_queue_open_flags_t open_flags __attribute__((aligned(8)));
	mar_msg_queue_group_changes_t change_flag __attribute__((aligned(8)));
	mar_msg_queue_creation_attributes_t create_attrs __attribute__((aligned(8)));
};

struct req_exec_msg_sync_queue_message {
	coroipc_request_header_t header __attribute__((aligned(8)));
	struct memb_ring_id ring_id __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
	mar_uint32_t queue_id __attribute__((aligned(8)));
	mar_time_t send_time __attribute__((aligned(8)));
	mar_msg_message_t message __attribute__((aligned(8)));
	mar_msg_sender_id_t sender_id __attribute__((aligned(8)));
};

struct req_exec_msg_sync_queue_refcount {
	coroipc_request_header_t header __attribute__((aligned(8)));
	struct memb_ring_id ring_id __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
	mar_uint32_t queue_id __attribute__((aligned(8)));
	struct refcount_set refcount_set[PROCESSOR_COUNT_MAX] __attribute__((aligned(8)));
};

struct req_exec_msg_sync_group {
	coroipc_request_header_t header __attribute__((aligned(8)));
	struct memb_ring_id ring_id __attribute__((aligned(8)));
	mar_name_t group_name __attribute__((aligned(8)));
	mar_msg_queue_group_policy_t policy __attribute__((aligned(8)));
};

struct req_exec_msg_sync_group_member {
	coroipc_request_header_t header __attribute__((aligned(8)));
	struct memb_ring_id ring_id __attribute__((aligned(8)));
	mar_name_t group_name __attribute__((aligned(8)));
	mar_name_t queue_name __attribute__((aligned(8)));
	mar_uint32_t queue_id __attribute__((aligned(8)));
};

struct req_exec_msg_sync_reply {
	coroipc_request_header_t header __attribute__((aligned(8)));
	struct memb_ring_id ring_id __attribute__((aligned(8)));
	mar_msg_sender_id_t sender_id __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
};

struct req_exec_msg_queue_timeout {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8))); /* ? */
	mar_name_t queue_name __attribute__((aligned(8)));
};

struct req_exec_msg_messageget_timeout {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8))); /* ? */
	mar_name_t queue_name __attribute__((aligned(8)));
};

struct req_exec_msg_sendreceive_timeout {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8))); /* ? */
	mar_msg_sender_id_t sender_id __attribute__((aligned(8)));
};

static void exec_msg_queueopen_endian_convert (void *msg)
{
	struct req_exec_msg_queueopen *to_swab =
		(struct req_exec_msg_queueopen *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->queue_name);
	swab_mar_msg_queue_handle_t (&to_swab->queue_handle);
	swab_mar_uint8_t (&to_swab->create_attrs_flag);
	swab_mar_msg_queue_creation_attributes_t (&to_swab->create_attrs);
	swab_mar_queue_open_flags_t (&to_swab->open_flags);
	swab_mar_time_t (&to_swab->timeout);

	return;
}

static void exec_msg_queueopenasync_endian_convert (void *msg)
{

	struct req_exec_msg_queueopenasync *to_swab =
		(struct req_exec_msg_queueopenasync *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->queue_name);
	swab_mar_msg_queue_handle_t (&to_swab->queue_handle);
	swab_mar_uint8_t (&to_swab->create_attrs_flag);
	swab_mar_msg_queue_creation_attributes_t (&to_swab->create_attrs);
	swab_mar_queue_open_flags_t (&to_swab->open_flags);
	swab_mar_invocation_t (&to_swab->invocation);

	return;
}

static void exec_msg_queueclose_endian_convert (void *msg)
{
	struct req_exec_msg_queueclose *to_swab =
		(struct req_exec_msg_queueclose *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->queue_name);
	swab_mar_uint32_t (&to_swab->queue_id);

	return;
}

static void exec_msg_queuestatusget_endian_convert (void *msg)
{
	struct req_exec_msg_queuestatusget *to_swab =
		(struct req_exec_msg_queuestatusget *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->queue_name);

	return;
}

static void exec_msg_queueretentiontimeset_endian_convert (void *msg)
{
	struct req_exec_msg_queueretentiontimeset *to_swab =
		(struct req_exec_msg_queueretentiontimeset *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->queue_name);
	swab_mar_uint32_t (&to_swab->queue_id);
	swab_mar_time_t (&to_swab->retention_time);

	return;
}

static void exec_msg_queueunlink_endian_convert (void *msg)
{
	struct req_exec_msg_queueunlink *to_swab =
		(struct req_exec_msg_queueunlink *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->queue_name);

	return;
}

static void exec_msg_queuegroupcreate_endian_convert (void *msg)
{
	struct req_exec_msg_queuegroupcreate *to_swab =
		(struct req_exec_msg_queuegroupcreate *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->group_name);
	swab_mar_msg_queue_group_policy_t (&to_swab->policy);

	return;
}

static void exec_msg_queuegroupinsert_endian_convert (void *msg)
{
	struct req_exec_msg_queuegroupinsert *to_swab =
		(struct req_exec_msg_queuegroupinsert *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->group_name);
	swab_mar_name_t (&to_swab->queue_name);

	return;
}

static void exec_msg_queuegroupremove_endian_convert (void *msg)
{
	struct req_exec_msg_queuegroupremove *to_swab =
		(struct req_exec_msg_queuegroupremove *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->group_name);
	swab_mar_name_t (&to_swab->queue_name);

	return;
}

static void exec_msg_queuegroupdelete_endian_convert (void *msg)
{
	struct req_exec_msg_queuegroupdelete *to_swab =
		(struct req_exec_msg_queuegroupdelete *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->group_name);

	return;
}

static void exec_msg_queuegrouptrack_endian_convert (void *msg)
{
	struct req_exec_msg_queuegrouptrack *to_swab =
		(struct req_exec_msg_queuegrouptrack *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->group_name);
	swab_mar_uint8_t (&to_swab->buffer_flag);

	return;
}

static void exec_msg_queuegrouptrackstop_endian_convert (void *msg)
{
	struct req_exec_msg_queuegrouptrackstop *to_swab =
		(struct req_exec_msg_queuegrouptrackstop *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->group_name);

	return;
}

static void exec_msg_queuegroupnotificationfree_endian_convert (void *msg)
{
	struct req_exec_msg_queuegroupnotificationfree *to_swab =
		(struct req_exec_msg_queuegroupnotificationfree *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);

	return;
}

static void exec_msg_messagesend_endian_convert (void *msg)
{
	struct req_exec_msg_messagesend *to_swab =
		(struct req_exec_msg_messagesend *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->destination);
	swab_mar_time_t (&to_swab->timeout);
	swab_mar_msg_message_t (&to_swab->message);

	return;
}

static void exec_msg_messagesendasync_endian_convert (void *msg)
{
	struct req_exec_msg_messagesendasync *to_swab =
		(struct req_exec_msg_messagesendasync *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->destination);
	swab_mar_invocation_t (&to_swab->invocation);
	swab_mar_msg_message_t (&to_swab->message);

	return;
}

static void exec_msg_messageget_endian_convert (void *msg)
{
	struct req_exec_msg_messageget *to_swab =
		(struct req_exec_msg_messageget *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->queue_name);
	swab_mar_uint32_t (&to_swab->queue_id);
	swab_mar_time_t (&to_swab->timeout);

	return;
}

static void exec_msg_messagedatafree_endian_convert (void *msg)
{
	struct req_exec_msg_messagedatafree *to_swab =
		(struct req_exec_msg_messagedatafree *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);

	return;
}

static void exec_msg_messagecancel_endian_convert (void *msg)
{
	struct req_exec_msg_messagecancel *to_swab =
		(struct req_exec_msg_messagecancel *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->queue_name);
	swab_mar_uint32_t (&to_swab->queue_id);

	return;
}

static void exec_msg_messagesendreceive_endian_convert (void *msg)
{
	struct req_exec_msg_messagesendreceive *to_swab =
		(struct req_exec_msg_messagesendreceive *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->destination);
	swab_mar_time_t (&to_swab->timeout);
	swab_mar_size_t (&to_swab->reply_size);
	swab_mar_msg_message_t (&to_swab->message);
	swab_mar_msg_sender_id_t (&to_swab->sender_id);

	return;
}

static void exec_msg_messagereply_endian_convert (void *msg)
{
	struct req_exec_msg_messagereply *to_swab =
		(struct req_exec_msg_messagereply *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_msg_message_t (&to_swab->reply_message);
	swab_mar_msg_sender_id_t (&to_swab->sender_id);
	swab_mar_time_t (&to_swab->timeout);

	return;
}

static void exec_msg_messagereplyasync_endian_convert (void *msg)
{
	struct req_exec_msg_messagereplyasync *to_swab =
		(struct req_exec_msg_messagereplyasync *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_msg_message_t (&to_swab->reply_message);
	swab_mar_msg_sender_id_t (&to_swab->sender_id);
	swab_mar_invocation_t (&to_swab->invocation);

	return;
}

static void exec_msg_queuecapacitythresholdsset_endian_convert (void *msg)
{
	struct req_exec_msg_queuecapacitythresholdsset *to_swab =
		(struct req_exec_msg_queuecapacitythresholdsset *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->queue_name);
	swab_mar_uint32_t (&to_swab->queue_id);
	swab_mar_msg_queue_thresholds_t (&to_swab->thresholds);

	return;
}

static void exec_msg_queuecapacitythresholdsget_endian_convert (void *msg)
{
	struct req_exec_msg_queuecapacitythresholdsget *to_swab =
		(struct req_exec_msg_queuecapacitythresholdsget *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->queue_name);
	swab_mar_uint32_t (&to_swab->queue_id);

	return;
}

static void exec_msg_metadatasizeget_endian_convert (void *msg)
{
	struct req_exec_msg_metadatasizeget *to_swab =
		(struct req_exec_msg_metadatasizeget *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);

	return;
}

static void exec_msg_limitget_endian_convert (void *msg)
{
	struct req_exec_msg_limitget *to_swab =
		(struct req_exec_msg_limitget *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_msg_limit_id_t (&to_swab->limit_id);

	return;
}

static void exec_msg_sync_queue_endian_convert (void *msg)
{
/* 	struct req_exec_msg_sync_queue *to_swab = */
/* 		(struct req_exec_msg_sync_queue *)msg; */

	return;
}

static void exec_msg_sync_queue_message_endian_convert (void *msg)
{
/* 	struct req_exec_msg_sync_queue_message *to_swab = */
/* 		(struct req_exec_msg_sync_queue_message *)msg; */

	return;
}

static void exec_msg_sync_queue_refcount_endian_convert (void *msg)
{
/* 	struct req_exec_msg_sync_queue_refcount *to_swab = */
/* 		(struct req_exec_msg_sync_queue_refcount *)msg; */

	return;
}

static void exec_msg_sync_group_endian_convert (void *msg)
{
/* 	struct req_exec_msg_sync_group *to_swab = */
/* 		(struct req_exec_msg_sync_group *)msg; */

	return;
}

static void exec_msg_sync_group_member_endian_convert (void *msg)
{
/* 	struct req_exec_msg_sync_group_member *to_swab = */
/* 		(struct req_exec_msg_sync_group_member *)msg; */

	return;
}

static void exec_msg_sync_reply_endian_convert (void *msg)
{
/* 	struct req_exec_msg_sync_reply *to_swab = */
/* 		(struct req_exec_msg_sync_reply *)msg; */

	return;
}

static void exec_msg_queue_timeout_endian_convert (void *msg)
{
	struct req_exec_msg_queue_timeout *to_swab =
		(struct req_exec_msg_queue_timeout *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->queue_name);

	return;
}

static void exec_msg_messageget_timeout_endian_convert (void *msg)
{
	struct req_exec_msg_messageget_timeout *to_swab =
		(struct req_exec_msg_messageget_timeout *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->queue_name);

	return;
}

static void exec_msg_sendreceive_timeout_endian_convert (void *msg)
{
	struct req_exec_msg_sendreceive_timeout *to_swab =
		(struct req_exec_msg_sendreceive_timeout *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_msg_sender_id_t (&to_swab->sender_id);

	return;
}

static void msg_queue_list_print (
	struct list_head *queue_head)
{
	struct list_head *queue_list;
	struct queue_entry *queue;

	struct list_head *message_list;
	struct message_entry *message;

	struct list_head *pending_list;
	struct pending_entry *pending;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_queue_list_print\n");

	for (queue_list = queue_head->next;
	     queue_list != queue_head;
	     queue_list = queue_list->next)
	{
		queue = list_entry (queue_list, struct queue_entry, queue_list);

		log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s (id=%u)\n",
			    (char *)(queue->queue_name.value),
			    (unsigned int)(queue->queue_id));

		for (message_list = queue->message_head.next;
		     message_list != &queue->message_head;
		     message_list = message_list->next)
		{
			message = list_entry (message_list, struct message_entry, queue_list);

			log_printf (LOGSYS_LEVEL_DEBUG, "\t\t message=%s\n",
				    (char *)(message->message.data));
		}

		for (pending_list = queue->pending_head.next;
		     pending_list != &queue->pending_head;
		     pending_list = pending_list->next)
		{
			pending = list_entry (pending_list, struct pending_entry, pending_list);

			log_printf (LOGSYS_LEVEL_DEBUG, "\t\t pending { nodeid=%x conn=%p }\n",
				    (unsigned int)(pending->source.nodeid),
				    (void *)(pending->source.conn));
		}
	}
}

static void msg_group_list_print (
	struct list_head *group_head)
{
	struct list_head *group_list;
	struct group_entry *group;

	struct list_head *queue_list;
	struct queue_entry *queue;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_group_list_print\n");

	for (group_list = group_head->next;
	     group_list != group_head;
	     group_list = group_list->next)
	{
		group = list_entry (group_list, struct group_entry, group_list);

		log_printf (LOGSYS_LEVEL_DEBUG, "\t group=%s\n",
			    (char *)(group->group_name.value));

		for (queue_list = group->queue_head.next;
		     queue_list != &group->queue_head;
		     queue_list = queue_list->next)
		{
			queue = list_entry (queue_list, struct queue_entry, group_list);

			log_printf (LOGSYS_LEVEL_DEBUG, "\t\t queue=%s (id=%u)\n",
				    (char *)(queue->queue_name.value),
				    (unsigned int)(queue->queue_id));
		}
	}
}

static void msg_reply_list_print (
	struct list_head *reply_head)
{
	struct list_head *reply_list;
	struct reply_entry *reply;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_reply_list_print\n");

	for (reply_list = reply_head->next;
	     reply_list != reply_head;
	     reply_list = reply_list->next)
	{
		reply = list_entry (reply_list, struct reply_entry, reply_list);

		log_printf (LOGSYS_LEVEL_DEBUG, "\t sender_id=%llx\n",
			    (unsigned long long)(reply->sender_id));
	}
}

static int msg_find_member_nodeid (
	unsigned int nodeid)
{
	unsigned int i;

	for (i = 0; i < msg_member_list_entries; i++) {
		if (nodeid == msg_member_list[i]) {
			return (1);
		}
	}
	return (0);
}

void msg_sync_refcount_increment (
	struct queue_entry *queue,
	unsigned int nodeid)
{
	unsigned int i;

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		if (queue->refcount_set[i].nodeid == 0) {
			queue->refcount_set[i].nodeid = nodeid;
			queue->refcount_set[i].refcount = 1;
			break;
		}
		if (queue->refcount_set[i].nodeid == nodeid) {
			queue->refcount_set[i].refcount += 1;
			break;
		}
	}
}

void msg_sync_refcount_decrement (
	struct queue_entry *queue,
	unsigned int nodeid)
{
	unsigned int i;

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		if (queue->refcount_set[i].nodeid == 0) {
			break;
		}
		if (queue->refcount_set[i].nodeid == nodeid) {
			queue->refcount_set[i].refcount -= 1;
			break;
		}
	}
}

void msg_sync_refcount_calculate (
	struct queue_entry *queue)
{
	unsigned int i;

	queue->refcount = 0;

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		if (queue->refcount_set[i].nodeid == 0) {
			break;
		}
		queue->refcount += queue->refcount_set[i].refcount;
	}
}

/* ! */

static unsigned int msg_group_track_current (
	struct group_entry *group,
	mar_msg_queue_group_notification_t *notification)
{
	struct queue_entry *queue;
	struct list_head *queue_list;
	unsigned int i = 0;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_group_track_current\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t group=%s\n",
		    (char *)(group->group_name.value));

	for (queue_list = group->queue_head.next;
	     queue_list != &group->queue_head;
	     queue_list = queue_list->next)
	{
		queue = list_entry (queue_list, struct queue_entry, group_list);

		memcpy (&notification[i].member.queue_name,
			&queue->queue_name,
			sizeof (mar_name_t));
		notification[i].change = queue->change_flag;
		i += 1;
	}
	return (i);
}

static unsigned int msg_group_track_changes (
	struct group_entry *group,
	mar_msg_queue_group_notification_t *notification)
{
	struct queue_entry *queue;
	struct list_head *queue_list;
	unsigned int i = 0;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_group_track_changes\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t group=%s\n",
		    (char *)(group->group_name.value));

	for (queue_list = group->queue_head.next;
	     queue_list != &group->queue_head;
	     queue_list = queue_list->next)
	{
		queue = list_entry (queue_list, struct queue_entry, group_list);

		memcpy (&notification[i].member.queue_name,
			&queue->queue_name,
			sizeof (mar_name_t));
		notification[i].change = queue->change_flag;
		i += 1;
	}
	return (i);
}

static unsigned int msg_group_track_changes_only (
	struct group_entry *group,
	mar_msg_queue_group_notification_t *notification)
{
	struct queue_entry *queue;
	struct list_head *queue_list;
	unsigned int i = 0;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_group_track_changes_only\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t group=%s\n",
		    (char *)(group->group_name.value));

	for (queue_list = group->queue_head.next;
	     queue_list != &group->queue_head;
	     queue_list = queue_list->next)
	{
		queue = list_entry (queue_list, struct queue_entry, group_list);

		if (queue->change_flag != SA_MSG_QUEUE_GROUP_NO_CHANGE)	{
			memcpy (&notification[i].member.queue_name,
				&queue->queue_name,
				sizeof (mar_name_t));
			notification[i].change = queue->change_flag;
			i += 1;
		}
	}
	return (i);
}

static void msg_queue_close (
	const mar_name_t *queue_name,
	const mar_uint32_t queue_id)
{
	struct req_exec_msg_queueclose req_exec_msg_queueclose;
	struct iovec iov;

	req_exec_msg_queueclose.header.size =
		sizeof (struct req_exec_msg_queueclose);
	req_exec_msg_queueclose.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUECLOSE);

	memset (&req_exec_msg_queueclose.source, 0,
		sizeof (mar_message_source_t));

	req_exec_msg_queueclose.queue_id = queue_id;

	memcpy (&req_exec_msg_queueclose.queue_name,
		queue_name, sizeof (mar_name_t));

	iov.iov_base = (void *)&req_exec_msg_queueclose;
	iov.iov_len = sizeof (struct req_exec_msg_queueclose);

	assert (api->totem_mcast (&iov, 1, TOTEM_AGREED) == 0);
}

static void msg_queue_timer_restart (
	struct list_head *queue_head)
{
	struct queue_entry *queue;
	struct list_head *queue_list;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_queue_timer_restart\n");

	for (queue_list = queue_head->next;
	     queue_list != queue_head;
	     queue_list = queue_list->next)
	{
		queue = list_entry (queue_list, struct queue_entry, queue_list);

		if ((lowest_nodeid == api->totem_nodeid_get()) &&
		    (queue->create_attrs.creation_flags != SA_MSG_QUEUE_PERSISTENT) &&
		    (queue->refcount == 0))
		{
			api->timer_add_absolute (
				(queue->create_attrs.retention_time + queue->close_time),
				(void *)(queue), msg_queue_timeout, &queue->timer_handle);
		}
	}
}

static void msg_confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id)
{
	unsigned int i;

	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_confchg_fn\n");

	memcpy (&saved_ring_id, ring_id,
		sizeof (struct memb_ring_id));

	if (configuration_type != TOTEM_CONFIGURATION_REGULAR) {
		return;
	}
	if (msg_sync_state != MSG_SYNC_STATE_NOT_STARTED) {
		return;
	}

	msg_sync_state = MSG_SYNC_STATE_STARTED;

	lowest_nodeid =  0xffffffff;

	for (i = 0; i < member_list_entries; i++) {
		if (lowest_nodeid > member_list[i]) {
			lowest_nodeid = member_list[i];
		}
	}

	memcpy (msg_member_list, member_list,
		sizeof (unsigned int) * member_list_entries);

	msg_member_list_entries = member_list_entries;

	return;
}

static int msg_sync_queue_transmit (
	struct queue_entry *queue)
{
	struct req_exec_msg_sync_queue req_exec_msg_sync_queue;
	struct iovec iov;

	int i;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_sync_queue_transmit { queue=%s id=%u }\n",
		    (char *)(queue->queue_name.value),
		    (unsigned int)(queue->queue_id));

	memset (&req_exec_msg_sync_queue, 0,
		sizeof (struct req_exec_msg_sync_queue));

	req_exec_msg_sync_queue.header.size =
		sizeof (struct req_exec_msg_sync_queue);
	req_exec_msg_sync_queue.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_SYNC_QUEUE);

	memcpy (&req_exec_msg_sync_queue.ring_id,
		&saved_ring_id, sizeof (struct memb_ring_id));
	memcpy (&req_exec_msg_sync_queue.queue_name,
		&queue->queue_name, sizeof (mar_name_t));
	memcpy (&req_exec_msg_sync_queue.source,
		&queue->source, sizeof (mar_message_source_t));
	memcpy (&req_exec_msg_sync_queue.create_attrs,
		&queue->create_attrs, sizeof (mar_msg_queue_creation_attributes_t));

	req_exec_msg_sync_queue.queue_id = queue->queue_id;
	req_exec_msg_sync_queue.close_time = queue->close_time;
	req_exec_msg_sync_queue.unlink_flag = queue->unlink_flag;
	req_exec_msg_sync_queue.open_flags = queue->open_flags;
	req_exec_msg_sync_queue.change_flag = queue->change_flag;
	req_exec_msg_sync_queue.queue_handle = queue->queue_handle;

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
		req_exec_msg_sync_queue.capacity_available[i] =	queue->priority[i].capacity_available;
		req_exec_msg_sync_queue.capacity_reached[i] = queue->priority[i].capacity_reached;
	}

	iov.iov_base = (void *)&req_exec_msg_sync_queue;
	iov.iov_len = sizeof (struct req_exec_msg_sync_queue);

	return (api->totem_mcast (&iov, 1, TOTEM_AGREED));
}

static int msg_sync_queue_message_transmit (
	struct queue_entry *queue,
	struct message_entry *message)
{
	struct req_exec_msg_sync_queue_message req_exec_msg_sync_queue_message;
	struct iovec iov[2];

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_sync_queue_message_transmit { queue=%s id=%u}\n",
		    (char *)(queue->queue_name.value),
		    (unsigned int)(queue->queue_id));

	req_exec_msg_sync_queue_message.header.size =
		sizeof (struct req_exec_msg_sync_queue_message);
	req_exec_msg_sync_queue_message.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_SYNC_QUEUE_MESSAGE);

	memcpy (&req_exec_msg_sync_queue_message.ring_id,
		&saved_ring_id, sizeof (struct memb_ring_id));
	memcpy (&req_exec_msg_sync_queue_message.queue_name,
		&queue->queue_name, sizeof (mar_name_t));
	memcpy (&req_exec_msg_sync_queue_message.message,
		&message->message, sizeof (mar_msg_message_t));

	req_exec_msg_sync_queue_message.queue_id = queue->queue_id;
	req_exec_msg_sync_queue_message.send_time = message->send_time;
	req_exec_msg_sync_queue_message.sender_id = message->sender_id;

	iov[0].iov_base = (void *)&req_exec_msg_sync_queue_message;
	iov[0].iov_len = sizeof (struct req_exec_msg_sync_queue_message);

	iov[1].iov_base = (void *)message->message.data;
	iov[1].iov_len = message->message.size;

	return (api->totem_mcast (iov, 2, TOTEM_AGREED));
}

static int msg_sync_queue_refcount_transmit (
	struct queue_entry *queue)
{
	struct req_exec_msg_sync_queue_refcount req_exec_msg_sync_queue_refcount;
	struct iovec iov;

	int i;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_sync_queue_refcount_transmit { queue=%s id=%u }\n",
		    (char *)(queue->queue_name.value),
		    (unsigned int)(queue->queue_id));

	memset (&req_exec_msg_sync_queue_refcount, 0,
		sizeof (struct req_exec_msg_sync_queue_refcount));

	req_exec_msg_sync_queue_refcount.header.size =
		sizeof (struct req_exec_msg_sync_queue_refcount);
	req_exec_msg_sync_queue_refcount.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_SYNC_QUEUE_REFCOUNT);

	memcpy (&req_exec_msg_sync_queue_refcount.ring_id,
		&saved_ring_id, sizeof (struct memb_ring_id));
	memcpy (&req_exec_msg_sync_queue_refcount.queue_name,
		&queue->queue_name, sizeof (mar_name_t));

	req_exec_msg_sync_queue_refcount.queue_id = queue->queue_id;

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		req_exec_msg_sync_queue_refcount.refcount_set[i].refcount = queue->refcount_set[i].refcount;
		req_exec_msg_sync_queue_refcount.refcount_set[i].nodeid = queue->refcount_set[i].nodeid;
	}

	iov.iov_base = (void *)&req_exec_msg_sync_queue_refcount;
	iov.iov_len = sizeof (struct req_exec_msg_sync_queue_refcount);

	return (api->totem_mcast (&iov, 1, TOTEM_AGREED));
}

static int msg_sync_group_transmit (
	struct group_entry *group)
{
	struct req_exec_msg_sync_group req_exec_msg_sync_group;
	struct iovec iov;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_sync_group_transmit { group=%s }\n",
		    (char *)(group->group_name.value));

	memset (&req_exec_msg_sync_group, 0,
		sizeof (struct req_exec_msg_sync_group));

	req_exec_msg_sync_group.header.size =
		sizeof (struct req_exec_msg_sync_group);
	req_exec_msg_sync_group.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_SYNC_GROUP);

	memcpy (&req_exec_msg_sync_group.ring_id,
		&saved_ring_id, sizeof (struct memb_ring_id));
	memcpy (&req_exec_msg_sync_group.group_name,
		&group->group_name, sizeof (mar_name_t));

	req_exec_msg_sync_group.policy = group->policy;

	iov.iov_base = (void *)&req_exec_msg_sync_group;
	iov.iov_len = sizeof (struct req_exec_msg_sync_group);

	return (api->totem_mcast (&iov, 1, TOTEM_AGREED));
}

static int msg_sync_group_member_transmit (
	struct group_entry *group,
	struct queue_entry *queue)
{
	struct req_exec_msg_sync_group_member req_exec_msg_sync_group_member;
	struct iovec iov;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_sync_group_member_transmit { group=%s queue=%s }\n",
		    (char *)(group->group_name.value),
		    (char *)(queue->queue_name.value));

	memset (&req_exec_msg_sync_group_member, 0,
		sizeof (struct req_exec_msg_sync_group_member));

	req_exec_msg_sync_group_member.header.size =
		sizeof (struct req_exec_msg_sync_group_member);
	req_exec_msg_sync_group_member.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_SYNC_GROUP_MEMBER);

	memcpy (&req_exec_msg_sync_group_member.ring_id,
		&saved_ring_id, sizeof (struct memb_ring_id));
	memcpy (&req_exec_msg_sync_group_member.group_name,
		&group->group_name, sizeof (mar_name_t));
	memcpy (&req_exec_msg_sync_group_member.queue_name,
		&queue->queue_name, sizeof (mar_name_t));

	req_exec_msg_sync_group_member.queue_id = queue->queue_id;

	iov.iov_base = (void *)&req_exec_msg_sync_group_member;
	iov.iov_len = sizeof (struct req_exec_msg_sync_group_member);

	return (api->totem_mcast (&iov, 1, TOTEM_AGREED));
}

static int msg_sync_reply_transmit (
	struct reply_entry *reply)
{
	struct req_exec_msg_sync_reply req_exec_msg_sync_reply;
	struct iovec iov;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_sync_reply_transmit\n");

	memset (&req_exec_msg_sync_reply, 0,
		sizeof (struct req_exec_msg_sync_reply));

	req_exec_msg_sync_reply.header.size =
		sizeof (struct req_exec_msg_sync_reply);
	req_exec_msg_sync_reply.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_SYNC_REPLY);

	memcpy (&req_exec_msg_sync_reply.ring_id,
		&saved_ring_id, sizeof (struct memb_ring_id));
	memcpy (&req_exec_msg_sync_reply.source,
		&reply->source, sizeof (mar_message_source_t));

	req_exec_msg_sync_reply.sender_id = reply->sender_id;

	iov.iov_base = (void *)&req_exec_msg_sync_reply;
	iov.iov_len = sizeof (struct req_exec_msg_sync_reply);

	return (api->totem_mcast (&iov, 1, TOTEM_AGREED));
}

static int msg_sync_queue_iterate (void)
{
	struct queue_entry *queue;
	struct list_head *queue_list;

	struct message_entry *message;
	struct list_head *message_list;

	int result;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_sync_queue_iterate\n");

	for (queue_list = msg_sync_iteration_queue;
	     queue_list != &queue_list_head;
	     queue_list = queue_list->next)
	{
		queue = list_entry (queue_list, struct queue_entry, queue_list);

		/*
		 * If this queue has an active retention timer,
		 * delete it immediately. When synchronization
		 * is complete, we will recreate the retention
		 * timers as needed.
		 */
		if (queue->timer_handle != 0) {
			api->timer_delete (queue->timer_handle);
		}

		if (msg_sync_iteration_state == MSG_SYNC_ITERATION_STATE_QUEUE)
		{
			result = msg_sync_queue_transmit (queue);
			if (result != 0) {
				return (-1);
			}
			msg_sync_iteration_state = MSG_SYNC_ITERATION_STATE_QUEUE_REFCOUNT;
		}

		if (msg_sync_iteration_state == MSG_SYNC_ITERATION_STATE_QUEUE_REFCOUNT)
		{
			result = msg_sync_queue_refcount_transmit (queue);
			if (result != 0) {
				return (-1);
			}
			msg_sync_iteration_queue_message = queue->message_head.next;
			msg_sync_iteration_state = MSG_SYNC_ITERATION_STATE_QUEUE_MESSAGE;
		}

		if (msg_sync_iteration_state == MSG_SYNC_ITERATION_STATE_QUEUE_MESSAGE)
		{
			for (message_list = msg_sync_iteration_queue_message;
			     message_list != &queue->message_head;
			     message_list = message_list->next)
			{
				message = list_entry (message_list, struct message_entry, queue_list);

				result = msg_sync_queue_message_transmit (queue, message);
				if (result != 0) {
					return (-1);
				}
				msg_sync_iteration_queue_message = message_list->next;
			}
		}

		msg_sync_iteration_state = MSG_SYNC_ITERATION_STATE_QUEUE;
		msg_sync_iteration_queue = queue_list->next;
	}

	return (0);
}

static int msg_sync_group_iterate (void)
{
	struct group_entry *group;
	struct list_head *group_list;

	struct queue_entry *queue;
	struct list_head *queue_list;

	int result;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_sync_group_iterate\n");

	for (group_list = msg_sync_iteration_group;
	     group_list != &group_list_head;
	     group_list = group_list->next)
	{
		group = list_entry (group_list, struct group_entry, group_list);

		if (msg_sync_iteration_state == MSG_SYNC_ITERATION_STATE_GROUP)
		{
			result = msg_sync_group_transmit (group);
			if (result != 0) {
				return (-1);
			}
			msg_sync_iteration_group_member = group->queue_head.next;
			msg_sync_iteration_state = MSG_SYNC_ITERATION_STATE_GROUP_MEMBER;
		}

		if (msg_sync_iteration_state == MSG_SYNC_ITERATION_STATE_GROUP_MEMBER)
		{
			for (queue_list = msg_sync_iteration_group_member;
			     queue_list != &group->queue_head;
			     queue_list = queue_list->next)
			{
				queue = list_entry (queue_list, struct queue_entry, group_list);

				result = msg_sync_group_member_transmit (group, queue);
				if (result != 0) {
					return (-1);
				}
				msg_sync_iteration_group_member = queue_list->next;
			}
		}

		msg_sync_iteration_state = MSG_SYNC_ITERATION_STATE_GROUP;
		msg_sync_iteration_group = group_list->next;
	}

	return (0);
}

static int msg_sync_reply_iterate (void)
{
	struct reply_entry *reply;
	struct list_head *reply_list;

	int result;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_sync_reply_iterate\n");

	for (reply_list = msg_sync_iteration_reply;
	     reply_list != &reply_list_head;
	     reply_list = reply_list->next)
	{
		reply = list_entry (reply_list, struct reply_entry, reply_list);

		if (msg_sync_iteration_state == MSG_SYNC_ITERATION_STATE_REPLY)
		{
			if (msg_find_member_nodeid (reply->sender_id >> 32)) {
				result = msg_sync_reply_transmit (reply);
				if (result != 0) {
					return (-1);
				}
			}
		}

		msg_sync_iteration_state = MSG_SYNC_ITERATION_STATE_REPLY;
		msg_sync_iteration_reply = reply_list->next;
	}

	return (0);
}

static void msg_sync_queue_enter (void)
{
	struct queue_entry *queue;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_sync_queue_enter\n");

	queue = list_entry (queue_list_head.next, struct queue_entry, queue_list);

	msg_sync_state = MSG_SYNC_STATE_QUEUE;
	msg_sync_iteration_state = MSG_SYNC_ITERATION_STATE_QUEUE;

	msg_sync_iteration_queue = queue_list_head.next;
	msg_sync_iteration_queue_message = queue->message_head.next;

	sync_queue_count = 0;
}

static void msg_sync_group_enter (void)
{
	struct group_entry *group;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_sync_group_enter\n");

	group = list_entry (group_list_head.next, struct group_entry, group_list);

	msg_sync_state = MSG_SYNC_STATE_GROUP;
	msg_sync_iteration_state = MSG_SYNC_ITERATION_STATE_GROUP;

	msg_sync_iteration_group = group_list_head.next;
	msg_sync_iteration_queue = group->queue_head.next;

	sync_group_count = 0;
}

static void msg_sync_reply_enter (void)
{
	struct reply_entry *reply;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_sync_reply_enter\n");

	reply = list_entry (reply_list_head.next, struct reply_entry, reply_list);

	msg_sync_state = MSG_SYNC_STATE_REPLY;
	msg_sync_iteration_state = MSG_SYNC_ITERATION_STATE_REPLY;

	msg_sync_iteration_reply = reply_list_head.next;
	/* ? */
}

static inline void msg_sync_queue_free (
	struct list_head *queue_head)
{
	struct queue_entry *queue;
	struct list_head *queue_list;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_sync_queue_free\n");

	queue_list = queue_head->next;

	while (queue_list != queue_head) {
		queue = list_entry (queue_list, struct queue_entry, queue_list);
		queue_list = queue_list->next;

		msg_queue_release (queue);
	}

	list_init (queue_head);	/* ? */
}

static inline void msg_sync_group_free (
	struct list_head *group_head)
{
	struct group_entry *group;
	struct list_head *group_list;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_sync_group_free\n");

	group_list = group_head->next;

	while (group_list != group_head) {
		group = list_entry (group_list, struct group_entry, group_list);
		group_list = group_list->next;

		msg_group_release (group);
	}

	list_init (group_head);	/* ? */
}

static inline void msg_sync_reply_free (
	struct list_head *reply_head)
{
	struct reply_entry *reply;
	struct list_head *reply_list;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_sync_reply_free\n");

	reply_list = reply_head->next;

	while (reply_list != reply_head) {
		reply = list_entry (reply_list, struct reply_entry, reply_list);
		reply_list = reply_list->next;

		msg_reply_release (reply);
	}

	list_init (reply_head);	/* ? */
}

static void msg_sync_init (
	const unsigned int *member_list,
	size_t member_list_entries,
	const struct memb_ring_id *ring_id)
{
	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_sync_init\n");

	msg_sync_queue_enter();

	return;
}

static int msg_sync_process (void)
{
	int continue_process = 0;
	int iterate_result;
	int iterate_finish;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_sync_process\n");

	switch (msg_sync_state)
	{
	case MSG_SYNC_STATE_QUEUE:
		iterate_finish = 1;
		continue_process = 1;

		if (lowest_nodeid == api->totem_nodeid_get()) {
			TRACE1 ("transmit queue list because lowest member in old configuration.\n");

			iterate_result = msg_sync_queue_iterate ();
			if (iterate_result != 0) {
				iterate_finish = 0;
			}
		}

		if (iterate_finish == 1) {
			msg_sync_group_enter ();
		}

		break;

	case MSG_SYNC_STATE_GROUP:
		iterate_finish = 1;
		continue_process = 1;

		if (lowest_nodeid == api->totem_nodeid_get()) {
			TRACE1 ("transmit group list because lowest member in old configuration.\n");

			iterate_result = msg_sync_group_iterate ();
			if (iterate_result != 0) {
				iterate_finish = 0;
			}
		}

		if (iterate_finish == 1) {
			msg_sync_reply_enter ();
		}

		break;

	case MSG_SYNC_STATE_REPLY:
		iterate_finish = 1;
		continue_process = 1;

		if (lowest_nodeid == api->totem_nodeid_get()) {
			TRACE1 ("transmit reply list because lowest member in old configuration.\n");

			iterate_result = msg_sync_reply_iterate ();
			if (iterate_result != 0) {
				iterate_finish = 0;
			}
		}

		if (iterate_finish == 1) {
			continue_process = 0;
		}

		break;

	default:
		assert (0);
	}

	return (continue_process);
}

static void msg_sync_activate (void)
{
	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_sync_activate\n");

	msg_sync_queue_free (&queue_list_head);
	msg_sync_group_free (&group_list_head);
	msg_sync_reply_free (&reply_list_head);

	if (!list_empty (&sync_queue_list_head)) {
		list_splice (&sync_queue_list_head, &queue_list_head);
	}

	if (!list_empty (&sync_group_list_head)) {
		list_splice (&sync_group_list_head, &group_list_head);
	}

	if (!list_empty (&sync_reply_list_head)) {
		list_splice (&sync_reply_list_head, &reply_list_head);
	}

	list_init (&sync_queue_list_head);
	list_init (&sync_group_list_head);
	list_init (&sync_reply_list_head);

	/*
	 * Now that synchronization is complete, we must
	 * iterate over the list of queues and determine
	 * if any retention timers need to be restarted.
	 */
	msg_queue_timer_restart (&queue_list_head);

	global_queue_count = sync_queue_count;
	global_group_count = sync_group_count;

	msg_sync_state = MSG_SYNC_STATE_NOT_STARTED;

	return;
}

static void msg_sync_abort (void)
{
	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_sync_abort\n");

	return;
}

static void msg_exec_dump_fn (void)
{
	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_exec_dump_fn\n");

	return;
}

static int msg_exec_init_fn (struct corosync_api_v1 *corosync_api)
{
#ifdef OPENAIS_SOLARIS
	logsys_subsys_init();
#endif
	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_exec_init_fn\n");

	api = corosync_api;

	return (0);
}

static int msg_lib_init_fn (void *conn)
{
	struct msg_pd *msg_pd = (struct msg_pd *)(api->ipc_private_data_get(conn));

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_lib_init_fn\n");

	list_init (&msg_pd->queue_list);
	list_init (&msg_pd->queue_cleanup_list);

	return (0);
}

static int msg_lib_exit_fn (void *conn)
{
	struct cleanup_entry *cleanup;
	struct list_head *cleanup_list;

	struct track_entry *track;
	struct list_head *track_list;

	struct msg_pd *msg_pd = (struct msg_pd *)(api->ipc_private_data_get(conn));

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_lib_exit_fn\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: global_queue_count=%u\n",
		    (unsigned int)(global_queue_count));
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: global_group_count=%u\n",
		    (unsigned int)(global_group_count));

	/* DEBUG */
	msg_group_list_print (&group_list_head);
	msg_queue_list_print (&queue_list_head);
	msg_reply_list_print (&reply_list_head);

	cleanup_list = msg_pd->queue_cleanup_list.next;

	while (!list_empty (&msg_pd->queue_cleanup_list)) {
		cleanup = list_entry (cleanup_list, struct cleanup_entry, cleanup_list);
		cleanup_list = cleanup_list->next;

		msg_queue_close (&cleanup->queue_name, cleanup->queue_id);

		list_del (&cleanup->cleanup_list);
		free (cleanup);
	}

	track_list = track_list_head.next;

	while (!list_empty (&track_list_head)) {
		track = list_entry (track_list, struct track_entry, track_list);
		track_list = track_list->next;

		list_del (&track->track_list);
		free (track);
	}

	return (0);
}

static void msg_queue_timeout (void *data)
{
	struct req_exec_msg_queue_timeout req_exec_msg_queue_timeout;
	struct iovec iovec;

	struct queue_entry *queue = (struct queue_entry *)data;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_queue_timeout\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(queue->queue_name.value));

	req_exec_msg_queue_timeout.header.size =
		sizeof (struct req_exec_msg_queue_timeout);
	req_exec_msg_queue_timeout.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUE_TIMEOUT);

	memcpy (&req_exec_msg_queue_timeout.source,
		&queue->source,
		sizeof (mar_message_source_t));
	memcpy (&req_exec_msg_queue_timeout.queue_name,
		&queue->queue_name,
		sizeof (mar_name_t));

	iovec.iov_base = (void *)&req_exec_msg_queue_timeout;
	iovec.iov_len = sizeof (struct req_exec_msg_queue_timeout);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void msg_messageget_timeout (void *data)
{
	struct req_exec_msg_messageget_timeout req_exec_msg_messageget_timeout;
	struct iovec iovec;

	struct pending_entry *pending = (struct pending_entry *)data;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_messageget_timeout\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(pending->queue_name.value));

	req_exec_msg_messageget_timeout.header.size =
		sizeof (struct req_exec_msg_messageget_timeout);
	req_exec_msg_messageget_timeout.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGEGET_TIMEOUT);

	memcpy (&req_exec_msg_messageget_timeout.source,
		&pending->source,
		sizeof (mar_message_source_t));
	memcpy (&req_exec_msg_messageget_timeout.queue_name,
		&pending->queue_name,
		sizeof (mar_name_t));

	iovec.iov_base = (void *)&req_exec_msg_messageget_timeout;
	iovec.iov_len = sizeof (struct req_exec_msg_messageget_timeout);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void msg_sendreceive_timeout (void *data)
{
	struct req_exec_msg_sendreceive_timeout req_exec_msg_sendreceive_timeout;
	struct iovec iovec;

	struct reply_entry *reply = (struct reply_entry *)data;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_sendreceive_timeout\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t sender_id=%llx\n",
		    (unsigned long long)(reply->sender_id));

	req_exec_msg_sendreceive_timeout.header.size =
		sizeof (struct req_exec_msg_sendreceive_timeout);
	req_exec_msg_sendreceive_timeout.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_SENDRECEIVE_TIMEOUT);

	memcpy (&req_exec_msg_sendreceive_timeout.source,
		&reply->source,
		sizeof (mar_message_source_t));

	req_exec_msg_sendreceive_timeout.sender_id = reply->sender_id;

	iovec.iov_base = (void *)&req_exec_msg_sendreceive_timeout;
	iovec.iov_len = sizeof (struct req_exec_msg_sendreceive_timeout);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void msg_pending_cancel (
	struct pending_entry *pending)
{
	struct res_lib_msg_messageget res_lib_msg_messageget;
	struct iovec iov;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_pending_cancel\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(pending->queue_name.value));

	res_lib_msg_messageget.header.size =
		sizeof (struct res_lib_msg_messageget);
	res_lib_msg_messageget.header.id =
		MESSAGE_RES_MSG_MESSAGEGET;
	res_lib_msg_messageget.header.error = SA_AIS_ERR_INTERRUPT;

	memset (&res_lib_msg_messageget.message, 0,
		sizeof (mar_msg_message_t));

	res_lib_msg_messageget.send_time = 0;
	res_lib_msg_messageget.sender_id = 0;

	iov.iov_base = (void *)&res_lib_msg_messageget;
	iov.iov_len = sizeof (struct res_lib_msg_messageget);

	api->ipc_response_iov_send (pending->source.conn, &iov, 1);
}

static void msg_pending_deliver (
	struct pending_entry *pending,
	struct message_entry *message)
{
	struct res_lib_msg_messageget res_lib_msg_messageget;
	struct iovec iov[2];

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_pending_deliver\n");

	res_lib_msg_messageget.header.size =
		sizeof (struct res_lib_msg_messageget);
	res_lib_msg_messageget.header.id =
		MESSAGE_RES_MSG_MESSAGEGET;
	res_lib_msg_messageget.header.error = SA_AIS_OK;

	memcpy (&res_lib_msg_messageget.message, &message->message,
		sizeof (mar_msg_message_t));

	res_lib_msg_messageget.send_time = message->send_time;
	res_lib_msg_messageget.sender_id = message->sender_id;

	iov[0].iov_base = (void *)&res_lib_msg_messageget;
	iov[0].iov_len = sizeof (struct res_lib_msg_messageget);

	iov[1].iov_base = (void *)message->message.data;
	iov[1].iov_len = message->message.size;

	api->ipc_response_iov_send (pending->source.conn, iov, 2);
}

static void msg_message_cancel (
	struct queue_entry *queue)
{
	struct pending_entry *pending;
	struct list_head *pending_list;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_message_cancel\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(queue->queue_name.value));

	pending_list = queue->pending_head.next;

	while (!list_empty (&queue->pending_head)) {
		pending = list_entry (pending_list, struct pending_entry, pending_list);
		pending_list = pending_list->next;

		if (api->ipc_source_is_local (&pending->source)) {
			api->timer_delete (pending->timer_handle);
			msg_pending_cancel (pending);
		}

		list_del (&pending->pending_list);
		free (pending);
	}
}	

static void msg_message_release (
	struct queue_entry *queue)
{
	struct message_entry *message;
	struct list_head *message_list;

	int i;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_message_release\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(queue->queue_name.value));

	message_list = queue->message_head.next;

	while (!list_empty (&queue->message_head)) {
		message = list_entry (message_list, struct message_entry, queue_list);
		message_list = message_list->next;

		list_del (&message->queue_list);
		list_del (&message->message_list);

		free (message->message.data);
		free (message);
	}

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++)
	{
		queue->priority[i].queue_used = 0;
		queue->priority[i].number_of_messages = 0;
	}
}

static void msg_pending_release (
	struct pending_entry *pending)
{
	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_pending_release\n");

	list_del (&pending->pending_list);
	free (pending);
}

static void msg_queue_release (
	struct queue_entry *queue)
{
	struct message_entry *message;
	struct list_head *message_list;

	struct pending_entry *pending;
	struct list_head *pending_list;

	int i;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_queue_release\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(queue->queue_name.value));

	pending_list = queue->pending_head.next;

	while (!list_empty (&queue->pending_head)) {
		pending = list_entry (pending_list, struct pending_entry, pending_list);
		pending_list = pending_list->next;

		if (api->ipc_source_is_local (&pending->source)) {
			api->timer_delete (pending->timer_handle);
			msg_pending_cancel (pending);
		}

		list_del (&pending->pending_list);
		free (pending);
	}

	message_list = queue->message_head.next;

	while (!list_empty (&queue->message_head)) {
		message = list_entry (message_list, struct message_entry, queue_list);
		message_list = message_list->next;

		list_del (&message->queue_list);
		list_del (&message->message_list);

		free (message->message.data);
		free (message);
	}

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
		queue->priority[i].queue_used = 0;
		queue->priority[i].number_of_messages = 0;
	}

	if (queue->group != NULL) {
		list_del (&queue->group_list);
	}

	global_queue_count -= 1;

	list_del (&queue->queue_list);
	free (queue);
}

static void msg_group_release (
	struct group_entry *group)
{
	struct queue_entry *queue;
	struct list_head *queue_list;

	struct track_entry *track;
	struct list_head *track_list;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_group_release\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t group=%s\n",
		    (char *)(group->group_name.value));

	queue_list = group->queue_head.next;

	while (!list_empty (&group->queue_head)) {
		queue = list_entry (queue_list, struct queue_entry, group_list);
		queue_list = queue_list->next;

		queue->group = NULL;

		list_del (&queue->group_list);
		list_init (&queue->group_list);
	}

	track_list = track_list_head.next;

	while (track_list != &track_list_head) {
		track = list_entry (track_list, struct track_entry, track_list);
		track_list = track_list->next;

		if (mar_name_match (&track->group_name, &group->group_name)) {
			list_del (&track->track_list);
			free (track);
		}
	}

	global_group_count -= 1;

	list_del (&group->group_list);
	free (group);
}

static void msg_reply_release (
	struct reply_entry *reply)
{
	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_reply_release\n");

	list_del (&reply->reply_list);

	free (reply);
}

static struct track_entry *msg_track_find (
	struct list_head *track_head,
	const mar_name_t *group_name,
	const void *conn)
{
	struct track_entry *track;
	struct list_head *track_list;

	for (track_list = track_head->next;
	     track_list != track_head;
	     track_list = track_list->next)
	{
		track = list_entry (track_list, struct track_entry, track_list);

		if ((mar_name_match (group_name, &track->group_name)) && (conn == track->source.conn)) {
			return (track);
		}
	}
	return (0);
}

static struct reply_entry *msg_reply_find (
	struct list_head *reply_head,
	const mar_msg_sender_id_t sender_id)
{
	struct reply_entry *reply;
	struct list_head *reply_list;

	for (reply_list = reply_head->next;
	     reply_list != reply_head;
	     reply_list = reply_list->next)
	{
		reply = list_entry (reply_list, struct reply_entry, reply_list);

		if (sender_id == reply->sender_id) {
			return (reply);
		}
	}
	return (0);
}

static struct queue_entry *msg_queue_find (
	struct list_head *queue_head,
	const mar_name_t *queue_name)
{
	struct queue_entry *queue;
	struct list_head *queue_list;

	for (queue_list = queue_head->next;
	     queue_list != queue_head;
	     queue_list = queue_list->next)
	{
		queue = list_entry (queue_list, struct queue_entry, queue_list);

		if (mar_name_match (queue_name, &queue->queue_name)) {
			return (queue);
		}
	}
	return (0);
}

static struct queue_entry *msg_queue_find_id (
	struct list_head *queue_head,
	const mar_name_t *queue_name,
	const mar_uint32_t queue_id)
{
	struct queue_entry *queue;
	struct list_head *queue_list;

	for (queue_list = queue_head->next;
	     queue_list != queue_head;
	     queue_list = queue_list->next)
	{
		queue = list_entry (queue_list, struct queue_entry, queue_list);

		if ((mar_name_match (queue_name, &queue->queue_name)) && (queue_id == queue->queue_id))	{
			return (queue);
		}
	}
	return (0);
}

static struct message_entry *msg_queue_find_message (
	struct queue_entry *queue)
{
	struct message_entry *message;

	int i;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_queue_find_message\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(queue->queue_name.value));

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++)
	{
		if (!list_empty (&queue->priority[i].message_head)) {
			message = list_entry (queue->priority[i].message_head.next, struct message_entry, message_list);

			/* DEBUG */
			log_printf (LOGSYS_LEVEL_DEBUG, "\t priority=%d\n", i);

			return (message);
		}
	}
	return (0);
}

static struct pending_entry *msg_queue_find_pending (
	struct queue_entry *queue,
	const mar_message_source_t *source)
{
	struct pending_entry *pending;
	struct list_head *pending_list;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: msg_queue_find_pending\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(queue->queue_name.value));
	log_printf (LOGSYS_LEVEL_DEBUG, "\t nodeid=%x conn=%p\n",
		    (unsigned int)(source->nodeid),
		    (void *)(source->conn));

	for (pending_list = queue->pending_head.next;
	     pending_list != &queue->pending_head;
	     pending_list = pending_list->next)
	{
		pending = list_entry (pending_list, struct pending_entry, pending_list);

		if ((source->nodeid == pending->source.nodeid) &&
		    (source->conn == pending->source.conn))
		{
			return (pending);
		}
	}
	return (0);
}

static struct cleanup_entry *msg_queue_cleanup_find (
	void *conn,
	const mar_name_t *queue_name)
{
	struct msg_pd *msg_pd = (struct msg_pd *)api->ipc_private_data_get (conn);

	struct cleanup_entry *cleanup;
	struct list_head *cleanup_list;

	for (cleanup_list = msg_pd->queue_cleanup_list.next;
	     cleanup_list != &msg_pd->queue_cleanup_list;
	     cleanup_list = cleanup_list->next)
	{
		cleanup = list_entry (cleanup_list, struct cleanup_entry, cleanup_list);

		if (mar_name_match (queue_name, &cleanup->queue_name)) {
			return (cleanup);
		}
	}
	return (0);
}

static struct group_entry *msg_group_find (
	struct list_head *group_head,
	const mar_name_t *group_name)
{
	struct group_entry *group;
	struct list_head *group_list;

	for (group_list = group_head->next;
	     group_list != group_head;
	     group_list = group_list->next)
	{
		group = list_entry (group_list, struct group_entry, group_list);

		if (mar_name_match (group_name, &group->group_name)) {
			return (group);
		}
	}
	return (0);
}

static struct queue_entry *msg_group_member_find (
	struct list_head *queue_head,
	const mar_name_t *queue_name)
{
	struct list_head *queue_list;
	struct queue_entry *queue;

	for (queue_list = queue_head->next;
	     queue_list != queue_head;
	     queue_list = queue_list->next)
	{
		queue = list_entry (queue_list, struct queue_entry, group_list);

		if (mar_name_match (queue_name, &queue->queue_name)) {
			return (queue);
		}
	}
	return (0);
}

static struct queue_entry *msg_group_member_next (
	struct group_entry *group)
{
	struct queue_entry *queue;

	if (group->next_queue->group_list.next == &group->queue_head) {
		queue = list_entry (group->queue_head.next,
			struct queue_entry, group_list);
	}
	else {
		queue = list_entry (group->next_queue->group_list.next,
			struct queue_entry, group_list);
	}

	return (queue);
}

static void msg_queue_priority_area_init (
	struct queue_entry *queue)
{
	int i;

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++)
	{
		queue->priority[i].queue_size = queue->create_attrs.size[i];
		queue->priority[i].capacity_reached = queue->create_attrs.size[i];
		queue->priority[i].capacity_available = 0;

		list_init (&queue->priority[i].message_head);
	}
}

static void message_handler_req_exec_msg_queueopen (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_queueopen
		*req_exec_msg_queueopen = msg;
	struct res_lib_msg_queueopen res_lib_msg_queueopen;
	SaAisErrorT error = SA_AIS_OK;
	SaSizeT queue_size = 0;

	struct cleanup_entry *cleanup = NULL;
	struct queue_entry *queue = NULL;
	struct msg_pd *msg_pd = NULL;
	int i;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgQueueOpen\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(req_exec_msg_queueopen->queue_name.value));

	queue = msg_queue_find (&queue_list_head,
		&req_exec_msg_queueopen->queue_name);

	if (queue == NULL) {
		/*
		 * This is a new queue, so SA_MSG_QUEUE_CREATE flag must be set
		 * and creation attributes must be present.
		 */
		if ((req_exec_msg_queueopen->create_attrs_flag == 0) ||
		    (req_exec_msg_queueopen->open_flags & SA_MSG_QUEUE_CREATE) == 0) {
			error = SA_AIS_ERR_NOT_EXIST;
			goto error_exit;
		}

		/*
		 * Check that creating a new queue would not cause the global
		 * queue count to exceed MAX_NUM_QUEUES.
		 */
		if (global_queue_count >= MSG_MAX_NUM_QUEUES) {
			error = SA_AIS_ERR_NO_RESOURCES;
			goto error_exit;
		}

		/*
		 * Check that each priority area does not exceed
		 * MSG_MAX_PRIORITY_AREA_SIZE and calculate total queue size.
		 */
		for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
			if (req_exec_msg_queueopen->create_attrs.size[i] > MSG_MAX_PRIORITY_AREA_SIZE) {
				error = SA_AIS_ERR_TOO_BIG;
				goto error_exit;
			}
			queue_size += req_exec_msg_queueopen->create_attrs.size[i];
		}

		/*
		 * Check that total queue size does not exceed MSG_MAX_QUEUE_SIZE.
		 */
		if (queue_size > MSG_MAX_QUEUE_SIZE) {
			error = SA_AIS_ERR_TOO_BIG;
			goto error_exit;
		}
	}
	else {
		/*
		 * This queue alreay exists, so check that the reference count is zero.
		 */
		if (queue->refcount != 0) {
			error = SA_AIS_ERR_BUSY;
			goto error_exit;
		}

		/*
		 * if this queue already exists and the SA_MSG_QUEUE_CREATE flag was set,
		 * check that the creation flags/attrs are equivalent to those of the
		 * existing queue.
		 */
		if ((req_exec_msg_queueopen->open_flags & SA_MSG_QUEUE_CREATE) &&
		    ((req_exec_msg_queueopen->create_attrs.creation_flags != queue->create_attrs.creation_flags) ||
		     (memcmp (req_exec_msg_queueopen->create_attrs.size,
			      queue->create_attrs.size,
			      sizeof (queue->create_attrs.size)) != 0)))
		{
			error = SA_AIS_ERR_EXIST;
			goto error_exit;
		}
	}

	cleanup = malloc (sizeof (struct cleanup_entry));
	if (cleanup == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}

	if (queue == NULL) {
		queue = malloc (sizeof (struct queue_entry));
		if (queue == NULL) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}
		memset (queue, 0, sizeof (struct queue_entry));
		memcpy (&queue->queue_name,
			&req_exec_msg_queueopen->queue_name,
			sizeof (mar_name_t));
		memcpy (&queue->create_attrs,
			&req_exec_msg_queueopen->create_attrs,
			sizeof (mar_msg_queue_creation_attributes_t));
		memcpy (&queue->source,
			&req_exec_msg_queueopen->source,
			sizeof (mar_message_source_t));

		queue->open_flags = req_exec_msg_queueopen->open_flags;
		queue->queue_handle = req_exec_msg_queueopen->queue_handle;

		msg_queue_priority_area_init (queue);

		list_init (&queue->group_list);
		list_init (&queue->queue_list);
		list_init (&queue->message_head);
		list_init (&queue->pending_head);

		list_add_tail (&queue->queue_list, &queue_list_head);

		queue->queue_id = global_queue_id;
		queue->refcount = 0;

		global_queue_count += 1;
		global_queue_id += 1;
	}
	else {
		if (queue->timer_handle != 0) {
			api->timer_delete (queue->timer_handle);
		}
		if (req_exec_msg_queueopen->open_flags & SA_MSG_QUEUE_EMPTY) {
			msg_message_release (queue);
		}
	}

	queue->close_time = 0;

	msg_sync_refcount_increment (queue, nodeid);
	msg_sync_refcount_calculate (queue);

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_queueopen->source))
	{
		res_lib_msg_queueopen.header.size =
			sizeof (struct res_lib_msg_queueopen);
		res_lib_msg_queueopen.header.id =
			MESSAGE_RES_MSG_QUEUEOPEN;
		res_lib_msg_queueopen.header.error = error;

		if (queue != NULL) {
			res_lib_msg_queueopen.queue_id = queue->queue_id;
		}

		if (error == SA_AIS_OK) {
			msg_pd = api->ipc_private_data_get (
				req_exec_msg_queueopen->source.conn);

			memcpy (&cleanup->queue_name,
				&queue->queue_name,
				sizeof (SaNameT));

			cleanup->queue_handle = req_exec_msg_queueopen->queue_handle;
			cleanup->queue_id = queue->queue_id;

			list_init (&cleanup->cleanup_list);
			list_add_tail (&cleanup->cleanup_list, &msg_pd->queue_cleanup_list);
		}
		else {
			free (cleanup);
		}

		api->ipc_response_send (
			req_exec_msg_queueopen->source.conn,
			&res_lib_msg_queueopen,
			sizeof (struct res_lib_msg_queueopen));
	}
}

static void message_handler_req_exec_msg_queueopenasync (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_queueopenasync
		*req_exec_msg_queueopenasync = msg;
	struct res_lib_msg_queueopenasync res_lib_msg_queueopenasync;
	struct res_lib_msg_queueopen_callback res_lib_msg_queueopen_callback;
	SaAisErrorT error = SA_AIS_OK;
	SaSizeT queue_size = 0;

	struct cleanup_entry *cleanup = NULL;
	struct queue_entry *queue = NULL;
	struct msg_pd *msg_pd = NULL;
	int i;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgQueueOpenAsync\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(req_exec_msg_queueopenasync->queue_name.value));

	queue = msg_queue_find (&queue_list_head,
		&req_exec_msg_queueopenasync->queue_name);

	if (queue == NULL) {
		/*
		 * This is a new queue, so SA_MSG_QUEUE_CREATE flag must be set
		 * and creation attributes must be present.
		 */
		if ((req_exec_msg_queueopenasync->create_attrs_flag == 0) ||
		    (req_exec_msg_queueopenasync->open_flags & SA_MSG_QUEUE_CREATE) == 0) {
			error = SA_AIS_ERR_NOT_EXIST;
			goto error_exit;
		}

		/*
		 * Check that creating a new queue would not cause the global
		 * queue count to exceed MSG_MAX_NUM_QUEUES.
		 */
		if (global_queue_count >= MSG_MAX_NUM_QUEUES) {
			error = SA_AIS_ERR_NO_RESOURCES;
			goto error_exit;
		}

		/*
		 * Check that each priority area does not exceed
		 * MSG_MAX_PRIORITY_AREA_SIZE and calculate total queue size.
		 */
		for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
			if (req_exec_msg_queueopenasync->create_attrs.size[i] > MSG_MAX_PRIORITY_AREA_SIZE) {
				error = SA_AIS_ERR_TOO_BIG;
				goto error_exit;
			}
			queue_size += req_exec_msg_queueopenasync->create_attrs.size[i];
		}

		/*
		 * Check that total queue size does not exceed MSG_MAX_QUEUE_SIZE.
		 */
		if (queue_size > MSG_MAX_QUEUE_SIZE) {
			error = SA_AIS_ERR_TOO_BIG;
			goto error_exit;
		}
	}
	else {
		/*
		 * This queue alreay exists, so check that the reference count is zero.
		 */
		if (queue->refcount != 0) {
			error = SA_AIS_ERR_BUSY;
			goto error_exit;
		}

		/*
		 * If this queue already exists and the SA_MSG_QUEUE_CREATE flag was set,
		 * check that the creation flags/attrs are equivalent to those of the
		 * existing queue.
		 */
		if ((req_exec_msg_queueopenasync->open_flags & SA_MSG_QUEUE_CREATE) &&
		    ((req_exec_msg_queueopenasync->create_attrs.creation_flags != queue->create_attrs.creation_flags) ||
		     (memcmp (req_exec_msg_queueopenasync->create_attrs.size,
			      queue->create_attrs.size,
			      sizeof (queue->create_attrs.size)) != 0)))
		{
			error = SA_AIS_ERR_EXIST;
			goto error_exit;
		}
	}

	cleanup = malloc (sizeof (struct cleanup_entry));
	if (cleanup == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}

	if (queue == NULL) {
		queue = malloc (sizeof (struct queue_entry));
		if (queue == NULL) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}
		memset (queue, 0, sizeof (struct queue_entry));
		memcpy (&queue->queue_name,
			&req_exec_msg_queueopenasync->queue_name,
			sizeof (mar_name_t));
		memcpy (&queue->create_attrs,
			&req_exec_msg_queueopenasync->create_attrs,
			sizeof (mar_msg_queue_creation_attributes_t));
		memcpy (&queue->source,
			&req_exec_msg_queueopenasync->source,
			sizeof (mar_message_source_t));

		queue->open_flags = req_exec_msg_queueopenasync->open_flags;
		queue->queue_handle = req_exec_msg_queueopenasync->queue_handle;

		msg_queue_priority_area_init (queue);

		list_init (&queue->group_list);
		list_init (&queue->queue_list);
		list_init (&queue->message_head);
		list_init (&queue->pending_head);

		list_add_tail  (&queue->queue_list, &queue_list_head);

		queue->queue_id = global_queue_id;
		queue->refcount = 0;

		global_queue_count += 1;
		global_queue_id += 1;
	}
	else {
		if (queue->timer_handle != 0) {
			api->timer_delete (queue->timer_handle);
		}
		if (req_exec_msg_queueopenasync->open_flags & SA_MSG_QUEUE_EMPTY) {
			msg_message_release (queue);
		}
	}

	queue->close_time = 0;

	msg_sync_refcount_increment (queue, nodeid);
	msg_sync_refcount_calculate (queue);

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_queueopenasync->source))
	{
		res_lib_msg_queueopenasync.header.size =
			sizeof (struct res_lib_msg_queueopenasync);
		res_lib_msg_queueopenasync.header.id =
			MESSAGE_RES_MSG_QUEUEOPENASYNC;
		res_lib_msg_queueopenasync.header.error = error;

		if (error == SA_AIS_OK) {
			msg_pd = api->ipc_private_data_get (
				req_exec_msg_queueopenasync->source.conn);

			memcpy (&cleanup->queue_name,
				&queue->queue_name,
				sizeof (SaNameT));

			cleanup->queue_handle = req_exec_msg_queueopenasync->queue_handle;
			cleanup->queue_id = queue->queue_id;

			list_init (&cleanup->cleanup_list);
			list_add_tail (&cleanup->cleanup_list, &msg_pd->queue_cleanup_list);
		}
		else {
			free (cleanup);
		}

		api->ipc_response_send (
			req_exec_msg_queueopenasync->source.conn,
			&res_lib_msg_queueopenasync,
			sizeof (struct res_lib_msg_queueopenasync));

		res_lib_msg_queueopen_callback.header.size =
			sizeof (struct res_lib_msg_queueopen_callback);
		res_lib_msg_queueopen_callback.header.id =
			MESSAGE_RES_MSG_QUEUEOPEN_CALLBACK;
		res_lib_msg_queueopen_callback.header.error = error;

		if (queue != NULL) {
			res_lib_msg_queueopen_callback.queue_id = queue->queue_id;
		}

		res_lib_msg_queueopen_callback.queue_handle =
			req_exec_msg_queueopenasync->queue_handle;
		res_lib_msg_queueopen_callback.invocation =
			req_exec_msg_queueopenasync->invocation;

		api->ipc_dispatch_send (
			req_exec_msg_queueopenasync->source.conn,
			&res_lib_msg_queueopen_callback,
			sizeof (struct res_lib_msg_queueopen_callback));
	}
}

static void message_handler_req_exec_msg_queueclose (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_queueclose
		*req_exec_msg_queueclose = msg;
	struct res_lib_msg_queueclose res_lib_msg_queueclose;
	SaAisErrorT error = SA_AIS_OK;

	struct queue_entry *queue = NULL;
	struct cleanup_entry *cleanup = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgQueueClose\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s (id=%u)\n",
		    (char *)(req_exec_msg_queueclose->queue_name.value),
		    (unsigned int)(req_exec_msg_queueclose->queue_id));

	queue = msg_queue_find_id (&queue_list_head,
		&req_exec_msg_queueclose->queue_name,
		req_exec_msg_queueclose->queue_id);
	if (queue == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	msg_sync_refcount_decrement (queue, nodeid);
	msg_sync_refcount_calculate (queue);

	memset (&queue->source, 0, sizeof (mar_message_source_t));

	queue->queue_handle = 0;

	if (queue->refcount == 0) {
		queue->close_time = api->timer_time_get();

		if ((queue->create_attrs.creation_flags == SA_MSG_QUEUE_PERSISTENT) &&
		    (queue->unlink_flag))
		{
			msg_queue_release (queue);
		}

		if ((queue->create_attrs.creation_flags != SA_MSG_QUEUE_PERSISTENT) &&
		    (lowest_nodeid == api->totem_nodeid_get()))
		{
			api->timer_add_absolute (
				(queue->create_attrs.retention_time + queue->close_time),
				(void *)(queue), msg_queue_timeout, &queue->timer_handle);
		}
	}

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_queueclose->source))
	{
		res_lib_msg_queueclose.header.size =
			sizeof (struct res_lib_msg_queueclose);
		res_lib_msg_queueclose.header.id =
			MESSAGE_RES_MSG_QUEUECLOSE;
		res_lib_msg_queueclose.header.error = error;

		api->ipc_response_send (
			req_exec_msg_queueclose->source.conn,
			&res_lib_msg_queueclose,
			sizeof (struct res_lib_msg_queueclose));

		if (error == SA_AIS_OK) {
			/*
			 * Remove the cleanup entry for this queue.
			 */
			cleanup = msg_queue_cleanup_find (
				req_exec_msg_queueclose->source.conn,
				&req_exec_msg_queueclose->queue_name);

			if (cleanup != NULL) {
				list_del (&cleanup->cleanup_list);
				free (cleanup);
			}
		}
	}
}

static void message_handler_req_exec_msg_queuestatusget (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_queuestatusget
		*req_exec_msg_queuestatusget = msg;
	struct res_lib_msg_queuestatusget res_lib_msg_queuestatusget;
	SaAisErrorT error = SA_AIS_OK;

	struct queue_entry *queue = NULL;
	int i;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgQueueStatusGet\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(req_exec_msg_queuestatusget->queue_name.value));

	queue = msg_queue_find (&queue_list_head,
		&req_exec_msg_queuestatusget->queue_name);
	if (queue == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	res_lib_msg_queuestatusget.queue_status.creation_flags =
		queue->create_attrs.creation_flags;
	res_lib_msg_queuestatusget.queue_status.retention_time =
		queue->create_attrs.retention_time;
	res_lib_msg_queuestatusget.queue_status.close_time =
		queue->close_time;

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++)
	{
		res_lib_msg_queuestatusget.queue_status.queue_usage[i].queue_size =
			queue->priority[i].queue_size;
		res_lib_msg_queuestatusget.queue_status.queue_usage[i].queue_used =
			queue->priority[i].queue_used;
		res_lib_msg_queuestatusget.queue_status.queue_usage[i].number_of_messages =
			queue->priority[i].number_of_messages;
	}

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_queuestatusget->source))
	{
		res_lib_msg_queuestatusget.header.size =
			sizeof (struct res_lib_msg_queuestatusget);
		res_lib_msg_queuestatusget.header.id =
			MESSAGE_RES_MSG_QUEUESTATUSGET;
		res_lib_msg_queuestatusget.header.error = error;

		api->ipc_response_send (
			req_exec_msg_queuestatusget->source.conn,
			&res_lib_msg_queuestatusget,
			sizeof (struct res_lib_msg_queuestatusget));
	}
}

static void message_handler_req_exec_msg_queueretentiontimeset (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_queueretentiontimeset
		*req_exec_msg_queueretentiontimeset = msg;
	struct res_lib_msg_queueretentiontimeset res_lib_msg_queueretentiontimeset;
	SaAisErrorT error = SA_AIS_OK;

	struct queue_entry *queue = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgQueueRetentionTimeSet\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(req_exec_msg_queueretentiontimeset->queue_name.value));

	queue = msg_queue_find_id (&queue_list_head,
		&req_exec_msg_queueretentiontimeset->queue_name,
		req_exec_msg_queueretentiontimeset->queue_id);
	if (queue == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if ((queue->unlink_flag != 0) ||
	    (queue->create_attrs.creation_flags & SA_MSG_QUEUE_PERSISTENT)) {
		error = SA_AIS_ERR_BAD_OPERATION;
		goto error_exit;
	}

	queue->create_attrs.retention_time =
		req_exec_msg_queueretentiontimeset->retention_time;

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_queueretentiontimeset->source))
	{
		res_lib_msg_queueretentiontimeset.header.size =
			sizeof (struct res_lib_msg_queueretentiontimeset);
		res_lib_msg_queueretentiontimeset.header.id =
			MESSAGE_RES_MSG_QUEUERETENTIONTIMESET;
		res_lib_msg_queueretentiontimeset.header.error = error;

		api->ipc_response_send (
			req_exec_msg_queueretentiontimeset->source.conn,
			&res_lib_msg_queueretentiontimeset,
			sizeof (struct res_lib_msg_queueretentiontimeset));
	}
}

static void message_handler_req_exec_msg_queueunlink (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_queueunlink
		*req_exec_msg_queueunlink = msg;
	struct res_lib_msg_queueunlink res_lib_msg_queueunlink;
	SaAisErrorT error = SA_AIS_OK;

	struct queue_entry *queue = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgQueueUnlink\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(req_exec_msg_queueunlink->queue_name.value));

	queue = msg_queue_find (&queue_list_head,
		&req_exec_msg_queueunlink->queue_name);
	if (queue == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	queue->unlink_flag = 1;

	if (queue->refcount == 0) {
		if (queue->timer_handle != 0) {
			api->timer_delete (queue->timer_handle);
		}
		msg_queue_release (queue);
	}

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_queueunlink->source))
	{
		res_lib_msg_queueunlink.header.size =
			sizeof (struct res_lib_msg_queueunlink);
		res_lib_msg_queueunlink.header.id =
			MESSAGE_RES_MSG_QUEUEUNLINK;
		res_lib_msg_queueunlink.header.error = error;

		api->ipc_response_send (
			req_exec_msg_queueunlink->source.conn,
			&res_lib_msg_queueunlink,
			sizeof (struct res_lib_msg_queueunlink));
	}
}

static void message_handler_req_exec_msg_queuegroupcreate (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_queuegroupcreate
		*req_exec_msg_queuegroupcreate = msg;
	struct res_lib_msg_queuegroupcreate res_lib_msg_queuegroupcreate;
	SaAisErrorT error = SA_AIS_OK;

	struct group_entry *group = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgQueueGroupCreate\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t group=%s\n",
		    (char *)(req_exec_msg_queuegroupcreate->group_name.value));

	if (global_group_count >= MSG_MAX_NUM_QUEUE_GROUPS) {
		error = SA_AIS_ERR_NO_RESOURCES;
		goto error_exit;
	}

	group = msg_group_find (&group_list_head,
		&req_exec_msg_queuegroupcreate->group_name);

	if (group == NULL) {
		group = malloc (sizeof (struct group_entry));
		if (group == NULL) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}
		memset (group, 0, sizeof (struct group_entry));
		memcpy (&group->group_name,
			&req_exec_msg_queuegroupcreate->group_name,
			sizeof (mar_name_t));

		group->policy = req_exec_msg_queuegroupcreate->policy;

		list_init (&group->queue_head);
		list_init (&group->group_list);

		list_add_tail  (&group->group_list, &group_list_head);

		global_group_count += 1;
	}
	else {
		error = SA_AIS_ERR_EXIST;
		goto error_exit;
	}

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_queuegroupcreate->source))
	{
		res_lib_msg_queuegroupcreate.header.size =
			sizeof (struct res_lib_msg_queuegroupcreate);
		res_lib_msg_queuegroupcreate.header.id =
			MESSAGE_RES_MSG_QUEUEGROUPCREATE;
		res_lib_msg_queuegroupcreate.header.error = error;

		api->ipc_response_send (
			req_exec_msg_queuegroupcreate->source.conn,
			&res_lib_msg_queuegroupcreate,
			sizeof (struct res_lib_msg_queuegroupcreate));
	}
}

static void message_handler_req_exec_msg_queuegroupinsert (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_queuegroupinsert
		*req_exec_msg_queuegroupinsert = msg;
	struct res_lib_msg_queuegroupinsert res_lib_msg_queuegroupinsert;
	struct res_lib_msg_queuegrouptrack_callback res_lib_msg_queuegrouptrack_callback;
	SaAisErrorT error = SA_AIS_OK;

	struct group_entry *group = NULL;
	struct queue_entry *queue = NULL;
	struct track_entry *track = NULL;
	struct iovec iov[2];

	mar_msg_queue_group_notification_t notification[MSG_MAX_NUM_QUEUES_PER_GROUP];
	unsigned int count = 0;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgQueueGroupInsert\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t group=%s queue=%s\n",
		    (char *)(req_exec_msg_queuegroupinsert->group_name.value),
		    (char *)(req_exec_msg_queuegroupinsert->queue_name.value));

	group = msg_group_find (&group_list_head,
		&req_exec_msg_queuegroupinsert->group_name);
	if (group == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	queue = msg_group_member_find (&group->queue_head,
		&req_exec_msg_queuegroupinsert->queue_name);
	if (queue != NULL) {
		error = SA_AIS_ERR_EXIST;
		goto error_exit;
	}

	queue = msg_queue_find (&queue_list_head,
		&req_exec_msg_queuegroupinsert->queue_name);
	if (queue == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * Temporary fix to prevent adding queue to multiple groups.
	 */
	if (queue->group != NULL) {
		error = SA_AIS_ERR_NOT_SUPPORTED;
		goto error_exit;
	}

	if (group->member_count >= MSG_MAX_NUM_QUEUES_PER_GROUP) {
		error = SA_AIS_ERR_NO_RESOURCES;
		goto error_exit;
	}

	if ((group->policy == SA_MSG_QUEUE_GROUP_ROUND_ROBIN)
	    && (group->next_queue == NULL)) {
		group->next_queue = queue;
	}

	queue->group = group;
	group->member_count += 1;
	queue->change_flag = SA_MSG_QUEUE_GROUP_ADDED;

	list_init (&queue->group_list);
	list_add_tail (&queue->group_list, &group->queue_head);

	track = msg_track_find (&track_list_head,
		&req_exec_msg_queuegroupinsert->group_name,
		req_exec_msg_queuegroupinsert->source.conn);

	if (track != NULL) {
		memset (notification, 0,
			sizeof (mar_msg_queue_group_notification_t) * MSG_MAX_NUM_QUEUES_PER_GROUP);

		if (track->track_flags & SA_TRACK_CHANGES) {
			count = msg_group_track_changes (group, notification);

			res_lib_msg_queuegrouptrack_callback.header.size =
				sizeof (struct res_lib_msg_queuegrouptrack_callback) +
				sizeof (mar_msg_queue_group_notification_t) * count;
			res_lib_msg_queuegrouptrack_callback.header.id =
				MESSAGE_RES_MSG_QUEUEGROUPTRACK_CALLBACK;
			res_lib_msg_queuegrouptrack_callback.header.error = error;

			/* DEBUG */
			log_printf (LOGSYS_LEVEL_DEBUG, "\t error=%u\n",
				    (unsigned int)(res_lib_msg_queuegrouptrack_callback.header.error));

			memcpy (&res_lib_msg_queuegrouptrack_callback.group_name,
				&group->group_name, sizeof (mar_name_t));

			res_lib_msg_queuegrouptrack_callback.number_of_items = count;
			res_lib_msg_queuegrouptrack_callback.queue_group_policy = group->policy;
			res_lib_msg_queuegrouptrack_callback.member_count = group->member_count;

			iov[0].iov_base = (void *)&res_lib_msg_queuegrouptrack_callback;
			iov[0].iov_len = sizeof (struct res_lib_msg_queuegrouptrack_callback);

			iov[1].iov_base = (void *)(notification);
			iov[1].iov_len = sizeof (mar_msg_queue_group_notification_t) * count;

			api->ipc_dispatch_iov_send (track->source.conn, iov, 2);
		}

		if (track->track_flags & SA_TRACK_CHANGES_ONLY) {
			count = msg_group_track_changes_only (group, notification);

			res_lib_msg_queuegrouptrack_callback.header.size =
				sizeof (struct res_lib_msg_queuegrouptrack_callback) +
				sizeof (mar_msg_queue_group_notification_t) * count;
			res_lib_msg_queuegrouptrack_callback.header.id =
				MESSAGE_RES_MSG_QUEUEGROUPTRACK_CALLBACK;
			res_lib_msg_queuegrouptrack_callback.header.error = error;

			/* DEBUG */
			log_printf (LOGSYS_LEVEL_DEBUG, "\t error=%u\n",
				    (unsigned int)(res_lib_msg_queuegrouptrack_callback.header.error));

			memcpy (&res_lib_msg_queuegrouptrack_callback.group_name,
				&group->group_name, sizeof (mar_name_t));

			res_lib_msg_queuegrouptrack_callback.number_of_items = count;
			res_lib_msg_queuegrouptrack_callback.queue_group_policy = group->policy;
			res_lib_msg_queuegrouptrack_callback.member_count = group->member_count;

			iov[0].iov_base = (void *)&res_lib_msg_queuegrouptrack_callback;
			iov[0].iov_len = sizeof (struct res_lib_msg_queuegrouptrack_callback);

			iov[1].iov_base = (void *)(notification);
			iov[1].iov_len = sizeof (mar_msg_queue_group_notification_t) * count;

			api->ipc_dispatch_iov_send (track->source.conn, iov, 2);
		}
	}

	queue->change_flag = SA_MSG_QUEUE_GROUP_NO_CHANGE;

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_queuegroupinsert->source))
	{
		res_lib_msg_queuegroupinsert.header.size =
			sizeof (struct res_lib_msg_queuegroupinsert);
		res_lib_msg_queuegroupinsert.header.id =
			MESSAGE_RES_MSG_QUEUEGROUPINSERT;
		res_lib_msg_queuegroupinsert.header.error = error;

		api->ipc_response_send (
			req_exec_msg_queuegroupinsert->source.conn,
			&res_lib_msg_queuegroupinsert,
			sizeof (struct res_lib_msg_queuegroupinsert));
	}
}

static void message_handler_req_exec_msg_queuegroupremove (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_queuegroupremove
		*req_exec_msg_queuegroupremove = msg;
	struct res_lib_msg_queuegroupremove res_lib_msg_queuegroupremove;
	struct res_lib_msg_queuegrouptrack_callback res_lib_msg_queuegrouptrack_callback;
	SaAisErrorT error = SA_AIS_OK;

	struct group_entry *group = NULL;
	struct queue_entry *queue = NULL;
	struct track_entry *track = NULL;
	struct iovec iov[2];

	mar_msg_queue_group_notification_t notification[MSG_MAX_NUM_QUEUES_PER_GROUP];
	unsigned int count = 0;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgQueueGroupRemove\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t group=%s queue=%s\n",
		    (char *)(req_exec_msg_queuegroupremove->group_name.value),
		    (char *)(req_exec_msg_queuegroupremove->queue_name.value));

	group = msg_group_find (&group_list_head,
		&req_exec_msg_queuegroupremove->group_name);
	if (group == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	queue = msg_group_member_find (&group->queue_head,
		&req_exec_msg_queuegroupremove->queue_name);
	if (queue == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (group->next_queue == queue) {
		group->next_queue = msg_group_member_next (group);
	}

	queue->group = NULL;
	group->member_count -= 1;
	queue->change_flag = SA_MSG_QUEUE_GROUP_REMOVED;

	track = msg_track_find (&track_list_head,
		&req_exec_msg_queuegroupremove->group_name,
		req_exec_msg_queuegroupremove->source.conn);

	if (track != NULL) {
		memset (notification, 0,
			sizeof (mar_msg_queue_group_notification_t) * MSG_MAX_NUM_QUEUES_PER_GROUP);

		if (track->track_flags & SA_TRACK_CHANGES) {
			count = msg_group_track_changes (group, notification);

			res_lib_msg_queuegrouptrack_callback.header.size =
				sizeof (struct res_lib_msg_queuegrouptrack_callback) +
				sizeof (mar_msg_queue_group_notification_t) * count;
			res_lib_msg_queuegrouptrack_callback.header.id =
				MESSAGE_RES_MSG_QUEUEGROUPTRACK_CALLBACK;
			res_lib_msg_queuegrouptrack_callback.header.error = error;

			memcpy (&res_lib_msg_queuegrouptrack_callback.group_name,
				&group->group_name, sizeof (mar_name_t));

			res_lib_msg_queuegrouptrack_callback.number_of_items = count;
			res_lib_msg_queuegrouptrack_callback.queue_group_policy = group->policy;
			res_lib_msg_queuegrouptrack_callback.member_count = group->member_count;

			iov[0].iov_base = (void *)&res_lib_msg_queuegrouptrack_callback;
			iov[0].iov_len = sizeof (struct res_lib_msg_queuegrouptrack_callback);

			iov[1].iov_base = (void *)(notification);
			iov[1].iov_len = sizeof (mar_msg_queue_group_notification_t) * count;

			api->ipc_dispatch_iov_send (track->source.conn, iov, 2);
		}

		if (track->track_flags & SA_TRACK_CHANGES_ONLY) {
			count = msg_group_track_changes_only (group, notification);

			res_lib_msg_queuegrouptrack_callback.header.size =
				sizeof (struct res_lib_msg_queuegrouptrack_callback) +
				sizeof (mar_msg_queue_group_notification_t) * count;
			res_lib_msg_queuegrouptrack_callback.header.id =
				MESSAGE_RES_MSG_QUEUEGROUPTRACK_CALLBACK;
			res_lib_msg_queuegrouptrack_callback.header.error = error;

			memcpy (&res_lib_msg_queuegrouptrack_callback.group_name,
				&group->group_name, sizeof (mar_name_t));

			res_lib_msg_queuegrouptrack_callback.number_of_items = count;
			res_lib_msg_queuegrouptrack_callback.queue_group_policy = group->policy;
			res_lib_msg_queuegrouptrack_callback.member_count = group->member_count;

			iov[0].iov_base = (void *)&res_lib_msg_queuegrouptrack_callback;
			iov[0].iov_len = sizeof (struct res_lib_msg_queuegrouptrack_callback);

			iov[1].iov_base = (void *)(notification);
			iov[1].iov_len = sizeof (mar_msg_queue_group_notification_t) * count;

			api->ipc_dispatch_iov_send (track->source.conn, iov, 2);
		}
	}

	list_del (&queue->group_list);

	if (group->member_count == 0) {
		group->next_queue = NULL;
	}

	queue->change_flag = SA_MSG_QUEUE_GROUP_NO_CHANGE;

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_queuegroupremove->source))
	{
		res_lib_msg_queuegroupremove.header.size =
			sizeof (struct res_lib_msg_queuegroupremove);
		res_lib_msg_queuegroupremove.header.id =
			MESSAGE_RES_MSG_QUEUEGROUPREMOVE;
		res_lib_msg_queuegroupremove.header.error = error;

		api->ipc_response_send (
			req_exec_msg_queuegroupremove->source.conn,
			&res_lib_msg_queuegroupremove,
			sizeof (struct res_lib_msg_queuegroupremove));
	}
}

static void message_handler_req_exec_msg_queuegroupdelete (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_queuegroupdelete
		*req_exec_msg_queuegroupdelete = msg;
	struct res_lib_msg_queuegroupdelete res_lib_msg_queuegroupdelete;
	SaAisErrorT error = SA_AIS_OK;

	struct group_entry *group = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgQueueGroupDelete\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t group=%s\n",
		    (char *)(req_exec_msg_queuegroupdelete->group_name.value));

	group = msg_group_find (&group_list_head,
		&req_exec_msg_queuegroupdelete->group_name);
	if (group == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	msg_group_release (group);

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_queuegroupdelete->source))
	{
		res_lib_msg_queuegroupdelete.header.size =
			sizeof (struct res_lib_msg_queuegroupdelete);
		res_lib_msg_queuegroupdelete.header.id =
			MESSAGE_RES_MSG_QUEUEGROUPDELETE;
		res_lib_msg_queuegroupdelete.header.error = error;

		api->ipc_response_send (
			req_exec_msg_queuegroupdelete->source.conn,
			&res_lib_msg_queuegroupdelete,
			sizeof (struct res_lib_msg_queuegroupdelete));
	}
}

static void message_handler_req_exec_msg_queuegrouptrack (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_queuegrouptrack
		*req_exec_msg_queuegrouptrack = msg;
	struct res_lib_msg_queuegrouptrack res_lib_msg_queuegrouptrack;
	SaAisErrorT error = SA_AIS_OK;

	struct group_entry *group = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgQueueGroupTrack\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t group=%s\n",
		    (char *)(req_exec_msg_queuegrouptrack->group_name.value));

	group = msg_group_find (&group_list_head,
		&req_exec_msg_queuegrouptrack->group_name);
	if (group == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_queuegrouptrack->source))
	{
		res_lib_msg_queuegrouptrack.header.size =
			sizeof (struct res_lib_msg_queuegrouptrack);
		res_lib_msg_queuegrouptrack.header.id =
			MESSAGE_RES_MSG_QUEUEGROUPTRACK;
		res_lib_msg_queuegrouptrack.header.error = error;

		api->ipc_response_send (
			req_exec_msg_queuegrouptrack->source.conn,
			&res_lib_msg_queuegrouptrack,
			sizeof (struct res_lib_msg_queuegrouptrack));
	}
}

static void message_handler_req_exec_msg_queuegrouptrackstop (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_queuegrouptrackstop
		*req_exec_msg_queuegrouptrackstop = msg;
	struct res_lib_msg_queuegrouptrackstop res_lib_msg_queuegrouptrackstop;
	SaAisErrorT error = SA_AIS_OK;

	struct group_entry *group = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgQueueGroupTrackStop\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t group=%s\n",
		    (char *)(req_exec_msg_queuegrouptrackstop->group_name.value));

	group = msg_group_find (&group_list_head,
		&req_exec_msg_queuegrouptrackstop->group_name);
	if (group == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_queuegrouptrackstop->source))
	{
		res_lib_msg_queuegrouptrackstop.header.size =
			sizeof (struct res_lib_msg_queuegrouptrackstop);
		res_lib_msg_queuegrouptrackstop.header.id =
			MESSAGE_RES_MSG_QUEUEGROUPTRACKSTOP;
		res_lib_msg_queuegrouptrackstop.header.error = error;

		api->ipc_response_send (
			req_exec_msg_queuegrouptrackstop->source.conn,
			&res_lib_msg_queuegrouptrackstop,
			sizeof (struct res_lib_msg_queuegrouptrackstop));
	}
}

static void message_handler_req_exec_msg_queuegroupnotificationfree (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_queuegroupnotificationfree
		*req_exec_msg_queuegroupnotificationfree = msg;
	struct res_lib_msg_queuegroupnotificationfree res_lib_msg_queuegroupnotificationfree;
	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgQueueGroupNotificationFree\n");

	if (api->ipc_source_is_local (&req_exec_msg_queuegroupnotificationfree->source))
	{
		res_lib_msg_queuegroupnotificationfree.header.size =
			sizeof (struct res_lib_msg_queuegroupnotificationfree);
		res_lib_msg_queuegroupnotificationfree.header.id =
			MESSAGE_RES_MSG_QUEUEGROUPNOTIFICATIONFREE;
		res_lib_msg_queuegroupnotificationfree.header.error = error;

		api->ipc_response_send (
			req_exec_msg_queuegroupnotificationfree->source.conn,
			&res_lib_msg_queuegroupnotificationfree,
			sizeof (struct res_lib_msg_queuegroupnotificationfree));
	}
}

static void message_handler_req_exec_msg_messagesend (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_messagesend
		*req_exec_msg_messagesend = msg;
	struct res_lib_msg_messagesend res_lib_msg_messagesend;
	struct res_lib_msg_messagereceived_callback res_lib_msg_messagereceived_callback;
	SaAisErrorT error = SA_AIS_OK;

	struct group_entry *group = NULL;
	struct queue_entry *queue = NULL;
	struct message_entry *message = NULL;
	struct pending_entry *pending = NULL;

	char *data = ((char *)(req_exec_msg_messagesend) +
		      sizeof (struct req_exec_msg_messagesend));
	unsigned int priority = req_exec_msg_messagesend->message.priority;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgMessageSend\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t destination=%s\n",
		    (char *)(req_exec_msg_messagesend->destination.value));

	group = msg_group_find (&group_list_head,
		&req_exec_msg_messagesend->destination);
	if (group == NULL) {
		queue = msg_queue_find (&queue_list_head,
			&req_exec_msg_messagesend->destination);
		if (queue == NULL) {
			error = SA_AIS_ERR_NOT_EXIST;
			goto error_exit;
		}
	}
	else {
		queue = group->next_queue;
		if (queue == NULL) {
			error = SA_AIS_ERR_QUEUE_NOT_AVAILABLE;
			goto error_exit;
		}
	}

	if (req_exec_msg_messagesend->message.size > MSG_MAX_MESSAGE_SIZE) {
		error = SA_AIS_ERR_TOO_BIG;
		goto error_exit;
	}

	if ((queue->priority[priority].queue_size -
	     queue->priority[priority].queue_used) < req_exec_msg_messagesend->message.size) {
		error = SA_AIS_ERR_QUEUE_FULL;
		goto error_exit;
	}

	message = malloc (sizeof (struct message_entry));
	if (message == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}
	memset (message, 0, sizeof (struct message_entry));
	memcpy (&message->message,
		&req_exec_msg_messagesend->message,
		sizeof (mar_msg_message_t));

	message->message.data = malloc (message->message.size);
	if (message->message.data == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}
	memset (message->message.data, 0, message->message.size);
	memcpy (message->message.data, (char *)(data), message->message.size);

	message->sender_id = 0;
	message->send_time = api->timer_time_get();

	if (list_empty (&queue->pending_head)) {
		list_add_tail (&message->queue_list,
			&queue->message_head);
		list_add_tail (&message->message_list,
			&queue->priority[(message->message.priority)].message_head);

		queue->priority[(message->message.priority)].queue_used += message->message.size;
		queue->priority[(message->message.priority)].number_of_messages += 1;
	}
	else {
		pending = list_entry (queue->pending_head.next,	struct pending_entry, pending_list);
		if (pending == NULL) {
			error = SA_AIS_ERR_LIBRARY;
			goto error_exit;
		}

		if (api->ipc_source_is_local (&pending->source)) {
			api->timer_delete (pending->timer_handle);
			msg_pending_deliver (pending, message);
		}

		list_del (&pending->pending_list);
		free (pending);
	}

	if (group != NULL) {
		group->next_queue = msg_group_member_next (group);
	}

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_messagesend->source))
	{
		res_lib_msg_messagesend.header.size =
			sizeof (struct res_lib_msg_messagesend);
		res_lib_msg_messagesend.header.id =
			MESSAGE_RES_MSG_MESSAGESEND;
		res_lib_msg_messagesend.header.error = error;

		api->ipc_response_send (
			req_exec_msg_messagesend->source.conn,
			&res_lib_msg_messagesend,
			sizeof (struct res_lib_msg_messagesend));
	}

	if ((error == SA_AIS_OK) && (queue->open_flags & SA_MSG_QUEUE_RECEIVE_CALLBACK) &&
	    (api->ipc_source_is_local (&queue->source)))
	{
		res_lib_msg_messagereceived_callback.header.size =
				sizeof (struct res_lib_msg_messagereceived_callback);
		res_lib_msg_messagereceived_callback.header.id =
			MESSAGE_RES_MSG_MESSAGERECEIVED_CALLBACK;
		res_lib_msg_messagereceived_callback.header.error = SA_AIS_OK;
		res_lib_msg_messagereceived_callback.queue_handle = queue->queue_handle;

		api->ipc_dispatch_send (
			queue->source.conn,
			&res_lib_msg_messagereceived_callback,
			sizeof (struct res_lib_msg_messagereceived_callback));
	}

	if ((error != SA_AIS_OK) && (message != NULL)) {
		free (message->message.data);
		free (message);
	}
}

static void message_handler_req_exec_msg_messagesendasync (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_messagesendasync
		*req_exec_msg_messagesendasync = msg;
	struct res_lib_msg_messagesendasync res_lib_msg_messagesendasync;
	struct res_lib_msg_messagereceived_callback res_lib_msg_messagereceived_callback;
	struct res_lib_msg_messagedelivered_callback res_lib_msg_messagedelivered_callback;
	SaAisErrorT error = SA_AIS_OK;

	struct group_entry *group = NULL;
	struct queue_entry *queue = NULL;
	struct message_entry *message = NULL;
	struct pending_entry *pending = NULL;

	char *data = ((char *)(req_exec_msg_messagesendasync) +
		      sizeof (struct req_exec_msg_messagesendasync));
	unsigned int priority = req_exec_msg_messagesendasync->message.priority;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgMessageSendAsync\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t destination=%s\n",
		    (char *)(req_exec_msg_messagesendasync->destination.value));

	group = msg_group_find (&group_list_head,
		&req_exec_msg_messagesendasync->destination);
	if (group == NULL) {
		queue = msg_queue_find (&queue_list_head,
			&req_exec_msg_messagesendasync->destination);
		if (queue == NULL) {
			error = SA_AIS_ERR_NOT_EXIST;
			goto error_exit;
		}
	}
	else {
		queue = group->next_queue;
		if (queue == NULL) {
			error = SA_AIS_ERR_QUEUE_NOT_AVAILABLE;
			goto error_exit;
		}
	}

	if (req_exec_msg_messagesendasync->message.size > MSG_MAX_MESSAGE_SIZE) {
		error = SA_AIS_ERR_TOO_BIG;
		goto error_exit;
	}

	if ((queue->priority[priority].queue_size -
	     queue->priority[priority].queue_used) < req_exec_msg_messagesendasync->message.size) {
		error = SA_AIS_ERR_QUEUE_FULL;
		goto error_exit;
	}

	message = malloc (sizeof (struct message_entry));
	if (message == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}
	memset (message, 0, sizeof (struct message_entry));
	memcpy (&message->message,
		&req_exec_msg_messagesendasync->message,
		sizeof (mar_msg_message_t));

	message->message.data = malloc (message->message.size);
	if (message->message.data == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}
	memset (message->message.data, 0, message->message.size);
	memcpy (message->message.data, (char *)(data), message->message.size);

	message->sender_id = 0;
	message->send_time = api->timer_time_get();

	if (list_empty (&queue->pending_head)) {
		list_add_tail (&message->queue_list,
			&queue->message_head);
		list_add_tail (&message->message_list,
			&queue->priority[(message->message.priority)].message_head);

		queue->priority[(message->message.priority)].queue_used += message->message.size;
		queue->priority[(message->message.priority)].number_of_messages += 1;
	}
	else {
		pending = list_entry (queue->pending_head.next,	struct pending_entry, pending_list);
		if (pending == NULL) {
			error = SA_AIS_ERR_LIBRARY;
			goto error_exit;
		}

		if (api->ipc_source_is_local (&pending->source)) {
			api->timer_delete (pending->timer_handle);
			msg_pending_deliver (pending, message);
		}

		list_del (&pending->pending_list);
		free (pending);
	}

	if (group != NULL) {
		group->next_queue = msg_group_member_next (group);
	}

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_messagesendasync->source))
	{
		res_lib_msg_messagesendasync.header.size =
			sizeof (struct res_lib_msg_messagesendasync);
		res_lib_msg_messagesendasync.header.id =
			MESSAGE_RES_MSG_MESSAGESENDASYNC;
		res_lib_msg_messagesendasync.header.error = error;

		api->ipc_response_send (
			req_exec_msg_messagesendasync->source.conn,
			&res_lib_msg_messagesendasync,
			sizeof (struct res_lib_msg_messagesendasync));

		if (req_exec_msg_messagesendasync->ack_flags & SA_MSG_MESSAGE_DELIVERED_ACK)
		{
			res_lib_msg_messagedelivered_callback.header.size =
				sizeof (struct res_lib_msg_messagedelivered_callback);
			res_lib_msg_messagedelivered_callback.header.id =
				MESSAGE_RES_MSG_MESSAGEDELIVERED_CALLBACK;
			res_lib_msg_messagedelivered_callback.header.error = error;

			res_lib_msg_messagedelivered_callback.invocation =
				req_exec_msg_messagesendasync->invocation;

			api->ipc_dispatch_send (
				req_exec_msg_messagesendasync->source.conn,
				&res_lib_msg_messagedelivered_callback,
				sizeof (struct res_lib_msg_messagedelivered_callback));
		}
	}

	/* DEBUG */
	if (error == SA_AIS_OK) {
		log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: error ok\n");
	}
	if (queue->open_flags & SA_MSG_QUEUE_RECEIVE_CALLBACK) {
		log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: flags ok\n");
	}

	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: nodeid=%x conn=%p\n",
		    (unsigned int)(queue->source.nodeid),
		    (void *)(queue->source.conn));

	if (api->ipc_source_is_local (&queue->source)) {
		log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: local ok\n");
	}
	/* ----- */

	if ((error == SA_AIS_OK) && (queue->open_flags & SA_MSG_QUEUE_RECEIVE_CALLBACK) &&
	    (api->ipc_source_is_local (&queue->source)))
	{
		res_lib_msg_messagereceived_callback.header.size =
			sizeof (struct res_lib_msg_messagereceived_callback);
		res_lib_msg_messagereceived_callback.header.id =
			MESSAGE_RES_MSG_MESSAGERECEIVED_CALLBACK;
		res_lib_msg_messagereceived_callback.header.error = SA_AIS_OK;
		res_lib_msg_messagereceived_callback.queue_handle = queue->queue_handle;

		api->ipc_dispatch_send (
			queue->source.conn,
			&res_lib_msg_messagereceived_callback,
			sizeof (struct res_lib_msg_messagereceived_callback));
	}

	if ((error != SA_AIS_OK) && (message != NULL)) {
		free (message->message.data);
		free (message);
	}
}

static void message_handler_req_exec_msg_messageget (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_messageget
		*req_exec_msg_messageget = msg;
	struct res_lib_msg_messageget res_lib_msg_messageget;
	SaAisErrorT error = SA_AIS_OK;

	struct iovec iov[2];

	struct queue_entry *queue = NULL;
	struct message_entry *message = NULL;
	struct pending_entry *pending = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgMessageGet\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(req_exec_msg_messageget->queue_name.value));

	queue = msg_queue_find_id (&queue_list_head,
		&req_exec_msg_messageget->queue_name,
		req_exec_msg_messageget->queue_id);
	if (queue == NULL) {
		error = SA_AIS_ERR_BAD_HANDLE;
		goto error_exit;
	}

	message = msg_queue_find_message (queue);
	if (message == NULL) {
		pending = malloc (sizeof (struct pending_entry));
		if (pending == NULL) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}
		memcpy (&pending->source,
			&req_exec_msg_messageget->source,
			sizeof (mar_message_source_t));
		memcpy (&pending->queue_name,
			&req_exec_msg_messageget->queue_name,
			sizeof (mar_name_t));

		list_add_tail (&pending->pending_list, &queue->pending_head);

		/* DEBUG */
		log_printf (LOGSYS_LEVEL_DEBUG, "\t pending { nodeid=%x conn=%p }\n",
			    (unsigned int)(pending->source.nodeid),
			    (void *)(pending->source.conn));

		if (api->ipc_source_is_local (&req_exec_msg_messageget->source)) {
			api->timer_add_duration (
				req_exec_msg_messageget->timeout, (void *)(pending),
				msg_messageget_timeout, &pending->timer_handle);
		}

		return;
	}

	res_lib_msg_messageget.send_time = message->send_time;
	res_lib_msg_messageget.sender_id = message->sender_id;

	list_del (&message->message_list);
	list_del (&message->queue_list);

	queue->priority[message->message.priority].queue_used -= message->message.size;
	queue->priority[message->message.priority].number_of_messages -= 1;

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_messageget->source))
	{
		res_lib_msg_messageget.header.size =
			sizeof (struct res_lib_msg_messageget);
		res_lib_msg_messageget.header.id =
			MESSAGE_RES_MSG_MESSAGEGET;
		res_lib_msg_messageget.header.error = error;

		iov[0].iov_base = (void *)&res_lib_msg_messageget;
		iov[0].iov_len = sizeof (struct res_lib_msg_messageget);

		if (error == SA_AIS_OK) {
			iov[1].iov_base = message->message.data;
			iov[1].iov_len = message->message.size;

			memcpy (&res_lib_msg_messageget.message,
				&message->message,
				sizeof (mar_msg_message_t));

			api->ipc_response_iov_send (req_exec_msg_messageget->source.conn, iov, 2);
		}
		else {
			api->ipc_response_iov_send (req_exec_msg_messageget->source.conn, iov, 1);
		}
	}

	free (message);
}

static void message_handler_req_exec_msg_messagedatafree (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_messagedatafree
		*req_exec_msg_messagedatafree = msg;
	struct res_lib_msg_messagedatafree res_lib_msg_messagedatafree;
	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgMessageDataFree\n");

	if (api->ipc_source_is_local (&req_exec_msg_messagedatafree->source))
	{
		res_lib_msg_messagedatafree.header.size =
			sizeof (struct res_lib_msg_messagedatafree);
		res_lib_msg_messagedatafree.header.id =
			MESSAGE_RES_MSG_MESSAGEDATAFREE;
		res_lib_msg_messagedatafree.header.error = error;

		api->ipc_response_send (
			req_exec_msg_messagedatafree->source.conn,
			&res_lib_msg_messagedatafree,
			sizeof (struct res_lib_msg_messagedatafree));
	}
}

static void message_handler_req_exec_msg_messagecancel (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_messagecancel
		*req_exec_msg_messagecancel = msg;
	struct res_lib_msg_messagecancel res_lib_msg_messagecancel;
	SaAisErrorT error = SA_AIS_OK;

	struct queue_entry *queue = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgMessageCancel\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(req_exec_msg_messagecancel->queue_name.value));

	queue = msg_queue_find_id (&queue_list_head,
		&req_exec_msg_messagecancel->queue_name,
		req_exec_msg_messagecancel->queue_id);
	if (queue == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (list_empty (&queue->pending_head)) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	msg_message_cancel (queue);

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_messagecancel->source))
	{
		res_lib_msg_messagecancel.header.size =
			sizeof (struct res_lib_msg_messagecancel);
		res_lib_msg_messagecancel.header.id =
			MESSAGE_RES_MSG_MESSAGECANCEL;
		res_lib_msg_messagecancel.header.error = error;

		api->ipc_response_send (
			req_exec_msg_messagecancel->source.conn,
			&res_lib_msg_messagecancel,
			sizeof (struct res_lib_msg_messagecancel));
	}
}

static void message_handler_req_exec_msg_messagesendreceive (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_messagesendreceive
		*req_exec_msg_messagesendreceive = msg;
	struct res_lib_msg_messagesendreceive res_lib_msg_messagesendreceive;
	struct res_lib_msg_messagereceived_callback res_lib_msg_messagereceived_callback;
	SaAisErrorT error = SA_AIS_OK;

	struct iovec iov;

	struct group_entry *group = NULL;
	struct queue_entry *queue = NULL;
	struct reply_entry *reply = NULL;
	struct message_entry *message = NULL;
	struct pending_entry *pending = NULL;

	char *data = ((char *)(req_exec_msg_messagesendreceive) +
		      sizeof (struct req_exec_msg_messagesendreceive));
	unsigned int priority = req_exec_msg_messagesendreceive->message.priority;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgMessageSendReceive\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t destination=%s\n",
		    (char *)(req_exec_msg_messagesendreceive->destination.value));

	group = msg_group_find (&group_list_head,
		&req_exec_msg_messagesendreceive->destination);
	if (group == NULL) {
		queue = msg_queue_find (&queue_list_head,
			&req_exec_msg_messagesendreceive->destination);
		if (queue == NULL) {
			error = SA_AIS_ERR_NOT_EXIST;
			goto error_exit;
		}
	}
	else {
		queue = group->next_queue;
		if (queue == NULL) {
			error = SA_AIS_ERR_QUEUE_NOT_AVAILABLE;
			goto error_exit;
		}
	}

	if (req_exec_msg_messagesendreceive->message.size > MSG_MAX_MESSAGE_SIZE) {
		error = SA_AIS_ERR_TOO_BIG;
		goto error_exit;
	}

	/*
	 * Verify that sufficient space is available for this message.
	 */
	if ((queue->priority[priority].queue_size -
	     queue->priority[priority].queue_used) < req_exec_msg_messagesendreceive->message.size) {
		error = SA_AIS_ERR_QUEUE_FULL;
		goto error_exit;
	}

	/*
	 * Create reply entry to map sender_id to ipc connection.
	 */
	reply = malloc (sizeof (struct reply_entry));
	if (reply == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}
	memset (reply, 0, sizeof (struct reply_entry));
	memcpy (&reply->source,
		&req_exec_msg_messagesendreceive->source,
		sizeof (mar_message_source_t));

	reply->sender_id = req_exec_msg_messagesendreceive->sender_id;
	reply->reply_size = req_exec_msg_messagesendreceive->reply_size;

	list_add (&reply->reply_list, &reply_list_head);

	/*
	 * Create message entry to be added to the queue.
	 */
	message = malloc (sizeof (struct message_entry));
	if (message == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}
	memset (message, 0, sizeof (struct message_entry));
	memcpy (&message->message,
		&req_exec_msg_messagesendreceive->message,
		sizeof (mar_msg_message_t));

	message->message.data = malloc (message->message.size);
	if (message->message.data == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}
	memset (message->message.data, 0, message->message.size);
	memcpy (message->message.data, (char *)(data), message->message.size);

	message->sender_id = req_exec_msg_messagesendreceive->sender_id;
	message->send_time = api->timer_time_get();

	if (list_empty (&queue->pending_head)) {
		list_add_tail (&message->queue_list,
			&queue->message_head);
		list_add_tail (&message->message_list,
			&queue->priority[(message->message.priority)].message_head);

		queue->priority[(message->message.priority)].queue_used += message->message.size;
		queue->priority[(message->message.priority)].number_of_messages += 1;
	}
	else {
		pending = list_entry (queue->pending_head.next,	struct pending_entry, pending_list);
		if (pending == NULL) {
			error = SA_AIS_ERR_LIBRARY;
			goto error_exit;
		}

		if (api->ipc_source_is_local (&pending->source)) {
			api->timer_delete (pending->timer_handle);
			msg_pending_deliver (pending, message);
		}

		list_del (&pending->pending_list);

		free (pending);
	}

	if (group != NULL) {
		group->next_queue = msg_group_member_next (group);
	}

	/*
	 * Create timer for this call to saMsgMessageSendReceive. If a reply is not
	 * received before this timer expires, SA_AIS_ERR_TIMEOUT will be returned
	 * to the caller. See msg_sendreceive_timeout function.
	 */
	if (api->ipc_source_is_local (&req_exec_msg_messagesendreceive->source)) {
		api->timer_add_duration (
			req_exec_msg_messagesendreceive->timeout, (void *)(reply),
			msg_sendreceive_timeout, &reply->timer_handle);
	}

	return;

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_messagesendreceive->source))
	{
		res_lib_msg_messagesendreceive.header.size =
			sizeof (struct res_lib_msg_messagesendreceive);
		res_lib_msg_messagesendreceive.header.id =
			MESSAGE_RES_MSG_MESSAGESENDRECEIVE;
		res_lib_msg_messagesendreceive.header.error = error;

		res_lib_msg_messagesendreceive.reply_time = 0;

		iov.iov_base = (void *)&res_lib_msg_messagesendreceive;
		iov.iov_len = sizeof (struct res_lib_msg_messagesendreceive);

		api->ipc_response_iov_send (
			req_exec_msg_messagesendreceive->source.conn, &iov, 1);
	}

	if ((error == SA_AIS_OK) && (queue->open_flags & SA_MSG_QUEUE_RECEIVE_CALLBACK) &&
	    (api->ipc_source_is_local (&queue->source)))
	{
		res_lib_msg_messagereceived_callback.header.size =
				sizeof (struct res_lib_msg_messagereceived_callback);
		res_lib_msg_messagereceived_callback.header.id =
			MESSAGE_RES_MSG_MESSAGERECEIVED_CALLBACK;
		res_lib_msg_messagereceived_callback.header.error = SA_AIS_OK;
		res_lib_msg_messagereceived_callback.queue_handle = queue->queue_handle;

		api->ipc_dispatch_send (
			queue->source.conn,
			&res_lib_msg_messagereceived_callback,
			sizeof (struct res_lib_msg_messagereceived_callback));
	}
}

static void message_handler_req_exec_msg_messagereply (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_messagereply
		*req_exec_msg_messagereply = msg;
	struct res_lib_msg_messagereply res_lib_msg_messagereply;
	struct res_lib_msg_messagesendreceive res_lib_msg_messagesendreceive;
	SaAisErrorT error = SA_AIS_OK;

	struct iovec iov[2];

	struct reply_entry *reply = NULL;

	char *data = ((char *)(req_exec_msg_messagereply) +
		      sizeof (struct req_exec_msg_messagereply));

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgMessageReply\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t sender_id=%llx\n",
		    (unsigned long long)(req_exec_msg_messagereply->sender_id));

	reply = msg_reply_find (&reply_list_head,
		req_exec_msg_messagereply->sender_id);
	if (reply == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if ((reply->reply_size != 0) &&
	    (reply->reply_size < req_exec_msg_messagereply->reply_message.size)) {
		error = SA_AIS_ERR_NO_SPACE;
		goto error_exit;
	}

	if (api->totem_nodeid_get() == (req_exec_msg_messagereply->sender_id >> 32))
	{
		api->timer_delete (reply->timer_handle);

		res_lib_msg_messagesendreceive.header.size =
			sizeof (struct res_lib_msg_messagesendreceive);
		res_lib_msg_messagesendreceive.header.id =
			MESSAGE_RES_MSG_MESSAGESENDRECEIVE;
		res_lib_msg_messagesendreceive.header.error = SA_AIS_OK;

		res_lib_msg_messagesendreceive.reply_time = api->timer_time_get();

		memcpy (&res_lib_msg_messagesendreceive.message,
			&req_exec_msg_messagereply->reply_message,
			sizeof (mar_msg_message_t));

		iov[0].iov_base = (void *)&res_lib_msg_messagesendreceive;
		iov[0].iov_len = sizeof (struct res_lib_msg_messagesendreceive);

		iov[1].iov_base = (void *)data;
		iov[1].iov_len = req_exec_msg_messagereply->reply_message.size;

		api->ipc_response_iov_send (reply->source.conn, iov, 2);
	}

	list_del (&reply->reply_list);

	free (reply);

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_messagereply->source))
	{
		res_lib_msg_messagereply.header.size =
			sizeof (struct res_lib_msg_messagereply);
		res_lib_msg_messagereply.header.id =
			MESSAGE_RES_MSG_MESSAGEREPLY;
		res_lib_msg_messagereply.header.error = error;

		api->ipc_response_send (
			req_exec_msg_messagereply->source.conn,
			&res_lib_msg_messagereply,
			sizeof (struct res_lib_msg_messagereply));
	}
}

static void message_handler_req_exec_msg_messagereplyasync (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_messagereplyasync
		*req_exec_msg_messagereplyasync = msg;
	struct res_lib_msg_messagereplyasync res_lib_msg_messagereplyasync;
	struct res_lib_msg_messagesendreceive res_lib_msg_messagesendreceive;
	struct res_lib_msg_messagedelivered_callback res_lib_msg_messagedelivered_callback;
	SaAisErrorT error = SA_AIS_OK;

	struct iovec iov[2];

	struct reply_entry *reply = NULL;

	char *data = ((char *)(req_exec_msg_messagereplyasync) +
		      sizeof (struct req_exec_msg_messagereplyasync));

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgMessageReplyAsync\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t sender_id=%llx\n",
		    (unsigned long long)(req_exec_msg_messagereplyasync->sender_id));

	reply = msg_reply_find (&reply_list_head,
		req_exec_msg_messagereplyasync->sender_id);
	if (reply == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if ((reply->reply_size != 0) &&
	    (reply->reply_size < req_exec_msg_messagereplyasync->reply_message.size)) {
		error = SA_AIS_ERR_NO_SPACE;
		goto error_exit;
	}

	if (api->totem_nodeid_get() == (req_exec_msg_messagereplyasync->sender_id >> 32))
	{
		api->timer_delete (reply->timer_handle);

		res_lib_msg_messagesendreceive.header.size =
			sizeof (struct res_lib_msg_messagesendreceive);
		res_lib_msg_messagesendreceive.header.id =
			MESSAGE_RES_MSG_MESSAGESENDRECEIVE;
		res_lib_msg_messagesendreceive.header.error = SA_AIS_OK;

		res_lib_msg_messagesendreceive.reply_time = api->timer_time_get();

		memcpy (&res_lib_msg_messagesendreceive.message,
			&req_exec_msg_messagereplyasync->reply_message,
			sizeof (mar_msg_message_t));

		iov[0].iov_base = (void *)&res_lib_msg_messagesendreceive;
		iov[0].iov_len = sizeof (struct res_lib_msg_messagesendreceive);

		iov[1].iov_base = (void *)data;
		iov[1].iov_len = req_exec_msg_messagereplyasync->reply_message.size;

		api->ipc_response_iov_send (reply->source.conn, iov, 2);
	}

	list_del (&reply->reply_list);

	free (reply);

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_messagereplyasync->source))
	{
		res_lib_msg_messagereplyasync.header.size =
			sizeof (struct res_lib_msg_messagereplyasync);
		res_lib_msg_messagereplyasync.header.id =
			MESSAGE_RES_MSG_MESSAGEREPLYASYNC;
		res_lib_msg_messagereplyasync.header.error = error;

		api->ipc_response_send (
			req_exec_msg_messagereplyasync->source.conn,
			&res_lib_msg_messagereplyasync,
			sizeof (struct res_lib_msg_messagereplyasync));

		if ((error == SA_AIS_OK) &&
		    (req_exec_msg_messagereplyasync->ack_flags & SA_MSG_MESSAGE_DELIVERED_ACK))
		{
			res_lib_msg_messagedelivered_callback.header.size =
				sizeof (struct res_lib_msg_messagedelivered_callback);
			res_lib_msg_messagedelivered_callback.header.id =
				MESSAGE_RES_MSG_MESSAGEDELIVERED_CALLBACK;
			res_lib_msg_messagedelivered_callback.header.error = error;

			res_lib_msg_messagedelivered_callback.invocation =
				req_exec_msg_messagereplyasync->invocation;

			api->ipc_dispatch_send (
				req_exec_msg_messagereplyasync->source.conn,
				&res_lib_msg_messagedelivered_callback,
				sizeof (struct res_lib_msg_messagedelivered_callback));
		}
	}
}

static void message_handler_req_exec_msg_queuecapacitythresholdsset (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_queuecapacitythresholdsset
		*req_exec_msg_queuecapacitythresholdsset = msg;
	struct res_lib_msg_queuecapacitythresholdsset res_lib_msg_queuecapacitythresholdsset;
	SaAisErrorT error = SA_AIS_OK;

	struct queue_entry *queue = NULL;
	int i;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgQueueCapacityThresholdsSet\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(req_exec_msg_queuecapacitythresholdsset->queue_name.value));

	queue = msg_queue_find_id (&queue_list_head,
		&req_exec_msg_queuecapacitythresholdsset->queue_name,
		req_exec_msg_queuecapacitythresholdsset->queue_id);
	if (queue == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
		queue->priority[i].capacity_reached =
			req_exec_msg_queuecapacitythresholdsset->thresholds.capacity_reached[i];
		queue->priority[i].capacity_available =
			req_exec_msg_queuecapacitythresholdsset->thresholds.capacity_available[i];
	}

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_queuecapacitythresholdsset->source))
	{
		res_lib_msg_queuecapacitythresholdsset.header.size =
			sizeof (struct res_lib_msg_queuecapacitythresholdsset);
		res_lib_msg_queuecapacitythresholdsset.header.id =
			MESSAGE_RES_MSG_QUEUECAPACITYTHRESHOLDSSET;
		res_lib_msg_queuecapacitythresholdsset.header.error = error;

		api->ipc_response_send (
			req_exec_msg_queuecapacitythresholdsset->source.conn,
			&res_lib_msg_queuecapacitythresholdsset,
			sizeof (struct res_lib_msg_queuecapacitythresholdsset));
	}
}

static void message_handler_req_exec_msg_queuecapacitythresholdsget (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_queuecapacitythresholdsget
		*req_exec_msg_queuecapacitythresholdsget = msg;
	struct res_lib_msg_queuecapacitythresholdsget res_lib_msg_queuecapacitythresholdsget;
	SaAisErrorT error = SA_AIS_OK;

	struct queue_entry *queue = NULL;
	int i;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgQueueCapacityThresholdsGet\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(req_exec_msg_queuecapacitythresholdsget->queue_name.value));

	queue = msg_queue_find_id (&queue_list_head,
		&req_exec_msg_queuecapacitythresholdsget->queue_name,
		req_exec_msg_queuecapacitythresholdsget->queue_id);
	if (queue == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
		res_lib_msg_queuecapacitythresholdsget.thresholds.capacity_reached[i] =
			queue->priority[i].capacity_reached;
		res_lib_msg_queuecapacitythresholdsget.thresholds.capacity_available[i] =
			queue->priority[i].capacity_available;
	}

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_queuecapacitythresholdsget->source))
	{
		res_lib_msg_queuecapacitythresholdsget.header.size =
			sizeof (struct res_lib_msg_queuecapacitythresholdsget);
		res_lib_msg_queuecapacitythresholdsget.header.id =
			MESSAGE_RES_MSG_QUEUECAPACITYTHRESHOLDSGET;
		res_lib_msg_queuecapacitythresholdsget.header.error = error;

		api->ipc_response_send (
			req_exec_msg_queuecapacitythresholdsget->source.conn,
			&res_lib_msg_queuecapacitythresholdsget,
			sizeof (struct res_lib_msg_queuecapacitythresholdsget));
	}
}

static void message_handler_req_exec_msg_metadatasizeget (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_metadatasizeget
		*req_exec_msg_metadatasizeget = msg;
	struct res_lib_msg_metadatasizeget res_lib_msg_metadatasizeget;
	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgMetadataSizeGet\n");

	if (api->ipc_source_is_local (&req_exec_msg_metadatasizeget->source))
	{
		res_lib_msg_metadatasizeget.header.size =
			sizeof (struct res_lib_msg_metadatasizeget);
		res_lib_msg_metadatasizeget.header.id =
			MESSAGE_RES_MSG_METADATASIZEGET;
		res_lib_msg_metadatasizeget.header.error = error;

		api->ipc_response_send (
			req_exec_msg_metadatasizeget->source.conn,
			&res_lib_msg_metadatasizeget,
			sizeof (struct res_lib_msg_metadatasizeget));
	}
}

static void message_handler_req_exec_msg_limitget (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_limitget
		*req_exec_msg_limitget = msg;
	struct res_lib_msg_limitget res_lib_msg_limitget;
	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saMsgLimitGet\n");

	if (api->ipc_source_is_local (&req_exec_msg_limitget->source))
	{
		res_lib_msg_limitget.header.size =
			sizeof (struct res_lib_msg_limitget);
		res_lib_msg_limitget.header.id =
			MESSAGE_RES_MSG_LIMITGET;
		res_lib_msg_limitget.header.error = error;

		api->ipc_response_send (
			req_exec_msg_limitget->source.conn,
			&res_lib_msg_limitget,
			sizeof (struct res_lib_msg_limitget));
	}
}

static void message_handler_req_exec_msg_sync_queue (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_sync_queue
		*req_exec_msg_sync_queue = msg;
	struct queue_entry *queue = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: sync_queue\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(&req_exec_msg_sync_queue->queue_name.value));

	if (memcmp (&req_exec_msg_sync_queue->ring_id,
		    &saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	queue = msg_queue_find_id (&sync_queue_list_head,
		&req_exec_msg_sync_queue->queue_name,
		req_exec_msg_sync_queue->queue_id);

	/*
	 * This queue should not exist.
	 */
	assert (queue == NULL);

	queue = malloc (sizeof (struct queue_entry));
	if (queue == NULL) {
		corosync_fatal_error (COROSYNC_OUT_OF_MEMORY);
	}
	memset (queue, 0, sizeof (struct queue_entry));

	memcpy (&queue->queue_name,
		&req_exec_msg_sync_queue->queue_name,
		sizeof (mar_name_t));
	memcpy (&queue->source,
		&req_exec_msg_sync_queue->source,
		sizeof (mar_message_source_t));
	memcpy (&queue->create_attrs,
		&req_exec_msg_sync_queue->create_attrs,
		sizeof (mar_msg_queue_creation_attributes_t));

	queue->queue_id = req_exec_msg_sync_queue->queue_id;
	queue->close_time = req_exec_msg_sync_queue->close_time;
	queue->unlink_flag = req_exec_msg_sync_queue->unlink_flag;
	queue->open_flags = req_exec_msg_sync_queue->open_flags;
	queue->change_flag = req_exec_msg_sync_queue->change_flag;
	queue->queue_handle = req_exec_msg_sync_queue->queue_handle;

	msg_queue_priority_area_init (queue);

	list_init (&queue->group_list);
	list_init (&queue->queue_list);
	list_init (&queue->message_head);
	list_init (&queue->pending_head);

	list_add_tail (&queue->queue_list, &sync_queue_list_head);

	sync_queue_count += 1;

	if (queue->queue_id >= global_queue_id) {
		global_queue_id = queue->queue_id + 1;
	}

	return;
}

static void message_handler_req_exec_msg_sync_queue_message (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_sync_queue_message
		*req_exec_msg_sync_queue_message = msg;
	struct queue_entry *queue = NULL;
	struct message_entry *message = NULL;

	char *data = ((char *)(req_exec_msg_sync_queue_message) +
		      sizeof (struct req_exec_msg_sync_queue_message));

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: sync_queue_message\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(&req_exec_msg_sync_queue_message->queue_name.value));

	if (memcmp (&req_exec_msg_sync_queue_message->ring_id,
		    &saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	queue = msg_queue_find_id (&sync_queue_list_head,
		&req_exec_msg_sync_queue_message->queue_name,
		req_exec_msg_sync_queue_message->queue_id);

	/*
	 * This queue must exist.
	 */
	assert (queue != NULL);

	message = malloc (sizeof (struct message_entry));
	if (message == NULL) {
		corosync_fatal_error (COROSYNC_OUT_OF_MEMORY);
	}
	memset (message, 0, sizeof (struct message_entry));
	memcpy (&message->message,
		&req_exec_msg_sync_queue_message->message,
		sizeof (mar_msg_message_t));

	message->message.data = malloc (message->message.size);
	if (message->message.data == NULL) {
		corosync_fatal_error (COROSYNC_OUT_OF_MEMORY);
	}
	memset (message->message.data, 0, message->message.size);
	memcpy (message->message.data, (char *)(data), message->message.size);

	message->sender_id = req_exec_msg_sync_queue_message->sender_id;
	message->send_time = req_exec_msg_sync_queue_message->send_time;

	list_add_tail (&message->queue_list, &queue->message_head);
	list_add_tail (&message->message_list, &queue->priority[(message->message.priority)].message_head);

	queue->priority[(message->message.priority)].queue_used += message->message.size;
	queue->priority[(message->message.priority)].number_of_messages += 1;

	return;
}

static void message_handler_req_exec_msg_sync_queue_refcount (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_sync_queue_refcount
		*req_exec_msg_sync_queue_refcount = msg;
	struct queue_entry *queue = NULL;

	unsigned int i;
	unsigned int j;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: sync_queue_refcount\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(&req_exec_msg_sync_queue_refcount->queue_name.value));

	if (memcmp (&req_exec_msg_sync_queue_refcount->ring_id,
		    &saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	queue = msg_queue_find_id (&sync_queue_list_head,
		&req_exec_msg_sync_queue_refcount->queue_name,
		req_exec_msg_sync_queue_refcount->queue_id);

	/*
	 * This queue must exist.
	 */
	assert (queue != NULL);

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		if (req_exec_msg_sync_queue_refcount->refcount_set[i].nodeid == 0) {
			break;
		}

		if (msg_find_member_nodeid (req_exec_msg_sync_queue_refcount->refcount_set[i].nodeid) == 0) {
			continue;
		}

		for (j = 0; j < PROCESSOR_COUNT_MAX; j++) {
			if (queue->refcount_set[j].nodeid == 0)
			{
				queue->refcount_set[j].nodeid =
					req_exec_msg_sync_queue_refcount->refcount_set[i].nodeid;
				queue->refcount_set[j].refcount =
					req_exec_msg_sync_queue_refcount->refcount_set[i].refcount;
				break;
			}

			if (req_exec_msg_sync_queue_refcount->refcount_set[i].nodeid == queue->refcount_set[j].nodeid)
			{
				queue->refcount_set[j].refcount +=
					req_exec_msg_sync_queue_refcount->refcount_set[i].refcount;
				break;
			}
		}
	}

	msg_sync_refcount_calculate (queue);

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "\t refcount=%u\n",
		    (unsigned int)(queue->refcount));

	return;
}

static void message_handler_req_exec_msg_sync_group (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_sync_group
		*req_exec_msg_sync_group = msg;
	struct group_entry *group = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: sync_group\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t group=%s \n",
		    (char *)(&req_exec_msg_sync_group->group_name.value));

	if (memcmp (&req_exec_msg_sync_group->ring_id,
		    &saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	group = msg_group_find (&sync_group_list_head,
		&req_exec_msg_sync_group->group_name);

	/*
	 * This group should not exist.
	 */
	assert (group == NULL);

	group = malloc (sizeof (struct group_entry));
	if (group == NULL) {
		corosync_fatal_error (COROSYNC_OUT_OF_MEMORY);
	}
	memset (group, 0, sizeof (struct group_entry));
	memcpy (&group->group_name,
		&req_exec_msg_sync_group->group_name,
		sizeof (mar_name_t));

	group->policy = req_exec_msg_sync_group->policy;

	list_init (&group->queue_head);
	list_init (&group->group_list);

	list_add_tail (&group->group_list, &sync_group_list_head);

	sync_group_count += 1;

	return;
}

static void message_handler_req_exec_msg_sync_group_member (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_sync_group_member
		*req_exec_msg_sync_group_member = msg;
	struct group_entry *group = NULL;
	struct queue_entry *queue = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: sync_group_member\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t group=%s queue=%s\n",
		    (char *)(&req_exec_msg_sync_group_member->group_name.value),
		    (char *)(&req_exec_msg_sync_group_member->queue_name.value));

	if (memcmp (&req_exec_msg_sync_group_member->ring_id,
		    &saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	group = msg_group_find (&sync_group_list_head,
		&req_exec_msg_sync_group_member->group_name);

	assert (group != NULL);

	queue = msg_queue_find_id (&sync_queue_list_head,
		&req_exec_msg_sync_group_member->queue_name,
		req_exec_msg_sync_group_member->queue_id);

	assert (queue != NULL);

	queue->group = group;

	list_init (&queue->group_list);
	list_add_tail (&queue->group_list, &group->queue_head);

	group->member_count += 1;

	return;
}

static void message_handler_req_exec_msg_sync_reply (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_sync_reply
		*req_exec_msg_sync_reply = msg;
	struct reply_entry *reply = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: sync_reply\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t sender_id=%llx\n",
		    (unsigned long long)(req_exec_msg_sync_reply->sender_id));

	if (memcmp (&req_exec_msg_sync_reply->ring_id,
		    &saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	reply = msg_reply_find (&sync_reply_list_head,
		req_exec_msg_sync_reply->sender_id);

	/*
	 * This reply should not exist.
	 */
	assert (reply == NULL);

	reply = malloc (sizeof (struct reply_entry));
	if (reply == NULL) {
		corosync_fatal_error (COROSYNC_OUT_OF_MEMORY);
	}
	memset (reply, 0, sizeof (struct reply_entry));
	memcpy (&reply->source,
		&req_exec_msg_sync_reply->source,
		sizeof (mar_message_source_t));

	reply->sender_id = req_exec_msg_sync_reply->sender_id;

	list_init (&reply->reply_list);
	list_add_tail (&reply->reply_list, &sync_reply_list_head);

	return;
}

static void message_handler_req_exec_msg_queue_timeout (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_queue_timeout
		*req_exec_msg_queue_timeout = msg;
	struct queue_entry *queue = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: queue_timeout\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(req_exec_msg_queue_timeout->queue_name.value));

	queue = msg_queue_find (&queue_list_head,
		&req_exec_msg_queue_timeout->queue_name);

	assert (queue != NULL);

	msg_queue_release (queue);

	return;
}

static void message_handler_req_exec_msg_messageget_timeout (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_messageget_timeout
		*req_exec_msg_messageget_timeout = msg;
	struct pending_entry *pending = NULL;
	struct queue_entry *queue = NULL;
	struct iovec iov;

	struct res_lib_msg_messageget res_lib_msg_messageget;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: messageget_timeout\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(req_exec_msg_messageget_timeout->queue_name.value));

	queue = msg_queue_find (&queue_list_head,
		&req_exec_msg_messageget_timeout->queue_name);

	assert (queue != NULL);

	pending = msg_queue_find_pending (queue,
		&req_exec_msg_messageget_timeout->source);

	assert (pending != NULL);

	if (api->ipc_source_is_local (&req_exec_msg_messageget_timeout->source))
	{
		res_lib_msg_messageget.header.size =
			sizeof (struct res_lib_msg_messageget);
		res_lib_msg_messageget.header.id =
			MESSAGE_RES_MSG_MESSAGEGET;
		res_lib_msg_messageget.header.error = SA_AIS_ERR_TIMEOUT;

		iov.iov_base = (void *)&res_lib_msg_messageget;
		iov.iov_len = sizeof (struct res_lib_msg_messageget);

		api->ipc_response_iov_send (
			req_exec_msg_messageget_timeout->source.conn, &iov, 1);
	}

	msg_pending_release (pending);

	return;
}

static void message_handler_req_exec_msg_sendreceive_timeout (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_msg_sendreceive_timeout
		*req_exec_msg_sendreceive_timeout = msg;
	struct reply_entry *reply = NULL;
	struct iovec iov;

	struct res_lib_msg_messagesendreceive res_lib_msg_messagesendreceive;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: sendreceive_timeout\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t sender_id=%llx\n",
		    (unsigned long long)(req_exec_msg_sendreceive_timeout->sender_id));

	reply = msg_reply_find (&reply_list_head,
		req_exec_msg_sendreceive_timeout->sender_id);

	assert (reply != NULL);

	if (api->ipc_source_is_local (&req_exec_msg_sendreceive_timeout->source))
	{
		res_lib_msg_messagesendreceive.header.size =
			sizeof (struct res_lib_msg_messagesendreceive);
		res_lib_msg_messagesendreceive.header.id =
			MESSAGE_RES_MSG_MESSAGESENDRECEIVE;
		res_lib_msg_messagesendreceive.header.error = SA_AIS_ERR_TIMEOUT;

		/* ! */

		iov.iov_base = (void *)&res_lib_msg_messagesendreceive;
		iov.iov_len = sizeof (struct res_lib_msg_messagesendreceive);

		api->ipc_response_iov_send (
			req_exec_msg_sendreceive_timeout->source.conn, &iov, 1);
	}

	msg_reply_release (reply);

	return;
}

static void message_handler_req_lib_msg_queueopen (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_queueopen *req_lib_msg_queueopen = msg;
	struct req_exec_msg_queueopen req_exec_msg_queueopen;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgQueueOpen\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(req_lib_msg_queueopen->queue_name.value));

	req_exec_msg_queueopen.header.size =
		sizeof (struct req_exec_msg_queueopen);
	req_exec_msg_queueopen.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEOPEN);

	api->ipc_source_set (&req_exec_msg_queueopen.source, conn);

	req_exec_msg_queueopen.queue_handle =
		req_lib_msg_queueopen->queue_handle;
	req_exec_msg_queueopen.open_flags =
		req_lib_msg_queueopen->open_flags;
	req_exec_msg_queueopen.create_attrs_flag =
		req_lib_msg_queueopen->create_attrs_flag;
	req_exec_msg_queueopen.timeout =
		req_lib_msg_queueopen->timeout;

	memcpy (&req_exec_msg_queueopen.queue_name,
		&req_lib_msg_queueopen->queue_name,
		sizeof (mar_name_t));
	memcpy (&req_exec_msg_queueopen.create_attrs,
		&req_lib_msg_queueopen->create_attrs,
		sizeof (mar_msg_queue_creation_attributes_t));

	iovec.iov_base = (void *)&req_exec_msg_queueopen;
	iovec.iov_len = sizeof (req_exec_msg_queueopen);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queueopenasync (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_queueopenasync *req_lib_msg_queueopenasync = msg;
	struct req_exec_msg_queueopenasync req_exec_msg_queueopenasync;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgQueueOpenAsync\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(req_lib_msg_queueopenasync->queue_name.value));

	req_exec_msg_queueopenasync.header.size =
		sizeof (struct req_exec_msg_queueopenasync);
	req_exec_msg_queueopenasync.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEOPENASYNC);

	api->ipc_source_set (&req_exec_msg_queueopenasync.source, conn);

	req_exec_msg_queueopenasync.queue_handle =
		req_lib_msg_queueopenasync->queue_handle;
	req_exec_msg_queueopenasync.open_flags =
		req_lib_msg_queueopenasync->open_flags;
	req_exec_msg_queueopenasync.create_attrs_flag =
		req_lib_msg_queueopenasync->create_attrs_flag;
	req_exec_msg_queueopenasync.invocation =
		req_lib_msg_queueopenasync->invocation;

	memcpy (&req_exec_msg_queueopenasync.queue_name,
		&req_lib_msg_queueopenasync->queue_name,
		sizeof (mar_name_t));
	memcpy (&req_exec_msg_queueopenasync.create_attrs,
		&req_lib_msg_queueopenasync->create_attrs,
		sizeof (mar_msg_queue_creation_attributes_t));

	iovec.iov_base = (void *)&req_exec_msg_queueopenasync;
	iovec.iov_len = sizeof (req_exec_msg_queueopenasync);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queueclose (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_queueclose *req_lib_msg_queueclose = msg;
	struct req_exec_msg_queueclose req_exec_msg_queueclose;
	struct iovec iovec;

	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgQueueClose\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(req_lib_msg_queueclose->queue_name.value));

	req_exec_msg_queueclose.header.size =
		sizeof (struct req_exec_msg_queueclose);
	req_exec_msg_queueclose.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUECLOSE);

	api->ipc_source_set (&req_exec_msg_queueclose.source, conn);

	req_exec_msg_queueclose.queue_id =
		req_lib_msg_queueclose->queue_id;

	memcpy (&req_exec_msg_queueclose.queue_name,
		&req_lib_msg_queueclose->queue_name,
		sizeof (mar_name_t));

	iovec.iov_base = (void *)&req_exec_msg_queueclose;
	iovec.iov_len = sizeof (req_exec_msg_queueclose);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuestatusget (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_queuestatusget *req_lib_msg_queuestatusget = msg;
	struct req_exec_msg_queuestatusget req_exec_msg_queuestatusget;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgQueueStatusGet\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t queue=%s\n",
		    (char *)(req_lib_msg_queuestatusget->queue_name.value));

	req_exec_msg_queuestatusget.header.size =
		sizeof (struct req_exec_msg_queuestatusget);
	req_exec_msg_queuestatusget.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUESTATUSGET);

	api->ipc_source_set (&req_exec_msg_queuestatusget.source, conn);

	memcpy (&req_exec_msg_queuestatusget.queue_name,
		&req_lib_msg_queuestatusget->queue_name,
		sizeof (mar_name_t));

	iovec.iov_base = (void *)&req_exec_msg_queuestatusget;
	iovec.iov_len = sizeof (req_exec_msg_queuestatusget);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queueretentiontimeset (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_queueretentiontimeset *req_lib_msg_queueretentiontimeset = msg;
	struct req_exec_msg_queueretentiontimeset req_exec_msg_queueretentiontimeset;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgQueueRetentionTimeSet\n");

	req_exec_msg_queueretentiontimeset.header.size =
		sizeof (struct req_exec_msg_queueretentiontimeset);
	req_exec_msg_queueretentiontimeset.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUERETENTIONTIMESET);

	api->ipc_source_set (&req_exec_msg_queueretentiontimeset.source, conn);

	memcpy (&req_exec_msg_queueretentiontimeset.queue_name,
		&req_lib_msg_queueretentiontimeset->queue_name,
		sizeof (mar_name_t));

	iovec.iov_base = (void *)&req_exec_msg_queueretentiontimeset;
	iovec.iov_len = sizeof (req_exec_msg_queueretentiontimeset);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queueunlink (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_queueunlink *req_lib_msg_queueunlink = msg;
	struct req_exec_msg_queueunlink req_exec_msg_queueunlink;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgQueueUnlink\n");

	req_exec_msg_queueunlink.header.size =
		sizeof (struct req_exec_msg_queueunlink);
	req_exec_msg_queueunlink.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEUNLINK);

	api->ipc_source_set (&req_exec_msg_queueunlink.source, conn);

	memcpy (&req_exec_msg_queueunlink.queue_name,
		&req_lib_msg_queueunlink->queue_name,
		sizeof (mar_name_t));

	iovec.iov_base = (void *)&req_exec_msg_queueunlink;
	iovec.iov_len = sizeof (req_exec_msg_queueunlink);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuegroupcreate (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_queuegroupcreate *req_lib_msg_queuegroupcreate = msg;
	struct req_exec_msg_queuegroupcreate req_exec_msg_queuegroupcreate;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgQueueGroupCreate\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t group=%s\n",
		    (char *)(req_lib_msg_queuegroupcreate->group_name.value));

	req_exec_msg_queuegroupcreate.header.size =
		sizeof (struct req_exec_msg_queuegroupcreate);
	req_exec_msg_queuegroupcreate.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPCREATE);

	api->ipc_source_set (&req_exec_msg_queuegroupcreate.source, conn);

	req_exec_msg_queuegroupcreate.policy =
		req_lib_msg_queuegroupcreate->policy;

	memcpy (&req_exec_msg_queuegroupcreate.group_name,
		&req_lib_msg_queuegroupcreate->group_name,
		sizeof (mar_name_t));

	iovec.iov_base = (void *)&req_exec_msg_queuegroupcreate;
	iovec.iov_len = sizeof (req_exec_msg_queuegroupcreate);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuegroupinsert (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_queuegroupinsert *req_lib_msg_queuegroupinsert = msg;
	struct req_exec_msg_queuegroupinsert req_exec_msg_queuegroupinsert;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgQueueGroupInsert\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t group=%s queue=%s\n",
		    (char *)(req_lib_msg_queuegroupinsert->group_name.value),
		    (char *)(req_lib_msg_queuegroupinsert->queue_name.value));

	req_exec_msg_queuegroupinsert.header.size =
		sizeof (struct req_exec_msg_queuegroupinsert);
	req_exec_msg_queuegroupinsert.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPINSERT);

	api->ipc_source_set (&req_exec_msg_queuegroupinsert.source, conn);

	memcpy (&req_exec_msg_queuegroupinsert.group_name,
		&req_lib_msg_queuegroupinsert->group_name,
		sizeof (mar_name_t));
	memcpy (&req_exec_msg_queuegroupinsert.queue_name,
		&req_lib_msg_queuegroupinsert->queue_name,
		sizeof (mar_name_t));

	iovec.iov_base = (void *)&req_exec_msg_queuegroupinsert;
	iovec.iov_len = sizeof (req_exec_msg_queuegroupinsert);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuegroupremove (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_queuegroupremove *req_lib_msg_queuegroupremove = msg;
	struct req_exec_msg_queuegroupremove req_exec_msg_queuegroupremove;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgQueueGroupRemove\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t group=%s queue=%s\n",
		    (char *)(req_lib_msg_queuegroupremove->group_name.value),
		    (char *)(req_lib_msg_queuegroupremove->queue_name.value));

	req_exec_msg_queuegroupremove.header.size =
		sizeof (struct req_exec_msg_queuegroupremove);
	req_exec_msg_queuegroupremove.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPREMOVE);

	api->ipc_source_set (&req_exec_msg_queuegroupremove.source, conn);

	memcpy (&req_exec_msg_queuegroupremove.group_name,
		&req_lib_msg_queuegroupremove->group_name,
		sizeof (mar_name_t));
	memcpy (&req_exec_msg_queuegroupremove.queue_name,
		&req_lib_msg_queuegroupremove->queue_name,
		sizeof (mar_name_t));

	iovec.iov_base = (void *)&req_exec_msg_queuegroupremove;
	iovec.iov_len = sizeof (req_exec_msg_queuegroupremove);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuegroupdelete (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_queuegroupdelete *req_lib_msg_queuegroupdelete = msg;
	struct req_exec_msg_queuegroupdelete req_exec_msg_queuegroupdelete;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgQueueGroupDelete\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t group=%s\n",
		    (char *)(req_lib_msg_queuegroupdelete->group_name.value));

	req_exec_msg_queuegroupdelete.header.size =
		sizeof (struct req_exec_msg_queuegroupdelete);
	req_exec_msg_queuegroupdelete.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPDELETE);

	api->ipc_source_set (&req_exec_msg_queuegroupdelete.source, conn);

	memcpy (&req_exec_msg_queuegroupdelete.group_name,
		&req_lib_msg_queuegroupdelete->group_name,
		sizeof (mar_name_t));

	iovec.iov_base = (void *)&req_exec_msg_queuegroupdelete;
	iovec.iov_len = sizeof (req_exec_msg_queuegroupdelete);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuegrouptrack (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_queuegrouptrack *req_lib_msg_queuegrouptrack = msg;
	struct res_lib_msg_queuegrouptrack res_lib_msg_queuegrouptrack;
	struct res_lib_msg_queuegrouptrack_callback res_lib_msg_queuegrouptrack_callback;
	SaAisErrorT error = SA_AIS_OK;

	struct group_entry *group = NULL;
	struct track_entry *track = NULL;
	struct iovec iov[2];

	mar_msg_queue_group_notification_t notification[MSG_MAX_NUM_QUEUES_PER_GROUP];
	unsigned int count = 0;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgQueueGroupTrack\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t group=%s\n",
		    (char *)(req_lib_msg_queuegrouptrack->group_name.value));

	group = msg_group_find (&group_list_head,
		&req_lib_msg_queuegrouptrack->group_name);
	if (group == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if ((req_lib_msg_queuegrouptrack->track_flags & SA_TRACK_CURRENT) &&
	    (req_lib_msg_queuegrouptrack->buffer_flag == 0))
	{
		count = msg_group_track_current (group, notification);

		res_lib_msg_queuegrouptrack_callback.header.size =
			sizeof (struct res_lib_msg_queuegrouptrack_callback) +
			sizeof (mar_msg_queue_group_notification_t) * count;
		res_lib_msg_queuegrouptrack_callback.header.id =
			MESSAGE_RES_MSG_QUEUEGROUPTRACK_CALLBACK;
		res_lib_msg_queuegrouptrack_callback.header.error = error;

		memcpy (&res_lib_msg_queuegrouptrack_callback.group_name,
			&group->group_name, sizeof (mar_name_t));

		res_lib_msg_queuegrouptrack_callback.number_of_items = count;
		res_lib_msg_queuegrouptrack_callback.queue_group_policy = group->policy;
		res_lib_msg_queuegrouptrack_callback.member_count = group->member_count;

		iov[0].iov_base = (void *)&res_lib_msg_queuegrouptrack_callback;
		iov[0].iov_len = sizeof (struct res_lib_msg_queuegrouptrack_callback);

		iov[1].iov_base = (void *)(notification);
		iov[1].iov_len = sizeof (mar_msg_queue_group_notification_t) * count;

		api->ipc_dispatch_iov_send (conn, iov, 2);
	}

	if ((req_lib_msg_queuegrouptrack->track_flags & SA_TRACK_CHANGES) ||
	    (req_lib_msg_queuegrouptrack->track_flags & SA_TRACK_CHANGES_ONLY))
	{
		track = msg_track_find (&track_list_head,
			&group->group_name, conn);
		if (track == NULL) {
			track = malloc (sizeof (struct track_entry));
			if (track == NULL) {
				error = SA_AIS_ERR_NO_MEMORY;
				goto error_exit;
			}
			memset (track, 0, sizeof (struct track_entry));
			memcpy (&track->group_name,
				&group->group_name, sizeof (mar_name_t));

			api->ipc_source_set (&track->source, conn);

			list_init (&track->track_list);
			list_add_tail (&track->track_list, &track_list_head);
		}
		track->track_flags = req_lib_msg_queuegrouptrack->track_flags;
	}

error_exit:
	if ((req_lib_msg_queuegrouptrack->track_flags & SA_TRACK_CURRENT) &&
	    (req_lib_msg_queuegrouptrack->buffer_flag == 1) && (error == SA_AIS_OK))
	{
		count = msg_group_track_current (group, notification);

		res_lib_msg_queuegrouptrack.header.size =
			sizeof (struct res_lib_msg_queuegrouptrack) +
			sizeof (mar_msg_queue_group_notification_t) * count;
		res_lib_msg_queuegrouptrack.header.id =
			MESSAGE_RES_MSG_QUEUEGROUPTRACK;
		res_lib_msg_queuegrouptrack.header.error = error;

		memcpy (&res_lib_msg_queuegrouptrack_callback.group_name,
				&group->group_name, sizeof (mar_name_t));

		res_lib_msg_queuegrouptrack.number_of_items = count;
		res_lib_msg_queuegrouptrack.queue_group_policy = group->policy;
		res_lib_msg_queuegrouptrack_callback.member_count = group->member_count;

		iov[0].iov_base = (void *)&res_lib_msg_queuegrouptrack;
		iov[0].iov_len = sizeof (struct res_lib_msg_queuegrouptrack);

		iov[1].iov_base = (void *)(notification);
		iov[1].iov_len = sizeof (mar_msg_queue_group_notification_t) * count;

		api->ipc_response_iov_send (conn, iov, 2);
	}
	else {
		res_lib_msg_queuegrouptrack.header.size =
			sizeof (struct res_lib_msg_queuegrouptrack);
		res_lib_msg_queuegrouptrack.header.id =
			MESSAGE_RES_MSG_QUEUEGROUPTRACK;
		res_lib_msg_queuegrouptrack.header.error = error;

		api->ipc_response_send (conn,
			&res_lib_msg_queuegrouptrack,
			sizeof (struct res_lib_msg_queuegrouptrack));
	}
}

static void message_handler_req_lib_msg_queuegrouptrackstop (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_queuegrouptrackstop *req_lib_msg_queuegrouptrackstop = msg;
	struct res_lib_msg_queuegrouptrackstop res_lib_msg_queuegrouptrackstop;
	SaAisErrorT error = SA_AIS_OK;

	struct group_entry *group = NULL;
	struct track_entry *track = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgQueueGroupTrackStop\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "\t group=%s\n",
		    (char *)(req_lib_msg_queuegrouptrackstop->group_name.value));

	group = msg_group_find (&group_list_head,
		&req_lib_msg_queuegrouptrackstop->group_name);
	if (group == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	track = msg_track_find (&track_list_head,
		&req_lib_msg_queuegrouptrackstop->group_name, conn);

	if (track == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	list_del (&track->track_list);

	free (track);

error_exit:
	res_lib_msg_queuegrouptrackstop.header.size =
		sizeof (struct res_lib_msg_queuegrouptrackstop);
	res_lib_msg_queuegrouptrackstop.header.id =
		MESSAGE_RES_MSG_QUEUEGROUPTRACKSTOP;
	res_lib_msg_queuegrouptrackstop.header.error = error;

	api->ipc_response_send (conn,
		&res_lib_msg_queuegrouptrackstop,
		sizeof (struct res_lib_msg_queuegrouptrackstop));
}

static void message_handler_req_lib_msg_queuegroupnotificationfree (
	void *conn,
	const void *msg)
{
/* 	const struct req_lib_msg_queuegroupnotificationfree *req_lib_msg_queuegroupnotificationfree = msg; */
	struct req_exec_msg_queuegroupnotificationfree req_exec_msg_queuegroupnotificationfree;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgQueueGroupNotificationFree\n");

	req_exec_msg_queuegroupnotificationfree.header.size =
		sizeof (struct req_exec_msg_queuegroupnotificationfree);
	req_exec_msg_queuegroupnotificationfree.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPNOTIFICATIONFREE);

	api->ipc_source_set (&req_exec_msg_queuegroupnotificationfree.source, conn);

	iovec.iov_base = (void *)&req_exec_msg_queuegroupnotificationfree;
	iovec.iov_len = sizeof (req_exec_msg_queuegroupnotificationfree);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_messagesend (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_messagesend *req_lib_msg_messagesend = msg;
	struct req_exec_msg_messagesend req_exec_msg_messagesend;
	struct iovec iovec[2];

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgMessageSend\n");

	req_exec_msg_messagesend.header.size =
		sizeof (struct req_exec_msg_messagesend);
	req_exec_msg_messagesend.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGESEND);

	api->ipc_source_set (&req_exec_msg_messagesend.source, conn);

	req_exec_msg_messagesend.timeout =
		req_lib_msg_messagesend->timeout;

	memcpy (&req_exec_msg_messagesend.destination,
		&req_lib_msg_messagesend->destination,
		sizeof (mar_name_t));
	memcpy (&req_exec_msg_messagesend.message,
		&req_lib_msg_messagesend->message,
		sizeof (mar_msg_message_t));

	/* ! */

	iovec[0].iov_base = (void *)&req_exec_msg_messagesend;
	iovec[0].iov_len = sizeof (struct req_exec_msg_messagesend);

	iovec[1].iov_base = (void *)(((char *)req_lib_msg_messagesend) +
		sizeof (struct req_lib_msg_messagesend));
	iovec[1].iov_len = (req_lib_msg_messagesend->header.size -
		sizeof (struct req_lib_msg_messagesend));

	req_exec_msg_messagesend.header.size += iovec[1].iov_len;

	assert (api->totem_mcast (iovec, 2, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_messagesendasync (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_messagesendasync *req_lib_msg_messagesendasync = msg;
	struct req_exec_msg_messagesendasync req_exec_msg_messagesendasync;
	struct iovec iovec[2];

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgMessageSendAsync\n");

	req_exec_msg_messagesendasync.header.size =
		sizeof (struct req_exec_msg_messagesendasync);
	req_exec_msg_messagesendasync.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGESENDASYNC);

	api->ipc_source_set (&req_exec_msg_messagesendasync.source, conn);

	req_exec_msg_messagesendasync.invocation =
		req_lib_msg_messagesendasync->invocation;
	req_exec_msg_messagesendasync.ack_flags =
		req_lib_msg_messagesendasync->ack_flags;

	memcpy (&req_exec_msg_messagesendasync.destination,
		&req_lib_msg_messagesendasync->destination,
		sizeof (mar_name_t));
	memcpy (&req_exec_msg_messagesendasync.message,
		&req_lib_msg_messagesendasync->message,
		sizeof (mar_msg_message_t));

	/* ! */

	iovec[0].iov_base = (void *)&req_exec_msg_messagesendasync;
	iovec[0].iov_len = sizeof (struct req_exec_msg_messagesendasync);

	iovec[1].iov_base = (void *)(((char *)req_lib_msg_messagesendasync) +
		sizeof (struct req_lib_msg_messagesendasync));
	iovec[1].iov_len = (req_lib_msg_messagesendasync->header.size -
		sizeof (struct req_lib_msg_messagesendasync));

	req_exec_msg_messagesendasync.header.size += iovec[1].iov_len;

	assert (api->totem_mcast (iovec, 2, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_messageget (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_messageget *req_lib_msg_messageget = msg;
	struct req_exec_msg_messageget req_exec_msg_messageget;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgMessageGet\n");

	req_exec_msg_messageget.header.size =
		sizeof (struct req_exec_msg_messageget);
	req_exec_msg_messageget.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGEGET);

	api->ipc_source_set (&req_exec_msg_messageget.source, conn);

	req_exec_msg_messageget.queue_id =
		req_lib_msg_messageget->queue_id;
	req_exec_msg_messageget.timeout =
		req_lib_msg_messageget->timeout;

	memcpy (&req_exec_msg_messageget.queue_name,
		&req_lib_msg_messageget->queue_name,
		sizeof (mar_name_t));	

	iovec.iov_base = (void *)&req_exec_msg_messageget;
	iovec.iov_len = sizeof (req_exec_msg_messageget);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_messagedatafree (
	void *conn,
	const void *msg)
{
	struct req_exec_msg_messagedatafree req_exec_msg_messagedatafree;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgMessageDataFree\n");

	req_exec_msg_messagedatafree.header.size =
		sizeof (struct req_exec_msg_messagedatafree);
	req_exec_msg_messagedatafree.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGEDATAFREE);

	api->ipc_source_set (&req_exec_msg_messagedatafree.source, conn);

	/* ! */

	iovec.iov_base = (void *)&req_exec_msg_messagedatafree;
	iovec.iov_len = sizeof (req_exec_msg_messagedatafree);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_messagecancel (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_messagecancel *req_lib_msg_messagecancel = msg;
	struct req_exec_msg_messagecancel req_exec_msg_messagecancel;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgMessageCancel\n");

	req_exec_msg_messagecancel.header.size =
		sizeof (struct req_exec_msg_messagecancel);
	req_exec_msg_messagecancel.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGECANCEL);

	api->ipc_source_set (&req_exec_msg_messagecancel.source, conn);

	req_exec_msg_messagecancel.queue_id =
		req_lib_msg_messagecancel->queue_id;

	memcpy (&req_exec_msg_messagecancel.queue_name,
		&req_lib_msg_messagecancel->queue_name,
		sizeof (mar_name_t));

	iovec.iov_base = (void *)&req_exec_msg_messagecancel;
	iovec.iov_len = sizeof (req_exec_msg_messagecancel);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_messagesendreceive (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_messagesendreceive *req_lib_msg_messagesendreceive = msg;
	struct req_exec_msg_messagesendreceive req_exec_msg_messagesendreceive;
	struct iovec iovec[2];

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgMessageSendReceive\n");

	req_exec_msg_messagesendreceive.header.size =
		sizeof (struct req_exec_msg_messagesendreceive);
	req_exec_msg_messagesendreceive.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGESENDRECEIVE);

	api->ipc_source_set (&req_exec_msg_messagesendreceive.source, conn);

	req_exec_msg_messagesendreceive.timeout =
		req_lib_msg_messagesendreceive->timeout;
	req_exec_msg_messagesendreceive.reply_size =
		req_lib_msg_messagesendreceive->reply_size;
	req_exec_msg_messagesendreceive.sender_id =
		(mar_msg_sender_id_t)((global_sender_id) |
		((unsigned long long)(req_exec_msg_messagesendreceive.source.nodeid) << 32));

	global_sender_id += 1;

	memcpy (&req_exec_msg_messagesendreceive.destination,
		&req_lib_msg_messagesendreceive->destination,
		sizeof (mar_name_t));
	memcpy (&req_exec_msg_messagesendreceive.message,
		&req_lib_msg_messagesendreceive->message,
		sizeof (mar_msg_message_t));

	iovec[0].iov_base = (void *)&req_exec_msg_messagesendreceive;
	iovec[0].iov_len = sizeof (req_exec_msg_messagesendreceive);

	iovec[1].iov_base = (void *)(((char *)req_lib_msg_messagesendreceive) +
		sizeof (struct req_lib_msg_messagesendreceive));
	iovec[1].iov_len = req_lib_msg_messagesendreceive->header.size -
		sizeof (struct req_lib_msg_messagesendreceive);

	req_exec_msg_messagesendreceive.header.size += iovec[1].iov_len;

	assert (api->totem_mcast (iovec, 2, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_messagereply (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_messagereply *req_lib_msg_messagereply = msg;
	struct req_exec_msg_messagereply req_exec_msg_messagereply;
	struct iovec iovec[2];

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgMessageReply\n");

	req_exec_msg_messagereply.header.size =
		sizeof (struct req_exec_msg_messagereply);
	req_exec_msg_messagereply.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGEREPLY);

	api->ipc_source_set (&req_exec_msg_messagereply.source, conn);

	req_exec_msg_messagereply.sender_id =
		req_lib_msg_messagereply->sender_id;
	req_exec_msg_messagereply.timeout =
		req_lib_msg_messagereply->timeout;

	memcpy (&req_exec_msg_messagereply.reply_message,
		&req_lib_msg_messagereply->reply_message,
		sizeof (mar_msg_message_t));

	iovec[0].iov_base = (void *)&req_exec_msg_messagereply;
	iovec[0].iov_len = sizeof (req_exec_msg_messagereply);

	iovec[1].iov_base = (void *)(((char *)req_lib_msg_messagereply) +
		sizeof (struct req_lib_msg_messagereply));
	iovec[1].iov_len = (req_lib_msg_messagereply->header.size -
		sizeof (struct req_lib_msg_messagereply));

	req_exec_msg_messagereply.header.size += iovec[1].iov_len;

	assert (api->totem_mcast (iovec, 2, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_messagereplyasync (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_messagereplyasync *req_lib_msg_messagereplyasync = msg;
	struct req_exec_msg_messagereplyasync req_exec_msg_messagereplyasync;
	struct iovec iovec[2];

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgMessageReplyAsync\n");

	req_exec_msg_messagereplyasync.header.size =
		sizeof (struct req_exec_msg_messagereplyasync);
	req_exec_msg_messagereplyasync.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGEREPLYASYNC);

	api->ipc_source_set (&req_exec_msg_messagereplyasync.source, conn);

	req_exec_msg_messagereplyasync.sender_id =
		req_lib_msg_messagereplyasync->sender_id;
	req_exec_msg_messagereplyasync.invocation =
		req_lib_msg_messagereplyasync->invocation;
	req_exec_msg_messagereplyasync.ack_flags =
		req_lib_msg_messagereplyasync->ack_flags;

	memcpy (&req_exec_msg_messagereplyasync.reply_message,
		&req_lib_msg_messagereplyasync->reply_message,
		sizeof (mar_msg_message_t));

	iovec[0].iov_base = (void *)&req_exec_msg_messagereplyasync;
	iovec[0].iov_len = sizeof (req_exec_msg_messagereplyasync);

	iovec[1].iov_base = (void *)(((char *)req_lib_msg_messagereplyasync) +
		sizeof (struct req_lib_msg_messagereplyasync));
	iovec[1].iov_len = (req_lib_msg_messagereplyasync->header.size -
		sizeof (struct req_lib_msg_messagereplyasync));

	req_exec_msg_messagereplyasync.header.size += iovec[1].iov_len;

	assert (api->totem_mcast (iovec, 2, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuecapacitythresholdsset (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_queuecapacitythresholdsset *req_lib_msg_queuecapacitythresholdsset = msg;
	struct req_exec_msg_queuecapacitythresholdsset req_exec_msg_queuecapacitythresholdsset;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgQueueCapacityThresholdsSet\n");

	req_exec_msg_queuecapacitythresholdsset.header.size =
		sizeof (struct req_exec_msg_queuecapacitythresholdsset);
	req_exec_msg_queuecapacitythresholdsset.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUECAPACITYTHRESHOLDSSET);

	api->ipc_source_set (&req_exec_msg_queuecapacitythresholdsset.source, conn);

	memcpy (&req_exec_msg_queuecapacitythresholdsset.queue_name,
		&req_lib_msg_queuecapacitythresholdsset->queue_name,
		sizeof (mar_name_t));

	iovec.iov_base = (void *)&req_exec_msg_queuecapacitythresholdsset;
	iovec.iov_len = sizeof (req_exec_msg_queuecapacitythresholdsset);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuecapacitythresholdsget (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_queuecapacitythresholdsget *req_lib_msg_queuecapacitythresholdsget = msg;
	struct req_exec_msg_queuecapacitythresholdsget req_exec_msg_queuecapacitythresholdsget;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgQueueCapacityThresholdsGet\n");

	req_exec_msg_queuecapacitythresholdsget.header.size =
		sizeof (struct req_exec_msg_queuecapacitythresholdsget);
	req_exec_msg_queuecapacitythresholdsget.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUECAPACITYTHRESHOLDSGET);

	api->ipc_source_set (&req_exec_msg_queuecapacitythresholdsget.source, conn);

	memcpy (&req_exec_msg_queuecapacitythresholdsget.queue_name,
		&req_lib_msg_queuecapacitythresholdsget->queue_name,
		sizeof (mar_name_t));

	iovec.iov_base = (void *)&req_exec_msg_queuecapacitythresholdsget;
	iovec.iov_len = sizeof (req_exec_msg_queuecapacitythresholdsget);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_metadatasizeget (
	void *conn,
	const void *msg)
{
/* 	const struct req_lib_msg_metadatasizeget *req_lib_msg_metadatasizeget = msg; */
	struct req_exec_msg_metadatasizeget req_exec_msg_metadatasizeget;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgMetadataSizeGet\n");

	req_exec_msg_metadatasizeget.header.size =
		sizeof (struct req_exec_msg_metadatasizeget);
	req_exec_msg_metadatasizeget.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_METADATASIZEGET);

	api->ipc_source_set (&req_exec_msg_metadatasizeget.source, conn);

	iovec.iov_base = (void *)&req_exec_msg_metadatasizeget;
	iovec.iov_len = sizeof (req_exec_msg_metadatasizeget);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_limitget (
	void *conn,
	const void *msg)
{
/* 	const struct req_lib_msg_limitget *req_lib_msg_limitget = msg; */
	struct req_exec_msg_limitget req_exec_msg_limitget;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saMsgLimitGet\n");

	req_exec_msg_limitget.header.size =
		sizeof (struct req_exec_msg_limitget);
	req_exec_msg_limitget.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_LIMITGET);

	api->ipc_source_set (&req_exec_msg_limitget.source, conn);

	iovec.iov_base = (void *)&req_exec_msg_limitget;
	iovec.iov_len = sizeof (req_exec_msg_limitget);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}
