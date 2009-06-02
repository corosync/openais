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

#include <config.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <inttypes.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/un.h>

#include <saAis.h>
#include <saLck.h>

#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include <corosync/coroipcc.h>
#include <corosync/corodefs.h>
#include <corosync/hdb.h>
#include <corosync/list.h>
#include <corosync/mar_gen.h>

#include "../include/ipc_lck.h"
#include "../include/mar_lck.h"
#include "../include/mar_sa.h"

#include "util.h"

struct lckInstance {
	hdb_handle_t ipc_handle;
	SaLckHandleT lck_handle;
	SaLckCallbacksT callbacks;
	int finalize;
	struct list_head resource_list;
};

struct lckResourceInstance {
	hdb_handle_t ipc_handle;
	hdb_handle_t resource_id;
	SaLckHandleT lck_handle;
	SaNameT resource_name;
	SaLckResourceHandleT resource_handle;
	SaLckResourceOpenFlagsT open_flags;
	struct list_head lock_id_list;
	struct list_head list;
};

struct lckLockIdInstance {
	hdb_handle_t ipc_handle;
	hdb_handle_t resource_id;
	SaLckHandleT lck_handle;
	SaLckLockIdT lock_id;
	SaLckResourceHandleT resource_handle;
	void *resource_lock;
	struct list_head list;
};

DECLARE_HDB_DATABASE(lckHandleDatabase, NULL);
DECLARE_HDB_DATABASE(lckResourceHandleDatabase, NULL);
DECLARE_HDB_DATABASE(lckLockIdHandleDatabase, NULL);

static SaVersionT lckVersionsSupported[] = {
	{ 'B', 3, 1 }
};

static struct saVersionDatabase lckVersionDatabase = {
	sizeof (lckVersionsSupported) / sizeof (SaVersionT),
	lckVersionsSupported
};

static void lckLockIdInstanceFinalize (struct lckLockIdInstance *lckLockIdInstance)
{
	list_del (&lckLockIdInstance->list);

	hdb_handle_destroy (&lckLockIdHandleDatabase, lckLockIdInstance->lock_id);

	return;
}

static void lckResourceInstanceFinalize (struct lckResourceInstance *lckResourceInstance)
{
	struct lckLockIdInstance *lckLockIdInstance;
	struct list_head *lckLockIdInstanceList;

	lckLockIdInstanceList = lckResourceInstance->lock_id_list.next;

	while (lckLockIdInstanceList != &lckResourceInstance->lock_id_list)
	{
		lckLockIdInstance = list_entry (lckLockIdInstanceList, struct lckLockIdInstance, list);
		lckLockIdInstanceList = lckLockIdInstanceList->next;
		lckLockIdInstanceFinalize (lckLockIdInstance);
	}

	list_del (&lckResourceInstance->list);

	hdb_handle_destroy (&lckResourceHandleDatabase, lckResourceInstance->resource_handle);

	return;
}

static void lckInstanceFinalize (struct lckInstance *lckInstance)
{
	struct lckResourceInstance *lckResourceInstance;
	struct list_head *lckResourceInstanceList;

	lckResourceInstanceList = lckInstance->resource_list.next;

	while (lckResourceInstanceList != &lckInstance->resource_list)
	{
		lckResourceInstance = list_entry (lckResourceInstanceList, struct lckResourceInstance, list);
		lckResourceInstanceList = lckResourceInstanceList->next;
		lckResourceInstanceFinalize (lckResourceInstance);
	}

	hdb_handle_destroy (&lckHandleDatabase, lckInstance->lck_handle);

	return;
}

SaAisErrorT
saLckInitialize (
	SaLckHandleT *lckHandle,
	const SaLckCallbacksT *lckCallbacks,
	SaVersionT *version)
{
	struct lckInstance *lckInstance;
	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saLckInitialize\n");

	if (lckHandle == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = saVersionVerify (&lckVersionDatabase, version);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_create (&lckHandleDatabase,
		sizeof (struct lckInstance), lckHandle));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&lckHandleDatabase,
		*lckHandle, (void *)&lckInstance));
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	error = coroipcc_service_connect (
		COROSYNC_SOCKET_NAME,
		LCK_SERVICE,
		IPC_REQUEST_SIZE,
		IPC_RESPONSE_SIZE,
		IPC_DISPATCH_SIZE,
		&lckInstance->ipc_handle);
	if (error != SA_AIS_OK) {
		goto error_put_destroy;
	}

	if (lckCallbacks != NULL) {
		memcpy (&lckInstance->callbacks, lckCallbacks, sizeof (SaLckCallbacksT));
	} else {
		memset (&lckInstance->callbacks, 0, sizeof (SaLckCallbacksT));
	}

	list_init (&lckInstance->resource_list);

	lckInstance->lck_handle = *lckHandle;

	hdb_handle_put (&lckHandleDatabase, *lckHandle);

	return (SA_AIS_OK);

error_put_destroy:
	hdb_handle_put (&lckHandleDatabase, *lckHandle);
