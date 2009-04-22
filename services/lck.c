/*
 * Copyright (c) 2005-2006 MontaVista Software, Inc.
 * Copyright (c) 2006 Red Hat, Inc.
 * Copyright (c) 2006 Sun Microsystems, Inc.
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

#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include <corosync/coroipcc.h>
#include <corosync/corodefs.h>
#include <corosync/mar_gen.h>
#include <corosync/list.h>
#include <corosync/lcr/lcr_comp.h>
#include <corosync/swab.h>
#include <corosync/engine/coroapi.h>
#include <corosync/engine/logsys.h>

#include "../include/saAis.h"
#include "../include/saAis.h"
#include "../include/saLck.h"
#include "../include/ipc_lck.h"

LOGSYS_DECLARE_SUBSYS ("LCK");

enum lck_message_req_types {
	MESSAGE_REQ_EXEC_LCK_RESOURCEOPEN = 0,
	MESSAGE_REQ_EXEC_LCK_RESOURCECLOSE = 1,
	MESSAGE_REQ_EXEC_LCK_RESOURCELOCK = 2,
	MESSAGE_REQ_EXEC_LCK_RESOURCEUNLOCK = 3,
	MESSAGE_REQ_EXEC_LCK_RESOURCELOCKORPHAN = 4,
	MESSAGE_REQ_EXEC_LCK_LOCKPURGE = 5,
	MESSAGE_REQ_EXEC_LCK_SYNC_RESOURCE = 6,
	MESSAGE_REQ_EXEC_LCK_SYNC_RESOURCE_LOCK = 7,
	MESSAGE_REQ_EXEC_LCK_SYNC_RESOURCE_REFCOUNT = 8,
};

struct refcount_set {
	unsigned int refcount;
	unsigned int nodeid;
};

typedef struct {
	unsigned int refcount __attribute__((aligned(8)));
	unsigned int nodeid __attribute__((aligned(8)));
} mar_refcount_set_t;

struct resource;

struct resource_lock {
	SaLckLockModeT lock_mode;
	SaLckLockIdT lock_id;
	SaLckLockFlagsT lock_flags;
	SaLckWaiterSignalT waiter_signal;
	SaLckLockStatusT lock_status;
	SaTimeT timeout;
	struct resource *resource;
	int async_call;
	SaInvocationT invocation;
	mar_message_source_t callback_source;
	mar_message_source_t response_source;
	struct list_head list;
	struct list_head resource_list;
};

struct resource {
	mar_name_t name;
	int refcount;
	struct list_head list;
	struct list_head resource_lock_list_head;
	struct list_head pr_granted_list_head;
	struct list_head pr_pending_list_head;
	struct list_head ex_pending_list_head;
	struct resource_lock *ex_granted;
	struct refcount_set refcount_set[PROCESSOR_COUNT_MAX];
};

struct resource_cleanup {
	mar_name_t name;
	SaLckResourceHandleT resource_handle;
	struct list_head resource_lock_list_head;
	struct list_head list;
};

DECLARE_LIST_INIT(resource_list_head);

DECLARE_LIST_INIT(sync_resource_list_head);

static struct corosync_api_v1 *api;

/* static int lck_dump_fn (void); */

static int lck_exec_init_fn (struct corosync_api_v1 *);

static int lck_lib_exit_fn (void *conn);

static int lck_lib_init_fn (void *conn);

static unsigned int my_member_list[PROCESSOR_COUNT_MAX];

static unsigned int my_member_list_entries = 0;

static unsigned int my_lowest_nodeid = 0;

