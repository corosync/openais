/*
 * Copyright (c) 2005-2006 MontaVista Software, Inc.
 * Copyright (c) 2006 Red Hat, Inc.
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
#include <signal.h>
#include <time.h>
#include <arpa/inet.h>

#include <assert.h>
#include <inttypes.h>

#include <corosync/ipc_gen.h>
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
	MESSAGE_REQ_EXEC_MSG_QUEUECAPACITYTHRESHOLDSET = 21,
	MESSAGE_REQ_EXEC_MSG_QUEUECAPACITYTHRESHOLDGET = 22,
	MESSAGE_REQ_EXEC_MSG_METADATASIZEGET = 23,
	MESSAGE_REQ_EXEC_MSG_LIMITGET = 24,
	MESSAGE_REQ_EXEC_MSG_QUEUE_TIMEOUT = 25,
	MESSAGE_REQ_EXEC_MSG_SYNC_QUEUE = 26,
	MESSAGE_REQ_EXEC_MSG_SYNC_QUEUE_MESSAGE = 27,
	MESSAGE_REQ_EXEC_MSG_SYNC_QUEUE_REFCOUNT = 28,
	MESSAGE_REQ_EXEC_MSG_SYNC_GROUP = 29,
	MESSAGE_REQ_EXEC_MSG_SYNC_GROUP_MEMBER = 30
};

enum msg_sync_state {
	MSG_SYNC_STATE_NOT_STARTED,
	MSG_SYNC_STATE_STARTED,
	MSG_SYNC_STATE_QUEUE,
	MSG_SYNC_STATE_GROUP,
};

enum msg_sync_iteration_state {
	MSG_SYNC_ITERATION_STATE_QUEUE,
	MSG_SYNC_ITERATION_STATE_QUEUE_REFCOUNT,
	MSG_SYNC_ITERATION_STATE_QUEUE_MESSAGE,
	MSG_SYNC_ITERATION_STATE_GROUP,
	MSG_SYNC_ITERATION_STATE_GROUP_MEMBER,
};

struct refcount_set {
	unsigned int refcount;
	unsigned int nodeid;
};

struct message_entry {
	SaTimeT send_time;
	SaMsgSenderIdT sender_id;
	SaMsgMessageT message;
	struct list_head queue_list;
	struct list_head list;
};

struct queue_cleanup {
	SaNameT queue_name;
	SaUint32T queue_id;
	SaMsgQueueHandleT queue_handle;
	struct list_head list;
};

struct priority_area {
	SaSizeT queue_size;
	SaSizeT queue_used;
	SaUint32T message_count;
	struct list_head message_head;
};

struct pending_entry {
	mar_message_source_t source;
	struct list_head list;
};

struct queue_entry {
	SaNameT queue_name;
	SaTimeT close_time;
	SaUint32T refcount;
	SaUint32T queue_id;
	SaUint8T unlink_flag;
	SaMsgQueueOpenFlagsT open_flags;
	SaMsgQueueGroupChangesT change_flag;
	SaMsgQueueCreationAttributesT create_attrs;
	struct group_entry *group;
	struct list_head queue_list;
	struct list_head group_list;
	struct list_head message_head;
	struct list_head pending_head;
	struct priority_area priority[SA_MSG_MESSAGE_LOWEST_PRIORITY+1];
	struct refcount_set refcount_set[PROCESSOR_COUNT_MAX];
	corosync_timer_handle_t timer_handle;
};

struct group_entry {
	SaNameT group_name;
	SaUint8T track_flags;
	SaUint32T change_count;
	SaUint32T member_count;
	SaMsgQueueGroupPolicyT policy;
	struct queue_entry *next_queue;
	struct list_head group_list;
	struct list_head queue_head;
};

/* 
 * Define the limits for the message service.
 * These limits are implementation specific and
 * can be obtained via the library call saMsgLimitGet
 * by passing the appropriate limitId (see saMsg.h).
 */
#define MAX_PRIORITY_AREA_SIZE 128000
#define MAX_QUEUE_SIZE         512000
#define MAX_NUM_QUEUES            32
#define MAX_NUM_QUEUE_GROUPS      16
#define MAX_NUM_QUEUES_PER_GROUP  16
#define MAX_MESSAGE_SIZE          16
#define MAX_REPLY_SIZE            16

DECLARE_LIST_INIT(queue_list_head);
DECLARE_LIST_INIT(group_list_head);

DECLARE_LIST_INIT(sync_queue_list_head);
DECLARE_LIST_INIT(sync_group_list_head);

static struct corosync_api_v1 *api;

static SaUint32T global_queue_id = 0;
static SaUint32T global_sender_id = 0;
static SaUint32T global_queue_count = 0;
static SaUint32T global_group_count = 0;

static void msg_exec_dump_fn (void);

static int msg_exec_init_fn (struct corosync_api_v1 *);
static int msg_lib_init_fn (void *conn);
static int msg_lib_exit_fn (void *conn);

