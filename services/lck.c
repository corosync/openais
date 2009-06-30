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
#include "../include/saLck.h"
#include "../include/ipc_lck.h"

/*If you want compile useful debug functions, uncomment next line*/
/*#define _LCK_DEBUG_*/

LOGSYS_DECLARE_SUBSYS ("LCK");

enum lck_message_req_types {
	MESSAGE_REQ_EXEC_LCK_RESOURCEOPEN = 0,
	MESSAGE_REQ_EXEC_LCK_RESOURCEOPENASYNC = 1,
	MESSAGE_REQ_EXEC_LCK_RESOURCECLOSE = 2,
	MESSAGE_REQ_EXEC_LCK_RESOURCELOCK = 3,
	MESSAGE_REQ_EXEC_LCK_RESOURCELOCKASYNC = 4,
	MESSAGE_REQ_EXEC_LCK_RESOURCEUNLOCK = 5,
	MESSAGE_REQ_EXEC_LCK_RESOURCEUNLOCKASYNC = 6,
	MESSAGE_REQ_EXEC_LCK_LOCKPURGE = 7,
	MESSAGE_REQ_EXEC_LCK_LIMITGET = 8,
	MESSAGE_REQ_EXEC_LCK_RESOURCELOCK_TIMEOUT = 9,
	MESSAGE_REQ_EXEC_LCK_SYNC_RESOURCE = 10,
	MESSAGE_REQ_EXEC_LCK_SYNC_RESOURCE_LOCK = 11,
	MESSAGE_REQ_EXEC_LCK_SYNC_RESOURCE_REFCOUNT = 12,
};

enum lck_sync_state {
	LCK_SYNC_STATE_NOT_STARTED,
	LCK_SYNC_STATE_STARTED,
	LCK_SYNC_STATE_RESOURCE,
};

enum lck_sync_iteration_state {
	LCK_SYNC_ITERATION_STATE_RESOURCE,
	LCK_SYNC_ITERATION_STATE_RESOURCE_LOCK,
	LCK_SYNC_ITERATION_STATE_RESOURCE_REFCOUNT,
};

struct refcount_set {
	unsigned int refcount;
	unsigned int nodeid;
};

typedef struct {
	unsigned int refcount __attribute__((aligned(8)));
	unsigned int nodeid __attribute__((aligned(8)));
} mar_refcount_set_t;

struct resource {
	mar_name_t resource_name;
	mar_uint32_t refcount;
	struct refcount_set refcount_set[PROCESSOR_COUNT_MAX];
	struct resource_lock *ex_lock_granted;
	struct list_head resource_lock_list_head;
	struct list_head pr_lock_granted_list_head;
	struct list_head pr_lock_pending_list_head;
	struct list_head ex_lock_pending_list_head;
	struct list_head resource_list;
	mar_message_source_t source;
};

struct resource_lock {
	mar_uint64_t lock_id;
	mar_uint32_t lock_mode;
	mar_uint32_t lock_flags;
	mar_uint32_t lock_status;
	mar_uint64_t waiter_signal;
	mar_uint64_t resource_handle;
	mar_invocation_t invocation;
	mar_uint8_t orphan_flag;
	mar_time_t timeout;
	struct resource *resource;
	struct list_head resource_lock_list;
	struct list_head list;
	mar_message_source_t response_source;
	mar_message_source_t callback_source;
	corosync_timer_handle_t timer_handle;
};

struct resource_cleanup {
	mar_name_t resource_name;
	hdb_handle_t resource_id;
	struct list_head cleanup_list;
};

struct resource_instance {
	mar_message_source_t source;
};

unsigned int global_lock_count = 0;
unsigned int sync_lock_count = 0;

/*
 * Define the limits for the lock service.
 * These limits are implementation specific and
 * can be obtained via the library call saLckLimitGet
 * by passing the appropriate limitId (see saLck.h).
 */
#define MAX_NUM_LOCKS 256

DECLARE_HDB_DATABASE (resource_hdb, NULL);

DECLARE_LIST_INIT(resource_list_head);

DECLARE_LIST_INIT(sync_resource_list_head);

static struct corosync_api_v1 *api;

static void lck_exec_dump_fn (void);

static int lck_exec_init_fn (struct corosync_api_v1 *);
static int lck_lib_init_fn (void *conn);
static int lck_lib_exit_fn (void *conn);