static void message_handler_req_exec_lck_resourceopen (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_resourceclose (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_resourcelock (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_resourceunlock (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_resourcelockorphan (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_lockpurge (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_sync_resource (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_sync_resource_lock (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_sync_resource_refcount (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_lib_lck_resourceopen (
	void *conn,
	const void *msg);

static void message_handler_req_lib_lck_resourceopenasync (
	void *conn,
	const void *msg);

static void message_handler_req_lib_lck_resourceclose (
	void *conn,
	const void *msg);

static void message_handler_req_lib_lck_resourcelock (
	void *conn,
	const void *msg);

static void message_handler_req_lib_lck_resourcelockasync (
	void *conn,
	const void *msg);

static void message_handler_req_lib_lck_resourceunlock (
	void *conn,
	const void *msg);

static void message_handler_req_lib_lck_resourceunlockasync (
	void *conn,
	const void *msg);

static void message_handler_req_lib_lck_lockpurge (
	void *conn,
	const void *msg);

static void exec_lck_resourceopen_endian_convert (void *msg);
static void exec_lck_resourceclose_endian_convert (void *msg);
static void exec_lck_resourcelock_endian_convert (void *msg);
static void exec_lck_resourceunlock_endian_convert (void *msg);
static void exec_lck_resourcelockorphan_endian_convert (void *msg);
static void exec_lck_lockpurge_endian_convert (void *msg);
#ifdef TODO
static void exec_lck_resource_endian_convert (void *msg);
static void exec_lck_resource_lock_endian_convert (void *msg);
#endif
static void exec_lck_sync_resource_endian_convert (void *msg);
static void exec_lck_sync_resource_lock_endian_convert (void *msg);
static void exec_lck_sync_resource_refcount_endian_convert (void *msg);

static void lck_sync_init (void);
static int  lck_sync_process (void);
static void lck_sync_activate (void);
static void lck_sync_abort (void);

static void sync_refcount_increment (
	struct resource *resource, unsigned int nodeid);
static void sync_refcount_decrement (
	struct resource *resource, unsigned int nodeid);
static void sync_refcount_calculate (
	struct resource *resource);

void resource_release (struct resource *resource);
void resource_lock_release (struct resource_lock *resource_lock);

/*
static struct list_head *recovery_lck_next = 0;
static struct list_head *recovery_lck_section_next = 0;
static int recovery_section_data_offset = 0;
static int recovery_section_send_flag = 0;
static int recovery_abort = 0;
*/

static struct memb_ring_id my_saved_ring_id;

static void lck_confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id);

struct lck_pd {
	struct list_head resource_list;
	struct list_head resource_cleanup_list;
};

/*
 * Executive Handler Definition
 */
static struct corosync_lib_handler lck_lib_engine[] =
{
	{ /* 0 */
		.lib_handler_fn		= message_handler_req_lib_lck_resourceopen,
		.response_size		= sizeof (struct res_lib_lck_resourceopen),
		.response_id		= MESSAGE_RES_LCK_RESOURCEOPEN,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn		= message_handler_req_lib_lck_resourceopenasync,
		.response_size		= sizeof (struct res_lib_lck_resourceopenasync),
		.response_id		= MESSAGE_RES_LCK_RESOURCEOPENASYNC,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn		= message_handler_req_lib_lck_resourceclose,
		.response_size		= sizeof (struct res_lib_lck_resourceclose),
		.response_id		= MESSAGE_RES_LCK_RESOURCECLOSE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn		= message_handler_req_lib_lck_resourcelock,
		.response_size		= sizeof (struct res_lib_lck_resourcelock),
		.response_id		= MESSAGE_RES_LCK_RESOURCELOCK,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn		= message_handler_req_lib_lck_resourcelockasync,
		.response_size		= sizeof (struct res_lib_lck_resourcelockasync),
		.response_id		= MESSAGE_RES_LCK_RESOURCELOCKASYNC,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 5 */
		.lib_handler_fn		= message_handler_req_lib_lck_resourceunlock,
		.response_size		= sizeof (struct res_lib_lck_resourceunlock),
		.response_id		= MESSAGE_RES_LCK_RESOURCELOCK,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 6 */
		.lib_handler_fn		= message_handler_req_lib_lck_resourceunlockasync,
		.response_size		= sizeof (struct res_lib_lck_resourceunlock),
		.response_id		= MESSAGE_RES_LCK_RESOURCEUNLOCKASYNC,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 7 */
		.lib_handler_fn		= message_handler_req_lib_lck_lockpurge,
		.response_size		= sizeof (struct res_lib_lck_lockpurge),
		.response_id		= MESSAGE_RES_LCK_LOCKPURGE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	}
};


static struct corosync_exec_handler lck_exec_engine[] = {
	{
		.exec_handler_fn	= message_handler_req_exec_lck_resourceopen,
		.exec_endian_convert_fn	= exec_lck_resourceopen_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_resourceclose,
		.exec_endian_convert_fn	= exec_lck_resourceclose_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_resourcelock,
		.exec_endian_convert_fn	= exec_lck_resourcelock_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_resourceunlock,
		.exec_endian_convert_fn	= exec_lck_resourceunlock_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_resourcelockorphan,
		.exec_endian_convert_fn	= exec_lck_resourcelockorphan_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_lockpurge,
		.exec_endian_convert_fn	= exec_lck_lockpurge_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_sync_resource,
		.exec_endian_convert_fn = exec_lck_sync_resource_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_sync_resource_lock,
		.exec_endian_convert_fn = exec_lck_sync_resource_lock_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_sync_resource_refcount,
		.exec_endian_convert_fn = exec_lck_sync_resource_refcount_endian_convert
	},
};

struct corosync_service_engine lck_service_engine = {
	.name				= "openais distributed locking service B.01.01",
	.id				= LCK_SERVICE,
	.private_data_size		= sizeof (struct lck_pd),
	.flow_control			= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED,
	.lib_init_fn			= lck_lib_init_fn,
	.lib_exit_fn			= lck_lib_exit_fn,
	.lib_engine			= lck_lib_engine,
	.lib_engine_count		= sizeof (lck_lib_engine) / sizeof (struct corosync_lib_handler),
	.exec_init_fn			= lck_exec_init_fn,
	.exec_dump_fn			= NULL,
	.exec_engine			= lck_exec_engine,
	.exec_engine_count		= sizeof (lck_exec_engine) / sizeof (struct corosync_exec_handler),
	.confchg_fn			= lck_confchg_fn,
	.sync_init			= lck_sync_init,
	.sync_process			= lck_sync_process,
	.sync_activate			= lck_sync_activate,
	.sync_abort			= lck_sync_abort,
};

/*
 * Dynamic loader definition
 */
static struct corosync_service_engine *lck_get_engine_ver0 (void);

static struct corosync_service_engine_iface_ver0 lck_service_engine_iface = {
	.corosync_get_service_engine_ver0	= lck_get_engine_ver0
};

static struct lcr_iface openais_lck_ver0[1] = {
	{
		.name				= "openais_lck",
		.version			= 0,
		.versions_replace		= 0,
		.versions_replace_count		= 0,
		.dependencies			= 0,
		.dependency_count		= 0,
		.constructor			= NULL,
		.destructor			= NULL,
		.interfaces			= (void **)(void *)&lck_service_engine_iface,
	}
};

static struct lcr_comp lck_comp_ver0 = {
	.iface_count				= 1,
	.ifaces					= openais_lck_ver0
};

static struct corosync_service_engine *lck_get_engine_ver0 (void)
{
	return (&lck_service_engine);
}

__attribute__ ((constructor)) static void register_this_component (void) {
	lcr_interfaces_set (&openais_lck_ver0[0], &lck_service_engine_iface);

	lcr_component_register (&lck_comp_ver0);
}

/*
 * All data types used for executive messages
 */
struct req_exec_lck_resourceopen {
	coroipc_request_header_t header;
	mar_message_source_t source;
	mar_name_t resource_name;
	SaLckResourceHandleT resource_handle;
	SaInvocationT invocation;
	SaTimeT timeout;
	SaLckResourceOpenFlagsT open_flags;
	int async_call;
	SaAisErrorT fail_with_error;
};

static void exec_lck_resourceopen_endian_convert (void *msg)
{
	struct req_exec_lck_resourceopen *to_swab =
		(struct req_exec_lck_resourceopen *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->resource_name);
	to_swab->resource_handle = swab64 (to_swab->resource_handle);
	to_swab->invocation = swab64 (to_swab->invocation);
	to_swab->timeout = swab64 (to_swab->timeout);
	to_swab->open_flags = swab32 (to_swab->open_flags);
	to_swab->async_call = swab32 (to_swab->async_call);
	to_swab->fail_with_error = swab32 (to_swab->fail_with_error);
}

struct req_exec_lck_resourceclose {
	coroipc_request_header_t header;
	mar_message_source_t source;
	mar_name_t lockResourceName;
	SaLckResourceHandleT resource_handle;
};

static void exec_lck_resourceclose_endian_convert (void *msg)
{
	struct req_exec_lck_resourceclose *to_swab =
		(struct req_exec_lck_resourceclose *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->lockResourceName);
	to_swab->resource_handle = swab64 (to_swab->resource_handle);
}

struct req_exec_lck_resourcelock {
	coroipc_request_header_t header;
	SaLckResourceHandleT resource_handle;
	SaInvocationT invocation;
	int async_call;
	SaAisErrorT fail_with_error;
	mar_message_source_t source;
	struct req_lib_lck_resourcelock req_lib_lck_resourcelock;
};

static void exec_lck_resourcelock_endian_convert (void *msg)
{
	struct req_exec_lck_resourcelock *to_swab =
		(struct req_exec_lck_resourcelock *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	to_swab->resource_handle = swab64 (to_swab->resource_handle);
	to_swab->invocation = swab64 (to_swab->invocation);
	to_swab->async_call = swab32 (to_swab->async_call);
	to_swab->fail_with_error = swab32 (to_swab->fail_with_error);
	swab_mar_message_source_t (&to_swab->source);
	swab_req_lib_lck_resourcelock (&to_swab->req_lib_lck_resourcelock);
}

struct req_exec_lck_resourceunlock {
	coroipc_request_header_t header;
	mar_message_source_t source;
	mar_name_t resource_name;
	SaLckLockIdT lock_id;
	SaInvocationT invocation;
	SaTimeT timeout;
	int async_call;
};

static void exec_lck_resourceunlock_endian_convert (void *msg)
{
	struct req_exec_lck_resourceunlock *to_swab =
		(struct req_exec_lck_resourceunlock *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->resource_name);
	to_swab->lock_id = swab64 (to_swab->lock_id);
	to_swab->invocation = swab64 (to_swab->invocation);
	to_swab->timeout = swab64 (to_swab->timeout);
	to_swab->async_call = swab32 (to_swab->async_call);
}

struct req_exec_lck_resourcelockorphan {
	coroipc_request_header_t header;
	mar_message_source_t source;
	mar_name_t resource_name;
	SaLckLockIdT lock_id;
};

static void exec_lck_resourcelockorphan_endian_convert (void *msg)
{
	struct req_exec_lck_resourcelockorphan *to_swab =
		(struct req_exec_lck_resourcelockorphan *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->resource_name);
	to_swab->lock_id = swab64 (to_swab->lock_id);
}

struct req_exec_lck_lockpurge {
	coroipc_request_header_t header;
	mar_message_source_t source;
	struct req_lib_lck_lockpurge req_lib_lck_lockpurge;
};

static void exec_lck_lockpurge_endian_convert (void *msg)
{
	struct req_exec_lck_lockpurge *to_swab =
		(struct req_exec_lck_lockpurge *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_req_lib_lck_lockpurge (&to_swab->req_lib_lck_lockpurge);
}

struct req_exec_lck_sync_resource {
	coroipc_request_header_t header;
	struct memb_ring_id ring_id;
	mar_name_t resource_name;
};

static void exec_lck_sync_resource_endian_convert (void *msg)
{
	struct req_exec_lck_sync_resource *to_swab =
		(struct req_exec_lck_sync_resource *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	/* swab_mar_memb_ring_id_t (&to_swab->memb_ring_id); */
	swab_mar_name_t (&to_swab->resource_name);
}

struct req_exec_lck_sync_resource_lock {
	coroipc_request_header_t header;
	struct memb_ring_id ring_id;
	mar_name_t resource_name;
	SaLckLockIdT lock_id;
	SaLckLockModeT lock_mode;
	SaLckLockFlagsT lock_flags;
	SaLckWaiterSignalT waiter_signal;
	SaLckLockStatusT lock_status;
	SaTimeT timeout;
};

static void exec_lck_sync_resource_lock_endian_convert (void *msg)
{
	struct req_exec_lck_sync_resource_lock *to_swab =
		(struct req_exec_lck_sync_resource_lock *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	/* swab_mar_memb_ring_id_t (&to_swab->memb_ring_id); */
	swab_mar_name_t (&to_swab->resource_name);
	to_swab->lock_id = swab64 (to_swab->lock_id);
	to_swab->lock_mode = swab64 (to_swab->lock_mode);
	to_swab->lock_flags = swab32 (to_swab->lock_flags);
	to_swab->waiter_signal = swab32 (to_swab->waiter_signal);
	to_swab->lock_status = swab64 (to_swab->lock_status);
	to_swab->timeout = swab64 (to_swab->timeout);
}

struct req_exec_lck_sync_resource_refcount {
	coroipc_request_header_t header __attribute__((aligned(8)));
	struct memb_ring_id ring_id __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
	mar_refcount_set_t refcount_set[PROCESSOR_COUNT_MAX] __attribute__((aligned(8)));
};

static void exec_lck_sync_resource_refcount_endian_convert (void *msg)
{
	return;
}

#ifdef PRINTING_ENABLED
static void print_resource_lock_list (struct resource *resource)
{
	struct list_head *list;
	struct resource_lock *resource_lock;

	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: resource_lock_list ...\n");

	for (list = resource->resource_lock_list_head.next;
	     list != &resource->resource_lock_list_head;
	     list = list->next)
	{
		resource_lock = list_entry (list, struct resource_lock, resource_list);

		log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]:\t id=%u mode=%u status=%u addr=%p\n",
			    (unsigned int)(resource_lock->lock_id),
			    (unsigned int)(resource_lock->lock_mode),
			    (unsigned int)(resource_lock->lock_status),
			    (void *)(resource_lock));
	}
}

static void print_pr_pending_list (struct resource *resource)
{
	struct list_head *list;
	struct resource_lock *resource_lock;

	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: pr_pending_list ...\n");

	for (list = resource->pr_pending_list_head.next;
	     list != &resource->pr_pending_list_head;
	     list = list->next)
	{
		resource_lock = list_entry (list, struct resource_lock, list);

		log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]:\t id=%u mode=%u status=%u addr=%p\n",
			    (unsigned int)(resource_lock->lock_id),
			    (unsigned int)(resource_lock->lock_mode),
			    (unsigned int)(resource_lock->lock_status),
			    (void *)(resource_lock));
	}
}

static void print_pr_granted_list (struct resource *resource)
{
	struct list_head *list;
	struct resource_lock *resource_lock;

	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: pr_granted_list ...\n");

	for (list = resource->pr_granted_list_head.next;
	     list != &resource->pr_granted_list_head;
	     list = list->next)
	{
		resource_lock = list_entry (list, struct resource_lock, list);

		log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]:\t id=%u mode=%u status=%u addr=%p\n",
			    (unsigned int)(resource_lock->lock_id),
			    (unsigned int)(resource_lock->lock_mode),
			    (unsigned int)(resource_lock->lock_status),
			    (void *)(resource_lock));
	}
}

static void print_ex_pending_list (struct resource *resource)
{
	struct list_head *list;
	struct resource_lock *resource_lock;

	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: ex_pending_list ...\n");

	for (list = resource->ex_pending_list_head.next;
	     list != &resource->ex_pending_list_head;
	     list = list->next)
	{
		resource_lock = list_entry (list, struct resource_lock, list);

		log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]:\t id=%u mode=%u status=%u addr=%p\n",
			    (unsigned int)(resource_lock->lock_id),
			    (unsigned int)(resource_lock->lock_mode),
			    (unsigned int)(resource_lock->lock_status),
			    (void *)(resource_lock));
	}
}

static void print_resource_list (struct list_head *head)
{
	struct list_head *list;
	struct resource *resource;

	for (list = head->next;
	     list != head;
	     list = list->next)
	{
		resource = list_entry (list, struct resource, list);

		log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: print_resource_list { name=%s addr=%p }\n",
			    get_mar_name_t (&resource->name), (void *)(resource));

		print_resource_lock_list (resource);
	}
}
#endif

void resource_release (struct resource *resource)
{
	struct resource_lock *resource_lock;
	struct list_head *list;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: resource_release { name=%s addr=%p }\n",
		    get_mar_name_t (&resource->name), (void *)(resource));

	for (list = resource->resource_lock_list_head.next;
	     list != &resource->resource_lock_list_head;)
	{
		resource_lock = list_entry (list, struct resource_lock, resource_list);

		list = list->next;

		resource_lock_release (resource_lock);
	}

	list_del (&resource->list);
}

void resource_lock_release (struct resource_lock *resource_lock)
{
	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: resource_lock_release { id=%u addr=%p }\n",
		    (unsigned int)(resource_lock->lock_id), (void *)(resource_lock));

	list_del (&resource_lock->list);
	free (resource_lock);
}

static void sync_refcount_increment (
	struct resource *resource,
	unsigned int nodeid)
{
	unsigned int i;

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		if (resource->refcount_set[i].nodeid == 0) {
			resource->refcount_set[i].nodeid = nodeid;
			resource->refcount_set[i].refcount = 1;
			break;
		}
		if (resource->refcount_set[i].nodeid == nodeid) {
			resource->refcount_set[i].refcount += 1;
			break;
		}
	}
}