static void message_handler_req_exec_msg_queueopen (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queueopenasync (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queueclose (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuestatusget (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queueretentiontimeset (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queueunlink (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegroupcreate (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegroupinsert (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegroupremove (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegroupdelete (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegrouptrack (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegrouptrackstop (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegroupnotificationfree (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagesend (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagesendasync (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messageget (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagedatafree (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagecancel (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagesendreceive (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagereply (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagereplyasync (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuecapacitythresholdset (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuecapacitythresholdget (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_metadatasizeget (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_limitget (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queue_timeout (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_sync_queue (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_sync_queue_message (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_sync_queue_refcount (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_sync_group (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_sync_group_member (
	const void *message,
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

static void message_handler_req_lib_msg_queuecapacitythresholdset (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_queuecapacitythresholdget (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_metadatasizeget (
	void *conn,
	const void *msg);

static void message_handler_req_lib_msg_limitget (
	void *conn,
	const void *msg);

static enum msg_sync_state msg_sync_state = MSG_SYNC_STATE_NOT_STARTED;
static enum msg_sync_iteration_state msg_sync_iteration_state;

static struct list_head *msg_sync_iteration_queue;
static struct list_head *msg_sync_iteration_group;
static struct list_head *msg_sync_iteration_message;
static struct list_head *msg_sync_iteration_refcount;

static void msg_sync_init (void);
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
		.response_size		= sizeof (struct res_lib_msg_queueopen),
		.response_id		= MESSAGE_RES_MSG_QUEUEOPEN,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queueopenasync,
		.response_size		= sizeof (struct res_lib_msg_queueopenasync),
		.response_id		= MESSAGE_RES_MSG_QUEUEOPENASYNC,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queueclose,
		.response_size		= sizeof (struct res_lib_msg_queueclose),
		.response_id		= MESSAGE_RES_MSG_QUEUECLOSE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queuestatusget,
		.response_size		= sizeof (struct res_lib_msg_queuestatusget),
		.response_id		= MESSAGE_RES_MSG_QUEUESTATUSGET,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queueretentiontimeset,
		.response_size		= sizeof (struct res_lib_msg_queueretentiontimeset),
		.response_id		= MESSAGE_RES_MSG_QUEUERETENTIONTIMESET,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queueunlink,
		.response_size		= sizeof (struct res_lib_msg_queueunlink),
		.response_id		= MESSAGE_RES_MSG_QUEUEUNLINK,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queuegroupcreate,
		.response_size		= sizeof (struct res_lib_msg_queuegroupcreate),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPCREATE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queuegroupinsert,
		.response_size		= sizeof (struct res_lib_msg_queuegroupinsert),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPINSERT,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queuegroupremove,
		.response_size		= sizeof (struct res_lib_msg_queuegroupremove),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPREMOVE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queuegroupdelete,
		.response_size		= sizeof (struct res_lib_msg_queuegroupdelete),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPDELETE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queuegrouptrack,
		.response_size		= sizeof (struct res_lib_msg_queuegrouptrack),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPTRACK,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queuegrouptrackstop,
		.response_size		= sizeof (struct res_lib_msg_queuegrouptrackstop),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPTRACKSTOP,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queuegroupnotificationfree,
		.response_size		= sizeof (struct res_lib_msg_queuegroupnotificationfree),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPNOTIFICATIONFREE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_messagesend,
		.response_size		= sizeof (struct res_lib_msg_messagesend),
		.response_id		= MESSAGE_RES_MSG_MESSAGESEND,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_messagesendasync,
		.response_size		= sizeof (struct res_lib_msg_messagesendasync),
		.response_id		= MESSAGE_RES_MSG_MESSAGESENDASYNC,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_messageget,
		.response_size		= sizeof (struct res_lib_msg_messageget),
		.response_id		= MESSAGE_RES_MSG_MESSAGEGET,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_messagedatafree,
		.response_size		= sizeof (struct res_lib_msg_messagedatafree),
		.response_id		= MESSAGE_RES_MSG_MESSAGEDATAFREE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_messagecancel,
		.response_size		= sizeof (struct res_lib_msg_messagecancel),
		.response_id		= MESSAGE_RES_MSG_MESSAGECANCEL,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_messagesendreceive,
		.response_size		= sizeof (struct res_lib_msg_messagesendreceive),
		.response_id		= MESSAGE_RES_MSG_MESSAGESENDRECEIVE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_messagereply,
		.response_size		= sizeof (struct res_lib_msg_messagereply),
		.response_id		= MESSAGE_RES_MSG_MESSAGEREPLY,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_messagereplyasync,
		.response_size		= sizeof (struct res_lib_msg_messagereplyasync),
		.response_id		= MESSAGE_RES_MSG_MESSAGEREPLYASYNC,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queuecapacitythresholdset,
		.response_size		= sizeof (struct res_lib_msg_queuecapacitythresholdset),
		.response_id		= MESSAGE_RES_MSG_QUEUECAPACITYTHRESHOLDSET,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_queuecapacitythresholdget,
		.response_size		= sizeof (struct res_lib_msg_queuecapacitythresholdget),
		.response_id		= MESSAGE_RES_MSG_QUEUECAPACITYTHRESHOLDGET,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_metadatasizeget,
		.response_size		= sizeof (struct res_lib_msg_metadatasizeget),
		.response_id		= MESSAGE_RES_MSG_METADATASIZEGET,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_msg_limitget,
		.response_size		= sizeof (struct res_lib_msg_limitget),
		.response_id		= MESSAGE_RES_MSG_LIMITGET,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
};

static struct corosync_exec_handler msg_exec_engine[] =
{
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queueopen,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queueopenasync,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queueclose,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queuestatusget,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queueretentiontimeset,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queueunlink,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queuegroupcreate,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queuegroupinsert,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queuegroupremove,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queuegroupdelete,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queuegrouptrack,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queuegrouptrackstop,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queuegroupnotificationfree,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_messagesend,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_messagesendasync,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_messageget,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_messagedatafree,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_messagecancel,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_messagesendreceive,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_messagereply,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_messagereplyasync,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queuecapacitythresholdset,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queuecapacitythresholdget,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_metadatasizeget,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_limitget,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_queue_timeout,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_sync_queue,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_sync_queue_message,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_sync_queue_refcount,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_sync_group,
	},
	{
		.exec_handler_fn	= message_handler_req_exec_msg_sync_group_member,
	}
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

__attribute__ ((constructor)) static void register_this_component (void)
{
	lcr_interfaces_set (&openais_msg_ver0[0], &msg_service_engine_iface);
	lcr_component_register (&msg_comp_ver0);
}

struct req_exec_msg_queueopen {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
	SaMsgQueueHandleT queue_handle;
	SaUint8T create_attrs_flag;
	SaMsgQueueCreationAttributesT create_attrs;
	SaMsgQueueOpenFlagsT open_flags;
	SaTimeT timeout;
};

struct req_exec_msg_queueopenasync {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
	SaMsgQueueHandleT queue_handle;
	SaUint8T create_attrs_flag;
	SaMsgQueueCreationAttributesT create_attrs;
	SaMsgQueueOpenFlagsT open_flags;
	SaInvocationT invocation;
};

struct req_exec_msg_queueclose {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
	SaUint32T queue_id;
};

struct req_exec_msg_queuestatusget {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
};

struct req_exec_msg_queueretentiontimeset {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
	SaUint32T queue_id;
	SaTimeT retention_time;
};

struct req_exec_msg_queueunlink {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
};

struct req_exec_msg_queuegroupcreate {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT group_name;
	SaMsgQueueGroupPolicyT policy;
};

struct req_exec_msg_queuegroupinsert {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT group_name;
	SaNameT queue_name;
};

struct req_exec_msg_queuegroupremove {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT group_name;
	SaNameT queue_name;
};

struct req_exec_msg_queuegroupdelete {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT group_name;
};

struct req_exec_msg_queuegrouptrack {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT group_name;
	SaUint8T track_flags;
	SaUint8T buffer_flag;
};

struct req_exec_msg_queuegrouptrackstop {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT group_name;
};

struct req_exec_msg_queuegroupnotificationfree {
	mar_req_header_t header;
	mar_message_source_t source;
};

struct req_exec_msg_messagesend {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT destination;
	SaTimeT timeout;
	SaMsgMessageT message;
};

struct req_exec_msg_messagesendasync {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT destination;
	SaInvocationT invocation;
	SaMsgMessageT message;
};

struct req_exec_msg_messageget {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
	SaUint32T queue_id;
	SaTimeT timeout;
};

struct req_exec_msg_messagedatafree {
	mar_req_header_t header;
	mar_message_source_t source;
};

struct req_exec_msg_messagecancel {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
	SaUint32T queue_id;
};

struct req_exec_msg_messagesendreceive {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT destination;
	SaTimeT timeout;
	SaMsgMessageT message;
	SaMsgSenderIdT sender_id;
};

struct req_exec_msg_messagereply {
	mar_req_header_t header;
	mar_message_source_t source;
	SaMsgMessageT reply_message;
	SaMsgSenderIdT sender_id;
	SaTimeT timeout;
};

struct req_exec_msg_messagereplyasync {
	mar_req_header_t header;
	mar_message_source_t source;
	SaMsgMessageT reply_message;
	SaMsgSenderIdT sender_id;
	SaInvocationT invocation;
};

struct req_exec_msg_queuecapacitythresholdset {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
	SaUint32T queue_id;
};

struct req_exec_msg_queuecapacitythresholdget {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
	SaUint32T queue_id;
};

struct req_exec_msg_metadatasizeget {
	mar_req_header_t header;
	mar_message_source_t source;
};

struct req_exec_msg_limitget {
	mar_req_header_t header;
	mar_message_source_t source;
	SaMsgLimitIdT limit_id;
};

struct req_exec_msg_queue_timeout {
	mar_req_header_t header;
	SaNameT queue_name;
};

struct req_exec_msg_sync_queue {
	mar_req_header_t header;
	struct memb_ring_id ring_id;
	SaNameT queue_name;
	SaUint32T queue_id;
	SaTimeT close_time;
	SaUint8T unlink_flag;
	SaMsgQueueOpenFlagsT open_flags;
	SaMsgQueueGroupChangesT change_flag;
	SaMsgQueueCreationAttributesT create_attrs;
	/* corosync_timer_handle_t ? */
};

struct req_exec_msg_sync_queue_message {
	mar_req_header_t header;
	struct memb_ring_id ring_id;
	SaNameT queue_name;
	SaUint32T queue_id;
	SaTimeT send_time;
	SaMsgSenderIdT sender_id;
	SaMsgMessageT message;
};

struct req_exec_msg_sync_queue_refcount {
	mar_req_header_t header;
	struct memb_ring_id ring_id;
	SaNameT queue_name;
	SaUint32T queue_id;
	struct refcount_set refcount_set[PROCESSOR_COUNT_MAX];
};

struct req_exec_msg_sync_group {
	mar_req_header_t header;
	struct memb_ring_id ring_id;
	SaNameT group_name;
	SaUint8T track_flags;
	SaMsgQueueGroupPolicyT policy;
};

struct req_exec_msg_sync_group_member {
	mar_req_header_t header;
	struct memb_ring_id ring_id;
	SaNameT group_name;
	SaNameT queue_name;
	SaUint32T queue_id;
};

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

static void msg_confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id) 
{
	unsigned int i, j;

	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: msg_confchg_fn\n");

	memcpy (&saved_ring_id, ring_id,
		sizeof (struct memb_ring_id));

	if (configuration_type != TOTEM_CONFIGURATION_REGULAR) {
		return;
	}
	if (msg_sync_state != MSG_SYNC_STATE_NOT_STARTED) {
		return;
	}

	msg_sync_state = MSG_SYNC_STATE_STARTED;

	for (i = 0; i < msg_member_list_entries; i++) {
		for (j = 0; j < member_list_entries; j++) {
			if (msg_member_list[i] == member_list[j]) {
				if (lowest_nodeid > member_list[j]) {
					lowest_nodeid = member_list[j];
				}
			}
		}
	}

	memcpy (msg_member_list, member_list,
		sizeof (unsigned int) * member_list_entries);

	msg_member_list_entries = member_list_entries;

	return;
}

static int msg_name_match (const SaNameT *name_a, const SaNameT *name_b)
{
	if (name_a->length == name_b->length) {
		return ((strncmp ((char *)name_a->value, (char *)name_b->value, (int)name_b->length)) == 0);
	}
	return (0);
}

#ifdef PRINT_ON
static void msg_print_queue_cleanup_list (
	struct list_head *cleanup_head)
{
	struct list_head *list;
	struct queue_cleanup *cleanup;

	for (list = cleanup_head->next;
	     list != cleanup_head;
	     list = list->next)
	{
		cleanup = list_entry (list, struct queue_cleanup, list);

		/* DEBUG */
		log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: cleanup queue=%s id=%u handle=0x%04x\n",
			    (char *)(cleanup->queue_name.value),
			    (unsigned int)(cleanup->queue_id),
			    (unsigned int)(cleanup->queue_handle));
	}
	return;
}

static void msg_print_queue_priority_list (
	struct list_head *message_head)
{
	struct list_head *list;
	struct message_entry *msg;

	for (list = message_head->next;
	     list != message_head;
	     list = list->next)
	{
		msg = list_entry (list, struct message_entry, list);

		/* DEBUG */
		/* log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]:\t message = %s\n", 
		   (char *)(msg->message.data)); */
	}
	return;
}

static void msg_print_queue_message_list (
	struct list_head *message_head)
{
	struct list_head *list;
	struct message_entry *msg;

	for (list = message_head->next;
	     list != message_head;
	     list = list->next)
	{
		msg = list_entry (list, struct message_entry, queue_list);

		/* DEBUG */
		/* log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]:\t message = %s\n",
		   (char *)(msg->message.data)); */
	}
	return;
}

static void msg_print_group_member_list (
	struct list_head *queue_list_head)
{
	struct list_head *list;
	struct queue_entry *queue;

	for (list = queue_list_head->next;
	     list != queue_list_head;
	     list = list->next)
	{
		queue = list_entry (list, struct queue_entry, group_list);

		/* DEBUG */
		log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]:\t queue = %s\n",
			    (char *)(queue->queue_name.value));
	}
	return;
}

static void msg_print_queue_list (
	struct list_head *queue_list_head)
{
	struct list_head *list;
	struct queue_entry *queue;
	int i;

	for (list = queue_list_head->next;
	     list != queue_list_head;
	     list = list->next)
	{
		queue = list_entry (list, struct queue_entry, queue_list);

		/* DEBUG */
		log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: queue = %s (refcount=%u)\n",
			    (char *)(queue->queue_name.value),
			    (unsigned int)(queue->refcount));

		/* msg_print_queue_message_list (&queue->message_head); */

		for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
			/* DEBUG */
			log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]:\t priority = %d ( %u )\n",
				    i, (unsigned int)(queue->priority[i].message_count));

			msg_print_queue_priority_list (&queue->priority[i].message_head);
		}
	}
	return;
}

static void msg_print_group_list (
	struct list_head *group_list_head)
{
	struct list_head *list;
	struct group_entry *group;

	for (list = group_list_head->next;
	     list != group_list_head;
	     list = list->next)
	{
		group = list_entry (list, struct group_entry, group_list);

		/* DEBUG */
		log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: group = %s\n",
			    (char *)(group->group_name.value));

		msg_print_group_member_list (&group->queue_head);
	}
	return;
}
#endif

static unsigned int msg_group_track_current (
	struct group_entry *group,
	SaMsgQueueGroupNotificationT *notification)
{
	struct queue_entry *queue;
	struct list_head *list;
	unsigned int i = 0;

	for (list = group->queue_head.next;
	     list != &group->queue_head;
	     list = list->next)
	{
		queue = list_entry (list, struct queue_entry, group_list);

		memcpy (&notification[i].member.queueName,
			&queue->queue_name, sizeof (SaNameT));
		notification[i].change = queue->change_flag;
		i++;
	}

	return (i);
}

static unsigned int msg_group_track_changes (
	struct group_entry *group,
	SaMsgQueueGroupNotificationT *notification)
{
	struct queue_entry *queue;
	struct list_head *list;
	unsigned int i = 0;

	for (list = group->queue_head.next;
	     list != &group->queue_head;
	     list = list->next)
	{
		queue = list_entry (list, struct queue_entry, group_list);

		memcpy (&notification[i].member.queueName,
			&queue->queue_name, sizeof (SaNameT));
		notification[i].change = queue->change_flag;
		i++;
	}

	return (i);
}

static unsigned int msg_group_track_changes_only (
	struct group_entry *group,
	SaMsgQueueGroupNotificationT *notification)
{
	struct queue_entry *queue;
	struct list_head *list;
	unsigned int i = 0;

	for (list = group->queue_head.next;
	     list != &group->queue_head;
	     list = list->next)
	{
		queue = list_entry (list, struct queue_entry, group_list);

		if (queue->change_flag != SA_MSG_QUEUE_GROUP_NO_CHANGE) {
			memcpy (&notification[i].member.queueName,
				&queue->queue_name, sizeof (SaNameT));
			notification[i].change = queue->change_flag;
			i++;
		}
	}

	return (i);
}

static void msg_deliver_pending_message (
	void *conn,
	struct message_entry *msg)
{
	struct res_lib_msg_messageget res_lib_msg_messageget;
	struct iovec iov[2];

	res_lib_msg_messageget.header.size =
		sizeof (struct res_lib_msg_messageget);
	res_lib_msg_messageget.header.id =
		MESSAGE_RES_MSG_MESSAGEGET;
	res_lib_msg_messageget.header.error = SA_AIS_OK;

	memcpy (&res_lib_msg_messageget.message, &msg->message,
		sizeof (SaMsgMessageT));

	res_lib_msg_messageget.send_time = msg->send_time;
	res_lib_msg_messageget.sender_id = msg->sender_id;

	iov[0].iov_base = &res_lib_msg_messageget;
	iov[0].iov_len = sizeof (struct res_lib_msg_messageget);

	iov[1].iov_base = msg->message.data;
	iov[1].iov_len = msg->message.size;

	api->ipc_response_iov_send (conn, iov, 2);

	return;
}

static struct queue_entry *msg_next_group_member (
	struct group_entry *group)
{
	struct queue_entry *queue;

	if (group->next_queue->group_list.next == &group->queue_head) {
		queue = list_entry (group->queue_head.next,
				    struct queue_entry,
				    group_list);
	}
	else {
		queue = list_entry (group->next_queue->group_list.next,
				    struct queue_entry,
				    group_list);
	}

	return (queue);
}

static struct message_entry *msg_get_message (
	struct queue_entry *queue)
{
	struct message_entry *msg = NULL;
	int i;

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
		if (queue->priority[i].message_count != 0) {
			msg = list_entry (queue->priority[i].message_head.next, struct message_entry, list);
			break;
		}
	}

	return (msg);
}

static struct queue_entry *msg_find_group_member (
	struct list_head *queue_list_head,
	const SaNameT *queue_name)
{
	struct list_head *list;
	struct queue_entry *queue;

	for (list = queue_list_head->next;
	     list != queue_list_head;
	     list = list->next)
	{
		queue = list_entry (list, struct queue_entry, group_list);

		if (msg_name_match (queue_name, &queue->queue_name))
		{
			return (queue);
		}
	}
	return (0);
}

static struct queue_cleanup *msg_find_queue_cleanup (
	void *conn,
	SaNameT *queue_name,
	SaUint32T queue_id)
{
	struct list_head *list;
	struct queue_cleanup *cleanup;
	struct msg_pd *msg_pd = (struct msg_pd *)api->ipc_private_data_get (conn);

	for (list = msg_pd->queue_cleanup_list.next;
	     list != &msg_pd->queue_cleanup_list;
	     list = list->next)
	{
		cleanup = list_entry (list, struct queue_cleanup, list);

		if ((msg_name_match (queue_name, &cleanup->queue_name)) &&
		    (queue_id == cleanup->queue_id))
		{
			return (cleanup);
		}
	}
	return (0);
}

static struct queue_entry *msg_find_queue_id (
	struct list_head *queue_list_head,
	const SaNameT *queue_name,
	SaUint32T queue_id)
{
	struct list_head *list;
	struct queue_entry *queue;

	for (list = queue_list_head->next;
	     list != queue_list_head;
	     list = list->next)
	{
		queue = list_entry (list, struct queue_entry, queue_list);

		if ((msg_name_match (queue_name, &queue->queue_name)) &&
		    (queue_id == queue->queue_id))
		{
			return (queue);
		}
	}
	return (0);
}

static struct queue_entry *msg_find_queue (
	struct list_head *queue_list_head,
	const SaNameT *queue_name)
{
	struct list_head *list;
	struct queue_entry *queue;

	for (list = queue_list_head->next;
	     list != queue_list_head;
	     list = list->next)
	{
		queue = list_entry (list, struct queue_entry, queue_list);

		if ((msg_name_match (queue_name, &queue->queue_name)) &&
		    (queue->unlink_flag == 0))
		{
			return (queue);
		}
	}
	return (0);
}

static struct group_entry *msg_find_group (
	struct list_head *group_list_head,
	const SaNameT *group_name)
{
	struct list_head *list;
	struct group_entry *group;

	for (list = group_list_head->next;
	     list != group_list_head;
	     list = list->next)
	{
		group = list_entry (list, struct group_entry, group_list);

		if (msg_name_match (group_name, &group->group_name))
		{
			return (group);
		}
	}
	return (0);
}

static int msg_close_queue (
	SaNameT *queue_name,
	SaUint32T queue_id)
{
	struct req_exec_msg_queueclose req_exec_msg_queueclose;
	struct iovec iov;

	req_exec_msg_queueclose.header.size =
		sizeof (struct req_exec_msg_queueclose);
	req_exec_msg_queueclose.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUECLOSE);

	memset (&req_exec_msg_queueclose.source, 0,
		sizeof (mar_message_source_t));
	memcpy (&req_exec_msg_queueclose.queue_name,
		queue_name, sizeof (SaNameT));

	req_exec_msg_queueclose.queue_id = queue_id;

	iov.iov_base = (char *)&req_exec_msg_queueclose;
	iov.iov_len = sizeof (struct req_exec_msg_queueclose);

	return (api->totem_mcast (&iov, 1, TOTEM_AGREED));
}

static void msg_release_queue_message (
	struct queue_entry *queue)
{
	struct message_entry *msg;
	struct list_head *list;
	int i;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]:\t msg_release_queue_message ( %s )\n",
		    (char *)(queue->queue_name.value));

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++)
	{
		list = queue->priority[i].message_head.next;

		while (!list_empty (&queue->priority[i].message_head)) {
			msg = list_entry (list, struct message_entry, list);

			/* DEBUG */
			log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]:\t msg = %s ( %d )\n",
				    (char *)(msg->message.data), (int)(i));

			list_del (&msg->list);
			list_init (&msg->list);

			list_del (&msg->queue_list);
			list_init (&msg->queue_list);

			free (msg->message.data);
			free (msg);

			list = queue->priority[i].message_head.next;
		}
		queue->priority[i].queue_used = 0;
		queue->priority[i].message_count = 0;
	}
	return;
}

static void msg_release_queue_cleanup (
	void *conn,
	const SaNameT *queue_name,
	SaUint32T queue_id)
{
	struct list_head *list;
	struct queue_cleanup *cleanup;
	struct msg_pd *msg_pd = (struct msg_pd *)api->ipc_private_data_get (conn);

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]:\t msg_release_queue_cleanup ( %s )\n",
		    (char *)(queue_name->value));

	for (list = msg_pd->queue_cleanup_list.next;
	     list != &msg_pd->queue_cleanup_list;
	     list = list->next)
	{
		cleanup = list_entry (list, struct queue_cleanup, list);

		if ((msg_name_match (queue_name, &cleanup->queue_name)) &&
		    (queue_id == cleanup->queue_id))
		{
			list_del (&cleanup->list);
			free (cleanup);
			return;
		}
	}
	return;
}

static void msg_release_queue (
	struct queue_entry *queue)
{
	struct message_entry *msg;
	struct list_head *list;
	int i;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]:\t msg_release_queue ( %s )\n",
		    (char *)(queue->queue_name.value));

	list_del (&queue->queue_list);
	list_init (&queue->queue_list);

	if (queue->group != NULL) {
		queue->group = NULL;
		list_del (&queue->group_list);
		list_init (&queue->group_list);
	}

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++)
	{
		list = queue->priority[i].message_head.next;

		while (!list_empty (&queue->priority[i].message_head)) {
			msg = list_entry (list, struct message_entry, list);

			list_del (&msg->list);
			list_init (&msg->list);

			list_del (&msg->queue_list);
			list_init (&msg->queue_list);

			free (msg->message.data);
			free (msg);

			list = queue->priority[i].message_head.next;
		}
		queue->priority[i].queue_used = 0;
		queue->priority[i].message_count = 0;
	}

	global_queue_count -= 1;

	free (queue);

	return;
}

static void msg_release_group (
	struct group_entry *group)
{
	struct list_head *list = group->queue_head.next;
	struct queue_entry *queue;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]:\t msg_release_group ( %s )\n",
		    (char *)(group->group_name.value));

	while (!list_empty (&group->queue_head)) {
		queue = list_entry (list, struct queue_entry, group_list);

		list_del (&queue->group_list);
		list_init (&queue->group_list);

		list = group->queue_head.next;
	}

	list_del (&group->group_list);
	list_init (&group->group_list);

	global_group_count -= 1;

	free (group);

	return;
}

static void msg_expire_queue (void *data)
{
	struct req_exec_msg_queue_timeout req_exec_msg_queue_timeout;
	struct iovec iovec;

	struct queue_entry *queue = (struct queue_entry *)data;

	req_exec_msg_queue_timeout.header.size =
		sizeof (struct req_exec_msg_queue_timeout);
	req_exec_msg_queue_timeout.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUE_TIMEOUT);

	memcpy (&req_exec_msg_queue_timeout.queue_name,
		&queue->queue_name, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queue_timeout;
	iovec.iov_len = sizeof (struct req_exec_msg_queue_timeout);

	api->totem_mcast (&iovec, 1, TOTEM_AGREED);

	return;
}

static inline void msg_sync_queue_free (
	struct list_head *queue_head)
{
	struct queue_entry *queue;
	struct list_head *list;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: msg_sync_queue_free\n");

	list = queue_head->next;

	while (list != queue_head) {
		queue = list_entry (list, struct queue_entry, queue_list);
		list = list->next;
		msg_release_queue (queue);
	}

	list_init (queue_head);
}

static inline void msg_sync_group_free (
	struct list_head *group_head)
{
	struct group_entry *group;
	struct list_head *list;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: msg_sync_group_free\n");

	list = group_head->next;

	while (list != group_head) {
		group = list_entry (list, struct group_entry, group_list);
		list = list->next;
		msg_release_group (group);
	}

	list_init (group_head);
}

static int msg_sync_queue_transmit (
	struct queue_entry *queue)
{
	struct req_exec_msg_sync_queue req_exec_msg_sync_queue;
	struct iovec iov;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: msg_sync_queue_transmit { queue=%s id=%u }\n",
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
		&queue->queue_name, sizeof (SaNameT));
	memcpy (&req_exec_msg_sync_queue.create_attrs,
		&queue->create_attrs, sizeof (SaMsgQueueCreationAttributesT));

	req_exec_msg_sync_queue.queue_id = queue->queue_id;
	req_exec_msg_sync_queue.close_time = queue->close_time;
	req_exec_msg_sync_queue.unlink_flag = queue->unlink_flag;
	req_exec_msg_sync_queue.open_flags = queue->open_flags;
	req_exec_msg_sync_queue.change_flag = queue->change_flag;

	iov.iov_base = (char *)&req_exec_msg_sync_queue;
	iov.iov_len = sizeof (struct req_exec_msg_sync_queue);

	return (api->totem_mcast (&iov, 1, TOTEM_AGREED));
}

static int msg_sync_queue_message_transmit (
	struct queue_entry *queue,
	struct message_entry *msg)
{
	struct req_exec_msg_sync_queue_message req_exec_msg_sync_queue_message;
	struct iovec iov[2];

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: msg_sync_queue_message_transmit { queue=%s id=%u}\n",
		    (char *)(queue->queue_name.value),
		    (unsigned int)(queue->queue_id));

	memset (&req_exec_msg_sync_queue_message, 0,
		sizeof (struct req_exec_msg_sync_queue_message));

	req_exec_msg_sync_queue_message.header.size =
		sizeof (struct req_exec_msg_sync_queue_message);
	req_exec_msg_sync_queue_message.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_SYNC_QUEUE_MESSAGE);

	memcpy (&req_exec_msg_sync_queue_message.ring_id,
		&saved_ring_id, sizeof (struct memb_ring_id));
	memcpy (&req_exec_msg_sync_queue_message.queue_name,
		&queue->queue_name, sizeof (SaNameT));
	memcpy (&req_exec_msg_sync_queue_message.message,
		&msg->message, sizeof (SaMsgMessageT));

	req_exec_msg_sync_queue_message.queue_id = queue->queue_id;
	req_exec_msg_sync_queue_message.send_time = msg->send_time;
	req_exec_msg_sync_queue_message.sender_id = msg->sender_id;

	iov[0].iov_base = (char *)&req_exec_msg_sync_queue_message;
	iov[0].iov_len = sizeof (struct req_exec_msg_sync_queue_message);
	iov[1].iov_base = msg->message.data;
	iov[1].iov_len = msg->message.size;

	return (api->totem_mcast (iov, 2, TOTEM_AGREED));
}

static int msg_sync_queue_refcount_transmit (
	struct queue_entry *queue)
{
	struct req_exec_msg_sync_queue_refcount req_exec_msg_sync_queue_refcount;
	struct iovec iov;

	int i;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: msg_sync_queue_refcount_transmit { queue=%s id=%u }\n",
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
		&queue->queue_name, sizeof (SaNameT));

	req_exec_msg_sync_queue_refcount.queue_id = queue->queue_id;

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		req_exec_msg_sync_queue_refcount.refcount_set[i].refcount =
			queue->refcount_set[i].refcount;
		req_exec_msg_sync_queue_refcount.refcount_set[i].nodeid =
			queue->refcount_set[i].nodeid;
	}

	iov.iov_base = (char *)&req_exec_msg_sync_queue_refcount;
	iov.iov_len = sizeof (struct req_exec_msg_sync_queue_refcount);

	return (api->totem_mcast (&iov, 1, TOTEM_AGREED));
}

static int msg_sync_group_transmit (
	struct group_entry *group)
{
	struct req_exec_msg_sync_group req_exec_msg_sync_group;
	struct iovec iov;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: msg_sync_group_transmit { group=%s }\n",
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
		&group->group_name, sizeof (SaNameT));

	req_exec_msg_sync_group.track_flags = group->track_flags;
	req_exec_msg_sync_group.policy = group->policy;

	iov.iov_base = (char *)&req_exec_msg_sync_group;
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
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: msg_sync_group_member_transmit { group=%s queue=%s }\n",
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
		&group->group_name, sizeof (SaNameT));
	memcpy (&req_exec_msg_sync_group_member.queue_name,
		&queue->queue_name, sizeof (SaNameT));

	req_exec_msg_sync_group_member.queue_id = queue->queue_id;

	iov.iov_base = (char *)&req_exec_msg_sync_group_member;
	iov.iov_len = sizeof (struct req_exec_msg_sync_group_member);

	return (api->totem_mcast (&iov, 1, TOTEM_AGREED));
}

static int msg_sync_queue_iterate (void)
{
	struct queue_entry *queue;
	struct list_head *queue_list;

	struct message_entry *msg;
	struct list_head *message_list;

	int result;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: msg_sync_queue_iterate\n");

	for (queue_list = msg_sync_iteration_queue;
	     queue_list != &queue_list_head;
	     queue_list = queue_list->next)
	{
		queue = list_entry (queue_list, struct queue_entry, queue_list);

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
			msg_sync_iteration_message = queue->message_head.next;
			msg_sync_iteration_state = MSG_SYNC_ITERATION_STATE_QUEUE_MESSAGE;
		}

		if (msg_sync_iteration_state == MSG_SYNC_ITERATION_STATE_QUEUE_MESSAGE)
		{
			for (message_list = msg_sync_iteration_message;
			     message_list != &queue->message_head;
			     message_list = message_list->next)
			{
				msg = list_entry (message_list, struct message_entry, queue_list);

				result = msg_sync_queue_message_transmit (queue, msg);
				if (result != 0) {
					return (-1);
				}
				msg_sync_iteration_message = message_list->next;
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
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: msg_sync_group_iterate\n");

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
			msg_sync_iteration_queue = group->queue_head.next;
			msg_sync_iteration_state = MSG_SYNC_ITERATION_STATE_GROUP_MEMBER;
		}

		if (msg_sync_iteration_state == MSG_SYNC_ITERATION_STATE_GROUP_MEMBER)
		{
			for (queue_list = msg_sync_iteration_queue;
			     queue_list != &group->queue_head;
			     queue_list = queue_list->next)
			{
				queue = list_entry (queue_list, struct queue_entry, group_list);

				result = msg_sync_group_member_transmit (group, queue);
				if (result != 0) {
					return (-1);
				}
				msg_sync_iteration_queue = queue_list->next;
			}
		}

		msg_sync_iteration_state = MSG_SYNC_ITERATION_STATE_GROUP;
		msg_sync_iteration_group = group_list->next;
	}

	return (0);
}

static void msg_sync_queue_enter (void)
{
	struct queue_entry *queue;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: msg_sync_queue_enter\n");

	queue = list_entry (queue_list_head.next, struct queue_entry, queue_list);

	msg_sync_state = MSG_SYNC_STATE_QUEUE;
	msg_sync_iteration_state = MSG_SYNC_ITERATION_STATE_QUEUE;

	msg_sync_iteration_queue = queue_list_head.next;
	msg_sync_iteration_message = queue->message_head.next;
}

static void msg_sync_group_enter (void)
{
	struct group_entry *group;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: msg_sync_group_enter\n");

	group = list_entry (group_list_head.next, struct group_entry, group_list);

	msg_sync_state = MSG_SYNC_STATE_GROUP;
	msg_sync_iteration_state = MSG_SYNC_ITERATION_STATE_GROUP;

	msg_sync_iteration_group = group_list_head.next;
	msg_sync_iteration_queue = group->queue_head.next; /* ! */
}

static void msg_sync_init (void)
{
	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: msg_sync_init\n");

	msg_sync_queue_enter ();

	/* DEBUG */
	/* msg_print_queue_list (&queue_list_head); */
	/* msg_print_group_list (&group_list_head); */

	return;
}

static int msg_sync_process (void)
{
	int continue_process = 0;
	int iterate_result;
	int iterate_finish;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: msg_sync_process\n");

	switch (msg_sync_state)
	{
	case MSG_SYNC_STATE_QUEUE:
		iterate_finish = 1;
		continue_process = 1;

		if (lowest_nodeid == api->totem_nodeid_get ()) {
			TRACE1 ("transmit queues because lowest member in old configuration.\n");

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

		if (lowest_nodeid == api->totem_nodeid_get ()) {
			TRACE1 ("transmit groups because lowest member in old configuration.\n");

			iterate_result = msg_sync_group_iterate ();
			if (iterate_result != 0) {
				iterate_finish = 0;
			}
		}

		if (iterate_finish == 1) {
			continue_process = 0;
		}

		break;
	}

	return (continue_process);
}

static void msg_sync_activate (void)
{
	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: msg_sync_activate\n");

	msg_sync_queue_free (&queue_list_head);
	msg_sync_group_free (&group_list_head);

	if (!list_empty (&sync_queue_list_head)) {
		list_splice (&sync_queue_list_head, &queue_list_head);
	}

	if (!list_empty (&sync_group_list_head)) {
		list_splice (&sync_group_list_head, &group_list_head);
	}

	list_init (&sync_queue_list_head);
	list_init (&sync_group_list_head);

	/* DEBUG */
	/* msg_print_queue_list (&queue_list_head);
	   msg_print_group_list (&group_list_head); */

	msg_sync_state = MSG_SYNC_STATE_NOT_STARTED;

	return;
}

static void msg_sync_abort (void)
{
	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: msg_sync_abort\n");

	msg_sync_queue_free (&sync_queue_list_head);
	msg_sync_group_free (&sync_group_list_head);

	list_init (&sync_queue_list_head);
	list_init (&sync_group_list_head);

	return;
}

static void msg_exec_dump_fn (void)
{
	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: msg_exec_dump_fn\n");

	return;
}

static int msg_exec_init_fn (struct corosync_api_v1 *corosync_api)
{
	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: msg_exec_init_fn\n");

	api = corosync_api;

	return (0);
}

static int msg_lib_init_fn (void *conn)
{
	struct msg_pd *msg_pd = (struct msg_pd *)(api->ipc_private_data_get (conn));

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: msg_lib_init_fn\n");

	list_init (&msg_pd->queue_list);
	list_init (&msg_pd->queue_cleanup_list);

	return (0);
}

static int msg_lib_exit_fn (void *conn)
{
	struct queue_cleanup *cleanup;
	struct list_head *cleanup_list;
	struct msg_pd *msg_pd = (struct msg_pd *)api->ipc_private_data_get(conn);

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: msg_lib_exit_fn\n");

	/* DEBUG */
	/* msg_print_queue_list (&queue_list_head);
	   msg_print_group_list (&group_list_head); */

	cleanup_list = msg_pd->queue_cleanup_list.next;

	while (!list_empty (&msg_pd->queue_cleanup_list))
	{
		cleanup = list_entry (cleanup_list, struct queue_cleanup, list);

		/* DEBUG */
		log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: cleanup queue=%s id=%u handle=0x%04x\n",
			    (char *)(cleanup->queue_name.value),
			    (unsigned int)(cleanup->queue_id),
			    (unsigned int)(cleanup->queue_handle));

		msg_close_queue (&cleanup->queue_name, cleanup->queue_id);

		list_del (&cleanup->list);
		free (cleanup);

		cleanup_list = msg_pd->queue_cleanup_list.next;
	}

	return (0);
}

static void message_handler_req_exec_msg_queueopen (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_queueopen *req_exec_msg_queueopen =
		message;
	struct res_lib_msg_queueopen res_lib_msg_queueopen;
	SaAisErrorT error = SA_AIS_OK;
	SaSizeT queue_size = 0;
	struct queue_cleanup *cleanup = NULL;
	struct queue_entry *queue = NULL;
	struct msg_pd *msg_pd = NULL;

	int i;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgQueueOpen\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t handle = 0x%04x\n",
		    (unsigned int)(req_exec_msg_queueopen->queue_handle));
	log_printf (LOGSYS_LEVEL_NOTICE, "\t queue = %s\n",
		    (char *)(req_exec_msg_queueopen->queue_name.value));

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
		if (req_exec_msg_queueopen->create_attrs.size[i] > MAX_PRIORITY_AREA_SIZE) {
			error = SA_AIS_ERR_TOO_BIG;
			goto error_exit;
		}
		queue_size += req_exec_msg_queueopen->create_attrs.size[i];
	}

	if (queue_size > MAX_QUEUE_SIZE) {
		error = SA_AIS_ERR_TOO_BIG;
		goto error_exit;
	}

	if ((global_queue_count + 1) > MAX_NUM_QUEUES) {
		error = SA_AIS_ERR_NO_RESOURCES;
		goto error_exit;
	}

	cleanup = malloc (sizeof (struct queue_cleanup));
	if (cleanup == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}

	queue = msg_find_queue (&queue_list_head,
		&req_exec_msg_queueopen->queue_name);

	if (queue == NULL) {
		if ((req_exec_msg_queueopen->create_attrs_flag == 0) ||
		    (req_exec_msg_queueopen->open_flags & SA_MSG_QUEUE_CREATE) == 0)
		{
			error = SA_AIS_ERR_NOT_EXIST;
			goto error_exit;
		}

		queue = malloc (sizeof (struct queue_entry));
		if (queue == NULL) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}
		memset (queue, 0, sizeof (struct queue_entry));
		memcpy (&queue->queue_name,
			&req_exec_msg_queueopen->queue_name,
			sizeof (SaNameT));
		memcpy (&queue->create_attrs,
			&req_exec_msg_queueopen->create_attrs,
			sizeof (SaMsgQueueCreationAttributesT));

		queue->open_flags = req_exec_msg_queueopen->open_flags;

		for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
			queue->priority[i].queue_size = queue->create_attrs.size[i];
			list_init (&queue->priority[i].message_head);
		}

		list_init (&queue->group_list);
		list_init (&queue->queue_list);
		list_init (&queue->message_head);
		list_init (&queue->pending_head);

		list_add_tail  (&queue->queue_list, &queue_list_head);

		global_queue_count += 1;
		queue->queue_id = (global_queue_id += 1);
		queue->refcount = 0;
	}
	else {
		if (queue->timer_handle != 0) {
			api->timer_delete (queue->timer_handle);
		}
		if (req_exec_msg_queueopen->open_flags & SA_MSG_QUEUE_EMPTY) {
			/* DEBUG */
			log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]:\t empty queue = %s\n",
				    (char *)(req_exec_msg_queueopen->queue_name.value));

			/* 
			 * If this queue already exists and is opened by another
			 * process with the SA_MSG_QUEUE_EMPTY flag set, then we
			 * should delete all existing messages in the queue.
			 */

			msg_release_queue_message (queue);
		}
	}

	queue->close_time = 0;

	msg_sync_refcount_increment (queue, nodeid);
	msg_sync_refcount_calculate (queue);

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t refcount = %u\n",
		    (unsigned int)(queue->refcount));

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_queueopen->source))
	{
		res_lib_msg_queueopen.header.size =
			sizeof (struct res_lib_msg_queueopen);
		res_lib_msg_queueopen.header.id =
			MESSAGE_RES_MSG_QUEUEOPEN;
		res_lib_msg_queueopen.header.error = error;

		if (error == SA_AIS_OK) {
			msg_pd = api->ipc_private_data_get (req_exec_msg_queueopen->source.conn);
			memcpy (&cleanup->queue_name, &queue->queue_name, sizeof (SaNameT));
			cleanup->queue_handle = req_exec_msg_queueopen->queue_handle;
			cleanup->queue_id = queue->queue_id;
			res_lib_msg_queueopen.queue_id = queue->queue_id;
			list_init (&cleanup->list);
			list_add_tail (&cleanup->list, &msg_pd->queue_cleanup_list);
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
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_queueopenasync *req_exec_msg_queueopenasync =
		message;
	struct res_lib_msg_queueopenasync res_lib_msg_queueopenasync;
	struct res_lib_msg_queueopen_callback res_lib_msg_queueopen_callback;
	SaAisErrorT error = SA_AIS_OK;
	SaSizeT queue_size = 0;
	struct queue_cleanup *cleanup = NULL;
	struct queue_entry *queue = NULL;
	struct msg_pd *msg_pd = NULL;

	int i;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgQueueOpenAsync\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t handle = 0x%04x\n",
		    (unsigned int)(req_exec_msg_queueopenasync->queue_handle));
	log_printf (LOGSYS_LEVEL_NOTICE, "\t queue = %s\n",
		    (char *)(req_exec_msg_queueopenasync->queue_name.value));

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
		if (req_exec_msg_queueopenasync->create_attrs.size[i] > MAX_PRIORITY_AREA_SIZE) {
			error = SA_AIS_ERR_TOO_BIG;
			goto error_exit;
		}
		queue_size += req_exec_msg_queueopenasync->create_attrs.size[i];
	}

	if (queue_size > MAX_QUEUE_SIZE) {
		error = SA_AIS_ERR_TOO_BIG;
		goto error_exit;
	}

	if ((global_queue_count + 1) > MAX_NUM_QUEUES) {
		error = SA_AIS_ERR_NO_RESOURCES;
		goto error_exit;
	}

	cleanup = malloc (sizeof (struct queue_cleanup));
	if (cleanup == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}

	queue = msg_find_queue (&queue_list_head,
		&req_exec_msg_queueopenasync->queue_name);

	if (queue == NULL) {
		if ((req_exec_msg_queueopenasync->create_attrs_flag == 0) &&
		    (req_exec_msg_queueopenasync->open_flags & SA_MSG_QUEUE_CREATE) == 0)
		{
			error = SA_AIS_ERR_NOT_EXIST;
			goto error_exit;
		}

		queue = malloc (sizeof (struct queue_entry));
		if (queue == NULL) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}
		memset (queue, 0, sizeof (struct queue_entry));
		memcpy (&queue->queue_name,
			&req_exec_msg_queueopenasync->queue_name,
			sizeof (SaNameT));
		memcpy (&queue->create_attrs,
			&req_exec_msg_queueopenasync->create_attrs,
			sizeof (SaMsgQueueCreationAttributesT));

		queue->open_flags = req_exec_msg_queueopenasync->open_flags;

		for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
			queue->priority[i].queue_size = queue->create_attrs.size[i];
			list_init (&queue->priority[i].message_head);
		}

		list_init (&queue->group_list);
		list_init (&queue->queue_list);
		list_init (&queue->message_head);
		list_init (&queue->pending_head);

		list_add_tail  (&queue->queue_list, &queue_list_head);

		global_queue_count += 1;
		queue->queue_id = (global_queue_id += 1);
		queue->refcount = 0;
	}
	else {
		if (queue->timer_handle != 0) {
			api->timer_delete (queue->timer_handle);
		}
		if (req_exec_msg_queueopenasync->open_flags & SA_MSG_QUEUE_EMPTY) {
			/* DEBUG */
			log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]:\t empty queue = %s\n",
				    (char *)(req_exec_msg_queueopenasync->queue_name.value));

			/* 
			 * If this queue already exists and is opened by another
			 * process with the SA_MSG_QUEUE_EMPTY flag set, then we
			 * should delete all existing messages in the queue.
			 */

			msg_release_queue_message (queue);
		}
	}

	queue->close_time = 0;

	msg_sync_refcount_increment (queue, nodeid);
	msg_sync_refcount_calculate (queue);

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t refcount = %u\n",
		    (unsigned int)(queue->refcount));

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_queueopenasync->source))
	{
		res_lib_msg_queueopenasync.header.size =
			sizeof (struct res_lib_msg_queueopenasync);
		res_lib_msg_queueopenasync.header.id =
			MESSAGE_RES_MSG_QUEUEOPENASYNC;
		res_lib_msg_queueopenasync.header.error = error;

		if (error == SA_AIS_OK) {
			msg_pd = api->ipc_private_data_get (req_exec_msg_queueopenasync->source.conn);
			memcpy (&cleanup->queue_name, &queue->queue_name, sizeof (SaNameT));
			cleanup->queue_handle = req_exec_msg_queueopenasync->queue_handle;
			cleanup->queue_id = queue->queue_id;
			res_lib_msg_queueopenasync.queue_id = queue->queue_id;
			list_init (&cleanup->list);
			list_add_tail (&cleanup->list, &msg_pd->queue_cleanup_list);
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
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_queueclose *req_exec_msg_queueclose =
		message;
	struct res_lib_msg_queueclose res_lib_msg_queueclose;
	SaAisErrorT error = SA_AIS_OK;
	struct queue_entry *queue = NULL;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgQueueClose\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t queue = %s (%u)\n",
		    (char *)(req_exec_msg_queueclose->queue_name.value),
		    (unsigned int)(req_exec_msg_queueclose->queue_id));

	queue = msg_find_queue (&queue_list_head,
		&req_exec_msg_queueclose->queue_name);
	if (queue == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	msg_sync_refcount_decrement (queue, nodeid);
	msg_sync_refcount_calculate (queue);

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t refcount = %u\n",
		    (unsigned int)(queue->refcount));

	if (queue->refcount == 0)
	{
		queue->close_time = api->timer_time_get();

		/* 
		 * If this queue is non-persistent and we are the lowest nodeid,
		 * create a timer that will expire based on the retention time
		 * (specified at queue creation time). When the timer expires,
		 * the queue will be deleted.
		 */
		if ((queue->create_attrs.creationFlags != SA_MSG_QUEUE_PERSISTENT) &&
		    (lowest_nodeid == api->totem_nodeid_get()))
		{
			api->timer_add_duration (
				queue->create_attrs.retentionTime,
				(void *)(queue), msg_expire_queue,
				&queue->timer_handle);
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
	}
}

static void message_handler_req_exec_msg_queuestatusget (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_queuestatusget *req_exec_msg_queuestatusget =
		message;
	struct res_lib_msg_queuestatusget res_lib_msg_queuestatusget;
	SaAisErrorT error = SA_AIS_OK;
	struct queue_entry *queue = NULL;

	int i;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgQueueStatusGet\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t queue = %s\n",
		    (char *)(req_exec_msg_queuestatusget->queue_name.value));

	queue = msg_find_queue (&queue_list_head,
		&req_exec_msg_queuestatusget->queue_name);
	if (queue == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	res_lib_msg_queuestatusget.queue_status.creationFlags =
		queue->create_attrs.creationFlags;
	res_lib_msg_queuestatusget.queue_status.retentionTime =
		queue->create_attrs.retentionTime;
	res_lib_msg_queuestatusget.queue_status.closeTime =
		queue->close_time;

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
		res_lib_msg_queuestatusget.queue_status.saMsgQueueUsage[i].queueSize =
			queue->priority[i].queue_size;
		res_lib_msg_queuestatusget.queue_status.saMsgQueueUsage[i].queueUsed =
			queue->priority[i].queue_used;
		res_lib_msg_queuestatusget.queue_status.saMsgQueueUsage[i].numberOfMessages =
			queue->priority[i].message_count;
	}

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t close_time = %llu\n",
		    (unsigned long long)(queue->close_time));
	log_printf (LOGSYS_LEVEL_NOTICE, "\t retention_time = %llu\n",
		    (unsigned long long)(queue->create_attrs.retentionTime));

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
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_queueretentiontimeset *req_exec_msg_queueretentiontimeset =
		message;
	struct res_lib_msg_queueretentiontimeset res_lib_msg_queueretentiontimeset;
	SaAisErrorT error = SA_AIS_OK;
	struct queue_entry *queue = NULL;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgQueueRetentionTimeSet\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t queue = %s (%u)\n",
		    (char *)(req_exec_msg_queueretentiontimeset->queue_name.value),
		    (unsigned int)(req_exec_msg_queueretentiontimeset->queue_id));

	queue = msg_find_queue (&queue_list_head,
		&req_exec_msg_queueretentiontimeset->queue_name);
	if (queue == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (queue->create_attrs.creationFlags == SA_MSG_QUEUE_PERSISTENT) {
		error = SA_AIS_ERR_BAD_OPERATION;
		goto error_exit;
	}

	/* 
	 * Note that if this queue has an active retention timer,
	 * changing the retention time has no effect on that timer.
	 * Alternative is to delete any active retention timer and
	 * replace with new timer using new retention time.
	 */

	queue->create_attrs.retentionTime =
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
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_queueunlink *req_exec_msg_queueunlink =
		message;
	struct res_lib_msg_queueunlink res_lib_msg_queueunlink;
	SaAisErrorT error = SA_AIS_OK;
	struct queue_entry *queue = NULL;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgQueueUnlink\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t queue = %s\n",
		    (char *)(req_exec_msg_queueunlink->queue_name.value));

	queue = msg_find_queue (&queue_list_head,
		&req_exec_msg_queueunlink->queue_name);
	if (queue == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	assert (queue->unlink_flag == 0);

	queue->unlink_flag = 1;

	if (queue->refcount == 0) {
		msg_release_queue (queue);
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
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_queuegroupcreate *req_exec_msg_queuegroupcreate =
		message;
	struct res_lib_msg_queuegroupcreate res_lib_msg_queuegroupcreate;
	SaAisErrorT error = SA_AIS_OK;
	struct group_entry *group = NULL;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgQueueGroupCreate\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t group = %s\n",
		    (char *)(req_exec_msg_queuegroupcreate->group_name.value));

	if ((global_group_count + 1) > MAX_NUM_QUEUE_GROUPS) {
		error = SA_AIS_ERR_NO_RESOURCES;
		goto error_exit;
	}

	group = msg_find_group (&group_list_head,
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
			sizeof (SaNameT));

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
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_queuegroupinsert *req_exec_msg_queuegroupinsert =
		message;
	struct res_lib_msg_queuegroupinsert res_lib_msg_queuegroupinsert;
	/* struct res_lib_msg_queuegrouptrack_callback res_lib_msg_queuegrouptrack_callback; */
	/* SaMsgQueueGroupNotificationT *notification = NULL; */
	SaAisErrorT error = SA_AIS_OK;
	struct group_entry *group = NULL;
	struct queue_entry *queue = NULL;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgQueueGroupInsert\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t group = %s\n",
		    (char *)(req_exec_msg_queuegroupinsert->group_name.value));
	log_printf (LOGSYS_LEVEL_NOTICE, "\t queue = %s\n",
		    (char *)(req_exec_msg_queuegroupinsert->queue_name.value));

	group = msg_find_group (&group_list_head,
		&req_exec_msg_queuegroupinsert->group_name);
	if (group == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	queue = msg_find_group_member (&group->queue_head,
		&req_exec_msg_queuegroupinsert->queue_name);
	if (queue != NULL) {
		error = SA_AIS_ERR_EXIST;
		goto error_exit;
	}

	queue = msg_find_queue (&queue_list_head,
		&req_exec_msg_queuegroupinsert->queue_name);
	if (queue == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (( group->member_count + 1) > MAX_NUM_QUEUES_PER_GROUP) {
		error = SA_AIS_ERR_NO_RESOURCES;
		goto error_exit;
	}

	if ((group->policy == SA_MSG_QUEUE_GROUP_ROUND_ROBIN) &&
	    (group->next_queue == NULL))
	{
		group->next_queue = queue;
	}

	queue->group = group;

	list_init (&queue->group_list);
	list_add_tail (&queue->group_list, &group->queue_head);

	group->member_count += 1;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t member_count = %u\n",
		    (unsigned int)(group->member_count));

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

		if (error == SA_AIS_OK) {
			if (group->track_flags & SA_TRACK_CHANGES) {
				/* !! */
			}

			if (group->track_flags & SA_TRACK_CHANGES_ONLY) {
				/* !! */
			}
		}
	}
}