static void message_handler_req_exec_lck_resourceopen (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_resourceopenasync (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_resourceclose (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_resourcelock (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_resourcelockasync (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_resourceunlock (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_resourceunlockasync (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_lockpurge (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_limitget (
	const void *message,
	unsigned int nodeid);

static void message_handler_req_exec_lck_resourcelock_timeout (
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

static void message_handler_req_lib_lck_limitget (
	void *conn,
	const void *msg);

static void exec_lck_resourceopen_endian_convert (void *msg);
static void exec_lck_resourceopenasync_endian_convert (void *msg);
static void exec_lck_resourceclose_endian_convert (void *msg);
static void exec_lck_resourcelock_endian_convert (void *msg);
static void exec_lck_resourcelockasync_endian_convert (void *msg);
static void exec_lck_resourceunlock_endian_convert (void *msg);
static void exec_lck_resourceunlockasync_endian_convert (void *msg);
static void exec_lck_lockpurge_endian_convert (void *msg);
static void exec_lck_limitget_endian_convert (void *msg);
static void exec_lck_resourcelock_timeout_endian_convert (void *msg);
static void exec_lck_sync_resource_endian_convert (void *msg);
static void exec_lck_sync_resource_lock_endian_convert (void *msg);
static void exec_lck_sync_resource_refcount_endian_convert (void *msg);

static void lck_sync_init (
	const unsigned int *member_list,
	size_t member_list_entries,
	const struct memb_ring_id *ring_id);
static int  lck_sync_process (void);
static void lck_sync_activate (void);
static void lck_sync_abort (void);

static enum lck_sync_state lck_sync_state = LCK_SYNC_STATE_NOT_STARTED;
static enum lck_sync_iteration_state lck_sync_iteration_state;

static struct list_head *lck_sync_iteration_resource;
static struct list_head *lck_sync_iteration_resource_lock;

static void lck_sync_refcount_increment (
	struct resource *resource, unsigned int nodeid);
static void lck_sync_refcount_decrement (
	struct resource *resource, unsigned int nodeid);
static void lck_sync_refcount_calculate (
	struct resource *resource);

static unsigned int lck_member_list[PROCESSOR_COUNT_MAX];
static unsigned int lck_member_list_entries = 0;
static unsigned int lowest_nodeid = 0;

static struct memb_ring_id saved_ring_id;

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

struct corosync_lib_handler lck_lib_engine[] =
{
	{
		.lib_handler_fn		= message_handler_req_lib_lck_resourceopen,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_lck_resourceopenasync,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_lck_resourceclose,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_lck_resourcelock,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_lck_resourcelockasync,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_lck_resourceunlock,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_lck_resourceunlockasync,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
		{
		.lib_handler_fn		= message_handler_req_lib_lck_lockpurge,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_lck_limitget,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
};

static struct corosync_exec_handler lck_exec_engine[] =
{
	{
		.exec_handler_fn	= message_handler_req_exec_lck_resourceopen,
		.exec_endian_convert_fn = exec_lck_resourceopen_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_resourceopenasync,
		.exec_endian_convert_fn = exec_lck_resourceopenasync_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_resourceclose,
		.exec_endian_convert_fn = exec_lck_resourceclose_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_resourcelock,
		.exec_endian_convert_fn = exec_lck_resourcelock_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_resourcelockasync,
		.exec_endian_convert_fn = exec_lck_resourcelockasync_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_resourceunlock,
		.exec_endian_convert_fn = exec_lck_resourceunlock_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_resourceunlockasync,
		.exec_endian_convert_fn = exec_lck_resourceunlockasync_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_lockpurge,
		.exec_endian_convert_fn = exec_lck_lockpurge_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_limitget,
		.exec_endian_convert_fn = exec_lck_limitget_endian_convert
	},
	{
		.exec_handler_fn	= message_handler_req_exec_lck_resourcelock_timeout,
		.exec_endian_convert_fn = exec_lck_resourcelock_timeout_endian_convert
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
	.name				= "openais distributed locking service B.03.01",
	.id				= LCK_SERVICE,
	.private_data_size		= sizeof (struct lck_pd),
	.flow_control			= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED,
	.lib_init_fn			= lck_lib_init_fn,
	.lib_exit_fn			= lck_lib_exit_fn,
	.lib_engine			= lck_lib_engine,
	.lib_engine_count		= sizeof (lck_lib_engine) / sizeof (struct corosync_lib_handler),
	.exec_init_fn			= lck_exec_init_fn,
	.exec_dump_fn			= lck_exec_dump_fn,
	.exec_engine			= lck_exec_engine,
	.exec_engine_count		= sizeof (lck_exec_engine) / sizeof (struct corosync_exec_handler),
	.confchg_fn			= lck_confchg_fn,
	.sync_mode			= CS_SYNC_V2,
	.sync_init			= lck_sync_init,
	.sync_process			= lck_sync_process,
	.sync_activate			= lck_sync_activate,
	.sync_abort			= lck_sync_abort,
};

static struct corosync_service_engine *lck_get_engine_ver0 (void);

static struct corosync_service_engine_iface_ver0 lck_service_engine_iface = {
	.corosync_get_service_engine_ver0 = lck_get_engine_ver0
};

static struct lcr_iface openais_lck_ver0[1] = {
	{
		.name			= "openais_lck",
		.version		= 0,
		.versions_replace	= 0,
		.versions_replace_count	= 0,
		.dependencies		= 0,
		.dependency_count	= 0,
		.constructor		= NULL,
		.destructor		= NULL,
		.interfaces		= (void **)(void *)&lck_service_engine_iface,
	}
};

static struct lcr_comp lck_comp_ver0 = {
	.iface_count	= 1,
	.ifaces		= openais_lck_ver0
};

static struct corosync_service_engine *lck_get_engine_ver0 (void)
{
	return (&lck_service_engine);
}

#ifdef OPENAIS_SOLARIS
void corosync_lcr_component_register (void);

void corosync_lcr_component_register (void)
{
#else
__attribute__ ((constructor)) static void corosync_lcr_component_register (void)
{
#endif
	lcr_interfaces_set (&openais_lck_ver0[0], &lck_service_engine_iface);
	lcr_component_register (&lck_comp_ver0);
}

struct req_exec_lck_resourceopen {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
	mar_uint32_t open_flags __attribute__((aligned(8)));
	mar_uint64_t resource_handle __attribute__((aligned(8)));
};

struct req_exec_lck_resourceopenasync {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
	mar_uint32_t open_flags __attribute__((aligned(8)));
	mar_uint64_t resource_handle __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
};

struct req_exec_lck_resourceclose {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
	hdb_handle_t resource_id __attribute__((aligned(8)));
	mar_uint8_t exit_flag __attribute__((aligned(8)));
};

struct req_exec_lck_resourcelock {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
	mar_uint64_t lock_id __attribute__((aligned(8)));
	mar_uint32_t lock_mode __attribute__((aligned(8)));
	mar_uint32_t lock_flags __attribute__((aligned(8)));
	mar_uint64_t waiter_signal __attribute__((aligned(8)));
	mar_uint64_t resource_handle __attribute__((aligned(8)));
	mar_message_source_t callback_source __attribute__((aligned(8)));
	mar_time_t timeout __attribute__((aligned(8)));
};

struct req_exec_lck_resourcelockasync {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
	mar_uint64_t lock_id __attribute__((aligned(8)));
	mar_uint32_t lock_mode __attribute__((aligned(8)));
	mar_uint32_t lock_flags __attribute__((aligned(8)));
	mar_uint64_t waiter_signal __attribute__((aligned(8)));
	mar_uint64_t resource_handle __attribute__((aligned(8)));
	mar_message_source_t callback_source __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
};

struct req_exec_lck_resourceunlock {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_uint64_t resource_handle __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
	mar_uint64_t lock_id __attribute__((aligned(8)));
};

struct req_exec_lck_resourceunlockasync {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_uint64_t resource_handle __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
	mar_uint64_t lock_id __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
};

struct req_exec_lck_lockpurge {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
};

struct req_exec_lck_limitget {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_uint64_t limit_id __attribute__((aligned(8)));
};

struct req_exec_lck_resourcelock_timeout {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
	mar_uint64_t lock_id __attribute__((aligned(8)));
	mar_message_source_t response_source __attribute__((aligned(8)));
	mar_message_source_t callback_source __attribute__((aligned(8)));
};

struct req_exec_lck_sync_resource {
	coroipc_request_header_t header __attribute__((aligned(8)));
	struct memb_ring_id ring_id __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
};

struct req_exec_lck_sync_resource_lock {
	coroipc_request_header_t header __attribute__((aligned(8)));
	struct memb_ring_id ring_id __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
	mar_uint64_t lock_id __attribute__((aligned(8)));
	mar_uint32_t lock_mode __attribute__((aligned(8)));
	mar_uint32_t lock_flags __attribute__((aligned(8)));
	mar_uint32_t lock_status __attribute__((aligned(8)));
	mar_uint64_t waiter_signal __attribute__((aligned(8)));
	mar_uint64_t resource_handle __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
	mar_uint8_t orphan_flag __attribute__((aligned(8)));
	mar_time_t timeout __attribute__((aligned(8)));
	mar_message_source_t response_source __attribute__((aligned(8)));
	mar_message_source_t callback_source __attribute__((aligned(8)));
};

struct req_exec_lck_sync_resource_refcount {
	coroipc_request_header_t header __attribute__((aligned(8)));
	struct memb_ring_id ring_id __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
	mar_refcount_set_t refcount_set[PROCESSOR_COUNT_MAX] __attribute__((aligned(8)));
};

static void exec_lck_resourceopen_endian_convert (void *msg)
{
	struct req_exec_lck_resourceopen *to_swab =
		(struct req_exec_lck_resourceopen *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->resource_name);
	swab_mar_uint32_t (&to_swab->open_flags);
	swab_mar_uint64_t (&to_swab->resource_handle);

	return;
}

static void exec_lck_resourceopenasync_endian_convert (void *msg)
{
	struct req_exec_lck_resourceopenasync *to_swab =
		(struct req_exec_lck_resourceopenasync *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->resource_name);
	swab_mar_uint32_t (&to_swab->open_flags);
	swab_mar_uint64_t (&to_swab->resource_handle);
	swab_mar_invocation_t (&to_swab->invocation);

	return;
}

static void exec_lck_resourceclose_endian_convert (void *msg)
{
	struct req_exec_lck_resourceclose *to_swab =
		(struct req_exec_lck_resourceclose *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->resource_name);
	swab_mar_uint64_t (&to_swab->resource_id);
	swab_mar_uint8_t (&to_swab->exit_flag);

	return;
}

static void exec_lck_resourcelock_endian_convert (void *msg)
{
	struct req_exec_lck_resourcelock *to_swab =
		(struct req_exec_lck_resourcelock *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->resource_name);
	swab_mar_uint64_t (&to_swab->lock_id);
	swab_mar_uint32_t (&to_swab->lock_mode);
	swab_mar_uint32_t (&to_swab->lock_flags);
	swab_mar_uint64_t (&to_swab->waiter_signal);
	swab_mar_uint64_t (&to_swab->resource_handle);
	swab_mar_message_source_t (&to_swab->callback_source);
	swab_mar_time_t (&to_swab->timeout);

	return;
}

static void exec_lck_resourcelockasync_endian_convert (void *msg)
{
	struct req_exec_lck_resourcelockasync *to_swab =
		(struct req_exec_lck_resourcelockasync *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->resource_name);
	swab_mar_uint64_t (&to_swab->lock_id);
	swab_mar_uint32_t (&to_swab->lock_mode);
	swab_mar_uint32_t (&to_swab->lock_flags);
	swab_mar_uint64_t (&to_swab->waiter_signal);
	swab_mar_uint64_t (&to_swab->resource_handle);
	swab_mar_message_source_t (&to_swab->callback_source);
	swab_mar_invocation_t (&to_swab->invocation);

	return;
}

static void exec_lck_resourceunlock_endian_convert (void *msg)
{
	struct req_exec_lck_resourceunlock *to_swab =
		(struct req_exec_lck_resourceunlock *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->resource_name);
	swab_mar_uint64_t (&to_swab->lock_id);

	return;
}

static void exec_lck_resourceunlockasync_endian_convert (void *msg)
{
	struct req_exec_lck_resourceunlockasync *to_swab =
		(struct req_exec_lck_resourceunlockasync *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->resource_name);
	swab_mar_uint64_t (&to_swab->lock_id);
	swab_mar_invocation_t (&to_swab->invocation);

	return;
}

static void exec_lck_lockpurge_endian_convert (void *msg)
{
	struct req_exec_lck_lockpurge *to_swab =
		(struct req_exec_lck_lockpurge *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_name_t (&to_swab->resource_name);

	return;
}

static void exec_lck_limitget_endian_convert (void *msg)
{
	struct req_exec_lck_limitget *to_swab =
		(struct req_exec_lck_limitget *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_message_source_t (&to_swab->source);
	swab_mar_uint64_t (&to_swab->limit_id);

	return;
}

static void exec_lck_resourcelock_timeout_endian_convert (void *msg)
{
	struct req_exec_lck_resourcelock_timeout *to_swab =
		(struct req_exec_lck_resourcelock_timeout *)msg;

	swab_coroipc_request_header_t (&to_swab->header);
	swab_mar_name_t (&to_swab->resource_name);
	swab_mar_uint64_t (&to_swab->lock_id);
	swab_mar_message_source_t (&to_swab->response_source);
	swab_mar_message_source_t (&to_swab->callback_source);

	return;
}

static void exec_lck_sync_resource_endian_convert (void *msg)
{
/* 	struct req_exec_lck_sync_resource *to_swab = */
/* 		(struct req_exec_lck_sync_resource *)msg; */

	return;
}

static void exec_lck_sync_resource_lock_endian_convert (void *msg)
{
/* 	struct req_exec_lck_sync_resource_lock *to_swab = */
/* 		(struct req_exec_lck_sync_resource_lock *)msg; */

	return;
}

static void exec_lck_sync_resource_refcount_endian_convert (void *msg)
{
/* 	struct req_exec_lck_sync_resource_refcount *to_swab = */
/* 		(struct req_exec_lck_sync_resource_refcount *)msg; */

	return;
}

static int lck_find_member_nodeid (
	unsigned int nodeid)
{
	unsigned int i;

	for (i = 0; i < lck_member_list_entries; i++) {
		if (nodeid == lck_member_list[i]) {
			return (1);
		}
	}
	return (0);
}

void lck_sync_refcount_increment (
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

void lck_sync_refcount_decrement (
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

void lck_sync_refcount_calculate (
	struct resource *resource)
{
	unsigned int i;

	resource->refcount = 0;

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		if (resource->refcount_set[i].nodeid == 0) {
			break;
		}
		resource->refcount += resource->refcount_set[i].refcount;
	}
}

static void lck_confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id)
{
	unsigned int i;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: lck_confchg_fn\n");

	memcpy (&saved_ring_id, ring_id,
		sizeof (struct memb_ring_id));

	if (configuration_type != TOTEM_CONFIGURATION_REGULAR) {
		return;
	}
	if (lck_sync_state != LCK_SYNC_STATE_NOT_STARTED) {
		return;
	}

	lck_sync_state = LCK_SYNC_STATE_STARTED;

	lowest_nodeid =  0xffffffff;

	for (i = 0; i < member_list_entries; i++) {
		if (lowest_nodeid > member_list[i]) {
			lowest_nodeid = member_list[i];
		}
	}

	memcpy (lck_member_list, member_list,
		sizeof (unsigned int) * member_list_entries);

	lck_member_list_entries = member_list_entries;

	return;
}

#ifdef _LCK_DEBUG_
static void lck_print_pr_pending_list (
	struct resource *resource)
{
	struct list_head *list;
	struct resource_lock *lock;

	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: pr_lock_pending_list { name=%s }\n",
		    (char *)(resource->resource_name.value));

	for (list = resource->pr_lock_pending_list_head.next;
	     list != &resource->pr_lock_pending_list_head;
	     list = list->next)
	{
		lock = list_entry (list, struct resource_lock, list);

		log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]:\t lock_id=%u (status=%d)\n",
			    (unsigned int)(lock->lock_id), lock->lock_status);
	}
}

static void lck_print_pr_granted_list (
	struct resource *resource)
{
	struct list_head *list;
	struct resource_lock *lock;

	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: pr_lock_granted_list { name=%s }\n",
		    (char *)(resource->resource_name.value));

	for (list = resource->pr_lock_granted_list_head.next;
	     list != &resource->pr_lock_granted_list_head;
	     list = list->next)
	{
		lock = list_entry (list, struct resource_lock, list);

		log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]:\t lock_id=%u (status=%d)\n",
			    (unsigned int)(lock->lock_id), lock->lock_status);
	}
}

static void lck_print_ex_pending_list (
	struct resource *resource)
{
	struct list_head *list;
	struct resource_lock *lock;

	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: ex_lock_pending_list { name=%s }\n",
		    (char *)(resource->resource_name.value));

	for (list = resource->ex_lock_pending_list_head.next;
	     list != &resource->ex_lock_pending_list_head;
	     list = list->next)
	{
		lock = list_entry (list, struct resource_lock, list);

		log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]:\t lock_id=%u (status=%d)\n",
			    (unsigned int)(lock->lock_id), lock->lock_status);
	}
}
#endif

static void lck_print_resource_lock_list (
	struct resource *resource)
{
	struct list_head *list;
	struct resource_lock *lock;

	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: resource_lock_list { name=%s }\n",
		    (char *)(resource->resource_name.value));

	for (list = resource->resource_lock_list_head.next;
	     list != &resource->resource_lock_list_head;
	     list = list->next)
	{
		lock = list_entry (list, struct resource_lock, resource_lock_list);

		log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]:\t [id=%u] status=%d flags=%d\n",
			    (unsigned int)(lock->lock_id), lock->lock_status, lock->lock_flags);
		log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: response { %x %p }\n",
			    (unsigned int)(lock->response_source.nodeid),
			    (void *)(lock->response_source.conn));
		log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: callback { %x %p }\n",
			    (unsigned int)(lock->callback_source.nodeid),
			    (void *)(lock->callback_source.conn));
	}

	return;
}

static void lck_print_resource_list (
	struct list_head *head)
{
	struct list_head *resource_list;
	struct resource *resource;

	resource_list = head->next;

	while (resource_list != head)
	{
		resource = list_entry (resource_list, struct resource, resource_list);
		resource_list = resource_list->next;

		log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: resource_name = %s\n",
			    (char *)(resource->resource_name.value));

		if (resource->ex_lock_granted != NULL) {
			log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]:\t ex_lock_granted { id=%u }\n",
				    (unsigned int)(resource->ex_lock_granted->lock_id));
		}

		lck_print_resource_lock_list (resource);
/* 		lck_print_ex_pending_list (resource); */
/* 		lck_print_pr_pending_list (resource); */
/* 		lck_print_pr_granted_list (resource); */
	}

	return;
}

