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
#include <arpa/inet.h>

#include <corosync/ipc_gen.h>
#include <corosync/mar_gen.h>
#include <corosync/swab.h>
#include <corosync/list.h>
#include <corosync/engine/coroapi.h>
#include <corosync/engine/logsys.h>
#include <corosync/lcr/lcr_comp.h>

#include "../include/saAis.h"
#include "../include/saMsg.h"
#include "../include/ipc_msg.h"

LOGSYS_DECLARE_SUBSYS ("MSG", LOG_INFO);

enum msg_exec_message_req_types {
	MESSAGE_REQ_EXEC_MSG_QUEUEOPEN = 0,
	MESSAGE_REQ_EXEC_MSG_QUEUECLOSE = 1,
	MESSAGE_REQ_EXEC_MSG_QUEUESTATUSGET = 2,
	MESSAGE_REQ_EXEC_MSG_QUEUEUNLINK = 3,
	MESSAGE_REQ_EXEC_MSG_QUEUEGROUPCREATE = 4,
	MESSAGE_REQ_EXEC_MSG_QUEUEGROUPINSERT = 5,
	MESSAGE_REQ_EXEC_MSG_QUEUEGROUPREMOVE = 6,
	MESSAGE_REQ_EXEC_MSG_QUEUEGROUPDELETE = 7,
	MESSAGE_REQ_EXEC_MSG_QUEUEGROUPTRACK = 8,
	MESSAGE_REQ_EXEC_MSG_QUEUEGROUPTRACKSTOP = 9,
	MESSAGE_REQ_EXEC_MSG_MESSAGESEND = 10,
	MESSAGE_REQ_EXEC_MSG_MESSAGEGET = 11,
	MESSAGE_REQ_EXEC_MSG_MESSAGECANCEL = 12,
	MESSAGE_REQ_EXEC_MSG_MESSAGESENDRECEIVE = 13,
	MESSAGE_REQ_EXEC_MSG_MESSAGEREPLY = 14,
	MESSAGE_REQ_EXEC_MSG_SYNC_QUEUE = 15,
	MESSAGE_REQ_EXEC_MSG_SYNC_QUEUE_ENTRY = 16,
	MESSAGE_REQ_EXEC_MSG_SYNC_GROUP = 17,
	MESSAGE_REQ_EXEC_MSG_SYNC_GROUP_ENTRY = 18,
};

struct message_entry {
	SaTimeT time;
	SaMsgMessageT message;
	struct list_head list;
};

struct message_queue {
	SaNameT name;
	SaUint8T refcount;
	SaMsgQueueGroupChangesT change;
	struct list_head group_list;
	struct list_head queue_list;
	struct list_head message_head;
};

struct queue_group {
	SaNameT name;
	SaUint8T track_flags;
	SaMsgQueueGroupPolicyT policy;
	struct message_queue *rr_queue;
	struct list_head group_list;
	struct list_head queue_head;
};

enum sync_state {
	SYNC_STATE_GROUP,
	SYNC_STATE_QUEUE,
};

enum iteration_state {
	ITERATION_STATE_GROUP,
	ITERATION_STATE_QUEUE,
};

/*
struct queue_group_entry {
	SaMsgQueueGroupChangesT change;
	struct message_queue *message_queue;
	struct list_head list;
};
*/

/*
struct queue_cleanup {
	struct message_queue *queue;
	SaMsgResourceHandleT queue_handle;
	struct list_head queue_lock_list_head;
	struct list_head list;
};
*/

DECLARE_LIST_INIT(queue_list_head);
DECLARE_LIST_INIT(group_list_head);

DECLARE_LIST_INIT(sync_queue_list_head);
DECLARE_LIST_INIT(sync_group_list_head);

static struct corosync_api_v1 *api;

static int msg_exec_init_fn (struct corosync_api_v1 *);

static int msg_lib_exit_fn (void *conn);

static int msg_lib_init_fn (void *conn);

static enum sync_state my_sync_state;

static enum iteration_state my_iteration_state;

static struct list_head *my_iteration_state_group;

static struct list_head *my_iteration_state_queue;

static struct list_head *my_iteration_state_entry;

static unsigned int my_member_list[PROCESSOR_COUNT_MAX];

static unsigned int my_member_list_entries = 0;

static unsigned int my_lowest_nodeid = 0;

/*
static int msg_dump_fn (void);
*/