static void sync_refcount_decrement (
	struct resource *resource,
	unsigned int nodeid)
{
	unsigned int i;

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		if (resource->refcount_set[i].nodeid == 0) {
			break;
		}
		if (resource->refcount_set[i].nodeid == nodeid) {
			resource->refcount_set[i].refcount -= 1;
			break;
		}
	}
}

static void sync_refcount_calculate (
	struct resource *resource)
{
	unsigned int i;

	resource->refcount = 0;

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		if (resource->refcount_set[i].nodeid == 0) {
			break;
		}
	}
	resource->refcount += resource->refcount_set[i].refcount;
}

static inline void sync_resource_free (struct list_head *head)
{
	struct resource *resource;
	struct list_head *list;

	list = head->next;

	while (list != head) {
		resource = list_entry (list, struct resource, list);

		list = list->next;

		resource_release (resource);
	}

	list_init (head);

	return;
}

static inline void sync_resource_lock_free (struct list_head *head)
{
	return;
}

static int sync_resource_transmit (
	struct resource *resource)
{
	struct req_exec_lck_sync_resource req_exec_lck_sync_resource;
	struct iovec iovec;

	memset (&req_exec_lck_sync_resource, 0,
		sizeof (struct req_exec_lck_sync_resource));

	req_exec_lck_sync_resource.header.size =
		sizeof (struct req_exec_lck_sync_resource);
	req_exec_lck_sync_resource.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_SYNC_RESOURCE);

	memcpy (&req_exec_lck_sync_resource.ring_id,
		&my_saved_ring_id, sizeof (struct memb_ring_id));
	memcpy (&req_exec_lck_sync_resource.resource_name,
		&resource->name, sizeof (mar_name_t));

	iovec.iov_base = (char *)&req_exec_lck_sync_resource;
	iovec.iov_len = sizeof (req_exec_lck_sync_resource);

	return (api->totem_mcast (&iovec, 1, TOTEM_AGREED));
}

static int sync_resource_lock_transmit (
	struct resource *resource,
	struct resource_lock *resource_lock)
{
	struct req_exec_lck_sync_resource_lock req_exec_lck_sync_resource_lock;
	struct iovec iovec;

	memset (&req_exec_lck_sync_resource_lock, 0,
		sizeof (struct req_exec_lck_sync_resource_lock));

	req_exec_lck_sync_resource_lock.header.size =
		sizeof (struct req_exec_lck_sync_resource_lock);
	req_exec_lck_sync_resource_lock.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_SYNC_RESOURCE_LOCK);

	memcpy (&req_exec_lck_sync_resource_lock.ring_id,
		&my_saved_ring_id, sizeof (struct memb_ring_id));
	memcpy (&req_exec_lck_sync_resource_lock.resource_name,
		&resource->name, sizeof (mar_name_t));

	req_exec_lck_sync_resource_lock.lock_id = resource_lock->lock_id;
	req_exec_lck_sync_resource_lock.lock_mode = resource_lock->lock_mode;
	req_exec_lck_sync_resource_lock.lock_flags = resource_lock->lock_flags;
	req_exec_lck_sync_resource_lock.waiter_signal = resource_lock->waiter_signal;
	req_exec_lck_sync_resource_lock.lock_status = resource_lock->lock_status;
	req_exec_lck_sync_resource_lock.timeout = resource_lock->timeout;

	iovec.iov_base = (char *)&req_exec_lck_sync_resource_lock;
	iovec.iov_len = sizeof (req_exec_lck_sync_resource_lock);

	return (api->totem_mcast (&iovec, 1, TOTEM_AGREED));
}

static int sync_resource_refcount_transmit (
	struct resource *resource)
{
	struct req_exec_lck_sync_resource_refcount req_exec_lck_sync_resource_refcount;
	struct iovec iovec;
	unsigned int i;

	memset (&req_exec_lck_sync_resource_refcount, 0,
		sizeof (struct req_exec_lck_sync_resource_refcount));

	req_exec_lck_sync_resource_refcount.header.size =
		sizeof (struct req_exec_lck_sync_resource_refcount);
	req_exec_lck_sync_resource_refcount.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_SYNC_RESOURCE_REFCOUNT);

	memcpy (&req_exec_lck_sync_resource_refcount.ring_id,
		&my_saved_ring_id, sizeof (struct memb_ring_id));
	memcpy (&req_exec_lck_sync_resource_refcount.resource_name,
		&resource->name, sizeof (mar_name_t));

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		req_exec_lck_sync_resource_refcount.refcount_set[i].refcount =
			resource->refcount_set[i].refcount;
		req_exec_lck_sync_resource_refcount.refcount_set[i].nodeid =
			resource->refcount_set[i].nodeid;
	}

	iovec.iov_base = (char *)&req_exec_lck_sync_resource_refcount;
	iovec.iov_len = sizeof (struct req_exec_lck_sync_resource_refcount);

	return (api->totem_mcast (&iovec, 1, TOTEM_AGREED));
}

static int sync_resource_iterate (void)
{
	struct resource *resource;
	struct resource_lock *resource_lock;
	struct list_head *resource_list;
	struct list_head *resource_lock_list;
	unsigned int res = 0;

	for (resource_list = resource_list_head.next;
	     resource_list != &resource_list_head;
	     resource_list = resource_list->next)
	{
		resource = list_entry (resource_list, struct resource, list);

		res = sync_resource_transmit (resource);
		if (res != 0) {
			break;
		}

		for (resource_lock_list = resource->resource_lock_list_head.next;
		     resource_lock_list != &resource->resource_lock_list_head;
		     resource_lock_list = resource_lock_list->next)
		{
			resource_lock = list_entry (resource_lock_list,
						    struct resource_lock,
						    resource_list);

			res = sync_resource_lock_transmit (resource, resource_lock);
			if (res != 0) {
				break;
			}
		}
	}

	return (res);
}

static int sync_refcount_iterate (void)
{
	struct resource *resource;
	struct list_head *list;
	unsigned int res = 0;

	for (list = resource_list_head.next;
	     list != &resource_list_head;
	     list = list->next) {

		resource = list_entry (list, struct resource, list);

		res = sync_resource_refcount_transmit (resource);
		if (res != 0) {
			break;
		}
	}

	return (res);
}

static void lck_sync_init (void)
{
	return;
}

static int lck_sync_process (void)
{
	unsigned int res = 0;

	if (my_lowest_nodeid == api->totem_nodeid_get ()) {
		TRACE1 ("transmit resources because lowest member in old configuration.\n");

		res = sync_resource_iterate ();
	}

	if (my_lowest_nodeid == api->totem_nodeid_get ()) {
		TRACE1 ("transmit refcounts because lowest member in old configuration.\n");

		sync_refcount_iterate ();
	}

	return (0);
}

static void lck_sync_activate (void)
{
	sync_resource_free (&resource_list_head);

	list_init (&resource_list_head);

	if (!list_empty (&sync_resource_list_head)) {
		list_splice (&sync_resource_list_head, &resource_list_head);
	}

	list_init (&sync_resource_list_head);

	return;
}

static void lck_sync_abort (void)
{
	return;
}

static void lck_confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id)
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

static struct resource *lck_resource_find (
	struct list_head *head,
	const mar_name_t *name)
{
	struct list_head *resource_list;
	struct resource *resource;

	for (resource_list = head->next;
	     resource_list != head;
	     resource_list = resource_list->next)
	{
		resource = list_entry (resource_list, struct resource, list);

		if (mar_name_match (name, &resource->name)) {
			return (resource);
		}
	}
	return (0);
}