error_destroy:
	hdb_handle_destroy (&lckHandleDatabase, *lckHandle);
error_exit:
	return (error);
}

SaAisErrorT
saLckSelectionObjectGet (
	SaLckHandleT lckHandle,
	SaSelectionObjectT *selectionObject)
{
	struct lckInstance *lckInstance;
	SaAisErrorT error = SA_AIS_OK;

	int fd;

	if (selectionObject == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&lckHandleDatabase,
		lckHandle, (void *)&lckInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = coroipcc_fd_get (lckInstance->ipc_handle, &fd);

	*selectionObject = fd;

	hdb_handle_put (&lckHandleDatabase, lckHandle);

error_exit:
	return (error);
}

SaAisErrorT
saLckOptionCheck (
	SaLckHandleT lckHandle,
	SaLckOptionsT *lckOptions)
{
	struct lckInstance *lckInstance;
	SaAisErrorT error = SA_AIS_OK;

	error = hdb_error_to_sa (hdb_handle_get (&lckHandleDatabase,
		lckHandle, (void *)&lckInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	/* TODO */

	hdb_handle_put (&lckHandleDatabase, lckHandle);

error_exit:
	return (error);
}

SaAisErrorT
saLckDispatch (
	SaLckHandleT lckHandle,
	SaDispatchFlagsT dispatchFlags)
{
	struct lckInstance *lckInstance;
	struct res_lib_lck_resourceopen_callback *res_lib_lck_resourceopen_callback;
	struct res_lib_lck_lockgrant_callback *res_lib_lck_lockgrant_callback;
	struct res_lib_lck_lockwaiter_callback *res_lib_lck_lockwaiter_callback;
	struct res_lib_lck_resourceunlock_callback *res_lib_lck_resourceunlock_callback;
	SaLckCallbacksT callbacks;
	SaAisErrorT error = SA_AIS_OK;

	coroipc_response_header_t *dispatch_data;
	int timeout = 1;
	int cont = 1;

	if (dispatchFlags != SA_DISPATCH_ONE &&
	    dispatchFlags != SA_DISPATCH_ALL &&
	    dispatchFlags != SA_DISPATCH_BLOCKING)
	{
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&lckHandleDatabase,
		lckHandle, (void *)&lckInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	if (dispatchFlags == SA_DISPATCH_ALL) {
		timeout = 0;
	}

	do {
		error = coroipcc_dispatch_get (
			lckInstance->ipc_handle,
			(void **)&dispatch_data,
			timeout);
		if (error == CS_ERR_BAD_HANDLE) {
			error = CS_OK;
			goto error_put;
		}
		if (error != CS_OK) {
			goto error_put;
		}

		if (dispatch_data == NULL) {
			if (dispatchFlags == CPG_DISPATCH_ALL) {
				break;
			} else {
				continue;
			}
		}

		memcpy (&callbacks, &lckInstance->callbacks,
			sizeof(lckInstance->callbacks));

		switch (dispatch_data->id) {
		case MESSAGE_RES_LCK_RESOURCEOPEN_CALLBACK:
			if (callbacks.saLckResourceOpenCallback == NULL) {
				continue;
			}
			res_lib_lck_resourceopen_callback =
				(struct res_lib_lck_resourceopen_callback *)dispatch_data;

			callbacks.saLckResourceOpenCallback (
				res_lib_lck_resourceopen_callback->invocation,
				res_lib_lck_resourceopen_callback->resource_handle,
				res_lib_lck_resourceopen_callback->header.error);

			break;

		case MESSAGE_RES_LCK_LOCKGRANT_CALLBACK:
			if (callbacks.saLckLockGrantCallback == NULL) {
				continue;
			}
			res_lib_lck_lockgrant_callback =
				(struct res_lib_lck_lockgrant_callback *)dispatch_data;

			callbacks.saLckLockGrantCallback (
				res_lib_lck_lockgrant_callback->invocation,
				res_lib_lck_lockgrant_callback->lock_status,
				res_lib_lck_lockgrant_callback->header.error);

			break;

		case MESSAGE_RES_LCK_LOCKWAITER_CALLBACK:
			if (callbacks.saLckLockWaiterCallback == NULL) {
				continue;
			}
			res_lib_lck_lockwaiter_callback =
				(struct res_lib_lck_lockwaiter_callback *)dispatch_data;

			callbacks.saLckLockWaiterCallback (
				res_lib_lck_lockwaiter_callback->waiter_signal,
				res_lib_lck_lockwaiter_callback->lock_id,
				res_lib_lck_lockwaiter_callback->mode_held,
				res_lib_lck_lockwaiter_callback->mode_requested);

			break;

		case MESSAGE_RES_LCK_RESOURCEUNLOCK_CALLBACK:
			if (callbacks.saLckResourceUnlockCallback == NULL) {
				continue;
			}
			res_lib_lck_resourceunlock_callback =
				(struct res_lib_lck_resourceunlock_callback *)dispatch_data;

			callbacks.saLckResourceUnlockCallback (
				res_lib_lck_resourceunlock_callback->invocation,
				res_lib_lck_resourceunlock_callback->header.error);

			break;

		default:
			break;
		}
		coroipcc_dispatch_put (lckInstance->ipc_handle);

		switch (dispatchFlags) {
		case SA_DISPATCH_ONE:
			cont = 0;
			break;
		case SA_DISPATCH_ALL:
			break;
		case SA_DISPATCH_BLOCKING:
			break;
		}
	} while (cont);

error_put:
	hdb_handle_put (&lckHandleDatabase, lckHandle);
error_exit:
	return (error);
}

SaAisErrorT
saLckFinalize (
	SaLckHandleT lckHandle)
{
	struct lckInstance *lckInstance;
	SaAisErrorT error = SA_AIS_OK;

	error = hdb_error_to_sa (hdb_handle_get (&lckHandleDatabase,
		lckHandle, (void *)&lckInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	if (lckInstance->finalize) {
		hdb_handle_put (&lckHandleDatabase, lckHandle);
		error = SA_AIS_ERR_BAD_HANDLE;
		goto error_exit;
	}

	lckInstance->finalize = 1;

	coroipcc_service_disconnect (lckInstance->ipc_handle);

	lckInstanceFinalize (lckInstance);

	hdb_handle_put (&lckHandleDatabase, lckHandle);

error_exit:
	return (error);
}

SaAisErrorT
saLckResourceOpen (
	SaLckHandleT lckHandle,
	const SaNameT *lckResourceName,
	SaLckResourceOpenFlagsT resourceFlags,
	SaTimeT timeout,
	SaLckResourceHandleT *lckResourceHandle)
{
	struct lckInstance *lckInstance;
	struct lckResourceInstance *lckResourceInstance;
	struct req_lib_lck_resourceopen req_lib_lck_resourceopen;
	struct res_lib_lck_resourceopen res_lib_lck_resourceopen;
	SaAisErrorT error = SA_AIS_OK;

	struct iovec iov;

	/* DEBUG */
	printf ("[DEBUG]: saLckResourceOpen\n");

	if (lckResourceHandle == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	if (lckResourceName == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	if ((resourceFlags & (~SA_LCK_RESOURCE_CREATE)) != 0) {
		error = SA_AIS_ERR_BAD_FLAGS;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&lckHandleDatabase,
		lckHandle, (void *)&lckInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_create (&lckResourceHandleDatabase,
		sizeof (struct lckResourceInstance), lckResourceHandle));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	error = hdb_error_to_sa (hdb_handle_get (&lckResourceHandleDatabase,
		*lckResourceHandle, (void *)&lckResourceInstance));
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	lckResourceInstance->ipc_handle = lckInstance->ipc_handle;
	lckResourceInstance->lck_handle = lckHandle;
	lckResourceInstance->resource_handle = *lckResourceHandle;

	memcpy (&lckResourceInstance->resource_name, lckResourceName,
		sizeof(SaNameT));

	req_lib_lck_resourceopen.header.size =
		sizeof (struct req_lib_lck_resourceopen);
	req_lib_lck_resourceopen.header.id =
		MESSAGE_REQ_LCK_RESOURCEOPEN;

/* 	memcpy (&req_lib_lck_resourceopen.resource_name, */
/* 		lckResourceName, sizeof (SaNameT)); */

	marshall_SaNameT_to_mar_name_t (
		&req_lib_lck_resourceopen.resource_name,
		(SaNameT *)lckResourceName);

	req_lib_lck_resourceopen.open_flags = resourceFlags;
	req_lib_lck_resourceopen.resource_handle = *lckResourceHandle;

	iov.iov_base = &req_lib_lck_resourceopen;
	iov.iov_len = sizeof (struct req_lib_lck_resourceopen);

	error = coroipcc_msg_send_reply_receive (
		lckResourceInstance->ipc_handle,
		&iov,
		1,
		&res_lib_lck_resourceopen,
		sizeof (struct res_lib_lck_resourceopen));

	if (error != SA_AIS_OK) {
		goto error_put_destroy;
	}

	if (res_lib_lck_resourceopen.header.error != SA_AIS_OK) {
		error = res_lib_lck_resourceopen.header.error;
		goto error_put_destroy;
	}

	lckResourceInstance->resource_id =
		res_lib_lck_resourceopen.resource_id;

	list_init (&lckResourceInstance->lock_id_list);
	list_init (&lckResourceInstance->list);
	list_add_tail (&lckResourceInstance->list, &lckInstance->resource_list);

	hdb_handle_put (&lckResourceHandleDatabase, *lckResourceHandle);
	hdb_handle_put (&lckHandleDatabase, lckHandle);

	return (error);

error_put_destroy:
	hdb_handle_put (&lckResourceHandleDatabase, *lckResourceHandle);
error_destroy:
	hdb_handle_destroy (&lckResourceHandleDatabase, *lckResourceHandle);
error_put:
	hdb_handle_put (&lckHandleDatabase, lckHandle);
error_exit:
	return (error);
}

SaAisErrorT
saLckResourceOpenAsync (
	SaLckHandleT lckHandle,
	SaInvocationT invocation,
	const SaNameT *lckResourceName,
	SaLckResourceOpenFlagsT resourceFlags)
{
	struct lckInstance *lckInstance;
	struct lckResourceInstance *lckResourceInstance;
	struct req_lib_lck_resourceopenasync req_lib_lck_resourceopenasync;
	struct res_lib_lck_resourceopenasync res_lib_lck_resourceopenasync;
	SaLckResourceHandleT lckResourceHandle;
	SaAisErrorT error = SA_AIS_OK;

	struct iovec iov;

	/* DEBUG */
	printf ("[DEBUG]: saLckResourceOpenAsync\n");

	if (lckResourceName == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	if ((resourceFlags & (~SA_LCK_RESOURCE_CREATE)) != 0) {
		error = SA_AIS_ERR_BAD_FLAGS;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&lckHandleDatabase,
		lckHandle, (void *)&lckInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	/* 
	 * Check that saLckLockGrantCallback is defined.
	 */
	if (lckInstance->callbacks.saLckResourceOpenCallback == NULL) {
		error = SA_AIS_ERR_INIT;
		goto error_put;
	}

	error = hdb_error_to_sa (hdb_handle_create (&lckResourceHandleDatabase,
		sizeof (struct lckResourceInstance), &lckResourceHandle));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	error = hdb_error_to_sa (hdb_handle_get (&lckResourceHandleDatabase,
		lckResourceHandle, (void *)&lckResourceInstance));
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	lckResourceInstance->ipc_handle = lckInstance->ipc_handle;
	lckResourceInstance->lck_handle = lckHandle;
	lckResourceInstance->resource_handle = lckResourceHandle;

	memcpy (&lckResourceInstance->resource_name, lckResourceName,
		sizeof (SaNameT));

	req_lib_lck_resourceopenasync.header.size =
		sizeof (struct req_lib_lck_resourceopenasync);
	req_lib_lck_resourceopenasync.header.id =
		MESSAGE_REQ_LCK_RESOURCEOPENASYNC;

/* 	memcpy (&req_lib_lck_resourceopenasync.resource_name, */
/* 		lckResourceName, sizeof (SaNameT)); */

	marshall_SaNameT_to_mar_name_t (
		&req_lib_lck_resourceopenasync.resource_name,
		(SaNameT *)lckResourceName);

	req_lib_lck_resourceopenasync.open_flags = resourceFlags;
	req_lib_lck_resourceopenasync.resource_handle = lckResourceHandle;
	req_lib_lck_resourceopenasync.invocation = invocation;

	iov.iov_base = &req_lib_lck_resourceopenasync;
	iov.iov_len = sizeof (struct req_lib_lck_resourceopenasync);

	error = coroipcc_msg_send_reply_receive (
		lckResourceInstance->ipc_handle,
		&iov,
		1,
		&res_lib_lck_resourceopenasync,
		sizeof (struct res_lib_lck_resourceopenasync));

	if (error != SA_AIS_OK) {
		goto error_put_destroy;
	}

	if (res_lib_lck_resourceopenasync.header.error != SA_AIS_OK) {
		error = res_lib_lck_resourceopenasync.header.error;
		goto error_put_destroy;
	}

	lckResourceInstance->resource_id =
		res_lib_lck_resourceopenasync.resource_id;

	list_init (&lckResourceInstance->lock_id_list);
	list_init (&lckResourceInstance->list);
	list_add_tail (&lckResourceInstance->list, &lckInstance->resource_list);

	hdb_handle_put (&lckResourceHandleDatabase, lckResourceHandle);
	hdb_handle_put (&lckHandleDatabase, lckHandle);

	return (error);

error_put_destroy:
	hdb_handle_put (&lckResourceHandleDatabase, lckResourceHandle);
error_destroy:
	hdb_handle_destroy (&lckResourceHandleDatabase, lckResourceHandle);
error_put:
	hdb_handle_put (&lckHandleDatabase, lckHandle);
error_exit:
	return (error);
}

SaAisErrorT
saLckResourceClose (
	SaLckResourceHandleT lckResourceHandle)
{
	struct lckResourceInstance *lckResourceInstance;
	struct req_lib_lck_resourceclose req_lib_lck_resourceclose;
	struct res_lib_lck_resourceclose res_lib_lck_resourceclose;
	struct iovec iov;
	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saLckResourceClose\n");

	error = hdb_error_to_sa (hdb_handle_get (&lckResourceHandleDatabase,
		lckResourceHandle, (void *)&lckResourceInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_lck_resourceclose.header.size =
		sizeof (struct req_lib_lck_resourceclose);
	req_lib_lck_resourceclose.header.id =
		MESSAGE_REQ_LCK_RESOURCECLOSE;

/* 	memcpy (&req_lib_lck_resourceclose.resource_name, */
/* 		&lckResourceInstance->resource_name, sizeof (SaNameT)); */

	marshall_SaNameT_to_mar_name_t (
		&req_lib_lck_resourceclose.resource_name,
		&lckResourceInstance->resource_name);

	req_lib_lck_resourceclose.resource_handle = lckResourceHandle;
	req_lib_lck_resourceclose.resource_id = lckResourceInstance->resource_id;

	iov.iov_base = &req_lib_lck_resourceclose;
	iov.iov_len = sizeof (struct req_lib_lck_resourceclose);

	error = coroipcc_msg_send_reply_receive (
		lckResourceInstance->ipc_handle,
		&iov,
		1,
		&res_lib_lck_resourceclose,
		sizeof (struct res_lib_lck_resourceclose));

	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_lck_resourceclose.header.error != SA_AIS_OK) {
		error = res_lib_lck_resourceclose.header.error;
		goto error_put;
	}

	/* lckResourceInstanceFinalize (lckResourceInstance) */

error_put:
	hdb_handle_put (&lckResourceHandleDatabase, lckResourceHandle);
error_exit:
	return (error);
}

SaAisErrorT
saLckResourceLock (
	SaLckResourceHandleT lckResourceHandle,
	SaLckLockIdT *lockId,
	SaLckLockModeT lockMode,
	SaLckLockFlagsT lockFlags,
	SaLckWaiterSignalT waiterSignal,
	SaTimeT timeout,
	SaLckLockStatusT *lockStatus)
{
	struct lckLockIdInstance *lckLockIdInstance;
	struct lckResourceInstance *lckResourceInstance;
	struct req_lib_lck_resourcelock req_lib_lck_resourcelock;
	struct res_lib_lck_resourcelock res_lib_lck_resourcelock;
	struct iovec iov;
	SaAisErrorT error = SA_AIS_OK;

	hdb_handle_t ipc_handle;

	/* DEBUG */
	printf ("[DEBUG]: saLckResourceLock\n");

	if ((lockMode != SA_LCK_PR_LOCK_MODE) && (lockMode != SA_LCK_EX_LOCK_MODE)) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&lckResourceHandleDatabase,
		lckResourceHandle, (void *)&lckResourceInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

/* 	memcpy (&req_lib_lck_resourcelock.resource_name, */
/* 		&lckResourceInstance->resource_name, sizeof (SaNameT)); */

	marshall_SaNameT_to_mar_name_t (
		&req_lib_lck_resourcelock.resource_name,
		&lckResourceInstance->resource_name);

	error = hdb_error_to_sa (hdb_handle_create (&lckLockIdHandleDatabase,
		sizeof (struct lckLockIdInstance), lockId));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	error = hdb_error_to_sa (hdb_handle_get (&lckLockIdHandleDatabase,
		*lockId, (void *)&lckLockIdInstance));
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	error = coroipcc_service_connect (
		COROSYNC_SOCKET_NAME,
		LCK_SERVICE,
		IPC_REQUEST_SIZE,
		IPC_RESPONSE_SIZE,
		IPC_DISPATCH_SIZE,
		&ipc_handle);
	if (error != SA_AIS_OK) {
		goto error_put_destroy;
	}

	lckLockIdInstance->ipc_handle = lckResourceInstance->ipc_handle;
	lckLockIdInstance->resource_id = lckResourceInstance->resource_id;
	lckLockIdInstance->lck_handle = lckResourceInstance->lck_handle;
	lckLockIdInstance->resource_handle = lckResourceHandle;
	lckLockIdInstance->lock_id = *lockId;

	req_lib_lck_resourcelock.header.size =
		sizeof (struct req_lib_lck_resourcelock);
	req_lib_lck_resourcelock.header.id =
		MESSAGE_REQ_LCK_RESOURCELOCK;

	req_lib_lck_resourcelock.lock_id = *lockId;
	req_lib_lck_resourcelock.lock_mode = lockMode;
	req_lib_lck_resourcelock.lock_flags = lockFlags;
	req_lib_lck_resourcelock.waiter_signal = waiterSignal;
	req_lib_lck_resourcelock.resource_handle = lckResourceHandle;
	req_lib_lck_resourcelock.resource_id = lckResourceInstance->resource_id;
	req_lib_lck_resourcelock.timeout = timeout;

	iov.iov_base = &req_lib_lck_resourcelock;
	iov.iov_len = sizeof (struct req_lib_lck_resourcelock);

	error = coroipcc_msg_send_reply_receive (
		ipc_handle,
		&iov,
		1,
		&res_lib_lck_resourcelock,
		sizeof (struct res_lib_lck_resourcelock));

	if (error != SA_AIS_OK) {
		goto error_disconnect;
	}

	if (res_lib_lck_resourcelock.header.error != SA_AIS_OK) {
		*lockStatus = res_lib_lck_resourcelock.lock_status;
		error = res_lib_lck_resourcelock.header.error;
		goto error_disconnect;
	}

	coroipcc_service_disconnect (ipc_handle);

	list_init (&lckLockIdInstance->list);
	list_add_tail (&lckLockIdInstance->list, &lckResourceInstance->lock_id_list);

	hdb_handle_put (&lckLockIdHandleDatabase, *lockId);
	hdb_handle_put (&lckResourceHandleDatabase, lckResourceHandle);

	*lockStatus = res_lib_lck_resourcelock.lock_status;

	return (error);

error_disconnect:
	coroipcc_service_disconnect (ipc_handle);
error_put_destroy:
	hdb_handle_put (&lckLockIdHandleDatabase, *lockId);
error_destroy:
	hdb_handle_destroy (&lckLockIdHandleDatabase, *lockId);
error_put:
	hdb_handle_put (&lckResourceHandleDatabase, lckResourceHandle);
error_exit:
	return (error);
}

SaAisErrorT
saLckResourceLockAsync (
	SaLckResourceHandleT lckResourceHandle,
	SaInvocationT invocation,
	SaLckLockIdT *lockId,
	SaLckLockModeT lockMode,
	SaLckLockFlagsT lockFlags,
	SaLckWaiterSignalT waiterSignal)
{
	struct lckInstance *lckInstance;
	struct lckLockIdInstance *lckLockIdInstance;
	struct lckResourceInstance *lckResourceInstance;
	struct req_lib_lck_resourcelockasync req_lib_lck_resourcelockasync;
	struct res_lib_lck_resourcelockasync res_lib_lck_resourcelockasync;
	struct iovec iov;
	SaAisErrorT error = SA_AIS_OK;

	hdb_handle_t ipc_handle;

	/* DEBUG */
	printf ("[DEBUG]: saLckResourceLockAsync\n");

	if ((lockMode != SA_LCK_PR_LOCK_MODE) && (lockMode != SA_LCK_EX_LOCK_MODE)) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&lckResourceHandleDatabase,
		lckResourceHandle, (void *)&lckResourceInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

/* 	memcpy (&req_lib_lck_resourcelockasync.resource_name, */
/* 		&lckResourceInstance->resource_name, sizeof (SaNameT)); */

	marshall_SaNameT_to_mar_name_t (
		&req_lib_lck_resourcelockasync.resource_name,
		&lckResourceInstance->resource_name);

	error = hdb_error_to_sa (hdb_handle_get (&lckHandleDatabase,
		lckResourceInstance->lck_handle, (void *)&lckInstance));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	/* 
	 * Check that saLckLockGrantCallback is defined.
	 */
	if (lckInstance->callbacks.saLckLockGrantCallback == NULL) {
		hdb_handle_put (&lckHandleDatabase,
			lckResourceInstance->lck_handle);
		error = SA_AIS_ERR_INIT;
		goto error_put;
	}

	hdb_handle_put (&lckHandleDatabase,
		lckResourceInstance->lck_handle);

	error = hdb_error_to_sa (hdb_handle_create (&lckLockIdHandleDatabase,
		sizeof (struct lckLockIdInstance), lockId));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	error = hdb_error_to_sa (hdb_handle_get (&lckLockIdHandleDatabase,
		*lockId, (void *)&lckLockIdInstance));
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	/*
	error = coroipcc_service_connect (
		COROSYNC_SOCKET_NAME,
		LCK_SERVICE,
		IPC_REQUEST_SIZE,
		IPC_RESPONSE_SIZE,
		IPC_DISPATCH_SIZE,
		&ipc_handle);
	if (error != SA_AIS_OK) {
		goto error_put_destroy;
	}
	*/

	lckLockIdInstance->ipc_handle = lckResourceInstance->ipc_handle;
	lckLockIdInstance->resource_id = lckResourceInstance->resource_id;
	lckLockIdInstance->lck_handle = lckResourceInstance->lck_handle;
	lckLockIdInstance->resource_handle = lckResourceHandle;
	lckLockIdInstance->lock_id = *lockId;

	req_lib_lck_resourcelockasync.header.size =
		sizeof (struct req_lib_lck_resourcelockasync);
	req_lib_lck_resourcelockasync.header.id =
		MESSAGE_REQ_LCK_RESOURCELOCKASYNC;

	req_lib_lck_resourcelockasync.lock_id = *lockId;
	req_lib_lck_resourcelockasync.lock_mode = lockMode;
	req_lib_lck_resourcelockasync.lock_flags = lockFlags;
	req_lib_lck_resourcelockasync.waiter_signal = waiterSignal;
	req_lib_lck_resourcelockasync.resource_handle = lckResourceHandle;
	req_lib_lck_resourcelockasync.resource_id = lckResourceInstance->resource_id;
	req_lib_lck_resourcelockasync.invocation = invocation;

	iov.iov_base = &req_lib_lck_resourcelockasync;
	iov.iov_len = sizeof (struct req_lib_lck_resourcelockasync);

	error = coroipcc_msg_send_reply_receive (
		/* ipc_handle, */
		lckResourceInstance->ipc_handle,
		&iov,
		1,
		&res_lib_lck_resourcelockasync,
		sizeof (struct res_lib_lck_resourcelockasync));

	if (error != SA_AIS_OK) {
		/* goto error_disconnect; */
		goto error_put_destroy;
	}

	if (res_lib_lck_resourcelockasync.header.error != SA_AIS_OK) {
		error = res_lib_lck_resourcelockasync.header.error;
		/* goto error_disconnect; */
		goto error_put_destroy;
	}

	coroipcc_service_disconnect (ipc_handle);

	list_init (&lckLockIdInstance->list);
	list_add_tail (&lckLockIdInstance->list, &lckResourceInstance->lock_id_list);

	hdb_handle_put (&lckLockIdHandleDatabase, *lockId);
	hdb_handle_put (&lckResourceHandleDatabase, lckResourceHandle);

	return (error);

/*
error_disconnect:
	coroipcc_service_disconnect (ipc_handle);
*/
error_put_destroy:
	hdb_handle_put (&lckLockIdHandleDatabase, *lockId);
error_destroy:
	hdb_handle_destroy (&lckLockIdHandleDatabase, *lockId);
error_put:
	hdb_handle_put (&lckResourceHandleDatabase, lckResourceHandle);
error_exit:
	return (error);
}

SaAisErrorT
saLckResourceUnlock (
	SaLckLockIdT lockId,
	SaTimeT timeout)
{
	struct lckLockIdInstance *lckLockIdInstance;
	struct lckResourceInstance *lckResourceInstance;
	struct req_lib_lck_resourceunlock req_lib_lck_resourceunlock;
	struct res_lib_lck_resourceunlock res_lib_lck_resourceunlock;
	struct iovec iov;
	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saLckResourceUnlock\n");

	error = hdb_error_to_sa (hdb_handle_get (&lckLockIdHandleDatabase,
		lockId, (void *)&lckLockIdInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&lckResourceHandleDatabase,
		lckLockIdInstance->resource_handle, (void *)&lckResourceInstance));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

/* 	memcpy (&req_lib_lck_resourceunlock.resource_name, */
/* 		&lckResourceInstance->resource_name, sizeof (SaNameT)); */

	marshall_SaNameT_to_mar_name_t (
		&req_lib_lck_resourceunlock.resource_name,
		&lckResourceInstance->resource_name);

	req_lib_lck_resourceunlock.resource_handle =
		lckLockIdInstance->resource_handle;

	hdb_handle_put (&lckResourceHandleDatabase, lckLockIdInstance->resource_handle);

	req_lib_lck_resourceunlock.header.size =
		sizeof (struct req_lib_lck_resourceunlock);
	req_lib_lck_resourceunlock.header.id =
		MESSAGE_REQ_LCK_RESOURCEUNLOCK;

	req_lib_lck_resourceunlock.lock_id = lockId;

	iov.iov_base = &req_lib_lck_resourceunlock;
	iov.iov_len = sizeof (struct req_lib_lck_resourceunlock);

	error = coroipcc_msg_send_reply_receive (
		lckLockIdInstance->ipc_handle,
		&iov,
		1,
		&res_lib_lck_resourceunlock,
		sizeof (struct res_lib_lck_resourceunlock));

	/* if (error != SA_AIS_OK) */

	if (res_lib_lck_resourceunlock.header.error != SA_AIS_OK) {
		error = res_lib_lck_resourceunlock.header.error;
		goto error_put;
	}

	hdb_handle_put (&lckLockIdHandleDatabase, lockId);
	hdb_handle_destroy (&lckLockIdHandleDatabase, lockId);

	return (error);

error_put:
	hdb_handle_put (&lckLockIdHandleDatabase, lockId);
error_exit:
	return (error);
}

SaAisErrorT
saLckResourceUnlockAsync (
	SaInvocationT invocation,
	SaLckLockIdT lockId)
{
	struct lckInstance *lckInstance;
	struct lckLockIdInstance *lckLockIdInstance;
	struct lckResourceInstance *lckResourceInstance;
	struct req_lib_lck_resourceunlockasync req_lib_lck_resourceunlockasync;
	struct res_lib_lck_resourceunlockasync res_lib_lck_resourceunlockasync;
	struct iovec iov;
	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saLckResourceUnlockAsync\n");

	error = hdb_error_to_sa (hdb_handle_get (&lckLockIdHandleDatabase,
		lockId, (void *)&lckLockIdInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&lckResourceHandleDatabase,
		lckLockIdInstance->resource_handle, (void *)&lckResourceInstance));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

/* 	memcpy (&req_lib_lck_resourceunlockasync.resource_name, */
/* 		&lckResourceInstance->resource_name, sizeof (SaNameT)); */

	marshall_SaNameT_to_mar_name_t (
		&req_lib_lck_resourceunlockasync.resource_name,
		&lckResourceInstance->resource_name);

	req_lib_lck_resourceunlockasync.resource_handle =
		lckLockIdInstance->resource_handle;

	hdb_handle_put (&lckResourceHandleDatabase, lckLockIdInstance->resource_handle);

	error = hdb_error_to_sa (hdb_handle_get (&lckHandleDatabase,
		lckResourceInstance->lck_handle, (void *)&lckInstance));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (lckInstance->callbacks.saLckResourceUnlockCallback == NULL) {
		hdb_handle_put (&lckHandleDatabase,
			lckResourceInstance->lck_handle);
		error = SA_AIS_ERR_INIT;
		goto error_put;
	}

	hdb_handle_put (&lckHandleDatabase,
		lckResourceInstance->lck_handle);

	req_lib_lck_resourceunlockasync.header.size =
		sizeof (struct req_lib_lck_resourceunlockasync);
	req_lib_lck_resourceunlockasync.header.id =
		MESSAGE_REQ_LCK_RESOURCEUNLOCKASYNC;

	req_lib_lck_resourceunlockasync.lock_id = lockId;
	req_lib_lck_resourceunlockasync.invocation = invocation;

	iov.iov_base = &req_lib_lck_resourceunlockasync;
	iov.iov_len = sizeof (struct req_lib_lck_resourceunlockasync);

	error = coroipcc_msg_send_reply_receive (
		lckLockIdInstance->ipc_handle,
		&iov,
		1,
		&res_lib_lck_resourceunlockasync,
		sizeof (struct res_lib_lck_resourceunlockasync));

	/* if (error != SA_AIS_OK) */

	if (res_lib_lck_resourceunlockasync.header.error != SA_AIS_OK) {
		error = res_lib_lck_resourceunlockasync.header.error;
		goto error_put;
	}

	hdb_handle_put (&lckLockIdHandleDatabase, lockId);
	hdb_handle_destroy (&lckLockIdHandleDatabase, lockId);

	return (error);

error_put:
	hdb_handle_put (&lckLockIdHandleDatabase, lockId);
error_exit:
	return (error);
}

SaAisErrorT
saLckLockPurge (
	SaLckResourceHandleT lckResourceHandle)
{
	struct lckResourceInstance *lckResourceInstance;
	struct req_lib_lck_lockpurge req_lib_lck_lockpurge;
	struct res_lib_lck_lockpurge res_lib_lck_lockpurge;
	struct iovec iov;
	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saLckLockPurge\n");

	error = hdb_error_to_sa (hdb_handle_get (&lckResourceHandleDatabase,
		lckResourceHandle, (void *)&lckResourceInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

/* 	memcpy (&req_lib_lck_lockpurge.resource_name, */
/* 		&lckResourceInstance->resource_name, sizeof (SaNameT)); */

	marshall_SaNameT_to_mar_name_t (
		&req_lib_lck_lockpurge.resource_name,
		&lckResourceInstance->resource_name);

	req_lib_lck_lockpurge.resource_handle = lckResourceHandle;

	req_lib_lck_lockpurge.header.size =
		sizeof (struct req_lib_lck_lockpurge);
	req_lib_lck_lockpurge.header.id =
		MESSAGE_REQ_LCK_LOCKPURGE;

	iov.iov_base = &req_lib_lck_lockpurge;
	iov.iov_len = sizeof (struct req_lib_lck_lockpurge);

	error = coroipcc_msg_send_reply_receive (
		lckResourceInstance->ipc_handle,
		&iov,
		1,
		&res_lib_lck_lockpurge,
		sizeof (struct res_lib_lck_lockpurge));

	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_lck_lockpurge.header.error != SA_AIS_OK) {
		error = res_lib_lck_lockpurge.header.error;
		goto error_put;
	}

error_put:
	hdb_handle_put (&lckResourceHandleDatabase, lckResourceHandle);
error_exit:
	return (error);
}

SaAisErrorT
saLckLimitGet (
	SaLckHandleT lckHandle,
	SaLckLimitIdT limitId,
	SaLimitValueT *limitValue)
{
	struct lckInstance *lckInstance;
	struct req_lib_lck_limitget req_lib_lck_limitget;
	struct res_lib_lck_limitget res_lib_lck_limitget;
	struct iovec iov;
	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saLckLimitGet\n");

	error = hdb_error_to_sa (hdb_handle_get (&lckHandleDatabase,
		lckHandle, (void *)&lckInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_lck_limitget.header.size =
		sizeof (struct req_lib_lck_limitget);
	req_lib_lck_limitget.header.id =
		MESSAGE_REQ_LCK_LIMITGET;

	iov.iov_base = &req_lib_lck_limitget;
	iov.iov_len = sizeof (struct req_lib_lck_limitget);

	error = coroipcc_msg_send_reply_receive (
		lckInstance->ipc_handle,
		&iov,
		1,
		&res_lib_lck_limitget,
		sizeof (struct res_lib_lck_limitget));

	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_lck_limitget.header.error != SA_AIS_OK) {
		error = res_lib_lck_limitget.header.error;
		goto error_put;
	}

error_put:
	hdb_handle_put (&lckHandleDatabase, lckHandle);
error_exit:
	return (error);
}