static void lck_resource_close (
	const mar_name_t *resource_name,
	const hdb_handle_t resource_id,
	const mar_message_source_t *source)
{
	struct req_exec_lck_resourceclose req_exec_lck_resourceclose;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: lck_resource_close { name=%s }\n",
		    (char *)(resource_name->value));

	req_exec_lck_resourceclose.header.size =
		sizeof (struct req_exec_lck_resourceclose);
	req_exec_lck_resourceclose.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCECLOSE);

	memcpy (&req_exec_lck_resourceclose.resource_name,
		resource_name,	sizeof (mar_name_t));
	memcpy (&req_exec_lck_resourceclose.source,
		source,	sizeof (mar_message_source_t));

	req_exec_lck_resourceclose.resource_id = resource_id;
	req_exec_lck_resourceclose.exit_flag = 1;

	iovec.iov_base = (void *)&req_exec_lck_resourceclose;
	iovec.iov_len = sizeof (struct req_exec_lck_resourceclose);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void lck_resourcelock_timeout (void *data)
{
	struct req_exec_lck_resourcelock_timeout req_exec_lck_resourcelock_timeout;
	struct iovec iovec;

	struct resource_lock *lock = (struct resource_lock *)data;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: lck_resourcelock_timeout { id=%u }\n",
		    (unsigned int)(lock->lock_id));

	req_exec_lck_resourcelock_timeout.header.size =
		sizeof (struct req_exec_lck_resourcelock_timeout);
	req_exec_lck_resourcelock_timeout.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCELOCK_TIMEOUT);

	memcpy (&req_exec_lck_resourcelock_timeout.resource_name,
		&lock->resource->resource_name, sizeof (mar_name_t));
	memcpy (&req_exec_lck_resourcelock_timeout.response_source,
		&lock->response_source, sizeof (mar_message_source_t));
	memcpy (&req_exec_lck_resourcelock_timeout.callback_source,
		&lock->callback_source, sizeof (mar_message_source_t));

	req_exec_lck_resourcelock_timeout.lock_id = lock->lock_id;

	iovec.iov_base = (void *)&req_exec_lck_resourcelock_timeout;
	iovec.iov_len = sizeof (struct req_exec_lck_resourcelock_timeout);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static inline void lck_sync_resource_lock_timer_stop (void)
{
	struct resource *resource;
	struct list_head *resource_list;

	struct resource_lock *resource_lock;
	struct list_head *resource_lock_list;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: lck_sync_resource_lock_timer_stop\n");

	for (resource_list = resource_list_head.next;
	     resource_list != &resource_list_head;
	     resource_list = resource_list->next)
	{
		resource = list_entry (resource_list,
			struct resource, resource_list);

		for (resource_lock_list = resource->resource_lock_list_head.next;
		     resource_lock_list != &resource->resource_lock_list_head;
		     resource_lock_list = resource_lock_list->next)
		{
			resource_lock = list_entry (resource_lock_list,
				struct resource_lock, resource_lock_list);

			if (resource_lock->timer_handle != 0)
			{
				resource_lock->timeout =
					api->timer_expire_time_get (resource_lock->timer_handle);

				api->timer_delete (resource_lock->timer_handle);

				/* DEBUG */
				log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]:\t resource_name = %s\n",
					    (char *)(resource->resource_name.value));
				log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]:\t lock_id = %u\n",
					    (unsigned int)(resource_lock->lock_id));
			}
		}
	}
}

static inline void lck_sync_resource_lock_timer_start (void)
{
	struct resource *resource;
	struct list_head *resource_list;

	struct resource_lock *resource_lock;
	struct list_head *resource_lock_list;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: lck_sync_resource_lock_timer_start\n");

	for (resource_list = resource_list_head.next;
	     resource_list != &resource_list_head;
	     resource_list = resource_list->next)
	{
		resource = list_entry (resource_list, struct resource, resource_list);

		for (resource_lock_list = resource->resource_lock_list_head.next;
		     resource_lock_list != &resource->resource_lock_list_head;
		     resource_lock_list = resource_lock_list->next)
		{
			resource_lock = list_entry (resource_lock_list,
				struct resource_lock, resource_lock_list);

			if ((resource_lock->timeout != 0) &&
			    (api->ipc_source_is_local (&resource_lock->response_source)))
			{
				api->timer_add_absolute (
					resource_lock->timeout, (void *)(resource_lock),
					lck_resourcelock_timeout, &resource_lock->timer_handle);

				resource_lock->timeout = 0;

				/* DEBUG */
				log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]:\t resource_name = %s\n",
					    (char *)(resource->resource_name.value));
				log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]:\t lock_id = %u\n",
					    (unsigned int)(resource_lock->lock_id));
			}
		}
	}
}

static inline void lck_sync_resource_free (
	struct list_head *resource_head)
{
	struct resource *resource;
	struct list_head *resource_list;

	struct resource_lock *resource_lock;
	struct list_head *resource_lock_list;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: lck_sync_resource_free\n");

	resource_list = resource_head->next;

	while (resource_list != resource_head) {
		resource = list_entry (resource_list,
			struct resource, resource_list);
		resource_list = resource_list->next;

		/* DEBUG */
		log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]:\t resource_name = %s\n",
			    (char *)(resource->resource_name.value));

		resource_lock_list = resource->resource_lock_list_head.next;

		while (resource_lock_list != &resource->resource_lock_list_head) {
			resource_lock = list_entry (resource_lock_list,
				struct resource_lock, resource_lock_list);
			resource_lock_list = resource_lock_list->next;

			/* DEBUG */
			log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]:\t lock_id = %u\n",
				    (unsigned int)(resource_lock->lock_id));

			list_del (&resource_lock->resource_lock_list);
			free (resource_lock);

		}

		list_del (&resource->resource_list);
		free (resource);
	}

	list_init (resource_head);
}

static int lck_sync_resource_transmit (
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
		&saved_ring_id, sizeof (struct memb_ring_id));
	memcpy (&req_exec_lck_sync_resource.resource_name,
		&resource->resource_name, sizeof (mar_name_t));

	iovec.iov_base = (void *)&req_exec_lck_sync_resource;
	iovec.iov_len = sizeof (req_exec_lck_sync_resource);

	return (api->totem_mcast (&iovec, 1, TOTEM_AGREED));
}

static int lck_sync_resource_lock_transmit (
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
		&saved_ring_id, sizeof (struct memb_ring_id));
	memcpy (&req_exec_lck_sync_resource_lock.resource_name,
		&resource->resource_name, sizeof (mar_name_t));

	memcpy (&req_exec_lck_sync_resource_lock.response_source,
		&resource_lock->response_source, sizeof (mar_message_source_t));
	memcpy (&req_exec_lck_sync_resource_lock.callback_source,
		&resource_lock->callback_source, sizeof (mar_message_source_t));

	req_exec_lck_sync_resource_lock.lock_id = resource_lock->lock_id;
	req_exec_lck_sync_resource_lock.lock_mode = resource_lock->lock_mode;
	req_exec_lck_sync_resource_lock.lock_flags = resource_lock->lock_flags;
	req_exec_lck_sync_resource_lock.lock_status = resource_lock->lock_status;
	req_exec_lck_sync_resource_lock.waiter_signal = resource_lock->waiter_signal;
	req_exec_lck_sync_resource_lock.orphan_flag = resource_lock->orphan_flag;
	req_exec_lck_sync_resource_lock.invocation = resource_lock->invocation;
	req_exec_lck_sync_resource_lock.timeout = resource_lock->timeout;

	iovec.iov_base = (void *)&req_exec_lck_sync_resource_lock;
	iovec.iov_len = sizeof (req_exec_lck_sync_resource_lock);

	return (api->totem_mcast (&iovec, 1, TOTEM_AGREED));
}

static int lck_sync_resource_refcount_transmit (
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
		&saved_ring_id, sizeof (struct memb_ring_id));
	memcpy (&req_exec_lck_sync_resource_refcount.resource_name,
		&resource->resource_name, sizeof (mar_name_t));

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++) {
		req_exec_lck_sync_resource_refcount.refcount_set[i].refcount =
			resource->refcount_set[i].refcount;
		req_exec_lck_sync_resource_refcount.refcount_set[i].nodeid =
			resource->refcount_set[i].nodeid;
	}

	iovec.iov_base = (void *)&req_exec_lck_sync_resource_refcount;
	iovec.iov_len = sizeof (req_exec_lck_sync_resource_refcount);

	return (api->totem_mcast (&iovec, 1, TOTEM_AGREED));
}

static int lck_sync_resource_iterate (void)
{
	struct resource *resource;
	struct list_head *resource_list;

	struct resource_lock *resource_lock;
	struct list_head *resource_lock_list;

	int result;

	for (resource_list = lck_sync_iteration_resource;
	     resource_list != &resource_list_head;
	     resource_list = resource_list->next)
	{
		resource = list_entry (resource_list,
			struct resource, resource_list);

		if (lck_sync_iteration_state == LCK_SYNC_ITERATION_STATE_RESOURCE)
		{
			result = lck_sync_resource_transmit (resource);
			if (result != 0) {
				return (-1);
			}
			lck_sync_iteration_state = LCK_SYNC_ITERATION_STATE_RESOURCE_REFCOUNT;
		}

		if (lck_sync_iteration_state == LCK_SYNC_ITERATION_STATE_RESOURCE_REFCOUNT)
		{
			result = lck_sync_resource_refcount_transmit (resource);
			if (result != 0) {
				return (-1);
			}
			lck_sync_iteration_resource_lock = resource->resource_lock_list_head.next;
			lck_sync_iteration_state = LCK_SYNC_ITERATION_STATE_RESOURCE_LOCK;
		}

		if (lck_sync_iteration_state == LCK_SYNC_ITERATION_STATE_RESOURCE_LOCK)
		{
			for (resource_lock_list = lck_sync_iteration_resource_lock;
			     resource_lock_list != &resource->resource_lock_list_head;
			     resource_lock_list = resource_lock_list->next)
			{
				resource_lock = list_entry (resource_lock_list,
					struct resource_lock, resource_lock_list);

				result = lck_sync_resource_lock_transmit (resource, resource_lock);
				if (result != 0) {
					return (-1);
				}
				lck_sync_iteration_resource_lock = resource_lock_list->next;
			}
		}

		lck_sync_iteration_state = LCK_SYNC_ITERATION_STATE_RESOURCE;
		lck_sync_iteration_resource = resource_list->next;
	}

	return (0);
}

static void lck_sync_resource_enter (void)
{
	struct resource *resource;

	resource = list_entry (resource_list_head.next, struct resource, resource_list);

	lck_sync_state = LCK_SYNC_STATE_RESOURCE;
	lck_sync_iteration_state = LCK_SYNC_ITERATION_STATE_RESOURCE;

	lck_sync_iteration_resource = resource_list_head.next;
	lck_sync_iteration_resource_lock = resource->resource_lock_list_head.next;
}

static void lck_sync_init (
	const unsigned int *member_list,
	size_t member_list_entries,
	const struct memb_ring_id *ring_id)
{
	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: lck_sync_init\n");
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]:\t global_lock_count = %u\n", global_lock_count);

	/* DEBUG */
	lck_print_resource_list (&resource_list_head);

	lck_sync_resource_enter ();

	/*
	 * Stop timers for pending lock requests.
	 */
	lck_sync_resource_lock_timer_stop ();

	/*
	 * Reset the lock couter.
	 */
	sync_lock_count = 0;

	return;
}