static void message_handler_req_exec_msg_queuegroupremove (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_queuegroupremove *req_exec_msg_queuegroupremove =
		message;
	struct res_lib_msg_queuegroupremove res_lib_msg_queuegroupremove;
	/* struct res_lib_msg_queuegrouptrack_callback res_lib_msg_queuegrouptrack_callback; */
	/* SaMsgQueueGroupNotificationT *notification = NULL; */
	SaAisErrorT error = SA_AIS_OK;
	struct group_entry *group = NULL;
	struct queue_entry *queue = NULL;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgQueueGroupRemove\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t group = %s\n",
		    (char *)(req_exec_msg_queuegroupremove->group_name.value));
	log_printf (LOGSYS_LEVEL_NOTICE, "\t queue = %s\n",
		    (char *)(req_exec_msg_queuegroupremove->queue_name.value));

	group = msg_find_group (&group_list_head,
		&req_exec_msg_queuegroupremove->group_name);
	if (group == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	queue = msg_find_group_member (&group->queue_head,
		&req_exec_msg_queuegroupremove->queue_name);
	if (queue == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (group->next_queue == queue) {
		group->next_queue = msg_next_group_member (group);
	}

	queue->group = NULL;

	list_del (&queue->group_list);
	list_init (&queue->group_list);

	group->member_count -= 1;

	if (group->member_count == 0) {
		group->next_queue = NULL;
	}

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t member_count = %u\n",
		    (unsigned int)(group->member_count));

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

		if (error == SA_AIS_OK) {
			if (group->track_flags & SA_TRACK_CHANGES) {
				/* !! */
			}

			if (group->track_flags & SA_TRACK_CHANGES_ONLY) {
				/* !! */
			}
		}
	}
}