static struct resource_lock *lck_resource_lock_find (
	struct resource *resource,
	const mar_message_source_t *source,
	SaLckLockIdT lock_id)
{
	struct list_head *list;
	struct resource_lock *resource_lock;

	for (list = resource->resource_lock_list_head.next;
	     list != &resource->resource_lock_list_head;
	     list = list->next)
	{
		resource_lock = list_entry (list, struct resource_lock, resource_list);

		if ((memcmp (&resource_lock->callback_source,
			     source, sizeof (mar_message_source_t)) == 0) &&
		    (lock_id == resource_lock->lock_id)) {
			return (resource_lock);
		}
	}
	return (0);
}

static struct resource_cleanup *lck_resource_cleanup_find (
	void *conn,
	SaLckResourceHandleT resource_handle)
{
	struct list_head *list;
	struct resource_cleanup *resource_cleanup;
	struct lck_pd *lck_pd = (struct lck_pd *)api->ipc_private_data_get (conn);

	for (list = lck_pd->resource_cleanup_list.next;
	     list != &lck_pd->resource_cleanup_list;
	     list = list->next)
	{
		resource_cleanup = list_entry (list, struct resource_cleanup, list);

		if (resource_cleanup->resource_handle == resource_handle) {
			return (resource_cleanup);
		}
	}
	return (0);
}

static int lck_resource_close (const mar_name_t *resource_name)
{
	struct req_exec_lck_resourceclose req_exec_lck_resourceclose;
	struct iovec iovec;

	req_exec_lck_resourceclose.header.size =
		sizeof (struct req_exec_lck_resourceclose);
	req_exec_lck_resourceclose.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCECLOSE);

	memcpy (&req_exec_lck_resourceclose.lockResourceName,
		resource_name, sizeof (mar_name_t));

	iovec.iov_base = (char *)&req_exec_lck_resourceclose;
	iovec.iov_len = sizeof (req_exec_lck_resourceclose);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
	return (-1);
}

static void resource_lock_orphan (struct resource_lock *resource_lock)
{
	struct req_exec_lck_resourcelockorphan req_exec_lck_resourcelockorphan;
	struct iovec iovec;

	req_exec_lck_resourcelockorphan.header.size =
		sizeof (struct req_exec_lck_resourcelockorphan);
	req_exec_lck_resourcelockorphan.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCELOCKORPHAN);

	memcpy (&req_exec_lck_resourcelockorphan.source,
		&resource_lock->callback_source,
		sizeof (mar_message_source_t));

	memcpy (&req_exec_lck_resourcelockorphan.resource_name,
		&resource_lock->resource->name,
		sizeof (mar_name_t));

	req_exec_lck_resourcelockorphan.lock_id = resource_lock->lock_id;

	iovec.iov_base = (char *)&req_exec_lck_resourcelockorphan;
	iovec.iov_len = sizeof (req_exec_lck_resourcelockorphan);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void lck_resource_cleanup_lock_remove (
	struct resource_cleanup *resource_cleanup)
{
	struct list_head *list;
	struct resource *resource;
	struct resource_lock *resource_lock;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: resource_cleanup_lock_remove { %s }\n",
		    get_mar_name_t (&resource_cleanup->name));

	resource = lck_resource_find (&resource_list_head,
		&resource_cleanup->name);

	assert (resource != NULL);

	for (list = resource->resource_lock_list_head.next;
	     list != &resource->resource_lock_list_head;
	     list = list->next)
	{
		resource_lock = list_entry (list, struct resource_lock, resource_list);

		/* DEBUG */
		log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]:\t lock_id=%u addr=%p\n",
			    (unsigned int)(resource_lock->lock_id),
			    (void *)(resource_lock));

		resource_lock_orphan (resource_lock);
	}

}

static void lck_resource_cleanup_remove (
	void *conn,
	SaLckResourceHandleT resource_handle)
{
	struct list_head *list;
	struct resource_cleanup *resource_cleanup;
	struct lck_pd *lck_pd = (struct lck_pd *)api->ipc_private_data_get (conn);

	for (list = lck_pd->resource_cleanup_list.next;
	     list != &lck_pd->resource_cleanup_list;
	     list = list->next) {

		resource_cleanup = list_entry (list, struct resource_cleanup, list);

		if (resource_cleanup->resource_handle == resource_handle) {
			list_del (&resource_cleanup->list);
			free (resource_cleanup);
			return;
		}
	}
}

static int lck_exec_init_fn (struct corosync_api_v1 *corosync_api)
{
	api = corosync_api;

	/*
	 *  Initialize the saved ring ID.
	 */
	return (0);
}

static int lck_lib_exit_fn (void *conn)
{
	struct resource_cleanup *resource_cleanup;
	struct list_head *cleanup_list;
	struct lck_pd *lck_pd = (struct lck_pd *)api->ipc_private_data_get (conn);

	log_printf (LOGSYS_LEVEL_DEBUG, "lck_exit_fn conn_info %p\n", conn);

	cleanup_list = lck_pd->resource_cleanup_list.next;

	while (!list_empty(&lck_pd->resource_cleanup_list)) {

		resource_cleanup = list_entry (cleanup_list, struct resource_cleanup, list);

		/* if (resource_cleanup->resource->name.length > 0) {
			lck_resource_cleanup_lock_remove (resource_cleanup);
			lck_resource_close (resource_cleanup->resource);
		}*/

		assert (resource_cleanup->name.length != 0);

		/* DEBUG */
		log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: resource_cleanup { %s }\n",
			    get_mar_name_t (&resource_cleanup->name));

		lck_resource_cleanup_lock_remove (resource_cleanup);
		lck_resource_close (&resource_cleanup->name);

		list_del (&resource_cleanup->list);
		free (resource_cleanup);

		cleanup_list = lck_pd->resource_cleanup_list.next;
	}

	return (0);
}

#if 0
static int lck_lib_exit_fn (void *conn)
{
	struct resource_cleanup *resource_cleanup;
	struct list_head *cleanup_list;
	struct lck_pd *lck_pd = (struct lck_pd *)api->ipc_private_data_get (conn);

	log_printf (LOGSYS_LEVEL_DEBUG, "lck_exit_fn conn_info %p\n", conn);

	for (cleanup_list = lck_pd->resource_cleanup_list.next;
	     cleanup_list != &lck_pd->resource_cleanup_list;
	     cleanup_list = cleanup_list->next)
	{
		resource_cleanup = list_entry (cleanup_list, struct resource_cleanup, list);

		/* DEBUG */
		log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: resource_cleanup { %s }\n",
			    get_mar_name_t (&resource_cleanup->name));

		lck_resource_cleanup_lock_remove (resource_cleanup);
		lck_resource_close (resource_cleanup->name);
	}

	return (0);
}
#endif

static int lck_lib_init_fn (void *conn)
{
	struct lck_pd *lck_pd = (struct lck_pd *)api->ipc_private_data_get (conn);

	log_printf (LOGSYS_LEVEL_DEBUG, "lck_init_fn conn_info %p\n", conn);

	list_init (&lck_pd->resource_list);
	list_init (&lck_pd->resource_cleanup_list);

	return (0);
}

static void message_handler_req_exec_lck_resourceopen (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_lck_resourceopen *req_exec_lck_resourceopen =
		message;
	struct res_lib_lck_resourceopen res_lib_lck_resourceopen;
	struct res_lib_lck_resourceopenasync res_lib_lck_resourceopenasync;
	struct resource *resource;
	struct resource_cleanup *resource_cleanup;
	SaAisErrorT error = SA_AIS_OK;
	struct lck_pd *lck_pd;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saLckResourceOpen %s\n",
		get_mar_name_t (&req_exec_lck_resourceopen->resource_name));

	if (req_exec_lck_resourceopen->fail_with_error != SA_AIS_OK) {
		error = req_exec_lck_resourceopen->fail_with_error;
		goto error_exit;
	}

	resource = lck_resource_find (&resource_list_head,
		&req_exec_lck_resourceopen->resource_name);

	if (resource == NULL) {
		if ((req_exec_lck_resourceopen->open_flags & SA_LCK_RESOURCE_CREATE) == 0) {
			error = SA_AIS_ERR_NOT_EXIST;
			goto error_exit;
		}
		resource = malloc (sizeof (struct resource));
		if (resource == NULL) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}
		memset (resource, 0, sizeof (struct resource));
		memcpy (&resource->name,
			&req_exec_lck_resourceopen->resource_name,
			sizeof (mar_name_t));

		list_init (&resource->list);
		list_init (&resource->resource_lock_list_head);
		/* list_add (&resource->list, &resource_list_head); */
		list_add_tail (&resource->list, &resource_list_head);
		list_init (&resource->pr_granted_list_head);
		list_init (&resource->pr_pending_list_head);
		list_init (&resource->ex_pending_list_head);

		resource->refcount = 0;
		resource->ex_granted = NULL;

		memset (&resource->refcount_set, 0,
			sizeof (struct refcount_set) * PROCESSOR_COUNT_MAX);
	}

	log_printf (LOGSYS_LEVEL_DEBUG, "RESOURCE opened is %p\n", resource);

	sync_refcount_increment (resource, nodeid);
	sync_refcount_calculate (resource);

	if (api->ipc_source_is_local (&req_exec_lck_resourceopen->source)) {
		resource_cleanup = malloc (sizeof (struct resource_cleanup));
		if (resource_cleanup == NULL) {
			free (resource);
			error = SA_AIS_ERR_NO_MEMORY;
		} else {
			lck_pd = (struct lck_pd *)api->ipc_private_data_get (req_exec_lck_resourceopen->source.conn);
			list_init (&resource_cleanup->list);
			list_init (&resource_cleanup->resource_lock_list_head);
			/* resource_cleanup->resource = resource; */
			resource_cleanup->resource_handle = req_exec_lck_resourceopen->resource_handle;

			memcpy (&resource_cleanup->name,
				&req_exec_lck_resourceopen->resource_name,
				sizeof (mar_name_t));

			/* list_add (&resource_cleanup->list, &lck_pd->resource_cleanup_list); */
			list_add_tail (&resource_cleanup->list, &lck_pd->resource_cleanup_list);
		}
		resource->refcount += 1;
	}

