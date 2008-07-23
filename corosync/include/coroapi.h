/*
 * Copyright (c) 2008 Red Hat, Inc.
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
#ifndef COROAPI_H_DEFINED
#define COROAPI_H_DEFINED

#include "../exec/objdb.h"

typedef void * corosync_timer_handle_t;

#define TOTEMIP_ADDRLEN (sizeof(struct in6_addr))

#define PROCESSOR_COUNT_MAX 384
#define INTERFACE_MAX 2

#define TOTEM_AGREED	0
#define TOTEM_SAFE	1

#if !defined(TOTEM_IP_ADDRESS)
struct totem_ip_address {
	unsigned int   nodeid;
	unsigned short family;
	unsigned char  addr[TOTEMIP_ADDRLEN];
} __attribute__((packed));
#endif

#if !defined(MEMB_RING_ID)
struct memb_ring_id {
	struct totem_ip_address rep;
	unsigned long long seq;
} __attribute__((packed));
#endif

#if !defined(TOTEM_CONFIGURATION_TYPE)
enum totem_configuration_type {
	TOTEM_CONFIGURATION_REGULAR,
	TOTEM_CONFIGURATION_TRANSITIONAL
};
#endif

enum corosync_lib_flow_control {
	COROSYNC_LIB_FLOW_CONTROL_REQUIRED = 1,
	COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED = 2
};

#if !defined (COROSYNC_FLOW_CONTROL_STATE)
enum corosync_flow_control_state {
	COROSYNC_FLOW_CONTROL_STATE_DISABLED,
	COROSYNC_FLOW_CONTROL_STATE_ENABLED
};
#endif

struct corosync_api_v1 {
	int (*timer_add_duration) (
		unsigned long long nanoseconds_in_future,
		void *data,
		void (*timer_nf) (void *data),
		corosync_timer_handle_t *handle);

	int (*timer_add_absolute) (
		unsigned long long nanoseconds_from_epoch,
		void *data,
		void (*timer_fn) (void *data),
		corosync_timer_handle_t *handle);
	
	void (*timer_delete) (
		corosync_timer_handle_t timer_handle);

	unsigned long long (*timer_time_get) (void);

	void (*ipc_source_set) (mar_message_source_t *source, void *conn);

	int (*ipc_source_is_local) (mar_message_source_t *source);

	void *(*ipc_private_data_get) (void *conn);

	int (*ipc_response_send) (void *conn, void *msg, int mlen);

	int (*ipc_dispatch_send) (void *conn, void *msg, int mlen);

	/*
	 * DEPRECATED
	 */
	int (*ipc_conn_send_response) (void *conn, void *msg, int mlen);

	/*
	 * DEPRECATED
	 */
	void *(*ipc_conn_partner_get) (void *conn);

	void (*ipc_fc_create) (
		void *conn,
		unsigned int service,
		char *id,
		int id_len,
		void (*flow_control_state_set_fn)
			(void *context,
				enum corosync_flow_control_state flow_control_state_set),
		void *context);

	void (*ipc_fc_destroy) (
		void *conn,
		unsigned int service,
		unsigned char *id,
		int id_len);

	void (*ipc_refcnt_inc) (void *conn);

	void (*ipc_refcnt_dec) (void *conn);

	int (*totem_nodeid_get) (void);

	int (*totem_ring_reenable) (void);

	int (*totem_mcast) (struct iovec *iovec, int iov_len, unsigned int gaurantee);

	int (*totem_send_ok) (struct iovec *iovec, int iov_len);

	unsigned int (*service_link_and_init) (
		struct objdb_iface_ver0 *objdb,
		char *service_name,
		unsigned int service_ver);

	unsigned int (*service_unlink_and_exit) (
		struct objdb_iface_ver0 *objdb,
		char *service_name,
		unsigned int service_ver);

	int (*totem_ifaces_get) (
		unsigned int nodeid,
		struct totem_ip_address *interfaces,
		char ***status,
		unsigned int *iface_count);

	char *(*totem_ifaces_print) (unsigned int nodeid);

	char *(*totem_ip_print) (struct totem_ip_address *addr);

	void (*error_memory_failure) (void);
};

#define SERVICE_ID_MAKE(a,b) ( ((a)<<16) | (b) )

#define SERVICE_HANDLER_MAXIMUM_COUNT 64

struct corosync_lib_handler {
	void (*lib_handler_fn) (void *conn, void *msg);
	int response_size;
	int response_id;
	enum corosync_lib_flow_control flow_control;
};

struct corosync_exec_handler {
	void (*exec_handler_fn) (void *msg, unsigned int nodeid);
	void (*exec_endian_convert_fn) (void *msg);
};

struct corosync_service_engine_iface_ver0 {
        struct corosync_service_engine *(*corosync_get_service_engine_ver0) (void);
};

struct corosync_service_engine {
	char *name;
	unsigned short id;
	unsigned int private_data_size;
	enum corosync_lib_flow_control flow_control;
	int (*exec_init_fn) (struct objdb_iface_ver0 *, struct corosync_api_v1 *);
	int (*exec_exit_fn) (struct objdb_iface_ver0 *);
	void (*exec_dump_fn) (void);
	int (*lib_init_fn) (void *conn);
	int (*lib_exit_fn) (void *conn);
	struct corosync_lib_handler *lib_engine;
	int lib_engine_count;
	struct corosync_exec_handler *exec_engine;
	int exec_engine_count;
	int (*config_init_fn) (struct objdb_iface_ver0 *);
	void (*confchg_fn) (
		enum totem_configuration_type configuration_type,
		unsigned int *member_list, int member_list_entries,
		unsigned int *left_list, int left_list_entries,
		unsigned int *joined_list, int joined_list_entries,
		struct memb_ring_id *ring_id);
	void (*sync_init) (void);
	int (*sync_process) (void);
	void (*sync_activate) (void);
	void (*sync_abort) (void);
};

typedef void *corosync_timer_handle;

#endif /* COROAPI_H_DEFINED */
	