static void message_handler_req_exec_msg_queuegroupdelete (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_queuegroupdelete *req_exec_msg_queuegroupdelete =
		message;
	struct res_lib_msg_queuegroupdelete res_lib_msg_queuegroupdelete;
	SaAisErrorT error = SA_AIS_OK;
	struct group_entry *group = NULL;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgQueueGroupDelete\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t group = %s\n",
		    (char *)(req_exec_msg_queuegroupdelete->group_name.value));

	group = msg_find_group (&group_list_head,
		&req_exec_msg_queuegroupdelete->group_name);
	if (group == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	msg_release_group (group);

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
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_queuegrouptrack *req_exec_msg_queuegrouptrack =
		message;
	struct res_lib_msg_queuegrouptrack res_lib_msg_queuegrouptrack;
	/* struct res_lib_msg_queuegrouptrack_callback res_lib_msg_queuegrouptrack_callback; */
	/* SaMsgQueueGroupNotificationT *notification = NULL; */
	SaAisErrorT error = SA_AIS_OK;
	struct group_entry *group = NULL;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgQueueGroupTrack\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t group = %s\n",
		    (char *)(req_exec_msg_queuegrouptrack->group_name.value));

	group = msg_find_group (&group_list_head,
		&req_exec_msg_queuegrouptrack->group_name);
	if (group == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (req_exec_msg_queuegrouptrack->track_flags & SA_TRACK_CURRENT) {
		/* DEBUG */
		log_printf (LOGSYS_LEVEL_NOTICE, "\t SA_TRACK_CURRENT\n");
	}

	if (req_exec_msg_queuegrouptrack->track_flags & SA_TRACK_CHANGES) {
		group->track_flags = req_exec_msg_queuegrouptrack->track_flags;
	}

	if (req_exec_msg_queuegrouptrack->track_flags & SA_TRACK_CHANGES_ONLY) {
		group->track_flags = req_exec_msg_queuegrouptrack->track_flags;
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
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_queuegrouptrackstop *req_exec_msg_queuegrouptrackstop =
		message;
	struct res_lib_msg_queuegrouptrackstop res_lib_msg_queuegrouptrackstop;
	SaAisErrorT error = SA_AIS_OK;
	struct group_entry *group = NULL;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgQueueGroupTrackStop\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t group = %s\n",
		    (char *)(req_exec_msg_queuegrouptrackstop->group_name.value));

	group = msg_find_group (&group_list_head,
		&req_exec_msg_queuegrouptrackstop->group_name);
	if (group == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	group->track_flags = 0;

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
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_queuegroupnotificationfree *req_exec_msg_queuegroupnotificationfree =
		message;
	struct res_lib_msg_queuegroupnotificationfree res_lib_msg_queuegroupnotificationfree;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgQueueGroupNotificationFree\n");

/* error_exit: */
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
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_messagesend *req_exec_msg_messagesend =
		message;
	struct res_lib_msg_messagesend res_lib_msg_messagesend;
	struct res_lib_msg_messagereceived_callback res_lib_msg_messagereceived_callback;
	struct queue_cleanup *cleanup = NULL;
	SaAisErrorT error = SA_AIS_OK;
	SaUint8T priority;

	struct group_entry *group = NULL;
	struct queue_entry *queue = NULL;
	struct message_entry *msg = NULL;
	struct pending_entry *get = NULL;

	char *data = ((char *)(req_exec_msg_messagesend) +
		      sizeof (struct req_exec_msg_messagesend));

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgMessageSend\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t destination = %s\n",
		    (char *)(req_exec_msg_messagesend->destination.value));
	log_printf (LOGSYS_LEVEL_NOTICE, "\t data = %s\n", (char *)(data));

	group = msg_find_group (&group_list_head,
		&req_exec_msg_messagesend->destination);
	if (group == NULL) {
		queue = msg_find_queue (&queue_list_head,
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

	if (req_exec_msg_messagesend->message.size > MAX_MESSAGE_SIZE) {
		error = SA_AIS_ERR_TOO_BIG;
		goto error_exit;
	}

	priority = req_exec_msg_messagesend->message.priority;

	if ((queue->priority[priority].queue_size -
	     queue->priority[priority].queue_used) < req_exec_msg_messagesend->message.size)
	{
		error = SA_AIS_ERR_QUEUE_FULL;
		goto error_exit;
	}

	msg = malloc (sizeof (struct message_entry));
	if (msg == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}

	memset (msg, 0, sizeof (struct message_entry));
	memcpy (&msg->message, &req_exec_msg_messagesend->message, sizeof (SaMsgMessageT));

	msg->message.data = malloc (msg->message.size);
	if (msg->message.data == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}

	memset (msg->message.data, 0, msg->message.size);
	memcpy (msg->message.data, (char *)(data), msg->message.size);

	msg->sender_id = 0;
	msg->send_time = api->timer_time_get();

	if (list_empty (&queue->pending_head)) {
		list_add_tail (&msg->queue_list, &queue->message_head);
		list_add_tail (&msg->list, &queue->priority[(msg->message.priority)].message_head);
		queue->priority[(msg->message.priority)].queue_used += msg->message.size;
		queue->priority[(msg->message.priority)].message_count += 1;

		/* DEBUG */
		log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: queue=%s priority=%u queue_used=%llu\n",
			    (char *)(queue->queue_name.value),
			    (unsigned int)(msg->message.priority),
			    (unsigned long long)(queue->priority[(msg->message.priority)].queue_used));
	}
	else {
		get = list_entry (queue->pending_head.next, struct pending_entry, list);
		if (get == NULL) {
			error = SA_AIS_ERR_LIBRARY; /* ? */
			goto error_exit;
		}

		list_del (&get->list);
		list_init (&get->list);

		if (api->ipc_source_is_local (&get->source)) {
			msg_deliver_pending_message (get->source.conn, msg);
		}

		free (get);
	}

	if (group != NULL) {
		group->next_queue = msg_next_group_member (group);
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

		if ((queue->open_flags & SA_MSG_QUEUE_RECEIVE_CALLBACK) && (error == SA_AIS_OK))
		{
			res_lib_msg_messagereceived_callback.header.size =
				sizeof (struct res_lib_msg_messagereceived_callback);
			res_lib_msg_messagereceived_callback.header.id =
				MESSAGE_RES_MSG_MESSAGERECEIVED_CALLBACK;
			res_lib_msg_messagereceived_callback.header.error = SA_AIS_OK;

			cleanup = msg_find_queue_cleanup (
				req_exec_msg_messagesend->source.conn,
				&queue->queue_name, queue->queue_id);

			res_lib_msg_messagereceived_callback.queue_handle = cleanup->queue_handle;

			api->ipc_dispatch_send (
				req_exec_msg_messagesend->source.conn,
				&res_lib_msg_messagereceived_callback,
				sizeof (struct res_lib_msg_messagereceived_callback));
		}
	}

	/* ? */
	if ((error != SA_AIS_OK) && (msg != NULL)) {
		if (msg->message.data != NULL) {
			free (msg->message.data);
		}
		free (msg);
	}
}

static void message_handler_req_exec_msg_messagesendasync (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_messagesendasync *req_exec_msg_messagesendasync =
		message;
	struct res_lib_msg_messagesendasync res_lib_msg_messagesendasync;
	struct res_lib_msg_messagereceived_callback res_lib_msg_messagereceived_callback;
	struct res_lib_msg_messagedelivered_callback res_lib_msg_messagedelivered_callback;
	struct queue_cleanup *cleanup = NULL;
	SaAisErrorT error = SA_AIS_OK;
	SaUint8T priority;

	struct group_entry *group = NULL;
	struct queue_entry *queue = NULL;
	struct message_entry *msg = NULL;
	struct pending_entry *get = NULL;

	char *data = ((char *)(req_exec_msg_messagesendasync) +
		      sizeof (struct req_exec_msg_messagesendasync));

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgMessageSendAsync\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t destination = %s\n",
		    (char *)(req_exec_msg_messagesendasync->destination.value));
	log_printf (LOGSYS_LEVEL_NOTICE, "\t data = %s\n", (char *)(data));

	group = msg_find_group (&group_list_head,
		&req_exec_msg_messagesendasync->destination);
	if (group == NULL) {
		queue = msg_find_queue (&queue_list_head,
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
	if (req_exec_msg_messagesendasync->message.size > MAX_MESSAGE_SIZE) {
		error = SA_AIS_ERR_TOO_BIG;
		goto error_exit;
	}

	priority = req_exec_msg_messagesendasync->message.priority;

	if ((queue->priority[priority].queue_size -
	     queue->priority[priority].queue_used) < req_exec_msg_messagesendasync->message.size)
	{
		error = SA_AIS_ERR_QUEUE_FULL;
		goto error_exit;
	}

	msg = malloc (sizeof (struct message_entry));
	if (msg == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}

	memset (msg, 0, sizeof (struct message_entry));
	memcpy (&msg->message, &req_exec_msg_messagesendasync->message, sizeof (SaMsgMessageT));

	msg->message.data = malloc (msg->message.size);
	if (msg->message.data == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}

	memset (msg->message.data, 0, msg->message.size);
	memcpy (msg->message.data, (char *)(data), msg->message.size);

	msg->sender_id = 0;
	msg->send_time = api->timer_time_get();

	if (list_empty (&queue->pending_head)) {
		list_add_tail (&msg->queue_list, &queue->message_head);
		list_add_tail (&msg->list, &queue->priority[(msg->message.priority)].message_head);
		queue->priority[(msg->message.priority)].queue_used += msg->message.size;
		queue->priority[(msg->message.priority)].message_count += 1;

		/* DEBUG */
		log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: queue=%s priority=%u queue_used=%llu\n",
			    (char *)(queue->queue_name.value),
			    (unsigned int)(msg->message.priority),
			    (unsigned long long)(queue->priority[(msg->message.priority)].queue_used));
	}
	else {
		get = list_entry (queue->pending_head.next, struct pending_entry, list);
		if (get == NULL) {
			error = SA_AIS_ERR_LIBRARY; /* ? */
			goto error_exit;
		}

		list_del (&get->list);
		list_init (&get->list);

		if (api->ipc_source_is_local (&get->source)) {
			msg_deliver_pending_message (get->source.conn, msg);
		}

		free (get);
	}

	if (group != NULL) {
		group->next_queue = msg_next_group_member (group);
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

		if ((queue->open_flags & SA_MSG_QUEUE_RECEIVE_CALLBACK) && (error == SA_AIS_OK))
		{
			res_lib_msg_messagereceived_callback.header.size =
				sizeof (struct res_lib_msg_messagereceived_callback);
			res_lib_msg_messagereceived_callback.header.id =
				MESSAGE_RES_MSG_MESSAGERECEIVED_CALLBACK;
			res_lib_msg_messagereceived_callback.header.error = SA_AIS_OK;

			cleanup = msg_find_queue_cleanup (
				req_exec_msg_messagesendasync->source.conn,
				&queue->queue_name, queue->queue_id);

			res_lib_msg_messagereceived_callback.queue_handle = cleanup->queue_handle;

			api->ipc_dispatch_send (
				req_exec_msg_messagesendasync->source.conn,
				&res_lib_msg_messagereceived_callback,
				sizeof (struct res_lib_msg_messagereceived_callback));
		}
	}

	/* ? */
	if ((error != SA_AIS_OK) && (msg != NULL)) {
		if (msg->message.data != NULL) {
			free (msg->message.data);
		}
		free (msg);
	}
}

static void message_handler_req_exec_msg_messageget (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_messageget *req_exec_msg_messageget =
		message;
	struct res_lib_msg_messageget res_lib_msg_messageget;
	SaAisErrorT error = SA_AIS_OK;
	struct queue_entry *queue = NULL;
	struct message_entry *msg = NULL;
	struct pending_entry *get = NULL;
	struct iovec iov[2];

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgMessageGet\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t queue = %s (%u)\n",
		    (char *)(req_exec_msg_messageget->queue_name.value),
		    (unsigned int)(req_exec_msg_messageget->queue_id));

	queue = msg_find_queue (&queue_list_head,
		&req_exec_msg_messageget->queue_name);
	if (queue == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	msg = msg_get_message (queue);

	if (msg == NULL)
	{
		get = malloc (sizeof (struct pending_entry));
		if (get == NULL) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}

		memcpy (&get->source,
			&req_exec_msg_messageget->source,
			sizeof (mar_message_source_t));

		list_add_tail (&get->list, &queue->pending_head);

		/* start timer */

		return;
	}

	res_lib_msg_messageget.send_time = msg->send_time;
	res_lib_msg_messageget.sender_id = msg->sender_id;

	/* list_del (queue->priority[msg->message.priority].message_head.next); */

	list_del (&msg->list);
	list_del (&msg->queue_list);

	queue->priority[msg->message.priority].message_count -= 1;

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_messageget->source))
	{
		res_lib_msg_messageget.header.size =
			sizeof (struct res_lib_msg_messageget);
		res_lib_msg_messageget.header.id =
			MESSAGE_RES_MSG_MESSAGEGET;
		res_lib_msg_messageget.header.error = error;

		memcpy (&res_lib_msg_messageget.message, &msg->message,
			sizeof (SaMsgMessageT)); /* ? */

		iov[0].iov_base = &res_lib_msg_messageget;
		iov[0].iov_len = sizeof (struct res_lib_msg_messageget);

		if (error == SA_AIS_OK) {
			iov[1].iov_base = msg->message.data;
			iov[1].iov_len = msg->message.size;

			api->ipc_response_iov_send (req_exec_msg_messageget->source.conn, iov, 2);
		}
		else {
			api->ipc_response_iov_send (req_exec_msg_messageget->source.conn, iov, 1);
		}
	}

	if (msg != NULL) {
		free (msg);
	}
}