static void message_handler_req_exec_msg_queueopen (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queueclose (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuestatusget (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queueunlink (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegroupcreate (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegroupinsert (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegroupremove (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegroupdelete (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegrouptrack (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_queuegrouptrackstop (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagesend (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messageget (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagecancel (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagesendreceive (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_messagereply (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_sync_queue (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_sync_queue_entry (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_sync_group (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_msg_sync_group_entry (
	void *message,
	unsigned int nodeid);

static void message_handler_req_lib_msg_queueopen (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_queueopenasync (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_queueclose (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_queuestatusget (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_queueunlink (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_queuegroupcreate (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_queuegroupinsert (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_queuegroupremove (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_queuegroupdelete (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_queuegrouptrack (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_queuegrouptrackstop (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_messagesend (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_messagesendasync (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_messageget (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_messagecancel (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_messagesendreceive (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_messagereply (
	void *conn,
	void *msg);

static void message_handler_req_lib_msg_messagereplyasync (
	void *conn,
	void *msg);

static void msg_sync_init (void);
static void msg_sync_activate (void);
static int  msg_sync_process (void);
static void msg_sync_abort(void);

void queue_entry_release (struct message_entry *entry);
void queue_release (struct message_queue *queue);
void grouo_release (struct queue_group *group);

static struct memb_ring_id my_saved_ring_id;

static void msg_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id);

struct msg_pd {
	struct list_head queue_list;
	struct list_head queue_cleanup_list;
};

/*
 * Executive Handler Definition
 */
struct corosync_lib_handler msg_lib_engine[] =
{
	{ /* 0 */
		.lib_handler_fn		= message_handler_req_lib_msg_queueopen,
		.response_size		= sizeof (struct res_lib_msg_queueopen),
		.response_id		= MESSAGE_RES_MSG_QUEUEOPEN,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn		= message_handler_req_lib_msg_queueopenasync,
		.response_size		= sizeof (struct res_lib_msg_queueopenasync),
		.response_id		= MESSAGE_RES_MSG_QUEUEOPENASYNC,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn		= message_handler_req_lib_msg_queueclose,
		.response_size		= sizeof (struct res_lib_msg_queueclose),
		.response_id		= MESSAGE_RES_MSG_QUEUECLOSE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn		= message_handler_req_lib_msg_queuestatusget,
		.response_size		= sizeof (struct res_lib_msg_queuestatusget),
		.response_id		= MESSAGE_RES_MSG_QUEUESTATUSGET,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn		= message_handler_req_lib_msg_queueunlink,
		.response_size		= sizeof (struct res_lib_msg_queueunlink),
		.response_id		= MESSAGE_RES_MSG_QUEUEUNLINK,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 5 */
		.lib_handler_fn		= message_handler_req_lib_msg_queuegroupcreate,
		.response_size		= sizeof (struct res_lib_msg_queuegroupcreate),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPCREATE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 6 */
		.lib_handler_fn		= message_handler_req_lib_msg_queuegroupinsert,
		.response_size		= sizeof (struct res_lib_msg_queuegroupinsert),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPINSERT,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 7 */
		.lib_handler_fn		= message_handler_req_lib_msg_queuegroupremove,
		.response_size		= sizeof (struct res_lib_msg_queuegroupremove),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPREMOVE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 8 */
		.lib_handler_fn		= message_handler_req_lib_msg_queuegroupdelete,
		.response_size		= sizeof (struct res_lib_msg_queuegroupdelete),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPDELETE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 9 */
		.lib_handler_fn		= message_handler_req_lib_msg_queuegrouptrack,
		.response_size		= sizeof (struct res_lib_msg_queuegrouptrack),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPTRACK,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 10 */
		.lib_handler_fn		= message_handler_req_lib_msg_queuegrouptrackstop,
		.response_size		= sizeof (struct res_lib_msg_queuegrouptrackstop),
		.response_id		= MESSAGE_RES_MSG_QUEUEGROUPTRACKSTOP,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 11 */
		.lib_handler_fn		= message_handler_req_lib_msg_messagesend,
		.response_size		= sizeof (struct res_lib_msg_messagesend),
		.response_id		= MESSAGE_RES_MSG_MESSAGESEND,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 12 */
		.lib_handler_fn		= message_handler_req_lib_msg_messagesendasync,
		.response_size		= sizeof (struct res_lib_msg_messagesendasync),
		.response_id		= MESSAGE_RES_MSG_MESSAGESENDASYNC,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 13 */
		.lib_handler_fn		= message_handler_req_lib_msg_messageget,
		.response_size		= sizeof (struct res_lib_msg_messageget),
		.response_id		= MESSAGE_RES_MSG_MESSAGEGET,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 14 */
		.lib_handler_fn		= message_handler_req_lib_msg_messagecancel,
		.response_size		= sizeof (struct res_lib_msg_messagecancel),
		.response_id		= MESSAGE_RES_MSG_MESSAGECANCEL,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 15 */
		.lib_handler_fn		= message_handler_req_lib_msg_messagesendreceive,
		.response_size		= sizeof (struct res_lib_msg_messagesendreceive),
		.response_id		= MESSAGE_RES_MSG_MESSAGESENDRECEIVE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 16 */
		.lib_handler_fn		= message_handler_req_lib_msg_messagereply,
		.response_size		= sizeof (struct res_lib_msg_messagereply),
		.response_id		= MESSAGE_RES_MSG_MESSAGEREPLY,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 17 */
		.lib_handler_fn		= message_handler_req_lib_msg_messagereplyasync,
		.response_size		= sizeof (struct res_lib_msg_messagereplyasync),
		.response_id		= MESSAGE_RES_MSG_MESSAGEREPLYASYNC,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
};


static struct corosync_exec_handler msg_exec_engine[] = {
	{
		.exec_handler_fn		= message_handler_req_exec_msg_queueopen,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_queueclose,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_queuestatusget,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_queueunlink,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_queuegroupcreate,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_queuegroupinsert,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_queuegroupremove,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_queuegroupdelete,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_queuegrouptrack,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_queuegrouptrackstop,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_messagesend,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_messageget,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_messagecancel,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_messagesendreceive,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_messagereply,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_sync_queue,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_sync_queue_entry,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_sync_group,
	},
	{
		.exec_handler_fn		= message_handler_req_exec_msg_sync_group_entry,
	},
};

struct corosync_service_engine msg_service_engine = {
	.name				= "openais message service B.01.01",
	.id				= MSG_SERVICE,
	.private_data_size		= sizeof (struct msg_pd),
	.flow_control			= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED, 
	.lib_init_fn			= msg_lib_init_fn,
	.lib_exit_fn			= msg_lib_exit_fn,
	.lib_engine			= msg_lib_engine,
	.lib_engine_count		= sizeof (msg_lib_engine) / sizeof (struct corosync_lib_handler),
	.exec_init_fn			= msg_exec_init_fn,
	.exec_dump_fn			= NULL,
	.exec_engine			= msg_exec_engine,
	.exec_engine_count		= sizeof (msg_exec_engine) / sizeof (struct corosync_exec_handler),
	.confchg_fn			= msg_confchg_fn,
	.sync_init			= msg_sync_init,
	.sync_process			= msg_sync_process,
	.sync_activate			= msg_sync_activate,
	.sync_abort			= msg_sync_abort,
};

static struct corosync_service_engine *msg_get_engine_ver0 (void);

static struct corosync_service_engine_iface_ver0 msg_service_engine_iface = {
	.corosync_get_service_engine_ver0	= msg_get_engine_ver0
};

static struct lcr_iface openais_msg_ver0[1] = {
	{
		.name			= "openais_msg",
		.version		= 0,
		.versions_replace	= 0,
		.versions_replace_count = 0,
		.dependencies		= 0,
		.dependency_count	= 0,
		.constructor		= NULL,
		.destructor		= NULL,
		.interfaces		= NULL
	}
};

static struct lcr_comp msg_comp_ver0 = {
	.iface_count			= 1,
	.ifaces				= openais_msg_ver0
};

static struct corosync_service_engine *msg_get_engine_ver0 (void)
{
	return (&msg_service_engine);
}

__attribute__ ((constructor)) static void register_this_component (void) {
	lcr_interfaces_set (&openais_msg_ver0[0], &msg_service_engine_iface);

	lcr_component_register (&msg_comp_ver0);
}

/*
 * All data types used for executive messages
 */
struct req_exec_msg_queueopen {
	mar_req_header_t header;
	mar_message_source_t source;
	int async_call;
	SaNameT queue_name;
	SaInvocationT invocation;
	SaMsgQueueHandleT queue_handle;
	SaMsgQueueCreationAttributesT creation_attributes;
	SaMsgQueueOpenFlagsT openFlags;
	SaTimeT timeout;
};

struct req_exec_msg_queueclose {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
};

struct req_exec_msg_queuestatusget {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
};

struct req_exec_msg_queueunlink {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
};

struct req_exec_msg_queuegroupcreate {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_group_name;
	SaMsgQueueGroupPolicyT policy;
};

struct req_exec_msg_queuegroupinsert {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
	SaNameT queue_group_name;
};

struct req_exec_msg_queuegroupremove {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
	SaNameT queue_group_name;
};

struct req_exec_msg_queuegroupdelete {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_group_name;
};

struct req_exec_msg_queuegrouptrack {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_group_name;
	SaUint8T track_flags;
	SaUint8T buffer_flag;
};

struct req_exec_msg_queuegrouptrackstop {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_group_name;
};

struct req_exec_msg_messagesend {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT destination;
	SaTimeT timeout;
	SaMsgMessageT message;
	SaInvocationT invocation;
	SaMsgAckFlagsT ack_flags;
	int async_call;
};

struct req_exec_msg_messageget {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
};

struct req_exec_msg_messagecancel {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
};

struct req_exec_msg_messagesendreceive {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
};

struct req_exec_msg_messagereply {
	mar_req_header_t header;
	mar_message_source_t source;
	SaNameT queue_name;
	int async_call;
};

struct req_exec_msg_sync_queue {
	mar_req_header_t header;
	struct memb_ring_id ring_id;
	SaNameT group_name;
	SaNameT queue_name;
	SaMsgQueueGroupChangesT change;
};

struct req_exec_msg_sync_queue_entry {
	mar_req_header_t header;
	struct memb_ring_id ring_id;
	SaNameT queue_name;
	SaTimeT time;
	SaMsgMessageT message;
};

struct req_exec_msg_sync_group {
	mar_req_header_t header;
	struct memb_ring_id ring_id;
	SaNameT group_name;
	SaUint8T track_flags;
	SaMsgQueueGroupPolicyT policy;
	SaNameT rr_queue_name;
};

struct req_exec_msg_sync_group_entry {
	mar_req_header_t header;
	struct memb_ring_id ring_id;
	SaNameT group_name;
	SaNameT queue_name;
};

char *getSaNameT (SaNameT *name)
{
#if 0
        static char ret_name[300];

        memset (ret_name, 0, sizeof (ret_name));
        if (name->length > 299) {
                memcpy (ret_name, name->value, 299);
        } else {

                memcpy (ret_name, name->value, name->length);
        }
        return (ret_name);
#endif
// TODO
        return ((char *)name->value);
}

int name_match(SaNameT *name1, SaNameT *name2)
{
	if (name1->length == name2->length) {
	return ((strncmp ((char *)name1->value, (char *)name2->value, name1->length)) == 0);
	}
	return 0;
}

SaTimeT cluster_time_now(void)
{
	struct timeval tv;
	SaTimeT time_now;

	if (gettimeofday(&tv, 0)) {
		return 0ULL;
	}

	time_now = (SaTimeT)(tv.tv_sec) * 1000000000ULL;
	time_now += (SaTimeT)(tv.tv_usec) * 1000ULL;

	return time_now;
}

#if 0
static void print_entry_list (struct message_queue *queue)
{
	struct list_head *list;
	struct message_entry *entry;

	for (list = queue->message_head.next;
	     list != &queue->message_head;
	     list = list->next)
	{
		entry = list_entry (list, struct message_entry, list);

		log_printf (LOG_LEVEL_NOTICE, "[DEBUG]: print_entry_list ( %s )\n",
			    (char *)(entry->message.data));
	}
}
#endif

#if 0
static void print_queue_list (struct list_head *head)
{
	struct list_head *list;
	struct message_queue *queue;

	for (list = head->next;
	     list != head;
	     list = list->next)
	{
		queue = list_entry (list, struct message_queue, queue_list);

		log_printf (LOG_LEVEL_NOTICE, "[DEBUG]: print_queue_list ( %s )\n",
			    getSaNameT (&queue->name));
	}
}
#endif

#if 0
static void print_group_list (struct list_head *head)
{
	struct list_head *list;
	struct queue_group *group;

	for (list = head->next;
	     list != head;
	     list = list->next)
	{
		group = list_entry (list, struct queue_group, group_list);

		log_printf (LOG_LEVEL_NOTICE, "[DEBUG]: print_group_list ( %s )\n",
			    getSaNameT (&group->name));
	}
}
#endif

void queue_entry_release (struct message_entry *entry)
{
	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "[DEBUG]: queue_entry_release { %s }\n",
		(char *)(entry->message.data));

	if (entry->message.data) {
		free (entry->message.data);
	}
	free (entry);
}

void queue_release (struct message_queue *queue)
{
	struct list_head *list;
	struct message_entry *entry;

	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "[DEBUG]: queue_release { %s }\n",
		getSaNameT (&queue->name));

	for (list = queue->message_head.next;
	     list != &queue->message_head;) {

		entry = list_entry (list, struct message_entry, list);

		list = list->next;
		queue_entry_release (entry);
	}
	list_del (&queue->queue_list);
	list_del (&queue->group_list);
	free (queue);
}

void group_release (struct queue_group *group)
{
	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "[DEBUG]: group_release { %s }\n",
		getSaNameT (&group->name));

	list_del (&group->group_list);
	free (group);
}

static struct message_queue *sync_queue_find (SaNameT *name)
{
	struct list_head *list;
	struct message_queue *queue;

	for (list = sync_queue_list_head.next;
	     list != &sync_queue_list_head;
	     list = list->next)
	{
	        queue = list_entry (list, struct message_queue, queue_list);

		if (name_match (name, &queue->name)) {
			return (queue);
		}
	}
	return (0);
}

static struct queue_group *sync_group_find (SaNameT *name)
{
	struct list_head *list;
	struct queue_group *group;

	for (list = sync_group_list_head.next;
	     list != &sync_group_list_head;
	     list = list->next)
	{
	        group = list_entry (list, struct queue_group, group_list);

		if (name_match (name, &group->name)) {
			return (group);
		}
	}
	return (0);
}

static inline void sync_group_free (struct list_head *group_head)
{
	struct queue_group *group;
	struct list_head *list;

	list = group_head->next;

	while (list != group_head) {
		group = list_entry (list, struct queue_group, group_list);
		list = list->next;
		group_release (group);
	}

	list_init (group_head);
}

static inline void sync_queue_free (struct list_head *queue_head)
{
	struct message_queue *queue;
	struct list_head *list;

	list = queue_head->next;

	while (list != queue_head) {
		queue = list_entry (list, struct message_queue, queue_list);
		list = list->next;
		queue_release (queue);
	}

	list_init (queue_head);
}

static inline void sync_queue_enter (void)
{
	struct message_queue *queue;

	ENTER();

	my_sync_state = SYNC_STATE_QUEUE;
	my_iteration_state = ITERATION_STATE_QUEUE;
	my_iteration_state_queue = queue_list_head.next;

	queue = list_entry (queue_list_head.next, struct message_queue, queue_list);

	my_iteration_state_entry = queue->message_head.next;

	LEAVE();
}

static inline void sync_group_enter (void)
{
	my_sync_state = SYNC_STATE_GROUP;
}

static int sync_queue_transmit (
	struct message_queue *queue)
{
	struct req_exec_msg_sync_queue req_exec_msg_sync_queue;
	struct iovec iovec;

	memset (&req_exec_msg_sync_queue, 0,
		sizeof (struct req_exec_msg_sync_queue));

	req_exec_msg_sync_queue.header.size =
		sizeof (struct req_exec_msg_sync_queue);
	req_exec_msg_sync_queue.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_SYNC_QUEUE);

	memcpy (&req_exec_msg_sync_queue.ring_id,
		&my_saved_ring_id, sizeof (struct memb_ring_id));
	memcpy (&req_exec_msg_sync_queue.queue_name,
		&queue->name, sizeof (SaNameT));

	req_exec_msg_sync_queue.change = queue->change;

	iovec.iov_base = (char *)&req_exec_msg_sync_queue;
	iovec.iov_len = sizeof (req_exec_msg_sync_queue);

	return (api->totem_mcast (&iovec, 1, TOTEM_AGREED));
}

static int sync_queue_entry_transmit (
	struct message_queue *queue,
	struct message_entry *entry)
{
	struct req_exec_msg_sync_queue_entry req_exec_msg_sync_queue_entry;
	struct iovec iovec[2];

	memset (&req_exec_msg_sync_queue_entry, 0,
		sizeof (struct req_exec_msg_sync_queue_entry));

	req_exec_msg_sync_queue_entry.header.size =
		sizeof (struct req_exec_msg_sync_queue_entry);
	req_exec_msg_sync_queue_entry.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_SYNC_QUEUE_ENTRY);

	memcpy (&req_exec_msg_sync_queue_entry.ring_id,
		&my_saved_ring_id, sizeof (struct memb_ring_id));
	memcpy (&req_exec_msg_sync_queue_entry.queue_name,
		&queue->name, sizeof (SaNameT));
	memcpy (&req_exec_msg_sync_queue_entry.message,
		&entry->message, sizeof (SaMsgMessageT));

	req_exec_msg_sync_queue_entry.time = entry->time;

	iovec[0].iov_base = (char *)&req_exec_msg_sync_queue_entry;
	iovec[0].iov_len = sizeof (req_exec_msg_sync_queue_entry);
	iovec[1].iov_base = entry->message.data;
	iovec[1].iov_len = entry->message.size;

	return (api->totem_mcast (iovec, 2, TOTEM_AGREED));
}

static int sync_group_transmit (
	struct queue_group *group)
{
	struct req_exec_msg_sync_group req_exec_msg_sync_group;
	struct iovec iovec;

	memset (&req_exec_msg_sync_group, 0,
		sizeof (struct req_exec_msg_sync_group));

	req_exec_msg_sync_group.header.size =
		sizeof (struct req_exec_msg_sync_group);
	req_exec_msg_sync_group.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_SYNC_GROUP);

	memcpy (&req_exec_msg_sync_group.ring_id,
		&my_saved_ring_id, sizeof (struct memb_ring_id));
	memcpy (&req_exec_msg_sync_group.group_name,
		&group->name, sizeof (SaNameT));

	if (group->rr_queue != NULL) {
		memcpy (&req_exec_msg_sync_group.rr_queue_name,
			&group->rr_queue->name, sizeof (SaNameT));
	}

	req_exec_msg_sync_group.policy = group->policy;
	req_exec_msg_sync_group.track_flags = group->track_flags;

	iovec.iov_base = (char *)&req_exec_msg_sync_group;
	iovec.iov_len = sizeof (req_exec_msg_sync_group);

	return (api->totem_mcast (&iovec, 1, TOTEM_AGREED));
}

static int sync_group_entry_transmit (
	struct queue_group *group,
	struct message_queue *queue)
{
	struct req_exec_msg_sync_group_entry req_exec_msg_sync_group_entry;
	struct iovec iovec;

	memset (&req_exec_msg_sync_group_entry, 0,
		sizeof (struct  req_exec_msg_sync_group_entry));

	req_exec_msg_sync_group_entry.header.size =
		sizeof (struct req_exec_msg_sync_group_entry);
	req_exec_msg_sync_group_entry.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_SYNC_GROUP_ENTRY);

	memcpy (&req_exec_msg_sync_group_entry.ring_id,
		&my_saved_ring_id, sizeof (struct memb_ring_id));
	memcpy (&req_exec_msg_sync_group_entry.group_name,
		&group->name, sizeof (SaNameT));
	memcpy (&req_exec_msg_sync_group_entry.queue_name,
		&queue->name, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_sync_group_entry;
	iovec.iov_len = sizeof (req_exec_msg_sync_group_entry);

	return (api->totem_mcast (&iovec, 1, TOTEM_AGREED));
}

static int sync_queue_iterate (void)
{
	struct message_queue *queue;
	struct message_entry *entry;
	struct list_head *queue_list;
	struct list_head *entry_list;
	unsigned int res = 0;

	for (queue_list = queue_list_head.next;
	     queue_list != &queue_list_head;
	     queue_list = queue_list->next)
	{
		queue = list_entry (queue_list, struct message_queue, queue_list);

		res = sync_queue_transmit (queue);
		if (res != 0) {
			break;
		}

		for (entry_list = queue->message_head.next;
		     entry_list != &queue->message_head;
		     entry_list = entry_list->next)
		{
			entry = list_entry (entry_list, struct message_entry, list);
			res = sync_queue_entry_transmit (queue, entry);
		}
	}

	return (res);
}

static int sync_group_iterate (void)
{
	struct queue_group *group;
	struct message_queue *queue;
	struct list_head *group_list;
	struct list_head *queue_list;
	unsigned int res = 0;

	for (group_list = group_list_head.next;
	     group_list != &group_list_head;
	     group_list = group_list->next)
	{
		group = list_entry (group_list, struct queue_group, group_list);

		res = sync_group_transmit (group);
		if (res != 0) {
			break;
		}

		for (queue_list = group->queue_head.next;
		     queue_list != &group->queue_head;
		     queue_list = queue_list->next)
		{
			queue = list_entry (queue_list, struct message_queue, group_list);
			res = sync_group_entry_transmit (group, queue);
		}
	}

	return (res);
}

static void msg_sync_init (void) 
{
	ENTER();

	sync_queue_enter ();

	LEAVE();
}

static int msg_sync_process (void)
{
	unsigned int done_queueing = 1;
	unsigned int continue_processing = 0;
	unsigned int res;

	switch (my_sync_state)
	{
	case SYNC_STATE_QUEUE:
		if (my_lowest_nodeid == api->totem_nodeid_get ()) {
			TRACE1 ("transmit queues because lowest member in old configuration.\n");

			res = sync_queue_iterate ();
			if (res == 0) {
				done_queueing = 1;
			}
		}
		if (done_queueing) {
			sync_group_enter ();
		}

		continue_processing = 1;
		break;

	case SYNC_STATE_GROUP:
		if (my_lowest_nodeid == api->totem_nodeid_get ()) {
			TRACE1 ("transmit groups because lowest member in old configuration.\n");

			res = sync_group_iterate ();
		}
		if (done_queueing) {
			continue_processing = 0;
		}
		break;
	}

	return (continue_processing);
}

static void msg_sync_activate (void) 
{
	sync_group_free (&group_list_head);
	sync_queue_free (&queue_list_head);

	if (!list_empty (&sync_group_list_head)) {
		list_splice (&sync_group_list_head, &group_list_head);
	}

	if (!list_empty (&sync_queue_list_head)) {
		list_splice (&sync_queue_list_head, &queue_list_head);
	}

	list_init (&sync_group_list_head);
	list_init (&sync_queue_list_head);

	/* DEBUG
	log_printf (LOG_LEVEL_NOTICE, "[DEBUG]: group list after activate...\n");
	print_group_list (&group_list_head);
	*/

	/* DEBUG
	log_printf (LOG_LEVEL_NOTICE, "[DEBUG]: queue list after activate...\n");
	print_queue_list (&queue_list_head);
	*/

 	return;
}

static void msg_sync_abort (void) 
{
	sync_group_free (&sync_group_list_head);
	sync_queue_free (&sync_queue_list_head);
}

static void msg_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id) 
{
	unsigned int i, j;

	/*
	 * Determine lowest nodeid in old regular configuration for the
	 * purpose of executing the synchronization algorithm
	 */
	if (configuration_type == TOTEM_CONFIGURATION_TRANSITIONAL) {
		for (i = 0; i < left_list_entries; i++) {
			for (j = 0; j < my_member_list_entries; j++) {
				if (left_list[i] == my_member_list[j]) {
					my_member_list[j] = 0;
				}
			}
		}
	}

	my_lowest_nodeid = 0xffffffff;

	/*
	 * Handle regular configuration
	 */
	if (configuration_type == TOTEM_CONFIGURATION_REGULAR) {
		memcpy (my_member_list, member_list,
			sizeof (unsigned int) * member_list_entries);
		my_member_list_entries = member_list_entries;
		memcpy (&my_saved_ring_id, ring_id,
			sizeof (struct memb_ring_id));
		for (i = 0; i < my_member_list_entries; i++) {
			if ((my_member_list[i] != 0) &&
			    (my_member_list[i] < my_lowest_nodeid)) {
				my_lowest_nodeid = my_member_list[i];
			}
		}
	}
}

static struct message_queue *queue_find (SaNameT *name)
{
	struct list_head *list;
	struct message_queue *queue;

	for (list = queue_list_head.next;
	     list != &queue_list_head;
	     list = list->next)
	{
	        queue = list_entry (list, struct message_queue, queue_list);

		if (name_match (name, &queue->name)) {
			return (queue);
		}
	}
	return (0);
}

static struct queue_group *group_find (SaNameT *name)
{
	struct list_head *list;
	struct queue_group *group;

	for (list = group_list_head.next;
	     list != &group_list_head;
	     list = list->next)
	{
	        group = list_entry (list, struct queue_group, group_list);

		if (name_match (name, &group->name)) {
			return (group);
		}
	}
	return (0);
}

static unsigned int queue_group_member_count (struct queue_group *group)
{
	struct list_head *list;

	unsigned int count = 0;

	for (list = group->queue_head.next;
	     list != &group->queue_head;
	     list = list->next)
	{
		count++;
	}
	return (count);
}

static unsigned int queue_group_change_count (struct queue_group *group)
{
	struct list_head *list;
	struct message_queue *queue;

	unsigned int count = 0;

	for (list = group->queue_head.next;
	     list != &group->queue_head;
	     list = list->next)
	{
		queue = list_entry (list, struct message_queue, group_list);

		if (queue->change != SA_MSG_QUEUE_GROUP_NO_CHANGE) {
			count++;
		}
	}
	return (count);
}

static unsigned int queue_group_track (
	struct queue_group *group,
	unsigned int flags,
	void *buffer)
{
	struct list_head *list;
	struct message_queue *queue;

	unsigned int i = 0;

	SaMsgQueueGroupNotificationT *notification =
		(SaMsgQueueGroupNotificationT *) buffer;


	switch (flags) {

	case SA_TRACK_CURRENT:
	case SA_TRACK_CHANGES:

		for (list = group->queue_head.next;
		     list != &group->queue_head;
		     list = list->next)
		{
			queue = list_entry (list, struct message_queue, group_list);
			memcpy (&notification[i].member.queueName,
				&queue->name, sizeof (SaNameT));
			notification[i].change = queue->change;
			i++;
		}
		break;

	case SA_TRACK_CHANGES_ONLY:

		for (list = group->queue_head.next;
		     list != &group->queue_head;
		     list = list->next)
		{
			queue = list_entry (list, struct message_queue, group_list);
			if (queue->change != SA_MSG_QUEUE_GROUP_NO_CHANGE) {
				memcpy (&notification[i].member.queueName,
					&queue->name, sizeof (SaNameT));
				notification[i].change = queue->change;
				i++;
			}
		}
		break;

	default:
		break;
	}

	return (i);
}

static struct message_queue *next_rr_queue (struct queue_group *group)
{
	struct message_queue *queue;

	if (group->rr_queue->group_list.next == &group->queue_head) {
		queue = list_entry (group->queue_head.next,
				    struct message_queue,
				    group_list);
	} else {
		queue =	list_entry (group->rr_queue->group_list.next,
				    struct message_queue,
				    group_list);
	}

	return (queue);
}

static int msg_exec_init_fn (struct corosync_api_v1 *corosync_api)
{
	api = corosync_api;

	/*
	 *  Initialize the saved ring ID.
	 */

	/* saved_ring_id.seq = 0; */
	/* saved_ring_id.rep.s_addr = this_ip->sin_addr.s_addr; */

	return (0);
}

static int msg_lib_exit_fn (void *conn)
{
	/*
	 * struct msg_pd *msg_pd = (struct msg_pd *)api->ipc_private_data_get (conn);
	 */

#ifdef COMPILE_OUT
	struct queue_cleanup *queue_cleanup;
	struct list_head *list;
	
	/*
	 * close all queues opened on this fd
	 */
	list = conn_info->conn_info_partner->ais_ci.u.libmsg_ci.queue_cleanup_list.next;	
	while (!list_empty(&conn_info->conn_info_partner->ais_ci.u.libmsg_ci.queue_cleanup_list)) {
		
		queue_cleanup = list_entry (list, struct queue_cleanup, list);

		if (queue_cleanup->queue->name.length > 0) {
			msg_queue_cleanup_lock_remove (queue_cleanup);
			msg_queue_close (queue_cleanup->queue);
		}

		list_del (&queue_cleanup->list);	
		free (queue_cleanup);
                
		list = conn_info->conn_info_partner->ais_ci.u.libmsg_ci.queue_cleanup_list.next;
	}
#endif	/* COMPILE_OUT */

	return (0);
}

static int msg_lib_init_fn (void *conn)
{
	struct msg_pd *msg_pd = (struct msg_pd *)api->ipc_private_data_get (conn);

	list_init (&msg_pd->queue_list);
	list_init (&msg_pd->queue_cleanup_list);

	return (0);
}

static void message_handler_req_exec_msg_queueopen (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_msg_queueopen *req_exec_msg_queueopen =
		(struct req_exec_msg_queueopen *)message;
	struct res_lib_msg_queueopen res_lib_msg_queueopen;
	struct res_lib_msg_queueopenasync res_lib_msg_queueopenasync;
	struct message_queue *queue;
	/* struct queue_cleanup *queue_cleanup; */
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saMsgQueueOpen %s\n",
		getSaNameT (&req_exec_msg_queueopen->queue_name));

	queue = queue_find (&req_exec_msg_queueopen->queue_name);

	/*
	 * If queue doesn't exist, create one
	 */
	if (queue == 0) {
		if ((req_exec_msg_queueopen->openFlags & SA_MSG_QUEUE_CREATE) == 0) {
			error = SA_AIS_ERR_NOT_EXIST;
			goto error_exit;
		}
		queue = malloc (sizeof (struct message_queue));
		if (queue == 0) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}
		memset (queue, 0, sizeof (struct message_queue));
		memcpy (&queue->name,
			&req_exec_msg_queueopen->queue_name,
			sizeof (SaNameT));

		list_init (&queue->group_list);
		list_init (&queue->queue_list);
		list_init (&queue->message_head);
		list_add (&queue->queue_list, &queue_list_head);

		queue->refcount = 0;
	}
	queue->refcount += 1;

#ifdef COMPILE_OUT
	/*
	 * Setup connection information and mark queue as referenced
	 */
	queue_cleanup = malloc (sizeof (struct queue_cleanup));
	if (queue_cleanup == 0) {
		free (queue);
		error = SA_AIS_ERR_NO_MEMORY;
	} else {
		list_init (&queue_cleanup->list);
		list_init (&queue_cleanup->queue_lock_list_head);
		queue_cleanup->queue = queue;
		queue_cleanup->queue_handle = req_exec_msg_queueopen->queue_handle;
		list_add (
			&queue_cleanup->list,
			&req_exec_msg_queueopen->source.conn_info->ais_ci.u.libmsg_ci.queue_cleanup_list);
	}
	queue->refcount += 1;
#endif	/* COMPILE_OUT */
	
	/*
	 * Send error result to MSG library
	 */
error_exit:
	/*
	 * If this node was the source of the message, respond to this node
	 */
	if (api->ipc_source_is_local (&req_exec_msg_queueopen->source)) {
		/*
		 * If its an async call respond with the invocation and handle
		 */
		if (req_exec_msg_queueopen->async_call)
		{
			res_lib_msg_queueopenasync.header.size =
				sizeof (struct res_lib_msg_queueopenasync);
			res_lib_msg_queueopenasync.header.id =
				MESSAGE_RES_MSG_QUEUEOPENASYNC;
			res_lib_msg_queueopenasync.header.error = error;

			res_lib_msg_queueopenasync.invocation =
				req_exec_msg_queueopen->invocation;
			res_lib_msg_queueopenasync.queueHandle =
				req_exec_msg_queueopen->queue_handle;

			memcpy (&res_lib_msg_queueopenasync.source,
				&req_exec_msg_queueopen->source,
				sizeof (mar_message_source_t));

			api->ipc_conn_send_response (
				req_exec_msg_queueopen->source.conn,
				&res_lib_msg_queueopenasync,
				sizeof (struct res_lib_msg_queueopenasync));

			api->ipc_conn_send_response (
				api->ipc_conn_partner_get (req_exec_msg_queueopen->source.conn),
				&res_lib_msg_queueopenasync,
				sizeof (struct res_lib_msg_queueopenasync));
		} else {
			/*
			 * otherwise respond with the normal queueopen response
			 */
			res_lib_msg_queueopen.header.size =
				sizeof (struct res_lib_msg_queueopen);
			res_lib_msg_queueopen.header.id =
				MESSAGE_RES_MSG_QUEUEOPEN;
			res_lib_msg_queueopen.header.error = error;

			res_lib_msg_queueopen.queueHandle =
				req_exec_msg_queueopen->queue_handle;

			memcpy (&res_lib_msg_queueopen.source,
				&req_exec_msg_queueopen->source,
				sizeof (mar_message_source_t));

			api->ipc_conn_send_response (
				req_exec_msg_queueopen->source.conn,
				&res_lib_msg_queueopen,
				sizeof (struct res_lib_msg_queueopen));
		}
	}
}

static void message_handler_req_exec_msg_queueclose (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_msg_queueclose *req_exec_msg_queueclose =
		(struct req_exec_msg_queueclose *)message;
	struct res_lib_msg_queueclose res_lib_msg_queueclose;
	struct message_queue *queue = 0;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saMsgQueueClose %s\n",
		getSaNameT (&req_exec_msg_queueclose->queue_name));

	queue = queue_find (&req_exec_msg_queueclose->queue_name);
	if (queue == 0) {
		goto error_exit;
	}
		
	queue->refcount -= 1;

	if (queue->refcount == 0) {
		/* free queue */
	}

error_exit:
	if (api->ipc_source_is_local(&req_exec_msg_queueclose->source))
	{

		/* TODO */

		/*
		 * msg_queue_cleanup_remove (
		 *	req_exec_msg_queueclose->source.conn_info,
		 *	req_exec_msg_queueclose->queue_handle);
		 */

		res_lib_msg_queueclose.header.size =
			sizeof (struct res_lib_msg_queueclose);
		res_lib_msg_queueclose.header.id =
			MESSAGE_RES_MSG_QUEUECLOSE;
		res_lib_msg_queueclose.header.error = error;

		api->ipc_conn_send_response (
			req_exec_msg_queueclose->source.conn,
			&res_lib_msg_queueclose,
			sizeof (struct res_lib_msg_queueclose));
	}
}

static void message_handler_req_exec_msg_queuestatusget (
	void *message,
	unsigned int nodeid)
{
#if 0
	struct req_exec_msg_queuestatusget *req_exec_msg_queuestatusget =
		(struct req_exec_msg_queuestatusget *)message;
	struct res_lib_msg_queuestatusget res_lib_msg_queuestatusget;
#endif
}

static void message_handler_req_exec_msg_queueunlink (
	void *message,
	unsigned int nodeid)
{
#if 0
	struct req_exec_msg_queueunlink *req_exec_msg_queueunlink =
		(struct req_exec_msg_queueunlink *)message;
	struct res_lib_msg_queueunlink res_lib_msg_queueunlink;
#endif
}

static void message_handler_req_exec_msg_queuegroupcreate (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_msg_queuegroupcreate *req_exec_msg_queuegroupcreate =
		(struct req_exec_msg_queuegroupcreate *)message;
	struct res_lib_msg_queuegroupcreate res_lib_msg_queuegroupcreate;
	struct queue_group *queue_group;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saMsgQueueGroupCreate %s\n",
		getSaNameT (&req_exec_msg_queuegroupcreate->queue_group_name));

	queue_group = group_find (&req_exec_msg_queuegroupcreate->queue_group_name);

	if (queue_group == 0) {
		queue_group = malloc (sizeof (struct queue_group));
		if (queue_group == 0) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}
		memset (queue_group, 0, sizeof (struct queue_group));
		memcpy (&queue_group->name,
			&req_exec_msg_queuegroupcreate->queue_group_name,
			sizeof (SaNameT));

		queue_group->policy = req_exec_msg_queuegroupcreate->policy;

		list_init (&queue_group->group_list);
		list_init (&queue_group->queue_head);
		list_add (&queue_group->group_list, &group_list_head);
	} else {
		error = SA_AIS_ERR_EXIST;
	}

error_exit:
	if (api->ipc_source_is_local(&req_exec_msg_queuegroupcreate->source)) {
		res_lib_msg_queuegroupcreate.header.size =
			sizeof (struct res_lib_msg_queuegroupcreate);
		res_lib_msg_queuegroupcreate.header.id =
			MESSAGE_RES_MSG_QUEUEGROUPCREATE;
		res_lib_msg_queuegroupcreate.header.error = error;

		api->ipc_conn_send_response (
			req_exec_msg_queuegroupcreate->source.conn,
			&res_lib_msg_queuegroupcreate,
			sizeof (struct res_lib_msg_queuegroupcreate));
	}
}

static void message_handler_req_exec_msg_queuegroupinsert (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_msg_queuegroupinsert *req_exec_msg_queuegroupinsert =
		(struct req_exec_msg_queuegroupinsert *)message;
	struct res_lib_msg_queuegroupinsert res_lib_msg_queuegroupinsert;
	struct res_lib_msg_queuegrouptrack res_lib_msg_queuegrouptrack;
	struct message_queue *queue;
	struct queue_group *queue_group;
	SaMsgQueueGroupNotificationT *notification = NULL;
	SaAisErrorT error = SA_AIS_OK;
	SaAisErrorT error_cb = SA_AIS_OK;

	unsigned int change_count = 0;
	unsigned int member_count = 0;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saMsgQueueGroupInsert %s\n",
		getSaNameT (&req_exec_msg_queuegroupinsert->queue_group_name));

	queue_group = group_find (&req_exec_msg_queuegroupinsert->queue_group_name);

	if (queue_group == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	queue = queue_find (&req_exec_msg_queuegroupinsert->queue_name);
	if (queue == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * If the policy is SA_MSG_QUEUE_GROUP_ROUND_ROBIN and the
	 * rr_queue is NULL, then this is the first queue in the group.
	 */
	if ((queue_group->policy == SA_MSG_QUEUE_GROUP_ROUND_ROBIN) &&
	    (queue_group->rr_queue == NULL))
	{
		queue_group->rr_queue = queue;
	}

	list_init (&queue->group_list);
	list_add (&queue->group_list, &queue_group->queue_head);

	queue->change = SA_MSG_QUEUE_GROUP_ADDED;

	if (queue_group->track_flags & SA_TRACK_CHANGES) {
		member_count = queue_group_member_count (queue_group);
		change_count = queue_group_change_count (queue_group);

		notification = malloc (sizeof (SaMsgQueueGroupNotificationT) * member_count);

		if (notification == NULL) {
			error_cb = SA_AIS_ERR_NO_MEMORY;
			goto error_track;
		}

		memset (notification, 0, sizeof (SaMsgQueueGroupNotificationT) * member_count);

		res_lib_msg_queuegrouptrack.notificationBuffer.numberOfItems =
			queue_group_track (queue_group,
					   SA_TRACK_CHANGES,
					   (void *)(notification));
	}

	if (queue_group->track_flags & SA_TRACK_CHANGES_ONLY) {
		member_count = queue_group_member_count (queue_group);
		change_count = queue_group_change_count (queue_group);

		notification = malloc (sizeof (SaMsgQueueGroupNotificationT) * change_count);

		if (notification == NULL) {
			error_cb = SA_AIS_ERR_NO_MEMORY;
			goto error_track;
		}

		memset (notification, 0, sizeof (SaMsgQueueGroupNotificationT) * change_count);

		res_lib_msg_queuegrouptrack.notificationBuffer.numberOfItems =
			queue_group_track (queue_group,
					   SA_TRACK_CHANGES_ONLY,
					   (void *)(notification));
	}

error_track:

	queue->change = SA_MSG_QUEUE_GROUP_NO_CHANGE;

error_exit:

	if (api->ipc_source_is_local(&req_exec_msg_queuegroupinsert->source)) {
		res_lib_msg_queuegroupinsert.header.size =
			sizeof (struct res_lib_msg_queuegroupinsert);
		res_lib_msg_queuegroupinsert.header.id =
			MESSAGE_RES_MSG_QUEUEGROUPINSERT;
		res_lib_msg_queuegroupinsert.header.error = error;

		api->ipc_conn_send_response (
			req_exec_msg_queuegroupinsert->source.conn,
			&res_lib_msg_queuegroupinsert,
			sizeof (struct res_lib_msg_queuegroupinsert));

		/*
		 * Track changes (callback) if tracking is enabled
		 */

		if ((queue_group->track_flags & SA_TRACK_CHANGES) ||
		    (queue_group->track_flags & SA_TRACK_CHANGES_ONLY))
		{
			res_lib_msg_queuegrouptrack.header.size =
				(sizeof (struct res_lib_msg_queuegrouptrack) +
				 (sizeof (SaMsgQueueGroupNotificationT) *
				  res_lib_msg_queuegrouptrack.notificationBuffer.numberOfItems));
			res_lib_msg_queuegrouptrack.header.id =
				MESSAGE_RES_MSG_QUEUEGROUPTRACK;
			res_lib_msg_queuegrouptrack.header.error = error_cb;
			res_lib_msg_queuegrouptrack.numberOfMembers = member_count;

			memcpy (&res_lib_msg_queuegrouptrack.queueGroupName,
				&req_exec_msg_queuegroupinsert->queue_group_name,
				sizeof (SaNameT));

			api->ipc_conn_send_response (
				api->ipc_conn_partner_get (req_exec_msg_queuegroupinsert->source.conn),
				&res_lib_msg_queuegrouptrack,
				sizeof (struct res_lib_msg_queuegrouptrack));

			api->ipc_conn_send_response (
				api->ipc_conn_partner_get (req_exec_msg_queuegroupinsert->source.conn),
				notification,
				(sizeof (SaMsgQueueGroupNotificationT) *
				 res_lib_msg_queuegrouptrack.notificationBuffer.numberOfItems));
		}
	}
}

static void message_handler_req_exec_msg_queuegroupremove (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_msg_queuegroupremove *req_exec_msg_queuegroupremove =
		(struct req_exec_msg_queuegroupremove *)message;
	struct res_lib_msg_queuegroupremove res_lib_msg_queuegroupremove;
	struct res_lib_msg_queuegrouptrack res_lib_msg_queuegrouptrack;
	struct queue_group *queue_group;
	struct message_queue *queue;
	SaMsgQueueGroupNotificationT *notification = NULL;
	SaAisErrorT error = SA_AIS_OK;
	SaAisErrorT error_cb = SA_AIS_OK;

	unsigned int change_count = 0;
	unsigned int member_count = 0;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saMsgQueueGroupRemove %s\n",
		getSaNameT (&req_exec_msg_queuegroupremove->queue_group_name));

	queue_group = group_find (&req_exec_msg_queuegroupremove->queue_group_name);

	if (queue_group == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	queue = queue_find (&req_exec_msg_queuegroupremove->queue_name);
	if (queue == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	/*
	 * FIXME: Need to verify that queue being removed
	 * is actually a member of the queue group.
	 */

	/*
	queue = group_queue_find (queue_group,
				  &req_exec_msg_queuegroupremove->queue_name);
	if (queue == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}
	*/

	queue->change = SA_MSG_QUEUE_GROUP_REMOVED;

	if (queue_group->track_flags & SA_TRACK_CHANGES) {
		member_count = queue_group_member_count (queue_group);
		change_count = queue_group_change_count (queue_group);

		notification = malloc (sizeof (SaMsgQueueGroupNotificationT) * member_count);

		if (notification == NULL) {
			error_cb = SA_AIS_ERR_NO_MEMORY;
			goto error_track;
		}

		memset (notification, 0, (sizeof (SaMsgQueueGroupNotificationT) * member_count));

		res_lib_msg_queuegrouptrack.notificationBuffer.numberOfItems =
			queue_group_track (queue_group,
					   SA_TRACK_CHANGES,
					   (void *)(notification));
	}

	if (queue_group->track_flags & SA_TRACK_CHANGES_ONLY) {
		member_count = queue_group_member_count (queue_group);
		change_count = queue_group_change_count (queue_group);

		notification = malloc (sizeof (SaMsgQueueGroupNotificationT) * change_count);

		if (notification == NULL) {
			error_cb = SA_AIS_ERR_NO_MEMORY;
			goto error_track;
		}

		memset (notification, 0, (sizeof (SaMsgQueueGroupNotificationT) * change_count));

		res_lib_msg_queuegrouptrack.notificationBuffer.numberOfItems =
			queue_group_track (queue_group,
					   SA_TRACK_CHANGES_ONLY,
					   (void *)(notification));
	}

error_track:

	queue->change = SA_MSG_QUEUE_GROUP_NO_CHANGE;

	/*
	 * If the queue we are removing is also the next rr_queue,
	 * we should set rr_queue to the next queue on the list.
	 */
	if (queue_group->rr_queue == queue) {
		queue_group->rr_queue = next_rr_queue (queue_group);
	}

	list_del (&queue->group_list);

error_exit:

	if (api->ipc_source_is_local(&req_exec_msg_queuegroupremove->source)) {
		res_lib_msg_queuegroupremove.header.size =
			sizeof (struct res_lib_msg_queuegroupremove);
		res_lib_msg_queuegroupremove.header.id =
			MESSAGE_RES_MSG_QUEUEGROUPREMOVE;
		res_lib_msg_queuegroupremove.header.error = error;

		api->ipc_conn_send_response (
			req_exec_msg_queuegroupremove->source.conn,
			&res_lib_msg_queuegroupremove,
			sizeof (struct res_lib_msg_queuegroupremove));

		/*
		 * Track changes (callback) if tracking is enabled
		 */

		if ((queue_group->track_flags & SA_TRACK_CHANGES) ||
		    (queue_group->track_flags & SA_TRACK_CHANGES_ONLY))
		{
			res_lib_msg_queuegrouptrack.header.size =
				(sizeof (struct res_lib_msg_queuegrouptrack) +
				 (sizeof (SaMsgQueueGroupNotificationT) *
				  res_lib_msg_queuegrouptrack.notificationBuffer.numberOfItems));
			res_lib_msg_queuegrouptrack.header.id =
				MESSAGE_RES_MSG_QUEUEGROUPTRACK;
			res_lib_msg_queuegrouptrack.header.error = error_cb;
			res_lib_msg_queuegrouptrack.numberOfMembers = member_count;

			memcpy (&res_lib_msg_queuegrouptrack.queueGroupName,
				&req_exec_msg_queuegroupremove->queue_group_name,
				sizeof (SaNameT));

			api->ipc_conn_send_response (
				api->ipc_conn_partner_get (req_exec_msg_queuegroupremove->source.conn),
				&res_lib_msg_queuegrouptrack,
				sizeof (struct res_lib_msg_queuegrouptrack));

			api->ipc_conn_send_response (
				api->ipc_conn_partner_get (req_exec_msg_queuegroupremove->source.conn),
				notification,
				(sizeof (SaMsgQueueGroupNotificationT) *
				 res_lib_msg_queuegrouptrack.notificationBuffer.numberOfItems));
		}
	}
}

static void message_handler_req_exec_msg_queuegroupdelete (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_msg_queuegroupdelete *req_exec_msg_queuegroupdelete =
		(struct req_exec_msg_queuegroupdelete *)message;
	struct res_lib_msg_queuegroupdelete res_lib_msg_queuegroupdelete;
	struct queue_group *queue_group;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saMsgQueueGroupDelete %s\n",
		getSaNameT (&req_exec_msg_queuegroupdelete->queue_group_name));

	queue_group = group_find (&req_exec_msg_queuegroupdelete->queue_group_name);

	if (queue_group) {
		list_del (&queue_group->group_list);
		free (queue_group);
	} else {
		error = SA_AIS_ERR_NOT_EXIST;
	}

	if (api->ipc_source_is_local(&req_exec_msg_queuegroupdelete->source)) {
		res_lib_msg_queuegroupdelete.header.size =
			sizeof (struct res_lib_msg_queuegroupdelete);
		res_lib_msg_queuegroupdelete.header.id =
			MESSAGE_RES_MSG_QUEUEGROUPDELETE;
		res_lib_msg_queuegroupdelete.header.error = error;

		api->ipc_conn_send_response (
			req_exec_msg_queuegroupdelete->source.conn,
			&res_lib_msg_queuegroupdelete,
			sizeof (struct res_lib_msg_queuegroupdelete));
	}
}

static void message_handler_req_exec_msg_queuegrouptrack (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_msg_queuegrouptrack *req_exec_msg_queuegrouptrack =
		(struct req_exec_msg_queuegrouptrack *)message;
	struct res_lib_msg_queuegrouptrack res_lib_msg_queuegrouptrack;
	struct queue_group *queue_group;
	SaAisErrorT error = SA_AIS_OK;

	unsigned int change_count = 0;
	unsigned int member_count = 0;

	SaMsgQueueGroupNotificationT *notification = NULL;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saMsgQueueGroupTrack %s\n",
		getSaNameT (&req_exec_msg_queuegrouptrack->queue_group_name));

	queue_group = group_find (&req_exec_msg_queuegrouptrack->queue_group_name);

	if (queue_group == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	member_count = queue_group_member_count (queue_group);
	change_count = queue_group_change_count (queue_group);

	if (req_exec_msg_queuegrouptrack->track_flags & SA_TRACK_CURRENT) {
		notification = malloc (sizeof (SaMsgQueueGroupNotificationT) * member_count);

		if (notification == NULL) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}

		memset (notification, 0, sizeof (SaMsgQueueGroupNotificationT) * member_count);

		res_lib_msg_queuegrouptrack.notificationBuffer.numberOfItems =
			queue_group_track (queue_group, SA_TRACK_CURRENT, (void *)(notification));
	}

	if (req_exec_msg_queuegrouptrack->track_flags & SA_TRACK_CHANGES) {
		queue_group->track_flags = req_exec_msg_queuegrouptrack->track_flags;
	}

	if (req_exec_msg_queuegrouptrack->track_flags & SA_TRACK_CHANGES_ONLY) {
		queue_group->track_flags = req_exec_msg_queuegrouptrack->track_flags;
	}

error_exit:
	if (api->ipc_source_is_local(&req_exec_msg_queuegrouptrack->source)) {
		res_lib_msg_queuegrouptrack.header.size =
			sizeof (struct res_lib_msg_queuegrouptrack);
		res_lib_msg_queuegrouptrack.header.id =
			MESSAGE_RES_MSG_QUEUEGROUPTRACK;
		res_lib_msg_queuegrouptrack.header.error = error;
		res_lib_msg_queuegrouptrack.numberOfMembers = member_count;

		memcpy (&res_lib_msg_queuegrouptrack.queueGroupName,
			&req_exec_msg_queuegrouptrack->queue_group_name,
			sizeof (SaNameT));

		if (req_exec_msg_queuegrouptrack->track_flags & SA_TRACK_CURRENT) {
			if (req_exec_msg_queuegrouptrack->buffer_flag) {
				res_lib_msg_queuegrouptrack.header.size +=
					(sizeof (SaMsgQueueGroupNotificationT) *
					 res_lib_msg_queuegrouptrack.notificationBuffer.numberOfItems);

				api->ipc_conn_send_response (
					req_exec_msg_queuegrouptrack->source.conn,
					&res_lib_msg_queuegrouptrack,
					sizeof (struct res_lib_msg_queuegrouptrack));

				api->ipc_conn_send_response (
					req_exec_msg_queuegrouptrack->source.conn,
					notification,
					(sizeof (SaMsgQueueGroupNotificationT) *
					 res_lib_msg_queuegrouptrack.notificationBuffer.numberOfItems));
			} else {
				api->ipc_conn_send_response (
					req_exec_msg_queuegrouptrack->source.conn,
					&res_lib_msg_queuegrouptrack,
					sizeof (struct res_lib_msg_queuegrouptrack));

				res_lib_msg_queuegrouptrack.header.size +=
					(sizeof (SaMsgQueueGroupNotificationT) *
					 res_lib_msg_queuegrouptrack.notificationBuffer.numberOfItems);

				api->ipc_conn_send_response (
					api->ipc_conn_partner_get (req_exec_msg_queuegrouptrack->source.conn),
					&res_lib_msg_queuegrouptrack,
					sizeof (struct res_lib_msg_queuegrouptrack));

				api->ipc_conn_send_response (
					api->ipc_conn_partner_get (req_exec_msg_queuegrouptrack->source.conn),
					notification,
					(sizeof (SaMsgQueueGroupNotificationT) *
					 res_lib_msg_queuegrouptrack.notificationBuffer.numberOfItems));
			}
		} else {
			api->ipc_conn_send_response (
				req_exec_msg_queuegrouptrack->source.conn,
				&res_lib_msg_queuegrouptrack,
				sizeof (struct res_lib_msg_queuegrouptrack));
		}
	}
}

static void message_handler_req_exec_msg_queuegrouptrackstop (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_msg_queuegrouptrackstop *req_exec_msg_queuegrouptrackstop =
		(struct req_exec_msg_queuegrouptrackstop *)message;
	struct res_lib_msg_queuegrouptrackstop res_lib_msg_queuegrouptrackstop;
	struct queue_group *queue_group;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saMsgQueueGroupTrackStop %s\n",
		getSaNameT (&req_exec_msg_queuegrouptrackstop->queue_group_name));

	queue_group = group_find (&req_exec_msg_queuegrouptrackstop->queue_group_name);

	if (queue_group == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if ((queue_group->track_flags != SA_TRACK_CHANGES) &&
	    (queue_group->track_flags != SA_TRACK_CHANGES_ONLY)) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	queue_group->track_flags = 0;

error_exit:
	if (api->ipc_source_is_local(&req_exec_msg_queuegrouptrackstop->source)) {
		res_lib_msg_queuegrouptrackstop.header.size =
			sizeof (struct res_lib_msg_queuegrouptrackstop);
		res_lib_msg_queuegrouptrackstop.header.id =
			MESSAGE_RES_MSG_QUEUEGROUPTRACKSTOP;
		res_lib_msg_queuegrouptrackstop.header.error = error;

		api->ipc_conn_send_response (
			req_exec_msg_queuegrouptrackstop->source.conn,
			&res_lib_msg_queuegrouptrackstop,
			sizeof (struct res_lib_msg_queuegrouptrackstop));
	}
}

static void message_handler_req_exec_msg_messagesend (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_msg_messagesend *req_exec_msg_messagesend =
		(struct req_exec_msg_messagesend *)message;
	struct res_lib_msg_messagesend res_lib_msg_messagesend;
	struct res_lib_msg_messagesendasync res_lib_msg_messagesendasync;
	struct queue_group *group;
	struct message_queue *queue = NULL;
	struct message_entry *entry = NULL;
	SaAisErrorT error = SA_AIS_OK;

	char *data = ((char *)(req_exec_msg_messagesend) +
		      sizeof (struct req_exec_msg_messagesend));

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saMsgMessageSend %s\n",
		getSaNameT (&req_exec_msg_messagesend->destination));

	group = group_find (&req_exec_msg_messagesend->destination);
	if (group == NULL) {
		queue = queue_find (&req_exec_msg_messagesend->destination);
		if (queue == NULL) {
			error = SA_AIS_ERR_NOT_EXIST;
			goto error_exit;
		}
	}

	entry = malloc (sizeof (struct message_entry));
	if (entry == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}

	memset (entry, 0, sizeof (struct message_entry));
	memcpy (&entry->message, &req_exec_msg_messagesend->message,
		sizeof (SaMsgMessageT));

	entry->message.data = malloc (entry->message.size);
	if (entry->message.data == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}

	memset (entry->message.data, 0, entry->message.size);
	memcpy (entry->message.data, (void *)(data), entry->message.size);

	entry->time = cluster_time_now();

	if (group != NULL) {
		/* if (group->policy == SA_MSG_QUEUE_GROUP_ROUND_ROBIN) */
		list_add_tail (&entry->list, &group->rr_queue->message_head);
		group->rr_queue = next_rr_queue (group);
	} else {
		list_add_tail (&entry->list, &queue->message_head);
	}

error_exit:

	if (api->ipc_source_is_local(&req_exec_msg_messagesend->source)) {
		if (req_exec_msg_messagesend->async_call) {
			res_lib_msg_messagesendasync.header.size =
				sizeof (struct res_lib_msg_messagesendasync);
			res_lib_msg_messagesendasync.header.id =
				MESSAGE_RES_MSG_MESSAGESENDASYNC;
			res_lib_msg_messagesendasync.header.error = error;
			res_lib_msg_messagesendasync.invocation =
				req_exec_msg_messagesend->invocation;

			memcpy (&res_lib_msg_messagesendasync.source,
				&req_exec_msg_messagesend->source,
				sizeof (mar_message_source_t));

			api->ipc_conn_send_response (
				req_exec_msg_messagesend->source.conn,
				&res_lib_msg_messagesendasync,
				sizeof (struct res_lib_msg_messagesendasync));

			api->ipc_conn_send_response (
				api->ipc_conn_partner_get (req_exec_msg_messagesend->source.conn),
				&res_lib_msg_messagesendasync,
				sizeof (struct res_lib_msg_messagesendasync));
		} else {
			res_lib_msg_messagesend.header.size =
				sizeof (struct res_lib_msg_messagesend);
			res_lib_msg_messagesend.header.id =
				MESSAGE_RES_MSG_MESSAGESEND;
			res_lib_msg_messagesend.header.error = error;

			memcpy (&res_lib_msg_messagesend.source,
				&req_exec_msg_messagesend->source,
				sizeof (mar_message_source_t));

			api->ipc_conn_send_response (
				req_exec_msg_messagesend->source.conn,
				&res_lib_msg_messagesend,
				sizeof (struct res_lib_msg_messagesend));
		}
	}
}

static void message_handler_req_exec_msg_messageget (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_msg_messageget *req_exec_msg_messageget =
		(struct req_exec_msg_messageget *)message;
	struct res_lib_msg_messageget res_lib_msg_messageget;
	struct message_queue *queue;
	struct message_entry *entry = NULL;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: saMsgMessageGet %s\n",
		getSaNameT (&req_exec_msg_messageget->queue_name));

	queue = queue_find (&req_exec_msg_messageget->queue_name);
	if (queue == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (list_empty (queue->message_head.next)) {
		error = SA_AIS_ERR_TIMEOUT; /* FIX ME */
		goto error_exit;
	}

	entry = list_entry (queue->message_head.next, struct message_entry, list);
	if (entry == NULL) {
		error = SA_AIS_ERR_LIBRARY; /* FIX ME */
		goto error_exit;
	}

	list_del (queue->message_head.next);

error_exit:

	if (api->ipc_source_is_local(&req_exec_msg_messageget->source)) {
		res_lib_msg_messageget.header.size =
			sizeof (struct res_lib_msg_messageget);
		res_lib_msg_messageget.header.id =
			MESSAGE_RES_MSG_MESSAGEGET;
		res_lib_msg_messageget.header.error = error;

		if (error == SA_AIS_OK) {
			memcpy (&res_lib_msg_messageget.message, &entry->message,
				sizeof (SaMsgMessageT));
		} else {
			memset (&res_lib_msg_messageget.message, 0,
				sizeof (SaMsgMessageT));
		}

		memcpy (&res_lib_msg_messageget.source,
			&req_exec_msg_messageget->source,
			sizeof (mar_message_source_t));

		api->ipc_conn_send_response (
			req_exec_msg_messageget->source.conn,
			&res_lib_msg_messageget,
			sizeof (struct res_lib_msg_messageget));

		api->ipc_conn_send_response (
			req_exec_msg_messageget->source.conn,
			res_lib_msg_messageget.message.data,
			res_lib_msg_messageget.message.size);
	}
}

static void message_handler_req_exec_msg_messagecancel (
	void *message,
	unsigned int nodeid)
{
#if 0
	struct req_exec_msg_messagecancel *req_exec_msg_messagecancel =
		(struct req_exec_msg_messagecancel *)message;
	struct res_lib_msg_messagecancel res_lib_msg_messagecancel;
#endif
}

static void message_handler_req_exec_msg_messagesendreceive (
	void *message,
	unsigned int nodeid)
{
#if 0
	struct req_exec_msg_messagesendreceive *req_exec_msg_messagesendreceive =
		(struct req_exec_msg_messagesendreceive *)message;
	struct res_lib_msg_messagesendreceive res_lib_msg_messagesendreceive;
#endif
}

static void message_handler_req_exec_msg_messagereply (
	void *message,
	unsigned int nodeid)
{
#if 0
	struct req_exec_msg_messagereply *req_exec_msg_messagereply =
		(struct req_exec_msg_messagereply *)message;
	struct res_lib_msg_messagereply res_lib_msg_messagereply;
#endif
}

static void message_handler_req_exec_msg_sync_queue (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_msg_sync_queue *req_exec_msg_sync_queue =
		(struct req_exec_msg_sync_queue *)message;
	struct message_queue *queue = NULL;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: sync queue %s\n",
		getSaNameT (&req_exec_msg_sync_queue->queue_name));

	/*
	 * Ignore messages from previous ring
	 */
	if (memcmp (&req_exec_msg_sync_queue->ring_id,
		    &my_saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	queue = sync_queue_find (&req_exec_msg_sync_queue->queue_name);

	if (queue == NULL)
	{
		queue = malloc (sizeof (struct message_queue));
		if (queue == NULL) {
			api->error_memory_failure ();
		}
		memset (queue, 0, sizeof (struct message_queue));
		memcpy (&queue->name,
			&req_exec_msg_sync_queue->queue_name,
			sizeof (SaNameT));

		queue->change = req_exec_msg_sync_queue->change;

		list_init (&queue->group_list);
		list_init (&queue->queue_list);
		list_init (&queue->message_head);
		list_add (&queue->queue_list, &sync_queue_list_head);

		/* DEBUG */
		/* print_queue_list (&sync_queue_list_head); */
	}
}

static void message_handler_req_exec_msg_sync_queue_entry (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_msg_sync_queue_entry *req_exec_msg_sync_queue_entry =
		(struct req_exec_msg_sync_queue_entry *)message;
	struct message_queue *queue;
	struct message_entry *entry;

	char *data = ((char *)(req_exec_msg_sync_queue_entry) +
		      sizeof (struct req_exec_msg_sync_queue_entry));

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: sync queue entry %s\n",
		getSaNameT (&req_exec_msg_sync_queue_entry->queue_name));

	/*
	 * Ignore messages from previous ring
	 */
	if (memcmp (&req_exec_msg_sync_queue_entry->ring_id,
		    &my_saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	queue = sync_queue_find (&req_exec_msg_sync_queue_entry->queue_name);

	assert (queue != NULL);

	entry = malloc (sizeof (struct message_entry));
	if (entry == NULL) {
		api->error_memory_failure ();
	}

	memset (entry, 0, sizeof (struct message_entry));
	memcpy (&entry->message, &req_exec_msg_sync_queue_entry->message,
		sizeof (SaMsgMessageT));

	entry->message.data = malloc (entry->message.size);
	if (entry->message.data == NULL) {
		api->error_memory_failure ();
	}

	memset (entry->message.data, 0, entry->message.size);
	memcpy (entry->message.data, (void *)(data), entry->message.size);

	entry->time = req_exec_msg_sync_queue_entry->time;

	list_add_tail (&entry->list, &queue->message_head);

	/* DEBUG */
	/* print_entry_list (queue); */
}

static void message_handler_req_exec_msg_sync_group (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_msg_sync_group *req_exec_msg_sync_group =
		(struct req_exec_msg_sync_group *)message;
	struct queue_group *group = NULL;
	struct message_queue *queue = NULL;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: sync group %s\n",
		getSaNameT (&req_exec_msg_sync_group->group_name));

	/*
	 * Ignore messages from previous ring
	 */
	if (memcmp (&req_exec_msg_sync_group->ring_id,
		    &my_saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	group = sync_group_find (&req_exec_msg_sync_group->group_name);

	if (group == NULL)
	{
		group = malloc (sizeof (struct queue_group));
		if (group == NULL) {
			api->error_memory_failure ();
		}
		memset (group, 0, sizeof (struct queue_group));
		memcpy (&group->name,
			&req_exec_msg_sync_group->group_name,
			sizeof (SaNameT));

		group->track_flags = req_exec_msg_sync_group->policy;

		if (req_exec_msg_sync_group->rr_queue_name.length != 0) {
			queue = sync_queue_find (&req_exec_msg_sync_group->rr_queue_name);
			assert (queue != NULL);
		}

		group->rr_queue = queue;

		list_init (&group->group_list);
		list_init (&group->queue_head);
		list_add (&group->group_list, &sync_group_list_head);

		/* DEBUG */
		/* print_group_list (&sync_group_list_head); */
	}
}

static void message_handler_req_exec_msg_sync_group_entry (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_msg_sync_group_entry *req_exec_msg_sync_group_entry =
		(struct req_exec_msg_sync_group_entry *)message;
	struct queue_group *group;
	struct message_queue *queue;

	log_printf (LOG_LEVEL_NOTICE, "EXEC request: sync group entry %s\n",
		getSaNameT (&req_exec_msg_sync_group_entry->group_name));

	/*
	 * Ignore messages from previous ring
	 */
	if (memcmp (&req_exec_msg_sync_group_entry->ring_id,
		    &my_saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	group = sync_group_find (&req_exec_msg_sync_group_entry->group_name);
	queue = sync_queue_find (&req_exec_msg_sync_group_entry->queue_name);

	assert (group != NULL);
	assert (queue != NULL);


	/*
	list_init (&queue->group_list);
	list_add (&queue->group_list, &group->queue_head);
	*/

	/* DEBUG */
	/*
	log_printf (LOG_LEVEL_NOTICE, "[DEBUG]: -=-=- GROUP = %s -=-=-\n",
		    getSaNameT (&group->name));
	print_queue_list (&group->queue_head);
	*/
}

static void message_handler_req_lib_msg_queueopen (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queueopen *req_lib_msg_queueopen =
		(struct req_lib_msg_queueopen *)msg;
	struct req_exec_msg_queueopen req_exec_msg_queueopen;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueOpen %s\n",
		getSaNameT (&req_lib_msg_queueopen->queueName));

	req_exec_msg_queueopen.header.size =
		sizeof (struct req_exec_msg_queueopen);
	req_exec_msg_queueopen.header.id = 
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEOPEN);

	api->ipc_source_set (&req_exec_msg_queueopen.source, conn);

	memcpy (&req_exec_msg_queueopen.queue_name,
		&req_lib_msg_queueopen->queueName, sizeof (SaNameT));

	memcpy (&req_exec_msg_queueopen.creation_attributes,
		&req_lib_msg_queueopen->creationAttributes,
		sizeof (SaMsgQueueCreationAttributesT));

	req_exec_msg_queueopen.async_call = 0;
	req_exec_msg_queueopen.invocation = 0;
	req_exec_msg_queueopen.queue_handle = req_lib_msg_queueopen->queueHandle;
	req_exec_msg_queueopen.openFlags = req_lib_msg_queueopen->openFlags;
	req_exec_msg_queueopen.timeout = req_lib_msg_queueopen->timeout;

	iovec.iov_base = (char *)&req_exec_msg_queueopen;
	iovec.iov_len = sizeof (req_exec_msg_queueopen);

	assert (api->totem_mcast (&iovec, 1,
		TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queueopenasync (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queueopen *req_lib_msg_queueopen =
		(struct req_lib_msg_queueopen *)msg;
	struct req_exec_msg_queueopen req_exec_msg_queueopen;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueOpenAsync %s\n",
		getSaNameT (&req_lib_msg_queueopen->queueName));

	req_exec_msg_queueopen.header.size =
		sizeof (struct req_exec_msg_queueopen);
	req_exec_msg_queueopen.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEOPEN);

	api->ipc_source_set (&req_exec_msg_queueopen.source, conn);

	memcpy (&req_exec_msg_queueopen.queue_name,
		&req_lib_msg_queueopen->queueName, sizeof (SaNameT));

	memcpy (&req_exec_msg_queueopen.creation_attributes,
		&req_lib_msg_queueopen->creationAttributes,
		sizeof (SaMsgQueueCreationAttributesT));

	req_exec_msg_queueopen.async_call = 1;
	req_exec_msg_queueopen.invocation = req_lib_msg_queueopen->invocation;
	req_exec_msg_queueopen.queue_handle = req_lib_msg_queueopen->queueHandle;
	req_exec_msg_queueopen.openFlags = req_lib_msg_queueopen->openFlags;
	req_exec_msg_queueopen.timeout = SA_TIME_END;

	iovec.iov_base = (char *)&req_exec_msg_queueopen;
	iovec.iov_len = sizeof (req_exec_msg_queueopen);

	assert (api->totem_mcast (&iovec, 1,
		TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queueclose (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queueclose *req_lib_msg_queueclose =
		(struct req_lib_msg_queueclose *)msg;
	struct req_exec_msg_queueclose req_exec_msg_queueclose;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueClose %s\n",
		getSaNameT (&req_lib_msg_queueclose->queueName));

	req_exec_msg_queueclose.header.size =
		sizeof (struct req_exec_msg_queueclose);
	req_exec_msg_queueclose.header.id = 
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUECLOSE);

	api->ipc_source_set (&req_exec_msg_queueclose.source, conn);

	memcpy (&req_exec_msg_queueclose.queue_name,
		&req_lib_msg_queueclose->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queueclose;
	iovec.iov_len = sizeof (req_exec_msg_queueclose);

	assert (api->totem_mcast (&iovec, 1,
		TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuestatusget (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queuestatusget *req_lib_msg_queuestatusget =
		(struct req_lib_msg_queuestatusget *)msg;
	struct req_exec_msg_queuestatusget req_exec_msg_queuestatusget;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueStatusGet %s\n",
		getSaNameT (&req_lib_msg_queuestatusget->queueName));

	req_exec_msg_queuestatusget.header.size =
		sizeof (struct req_exec_msg_queuestatusget);
	req_exec_msg_queuestatusget.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUESTATUSGET);

	api->ipc_source_set (&req_exec_msg_queuestatusget.source, conn);

	memcpy (&req_exec_msg_queuestatusget.queue_name,
		&req_lib_msg_queuestatusget->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuestatusget;
	iovec.iov_len = sizeof (req_exec_msg_queuestatusget);

	assert (api->totem_mcast (&iovec, 1,
		TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queueunlink (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queueunlink *req_lib_msg_queueunlink =
		(struct req_lib_msg_queueunlink *)msg;
	struct req_exec_msg_queueunlink req_exec_msg_queueunlink;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueUnlink %s\n",
		getSaNameT (&req_lib_msg_queueunlink->queueName));

	req_exec_msg_queueunlink.header.size =
		sizeof (struct req_exec_msg_queueunlink);
	req_exec_msg_queueunlink.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEUNLINK);

	api->ipc_source_set (&req_exec_msg_queueunlink.source, conn);

	memcpy (&req_exec_msg_queueunlink.queue_name,
		&req_lib_msg_queueunlink->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queueunlink;
	iovec.iov_len = sizeof (req_exec_msg_queueunlink);

	assert (api->totem_mcast (&iovec, 1,
		TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuegroupcreate (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queuegroupcreate *req_lib_msg_queuegroupcreate =
		(struct req_lib_msg_queuegroupcreate *)msg;
	struct req_exec_msg_queuegroupcreate req_exec_msg_queuegroupcreate;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueGroupCreate %s\n",
		getSaNameT (&req_lib_msg_queuegroupcreate->queueGroupName));

	req_exec_msg_queuegroupcreate.header.size =
		sizeof (struct req_exec_msg_queuegroupcreate);
	req_exec_msg_queuegroupcreate.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPCREATE);

	api->ipc_source_set (&req_exec_msg_queuegroupcreate.source, conn);

	memcpy (&req_exec_msg_queuegroupcreate.queue_group_name,
		&req_lib_msg_queuegroupcreate->queueGroupName, sizeof (SaNameT));

	req_exec_msg_queuegroupcreate.policy =
		req_lib_msg_queuegroupcreate->queueGroupPolicy;

	iovec.iov_base = (char *)&req_exec_msg_queuegroupcreate;
	iovec.iov_len = sizeof (req_exec_msg_queuegroupcreate);

	assert (api->totem_mcast (&iovec, 1,
		TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuegroupinsert (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queuegroupinsert *req_lib_msg_queuegroupinsert =
		(struct req_lib_msg_queuegroupinsert *)msg;
	struct req_exec_msg_queuegroupinsert req_exec_msg_queuegroupinsert;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueGroupInsert %s\n",
		getSaNameT (&req_lib_msg_queuegroupinsert->queueGroupName));

	req_exec_msg_queuegroupinsert.header.size =
		sizeof (struct req_exec_msg_queuegroupinsert);
	req_exec_msg_queuegroupinsert.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPINSERT);

	api->ipc_source_set (&req_exec_msg_queuegroupinsert.source, conn);

	memcpy (&req_exec_msg_queuegroupinsert.queue_name,
		&req_lib_msg_queuegroupinsert->queueName, sizeof (SaNameT));
	memcpy (&req_exec_msg_queuegroupinsert.queue_group_name,
		&req_lib_msg_queuegroupinsert->queueGroupName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuegroupinsert;
	iovec.iov_len = sizeof (req_exec_msg_queuegroupinsert);

	assert (api->totem_mcast (&iovec, 1,
		TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuegroupremove (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queuegroupremove *req_lib_msg_queuegroupremove =
		(struct req_lib_msg_queuegroupremove *)msg;
	struct req_exec_msg_queuegroupremove req_exec_msg_queuegroupremove;
	struct iovec iovec;

	req_exec_msg_queuegroupremove.header.size =
		sizeof (struct req_exec_msg_queuegroupremove);
	req_exec_msg_queuegroupremove.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPREMOVE);

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueGroupRemove %s\n",
		getSaNameT (&req_lib_msg_queuegroupremove->queueGroupName));

	api->ipc_source_set (&req_exec_msg_queuegroupremove.source, conn);

	memcpy (&req_exec_msg_queuegroupremove.queue_name,
		&req_lib_msg_queuegroupremove->queueName, sizeof (SaNameT));
	memcpy (&req_exec_msg_queuegroupremove.queue_group_name,
		&req_lib_msg_queuegroupremove->queueGroupName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuegroupremove;
	iovec.iov_len = sizeof (req_exec_msg_queuegroupremove);

	assert (api->totem_mcast (&iovec, 1,
		TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuegroupdelete (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queuegroupdelete *req_lib_msg_queuegroupdelete =
		(struct req_lib_msg_queuegroupdelete *)msg;
	struct req_exec_msg_queuegroupdelete req_exec_msg_queuegroupdelete;
	struct iovec iovec;

	req_exec_msg_queuegroupdelete.header.size =
		sizeof (struct req_exec_msg_queuegroupdelete);
	req_exec_msg_queuegroupdelete.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPDELETE);

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueGroupDelete %s\n",
		getSaNameT (&req_lib_msg_queuegroupdelete->queueGroupName));

	api->ipc_source_set (&req_exec_msg_queuegroupdelete.source, conn);

	memcpy (&req_exec_msg_queuegroupdelete.queue_group_name,
		&req_lib_msg_queuegroupdelete->queueGroupName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuegroupdelete;
	iovec.iov_len = sizeof (req_exec_msg_queuegroupdelete);

	assert (api->totem_mcast (&iovec, 1,
		TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuegrouptrack (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queuegrouptrack *req_lib_msg_queuegrouptrack =
		(struct req_lib_msg_queuegrouptrack *)msg;
	struct req_exec_msg_queuegrouptrack req_exec_msg_queuegrouptrack;
	struct iovec iovec;

	req_exec_msg_queuegrouptrack.header.size =
		sizeof (struct req_exec_msg_queuegrouptrack);
	req_exec_msg_queuegrouptrack.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPTRACK);

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueGroupTrack %s\n",
		getSaNameT (&req_lib_msg_queuegrouptrack->queueGroupName));

	api->ipc_source_set (&req_exec_msg_queuegrouptrack.source, conn);

	memcpy (&req_exec_msg_queuegrouptrack.queue_group_name,
		&req_lib_msg_queuegrouptrack->queueGroupName, sizeof (SaNameT));

	req_exec_msg_queuegrouptrack.track_flags =
		req_lib_msg_queuegrouptrack->trackFlags;
	req_exec_msg_queuegrouptrack.buffer_flag =
		req_lib_msg_queuegrouptrack->bufferFlag;

	iovec.iov_base = (char *)&req_exec_msg_queuegrouptrack;
	iovec.iov_len = sizeof (req_exec_msg_queuegrouptrack);

	assert (api->totem_mcast (&iovec, 1,
		TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_queuegrouptrackstop (
	void *conn,
	void *msg)
{
	struct req_lib_msg_queuegrouptrackstop *req_lib_msg_queuegrouptrackstop =
		(struct req_lib_msg_queuegrouptrackstop *)msg;
	struct req_exec_msg_queuegrouptrackstop req_exec_msg_queuegrouptrackstop;
	struct iovec iovec;

	req_exec_msg_queuegrouptrackstop.header.size =
		sizeof (struct req_exec_msg_queuegrouptrackstop);
	req_exec_msg_queuegrouptrackstop.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_QUEUEGROUPTRACKSTOP);

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgQueueGroupTrackStop %s\n",
		getSaNameT (&req_lib_msg_queuegrouptrackstop->queueGroupName));

	api->ipc_source_set (&req_exec_msg_queuegrouptrackstop.source, conn);

	memcpy (&req_exec_msg_queuegrouptrackstop.queue_group_name,
		&req_lib_msg_queuegrouptrackstop->queueGroupName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_queuegrouptrackstop;
	iovec.iov_len = sizeof (req_exec_msg_queuegrouptrackstop);

	assert (api->totem_mcast (&iovec, 1,
		TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_messagesend (
	void *conn,
	void *msg)
{
	struct req_lib_msg_messagesend *req_lib_msg_messagesend =
		(struct req_lib_msg_messagesend *)msg;
	struct req_exec_msg_messagesend req_exec_msg_messagesend;
	struct iovec iovecs[2];

	req_exec_msg_messagesend.header.size =
		sizeof (struct req_exec_msg_messagesend);
	req_exec_msg_messagesend.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGESEND);
	req_exec_msg_messagesend.async_call = 0;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageSend %s\n",
		getSaNameT (&req_lib_msg_messagesend->destination));

	api->ipc_source_set (&req_exec_msg_messagesend.source, conn);

	memcpy (&req_exec_msg_messagesend.destination,
		&req_lib_msg_messagesend->destination, sizeof (SaNameT));
	memcpy (&req_exec_msg_messagesend.message,
		&req_lib_msg_messagesend->message, sizeof (SaMsgMessageT));

	req_exec_msg_messagesend.async_call = 0;
	req_exec_msg_messagesend.invocation = 0;
	req_exec_msg_messagesend.ack_flags = req_lib_msg_messagesend->ackFlags;
	req_exec_msg_messagesend.timeout = req_lib_msg_messagesend->timeout;

	iovecs[0].iov_base = (char *)&req_exec_msg_messagesend;
	iovecs[0].iov_len = sizeof (req_exec_msg_messagesend);

	iovecs[1].iov_base = ((char *)req_lib_msg_messagesend) +
		sizeof (struct req_lib_msg_messagesend);
	iovecs[1].iov_len = req_lib_msg_messagesend->header.size -
		sizeof (struct req_lib_msg_messagesend);

	req_exec_msg_messagesend.header.size += iovecs[1].iov_len;

	if (iovecs[1].iov_len > 0) {
		assert (api->totem_mcast (iovecs, 2,
			TOTEM_AGREED) == 0);
	} else {
		assert (api->totem_mcast (iovecs, 1,
			TOTEM_AGREED) == 0);
	}
}

static void message_handler_req_lib_msg_messagesendasync (
	void *conn,
	void *msg)
{
	struct req_lib_msg_messagesend *req_lib_msg_messagesend =
		(struct req_lib_msg_messagesend *)msg;
	struct req_exec_msg_messagesend req_exec_msg_messagesend;
	struct iovec iovecs[2];

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageSendAsync %s\n",
		getSaNameT (&req_lib_msg_messagesend->destination));

	req_exec_msg_messagesend.header.size =
		sizeof (struct req_exec_msg_messagesend);
	req_exec_msg_messagesend.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGESEND);

	api->ipc_source_set (&req_exec_msg_messagesend.source, conn);

	memcpy (&req_exec_msg_messagesend.destination,
		&req_lib_msg_messagesend->destination, sizeof (SaNameT));
	memcpy (&req_exec_msg_messagesend.message,
		&req_lib_msg_messagesend->message, sizeof (SaMsgMessageT));

	req_exec_msg_messagesend.async_call = 1;
	req_exec_msg_messagesend.invocation = req_lib_msg_messagesend->invocation;
	req_exec_msg_messagesend.ack_flags = req_lib_msg_messagesend->ackFlags;
	req_exec_msg_messagesend.timeout = SA_TIME_END;

	iovecs[0].iov_base = (char *)&req_exec_msg_messagesend;
	iovecs[0].iov_len = sizeof (req_exec_msg_messagesend);

	iovecs[1].iov_base = ((char *)req_lib_msg_messagesend) +
		sizeof (struct req_lib_msg_messagesend);
	iovecs[1].iov_len = req_lib_msg_messagesend->header.size -
		sizeof (struct req_lib_msg_messagesend);

	req_exec_msg_messagesend.header.size += iovecs[1].iov_len;

	if (iovecs[1].iov_len > 0) {
		assert (api->totem_mcast (iovecs, 2,
			TOTEM_AGREED) == 0);
	} else {
		assert (api->totem_mcast (iovecs, 2,
			TOTEM_AGREED) == 0);
	}
}

static void message_handler_req_lib_msg_messageget (
	void *conn,
	void *msg)
{
	struct req_lib_msg_messageget *req_lib_msg_messageget =
		(struct req_lib_msg_messageget *)msg;
	struct req_exec_msg_messageget req_exec_msg_messageget;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageGet %s\n",
		getSaNameT (&req_lib_msg_messageget->queueName));

	req_exec_msg_messageget.header.size =
		sizeof (struct req_exec_msg_messageget);
	req_exec_msg_messageget.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGEGET);

	api->ipc_source_set (&req_exec_msg_messageget.source, conn);

	memcpy (&req_exec_msg_messageget.queue_name,
		&req_lib_msg_messageget->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_messageget;
	iovec.iov_len = sizeof (req_exec_msg_messageget);

	assert (api->totem_mcast (&iovec, 1,
		TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_messagecancel (
	void *conn,
	void *msg)
{
	struct req_lib_msg_messagecancel *req_lib_msg_messagecancel =
		(struct req_lib_msg_messagecancel *)msg;
	struct req_exec_msg_messagecancel req_exec_msg_messagecancel;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageCancel %s\n",
		getSaNameT (&req_lib_msg_messagecancel->queueName));

	req_exec_msg_messagecancel.header.size =
		sizeof (struct req_exec_msg_messagecancel);
	req_exec_msg_messagecancel.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGECANCEL);

	api->ipc_source_set (&req_exec_msg_messagecancel.source, conn);

	memcpy (&req_exec_msg_messagecancel.queue_name,
		&req_lib_msg_messagecancel->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_messagecancel;
	iovec.iov_len = sizeof (req_exec_msg_messagecancel);

	assert (api->totem_mcast (&iovec, 1,
		TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_messagesendreceive (
	void *conn,
	void *msg)
{
	struct req_lib_msg_messagesendreceive *req_lib_msg_messagesendreceive =
		(struct req_lib_msg_messagesendreceive *)msg;
	struct req_exec_msg_messagesendreceive req_exec_msg_messagesendreceive;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageSendReceive %s\n",
		getSaNameT (&req_lib_msg_messagesendreceive->queueName));

	req_exec_msg_messagesendreceive.header.size =
		sizeof (struct req_exec_msg_messagesendreceive);
	req_exec_msg_messagesendreceive.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGESENDRECEIVE);

	api->ipc_source_set (&req_exec_msg_messagesendreceive.source, conn);

	memcpy (&req_exec_msg_messagesendreceive.queue_name,
		&req_lib_msg_messagesendreceive->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_messagesendreceive;
	iovec.iov_len = sizeof (req_exec_msg_messagesendreceive);

	assert (api->totem_mcast (&iovec, 1,
		TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_messagereply (
	void *conn,
	void *msg)
{
	struct req_lib_msg_messagereply *req_lib_msg_messagereply =
		(struct req_lib_msg_messagereply *)msg;
	struct req_exec_msg_messagereply req_exec_msg_messagereply;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageReply %s\n",
		getSaNameT (&req_lib_msg_messagereply->queueName));

	req_exec_msg_messagereply.header.size =
		sizeof (struct req_exec_msg_messagereply);
	req_exec_msg_messagereply.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGEREPLY);
	req_exec_msg_messagereply.async_call = 0;

	api->ipc_source_set (&req_exec_msg_messagereply.source, conn);

	memcpy (&req_exec_msg_messagereply.queue_name,
		&req_lib_msg_messagereply->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_messagereply;
	iovec.iov_len = sizeof (req_exec_msg_messagereply);

	assert (api->totem_mcast (&iovec, 1,
		TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_msg_messagereplyasync (
	void *conn,
	void *msg)
{
	struct req_lib_msg_messagereply *req_lib_msg_messagereply =
		(struct req_lib_msg_messagereply *)msg;
	struct req_exec_msg_messagereply req_exec_msg_messagereply;
	struct iovec iovec;

	log_printf (LOG_LEVEL_NOTICE, "LIB request: saMsgMessageReplyAsync %s\n",
		getSaNameT (&req_lib_msg_messagereply->queueName));

	req_exec_msg_messagereply.header.size =
		sizeof (struct req_exec_msg_messagereply);
	req_exec_msg_messagereply.header.id =
		SERVICE_ID_MAKE (MSG_SERVICE, MESSAGE_REQ_EXEC_MSG_MESSAGEREPLY);
	req_exec_msg_messagereply.async_call = 1;

	api->ipc_source_set (&req_exec_msg_messagereply.source, conn);

	memcpy (&req_exec_msg_messagereply.queue_name,
		&req_lib_msg_messagereply->queueName, sizeof (SaNameT));

	iovec.iov_base = (char *)&req_exec_msg_messagereply;
	iovec.iov_len = sizeof (req_exec_msg_messagereply);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}