error_exit:
	if (api->ipc_source_is_local (&req_exec_lck_resourceopen->source)) {
		if (req_exec_lck_resourceopen->async_call) {
			res_lib_lck_resourceopenasync.header.size = sizeof (struct res_lib_lck_resourceopenasync);
			res_lib_lck_resourceopenasync.header.id = MESSAGE_RES_LCK_RESOURCEOPENASYNC;
			res_lib_lck_resourceopenasync.header.error = error;
			res_lib_lck_resourceopenasync.resourceHandle = req_exec_lck_resourceopen->resource_handle;
			res_lib_lck_resourceopenasync.invocation = req_exec_lck_resourceopen->invocation;
			memcpy (&res_lib_lck_resourceopenasync.source,
				&req_exec_lck_resourceopen->source,
				sizeof (mar_message_source_t));

			api->ipc_response_send (
				req_exec_lck_resourceopen->source.conn,
				&res_lib_lck_resourceopenasync,
				sizeof (struct res_lib_lck_resourceopenasync));
			api->ipc_dispatch_send (
				req_exec_lck_resourceopen->source.conn,
				&res_lib_lck_resourceopenasync,
				sizeof (struct res_lib_lck_resourceopenasync));
		} else {
			res_lib_lck_resourceopen.header.size = sizeof (struct res_lib_lck_resourceopen);
			res_lib_lck_resourceopen.header.id = MESSAGE_RES_LCK_RESOURCEOPEN;
			res_lib_lck_resourceopen.header.error = error;
			memcpy (&res_lib_lck_resourceopen.source,
				&req_exec_lck_resourceopen->source,
				sizeof (mar_message_source_t));

			api->ipc_response_send (req_exec_lck_resourceopen->source.conn,
				&res_lib_lck_resourceopen,
				sizeof (struct res_lib_lck_resourceopen));
		}
	}
}

static void message_handler_req_exec_lck_resourceclose (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_lck_resourceclose *req_exec_lck_resourceclose =
		message;
	struct res_lib_lck_resourceclose res_lib_lck_resourceclose;
	struct resource *resource = 0;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saLckResourceClose %s\n",
		get_mar_name_t (&req_exec_lck_resourceclose->lockResourceName));

	resource = lck_resource_find (&resource_list_head,
		&req_exec_lck_resourceclose->lockResourceName);

	if (resource == NULL) {
		goto error_exit;
	}

	/* resource->refcount -= 1; */

	sync_refcount_decrement (resource, nodeid);
	sync_refcount_calculate (resource);

	if (resource->refcount == 0) {
		/* TODO */
	}

error_exit:
	if (api->ipc_source_is_local(&req_exec_lck_resourceclose->source)) {
		lck_resource_cleanup_remove (
			req_exec_lck_resourceclose->source.conn,
			req_exec_lck_resourceclose->resource_handle);

		res_lib_lck_resourceclose.header.size = sizeof (struct res_lib_lck_resourceclose);
		res_lib_lck_resourceclose.header.id = MESSAGE_RES_LCK_RESOURCECLOSE;
		res_lib_lck_resourceclose.header.error = error;

		api->ipc_response_send (
			req_exec_lck_resourceclose->source.conn,
			&res_lib_lck_resourceclose, sizeof (struct res_lib_lck_resourceclose));
	}
}

static void waiter_notification_send (struct resource_lock *resource_lock)
{
	struct res_lib_lck_lockwaitercallback res_lib_lck_lockwaitercallback;

	if (api->ipc_source_is_local (&resource_lock->callback_source) == 0) {
		return;
	}

	res_lib_lck_lockwaitercallback.header.size = sizeof (struct res_lib_lck_lockwaitercallback);
	res_lib_lck_lockwaitercallback.header.id = MESSAGE_RES_LCK_LOCKWAITERCALLBACK;
	res_lib_lck_lockwaitercallback.header.error = SA_AIS_OK;
	res_lib_lck_lockwaitercallback.waiter_signal = resource_lock->waiter_signal;
	res_lib_lck_lockwaitercallback.lock_id = resource_lock->lock_id;
	res_lib_lck_lockwaitercallback.mode_requested = resource_lock->lock_mode;

	if (resource_lock->resource->ex_granted) {
		res_lib_lck_lockwaitercallback.mode_held = SA_LCK_EX_LOCK_MODE;
	} else {
		res_lib_lck_lockwaitercallback.mode_held = SA_LCK_PR_LOCK_MODE;
	}

	api->ipc_dispatch_send (
		resource_lock->callback_source.conn,
		&res_lib_lck_lockwaitercallback,
		sizeof (struct res_lib_lck_lockwaitercallback));
}

static void waiter_notification_list_send (struct list_head *list_notify_head)
{
	struct list_head *list;
	struct resource_lock *resource_lock;

	for (list = list_notify_head->next;
		list != list_notify_head;
		list = list->next) {

		resource_lock = list_entry (list, struct resource_lock, list);
		waiter_notification_send (resource_lock);
	}
}

static void resource_lock_async_deliver (
	const mar_message_source_t *source,
	struct resource_lock *resource_lock,
	SaAisErrorT error)
{
	struct res_lib_lck_resourcelockasync res_lib_lck_resourcelockasync;

	if (source && api->ipc_source_is_local(source)) {
		if (resource_lock->async_call) {
			res_lib_lck_resourcelockasync.header.size = sizeof (struct res_lib_lck_resourcelockasync);
			res_lib_lck_resourcelockasync.header.id = MESSAGE_RES_LCK_RESOURCELOCKASYNC;
			res_lib_lck_resourcelockasync.header.error = error;
			res_lib_lck_resourcelockasync.resource_lock = (void *)resource_lock;
			res_lib_lck_resourcelockasync.lockStatus = resource_lock->lock_status;
			res_lib_lck_resourcelockasync.invocation = resource_lock->invocation;
			res_lib_lck_resourcelockasync.lockId = resource_lock->lock_id;

			api->ipc_dispatch_send (
				source->conn,
				&res_lib_lck_resourcelockasync,
				sizeof (struct res_lib_lck_resourcelockasync));
		}
	}
}

static void lock_response_deliver (
	const mar_message_source_t *source,
	struct resource_lock *resource_lock,
	SaAisErrorT error)
{
	struct res_lib_lck_resourcelock res_lib_lck_resourcelock;

	if (source && api->ipc_source_is_local(source)) {
		if (resource_lock->async_call) {
			resource_lock_async_deliver (&resource_lock->callback_source, resource_lock, error);
		} else {
			res_lib_lck_resourcelock.header.size = sizeof (struct res_lib_lck_resourcelock);
			res_lib_lck_resourcelock.header.id = MESSAGE_RES_LCK_RESOURCELOCK;
			res_lib_lck_resourcelock.header.error = error;
			res_lib_lck_resourcelock.resource_lock = (void *)resource_lock;
			res_lib_lck_resourcelock.lockStatus = resource_lock->lock_status;

			api->ipc_response_send (source->conn,
				&res_lib_lck_resourcelock,
				sizeof (struct res_lib_lck_resourcelock));
		}
	}
}


/*
 * Queue a lock if resource flags allow it
 */
static void lock_queue (
	struct resource *resource,
	struct resource_lock *resource_lock)
{
	if ((resource_lock->lock_flags & SA_LCK_LOCK_NO_QUEUE) == 0) {
		/*
		 * Add lock to the list
		 */
		if (resource_lock->lock_mode == SA_LCK_PR_LOCK_MODE) {
			list_add_tail (&resource_lock->list,
				&resource->pr_pending_list_head);
				waiter_notification_send (resource->ex_granted);
		} else
		if (resource_lock->lock_mode == SA_LCK_EX_LOCK_MODE) {
			list_add_tail (&resource_lock->list,
				&resource->ex_pending_list_head);
				waiter_notification_list_send (&resource->pr_granted_list_head);
		}
	} else {
		resource_lock->lock_status = SA_LCK_LOCK_NOT_QUEUED;
	}
}

/*
The algorithm:

if ex lock granted
	if ex pending list has locks
		send waiter notification to ex lock granted
else
	if ex pending list has locks
		if pr granted list has locks
			send waiter notification to all pr granted locks
		else
			grant ex lock from pending to granted
	else
		grant all pr pending locks to pr granted list
*/
#define SA_LCK_LOCK_NO_STATUS 0
static void lock_algorithm (
	struct resource *resource,
	struct resource_lock *resource_lock)
{
	resource_lock->lock_status = SA_LCK_LOCK_NO_STATUS; /* no status */
	if (resource->ex_granted) {
		/*
		 * Exclusive lock granted
		 */
		lock_queue (resource, resource_lock);
	} else {
		/*
		 * Exclusive lock not granted
		 */
		if (resource_lock->lock_mode == SA_LCK_EX_LOCK_MODE) {
			if (list_empty (&resource->pr_granted_list_head) == 0) {
				lock_queue (resource, resource_lock);
			} else {
				/*
				 * grant ex lock from pending to granted
				 */
				resource->ex_granted = resource_lock;
				resource_lock->lock_status = SA_LCK_LOCK_GRANTED;
			}
		} else {
			/*
			 * grant all pr pending locks to pr granted list
			 */
			/* list_add (&resource_lock->list, &resource->pr_granted_list_head); */
			list_add_tail (&resource_lock->list, &resource->pr_granted_list_head);
			resource_lock->lock_status = SA_LCK_LOCK_GRANTED;
		}
	}
}