static void message_handler_req_exec_msg_messagedatafree (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_messagedatafree *req_exec_msg_messagedatafree =
		message;
	struct res_lib_msg_messagedatafree res_lib_msg_messagedatafree;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgMessageDataFree\n");

/* error_exit: */
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
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_messagecancel *req_exec_msg_messagecancel =
		message;
	struct res_lib_msg_messagecancel res_lib_msg_messagecancel;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgMessageCancel\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t queue = %s (%u)\n",
		    (char *)(req_exec_msg_messagecancel->queue_name.value),
		    (unsigned int)(req_exec_msg_messagecancel->queue_id));

/* error_exit: */
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
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_messagesendreceive *req_exec_msg_messagesendreceive =
		message;
	struct res_lib_msg_messagesendreceive res_lib_msg_messagesendreceive;
	SaAisErrorT error = SA_AIS_OK;
	SaUint8T priority;

	struct group_entry *group = NULL;
	struct queue_entry *queue = NULL;
	struct message_entry *msg = NULL;
	struct pending_entry *get = NULL;

	char *data = ((char *)(req_exec_msg_messagesendreceive) +
		      sizeof (struct req_exec_msg_messagesendreceive));

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgMessageSendReceive\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t destination = %s\n",
		    (char *)(req_exec_msg_messagesendreceive->destination.value));
	log_printf (LOGSYS_LEVEL_NOTICE, "\t data = %s\n", (char *)(data));

	group = msg_find_group (&group_list_head,
		&req_exec_msg_messagesendreceive->destination);
	if (group == NULL) {
		queue = msg_find_queue (&queue_list_head,
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

	if (req_exec_msg_messagesendreceive->message.size > MAX_MESSAGE_SIZE) {
		error = SA_AIS_ERR_TOO_BIG;
		goto error_exit;
	}

	priority = req_exec_msg_messagesendreceive->message.priority;

	if ((queue->priority[priority].queue_size -
	     queue->priority[priority].queue_used) < req_exec_msg_messagesendreceive->message.size)
	{
		error = SA_AIS_ERR_QUEUE_FULL;
		goto error_exit;
	}

	msg = malloc (sizeof (struct message_entry));
	if (msg == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}

	memset (msg, 0, sizeof (struct message_entry));
	memcpy (&msg->message, &req_exec_msg_messagesendreceive->message, sizeof (SaMsgMessageT));

	msg->message.data = malloc (msg->message.size);
	if (msg->message.data == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}

	memset (msg->message.data, 0, msg->message.size);
	memcpy (msg->message.data, (char *)(data), msg->message.size);

	msg->sender_id = req_exec_msg_messagesendreceive->sender_id;
	msg->send_time = api->timer_time_get();

	if (list_empty (&queue->pending_head)) {
		list_add_tail (&msg->queue_list, &queue->message_head);
		list_add_tail (&msg->list, &queue->priority[(msg->message.priority)].message_head);
		queue->priority[(msg->message.priority)].queue_used += msg->message.size;
		queue->priority[(msg->message.priority)].message_count += 1;

		/* DEBUG */
		log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: queue=%s priority=%u queue_used=%llu\n",
			    (char *)(queue->queue_name.value),
			    (unsigned int)(msg->message.priority),
			    (unsigned long long)(queue->priority[(msg->message.priority)].queue_used));
	}
	else {
		get = list_entry (queue->pending_head.next, struct pending_entry, list);
		if (get == NULL) {
			error = SA_AIS_ERR_LIBRARY; /* ? */
			goto error_exit;
		}

		list_del (&get->list);
		list_init (&get->list);

		if (api->ipc_source_is_local (&get->source)) {
			msg_deliver_pending_message (get->source.conn, msg);
		}

		free (get);
	}

	if (group != NULL) {
		group->next_queue = msg_next_group_member (group);
	}

error_exit:
	if (api->ipc_source_is_local (&req_exec_msg_messagesendreceive->source))
	{
		res_lib_msg_messagesendreceive.header.size =
			sizeof (struct res_lib_msg_messagesendreceive);
		res_lib_msg_messagesendreceive.header.id =
			MESSAGE_RES_MSG_MESSAGESENDRECEIVE;
		res_lib_msg_messagesendreceive.header.error = error;

		api->ipc_response_send (
			req_exec_msg_messagesendreceive->source.conn,
			&res_lib_msg_messagesendreceive,
			sizeof (struct res_lib_msg_messagesendreceive));
	}

	/* ? */
	if ((error != SA_AIS_OK) && (msg != NULL)) {
		if (msg->message.data != NULL) {
			free (msg->message.data);
		}
		free (msg);
	}
}