static int lck_sync_process (void)
{
	int continue_process = 0;
	int iterate_result;
	int iterate_finish;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: lck_sync_process\n");

	switch (lck_sync_state)
	{
	case LCK_SYNC_STATE_RESOURCE:
		iterate_finish = 1;
		continue_process = 1;

		if (lowest_nodeid == api->totem_nodeid_get()) {
			TRACE1 ("transmit resources because lowest member in old configuration.\n");

			iterate_result = lck_sync_resource_iterate ();
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

static void lck_sync_activate (void)
{
	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: lck_sync_activate\n");

	lck_sync_resource_free (&resource_list_head);

	if (!list_empty (&sync_resource_list_head)) {
		list_splice (&sync_resource_list_head, &resource_list_head);
	}

	list_init (&sync_resource_list_head);

	/*
	 * Set the global lock count.
	 */
	global_lock_count = sync_lock_count;

	/*
	 * Restart timers for pending lock requests.
	 */
	lck_sync_resource_lock_timer_start ();

	lck_sync_state = LCK_SYNC_STATE_NOT_STARTED;

	/* DEBUG */
	lck_print_resource_list (&resource_list_head);
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]:\t global_lock_count = %u\n", global_lock_count);

	return;
}

static void lck_sync_abort (void)
{
	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: lck_sync_abort\n");

	lck_sync_resource_free (&resource_list_head);

	list_init (&sync_resource_list_head);

	return;
}

static void lck_exec_dump_fn (void)
{
	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: lck_exec_dump_fn\n");

	return;
}

static int lck_exec_init_fn (struct corosync_api_v1 *corosync_api)
{
	/* DEBUG */

#ifdef OPENAIS_SOLARIS
	logsys_subsys_init();
#endif

	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: lck_exec_init_fn\n");

	api = corosync_api;

	return (0);
}

static int lck_lib_init_fn (void *conn)
{
	struct lck_pd *lck_pd = (struct lck_pd *)(api->ipc_private_data_get(conn));

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: lck_lib_init_fn\n");

	list_init (&lck_pd->resource_list);
	list_init (&lck_pd->resource_cleanup_list);

	return (0);
}

static int lck_lib_exit_fn (void *conn)
{
	struct resource_cleanup *cleanup;
	struct list_head *cleanup_list;
	mar_message_source_t source;

	struct lck_pd *lck_pd = (struct lck_pd *)(api->ipc_private_data_get(conn));

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: lck_lib_exit_fn\n");

	api->ipc_source_set (&source, conn);

	cleanup_list = lck_pd->resource_cleanup_list.next;

	while (!list_empty (&lck_pd->resource_cleanup_list))
	{
		cleanup = list_entry (cleanup_list, struct resource_cleanup, cleanup_list);
		cleanup_list = cleanup_list->next;

		lck_resource_close (
			&cleanup->resource_name,
			cleanup->resource_id,
			&source);

		list_del (&cleanup->cleanup_list);
		free (cleanup);
	}

	return (0);
}

static int lck_resource_orphan_check (
	struct resource *resource)
{
	struct resource_lock *lock;
	struct list_head *list;

	if (resource->ex_lock_granted != NULL) {
		if (resource->ex_lock_granted->orphan_flag) {
			return (1);
		}
		else {
			return (0);
		}
	}

	for (list = resource->pr_lock_granted_list_head.next;
	     list != &resource->pr_lock_granted_list_head;
	     list = list->next)
	{
		lock = list_entry (list, struct resource_lock, list);

		if (lock->orphan_flag) {
			return (1);
		}
	}
	return (0);
}

static struct resource *lck_resource_find (
	struct list_head *resource_head,
	const mar_name_t *resource_name)
{
	struct resource *resource;
	struct list_head *resource_list;

	for (resource_list = resource_head->next;
	     resource_list != resource_head;
	     resource_list = resource_list->next)
	{
		resource = list_entry (resource_list, struct resource, resource_list);

		if (mar_name_match (resource_name, &resource->resource_name)) {
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
	struct resource_lock *resource_lock;
	struct list_head *resource_lock_list;

	for (resource_lock_list = resource->resource_lock_list_head.next;
	     resource_lock_list != &resource->resource_lock_list_head;
	     resource_lock_list = resource_lock_list->next)
	{
		resource_lock = list_entry (resource_lock_list, struct resource_lock, resource_lock_list);

		if ((memcmp (&resource_lock->callback_source, source,
			    sizeof (mar_message_source_t)) == 0) &&
		    (lock_id == resource_lock->lock_id))
		{
			return (resource_lock);
		}
	}
	return (0);
}

static void lck_resourcelock_response_send (
	struct resource_lock *resource_lock,
	SaAisErrorT error)
{
	struct res_lib_lck_resourcelock res_lib_lck_resourcelock;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: lck_resourcelock_response_send\n");

	if (api->ipc_source_is_local (&resource_lock->response_source))
	{
		res_lib_lck_resourcelock.header.size =
			sizeof (struct res_lib_lck_resourcelock);
		res_lib_lck_resourcelock.header.id =
			MESSAGE_RES_LCK_RESOURCELOCK;
		res_lib_lck_resourcelock.header.error = error;

		if (resource_lock != NULL) {
			res_lib_lck_resourcelock.lock_status =
				resource_lock->lock_status;
		}

		api->ipc_response_send (
			resource_lock->response_source.conn,
			&res_lib_lck_resourcelock,
			sizeof (struct res_lib_lck_resourcelock));
	}
}

static void lck_lockwaiter_callback_send (
	struct resource_lock *request_lock,
	struct resource_lock *grant_lock)
{
	struct res_lib_lck_lockwaiter_callback res_lib_lck_lockwaiter_callback;

	if ((api->ipc_source_is_local (&grant_lock->callback_source)) &&
	    (grant_lock->orphan_flag == 0))
	{
		res_lib_lck_lockwaiter_callback.header.size =
			sizeof (struct res_lib_lck_lockwaiter_callback);
		res_lib_lck_lockwaiter_callback.header.id =
			MESSAGE_RES_LCK_LOCKWAITER_CALLBACK;
		res_lib_lck_lockwaiter_callback.header.error = SA_AIS_OK;

		res_lib_lck_lockwaiter_callback.waiter_signal = request_lock->waiter_signal;
		res_lib_lck_lockwaiter_callback.lock_id = grant_lock->lock_id;
		res_lib_lck_lockwaiter_callback.mode_requested = request_lock->lock_mode;
		res_lib_lck_lockwaiter_callback.resource_handle = grant_lock->resource_handle;

		if (grant_lock->resource->ex_lock_granted != NULL) {
			res_lib_lck_lockwaiter_callback.mode_held = SA_LCK_EX_LOCK_MODE;
		}
		else {
			res_lib_lck_lockwaiter_callback.mode_held = SA_LCK_PR_LOCK_MODE;
		}

		api->ipc_dispatch_send (
			grant_lock->callback_source.conn,
			&res_lib_lck_lockwaiter_callback,
			sizeof (struct res_lib_lck_lockwaiter_callback));
	}
}

static void lck_lockwaiter_callback_list_send (
	struct resource_lock *request_lock,
	struct list_head *grant_list_head)
{
	struct resource_lock *grant_lock;
	struct list_head *grant_list;

	for (grant_list = grant_list_head->next;
	     grant_list != grant_list_head;
	     grant_list = grant_list->next)
	{
		grant_lock = list_entry (grant_list, struct resource_lock, list);
		lck_lockwaiter_callback_send (request_lock, grant_lock);
	}
}

static void lck_lockgrant_callback_send (
	struct resource_lock *resource_lock,
	SaAisErrorT error)
{
	struct res_lib_lck_lockgrant_callback res_lib_lck_lockgrant_callback;

	if (api->ipc_source_is_local (&resource_lock->callback_source))
	{
		res_lib_lck_lockgrant_callback.header.size =
			sizeof (struct res_lib_lck_lockgrant_callback);
		res_lib_lck_lockgrant_callback.header.id =
			MESSAGE_RES_LCK_LOCKGRANT_CALLBACK;
		res_lib_lck_lockgrant_callback.header.error = error;

		res_lib_lck_lockgrant_callback.resource_handle =
			resource_lock->resource_handle;

		if (resource_lock != NULL) {
			res_lib_lck_lockgrant_callback.lock_status =
				resource_lock->lock_status;
			res_lib_lck_lockgrant_callback.lock_id =
				resource_lock->lock_id;
			res_lib_lck_lockgrant_callback.invocation =
				resource_lock->invocation;
		}

		api->ipc_dispatch_send (
			resource_lock->callback_source.conn,
			&res_lib_lck_lockgrant_callback,
			sizeof (struct res_lib_lck_lockgrant_callback));
	}
}

static void lck_grant_lock (
	struct resource *resource,
	struct resource_lock *resource_lock)
{
	if (resource_lock->lock_mode == SA_LCK_PR_LOCK_MODE) {
		list_add_tail (&resource_lock->list,
			&resource->pr_lock_granted_list_head);
		resource_lock->lock_status = SA_LCK_LOCK_GRANTED;
		global_lock_count += 1;
	}
	else if (resource_lock->lock_mode == SA_LCK_EX_LOCK_MODE) {
		resource->ex_lock_granted = resource_lock;
		resource_lock->lock_status = SA_LCK_LOCK_GRANTED;
		global_lock_count += 1;
	}
}

static void lck_queue_lock (
	struct resource *resource,
	struct resource_lock *resource_lock)
{
	if (resource_lock->lock_flags & SA_LCK_LOCK_NO_QUEUE) {
		resource_lock->lock_status = SA_LCK_LOCK_NOT_QUEUED;
	}
	else {
		if (lck_resource_orphan_check (resource)) {
			resource_lock->lock_status = SA_LCK_LOCK_ORPHANED;
		}
		if (resource_lock->lock_mode == SA_LCK_PR_LOCK_MODE) {
			list_add_tail (&resource_lock->list,
				&resource->pr_lock_pending_list_head);

			global_lock_count += 1;

			lck_lockwaiter_callback_send (
				resource_lock,
				resource->ex_lock_granted);
		}
		else if (resource_lock->lock_mode == SA_LCK_EX_LOCK_MODE) {
			list_add_tail (&resource_lock->list,
				&resource->ex_lock_pending_list_head);

			global_lock_count += 1;

			if (resource->ex_lock_granted != NULL) {
				lck_lockwaiter_callback_send (
					resource_lock,
					resource->ex_lock_granted);
			}
			else {
				lck_lockwaiter_callback_list_send (
					resource_lock,
					&resource->pr_lock_granted_list_head);
			}
		}
	}
}

static void lck_lock (
	struct resource *resource,
	struct resource_lock *lock)
{
	lock->lock_status = 0;

	if (resource->ex_lock_granted != NULL) {
		/*
		 * Exclusive lock is granted on this resource.
		 * Add this lock request to the pending lock list.
		 */
		lck_queue_lock (resource, lock);
	}
	else {
		/*
		 * Exclusive lock is not granted on this resource.
		 */
		if (lock->lock_mode == SA_LCK_EX_LOCK_MODE) {
			if (list_empty (&resource->pr_lock_granted_list_head) == 0) {
				/*
				 * This lock request is for an exclusive lock and
				 * shared locks are granted on this resource.
				 * Add this lock request to the pending lock list.
				 */
				lck_queue_lock (resource, lock);
			}
			else {
				/*
				 * This lock request is for an exclusive lock and
				 * no locks are currently granted on this resource.
				 * Acquire an exclusive lock on the resource.
				 */
				lck_grant_lock (resource, lock);
			}
		}
		else {
			/*
			 * This lock request is for a shared lock.
			 * Acquire a shared lock on the resource.
			 */
			lck_grant_lock (resource, lock);
		}
	}
}

static void lck_unlock (
	struct resource *resource,
	struct resource_lock *resource_lock)
{
	struct resource_lock *lock;
	struct list_head *list;

	if (resource_lock == resource->ex_lock_granted) {
		/*
		 * We are unlocking the exclusive lock.
		 * Reset the exclusive lock and continue.
		 */
		resource->ex_lock_granted = NULL;
	}
	else {
		/*
		 * We are not unlocking the exclusive lock, therefore
		 * this lock must be in one of the lock lists.
		 * Remove the lock from the list.
		 */
		list_del (&resource_lock->list);

		/*
		 * If we are unlocking a lock that was queued, we must
		 * send a response/callback to the library.
		 */
		if (resource_lock->lock_status == 0) {
			if (resource_lock->timer_handle != 0) {
				api->timer_delete (resource_lock->timer_handle);
				lck_resourcelock_response_send (resource_lock, SA_AIS_OK);
			}
		}
	}

	/*
	 * All locks are in the resource_lock_list.
	 * Remove the lock from the list.
	 */
	list_del (&resource_lock->resource_lock_list);

	if ((resource->ex_lock_granted == NULL) &&
	    (list_empty (&resource->pr_lock_granted_list_head)))
	{
		/*
		 * There is no exclusive lock being held for this resource
		 * and there are no granted shared locks.
		 * Check if there are any pending exclusive lock requests.
		 */
		if (list_empty (&resource->ex_lock_pending_list_head) == 0) {
			/*
			 * There are pending exclusive lock requests.
			 * Grant the first request on the list.
			 */
			lock = list_entry (
				resource->ex_lock_pending_list_head.next,
				struct resource_lock, list);

			list_del (&lock->list);

			resource->ex_lock_granted = lock;

			lock->lock_status = SA_LCK_LOCK_GRANTED;

			if (lock->timer_handle != 0) {
				api->timer_delete (lock->timer_handle);
				lck_resourcelock_response_send (lock, SA_AIS_OK);
			}
			else {
				lck_lockgrant_callback_send (lock, SA_AIS_OK);
			}
		}
	}

	if (resource->ex_lock_granted == NULL) {
		/*
		 * There is no exclusive lock being held on this resource.
		 * Check if there are any pending shared lock requests.
		 */
		if (list_empty (&resource->pr_lock_pending_list_head) == 0) {
			/*
			 * There are pending shared lock requests.
			 * Grant all of requests in the list.
			 */
			for (list = resource->pr_lock_pending_list_head.next;
			     list != &resource->pr_lock_pending_list_head;
			     list = list->next)
			{
				lock = list_entry (list, struct resource_lock, list);
				lock->lock_status = SA_LCK_LOCK_GRANTED;

				if (lock->timer_handle != 0) {
					api->timer_delete (lock->timer_handle);
					lck_resourcelock_response_send (lock, SA_AIS_OK);
				}
				else {
					lck_lockgrant_callback_send (lock, SA_AIS_OK);
				}
			}

			/*
			 * Move pending shared locks to granted list.
			 */
			list_splice (&resource->pr_lock_pending_list_head,
				&resource->pr_lock_granted_list_head);

			list_init (&resource->pr_lock_pending_list_head);
		}
	}
}

static void lck_purge (
	struct resource *resource)
{
	struct resource_lock *resource_lock;
	struct list_head *resource_lock_list;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: lck_purge { name=%s }\n",
		    (char *)(resource->resource_name.value));

	resource_lock_list = resource->resource_lock_list_head.next;

	while (resource_lock_list != &resource->resource_lock_list_head)
	{
		resource_lock = list_entry (resource_lock_list, struct resource_lock, resource_lock_list);
		resource_lock_list = resource_lock_list->next;

		if (resource_lock->orphan_flag == 1) {
			lck_unlock (resource, resource_lock);
			global_lock_count -= 1;
			free (resource_lock);
		}
	}
	return;
}

static void lck_resourcelock_release (
	struct resource *resource,
	const unsigned int exit_flag,
	const mar_message_source_t *source)
{
	struct resource_lock *resource_lock;
	struct list_head *resource_lock_list;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "[DEBUG]: lck_resourcelock_release { name=%s } [%u]\n",
		    (char *)(resource->resource_name.value),
		    (unsigned int)(exit_flag));

	resource_lock_list = resource->resource_lock_list_head.next;

	while (resource_lock_list != &resource->resource_lock_list_head)
	{
		resource_lock = list_entry (resource_lock_list, struct resource_lock, resource_lock_list);
		resource_lock_list = resource_lock_list->next;

		if (memcmp (&resource_lock->callback_source,
			    source, sizeof (mar_message_source_t)) == 0)
		{
			/*
			 * This is a hack to prevent lck_unlock from sending
			 * a callback (or response) to the library.
			 */
			if (exit_flag) {
				memset (&resource_lock->response_source, 0, sizeof (mar_message_source_t));
				memset (&resource_lock->callback_source, 0, sizeof (mar_message_source_t));
			}

			if (resource_lock->lock_status == SA_LCK_LOCK_GRANTED) {
				if (resource_lock->lock_flags & SA_LCK_LOCK_ORPHAN) {
					resource_lock->orphan_flag = 1;
				}
				else {
					lck_unlock (resource, resource_lock);
					global_lock_count -= 1;
					free (resource_lock);
				}
			}
			else {
/* 				if (resource_lock->timer_handle != 0) { */
/* 					api->timer_delete (resource_lock->timer_handle); */
/* 				} */
				lck_unlock (resource, resource_lock);
				global_lock_count -= 1;
				free (resource_lock);
			}
		}
	}
	return;
}

static void message_handler_req_exec_lck_resourceopen (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_lck_resourceopen *req_exec_lck_resourceopen =
		message;
	struct res_lib_lck_resourceopen res_lib_lck_resourceopen;
	SaAisErrorT error = SA_AIS_OK;

	struct lck_pd *lck_pd = NULL;
	struct resource *resource = NULL;
	struct resource_cleanup *cleanup = NULL;
	struct resource_instance *resource_instance = NULL;
	hdb_handle_t resource_id = 0;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saLckResourceOpen\n");

	if (api->ipc_source_is_local (&req_exec_lck_resourceopen->source)) {
		cleanup = malloc (sizeof (struct resource_cleanup));
		if (cleanup == NULL) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}
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
		memcpy (&resource->resource_name,
			&req_exec_lck_resourceopen->resource_name,
			sizeof (mar_name_t));
/* 		memcpy (&resource->source, */
/* 			&req_exec_lck_resourceopen->source, */
/* 			sizeof (mar_message_source_t)); */

		resource->ex_lock_granted = NULL;

		list_init (&resource->resource_lock_list_head);
		list_init (&resource->pr_lock_granted_list_head);
		list_init (&resource->pr_lock_pending_list_head);
		list_init (&resource->ex_lock_pending_list_head);

		list_init (&resource->resource_list);
		list_add_tail (&resource->resource_list, &resource_list_head);
	}

	lck_sync_refcount_increment (resource, nodeid);
	lck_sync_refcount_calculate (resource);

error_exit:
	if (api->ipc_source_is_local (&req_exec_lck_resourceopen->source))
	{
		if (error == SA_AIS_OK) {
			/*
			 * Create resource instance.
			 */
			hdb_handle_create (&resource_hdb,
				sizeof (struct resource_instance), &resource_id);
			hdb_handle_get (&resource_hdb,
				resource_id, (void *)&resource_instance);
			memcpy (&resource_instance->source,
				&req_exec_lck_resourceopen->source,
				sizeof (mar_message_source_t));

			/*
			 * Add resource to cleanup list.
			 */
			lck_pd = api->ipc_private_data_get (req_exec_lck_resourceopen->source.conn);

			memcpy (&cleanup->resource_name,
				&resource->resource_name, sizeof (mar_name_t));

			cleanup->resource_id = resource_id;
			list_init (&cleanup->cleanup_list);
			list_add_tail (&cleanup->cleanup_list, &lck_pd->resource_cleanup_list);
		}
		else {
			free (cleanup);
		}

		res_lib_lck_resourceopen.header.size =
			sizeof (struct res_lib_lck_resourceopen);
		res_lib_lck_resourceopen.header.id =
			MESSAGE_RES_LCK_RESOURCEOPEN;
		res_lib_lck_resourceopen.header.error = error;
		res_lib_lck_resourceopen.resource_id = resource_id;

		api->ipc_response_send (
			req_exec_lck_resourceopen->source.conn,
			&res_lib_lck_resourceopen,
			sizeof (struct res_lib_lck_resourceopen));
	}
}

static void message_handler_req_exec_lck_resourceopenasync (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_lck_resourceopenasync *req_exec_lck_resourceopenasync =
		message;
	struct res_lib_lck_resourceopenasync res_lib_lck_resourceopenasync;
	struct res_lib_lck_resourceopen_callback res_lib_lck_resourceopen_callback;
	SaAisErrorT error = SA_AIS_OK;

	struct lck_pd *lck_pd = NULL;
	struct resource *resource = NULL;
	struct resource_cleanup *cleanup = NULL;
	struct resource_instance *resource_instance = NULL;
	hdb_handle_t resource_id = 0;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saLckResourceOpenAsync\n");

	if (api->ipc_source_is_local (&req_exec_lck_resourceopenasync->source)) {
		cleanup = malloc (sizeof (struct resource_cleanup));
		if (cleanup == NULL) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}
	}

	resource = lck_resource_find (&resource_list_head,
		&req_exec_lck_resourceopenasync->resource_name);

	if (resource == NULL) {
		if ((req_exec_lck_resourceopenasync->open_flags & SA_LCK_RESOURCE_CREATE) == 0) {
			error = SA_AIS_ERR_NOT_EXIST;
			goto error_exit;
		}

		resource = malloc (sizeof (struct resource));
		if (resource == NULL) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}
		memset (resource, 0, sizeof (struct resource));
		memcpy (&resource->resource_name,
			&req_exec_lck_resourceopenasync->resource_name,
			sizeof (mar_name_t));
/* 		memcpy (&resource->source, */
/* 			&req_exec_lck_resourceopenasync->source, */
/* 			sizeof (mar_message_source_t)); */

		resource->ex_lock_granted = NULL;

		list_init (&resource->resource_lock_list_head);
		list_init (&resource->pr_lock_granted_list_head);
		list_init (&resource->pr_lock_pending_list_head);
		list_init (&resource->ex_lock_pending_list_head);

		list_init (&resource->resource_list);
		list_add_tail (&resource->resource_list, &resource_list_head);
	}

	lck_sync_refcount_increment (resource, nodeid);
	lck_sync_refcount_calculate (resource);