/*
 *	if lock in ex, set ex to null
 *	delete resource lock from list
 *
 *	 if ex lock not granted
 *		if ex pending list has locks
 *			grant first ex pending list lock to ex lock
 *	if ex lock not granted
 *		if pr pending list has locks
 *			assign all pr pending locks to pr granted lock list
 */
static void unlock_algorithm (
	struct resource *resource,
	struct resource_lock *resource_lock)
{
	struct resource_lock *resource_lock_grant;
	struct list_head *list;
	struct list_head *list_p;

	/*
	 * If unlocking the ex lock, reset ex granted
	 */
	if (resource_lock == resource->ex_granted) {
		resource->ex_granted = 0;
	}
	else {
		/*
		 * Delete resource lock from whichever list it is on
		 */
		list_del (&resource_lock->list);
	}
	list_del (&resource_lock->resource_list);

	/*
	 * Check if EX locks are available, if so assign one
	 */
	if (resource->ex_granted == 0) {
		if (list_empty (&resource->ex_pending_list_head) == 0) {
			/*
			 * grant first ex pending list lock to ex lock
			 */
			resource_lock_grant = list_entry (
				resource->ex_pending_list_head.next,
				struct resource_lock, list);

			list_del (&resource_lock_grant->list);
			resource->ex_granted = resource_lock_grant;

			resource_lock_grant->lock_status = SA_LCK_LOCK_GRANTED;
			lock_response_deliver (
				&resource_lock_grant->response_source,
				resource_lock_grant,
				SA_AIS_OK);
		}
	}

	/*
	 * Couldn't assign EX lock, so assign any pending PR locks
	 */
	if (resource->ex_granted == 0) {
		if (list_empty (&resource->pr_pending_list_head) == 0) {
			/*
			 * assign all pr pending locks to pr granted lock list
			 */

		   for (list = resource->pr_pending_list_head.next;
				list != &resource->pr_pending_list_head;
				list = list->next) {

				resource_lock_grant = list_entry (list, struct resource_lock, list);
				resource_lock_grant->lock_status = SA_LCK_LOCK_GRANTED;

				lock_response_deliver (
					&resource_lock_grant->response_source,
					resource_lock_grant,
					SA_AIS_OK);
			}

			/*
			 * Add pending locks to granted list
			 */
			list_p = resource->pr_pending_list_head.next;
			list_del (&resource->pr_pending_list_head);
			list_init (&resource->pr_pending_list_head);
			list_add_tail (list_p, &resource->pr_granted_list_head);
		}
	}
}

static void message_handler_req_exec_lck_resourcelock (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_lck_resourcelock *req_exec_lck_resourcelock =
		message;
	struct resource *resource = 0;
	struct resource_lock *resource_lock = 0;
	struct resource_cleanup *resource_cleanup = 0;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saLckResourceLock %s\n",
		get_mar_name_t (&req_exec_lck_resourcelock->req_lib_lck_resourcelock.lockResourceName));

	resource = lck_resource_find (&resource_list_head,
		&req_exec_lck_resourcelock->req_lib_lck_resourcelock.lockResourceName);

	if (resource == NULL) {
		goto error_exit;
	}
	resource->refcount += 1;

	resource_lock = malloc (sizeof (struct resource_lock));
	if (resource_lock == NULL) {
		lock_response_deliver (&req_exec_lck_resourcelock->source,
			resource_lock,
			SA_AIS_ERR_NO_MEMORY);
		goto error_exit;
	}

	memset (resource_lock, 0, sizeof (struct resource_lock));

	list_init (&resource_lock->list);
	list_init (&resource_lock->resource_list);
	/* list_init (&resource_lock->resource_cleanup_list); */

	list_add_tail (&resource_lock->resource_list, &resource->resource_lock_list_head);

	resource_lock->resource = resource;

	resource_lock->lock_mode =
		req_exec_lck_resourcelock->req_lib_lck_resourcelock.lockMode;
	resource_lock->lock_flags =
		req_exec_lck_resourcelock->req_lib_lck_resourcelock.lockFlags;
	resource_lock->waiter_signal =
		req_exec_lck_resourcelock->req_lib_lck_resourcelock.waiterSignal;
	resource_lock->timeout =
		req_exec_lck_resourcelock->req_lib_lck_resourcelock.timeout;
	resource_lock->lock_id =
		req_exec_lck_resourcelock->req_lib_lck_resourcelock.lockId;
	resource_lock->async_call =
		req_exec_lck_resourcelock->req_lib_lck_resourcelock.async_call;
	resource_lock->invocation =
		req_exec_lck_resourcelock->req_lib_lck_resourcelock.invocation;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]:\t lock_id=%u addr=%p\n",
		    (unsigned int)(resource_lock->lock_id),
		    (void *)(resource_lock));

	/*
	 * Waiter callback source
	 */
	memcpy (&resource_lock->callback_source,
		&req_exec_lck_resourcelock->req_lib_lck_resourcelock.source,
		sizeof (mar_message_source_t));

	lock_algorithm (resource, resource_lock);

	/*
	 * Add resource lock to cleanup handler for this api resource instance
	 */
	if (api->ipc_source_is_local (&req_exec_lck_resourcelock->source)) {
		resource_cleanup = lck_resource_cleanup_find (
			resource_lock->callback_source.conn,
			req_exec_lck_resourcelock->resource_handle);

		assert (resource_cleanup != NULL);

		/* list_add (&resource_lock->resource_cleanup_list,
		   &resource_cleanup->resource_lock_list_head); */
		/* list_add_tail (&resource_lock->resource_cleanup_list,
		   &resource_cleanup->resource_lock_list_head); */

		/*
		 * If lock queued by lock algorithm, dont send response to library now
		 */
		if (resource_lock->lock_status != SA_LCK_LOCK_NO_STATUS) {
			/*
			 * If lock granted or denied, deliver callback or
			 * response to library for non-async calls
			 */
			lock_response_deliver (
				&req_exec_lck_resourcelock->source,
				resource_lock,
				SA_AIS_OK);
		} else {
			memcpy (&resource_lock->response_source,
				&req_exec_lck_resourcelock->source,
				sizeof (mar_message_source_t));
		}

		/*
		 * Deliver async response to library
		 */
		req_exec_lck_resourcelock->source.conn =
			req_exec_lck_resourcelock->source.conn;
		resource_lock_async_deliver (
			&req_exec_lck_resourcelock->source,
			resource_lock,
			SA_AIS_OK);
	}

error_exit:
	return;
}

static void message_handler_req_exec_lck_resourceunlock (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_lck_resourceunlock *req_exec_lck_resourceunlock =
		message;
	struct res_lib_lck_resourceunlock res_lib_lck_resourceunlock;
	struct res_lib_lck_resourceunlockasync res_lib_lck_resourceunlockasync;
	struct resource *resource = NULL;
	struct resource_lock *resource_lock = NULL;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: saLckResourceUnlock %s\n",
		get_mar_name_t (&req_exec_lck_resourceunlock->resource_name));

	resource = lck_resource_find (&resource_list_head,
		&req_exec_lck_resourceunlock->resource_name);

	if (resource == NULL) {
		goto error_exit;
 	}

	resource->refcount -= 1;

	resource_lock = lck_resource_lock_find (resource,
		&req_exec_lck_resourceunlock->source,
		req_exec_lck_resourceunlock->lock_id);

	assert (resource_lock != NULL);

	/* list_del (&resource_lock->resource_cleanup_list); */

	unlock_algorithm (resource, resource_lock);

error_exit:
	if (api->ipc_source_is_local(&req_exec_lck_resourceunlock->source)) {
		if (req_exec_lck_resourceunlock->async_call) {
			res_lib_lck_resourceunlockasync.header.size = sizeof (struct res_lib_lck_resourceunlockasync);
			res_lib_lck_resourceunlockasync.header.id = MESSAGE_RES_LCK_RESOURCEUNLOCKASYNC;
			res_lib_lck_resourceunlockasync.header.error = error;
			res_lib_lck_resourceunlockasync.invocation =
				req_exec_lck_resourceunlock->invocation;
			res_lib_lck_resourceunlockasync.lockId = req_exec_lck_resourceunlock->lock_id;

			api->ipc_response_send (
				req_exec_lck_resourceunlock->source.conn,
				&res_lib_lck_resourceunlockasync,
				sizeof (struct res_lib_lck_resourceunlockasync));

			api->ipc_dispatch_send (
				req_exec_lck_resourceunlock->source.conn,
				&res_lib_lck_resourceunlockasync,
				sizeof (struct res_lib_lck_resourceunlockasync));
		} else {
			res_lib_lck_resourceunlock.header.size = sizeof (struct res_lib_lck_resourceunlock);
			res_lib_lck_resourceunlock.header.id = MESSAGE_RES_LCK_RESOURCEUNLOCK;
			res_lib_lck_resourceunlock.header.error = error;
			api->ipc_response_send (req_exec_lck_resourceunlock->source.conn,
				&res_lib_lck_resourceunlock, sizeof (struct res_lib_lck_resourceunlock));
		}
	}
}