static void message_handler_req_exec_msg_messagereply (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_messagereply *req_exec_msg_messagereply =
		message;
	struct res_lib_msg_messagereply res_lib_msg_messagereply;
	SaAisErrorT error = SA_AIS_OK;

	char *data = ((char *)(req_exec_msg_messagereply) +
		      sizeof (struct req_exec_msg_messagereply));

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgMessageReply\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t sender_id = 0x%04x\n",
		    (unsigned int)(req_exec_msg_messagereply->sender_id));
	log_printf (LOGSYS_LEVEL_NOTICE, "\t data = %s\n", (char *)(data));

	if (req_exec_msg_messagereply->reply_message.size > MAX_REPLY_SIZE) {
		error = SA_AIS_ERR_TOO_BIG;
		goto error_exit;
	}

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
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_messagereplyasync *req_exec_msg_messagereplyasync =
		message;
	struct res_lib_msg_messagereplyasync res_lib_msg_messagereplyasync;
	SaAisErrorT error = SA_AIS_OK;

	char *data = ((char *)(req_exec_msg_messagereplyasync) +
		      sizeof (struct req_exec_msg_messagereplyasync));

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgMessageReplyAsync\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t sender_id = 0x%04x\n",
		    (unsigned int)(req_exec_msg_messagereplyasync->sender_id));
	log_printf (LOGSYS_LEVEL_NOTICE, "\t data = %s\n", (char *)(data));

	if (req_exec_msg_messagereplyasync->reply_message.size > MAX_REPLY_SIZE) {
		error = SA_AIS_ERR_TOO_BIG;
		goto error_exit;
	}

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
	}
}

static void message_handler_req_exec_msg_queuecapacitythresholdset (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_queuecapacitythresholdset *req_exec_msg_queuecapacitythresholdset =
		message;
	struct res_lib_msg_queuecapacitythresholdset res_lib_msg_queuecapacitythresholdset;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgQueueCapacityThresholdSet\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t queue = %s (%u)\n",
		    (char *)(req_exec_msg_queuecapacitythresholdset->queue_name.value),
		    (unsigned int)(req_exec_msg_queuecapacitythresholdset->queue_id));

/* error_exit: */
	if (api->ipc_source_is_local (&req_exec_msg_queuecapacitythresholdset->source))
	{
		res_lib_msg_queuecapacitythresholdset.header.size =
			sizeof (struct res_lib_msg_queuecapacitythresholdset);
		res_lib_msg_queuecapacitythresholdset.header.id =
			MESSAGE_RES_MSG_QUEUECAPACITYTHRESHOLDSET;
		res_lib_msg_queuecapacitythresholdset.header.error = error;

		api->ipc_response_send (
			req_exec_msg_queuecapacitythresholdset->source.conn,
			&res_lib_msg_queuecapacitythresholdset,
			sizeof (struct res_lib_msg_queuecapacitythresholdset));
	}
}

static void message_handler_req_exec_msg_queuecapacitythresholdget (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_queuecapacitythresholdget *req_exec_msg_queuecapacitythresholdget =
		message;
	struct res_lib_msg_queuecapacitythresholdget res_lib_msg_queuecapacitythresholdget;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgQueueCapacityThresholdGet\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t queue = %s (%u)\n",
		    (char *)(req_exec_msg_queuecapacitythresholdget->queue_name.value),
		    (unsigned int)(req_exec_msg_queuecapacitythresholdget->queue_id));

/* error_exit: */
	if (api->ipc_source_is_local (&req_exec_msg_queuecapacitythresholdget->source))
	{
		res_lib_msg_queuecapacitythresholdget.header.size =
			sizeof (struct res_lib_msg_queuecapacitythresholdget);
		res_lib_msg_queuecapacitythresholdget.header.id =
			MESSAGE_RES_MSG_QUEUECAPACITYTHRESHOLDGET;
		res_lib_msg_queuecapacitythresholdget.header.error = error;

		api->ipc_response_send (
			req_exec_msg_queuecapacitythresholdget->source.conn,
			&res_lib_msg_queuecapacitythresholdget,
			sizeof (struct res_lib_msg_queuecapacitythresholdget));
	}
}

static void message_handler_req_exec_msg_metadatasizeget (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_metadatasizeget *req_exec_msg_metadatasizeget =
		message;
	struct res_lib_msg_metadatasizeget res_lib_msg_metadatasizeget;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgMetadataSizeGet\n");

/* error_exit: */
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
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_limitget *req_exec_msg_limitget =
		message;
	struct res_lib_msg_limitget res_lib_msg_limitget;
	SaAisErrorT error = SA_AIS_OK;
	SaUint64T value = 0;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saMsgLimitGet\n");

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "\t limit_id = %u\n",
		    (unsigned int)(req_exec_msg_limitget->limit_id));

	switch (req_exec_msg_limitget->limit_id)
	{
	case SA_MSG_MAX_PRIORITY_AREA_SIZE_ID:
		value = MAX_PRIORITY_AREA_SIZE;
		break;
	case SA_MSG_MAX_QUEUE_SIZE_ID:
		value = MAX_QUEUE_SIZE;
		break;
	case SA_MSG_MAX_NUM_QUEUES_ID:
		value = MAX_NUM_QUEUES;
		break;
	case SA_MSG_MAX_NUM_QUEUE_GROUPS_ID:
		value = MAX_NUM_QUEUE_GROUPS;
		break;
	case SA_MSG_MAX_NUM_QUEUES_PER_GROUP_ID:
		value = MAX_NUM_QUEUES_PER_GROUP;
		break;
	case SA_MSG_MAX_MESSAGE_SIZE_ID:
		value = MAX_MESSAGE_SIZE;
		break;
	case SA_MSG_MAX_REPLY_SIZE_ID:
		value = MAX_REPLY_SIZE;
		break;
	default:
		error = SA_AIS_ERR_INVALID_PARAM;
		break;
	}

/* error_exit: */
	if (api->ipc_source_is_local (&req_exec_msg_limitget->source))
	{
		res_lib_msg_limitget.header.size =
			sizeof (struct res_lib_msg_limitget);
		res_lib_msg_limitget.header.id =
			MESSAGE_RES_MSG_LIMITGET;
		res_lib_msg_limitget.header.error = error;
		res_lib_msg_limitget.value = value;

		api->ipc_response_send (
			req_exec_msg_limitget->source.conn,
			&res_lib_msg_limitget,
			sizeof (struct res_lib_msg_limitget));
	}
}

static void message_handler_req_exec_msg_queue_timeout (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_queue_timeout *req_exec_msg_queue_timeout =
		message;
	struct queue_entry *queue = NULL;

	queue = msg_find_queue (&queue_list_head,
		&req_exec_msg_queue_timeout->queue_name);

	assert (queue != NULL);	/* ? */

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: retention timeout { queue = %s }\n",
		    (char *)(queue->queue_name.value));

	msg_release_queue (queue);

	return;
}