error_exit:
	if (api->ipc_source_is_local (&req_exec_lck_resourceopenasync->source))
	{
		if (error == SA_AIS_OK) {
			/*
			 * Create resource instance.
			 */
			hdb_handle_create (&resource_hdb,
				sizeof (struct resource_instance), &resource_id);
			hdb_handle_get (&resource_hdb,
				resource_id, (void *)&resource_instance);
			memcpy (&resource_instance->source,
				&req_exec_lck_resourceopenasync->source,
				sizeof (mar_message_source_t));

			/*
			 * Add resource to cleanup list.
			 */
			lck_pd = api->ipc_private_data_get (req_exec_lck_resourceopenasync->source.conn);

			memcpy (&cleanup->resource_name,
				&resource->resource_name, sizeof (mar_name_t));

			cleanup->resource_id = resource_id;
			list_init (&cleanup->cleanup_list);
			list_add_tail (&cleanup->cleanup_list, &lck_pd->resource_cleanup_list);
		}
		else {
			free (cleanup);
		}

		/*
		 * Send response to library.
		 */
		res_lib_lck_resourceopenasync.header.size =
			sizeof (struct res_lib_lck_resourceopenasync);
		res_lib_lck_resourceopenasync.header.id =
			MESSAGE_RES_LCK_RESOURCEOPENASYNC;
		res_lib_lck_resourceopenasync.header.error = error;
		res_lib_lck_resourceopenasync.resource_id = resource_id;

		api->ipc_response_send (
			req_exec_lck_resourceopenasync->source.conn,
			&res_lib_lck_resourceopenasync,
			sizeof (struct res_lib_lck_resourceopenasync));

		/*
		 * Send callback to library.
		 */
		res_lib_lck_resourceopen_callback.header.size =
			sizeof (struct res_lib_lck_resourceopen_callback);
		res_lib_lck_resourceopen_callback.header.id =
			MESSAGE_RES_LCK_RESOURCEOPEN_CALLBACK;
		res_lib_lck_resourceopen_callback.header.error = error;

		res_lib_lck_resourceopen_callback.resource_handle =
			req_exec_lck_resourceopenasync->resource_handle;
		res_lib_lck_resourceopen_callback.invocation =
			req_exec_lck_resourceopenasync->invocation;

		api->ipc_dispatch_send (
			req_exec_lck_resourceopenasync->source.conn,
			&res_lib_lck_resourceopen_callback,
			sizeof (struct res_lib_lck_resourceopen_callback));
	}
}