static void message_handler_req_exec_lck_resourcelockorphan (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_lck_resourcelockorphan *req_exec_lck_resourcelockorphan =
		message;
	struct resource *resource = 0;
	struct resource_lock *resource_lock = 0;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: orphan locks for resource %s\n",
		get_mar_name_t (&req_exec_lck_resourcelockorphan->resource_name));

	resource = lck_resource_find (&resource_list_head,
		&req_exec_lck_resourcelockorphan->resource_name);

	assert (resource != NULL);

	resource->refcount -= 1;

	resource_lock = lck_resource_lock_find (resource,
		&req_exec_lck_resourcelockorphan->source,
		req_exec_lck_resourcelockorphan->lock_id);

	assert (resource_lock != NULL);

	/* list_del (&resource_lock->resource_cleanup_list); */

	unlock_algorithm (resource, resource_lock);
}

static void message_handler_req_exec_lck_lockpurge (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_lck_lockpurge *req_exec_lck_lockpurge =
		msg;
	struct res_lib_lck_lockpurge res_lib_lck_lockpurge;
	struct resource *resource = 0;
	SaAisErrorT error = SA_AIS_OK;

	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saLckLockPurge %s\n",
		get_mar_name_t (&req_exec_lck_lockpurge->req_lib_lck_lockpurge.lockResourceName));

	resource = lck_resource_find (&resource_list_head,
		&req_exec_lck_lockpurge->req_lib_lck_lockpurge.lockResourceName);

	if (resource == NULL) {
		goto error_exit;
	}

error_exit:
	if (api->ipc_source_is_local(&req_exec_lck_lockpurge->source)) {
//		lck_resource_cleanup_remove (req_exec_lck_lockpurge->source.conn,
//			resource);

		res_lib_lck_lockpurge.header.size = sizeof (struct res_lib_lck_lockpurge);
		res_lib_lck_lockpurge.header.id = MESSAGE_RES_LCK_LOCKPURGE;
		res_lib_lck_lockpurge.header.error = error;

		api->ipc_response_send (req_exec_lck_lockpurge->source.conn,
			&res_lib_lck_lockpurge, sizeof (struct res_lib_lck_lockpurge));
	}
}

static void message_handler_req_exec_lck_sync_resource (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_lck_sync_resource *req_exec_lck_sync_resource =
		msg;
	struct resource *resource;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: sync resource %s\n",
		    get_mar_name_t (&req_exec_lck_sync_resource->resource_name));

	/*
	 * Ignore message from previous ring
	 */
	if (memcmp (&req_exec_lck_sync_resource->ring_id,
		    &my_saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	resource = lck_resource_find (&sync_resource_list_head,
		&req_exec_lck_sync_resource->resource_name);

	if (resource == NULL) {
		resource = malloc (sizeof (struct resource));
		if (resource == NULL) {
			api->error_memory_failure ();
		}

		memset (resource, 0, sizeof (struct resource));
		memcpy (&resource->name,
			&req_exec_lck_sync_resource->resource_name,
			sizeof (mar_name_t));

		list_init (&resource->list);
		list_init (&resource->resource_lock_list_head);
		/* list_add  (&resource->list, &sync_resource_list_head); */
		list_add_tail  (&resource->list, &sync_resource_list_head);
		list_init (&resource->pr_granted_list_head);
		list_init (&resource->pr_pending_list_head);
		list_init (&resource->ex_pending_list_head);
	}
}

static void message_handler_req_exec_lck_sync_resource_lock (
	const void *msg,
	unsigned int nodeid)
{
	const struct req_exec_lck_sync_resource_lock *req_exec_lck_sync_resource_lock =
		msg;
	struct resource_lock *resource_lock;
	struct resource *resource;

	log_printf (LOGSYS_LEVEL_NOTICE, "EXEC request: sync resource lock %u\n",
		    (unsigned int)(req_exec_lck_sync_resource_lock->lock_id));

	/*
	 * Ignore message from previous ring
	 */
	if (memcmp (&req_exec_lck_sync_resource_lock->ring_id,
		    &my_saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	resource = lck_resource_find (&sync_resource_list_head,
		&req_exec_lck_sync_resource_lock->resource_name);

	assert (resource != NULL);

	/* FIXME: check to make sure the lock doesn't already exist */

	resource_lock = malloc (sizeof (struct resource_lock));
	if (resource_lock == NULL) {
		api->error_memory_failure ();
	}
	memset (resource_lock, 0, sizeof (struct resource_lock));

	list_init (&resource_lock->list);
	list_init (&resource_lock->resource_list);
	/* list_init (&resource_lock->resource_cleanup_list); */

	list_add_tail (&resource_lock->resource_list, &resource->resource_lock_list_head);

	resource_lock->lock_id = req_exec_lck_sync_resource_lock->lock_id;
	resource_lock->lock_mode = req_exec_lck_sync_resource_lock->lock_mode;
	resource_lock->lock_flags = req_exec_lck_sync_resource_lock->lock_flags;
	resource_lock->waiter_signal = req_exec_lck_sync_resource_lock->waiter_signal;
	resource_lock->lock_status = req_exec_lck_sync_resource_lock->lock_status;
	resource_lock->timeout = req_exec_lck_sync_resource_lock->timeout;
	resource_lock->resource = resource;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_NOTICE, "[DEBUG]: lock_id=%u lock_mode=%u lock_status=%u\n",
		    (unsigned int)(resource_lock->lock_id),
		    (unsigned int)(resource_lock->lock_mode),
		    (unsigned int)(resource_lock->lock_status));

	/*
	 * Determine the list that this lock should be added to
	 */
	if (resource_lock->lock_mode == SA_LCK_PR_LOCK_MODE) {
		if (resource_lock->lock_status == SA_LCK_LOCK_GRANTED) {
			list_add_tail (&resource_lock->list, &resource->pr_granted_list_head);
		} else {
			list_add_tail (&resource_lock->list, &resource->pr_pending_list_head);
		}
	}

	if (resource_lock->lock_mode == SA_LCK_EX_LOCK_MODE) {
		if (resource_lock->lock_status == SA_LCK_LOCK_GRANTED) {
			resource->ex_granted = resource_lock;
		} else {
			list_add_tail (&resource_lock->list, &resource->ex_pending_list_head);
		}
	}
}

static void message_handler_req_exec_lck_sync_resource_refcount (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_lck_sync_resource_refcount *req_exec_lck_sync_resource_refcount
		= message;
	struct resource *resource;
	unsigned int i, j;

	/*
	 * Ignore messages from previous ring
	 */
	if (memcmp (&req_exec_lck_sync_resource_refcount->ring_id,
		    &my_saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	resource = lck_resource_find (&sync_resource_list_head,
		&req_exec_lck_sync_resource_refcount->resource_name);

	assert (resource != NULL);

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		if (req_exec_lck_sync_resource_refcount->refcount_set[i].nodeid == 0) {
			break;
		}
		for (j = 0; j < PROCESSOR_COUNT_MAX; j++) {
			if (resource->refcount_set[j].nodeid == 0) {
				resource->refcount_set[j].nodeid =
					req_exec_lck_sync_resource_refcount->refcount_set[i].nodeid;
				resource->refcount_set[j].refcount =
					req_exec_lck_sync_resource_refcount->refcount_set[i].refcount;
				break;
			}
			if (req_exec_lck_sync_resource_refcount->refcount_set[i].nodeid == resource->refcount_set[j].nodeid) {
				resource->refcount_set[j].refcount +=
					req_exec_lck_sync_resource_refcount->refcount_set[i].refcount;
				break;
			}
		}
	}

	sync_refcount_calculate (resource);

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: sync refcount for resource %s is %u\n",
		    get_mar_name_t (&resource->name), (unsigned int)(resource->refcount));
}

static void message_handler_req_lib_lck_resourceopen (
	void *conn,
	const void *msg)
{
	struct req_lib_lck_resourceopen *req_lib_lck_resourceopen
		= (struct req_lib_lck_resourceopen *)msg;
	struct req_exec_lck_resourceopen req_exec_lck_resourceopen;
	struct iovec iovec;

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saLckResourceOpen %s\n",
		get_mar_name_t (&req_lib_lck_resourceopen->lockResourceName));

	req_exec_lck_resourceopen.header.size =
		sizeof (struct req_exec_lck_resourceopen);
	req_exec_lck_resourceopen.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCEOPEN);

	api->ipc_source_set (&req_exec_lck_resourceopen.source, conn);

	memcpy (&req_exec_lck_resourceopen.resource_name,
		&req_lib_lck_resourceopen->lockResourceName,
		sizeof (SaNameT));

	req_exec_lck_resourceopen.open_flags = req_lib_lck_resourceopen->resourceOpenFlags;
	req_exec_lck_resourceopen.async_call = 0;
	req_exec_lck_resourceopen.invocation = 0;
	req_exec_lck_resourceopen.resource_handle = req_lib_lck_resourceopen->resourceHandle;
	req_exec_lck_resourceopen.fail_with_error = SA_AIS_OK;

	iovec.iov_base = (char *)&req_exec_lck_resourceopen;
	iovec.iov_len = sizeof (req_exec_lck_resourceopen);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_lck_resourceopenasync (
	void *conn,
	const void *msg)
{
	struct req_lib_lck_resourceopen *req_lib_lck_resourceopen
		= (struct req_lib_lck_resourceopen *)msg;
	struct req_exec_lck_resourceopen req_exec_lck_resourceopen;
	struct iovec iovec;

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saLckResourceOpenAsync %s\n",
		get_mar_name_t (&req_lib_lck_resourceopen->lockResourceName));

	req_exec_lck_resourceopen.header.size =
		sizeof (struct req_exec_lck_resourceopen);
	req_exec_lck_resourceopen.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCEOPEN);

	api->ipc_source_set (&req_exec_lck_resourceopen.source, conn);

	memcpy (&req_exec_lck_resourceopen.resource_name,
		&req_lib_lck_resourceopen->lockResourceName,
		sizeof (mar_name_t));

	req_exec_lck_resourceopen.resource_handle = req_lib_lck_resourceopen->resourceHandle;
	req_exec_lck_resourceopen.invocation = req_lib_lck_resourceopen->invocation;
	req_exec_lck_resourceopen.open_flags = req_lib_lck_resourceopen->resourceOpenFlags;
	req_exec_lck_resourceopen.fail_with_error = SA_AIS_OK;
	req_exec_lck_resourceopen.timeout = 0;
	req_exec_lck_resourceopen.async_call = 1;

	iovec.iov_base = (char *)&req_exec_lck_resourceopen;
	iovec.iov_len = sizeof (req_exec_lck_resourceopen);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_lck_resourceclose (
	void *conn,
	const void *msg)
{
	struct req_lib_lck_resourceclose *req_lib_lck_resourceclose
		= (struct req_lib_lck_resourceclose *)msg;
	struct req_exec_lck_resourceclose req_exec_lck_resourceclose;
	struct iovec iovecs[2];
	struct resource *resource;
	struct res_lib_lck_resourceclose res_lib_lck_resourceclose;

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saLckResourceClose %s\n",
		get_mar_name_t (&req_lib_lck_resourceclose->lockResourceName));

	resource = lck_resource_find (&resource_list_head,
		&req_lib_lck_resourceclose->lockResourceName);

	if (resource != NULL) {
		req_exec_lck_resourceclose.header.size =
			sizeof (struct req_exec_lck_resourceclose);
		req_exec_lck_resourceclose.header.id =
			SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCECLOSE);

		api->ipc_source_set (&req_exec_lck_resourceclose.source, conn);

		memcpy (&req_exec_lck_resourceclose.lockResourceName,
			&req_lib_lck_resourceclose->lockResourceName, sizeof (mar_name_t));

		req_exec_lck_resourceclose.resource_handle = req_lib_lck_resourceclose->resourceHandle;
		iovecs[0].iov_base = (char *)&req_exec_lck_resourceclose;
		iovecs[0].iov_len = sizeof (req_exec_lck_resourceclose);

		assert (api->totem_mcast (iovecs, 1, TOTEM_AGREED) == 0);
	}
	else {
		res_lib_lck_resourceclose.header.size = sizeof (struct res_lib_lck_resourceclose);
		res_lib_lck_resourceclose.header.id = MESSAGE_RES_LCK_RESOURCECLOSE;
		res_lib_lck_resourceclose.header.error = SA_AIS_ERR_NOT_EXIST;

		api->ipc_response_send (conn,
			&res_lib_lck_resourceclose,
			sizeof (struct res_lib_lck_resourceclose));
	}
}

