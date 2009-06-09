/*
 * Copyright (c) 2006 Red Hat, Inc.
 * Copyright (c) 2006 Sun Microsystems, Inc.
 *
 * All rights reserved.
 *
 * Author: Patrick Caulfield (pcaulfie@redhat.com)
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTIBUTORS "AS IS"
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
#ifndef OPENAIS_BSD
#include <alloca.h>
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../include/saAis.h"
#include "../include/cpg.h"
#include "../include/saClm.h"
#include "../include/ipc_gen.h"
#include "../include/ipc_cpg.h"
#include "../include/mar_cpg.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "../lcr/lcr_comp.h"
#include "totempg.h"
#include "totemip.h"
#include "main.h"
#include "tlist.h"
#include "ipc.h"
#include "mempool.h"
#include "objdb.h"
#include "service.h"
#include "jhash.h"
#include "swab.h"
#include "ipc.h"
#include "print.h"

#define GROUP_HASH_SIZE 32

enum cpg_message_req_types {
	MESSAGE_REQ_EXEC_CPG_PROCJOIN = 0,
	MESSAGE_REQ_EXEC_CPG_PROCLEAVE = 1,
	MESSAGE_REQ_EXEC_CPG_JOINLIST = 2,
	MESSAGE_REQ_EXEC_CPG_MCAST = 3,
	MESSAGE_REQ_EXEC_CPG_DOWNLIST = 4
};

/*
 * state`		exec deliver
 * match group name, pid -> if matched deliver for YES:
 * XXX indicates impossible state
 *
 *			join			leave			mcast
 * UNJOINED		XXX			XXX			NO
 * LEAVE_STARTED	XXX			YES(unjoined_enter)	YES
 * JOIN_STARTED		YES(join_started_enter)	XXX			NO
 * JOIN_COMPLETED	XXX			NO			YES
 *
 * join_started_enter
 * 	set JOIN_COMPLETED
 *	add entry to process_info list
 * unjoined_enter
 *	set UNJOINED
 *	delete entry from process_info list
 *
 *
 *			library accept join error codes
 * UNJOINED		YES(CPG_OK) 			set JOIN_STARTED
 * LEAVE_STARTED	NO(CPG_ERR_BUSY)
 * JOIN_STARTED		NO(CPG_ERR_EXIST)
 * JOIN_COMPlETED	NO(CPG_ERR_EXIST)
 *
 *			library accept leave error codes
 * UNJOINED		NO(CPG_ERR_NOT_EXIST)
 * LEAVE_STARTED	NO(CPG_ERR_NOT_EXIST)
 * JOIN_STARTED		NO(CPG_ERR_BUSY)
 * JOIN_COMPLETED	YES(CPG_OK)			set LEAVE_STARTED
 *
 *			library accept mcast
 * UNJOINED		NO(CPG_ERR_NOT_EXIST)
 * LEAVE_STARTED	NO(CPG_ERR_NOT_EXIST)
 * JOIN_STARTED		YES(CPG_OK)
 * JOIN_COMPLETED	YES(CPG_OK)
 */
enum cpd_state {
	CPD_STATE_UNJOINED,
	CPD_STATE_LEAVE_STARTED,
	CPD_STATE_JOIN_STARTED,
	CPD_STATE_JOIN_COMPLETED
};

struct cpg_pd {
	void *conn;
	mar_cpg_name_t group_name;
	uint32_t pid;
	enum cpd_state cpd_state;
	struct list_head list;
};
DECLARE_LIST_INIT(cpg_pd_list_head);

struct process_info {
	unsigned int nodeid;
	uint32_t pid;
	mar_cpg_name_t group;
	struct list_head list; /* on the group_info members list */
};
DECLARE_LIST_INIT(process_info_list_head);

struct join_list_entry {
	uint32_t pid;
	mar_cpg_name_t group_name;
};

/*
 * Service Interfaces required by service_message_handler struct
 */
static void cpg_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id);

static int cpg_exec_init_fn (struct objdb_iface_ver0 *objdb);

static int cpg_lib_init_fn (void *conn);

static int cpg_lib_exit_fn (void *conn);