static void message_handler_req_exec_msg_sync_queue (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_sync_queue *req_exec_msg_sync_queue =
		message;
	struct queue_entry *queue = NULL;

	int i;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: sync queue\n");
	log_printf (LOGSYS_LEVEL_NOTICE, "\t queue = %s (%u)\n",
		    (char *)(&req_exec_msg_sync_queue->queue_name.value),
		    (unsigned int)(req_exec_msg_sync_queue->queue_id));

	if (memcmp (&req_exec_msg_sync_queue->ring_id,
		    &saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	queue = msg_find_queue_id (&sync_queue_list_head,
		&req_exec_msg_sync_queue->queue_name,
		req_exec_msg_sync_queue->queue_id);
	/* 
	 * This queue should not exist yet.
	 */
	assert (queue == NULL);

	queue = malloc (sizeof (struct queue_entry));
	if (queue == NULL) {
		corosync_fatal_error (COROSYNC_OUT_OF_MEMORY);
	}
	memset (queue, 0, sizeof (struct queue_entry));
	memcpy (&queue->queue_name,
		&req_exec_msg_sync_queue->queue_name,
		sizeof (SaNameT));
	memcpy (&queue->create_attrs,
		&req_exec_msg_sync_queue->create_attrs,
		sizeof (SaMsgQueueCreationAttributesT));

	queue->queue_id = req_exec_msg_sync_queue->queue_id;
	queue->open_flags = req_exec_msg_sync_queue->open_flags;

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
		queue->priority[i].queue_size = queue->create_attrs.size[i];
		list_init (&queue->priority[i].message_head);
	}

	list_init (&queue->group_list);
	list_init (&queue->queue_list);
	list_init (&queue->message_head);
	list_init (&queue->pending_head);

	list_add_tail (&queue->queue_list, &sync_queue_list_head);

	/* global_queue_count */

	if (queue->queue_id >= global_queue_id) {
		global_queue_id = queue->queue_id + 1;
	}

	return;
}

static void message_handler_req_exec_msg_sync_queue_message (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_sync_queue_message *req_exec_msg_sync_queue_message =
		message;
	struct queue_entry *queue = NULL;
	struct message_entry *msg = NULL;

	char *data = ((char *)(req_exec_msg_sync_queue_message) +
		      sizeof (struct req_exec_msg_sync_queue_message));

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: sync queue message\n");
	log_printf (LOGSYS_LEVEL_NOTICE, "\t queue = %s (%s)\n",
		    (char *)(&req_exec_msg_sync_queue_message->queue_name.value),
		    (char *)(data));

	if (memcmp (&req_exec_msg_sync_queue_message->ring_id,
		    &saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	queue = msg_find_queue_id (&sync_queue_list_head,
		&req_exec_msg_sync_queue_message->queue_name,
		req_exec_msg_sync_queue_message->queue_id);

	assert (queue != NULL);

	msg = malloc (sizeof (struct message_entry));
	if (msg == NULL) {
		corosync_fatal_error (COROSYNC_OUT_OF_MEMORY);
	}
	memset (msg, 0, sizeof (struct message_entry));
	memcpy (&msg->message, &req_exec_msg_sync_queue_message->message, sizeof (SaMsgMessageT));

	msg->message.data = malloc (msg->message.size);
	if (msg->message.data == NULL) {
		corosync_fatal_error (COROSYNC_OUT_OF_MEMORY);
	}
	memset (msg->message.data, 0, msg->message.size);
	memcpy (msg->message.data, (char *)(data), msg->message.size);

	msg->sender_id = req_exec_msg_sync_queue_message->sender_id;
	msg->send_time = req_exec_msg_sync_queue_message->send_time;

	list_add_tail (&msg->queue_list, &queue->message_head);
	list_add_tail (&msg->list, &queue->priority[(msg->message.priority)].message_head);
	queue->priority[(msg->message.priority)].queue_used += msg->message.size;
	queue->priority[(msg->message.priority)].message_count += 1;

	return;
}

static void message_handler_req_exec_msg_sync_queue_refcount (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_sync_queue_refcount *req_exec_msg_sync_queue_refcount =
		message;
	struct queue_entry *queue = NULL;

	unsigned int i;
	unsigned int j;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: sync refcount\n");

	if (memcmp (&req_exec_msg_sync_queue_refcount->ring_id,
		    &saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	queue = msg_find_queue_id (&sync_queue_list_head,
		&req_exec_msg_sync_queue_refcount->queue_name,
		req_exec_msg_sync_queue_refcount->queue_id);

	assert (queue != NULL);

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++)
	{
		if (req_exec_msg_sync_queue_refcount->refcount_set[i].nodeid == 0) {
			break;
		}

		if (msg_find_member_nodeid (req_exec_msg_sync_queue_refcount->refcount_set[i].nodeid) == 0) {
			continue;
		}

		for (j = 0; j < PROCESSOR_COUNT_MAX; j++)
		{
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
	log_printf (LOGSYS_LEVEL_NOTICE, "\t queue=%s refcount=%u\n",
		    (char *)(queue->queue_name.value),
		    (unsigned int)(queue->queue_id));

	return;
}

static void message_handler_req_exec_msg_sync_group (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_sync_group *req_exec_msg_sync_group =
		message;
	struct group_entry *group = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: sync group\n");
	log_printf (LOGSYS_LEVEL_NOTICE, "\t group = %s \n",
		    (char *)(&req_exec_msg_sync_group->group_name.value));

	if (memcmp (&req_exec_msg_sync_group->ring_id,
		    &saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	group = msg_find_group (&sync_group_list_head,
		&req_exec_msg_sync_group->group_name);

	/* 
	 * This group should not exist yet.
	 */
	assert (group == NULL);
	
	group = malloc (sizeof (struct group_entry));
	if (group == NULL) {
		corosync_fatal_error (COROSYNC_OUT_OF_MEMORY);
	}
	memset (group, 0, sizeof (struct group_entry));
	memcpy (&group->group_name,
		&req_exec_msg_sync_group->group_name,
		sizeof (SaNameT));

	group->track_flags = req_exec_msg_sync_group->track_flags;
	group->policy = req_exec_msg_sync_group->policy;

	list_init (&group->queue_head);
	list_init (&group->group_list);

	list_add_tail  (&group->group_list, &sync_group_list_head);

	/* global_group_count += 1; */

	return;
}

static void message_handler_req_exec_msg_sync_group_member (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_msg_sync_group_member *req_exec_msg_sync_group_member =
		message;
	struct group_entry *group = NULL;
	struct queue_entry *queue = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: sync group member\n");
	log_printf (LOGSYS_LEVEL_NOTICE, "\t group = %s\n",
		    (char *)(&req_exec_msg_sync_group_member->group_name.value));
	log_printf (LOGSYS_LEVEL_NOTICE, "\t queue = %s (%u)\n",
		    (char *)(&req_exec_msg_sync_group_member->queue_name.value),
		    (unsigned int)(req_exec_msg_sync_group_member->queue_id));

	if (memcmp (&req_exec_msg_sync_group_member->ring_id,
		    &saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	group = msg_find_group (&sync_group_list_head,
		&req_exec_msg_sync_group_member->group_name);
	queue = msg_find_queue_id (&sync_queue_list_head,
		&req_exec_msg_sync_group_member->queue_name,
		req_exec_msg_sync_group_member->queue_id);

	/* 
	 * Both the group and the queue must already exist.
	 */
	assert (group != NULL);
	assert (queue != NULL);

	queue->group = group;

	list_init (&queue->group_list);
	list_add_tail (&queue->group_list, &group->queue_head);

	group->member_count += 1;

	return;
}

static void message_handler_req_lib_msg_queueopen (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_queueopen *req_lib_msg_queueopen = msg;
	struct req_exec_msg_queueopen req_exec_msg_queueopen;
	struct iovec iovec;

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgQueueOpen\n");

	req_exec_msg_queueopen.header.size =
		sizeof (struct req_exec_msg_queueopen);
	req_exec_msg_queueopen.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEOPEN);

	api->ipc_source_set (&req_exec_msg_queueopen.source, conn);

	memcpy (&req_exec_msg_queueopen.queue_name,
		&req_lib_msg_queueopen->queue_name, sizeof (SaNameT));
	memcpy (&req_exec_msg_queueopen.create_attrs,
		&req_lib_msg_queueopen->create_attrs, sizeof (SaMsgQueueCreationAttributesT));

	req_exec_msg_queueopen.queue_handle =
		req_lib_msg_queueopen->queue_handle;
	req_exec_msg_queueopen.open_flags =
		req_lib_msg_queueopen->open_flags;
	req_exec_msg_queueopen.create_attrs_flag =
		req_lib_msg_queueopen->create_attrs_flag;
	req_exec_msg_queueopen.timeout =
		req_lib_msg_queueopen->timeout;

	iovec.iov_base = (char *)&req_exec_msg_queueopen;
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

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgQueueOpenAsync\n");

	req_exec_msg_queueopenasync.header.size =
		sizeof (struct req_exec_msg_queueopenasync);
	req_exec_msg_queueopenasync.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEOPENASYNC);

	api->ipc_source_set (&req_exec_msg_queueopenasync.source, conn);

	memcpy (&req_exec_msg_queueopenasync.queue_name,
		&req_lib_msg_queueopenasync->queue_name, sizeof (SaNameT));
	memcpy (&req_exec_msg_queueopenasync.create_attrs,
		&req_lib_msg_queueopenasync->create_attrs, sizeof (SaMsgQueueCreationAttributesT));

	req_exec_msg_queueopenasync.queue_handle =
		req_lib_msg_queueopenasync->queue_handle;
	req_exec_msg_queueopenasync.open_flags =
		req_lib_msg_queueopenasync->open_flags;
	req_exec_msg_queueopenasync.create_attrs_flag =
		req_lib_msg_queueopenasync->create_attrs_flag;
	req_exec_msg_queueopenasync.invocation =
		req_lib_msg_queueopenasync->invocation;

	iovec.iov_base = (char *)&req_exec_msg_queueopenasync;
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

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgQueueClose\n");

	req_exec_msg_queueclose.header.size =
		sizeof (struct req_exec_msg_queueclose);
	req_exec_msg_queueclose.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUECLOSE);

	api->ipc_source_set (&req_exec_msg_queueclose.source, conn);

	memcpy (&req_exec_msg_queueclose.queue_name,
		&req_lib_msg_queueclose->queue_name, sizeof (SaNameT));

	req_exec_msg_queueclose.queue_id =
		req_lib_msg_queueclose->queue_id;

	iovec.iov_base = (char *)&req_exec_msg_queueclose;
	iovec.iov_len = sizeof (req_exec_msg_queueclose);

	msg_release_queue_cleanup (conn,
		&req_lib_msg_queueclose->queue_name,
		req_lib_msg_queueclose->queue_id);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuestatusget (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_queuestatusget *req_lib_msg_queuestatusget = msg;
	struct req_exec_msg_queuestatusget req_exec_msg_queuestatusget;
	struct iovec iovec;

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgQueueStatusGet\n");

	req_exec_msg_queuestatusget.header.size =
		sizeof (struct req_exec_msg_queuestatusget);
	req_exec_msg_queuestatusget.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUESTATUSGET);

	api->ipc_source_set (&req_exec_msg_queuestatusget.source, conn);

	memcpy (&req_exec_msg_queuestatusget.queue_name,
		&req_lib_msg_queuestatusget->queue_name, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuestatusget;
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

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgQueueRetentionTimeSet\n");

	req_exec_msg_queueretentiontimeset.header.size =
		sizeof (struct req_exec_msg_queueretentiontimeset);
	req_exec_msg_queueretentiontimeset.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUERETENTIONTIMESET);

	api->ipc_source_set (&req_exec_msg_queueretentiontimeset.source, conn);

	memcpy (&req_exec_msg_queueretentiontimeset.queue_name,
		&req_lib_msg_queueretentiontimeset->queue_name, sizeof (SaNameT));

	req_exec_msg_queueretentiontimeset.queue_id =
		req_lib_msg_queueretentiontimeset->queue_id;
	req_exec_msg_queueretentiontimeset.retention_time =
		req_lib_msg_queueretentiontimeset->retention_time;

	iovec.iov_base = (char *)&req_exec_msg_queueretentiontimeset;
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

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgQueueUnlink\n");

	req_exec_msg_queueunlink.header.size =
		sizeof (struct req_exec_msg_queueunlink);
	req_exec_msg_queueunlink.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUESTATUSGET);

	api->ipc_source_set (&req_exec_msg_queueunlink.source, conn);

	memcpy (&req_exec_msg_queueunlink.queue_name,
		&req_lib_msg_queueunlink->queue_name, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queueunlink;
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

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgQueueGroupCreate\n");

	req_exec_msg_queuegroupcreate.header.size =
		sizeof (struct req_exec_msg_queuegroupcreate);
	req_exec_msg_queuegroupcreate.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPCREATE);

	api->ipc_source_set (&req_exec_msg_queuegroupcreate.source, conn);

	memcpy (&req_exec_msg_queuegroupcreate.group_name,
		&req_lib_msg_queuegroupcreate->group_name, sizeof (SaNameT));

	req_exec_msg_queuegroupcreate.policy =
		req_lib_msg_queuegroupcreate->policy;

	iovec.iov_base = (char *)&req_exec_msg_queuegroupcreate;
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

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgQueueGroupInsert\n");

	req_exec_msg_queuegroupinsert.header.size =
		sizeof (struct req_exec_msg_queuegroupinsert);
	req_exec_msg_queuegroupinsert.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPINSERT);

	api->ipc_source_set (&req_exec_msg_queuegroupinsert.source, conn);

	memcpy (&req_exec_msg_queuegroupinsert.group_name,
		&req_lib_msg_queuegroupinsert->group_name, sizeof (SaNameT));
	memcpy (&req_exec_msg_queuegroupinsert.queue_name,
		&req_lib_msg_queuegroupinsert->queue_name, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuegroupinsert;
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

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgQueueGroupRemove\n");

	req_exec_msg_queuegroupremove.header.size =
		sizeof (struct req_exec_msg_queuegroupremove);
	req_exec_msg_queuegroupremove.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPREMOVE);

	api->ipc_source_set (&req_exec_msg_queuegroupremove.source, conn);

	memcpy (&req_exec_msg_queuegroupremove.group_name,
		&req_lib_msg_queuegroupremove->group_name, sizeof (SaNameT));
	memcpy (&req_exec_msg_queuegroupremove.queue_name,
		&req_lib_msg_queuegroupremove->queue_name, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuegroupremove;
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

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgQueueGroupDelete\n");

	req_exec_msg_queuegroupdelete.header.size =
		sizeof (struct req_exec_msg_queuegroupdelete);
	req_exec_msg_queuegroupdelete.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPDELETE);

	api->ipc_source_set (&req_exec_msg_queuegroupdelete.source, conn);

	memcpy (&req_exec_msg_queuegroupdelete.group_name,
		&req_lib_msg_queuegroupdelete->group_name, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuegroupdelete;
	iovec.iov_len = sizeof (req_exec_msg_queuegroupdelete);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuegrouptrack (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_queuegrouptrack *req_lib_msg_queuegrouptrack = msg;
	struct req_exec_msg_queuegrouptrack req_exec_msg_queuegrouptrack;
	struct iovec iovec;

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgQueueGroupTrack\n");

	req_exec_msg_queuegrouptrack.header.size =
		sizeof (struct req_exec_msg_queuegrouptrack);
	req_exec_msg_queuegrouptrack.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPTRACK);

	api->ipc_source_set (&req_exec_msg_queuegrouptrack.source, conn);

	memcpy (&req_exec_msg_queuegrouptrack.group_name,
		&req_lib_msg_queuegrouptrack->group_name, sizeof (SaNameT));

	req_exec_msg_queuegrouptrack.track_flags =
		req_lib_msg_queuegrouptrack->track_flags;
	req_exec_msg_queuegrouptrack.buffer_flag =
		req_lib_msg_queuegrouptrack->buffer_flag;

	iovec.iov_base = (char *)&req_exec_msg_queuegrouptrack;
	iovec.iov_len = sizeof (req_exec_msg_queuegrouptrack);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuegrouptrackstop (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_queuegrouptrackstop *req_lib_msg_queuegrouptrackstop = msg;
	struct req_exec_msg_queuegrouptrackstop req_exec_msg_queuegrouptrackstop;
	struct iovec iovec;

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgQueueGroupTrackstopStop\n");

	req_exec_msg_queuegrouptrackstop.header.size =
		sizeof (struct req_exec_msg_queuegrouptrackstop);
	req_exec_msg_queuegrouptrackstop.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPTRACKSTOP);

	api->ipc_source_set (&req_exec_msg_queuegrouptrackstop.source, conn);

	memcpy (&req_exec_msg_queuegrouptrackstop.group_name,
		&req_lib_msg_queuegrouptrackstop->group_name, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuegrouptrackstop;
	iovec.iov_len = sizeof (req_exec_msg_queuegrouptrackstop);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuegroupnotificationfree (
	void *conn,
	const void *msg)
{
	struct req_exec_msg_queuegroupnotificationfree req_exec_msg_queuegroupnotificationfree;
	struct iovec iovec;

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgQueueGroupNotificationFree\n");

	req_exec_msg_queuegroupnotificationfree.header.size =
		sizeof (struct req_exec_msg_queuegroupnotificationfree);
	req_exec_msg_queuegroupnotificationfree.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPNOTIFICATIONFREE);

	api->ipc_source_set (&req_exec_msg_queuegroupnotificationfree.source, conn);

	iovec.iov_base = (char *)&req_exec_msg_queuegroupnotificationfree;
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

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgMessageSend\n");

	req_exec_msg_messagesend.header.size =
		sizeof (struct req_exec_msg_messagesend);
	req_exec_msg_messagesend.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGESEND);

	api->ipc_source_set (&req_exec_msg_messagesend.source, conn);

	memcpy (&req_exec_msg_messagesend.destination,
		&req_lib_msg_messagesend->destination, sizeof (SaNameT));
	memcpy (&req_exec_msg_messagesend.message,
		&req_lib_msg_messagesend->message, sizeof (SaMsgMessageT));

	req_exec_msg_messagesend.timeout =
		req_lib_msg_messagesend->timeout;

	iovec[0].iov_base = (char *)&req_exec_msg_messagesend;
	iovec[0].iov_len = sizeof (struct req_exec_msg_messagesend);

	iovec[1].iov_base = ((char *)req_lib_msg_messagesend) +
		sizeof (struct req_lib_msg_messagesend);
	iovec[1].iov_len = req_lib_msg_messagesend->header.size -
		sizeof (struct req_lib_msg_messagesend);

	req_exec_msg_messagesend.header.size += iovec[1].iov_len;

	if (iovec[1].iov_len > 0) {
		assert (api->totem_mcast (iovec, 2, TOTEM_AGREED) == 0);
	} else {
		assert (api->totem_mcast (iovec, 1, TOTEM_AGREED) == 0);
	}
}

static void message_handler_req_lib_msg_messagesendasync (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_messagesendasync *req_lib_msg_messagesendasync = msg;
	struct req_exec_msg_messagesendasync req_exec_msg_messagesendasync;
	struct iovec iovec[2];

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgMessageSendAsync\n");

	req_exec_msg_messagesendasync.header.size =
		sizeof (struct req_exec_msg_messagesendasync);
	req_exec_msg_messagesendasync.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGESENDASYNC);

	api->ipc_source_set (&req_exec_msg_messagesendasync.source, conn);

	memcpy (&req_exec_msg_messagesendasync.destination,
		&req_lib_msg_messagesendasync->destination, sizeof (SaNameT));
	memcpy (&req_exec_msg_messagesendasync.message,
		&req_lib_msg_messagesendasync->message, sizeof (SaMsgMessageT));

	req_exec_msg_messagesendasync.invocation =
		req_lib_msg_messagesendasync->invocation;

	iovec[0].iov_base = (char *)&req_exec_msg_messagesendasync;
	iovec[0].iov_len = sizeof (struct req_exec_msg_messagesendasync);

	iovec[1].iov_base = ((char *)req_lib_msg_messagesendasync) +
		sizeof (struct req_lib_msg_messagesendasync);
	iovec[1].iov_len = req_lib_msg_messagesendasync->header.size -
		sizeof (struct req_lib_msg_messagesendasync);

	req_exec_msg_messagesendasync.header.size += iovec[1].iov_len;

	if (iovec[1].iov_len > 0) {
		assert (api->totem_mcast (iovec, 2, TOTEM_AGREED) == 0);
	} else {
		assert (api->totem_mcast (iovec, 1, TOTEM_AGREED) == 0);
	}
}

static void message_handler_req_lib_msg_messageget (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_messageget *req_lib_msg_messageget = msg;
	struct req_exec_msg_messageget req_exec_msg_messageget;
	struct iovec iovec;

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgMessageGet\n");

	req_exec_msg_messageget.header.size =
		sizeof (struct req_exec_msg_messageget);
	req_exec_msg_messageget.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGEGET);

	api->ipc_source_set (&req_exec_msg_messageget.source, conn);

	memcpy (&req_exec_msg_messageget.queue_name,
		&req_lib_msg_messageget->queue_name, sizeof (SaNameT));

	req_exec_msg_messageget.queue_id =
		req_lib_msg_messageget->queue_id;
	req_exec_msg_messageget.timeout =
		req_lib_msg_messageget->timeout;

	iovec.iov_base = (char *)&req_exec_msg_messageget;
	iovec.iov_len = sizeof (req_exec_msg_messageget);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_messagedatafree (
	void *conn,
	const void *msg)
{
	struct req_exec_msg_messagedatafree req_exec_msg_messagedatafree;
	struct iovec iovec;

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgMessageDataFree\n");

	req_exec_msg_messagedatafree.header.size =
		sizeof (struct req_exec_msg_messagedatafree);
	req_exec_msg_messagedatafree.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGEDATAFREE);

	api->ipc_source_set (&req_exec_msg_messagedatafree.source, conn);

	iovec.iov_base = (char *)&req_exec_msg_messagedatafree;
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

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgMessageCancel\n");

	req_exec_msg_messagecancel.header.size =
		sizeof (struct req_exec_msg_messagecancel);
	req_exec_msg_messagecancel.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGECANCEL);

	api->ipc_source_set (&req_exec_msg_messagecancel.source, conn);

	memcpy (&req_exec_msg_messagecancel.queue_name,
		&req_lib_msg_messagecancel->queue_name, sizeof (SaNameT));

	req_exec_msg_messagecancel.queue_id =
		req_lib_msg_messagecancel->queue_id;

	iovec.iov_base = (char *)&req_exec_msg_messagecancel;
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

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgMessageSendReceive\n");

	req_exec_msg_messagesendreceive.header.size =
		sizeof (struct req_exec_msg_messagesendreceive);
	req_exec_msg_messagesendreceive.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGESENDRECEIVE);

	api->ipc_source_set (&req_exec_msg_messagesendreceive.source, conn);

	memcpy (&req_exec_msg_messagesendreceive.destination,
		&req_lib_msg_messagesendreceive->destination, sizeof (SaNameT));
	memcpy (&req_exec_msg_messagesendreceive.message,
		&req_lib_msg_messagesendreceive->message, sizeof (SaMsgMessageT));

	req_exec_msg_messagesendreceive.timeout =
		req_lib_msg_messagesendreceive->timeout;
	req_exec_msg_messagesendreceive.sender_id =
		(SaMsgSenderIdT)((global_sender_id++) |
		((unsigned long long)req_exec_msg_messagesendreceive.source.nodeid) << 32);

	iovec[0].iov_base = (char *)&req_exec_msg_messagesendreceive;
	iovec[0].iov_len = sizeof (req_exec_msg_messagesendreceive);

	iovec[1].iov_base = ((char *)req_lib_msg_messagesendreceive) +
		sizeof (struct req_lib_msg_messagesendreceive);
	iovec[1].iov_len = req_lib_msg_messagesendreceive->header.size -
		sizeof (struct req_lib_msg_messagesendreceive);

	req_exec_msg_messagesendreceive.header.size += iovec[1].iov_len;

	if (iovec[1].iov_len > 0) {
		assert (api->totem_mcast (iovec, 2, TOTEM_AGREED) == 0);
	} else {
		assert (api->totem_mcast (iovec, 1, TOTEM_AGREED) == 0);
	}
}

static void message_handler_req_lib_msg_messagereply (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_messagereply *req_lib_msg_messagereply = msg;
	struct req_exec_msg_messagereply req_exec_msg_messagereply;
	struct iovec iovec;

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgMessageReply\n");

	req_exec_msg_messagereply.header.size =
		sizeof (struct req_exec_msg_messagereply);
	req_exec_msg_messagereply.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGEREPLY);

	api->ipc_source_set (&req_exec_msg_messagereply.source, conn);

	memcpy (&req_exec_msg_messagereply.reply_message,
		&req_lib_msg_messagereply->reply_message, sizeof (SaMsgMessageT));

	req_exec_msg_messagereply.sender_id =
		req_lib_msg_messagereply->sender_id;
	req_exec_msg_messagereply.timeout =
		req_lib_msg_messagereply->timeout;

	iovec.iov_base = (char *)&req_exec_msg_messagereply;
	iovec.iov_len = sizeof (req_exec_msg_messagereply);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_messagereplyasync (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_messagereplyasync *req_lib_msg_messagereplyasync = msg;
	struct req_exec_msg_messagereplyasync req_exec_msg_messagereplyasync;
	struct iovec iovec;

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgMessageReplyAsync\n");

	req_exec_msg_messagereplyasync.header.size =
		sizeof (struct req_exec_msg_messagereplyasync);
	req_exec_msg_messagereplyasync.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGEREPLYASYNC);

	api->ipc_source_set (&req_exec_msg_messagereplyasync.source, conn);

	memcpy (&req_exec_msg_messagereplyasync.reply_message,
		&req_lib_msg_messagereplyasync->reply_message, sizeof (SaMsgMessageT));

	req_exec_msg_messagereplyasync.sender_id =
		req_lib_msg_messagereplyasync->sender_id;
	req_exec_msg_messagereplyasync.invocation =
		req_lib_msg_messagereplyasync->invocation;

	iovec.iov_base = (char *)&req_exec_msg_messagereplyasync;
	iovec.iov_len = sizeof (req_exec_msg_messagereplyasync);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuecapacitythresholdset (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_queuecapacitythresholdset *req_lib_msg_queuecapacitythresholdset = msg;
	struct req_exec_msg_queuecapacitythresholdget req_exec_msg_queuecapacitythresholdset;
	struct iovec iovec;

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgQueueCapacityThresholdSet\n");

	req_exec_msg_queuecapacitythresholdset.header.size =
		sizeof (struct req_exec_msg_queuecapacitythresholdset);
	req_exec_msg_queuecapacitythresholdset.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUECAPACITYTHRESHOLDSET);

	api->ipc_source_set (&req_exec_msg_queuecapacitythresholdset.source, conn);

	memcpy (&req_exec_msg_queuecapacitythresholdset.queue_name,
		&req_lib_msg_queuecapacitythresholdset->queue_name, sizeof (SaNameT));

	req_exec_msg_queuecapacitythresholdset.queue_id =
		req_lib_msg_queuecapacitythresholdset->queue_id;

	iovec.iov_base = (char *)&req_exec_msg_queuecapacitythresholdset;
	iovec.iov_len = sizeof (req_exec_msg_queuecapacitythresholdset);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuecapacitythresholdget (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_queuecapacitythresholdget *req_lib_msg_queuecapacitythresholdget = msg;
	struct req_exec_msg_queuecapacitythresholdget req_exec_msg_queuecapacitythresholdget;
	struct iovec iovec;

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgQueueCapacityThresholdGet\n");

	req_exec_msg_queuecapacitythresholdget.header.size =
		sizeof (struct req_exec_msg_queuecapacitythresholdget);
	req_exec_msg_queuecapacitythresholdget.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUECAPACITYTHRESHOLDGET);

	api->ipc_source_set (&req_exec_msg_queuecapacitythresholdget.source, conn);

	memcpy (&req_exec_msg_queuecapacitythresholdget.queue_name,
		&req_lib_msg_queuecapacitythresholdget->queue_name, sizeof (SaNameT));

	req_exec_msg_queuecapacitythresholdget.queue_id =
		req_lib_msg_queuecapacitythresholdget->queue_id;

	iovec.iov_base = (char *)&req_exec_msg_queuecapacitythresholdget;
	iovec.iov_len = sizeof (req_exec_msg_queuecapacitythresholdget);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_metadatasizeget (
	void *conn,
	const void *msg)
{
	struct req_exec_msg_metadatasizeget req_exec_msg_metadatasizeget;
	struct iovec iovec;

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgMetadataSizeGet\n");

	req_exec_msg_metadatasizeget.header.size =
		sizeof (struct req_exec_msg_metadatasizeget);
	req_exec_msg_metadatasizeget.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_METADATASIZEGET);

	api->ipc_source_set (&req_exec_msg_metadatasizeget.source, conn);

	iovec.iov_base = (char *)&req_exec_msg_metadatasizeget;
	iovec.iov_len = sizeof (req_exec_msg_metadatasizeget);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_limitget (
	void *conn,
	const void *msg)
{
	const struct req_lib_msg_limitget *req_lib_msg_limitget = msg;
	struct req_exec_msg_limitget req_exec_msg_limitget;
	struct iovec iovec;

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saMsgLimitGet\n");

	req_exec_msg_limitget.header.size =
		sizeof (struct req_exec_msg_limitget);
	req_exec_msg_limitget.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_LIMITGET);

	api->ipc_source_set (&req_exec_msg_limitget.source, conn);

	req_exec_msg_limitget.limit_id = req_lib_msg_limitget->limit_id;

	iovec.iov_base = (char *)&req_exec_msg_limitget;
	iovec.iov_len = sizeof (req_exec_msg_limitget);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}