static void message_handler_req_exec_lck_resourceclose (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_lck_resourceclose *req_exec_lck_resourceclose =
		message;
	struct res_lib_lck_resourceclose res_lib_lck_resourceclose;
	SaAisErrorT error = SA_AIS_OK;

	struct resource *resource = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saLckResourceClose\n");

	resource = lck_resource_find (&resource_list_head,
		&req_exec_lck_resourceclose->resource_name);

	if (resource == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	lck_sync_refcount_decrement (resource, nodeid);
	lck_sync_refcount_calculate (resource);

	lck_resourcelock_release (resource,
		req_exec_lck_resourceclose->exit_flag,
		&req_exec_lck_resourceclose->source);

	if ((resource->refcount == 0) &&
	    (resource->ex_lock_granted == NULL) &&
	    (list_empty (&resource->pr_lock_granted_list_head)))
	{
		list_del (&resource->resource_list);
		free (resource);
	}

error_exit:
	if (api->ipc_source_is_local (&req_exec_lck_resourceclose->source))
	{
		if (req_exec_lck_resourceclose->exit_flag == 0) {
			res_lib_lck_resourceclose.header.size =
				sizeof (struct res_lib_lck_resourceclose);
			res_lib_lck_resourceclose.header.id =
				MESSAGE_RES_LCK_RESOURCECLOSE;
			res_lib_lck_resourceclose.header.error = error;

			api->ipc_response_send (
				req_exec_lck_resourceclose->source.conn,
				&res_lib_lck_resourceclose,
				sizeof (struct res_lib_lck_resourceclose));
		}

		if (error == SA_AIS_OK) {
			hdb_handle_put (&resource_hdb, req_exec_lck_resourceclose->resource_id);
			hdb_handle_destroy (&resource_hdb, req_exec_lck_resourceclose->resource_id);
		}
	}
}

static void message_handler_req_exec_lck_resourcelock (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_lck_resourcelock *req_exec_lck_resourcelock =
		message;
	struct res_lib_lck_resourcelock res_lib_lck_resourcelock;
	SaAisErrorT error = SA_AIS_OK;

	struct resource *resource = NULL;
	struct resource_lock *lock = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saLckResourceLock\n");

	/* if ((req_exec_lck_resourcelock->lock_flags & ~SA_LCK_LOCK_NO_QUEUE) && */
	if (global_lock_count == MAX_NUM_LOCKS)
	{
		error = SA_AIS_ERR_NO_RESOURCES;
		goto error_exit;
	}

	resource = lck_resource_find (&resource_list_head,
		&req_exec_lck_resourcelock->resource_name);

	if (resource == NULL) {
		error = SA_AIS_ERR_LIBRARY; /* ? */
		goto error_exit;
	}

	lock = malloc (sizeof (struct resource_lock));
	if (lock == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}
	memset (lock, 0, sizeof (struct resource_lock));

	memcpy (&lock->response_source,
		&req_exec_lck_resourcelock->source,
		sizeof (mar_message_source_t));
	memcpy (&lock->callback_source,
		&req_exec_lck_resourcelock->callback_source,
		sizeof (mar_message_source_t));

	lock->resource = resource;
	lock->orphan_flag = 0;
	lock->invocation = 0;
	lock->lock_id = req_exec_lck_resourcelock->lock_id;
	lock->lock_mode = req_exec_lck_resourcelock->lock_mode;
	lock->lock_flags = req_exec_lck_resourcelock->lock_flags;
	lock->waiter_signal = req_exec_lck_resourcelock->waiter_signal;
	/* lock->resource_id = req_exec_lck_resourcelock->resource_id; */
	lock->resource_handle = req_exec_lck_resourcelock->resource_handle;

	list_init (&lock->list);
	list_init (&lock->resource_lock_list);
	list_add_tail (&lock->resource_lock_list, &resource->resource_lock_list_head);

	/* !!! */

	lck_lock (resource, lock);

	/* !!! */

error_exit:
	if (api->ipc_source_is_local (&req_exec_lck_resourcelock->source))
	{
		if ((lock != NULL) &&
		    (lock->lock_status != SA_LCK_LOCK_GRANTED) &&
		    (lock->lock_status != SA_LCK_LOCK_NOT_QUEUED))
		{
			api->timer_add_duration (
				req_exec_lck_resourcelock->timeout, (void *)(lock),
				lck_resourcelock_timeout, &lock->timer_handle);
		}
		else {
			res_lib_lck_resourcelock.header.size =
				sizeof (struct res_lib_lck_resourcelock);
			res_lib_lck_resourcelock.header.id =
				MESSAGE_RES_LCK_RESOURCELOCK;
			res_lib_lck_resourcelock.header.error = error;

			if (lock != NULL) {
				res_lib_lck_resourcelock.lock_status = lock->lock_status;
			}

			api->ipc_response_send (
				req_exec_lck_resourcelock->source.conn,
				&res_lib_lck_resourcelock,
				sizeof (struct res_lib_lck_resourcelock));
		}
	}
}

static void message_handler_req_exec_lck_resourcelockasync (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_lck_resourcelockasync *req_exec_lck_resourcelockasync =
		message;
	struct res_lib_lck_resourcelockasync res_lib_lck_resourcelockasync;
	SaAisErrorT error = SA_AIS_OK;

	struct resource *resource = NULL;
	struct resource_lock *lock = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saLckResourceLockAsync\n");

	/* if ((req_exec_lck_resourcelockasync->lock_flags & ~SA_LCK_LOCK_NO_QUEUE) && */
	if (global_lock_count == MAX_NUM_LOCKS)
	{
		error = SA_AIS_ERR_NO_RESOURCES;
		goto error_exit;
	}

	resource = lck_resource_find (&resource_list_head,
		&req_exec_lck_resourcelockasync->resource_name);

	if (resource == NULL) {
		error = SA_AIS_ERR_LIBRARY; /* ? */
		goto error_exit;
	}

	lock = malloc (sizeof (struct resource_lock));
	if (lock == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}
	memset (lock, 0, sizeof (struct resource_lock));

	memcpy (&lock->response_source,
		&req_exec_lck_resourcelockasync->source,
		sizeof (mar_message_source_t));
	memcpy (&lock->callback_source,
		&req_exec_lck_resourcelockasync->callback_source,
		sizeof (mar_message_source_t));

	lock->resource = resource;
	lock->orphan_flag = 0;
	lock->invocation = req_exec_lck_resourcelockasync->invocation;
	lock->lock_id = req_exec_lck_resourcelockasync->lock_id;
	lock->lock_mode = req_exec_lck_resourcelockasync->lock_mode;
	lock->lock_flags = req_exec_lck_resourcelockasync->lock_flags;
	lock->waiter_signal = req_exec_lck_resourcelockasync->waiter_signal;
	/* lock->resource_id = req_exec_lck_resourcelockasync->resource_id; */
	lock->resource_handle = req_exec_lck_resourcelockasync->resource_handle;

	list_init (&lock->list);
	list_init (&lock->resource_lock_list);
	list_add_tail (&lock->resource_lock_list, &resource->resource_lock_list_head);

	/* !!! */

	lck_lock (resource, lock);

	/* !!! */

error_exit:
	if (api->ipc_source_is_local (&req_exec_lck_resourcelockasync->source))
	{
		res_lib_lck_resourcelockasync.header.size =
			sizeof (struct res_lib_lck_resourcelockasync);
		res_lib_lck_resourcelockasync.header.id =
			MESSAGE_RES_LCK_RESOURCELOCKASYNC;
		res_lib_lck_resourcelockasync.header.error = error;

		api->ipc_response_send (
			req_exec_lck_resourcelockasync->source.conn,
			&res_lib_lck_resourcelockasync,
			sizeof (struct res_lib_lck_resourcelockasync));

		/* if ((lock != NULL) && (lock->lock_status == SA_LCK_LOCK_GRANTED)) { */
		if ((lock != NULL) && (lock->lock_status != 0)) {
			lck_lockgrant_callback_send (lock, error);
		}
	}
}

static void message_handler_req_exec_lck_resourceunlock (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_lck_resourceunlock *req_exec_lck_resourceunlock =
		message;
	struct res_lib_lck_resourceunlock res_lib_lck_resourceunlock;
	SaAisErrorT error = SA_AIS_OK;

	struct resource *resource = NULL;
	struct resource_lock *resource_lock = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saLckResourceUnlock\n");

	resource = lck_resource_find (&resource_list_head,
		&req_exec_lck_resourceunlock->resource_name);

	if (resource == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	resource_lock = lck_resource_lock_find (resource,
		&req_exec_lck_resourceunlock->source,
		req_exec_lck_resourceunlock->lock_id);

/* 	if (resource_lock == NULL) { */
/* 		error = SA_AIS_ERR_NOT_EXIST; */
/* 		goto error_exit; */
/* 	} */

/* 	lck_unlock (resource, resource_lock); */
/* 	global_lock_count -= 1; */
/* 	free (resource_lock); */

	if (resource_lock != NULL) {
		lck_unlock (resource, resource_lock);
		global_lock_count -= 1;
		free (resource_lock);
	}

error_exit:
	if (api->ipc_source_is_local (&req_exec_lck_resourceunlock->source))
	{
		res_lib_lck_resourceunlock.header.size =
			sizeof (struct res_lib_lck_resourceunlock);
		res_lib_lck_resourceunlock.header.id =
			MESSAGE_RES_LCK_RESOURCEUNLOCK;
		res_lib_lck_resourceunlock.header.error = error;

		api->ipc_response_send (
			req_exec_lck_resourceunlock->source.conn,
			&res_lib_lck_resourceunlock,
			sizeof (struct res_lib_lck_resourceunlock));
	}
}

static void message_handler_req_exec_lck_resourceunlockasync (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_lck_resourceunlockasync *req_exec_lck_resourceunlockasync =
		message;
	struct res_lib_lck_resourceunlockasync res_lib_lck_resourceunlockasync;
	struct res_lib_lck_resourceunlock_callback res_lib_lck_resourceunlock_callback;
	SaAisErrorT error = SA_AIS_OK;

	struct resource *resource = NULL;
	struct resource_lock *resource_lock = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saLckResourceUnlockAsync\n");

	resource = lck_resource_find (&resource_list_head,
		&req_exec_lck_resourceunlockasync->resource_name);
	if (resource == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	resource_lock = lck_resource_lock_find (resource,
		&req_exec_lck_resourceunlockasync->source,
		req_exec_lck_resourceunlockasync->lock_id);

/* 	if (resource_lock == NULL) { */
/* 		error = SA_AIS_ERR_NOT_EXIST; */
/* 		goto error_exit; */
/* 	} */

/* 	lck_unlock (resource, resource_lock); */
/* 	global_lock_count -= 1; */
/* 	free (resource_lock); */

	if (resource_lock != NULL) {
		lck_unlock (resource, resource_lock);
		global_lock_count -= 1;
		free (resource_lock);
	}

error_exit:
	if (api->ipc_source_is_local (&req_exec_lck_resourceunlockasync->source))
	{
		res_lib_lck_resourceunlockasync.header.size =
			sizeof (struct res_lib_lck_resourceunlockasync);
		res_lib_lck_resourceunlockasync.header.id =
			MESSAGE_RES_LCK_RESOURCEUNLOCKASYNC;
		res_lib_lck_resourceunlockasync.header.error = error;

		api->ipc_response_send (
			req_exec_lck_resourceunlockasync->source.conn,
			&res_lib_lck_resourceunlockasync,
			sizeof (struct res_lib_lck_resourceunlockasync));

		res_lib_lck_resourceunlock_callback.header.size =
			sizeof (struct res_lib_lck_resourceunlock_callback);
		res_lib_lck_resourceunlock_callback.header.id =
			MESSAGE_RES_LCK_RESOURCEUNLOCK_CALLBACK;
		res_lib_lck_resourceunlock_callback.header.error = error;

		res_lib_lck_resourceunlock_callback.invocation =
			req_exec_lck_resourceunlockasync->invocation;
		res_lib_lck_resourceunlock_callback.resource_handle =
			req_exec_lck_resourceunlockasync->resource_handle;
		res_lib_lck_resourceunlock_callback.lock_id =
			req_exec_lck_resourceunlockasync->lock_id;

		api->ipc_dispatch_send (
			req_exec_lck_resourceunlockasync->source.conn,
			&res_lib_lck_resourceunlock_callback,
			sizeof (struct res_lib_lck_resourceunlock_callback));
	}
}

static void message_handler_req_exec_lck_lockpurge (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_lck_lockpurge *req_exec_lck_lockpurge =
		message;
	struct res_lib_lck_lockpurge res_lib_lck_lockpurge;
	SaAisErrorT error = SA_AIS_OK;

	struct resource *resource = NULL;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saLckResourceLockPurge\n");

	resource = lck_resource_find (&resource_list_head,
		&req_exec_lck_lockpurge->resource_name);

	if (resource == NULL) {
		/*
		 * The error code SA_AIS_ERR_NOT_EXIST is not valid
		 * for saLckLockPurge. What should we return here?
		 */
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	lck_purge (resource);

error_exit:
	if (api->ipc_source_is_local (&req_exec_lck_lockpurge->source))
	{
		res_lib_lck_lockpurge.header.size =
			sizeof (struct res_lib_lck_lockpurge);
		res_lib_lck_lockpurge.header.id =
			MESSAGE_RES_LCK_LOCKPURGE;
		res_lib_lck_lockpurge.header.error = error;

		api->ipc_response_send (
			req_exec_lck_lockpurge->source.conn,
			&res_lib_lck_lockpurge,
			sizeof (struct res_lib_lck_lockpurge));
	}
}

static void message_handler_req_exec_lck_limitget (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_lck_limitget *req_exec_lck_limitget =
		message;
	struct res_lib_lck_limitget res_lib_lck_limitget;
	SaAisErrorT error = SA_AIS_OK;
	SaUint64T value = 0;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: saLckLimitGet\n");

	switch (req_exec_lck_limitget->limit_id)
	{
	case SA_LCK_MAX_NUM_LOCKS_ID:
		value = MAX_NUM_LOCKS;
		break;
	default:
		error = SA_AIS_ERR_INVALID_PARAM;
		break;
	}

/*error_exit:*/
	if (api->ipc_source_is_local (&req_exec_lck_limitget->source))
	{
		res_lib_lck_limitget.header.size =
			sizeof (struct res_lib_lck_limitget);
		res_lib_lck_limitget.header.id =
			MESSAGE_RES_LCK_LIMITGET;
		res_lib_lck_limitget.header.error = error;
		res_lib_lck_limitget.value = value;

		api->ipc_response_send (
			req_exec_lck_limitget->source.conn,
			&res_lib_lck_limitget,
			sizeof (struct res_lib_lck_limitget));
	}
}

static void message_handler_req_exec_lck_resourcelock_timeout (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_lck_resourcelock_timeout *req_exec_lck_resourcelock_timeout =
		message;
	struct res_lib_lck_resourcelock res_lib_lck_resourcelock;
/*	SaAisErrorT error = SA_AIS_OK;*/

	struct resource *resource = NULL;
	struct resource_lock *resource_lock = NULL;

	resource = lck_resource_find (&resource_list_head,
		&req_exec_lck_resourcelock_timeout->resource_name);

	assert (resource != NULL);

	resource_lock = lck_resource_lock_find (
		resource,
		&req_exec_lck_resourcelock_timeout->callback_source,
		req_exec_lck_resourcelock_timeout->lock_id);

	assert (resource_lock != NULL);

	if (api->ipc_source_is_local (&req_exec_lck_resourcelock_timeout->response_source))
	{
		res_lib_lck_resourcelock.header.size =
			sizeof (struct res_lib_lck_resourcelock);
		res_lib_lck_resourcelock.header.id =
			MESSAGE_RES_LCK_RESOURCELOCK;
		res_lib_lck_resourcelock.header.error = SA_AIS_ERR_TIMEOUT;

		res_lib_lck_resourcelock.lock_status =
			resource_lock->lock_status;

		api->ipc_response_send (
			req_exec_lck_resourcelock_timeout->response_source.conn,
			&res_lib_lck_resourcelock,
			sizeof (struct res_lib_lck_resourcelock));
	}

	list_del (&resource_lock->list);
	list_del (&resource_lock->resource_lock_list);

	free (resource_lock);
}

static void message_handler_req_exec_lck_sync_resource (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_lck_sync_resource *req_exec_lck_sync_resource =
		message;
	struct resource *resource;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: sync_resource\n");

	/*
	 * Ignore message from previous ring
	 */
	if (memcmp (&req_exec_lck_sync_resource->ring_id,
		    &saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	resource = lck_resource_find (&sync_resource_list_head,
		&req_exec_lck_sync_resource->resource_name);

	/*
	 * This resource should not exist.
	 */
	assert (resource == NULL);

	resource = malloc (sizeof (struct resource));
	if (resource == NULL) {
		api->error_memory_failure();
	}

	memset (resource, 0, sizeof (struct resource));
	memcpy (&resource->resource_name,
		&req_exec_lck_sync_resource->resource_name,
		sizeof (mar_name_t));
	memcpy (&resource->source,
		&req_exec_lck_sync_resource->source,
		sizeof (mar_message_source_t));

	resource->ex_lock_granted = NULL;

	list_init (&resource->resource_lock_list_head);
	list_init (&resource->pr_lock_granted_list_head);
	list_init (&resource->pr_lock_pending_list_head);
	list_init (&resource->ex_lock_pending_list_head);

	list_init (&resource->resource_list);
	list_add_tail (&resource->resource_list, &sync_resource_list_head);

	return;
}

static void message_handler_req_exec_lck_sync_resource_lock (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_lck_sync_resource_lock *req_exec_lck_sync_resource_lock =
		message;
	struct resource *resource;
	struct resource_lock *resource_lock;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: sync_resource_lock\n");

	/*
	 * Ignore message from previous ring
	 */
	if (memcmp (&req_exec_lck_sync_resource_lock->ring_id,
		    &saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	resource = lck_resource_find (&sync_resource_list_head,
		&req_exec_lck_sync_resource_lock->resource_name);

	/*
	 * This resource should exist.
	 */
	assert (resource != NULL);

	/* TODO: check to make sure the lock doesn't already exist */

	resource_lock = malloc (sizeof (struct resource_lock));
	if (resource_lock == NULL) {
		api->error_memory_failure ();
	}

	memset (resource_lock, 0, sizeof (struct resource_lock));
	memcpy (&resource_lock->response_source,
		&req_exec_lck_sync_resource_lock->response_source,
		sizeof (mar_message_source_t));
	memcpy (&resource_lock->callback_source,
		&req_exec_lck_sync_resource_lock->callback_source,
		sizeof (mar_message_source_t));

	list_init (&resource_lock->list);
	list_init (&resource_lock->resource_lock_list);

	list_add_tail (&resource_lock->resource_lock_list, &resource->resource_lock_list_head);

	resource_lock->resource = resource;

	resource_lock->lock_id = req_exec_lck_sync_resource_lock->lock_id;
	resource_lock->lock_mode = req_exec_lck_sync_resource_lock->lock_mode;
	resource_lock->lock_flags = req_exec_lck_sync_resource_lock->lock_flags;
	resource_lock->lock_status = req_exec_lck_sync_resource_lock->lock_status;
	resource_lock->waiter_signal = req_exec_lck_sync_resource_lock->waiter_signal;
	resource_lock->orphan_flag = req_exec_lck_sync_resource_lock->orphan_flag;
	resource_lock->invocation = req_exec_lck_sync_resource_lock->invocation;
	resource_lock->timeout = req_exec_lck_sync_resource_lock->timeout;

	/*
	 * Determine the list that this lock should be added to
	 */
	if (resource_lock->lock_mode == SA_LCK_PR_LOCK_MODE) {
		if (resource_lock->lock_status == SA_LCK_LOCK_GRANTED) {
			list_add_tail (&resource_lock->list, &resource->pr_lock_granted_list_head);
		} else {
			list_add_tail (&resource_lock->list, &resource->pr_lock_pending_list_head);
		}
	}

	if (resource_lock->lock_mode == SA_LCK_EX_LOCK_MODE) {
		if (resource_lock->lock_status == SA_LCK_LOCK_GRANTED) {
			resource->ex_lock_granted = resource_lock;
		} else {
			list_add_tail (&resource_lock->list, &resource->ex_lock_pending_list_head);
		}
	}

	/*
	 * Increment the lock count.
	 */
	sync_lock_count += 1;

	return;
}

static void message_handler_req_exec_lck_sync_resource_refcount (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_lck_sync_resource_refcount *req_exec_lck_sync_resource_refcount =
		message;
	struct resource *resource;

	unsigned int i;
	unsigned int j;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "EXEC request: sync_resource_refcount\n");

	/*
	 * Ignore message from previous ring
	 */
	if (memcmp (&req_exec_lck_sync_resource_refcount->ring_id,
		    &saved_ring_id, sizeof (struct memb_ring_id)) != 0)
	{
		return;
	}

	resource = lck_resource_find (&sync_resource_list_head,
		&req_exec_lck_sync_resource_refcount->resource_name);

	/*
	 * This resource should exist.
	 */
	assert (resource != NULL);

	for (i = 0; i < PROCESSOR_COUNT_MAX; i++)
	{
		if (req_exec_lck_sync_resource_refcount->refcount_set[i].nodeid == 0) {
			break;
		}

		if (lck_find_member_nodeid (req_exec_lck_sync_resource_refcount->refcount_set[i].nodeid) == 0) {
			continue;
		}

		for (j = 0; j < PROCESSOR_COUNT_MAX; j++)
		{
			if (resource->refcount_set[j].nodeid == 0)
			{
				resource->refcount_set[j].nodeid =
					req_exec_lck_sync_resource_refcount->refcount_set[i].nodeid;
				resource->refcount_set[j].refcount =
					req_exec_lck_sync_resource_refcount->refcount_set[i].refcount;
				break;
			}

			if (req_exec_lck_sync_resource_refcount->refcount_set[i].nodeid == resource->refcount_set[j].nodeid)
			{
				resource->refcount_set[j].refcount +=
					req_exec_lck_sync_resource_refcount->refcount_set[i].refcount;
				break;
			}
		}
	}

	lck_sync_refcount_calculate (resource);

	return;
}

static void message_handler_req_lib_lck_resourceopen (
	void *conn,
	const void *msg)
{
	const struct req_lib_lck_resourceopen *req_lib_lck_resourceopen = msg;
	struct req_exec_lck_resourceopen req_exec_lck_resourceopen;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saLckResourceOpen\n");

	req_exec_lck_resourceopen.header.size =
		sizeof (struct req_exec_lck_resourceopen);
	req_exec_lck_resourceopen.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCEOPEN);

	api->ipc_source_set (&req_exec_lck_resourceopen.source, conn);

	memcpy (&req_exec_lck_resourceopen.resource_name,
		&req_lib_lck_resourceopen->resource_name,
		sizeof (mar_name_t));

	req_exec_lck_resourceopen.open_flags =
		req_lib_lck_resourceopen->open_flags;
	req_exec_lck_resourceopen.resource_handle =
		req_lib_lck_resourceopen->resource_handle;

	iovec.iov_base = (void *)&req_exec_lck_resourceopen;
	iovec.iov_len = sizeof (struct req_exec_lck_resourceopen);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_lck_resourceopenasync (
	void *conn,
	const void *msg)
{
	const struct req_lib_lck_resourceopenasync *req_lib_lck_resourceopenasync = msg;
	struct req_exec_lck_resourceopenasync req_exec_lck_resourceopenasync;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saLckResourceOpenAsync\n");

	req_exec_lck_resourceopenasync.header.size =
		sizeof (struct req_exec_lck_resourceopenasync);
	req_exec_lck_resourceopenasync.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCEOPENASYNC);

	api->ipc_source_set (&req_exec_lck_resourceopenasync.source, conn);

	memcpy (&req_exec_lck_resourceopenasync.resource_name,
		&req_lib_lck_resourceopenasync->resource_name,
		sizeof (mar_name_t));

	req_exec_lck_resourceopenasync.open_flags =
		req_lib_lck_resourceopenasync->open_flags;
	req_exec_lck_resourceopenasync.resource_handle =
		req_lib_lck_resourceopenasync->resource_handle;
	req_exec_lck_resourceopenasync.invocation =
		req_lib_lck_resourceopenasync->invocation;

	iovec.iov_base = (void *)&req_exec_lck_resourceopenasync;
	iovec.iov_len = sizeof (struct req_exec_lck_resourceopenasync);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_lck_resourceclose (
	void *conn,
	const void *msg)
{
	const struct req_lib_lck_resourceclose *req_lib_lck_resourceclose = msg;
	struct req_exec_lck_resourceclose req_exec_lck_resourceclose;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saLckResourceClose\n");

	req_exec_lck_resourceclose.header.size =
		sizeof (struct req_exec_lck_resourceclose);
	req_exec_lck_resourceclose.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCECLOSE);

	api->ipc_source_set (&req_exec_lck_resourceclose.source, conn);

	memcpy (&req_exec_lck_resourceclose.resource_name,
		&req_lib_lck_resourceclose->resource_name,
		sizeof (mar_name_t));

	req_exec_lck_resourceclose.resource_id =
		req_lib_lck_resourceclose->resource_id;

	req_exec_lck_resourceclose.exit_flag = 0;

	iovec.iov_base = (void *)&req_exec_lck_resourceclose;
	iovec.iov_len = sizeof (struct req_exec_lck_resourceclose);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_lck_resourcelock (
	void *conn,
	const void *msg)
{
	const struct req_lib_lck_resourcelock *req_lib_lck_resourcelock = msg;
	struct req_exec_lck_resourcelock req_exec_lck_resourcelock;
	struct resource_instance *resource_instance;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saLckResourceLock\n");

	req_exec_lck_resourcelock.header.size =
		sizeof (struct req_exec_lck_resourcelock);
	req_exec_lck_resourcelock.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCELOCK);

	api->ipc_source_set (&req_exec_lck_resourcelock.source, conn);

	memcpy (&req_exec_lck_resourcelock.resource_name,
		&req_lib_lck_resourcelock->resource_name,
		sizeof (mar_name_t));

	req_exec_lck_resourcelock.lock_id =
		req_lib_lck_resourcelock->lock_id;
	req_exec_lck_resourcelock.lock_mode =
		req_lib_lck_resourcelock->lock_mode;
	req_exec_lck_resourcelock.lock_flags =
		req_lib_lck_resourcelock->lock_flags;
	req_exec_lck_resourcelock.waiter_signal =
		req_lib_lck_resourcelock->waiter_signal;
	req_exec_lck_resourcelock.resource_handle =
		req_lib_lck_resourcelock->resource_handle;
/* 	req_exec_lck_resourcelock.resource_id = */
/* 		req_lib_lck_resourcelock->resource_id; */
	req_exec_lck_resourcelock.timeout =
		req_lib_lck_resourcelock->timeout;

	hdb_handle_get (&resource_hdb, req_lib_lck_resourcelock->resource_id,
		(void *)&resource_instance);

	memcpy (&req_exec_lck_resourcelock.callback_source,
		&resource_instance->source, sizeof (mar_message_source_t));

	hdb_handle_put (&resource_hdb, req_lib_lck_resourcelock->resource_id);

	iovec.iov_base = (void *)&req_exec_lck_resourcelock;
	iovec.iov_len = sizeof (struct req_exec_lck_resourcelock);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_lck_resourcelockasync (
	void *conn,
	const void *msg)
{
	const struct req_lib_lck_resourcelockasync *req_lib_lck_resourcelockasync = msg;
	struct req_exec_lck_resourcelockasync req_exec_lck_resourcelockasync;
	struct resource_instance *resource_instance;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saLckResourceLockAsync\n");

	req_exec_lck_resourcelockasync.header.size =
		sizeof (struct req_exec_lck_resourcelockasync);
	req_exec_lck_resourcelockasync.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCELOCKASYNC);

	api->ipc_source_set (&req_exec_lck_resourcelockasync.source, conn);

	memcpy (&req_exec_lck_resourcelockasync.resource_name,
		&req_lib_lck_resourcelockasync->resource_name,
		sizeof (mar_name_t));

	req_exec_lck_resourcelockasync.lock_id =
		req_lib_lck_resourcelockasync->lock_id;
	req_exec_lck_resourcelockasync.lock_mode =
		req_lib_lck_resourcelockasync->lock_mode;
	req_exec_lck_resourcelockasync.lock_flags =
		req_lib_lck_resourcelockasync->lock_flags;
	req_exec_lck_resourcelockasync.waiter_signal =
		req_lib_lck_resourcelockasync->waiter_signal;
	req_exec_lck_resourcelockasync.resource_handle =
		req_lib_lck_resourcelockasync->resource_handle;
/* 	req_exec_lck_resourcelockasync.resource_id = */
/* 		req_lib_lck_resourcelockasync->resource_id; */
	req_exec_lck_resourcelockasync.invocation =
		req_lib_lck_resourcelockasync->invocation;

	hdb_handle_get (&resource_hdb, req_lib_lck_resourcelockasync->resource_id,
		(void *)&resource_instance);

	memcpy (&req_exec_lck_resourcelockasync.callback_source,
		&resource_instance->source, sizeof (mar_message_source_t));

	hdb_handle_put (&resource_hdb, req_lib_lck_resourcelockasync->resource_id);

	iovec.iov_base = (void *)&req_exec_lck_resourcelockasync;
	iovec.iov_len = sizeof (struct req_exec_lck_resourcelockasync);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_lck_resourceunlock (
	void *conn,
	const void *msg)
{
	const struct req_lib_lck_resourceunlock *req_lib_lck_resourceunlock = msg;
	struct req_exec_lck_resourceunlock req_exec_lck_resourceunlock;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saLckResourceUnlock\n");

	req_exec_lck_resourceunlock.header.size =
		sizeof (struct req_exec_lck_resourceunlock);
	req_exec_lck_resourceunlock.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCEUNLOCK);

	api->ipc_source_set (&req_exec_lck_resourceunlock.source, conn);

	memcpy (&req_exec_lck_resourceunlock.resource_name,
		&req_lib_lck_resourceunlock->resource_name,
		sizeof (mar_name_t));

	req_exec_lck_resourceunlock.resource_handle =
		req_lib_lck_resourceunlock->resource_handle;
	req_exec_lck_resourceunlock.lock_id =
		req_lib_lck_resourceunlock->lock_id;

	iovec.iov_base = (void *)&req_exec_lck_resourceunlock;
	iovec.iov_len = sizeof (struct req_exec_lck_resourceunlock);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_lck_resourceunlockasync (
	void *conn,
	const void *msg)
{
	const struct req_lib_lck_resourceunlockasync *req_lib_lck_resourceunlockasync = msg;
	struct req_exec_lck_resourceunlockasync req_exec_lck_resourceunlockasync;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saLckResourceUnlockAsync\n");

	req_exec_lck_resourceunlockasync.header.size =
		sizeof (struct req_exec_lck_resourceunlockasync);
	req_exec_lck_resourceunlockasync.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_RESOURCEUNLOCKASYNC);

	api->ipc_source_set (&req_exec_lck_resourceunlockasync.source, conn);

	memcpy (&req_exec_lck_resourceunlockasync.resource_name,
		&req_lib_lck_resourceunlockasync->resource_name,
		sizeof (mar_name_t));

	req_exec_lck_resourceunlockasync.resource_handle =
		req_lib_lck_resourceunlockasync->resource_handle;
	req_exec_lck_resourceunlockasync.lock_id =
		req_lib_lck_resourceunlockasync->lock_id;
	req_exec_lck_resourceunlockasync.invocation =
		req_lib_lck_resourceunlockasync->invocation;

	iovec.iov_base = (void *)&req_exec_lck_resourceunlockasync;
	iovec.iov_len = sizeof (struct req_exec_lck_resourceunlockasync);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_lck_lockpurge (
	void *conn,
	const void *msg)
{
	const struct req_lib_lck_lockpurge *req_lib_lck_lockpurge = msg;
	struct req_exec_lck_lockpurge req_exec_lck_lockpurge;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saLckLockPurge\n");

	req_exec_lck_lockpurge.header.size =
		sizeof (struct req_exec_lck_lockpurge);
	req_exec_lck_lockpurge.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_LOCKPURGE);

	api->ipc_source_set (&req_exec_lck_lockpurge.source, conn);

	memcpy (&req_exec_lck_lockpurge.resource_name,
		&req_lib_lck_lockpurge->resource_name,
		sizeof (mar_name_t));

	iovec.iov_base = (void *)&req_exec_lck_lockpurge;
	iovec.iov_len = sizeof (struct req_exec_lck_lockpurge);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}

static void message_handler_req_lib_lck_limitget (
	void *conn,
	const void *msg)
{
	const struct req_lib_lck_limitget *req_lib_lck_limitget = msg;
	struct req_exec_lck_limitget req_exec_lck_limitget;
	struct iovec iovec;

	/* DEBUG */
	log_printf (LOGSYS_LEVEL_DEBUG, "LIB request: saLckLimitGet\n");

	req_exec_lck_limitget.header.size =
		sizeof (struct req_exec_lck_limitget);
	req_exec_lck_limitget.header.id =
		SERVICE_ID_MAKE (LCK_SERVICE, MESSAGE_REQ_EXEC_LCK_LIMITGET);

	api->ipc_source_set (&req_exec_lck_limitget.source, conn);

	req_exec_lck_limitget.limit_id = req_lib_lck_limitget->limit_id;

	iovec.iov_base = (void *)&req_exec_lck_limitget;
	iovec.iov_len = sizeof (struct req_exec_lck_limitget);

	assert (api->totem_mcast (&iovec, 1, TOTEM_AGREED) == 0);
}