static void message_handler_req_exec_cpg_procjoin (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_cpg_procleave (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_cpg_joinlist (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_cpg_mcast (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_cpg_downlist (
	void *message,
	unsigned int nodeid);

static void exec_cpg_procjoin_endian_convert (void *msg);

static void exec_cpg_joinlist_endian_convert (void *msg);

static void exec_cpg_mcast_endian_convert (void *msg);

static void exec_cpg_downlist_endian_convert (void *msg);

static void message_handler_req_lib_cpg_join (void *conn, void *message);

static void message_handler_req_lib_cpg_leave (void *conn, void *message);

static void message_handler_req_lib_cpg_mcast (void *conn, void *message);

static void message_handler_req_lib_cpg_membership (void *conn, void *message);

static void message_handler_req_lib_cpg_local_get (void *conn, void *message);

static int cpg_node_joinleave_send (unsigned int pid, mar_cpg_name_t *group_name, int fn, int reason);

static int cpg_exec_send_joinlist(void);

static void cpg_sync_init (void);
static int  cpg_sync_process (void);
static void cpg_sync_activate (void);
static void cpg_sync_abort (void);
/*
 * Library Handler Definition
 */
static struct openais_lib_handler cpg_lib_service[] =
{
	{ /* 0 */
		.lib_handler_fn				= message_handler_req_lib_cpg_join,
		.response_size				= sizeof (struct res_lib_cpg_join),
		.response_id				= MESSAGE_RES_CPG_JOIN,
		.flow_control				= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn				= message_handler_req_lib_cpg_leave,
		.response_size				= sizeof (struct res_lib_cpg_leave),
		.response_id				= MESSAGE_RES_CPG_LEAVE,
		.flow_control				= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn				= message_handler_req_lib_cpg_mcast,
		.response_size				= sizeof (struct res_lib_cpg_mcast),
		.response_id				= MESSAGE_RES_CPG_MCAST,
		.flow_control				= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn				= message_handler_req_lib_cpg_membership,
		.response_size				= sizeof (mar_res_header_t),
		.response_id				= MESSAGE_RES_CPG_MEMBERSHIP,
		.flow_control				= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn				= message_handler_req_lib_cpg_local_get,
		.response_size				= sizeof (struct res_lib_cpg_local_get),
		.response_id				= MESSAGE_RES_CPG_LOCAL_GET,
		.flow_control				= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	}
};

static struct openais_exec_handler cpg_exec_service[] =
{
	{ /* 0 */
		.exec_handler_fn	= message_handler_req_exec_cpg_procjoin,
		.exec_endian_convert_fn	= exec_cpg_procjoin_endian_convert
	},
	{ /* 1 */
		.exec_handler_fn	= message_handler_req_exec_cpg_procleave,
		.exec_endian_convert_fn	= exec_cpg_procjoin_endian_convert
	},
	{ /* 2 */
		.exec_handler_fn	= message_handler_req_exec_cpg_joinlist,
		.exec_endian_convert_fn	= exec_cpg_joinlist_endian_convert
	},
	{ /* 3 */
		.exec_handler_fn	= message_handler_req_exec_cpg_mcast,
		.exec_endian_convert_fn	= exec_cpg_mcast_endian_convert
	},
	{ /* 4 */
		.exec_handler_fn	= message_handler_req_exec_cpg_downlist,
		.exec_endian_convert_fn	= exec_cpg_downlist_endian_convert
	},
};

struct openais_service_handler cpg_service_handler = {
	.name				        = (unsigned char *)"openais cluster closed process group service v1.01",
	.id					= CPG_SERVICE,
	.private_data_size			= sizeof (struct cpg_pd),
	.flow_control				= OPENAIS_FLOW_CONTROL_REQUIRED,
	.lib_init_fn				= cpg_lib_init_fn,
	.lib_exit_fn				= cpg_lib_exit_fn,
	.lib_service				= cpg_lib_service,
	.lib_service_count			= sizeof (cpg_lib_service) / sizeof (struct openais_lib_handler),
	.exec_init_fn				= cpg_exec_init_fn,
	.exec_dump_fn				= NULL,
	.exec_service				= cpg_exec_service,
	.exec_service_count		        = sizeof (cpg_exec_service) / sizeof (struct openais_exec_handler),
	.confchg_fn                             = cpg_confchg_fn,
	.sync_init                              = cpg_sync_init,
	.sync_process                           = cpg_sync_process,
	.sync_activate                          = cpg_sync_activate,
	.sync_abort                             = cpg_sync_abort
};

/*
 * Dynamic loader definition
 */
static struct openais_service_handler *cpg_get_service_handler_ver0 (void);

static struct openais_service_handler_iface_ver0 cpg_service_handler_iface = {
	.openais_get_service_handler_ver0		= cpg_get_service_handler_ver0
};

static struct lcr_iface openais_cpg_ver0[1] = {
	{
		.name				= "openais_cpg",
		.version			= 0,
		.versions_replace		= 0,
		.versions_replace_count         = 0,
		.dependencies			= 0,
		.dependency_count		= 0,
		.constructor			= NULL,
		.destructor			= NULL,
		.interfaces			= NULL
	}
};

static struct lcr_comp cpg_comp_ver0 = {
	.iface_count			= 1,
	.ifaces			        = openais_cpg_ver0
};


static struct openais_service_handler *cpg_get_service_handler_ver0 (void)
{
	return (&cpg_service_handler);
}

__attribute__ ((constructor)) static void cpg_comp_register (void) {
        lcr_interfaces_set (&openais_cpg_ver0[0], &cpg_service_handler_iface);

	lcr_component_register (&cpg_comp_ver0);
}

struct req_exec_cpg_procjoin {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_cpg_name_t group_name __attribute__((aligned(8)));
	mar_uint32_t pid __attribute__((aligned(8)));
	mar_uint32_t reason __attribute__((aligned(8)));
};

struct req_exec_cpg_mcast {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_cpg_name_t group_name __attribute__((aligned(8)));
	mar_uint32_t msglen __attribute__((aligned(8)));
	mar_uint32_t pid __attribute__((aligned(8)));
	mar_message_source_t source __attribute__((aligned(8)));
	mar_uint8_t message[] __attribute__((aligned(8)));
};

struct req_exec_cpg_downlist {
	mar_req_header_t header __attribute__((aligned(8)));
	mar_uint32_t left_nodes __attribute__((aligned(8)));
	mar_uint32_t nodeids[PROCESSOR_COUNT_MAX]  __attribute__((aligned(8)));
};

static struct req_exec_cpg_downlist req_exec_cpg_downlist;

static void cpg_sync_init (void)
{
}

static int cpg_sync_process (void)
{
	return cpg_exec_send_joinlist();
}

static void cpg_sync_activate (void)
{

}
static void cpg_sync_abort (void)
{

}

static int notify_lib_joinlist(
	mar_cpg_name_t *group_name,
	void *conn,
	int joined_list_entries,
	mar_cpg_address_t *joined_list,
	int left_list_entries,
	mar_cpg_address_t *left_list,
	int id)
{
	int size;
	char *buf;
	struct list_head *iter;
	int count;
	struct res_lib_cpg_confchg_callback *res;
	mar_cpg_address_t *retgi;

	count = 0;

	for (iter = process_info_list_head.next; iter != &process_info_list_head; iter = iter->next) {
		struct process_info *pi = list_entry (iter, struct process_info, list);
		if (mar_name_compare (&pi->group, group_name) == 0) {
			int i;
			int founded = 0;

			for (i = 0; i < left_list_entries; i++) {
				if (left_list[i].nodeid == pi->nodeid && left_list[i].pid == pi->pid) {
					founded++;
				}
			}

			if (!founded)
				count++;
		}
	}
	size = sizeof(struct res_lib_cpg_confchg_callback) +
		sizeof(mar_cpg_address_t) * (count + left_list_entries + joined_list_entries);
	buf = alloca(size);
	if (!buf)
		return CPG_ERR_LIBRARY;

	res = (struct res_lib_cpg_confchg_callback *)buf;
	res->joined_list_entries = joined_list_entries;
	res->left_list_entries = left_list_entries;
	res->member_list_entries = count;
	retgi = res->member_list;
	res->header.size = size;
	res->header.id = id;
	memcpy(&res->group_name, group_name, sizeof(mar_cpg_name_t));

	for (iter = process_info_list_head.next; iter != &process_info_list_head; iter = iter->next) {
		struct process_info *pi=list_entry (iter, struct process_info, list);

		if (mar_name_compare (&pi->group, group_name) == 0) {
			int i;
			int founded = 0;

			for (i = 0;i < left_list_entries; i++) {
				if (left_list[i].nodeid == pi->nodeid && left_list[i].pid == pi->pid) {
					founded++;
				}
			}

			if (!founded) {
				retgi->nodeid = pi->nodeid;
				retgi->pid = pi->pid;
				retgi++;
			}
		}
	}

	if (left_list_entries) {
		memcpy (retgi, left_list, left_list_entries * sizeof(mar_cpg_address_t));
		retgi += left_list_entries;
	}

	if (joined_list_entries) {
		memcpy (retgi, joined_list, joined_list_entries * sizeof(mar_cpg_address_t));
		retgi += joined_list_entries;
	}

	if (conn) {
		openais_dispatch_send (conn, buf, size);
	} else {
		for (iter = cpg_pd_list_head.next; iter != &cpg_pd_list_head; iter = iter->next) {
			struct cpg_pd *cpd = list_entry (iter, struct cpg_pd, list);
			if (mar_name_compare (&cpd->group_name, group_name) == 0) {
				assert (left_list_entries <= 1);
				assert (joined_list_entries <= 1);
				if (joined_list_entries) {
					if (joined_list[0].pid == cpd->pid &&
						joined_list[0].nodeid == totempg_my_nodeid_get()) {
						cpd->cpd_state = CPD_STATE_JOIN_COMPLETED;
					}
				}
				if (cpd->cpd_state == CPD_STATE_JOIN_COMPLETED ||
					cpd->cpd_state == CPD_STATE_LEAVE_STARTED) {
					openais_dispatch_send (cpd->conn, buf, size);
				}
				if (left_list_entries) {
					if (left_list[0].pid == cpd->pid &&
						left_list[0].nodeid == totempg_my_nodeid_get()) {
						
						cpd->pid = 0;
						memset (&cpd->group_name, 0, sizeof(cpd->group_name));
						cpd->cpd_state = CPD_STATE_UNJOINED;
					}
				}
			}
		}
	}

	return CPG_OK;
}

static int cpg_exec_init_fn (struct objdb_iface_ver0 *objdb)
{
	log_init ("CPG");

	return (0);
}

static int cpg_lib_exit_fn (void *conn)
{
	struct cpg_pd *cpd = (struct cpg_pd *)openais_conn_private_data_get (conn);

	if (cpd->group_name.length > 0) {
		cpg_node_joinleave_send (cpd->pid, &cpd->group_name,
				MESSAGE_REQ_EXEC_CPG_PROCLEAVE, CONFCHG_CPG_REASON_LEAVE);
	}
	list_del (&cpd->list);
	openais_conn_refcount_dec (conn);
	return (0);
}

static int cpg_node_joinleave_send (unsigned int pid, mar_cpg_name_t *group_name, int fn, int reason)
{
	struct req_exec_cpg_procjoin req_exec_cpg_procjoin;
	struct iovec req_exec_cpg_iovec;
	int result;

	memcpy(&req_exec_cpg_procjoin.group_name, group_name, sizeof(mar_cpg_name_t));
	req_exec_cpg_procjoin.pid = pid;
	req_exec_cpg_procjoin.reason = reason;

	req_exec_cpg_procjoin.header.size = sizeof(req_exec_cpg_procjoin);
	req_exec_cpg_procjoin.header.id = SERVICE_ID_MAKE(CPG_SERVICE, fn);

	req_exec_cpg_iovec.iov_base = (char *)&req_exec_cpg_procjoin;
	req_exec_cpg_iovec.iov_len = sizeof(req_exec_cpg_procjoin);

	result = totempg_groups_mcast_joined (openais_group_handle, &req_exec_cpg_iovec, 1, TOTEMPG_AGREED);

	return (result);
}

static void cpg_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{
	int i;
	uint32_t lowest_nodeid = 0xffffffff;
	struct iovec req_exec_cpg_iovec;

	/* We don't send the library joinlist in here because it can end up
	   out of order with the rest of the messages (which are totem ordered).
	   So we get the lowest nodeid to send out a list of left nodes instead.
	   On receipt of that message, all nodes will then notify their local clients
	   of the new joinlist */

	if (left_list_entries) {
		for (i = 0; i < member_list_entries; i++) {
			if (member_list[i] < lowest_nodeid)
				lowest_nodeid = member_list[i];
		}

		log_printf(LOG_LEVEL_DEBUG, "confchg, low nodeid=%d, us = %d\n", lowest_nodeid, totempg_my_nodeid_get());
		if (lowest_nodeid == totempg_my_nodeid_get()) {

			req_exec_cpg_downlist.header.id = SERVICE_ID_MAKE(CPG_SERVICE, MESSAGE_REQ_EXEC_CPG_DOWNLIST);
			req_exec_cpg_downlist.header.size = sizeof(struct req_exec_cpg_downlist);

			req_exec_cpg_downlist.left_nodes = left_list_entries;
			for (i = 0; i < left_list_entries; i++) {
				req_exec_cpg_downlist.nodeids[i] = left_list[i];
			}
			log_printf(LOG_LEVEL_DEBUG, "confchg, build downlist: %d nodes\n", left_list_entries);
		}
	}

	/* Don't send this message until we get the final configuration message */
	if (configuration_type == TOTEM_CONFIGURATION_REGULAR && req_exec_cpg_downlist.left_nodes) {
		req_exec_cpg_iovec.iov_base = (char *)&req_exec_cpg_downlist;
		req_exec_cpg_iovec.iov_len = req_exec_cpg_downlist.header.size;

		totempg_groups_mcast_joined (openais_group_handle, &req_exec_cpg_iovec, 1, TOTEMPG_AGREED);
		req_exec_cpg_downlist.left_nodes = 0;
		log_printf(LOG_LEVEL_DEBUG, "confchg, sent downlist\n");
	}
}

/* Can byteswap join & leave messages */
static void exec_cpg_procjoin_endian_convert (void *msg)
{
	struct req_exec_cpg_procjoin *req_exec_cpg_procjoin = (struct req_exec_cpg_procjoin *)msg;

	req_exec_cpg_procjoin->pid = swab32(req_exec_cpg_procjoin->pid);
	swab_mar_cpg_name_t (&req_exec_cpg_procjoin->group_name);
	req_exec_cpg_procjoin->reason = swab32(req_exec_cpg_procjoin->reason);
}

static void exec_cpg_joinlist_endian_convert (void *msg)
{
	mar_res_header_t *res = (mar_res_header_t *)msg;
	struct join_list_entry *jle = (struct join_list_entry *)(msg + sizeof(mar_res_header_t));

	/* XXX shouldn't mar_res_header be swabbed? */

	while ((void*)jle < msg + res->size) {
		jle->pid = swab32(jle->pid);
		swab_mar_cpg_name_t (&jle->group_name);
		jle++;
	}
}

static void exec_cpg_downlist_endian_convert (void *msg)
{
	struct req_exec_cpg_downlist *req_exec_cpg_downlist = (struct req_exec_cpg_downlist *)msg;
	unsigned int i;

	req_exec_cpg_downlist->left_nodes = swab32(req_exec_cpg_downlist->left_nodes);

	for (i = 0; i < req_exec_cpg_downlist->left_nodes; i++) {
		req_exec_cpg_downlist->nodeids[i] = swab32(req_exec_cpg_downlist->nodeids[i]);
	}
}


static void exec_cpg_mcast_endian_convert (void *msg)
{
	struct req_exec_cpg_mcast *req_exec_cpg_mcast = (struct req_exec_cpg_mcast *)msg;

	swab_mar_req_header_t (&req_exec_cpg_mcast->header);
	swab_mar_cpg_name_t (&req_exec_cpg_mcast->group_name);
	req_exec_cpg_mcast->pid = swab32(req_exec_cpg_mcast->pid);
	req_exec_cpg_mcast->msglen = swab32(req_exec_cpg_mcast->msglen);
	swab_mar_message_source_t (&req_exec_cpg_mcast->source);
}

struct process_info *process_info_find(mar_cpg_name_t *group_name, uint32_t pid, unsigned int nodeid) {
	struct list_head *iter;

	for (iter = process_info_list_head.next; iter != &process_info_list_head; ) {
		struct process_info *pi = list_entry (iter, struct process_info, list);
		iter = iter->next;

		if (pi->pid == pid && pi->nodeid == nodeid &&
			mar_name_compare (&pi->group, group_name) == 0) {
				return pi;
		}
	}

	return NULL;
}

static void do_proc_join(
	mar_cpg_name_t *name,
	uint32_t pid,
	unsigned int nodeid,
	int reason)
{
	struct process_info *pi;
	struct process_info *pi_entry;
	mar_cpg_address_t notify_info;
	struct list_head *list;
	struct list_head *list_to_add = NULL;

	if (process_info_find(name, pid, nodeid) != NULL) {
		return ;
	}

	pi = malloc (sizeof (struct process_info));
	if (!pi) {
		log_printf(LOG_LEVEL_WARNING, "Unable to allocate process_info struct");
		return;
	}
	pi->nodeid = nodeid;
	pi->pid = pid;
	memcpy(&pi->group, name, sizeof(*name));
	list_init(&pi->list);

	/*
	 * Insert new process in sorted order so synchronization works properly
	 */
	list_to_add = &process_info_list_head;
	for (list = process_info_list_head.next; list != &process_info_list_head; list = list->next) {

		pi_entry = list_entry(list, struct process_info, list);
		if (pi_entry->nodeid > pi->nodeid ||
			(pi_entry->nodeid == pi->nodeid && pi_entry->pid > pi->pid)) {

			break;
		}
		list_to_add = list;
	}
	list_splice (&pi->list, list_to_add);

	notify_info.pid = pi->pid;
	notify_info.nodeid = nodeid;
	notify_info.reason = reason;

	notify_lib_joinlist(&pi->group, NULL,
			    1, &notify_info,
			    0, NULL,
			    MESSAGE_RES_CPG_CONFCHG_CALLBACK);
}

static void message_handler_req_exec_cpg_downlist (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_cpg_downlist *req_exec_cpg_downlist = (struct req_exec_cpg_downlist *)message;
	int i;
	mar_cpg_address_t left_list[1];
	struct list_head *iter;

	/*
		FOR OPTIMALIZATION - Make list of lists
	*/

	log_printf (LOG_LEVEL_DEBUG, "downlist left_list: %d\n", req_exec_cpg_downlist->left_nodes);

	for (iter = process_info_list_head.next; iter != &process_info_list_head; ) {
		struct process_info *pi = list_entry(iter, struct process_info, list);
		iter = iter->next;

		for (i = 0; i < req_exec_cpg_downlist->left_nodes; i++) {
			if (pi->nodeid == req_exec_cpg_downlist->nodeids[i]) {
				left_list[0].nodeid = pi->nodeid;
				left_list[0].pid = pi->pid;
				left_list[0].reason = CONFCHG_CPG_REASON_NODEDOWN;

				notify_lib_joinlist(&pi->group, NULL,
                                	            0, NULL,
                                        	    1, left_list,
	                                            MESSAGE_RES_CPG_CONFCHG_CALLBACK);
				list_del (&pi->list);
				free (pi);
			}
		}
	}
}

static void message_handler_req_exec_cpg_procjoin (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_cpg_procjoin *req_exec_cpg_procjoin = (struct req_exec_cpg_procjoin *)message;

	log_printf(LOG_LEVEL_DEBUG, "got procjoin message from cluster node %d\n", nodeid);

	do_proc_join (&req_exec_cpg_procjoin->group_name,
		req_exec_cpg_procjoin->pid, nodeid,
		CONFCHG_CPG_REASON_JOIN);
}

static void message_handler_req_exec_cpg_procleave (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_cpg_procjoin *req_exec_cpg_procjoin = (struct req_exec_cpg_procjoin *)message;
	struct process_info *pi;
	volatile struct list_head *iter;
	mar_cpg_address_t notify_info;

	log_printf(LOG_LEVEL_DEBUG, "got procleave message from cluster node %d\n", nodeid);

	notify_info.pid = req_exec_cpg_procjoin->pid;
	notify_info.nodeid = nodeid;
	notify_info.reason = req_exec_cpg_procjoin->reason;

	notify_lib_joinlist(&req_exec_cpg_procjoin->group_name, NULL,
		0, NULL,
		1, &notify_info,
		MESSAGE_RES_CPG_CONFCHG_CALLBACK);

        for (iter = process_info_list_head.next; iter != &process_info_list_head; ) {
		pi = list_entry(iter, struct process_info, list);
		iter = iter->next;
		if (pi->pid == req_exec_cpg_procjoin->pid && pi->nodeid == nodeid &&
			mar_name_compare (&pi->group, &req_exec_cpg_procjoin->group_name)==0) {
			list_del (&pi->list);
			free (pi);
		}

	}
}


/* Got a proclist from another node */
static void message_handler_req_exec_cpg_joinlist (
	void *message,
	unsigned int nodeid)
{
	mar_res_header_t *res = (mar_res_header_t *)message;
	struct join_list_entry *jle = (struct join_list_entry *)(message + sizeof(mar_res_header_t));

	log_printf(LOG_LEVEL_NOTICE, "got joinlist message from node %d\n",
		nodeid);

	/* Ignore our own messages */
	if (nodeid == totempg_my_nodeid_get()) {
		return;
	}

	while ((void*)jle < message + res->size) {
		do_proc_join (&jle->group_name, jle->pid, nodeid,
			CONFCHG_CPG_REASON_NODEUP);
		jle++;
	}
}

static void message_handler_req_exec_cpg_mcast (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_cpg_mcast *req_exec_cpg_mcast = (struct req_exec_cpg_mcast *)message;
        struct res_lib_cpg_deliver_callback res_lib_cpg_mcast;
	int msglen = req_exec_cpg_mcast->msglen;
	struct list_head *iter;
	struct cpg_pd *cpd;
	struct iovec iovec[2];

	res_lib_cpg_mcast.header.id = MESSAGE_RES_CPG_DELIVER_CALLBACK;
	res_lib_cpg_mcast.header.size = sizeof(res_lib_cpg_mcast) + msglen;
	res_lib_cpg_mcast.msglen = msglen;
	res_lib_cpg_mcast.pid = req_exec_cpg_mcast->pid;
	res_lib_cpg_mcast.nodeid = nodeid;

	memcpy(&res_lib_cpg_mcast.group_name, &req_exec_cpg_mcast->group_name,
		sizeof(mar_cpg_name_t));

	iovec[0].iov_base = &res_lib_cpg_mcast;
	iovec[0].iov_len = sizeof (res_lib_cpg_mcast);

	iovec[1].iov_base = (char*)message+sizeof(*req_exec_cpg_mcast);
	iovec[1].iov_len = msglen;

	for (iter = cpg_pd_list_head.next; iter != &cpg_pd_list_head; ) {
		cpd = list_entry(iter, struct cpg_pd, list);
		iter = iter->next;

		if ((cpd->cpd_state == CPD_STATE_LEAVE_STARTED || cpd->cpd_state == CPD_STATE_JOIN_COMPLETED)
			&& (mar_name_compare (&cpd->group_name, &req_exec_cpg_mcast->group_name) == 0)) {
			openais_dispatch_iov_send (cpd->conn, iovec, 2);
		}
        }
}


static int cpg_exec_send_joinlist(void)
{
	int count = 0;
	struct list_head *iter;
	mar_res_header_t *res;
	struct join_list_entry *jle;
	char *buf;
	struct iovec req_exec_cpg_iovec;

	for (iter = process_info_list_head.next; iter != &process_info_list_head; iter = iter->next) {
		struct process_info *pi = list_entry (iter, struct process_info, list);

		if (pi->nodeid == totempg_my_nodeid_get ()) {
			count++;
		}
	}

	/* Nothing to send */
	if (!count)
		return 0;

	buf = alloca (sizeof (mar_res_header_t) + sizeof (struct join_list_entry) * count);
	if (!buf) {
		log_printf(LOG_LEVEL_WARNING, "Unable to allocate joinlist buffer");
		return -1;
	}

	jle = (struct join_list_entry *)(buf + sizeof(mar_res_header_t));
	res = (mar_res_header_t *)buf;

	for (iter = process_info_list_head.next; iter != &process_info_list_head; iter = iter->next) {
		struct process_info *pi = list_entry (iter, struct process_info, list);

		if (pi->nodeid == totempg_my_nodeid_get ()) {
			memcpy (&jle->group_name, &pi->group, sizeof (mar_cpg_name_t));
			jle->pid = pi->pid;
			jle++;
		}
	}

	res->id = SERVICE_ID_MAKE(CPG_SERVICE, MESSAGE_REQ_EXEC_CPG_JOINLIST);
	res->size = sizeof (mar_res_header_t) + sizeof (struct join_list_entry) * count;

	req_exec_cpg_iovec.iov_base = buf;
	req_exec_cpg_iovec.iov_len = res->size;

	return totempg_groups_mcast_joined (openais_group_handle, &req_exec_cpg_iovec, 1, TOTEMPG_AGREED);
}

static int cpg_lib_init_fn (void *conn)
{
	struct cpg_pd *cpd = (struct cpg_pd *)openais_conn_private_data_get (conn);
	memset (cpd, 0, sizeof(struct cpg_pd));
	cpd->conn = conn;
	list_add (&cpd->list, &cpg_pd_list_head);

	openais_conn_refcount_inc (conn);
	log_printf(LOG_LEVEL_DEBUG, "lib_init_fn: conn=%p, cpd=%p\n", conn, cpd);
	return (0);
}

/*
 * Join message from the library
 */
static void message_handler_req_lib_cpg_join (void *conn, void *message)
{
	struct req_lib_cpg_join *req_lib_cpg_join = (struct req_lib_cpg_join *)message;
	struct cpg_pd *cpd = (struct cpg_pd *)openais_conn_private_data_get (conn);
	struct res_lib_cpg_join res_lib_cpg_join;
	SaAisErrorT error = CPG_OK;

	switch (cpd->cpd_state) {
	case CPD_STATE_UNJOINED:
		error = CPG_OK;
		cpd->cpd_state = CPD_STATE_JOIN_STARTED;
		cpd->pid = req_lib_cpg_join->pid;
		memcpy (&cpd->group_name, &req_lib_cpg_join->group_name,
			sizeof (cpd->group_name));

		cpg_node_joinleave_send (req_lib_cpg_join->pid,
			&req_lib_cpg_join->group_name,
			MESSAGE_REQ_EXEC_CPG_PROCJOIN, CONFCHG_CPG_REASON_JOIN);
		break;
	case CPD_STATE_LEAVE_STARTED:
		error = CPG_ERR_BUSY;
		break;
	case CPD_STATE_JOIN_STARTED:
		error = CPG_ERR_EXIST;
		break;
	case CPD_STATE_JOIN_COMPLETED:
		error = CPG_ERR_EXIST;
		break;
	}

	res_lib_cpg_join.header.size = sizeof(res_lib_cpg_join);
        res_lib_cpg_join.header.id = MESSAGE_RES_CPG_JOIN;
        res_lib_cpg_join.header.error = error;
        openais_response_send (conn, &res_lib_cpg_join, sizeof(res_lib_cpg_join));
}


/*
 * Leave message from the library
 */
static void message_handler_req_lib_cpg_leave (void *conn, void *message)
{
	struct res_lib_cpg_leave res_lib_cpg_leave;
	SaAisErrorT error = CPG_OK;
	struct req_lib_cpg_leave  *req_lib_cpg_leave = (struct req_lib_cpg_leave *)message;
	struct cpg_pd *cpd = (struct cpg_pd *)openais_conn_private_data_get (conn);

	switch (cpd->cpd_state) {
	case CPD_STATE_UNJOINED:
		error = CPG_ERR_NOT_EXIST;
		break;
	case CPD_STATE_LEAVE_STARTED:
		error = CPG_ERR_NOT_EXIST;
		break;
	case CPD_STATE_JOIN_STARTED:
		error = CPG_ERR_BUSY;
		break;
	case CPD_STATE_JOIN_COMPLETED:
		error = CPG_OK;
		cpd->cpd_state = CPD_STATE_LEAVE_STARTED;
		cpg_node_joinleave_send (req_lib_cpg_leave->pid,
			&req_lib_cpg_leave->group_name,
			MESSAGE_REQ_EXEC_CPG_PROCLEAVE,
			CONFCHG_CPG_REASON_LEAVE);
		break;
	}

	/* send return */
	res_lib_cpg_leave.header.size = sizeof(res_lib_cpg_leave);
	res_lib_cpg_leave.header.id = MESSAGE_RES_CPG_LEAVE;
	res_lib_cpg_leave.header.error = error;
	openais_response_send(conn, &res_lib_cpg_leave, sizeof(res_lib_cpg_leave));
}

/* Mcast message from the library */
static void message_handler_req_lib_cpg_mcast (void *conn, void *message)
{
	struct req_lib_cpg_mcast *req_lib_cpg_mcast = (struct req_lib_cpg_mcast *)message;
	struct cpg_pd *cpd = (struct cpg_pd *)openais_conn_private_data_get (conn);
        mar_cpg_name_t group_name = cpd->group_name;

	struct iovec req_exec_cpg_iovec[2];
	struct req_exec_cpg_mcast req_exec_cpg_mcast;
	struct res_lib_cpg_mcast res_lib_cpg_mcast;
	int msglen = req_lib_cpg_mcast->msglen;
	int result;
	SaAisErrorT error = CPG_OK;

	log_printf(LOG_LEVEL_DEBUG, "got mcast request on %p\n", conn);

	switch (cpd->cpd_state) {
	case CPD_STATE_UNJOINED:
		error = CPG_ERR_NOT_EXIST;
		break;
	case CPD_STATE_LEAVE_STARTED:
		error = CPG_ERR_NOT_EXIST;
		break;
	case CPD_STATE_JOIN_STARTED:
		error = CPG_OK;
		break;
	case CPD_STATE_JOIN_COMPLETED:
		error = CPG_OK;
		break;
	}

	if (error == CPG_OK) {
		req_exec_cpg_mcast.header.size = sizeof(req_exec_cpg_mcast) + msglen;
		req_exec_cpg_mcast.header.id = SERVICE_ID_MAKE(CPG_SERVICE,
			MESSAGE_REQ_EXEC_CPG_MCAST);
		req_exec_cpg_mcast.pid = cpd->pid;
		req_exec_cpg_mcast.msglen = msglen;
		message_source_set (&req_exec_cpg_mcast.source, conn);
		memcpy(&req_exec_cpg_mcast.group_name, &group_name,
			sizeof(mar_cpg_name_t));

		req_exec_cpg_iovec[0].iov_base = (char *)&req_exec_cpg_mcast;
		req_exec_cpg_iovec[0].iov_len = sizeof(req_exec_cpg_mcast);
		req_exec_cpg_iovec[1].iov_base = (char *)&req_lib_cpg_mcast->message;
		req_exec_cpg_iovec[1].iov_len = msglen;

		result = totempg_groups_mcast_joined (openais_group_handle,
			req_exec_cpg_iovec, 2, TOTEMPG_AGREED);
		assert(result == 0);
	}

	res_lib_cpg_mcast.header.size = sizeof(res_lib_cpg_mcast);
	res_lib_cpg_mcast.header.id = MESSAGE_RES_CPG_MCAST;
	res_lib_cpg_mcast.header.error = error;
	openais_response_send (conn, &res_lib_cpg_mcast,
		sizeof (res_lib_cpg_mcast));
}

static void message_handler_req_lib_cpg_membership (void *conn, void *message)
{
	struct cpg_pd *cpd = (struct cpg_pd *)openais_conn_private_data_get (conn);
	SaAisErrorT error = CPG_OK;
	mar_res_header_t res;

	switch (cpd->cpd_state) {
	case CPD_STATE_UNJOINED:
		error = CPG_ERR_NOT_EXIST;
		break;
	case CPD_STATE_LEAVE_STARTED:
		error = CPG_ERR_NOT_EXIST;
		break;
	case CPD_STATE_JOIN_STARTED:
		error = CPG_ERR_BUSY;
		break;
	case CPD_STATE_JOIN_COMPLETED:
		error = CPG_OK;
		break;
	}

	res.size = sizeof (res);
	res.id = MESSAGE_RES_CPG_MEMBERSHIP;
	res.error = error;
	openais_response_send (conn, &res, sizeof(res));
		return;

	if (error == CPG_OK) {
		notify_lib_joinlist (&cpd->group_name, conn, 0, NULL, 0, NULL,
			MESSAGE_RES_CPG_MEMBERSHIP);
	}
}

static void message_handler_req_lib_cpg_local_get (void *conn, void *message)
{
	struct res_lib_cpg_local_get res_lib_cpg_local_get;

	res_lib_cpg_local_get.header.size = sizeof (res_lib_cpg_local_get);
	res_lib_cpg_local_get.header.id = MESSAGE_RES_CPG_LOCAL_GET;
	res_lib_cpg_local_get.header.error = CPG_OK;
	res_lib_cpg_local_get.local_nodeid = totempg_my_nodeid_get ();

	openais_response_send (conn, &res_lib_cpg_local_get,
		sizeof (res_lib_cpg_local_get));
}