static void message_handler_req_lib_lck_resourcelock (
	void *conn,
	const void *msg)
{
	struct req_lib_lck_resourcelock *req_lib_lck_resourcelock
		= (struct req_lib_lck_resourcelock *)msg;
	struct req_exec_lck_resourcelock req_exec_lck_resourcelock;
	struct iovec iovecs[2];

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saLckResourceLock %s\n",
		get_mar_name_t (&req_lib_lck_resourcelock->lockResourceName));

	req_exec_lck_resourcelock.header.size =
		sizeof (struct req_exec_lck_resourcelock);
	req_exec_lck_resourcelock.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCELOCK);

	api->ipc_source_set (&req_exec_lck_resourcelock.source, conn);

	memcpy (&req_exec_lck_resourcelock.req_lib_lck_resourcelock,
		req_lib_lck_resourcelock,
		sizeof (struct req_lib_lck_resourcelock));

	req_exec_lck_resourcelock.resource_handle = req_lib_lck_resourcelock->resourceHandle;
	req_exec_lck_resourcelock.async_call = 0;
	req_exec_lck_resourcelock.invocation = 0;
	req_exec_lck_resourcelock.fail_with_error = SA_AIS_OK;

	iovecs[0].iov_base = (char *)&req_exec_lck_resourcelock;
	iovecs[0].iov_len = sizeof (req_exec_lck_resourcelock);

	assert (api->totem_mcast (iovecs, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_lck_resourcelockasync (
	void *conn,
	const void *msg)
{
	struct req_lib_lck_resourcelock *req_lib_lck_resourcelock
		= (struct req_lib_lck_resourcelock *)msg;
	struct req_exec_lck_resourcelock req_exec_lck_resourcelock;
	struct iovec iovecs[2];

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saLckResourceLockAsync %s\n",
		get_mar_name_t (&req_lib_lck_resourcelock->lockResourceName));

	req_exec_lck_resourcelock.header.size =
		sizeof (struct req_exec_lck_resourcelock);
	req_exec_lck_resourcelock.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCELOCK);

	api->ipc_source_set (&req_exec_lck_resourcelock.source, conn);

	memcpy (&req_exec_lck_resourcelock.req_lib_lck_resourcelock,
		req_lib_lck_resourcelock,
		sizeof (struct req_lib_lck_resourcelock));

	req_exec_lck_resourcelock.resource_handle = req_lib_lck_resourcelock->resourceHandle;
	req_exec_lck_resourcelock.async_call = 1;
	req_exec_lck_resourcelock.invocation = req_lib_lck_resourcelock->invocation;
	req_exec_lck_resourcelock.fail_with_error = SA_AIS_OK;

	iovecs[0].iov_base = (char *)&req_exec_lck_resourcelock;
	iovecs[0].iov_len = sizeof (req_exec_lck_resourcelock);

	assert (api->totem_mcast (iovecs, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_lck_resourceunlock (
	void *conn,
	const void *msg)
{
	struct req_lib_lck_resourceunlock *req_lib_lck_resourceunlock
		= (struct req_lib_lck_resourceunlock *)msg;
	struct req_exec_lck_resourceunlock req_exec_lck_resourceunlock;
	struct iovec iovec;

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saLckResourceUnlock %s\n",
		get_mar_name_t (&req_lib_lck_resourceunlock->lockResourceName));

	req_exec_lck_resourceunlock.header.size =
		sizeof (struct req_exec_lck_resourceunlock);
	req_exec_lck_resourceunlock.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCEUNLOCK);

	api->ipc_source_set (&req_exec_lck_resourceunlock.source, conn);

	memcpy (&req_exec_lck_resourceunlock.resource_name,
		&req_lib_lck_resourceunlock->lockResourceName,
		sizeof (mar_name_t));

	req_exec_lck_resourceunlock.lock_id = req_lib_lck_resourceunlock->lockId;
	req_exec_lck_resourceunlock.async_call = 0;
	req_exec_lck_resourceunlock.invocation = 0;

	iovec.iov_base = (char *)&req_exec_lck_resourceunlock;
	iovec.iov_len = sizeof (req_exec_lck_resourceunlock);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_lck_resourceunlockasync (
	void *conn,
	const void *msg)
{
	struct req_lib_lck_resourceunlock *req_lib_lck_resourceunlock
		= (struct req_lib_lck_resourceunlock *)msg;
	struct req_exec_lck_resourceunlock req_exec_lck_resourceunlock;
	struct iovec iovec;

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saLckResourceUnlockAsync %s\n",
		get_mar_name_t (&req_lib_lck_resourceunlock->lockResourceName));

	req_exec_lck_resourceunlock.header.size =
		sizeof (struct req_exec_lck_resourceunlock);
	req_exec_lck_resourceunlock.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCEUNLOCK);

	api->ipc_source_set (&req_exec_lck_resourceunlock.source, conn);

	memcpy (&req_exec_lck_resourceunlock.resource_name,
		&req_lib_lck_resourceunlock->lockResourceName,
		sizeof (mar_name_t));

	req_exec_lck_resourceunlock.lock_id = req_lib_lck_resourceunlock->lockId;
	req_exec_lck_resourceunlock.invocation = req_lib_lck_resourceunlock->invocation;
	req_exec_lck_resourceunlock.async_call = 1;

	iovec.iov_base = (char *)&req_exec_lck_resourceunlock;
	iovec.iov_len = sizeof (req_exec_lck_resourceunlock);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_lck_lockpurge (
	void *conn,
	const void *msg)
{
	struct req_lib_lck_lockpurge *req_lib_lck_lockpurge
		= (struct req_lib_lck_lockpurge *)msg;
	struct req_exec_lck_lockpurge req_exec_lck_lockpurge;
	struct iovec iovecs[2];

	log_printf (LOGSYS_LEVEL_NOTICE, "LIB request: saLckResourceLockPurge %s\n",
		get_mar_name_t (&req_lib_lck_lockpurge->lockResourceName));

	req_exec_lck_lockpurge.header.size =
		sizeof (struct req_exec_lck_lockpurge);
	req_exec_lck_lockpurge.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_LOCKPURGE);

	api->ipc_source_set (&req_exec_lck_lockpurge.source, conn);

	memcpy (&req_exec_lck_lockpurge.req_lib_lck_lockpurge,
		req_lib_lck_lockpurge,
		sizeof (struct req_lib_lck_lockpurge));

	iovecs[0].iov_base = (char *)&req_exec_lck_lockpurge;
	iovecs[0].iov_len = sizeof (req_exec_lck_lockpurge);

	assert (api->totem_mcast (iovecs, 1, TOTEM_AGREED) == 0);
}
