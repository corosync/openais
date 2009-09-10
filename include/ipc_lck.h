/*
 * Copyright (c) 2005 MontaVista Software, Inc.
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

#ifndef IPC_MSG_H_DEFINED
#define IPC_MSG_H_DEFINED

#include "saAis.h"
#include "saLck.h"
#include <corosync/hdb.h>
#include "mar_lck.h"

enum req_lib_lck_resource_types {
	MESSAGE_REQ_LCK_RESOURCEOPEN = 0,
	MESSAGE_REQ_LCK_RESOURCEOPENASYNC = 1,
	MESSAGE_REQ_LCK_RESOURCECLOSE = 2,
	MESSAGE_REQ_LCK_RESOURCELOCK = 3,
	MESSAGE_REQ_LCK_RESOURCELOCKASYNC = 4,
	MESSAGE_REQ_LCK_RESOURCEUNLOCK = 5,
	MESSAGE_REQ_LCK_RESOURCEUNLOCKASYNC = 6,
	MESSAGE_REQ_LCK_LOCKPURGE = 7,
	MESSAGE_REQ_LCK_LIMITGET = 8,
};

enum res_lib_lck_resource_types {
	MESSAGE_RES_LCK_RESOURCEOPEN = 0,
	MESSAGE_RES_LCK_RESOURCEOPENASYNC = 1,
	MESSAGE_RES_LCK_RESOURCECLOSE = 2,
	MESSAGE_RES_LCK_RESOURCELOCK = 3,
	MESSAGE_RES_LCK_RESOURCELOCKASYNC = 4,
	MESSAGE_RES_LCK_RESOURCEUNLOCK = 5,
	MESSAGE_RES_LCK_RESOURCEUNLOCKASYNC = 6,
	MESSAGE_RES_LCK_LOCKPURGE = 7,
	MESSAGE_RES_LCK_LIMITGET = 8,
	MESSAGE_RES_LCK_RESOURCEOPEN_CALLBACK = 9,
	MESSAGE_RES_LCK_LOCKGRANT_CALLBACK = 10,
	MESSAGE_RES_LCK_LOCKWAITER_CALLBACK = 11,
	MESSAGE_RES_LCK_RESOURCEUNLOCK_CALLBACK = 12,
};

/*
 * Define the limits for the lock service.
 * These limits are implementation specific and
 * can be obtained via the library call saLckLimitGet
 * by passing the appropriate limitId (see saLck.h).
 */
#define LCK_MAX_NUM_LOCKS 4096

struct req_lib_lck_resourceopen {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
	mar_uint32_t open_flags __attribute__((aligned(8)));
	mar_uint64_t resource_handle __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_lck_resourceopen {
	coroipc_response_header_t header __attribute__((aligned(8)));
	hdb_handle_t resource_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_lck_resourceopenasync {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
	mar_uint32_t open_flags __attribute__((aligned(8)));
	mar_uint64_t resource_handle __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_lck_resourceopenasync {
	coroipc_response_header_t header __attribute__((aligned(8)));
	hdb_handle_t resource_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_lck_resourceclose {
	coroipc_request_header_t header __attribute__((aligned(8)));
	hdb_handle_t resource_id __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
	mar_uint64_t resource_handle __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_lck_resourceclose {
	coroipc_response_header_t header __attribute__((aligned(8)));
}; __attribute__((aligned(8)))

struct req_lib_lck_resourcelock {
	coroipc_request_header_t header __attribute__((aligned(8)));
	hdb_handle_t resource_id __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
	mar_uint64_t lock_id __attribute__((aligned(8)));
	mar_uint32_t lock_mode __attribute__((aligned(8)));
	mar_uint32_t lock_flags __attribute__((aligned(8)));
	mar_uint64_t waiter_signal __attribute__((aligned(8)));
	mar_uint64_t resource_handle __attribute__((aligned(8)));
	mar_time_t timeout __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_lck_resourcelock {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint32_t lock_status __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_lck_resourcelockasync {
	coroipc_request_header_t header __attribute__((aligned(8)));
	hdb_handle_t resource_id __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
	mar_uint64_t lock_id __attribute__((aligned(8)));
	mar_uint32_t lock_mode __attribute__((aligned(8)));
	mar_uint32_t lock_flags __attribute__((aligned(8)));
	mar_uint64_t waiter_signal __attribute__((aligned(8)));
	mar_uint64_t resource_handle __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_lck_resourcelockasync {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_lck_resourceunlock {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
	mar_uint64_t resource_handle __attribute__((aligned(8)));
	mar_uint64_t lock_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_lck_resourceunlock {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_lck_resourceunlockasync {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
	mar_uint64_t resource_handle __attribute__((aligned(8)));
	mar_uint64_t lock_id __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_lck_resourceunlockasync {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_lck_lockpurge {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_name_t resource_name __attribute__((aligned(8)));
	mar_uint64_t resource_handle __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_lck_lockpurge {
	coroipc_response_header_t header __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct req_lib_lck_limitget {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_uint64_t limit_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_lck_limitget {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint64_t value __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_lck_resourceopen_callback {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint64_t resource_handle __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
	mar_uint32_t lock_status __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_lck_lockgrant_callback {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint64_t resource_handle __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
	mar_uint32_t lock_status __attribute__((aligned(8)));
	mar_uint64_t lock_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_lck_lockwaiter_callback {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint64_t resource_handle __attribute__((aligned(8)));
	mar_uint64_t waiter_signal __attribute__((aligned(8)));
	mar_uint64_t lock_id __attribute__((aligned(8)));
	mar_uint32_t mode_held __attribute__((aligned(8)));
	mar_uint32_t mode_requested __attribute__((aligned(8)));
} __attribute__((aligned(8)));

struct res_lib_lck_resourceunlock_callback {
	coroipc_response_header_t header __attribute__((aligned(8)));
	mar_uint64_t resource_handle __attribute__((aligned(8)));
	mar_invocation_t invocation __attribute__((aligned(8)));
	mar_uint64_t lock_id __attribute__((aligned(8)));
} __attribute__((aligned(8)));

#endif /* IPC_LCK_H_DEFINED */
