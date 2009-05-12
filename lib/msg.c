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
#include <saMsg.h>

#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include <corosync/coroipcc.h>
#include <corosync/corodefs.h>
#include <corosync/hdb.h>
#include <corosync/list.h>

#include "../include/ipc_msg.h"
#include "util.h"

struct msgInstance {
	hdb_handle_t ipc_handle;
	SaMsgHandleT msg_handle;
	SaMsgCallbacksT callbacks;
	int finalize;
};

struct queueInstance {
	hdb_handle_t ipc_handle;
	SaNameT queue_name;
	SaUint32T queue_id;
	SaMsgHandleT msg_handle;
	SaMsgQueueHandleT queue_handle;
	SaMsgQueueOpenFlagsT open_flags;
	SaMsgQueueCreationAttributesT create_attrs;
};

DECLARE_HDB_DATABASE(msgHandleDatabase, NULL);

DECLARE_HDB_DATABASE(queueHandleDatabase, NULL);

static SaVersionT msgVersionsSupported[] = {
	{ 'B', 1, 1 }
};

static struct saVersionDatabase msgVersionDatabase = {
	sizeof (msgVersionsSupported) / sizeof (SaVersionT),
	msgVersionsSupported
};

#ifdef COMPILE_OUT
static void msgInstanceFinalize (struct msgInstance *msgInstance)
{
	return;
}

static void queueInstanceFinalize (struct queueInstance *queueInstance)
{
	return;
}

#endif /* COMPILE_OUT */

SaAisErrorT
saMsgInitialize (
	SaMsgHandleT *msgHandle,
	const SaMsgCallbacksT *callbacks,
	SaVersionT *version)
{
	struct msgInstance *msgInstance;
	SaAisErrorT error = SA_AIS_OK;

	if (msgHandle == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = saVersionVerify (&msgVersionDatabase, version);
	if (error != SA_AIS_OK) {
		goto error_no_destroy;
	}

	error = hdb_error_to_sa (hdb_handle_create (&msgHandleDatabase,
		sizeof (struct msgInstance), msgHandle));
	if (error != SA_AIS_OK) {
		goto error_no_destroy;
	}

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		*msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	error = coroipcc_service_connect (
		COROSYNC_SOCKET_NAME,
		MSG_SERVICE,
		IPC_REQUEST_SIZE,
		IPC_RESPONSE_SIZE,
		IPC_DISPATCH_SIZE,
		&msgInstance->ipc_handle);
	if (error != SA_AIS_OK) {
		goto error_put_destroy;
	}

	if (callbacks != NULL) {
		memcpy (&msgInstance->callbacks, callbacks, sizeof (SaMsgCallbacksT));
	} else {
		memset (&msgInstance->callbacks, 0, sizeof (SaMsgCallbacksT));
	}

	msgInstance->msg_handle = *msgHandle;

	hdb_handle_put (&msgHandleDatabase, *msgHandle);

	return (SA_AIS_OK);

error_put_destroy:
	hdb_handle_put (&msgHandleDatabase, *msgHandle);
error_destroy:
	hdb_handle_destroy (&msgHandleDatabase, *msgHandle);
error_no_destroy:
	return (error);
}

SaAisErrorT
saMsgSelectionObjectGet (
	const SaMsgHandleT msgHandle,
	SaSelectionObjectT *selectionObject)
{
	struct msgInstance *msgInstance;
	SaAisErrorT error = SA_AIS_OK;
	int fd;

	if (selectionObject == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	error = coroipcc_fd_get (msgInstance->ipc_handle, &fd);

	*selectionObject = fd;

	hdb_handle_put (&msgHandleDatabase, msgHandle);

	return (error);
}

SaAisErrorT
saMsgDispatch (
	SaMsgHandleT msgHandle,
	SaDispatchFlagsT dispatchFlags)
{
	SaMsgCallbacksT callbacks;
	SaAisErrorT error = SA_AIS_OK;
	struct msgInstance *msgInstance;
	/* struct queueInstance *queueInstance; */
	coroipc_response_header_t *dispatch_data;
	int timeout = 1;
	int cont = 1;

	struct res_lib_msg_queueopen_callback *res_lib_msg_queueopen_callback;
	struct res_lib_msg_queuegrouptrack_callback *res_lib_msg_queuegrouptrack_callback;
	struct res_lib_msg_messagedelivered_callback *res_lib_msg_messagedelivered_callback;
	struct res_lib_msg_messagereceived_callback *res_lib_msg_messagereceived_callback;

	if (dispatchFlags != SA_DISPATCH_ONE &&
	    dispatchFlags != SA_DISPATCH_ALL &&
	    dispatchFlags != SA_DISPATCH_BLOCKING)
	{
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	if (dispatchFlags == SA_DISPATCH_ALL) {
		timeout = 0;
	}

	do {

		error = coroipcc_dispatch_get (
			msgInstance->ipc_handle,
			(void **)&dispatch_data,
			timeout);
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

		memcpy (&callbacks, &msgInstance->callbacks,
			sizeof (msgInstance->callbacks));

		switch (dispatch_data->id) {
		case MESSAGE_RES_MSG_QUEUEOPEN_CALLBACK:
			if (callbacks.saMsgQueueOpenCallback == NULL) {
				continue;
			}
			res_lib_msg_queueopen_callback =
				(struct res_lib_msg_queueopen_callback *)dispatch_data;

			callbacks.saMsgQueueOpenCallback (
				res_lib_msg_queueopen_callback->invocation,
				res_lib_msg_queueopen_callback->queue_handle,
				res_lib_msg_queueopen_callback->header.error);

			break;

		case MESSAGE_RES_MSG_QUEUEGROUPTRACK_CALLBACK:
			if (callbacks.saMsgQueueGroupTrackCallback == NULL) {
				continue;
			}
			res_lib_msg_queuegrouptrack_callback =
				(struct res_lib_msg_queuegrouptrack_callback *)dispatch_data;

			res_lib_msg_queuegrouptrack_callback->buffer.notification =
				(SaMsgQueueGroupNotificationT *)(((char *)res_lib_msg_queuegrouptrack_callback) +
				sizeof (struct res_lib_msg_queuegrouptrack_callback));

			callbacks.saMsgQueueGroupTrackCallback (
				&res_lib_msg_queuegrouptrack_callback->group_name,
				&res_lib_msg_queuegrouptrack_callback->buffer,
				res_lib_msg_queuegrouptrack_callback->member_count,
				res_lib_msg_queuegrouptrack_callback->header.error);
			break;

		case MESSAGE_RES_MSG_MESSAGEDELIVERED_CALLBACK:
			if (callbacks.saMsgMessageDeliveredCallback == NULL) {
				continue;
			}
			res_lib_msg_messagedelivered_callback =
				(struct res_lib_msg_messagedelivered_callback *)dispatch_data;

			callbacks.saMsgMessageDeliveredCallback (
				res_lib_msg_messagedelivered_callback->invocation,
				res_lib_msg_messagedelivered_callback->header.error);

			break;

		case MESSAGE_RES_MSG_MESSAGERECEIVED_CALLBACK:
			if (callbacks.saMsgMessageReceivedCallback == NULL) {
				continue;
			}
			res_lib_msg_messagereceived_callback =
				(struct res_lib_msg_messagereceived_callback *)dispatch_data;

			callbacks.saMsgMessageReceivedCallback (
				res_lib_msg_messagereceived_callback->queue_handle);

			break;

		default:
			break;
		}
		coroipcc_dispatch_put (msgInstance->ipc_handle);

		switch (dispatchFlags)
		{
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
	hdb_handle_put (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgFinalize (
	SaMsgHandleT msgHandle)
{
	struct msgInstance *msgInstance;
	SaAisErrorT error = SA_AIS_OK;

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	if (msgInstance->finalize) {
		hdb_handle_put (&msgHandleDatabase, msgHandle);
		return (SA_AIS_ERR_BAD_HANDLE);
	}

	msgInstance->finalize = 1;

	/* msgInstanceFinalize (msgInstance); */

	error = coroipcc_service_disconnect (msgInstance->ipc_handle); /* ? */

	hdb_handle_put (&msgHandleDatabase, msgHandle);

	return (SA_AIS_OK);
}

SaAisErrorT
saMsgQueueOpen (
	SaMsgHandleT msgHandle,
	const SaNameT *queueName,
	const SaMsgQueueCreationAttributesT *creationAttributes,
	SaMsgQueueOpenFlagsT openFlags,
	SaTimeT timeout,
	SaMsgQueueHandleT *queueHandle)
{
	struct msgInstance *msgInstance;
	struct queueInstance *queueInstance;
	struct req_lib_msg_queueopen req_lib_msg_queueopen;
	struct res_lib_msg_queueopen res_lib_msg_queueopen;
	struct iovec iov;

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueOpen\n");

	if (queueName == NULL || queueHandle == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	if ((openFlags & (~SA_MSG_QUEUE_CREATE) & (~SA_MSG_QUEUE_RECEIVE_CALLBACK) & (~SA_MSG_QUEUE_EMPTY)) != 0) {
		error = SA_AIS_ERR_BAD_FLAGS;
		goto error_exit;
	}

	if ((!(openFlags & SA_MSG_QUEUE_CREATE)) && creationAttributes != NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	if (openFlags & SA_MSG_QUEUE_CREATE) {
	    if (creationAttributes == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_put;
	    }

	    if ((creationAttributes->creationFlags != 0) &&
		(creationAttributes->creationFlags != SA_MSG_QUEUE_PERSISTENT)) {
			error = SA_AIS_ERR_BAD_FLAGS;
			goto error_exit;
		}
	}

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	if ((openFlags & SA_MSG_QUEUE_RECEIVE_CALLBACK) &&
	    (msgInstance->callbacks.saMsgMessageReceivedCallback == NULL))
	{
		error = SA_AIS_ERR_INIT;
		goto error_put;
	}

	error = hdb_error_to_sa (hdb_handle_create (&queueHandleDatabase,
		sizeof (struct queueInstance), queueHandle));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	error = hdb_error_to_sa (hdb_handle_get (&queueHandleDatabase,
		*queueHandle, (void *)&queueInstance));
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	queueInstance->ipc_handle = msgInstance->ipc_handle;
	queueInstance->open_flags = openFlags;

	req_lib_msg_queueopen.header.size =
		sizeof (struct req_lib_msg_queueopen);
	req_lib_msg_queueopen.header.id =
		MESSAGE_REQ_MSG_QUEUEOPEN;

	memcpy (&req_lib_msg_queueopen.queue_name,
		queueName, sizeof (SaNameT));
	memcpy (&queueInstance->queue_name,
		queueName, sizeof (SaNameT));

	req_lib_msg_queueopen.queue_handle = *queueHandle;
	req_lib_msg_queueopen.open_flags = openFlags;
	req_lib_msg_queueopen.timeout = timeout;

	if (creationAttributes != NULL) {
		memcpy (&req_lib_msg_queueopen.create_attrs,
			creationAttributes, sizeof (SaMsgQueueCreationAttributesT));
		memcpy (&queueInstance->create_attrs,
			creationAttributes, sizeof (SaMsgQueueCreationAttributesT));
		req_lib_msg_queueopen.create_attrs_flag = 1;
	} else {
		req_lib_msg_queueopen.create_attrs_flag = 0;
	}

	iov.iov_base = &req_lib_msg_queueopen;
	iov.iov_len = sizeof (struct req_lib_msg_queueopen);

	error = coroipcc_msg_send_reply_receive (
		queueInstance->ipc_handle,
		&iov,
		1,
		&res_lib_msg_queueopen,
		sizeof (struct res_lib_msg_queueopen));

	if (error != SA_AIS_OK) {
		goto error_put_destroy;
	}

	if (res_lib_msg_queueopen.header.error != SA_AIS_OK) {
		error = res_lib_msg_queueopen.header.error;
		goto error_put_destroy;
	}

	queueInstance->queue_id = res_lib_msg_queueopen.queue_id;

	hdb_handle_put (&queueHandleDatabase, *queueHandle);
	hdb_handle_put (&msgHandleDatabase, msgHandle);

	return (error);

error_put_destroy:
	hdb_handle_put (&queueHandleDatabase, *queueHandle);
error_destroy:
	hdb_handle_destroy (&queueHandleDatabase, *queueHandle);
error_put:
	hdb_handle_put (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgQueueOpenAsync (
	SaMsgHandleT msgHandle,
	SaInvocationT invocation,
	const SaNameT *queueName,
	const SaMsgQueueCreationAttributesT *creationAttributes,
	SaMsgQueueOpenFlagsT openFlags)
{
	struct msgInstance *msgInstance;
	struct queueInstance *queueInstance;
	struct req_lib_msg_queueopenasync req_lib_msg_queueopenasync;
	struct res_lib_msg_queueopenasync res_lib_msg_queueopenasync;
	struct iovec iov;

	SaMsgQueueHandleT queueHandle;
	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueOpenAsync\n");

	if (queueName == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	if ((openFlags & (~SA_MSG_QUEUE_CREATE) & (~SA_MSG_QUEUE_RECEIVE_CALLBACK) & (~SA_MSG_QUEUE_EMPTY)) != 0) {
		error = SA_AIS_ERR_BAD_FLAGS;
		goto error_exit;
	}

	if ((!(openFlags & SA_MSG_QUEUE_CREATE)) && creationAttributes != NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	if (openFlags & SA_MSG_QUEUE_CREATE) {
	    if (creationAttributes == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_put;
	    }

	    if ((creationAttributes->creationFlags != 0) &&
		(creationAttributes->creationFlags != SA_MSG_QUEUE_PERSISTENT)) {
			error = SA_AIS_ERR_BAD_FLAGS;
			goto error_exit;
		}
	}

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	if ((openFlags & SA_MSG_QUEUE_RECEIVE_CALLBACK) &&
	    (msgInstance->callbacks.saMsgMessageReceivedCallback == NULL)) {
		error = SA_AIS_ERR_INIT;
		goto error_put;
	}

	if (msgInstance->callbacks.saMsgQueueOpenCallback == NULL) {
		error = SA_AIS_ERR_INIT;
		goto error_put;
	}

	error = hdb_error_to_sa (hdb_handle_create (&queueHandleDatabase,
		sizeof (struct queueInstance), &queueHandle));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	error = hdb_error_to_sa (hdb_handle_get (&queueHandleDatabase,
		queueHandle, (void *)&queueInstance));
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	queueInstance->ipc_handle = msgInstance->ipc_handle;
	queueInstance->open_flags = openFlags;

	req_lib_msg_queueopenasync.header.size =
		sizeof (struct req_lib_msg_queueopenasync);
	req_lib_msg_queueopenasync.header.id =
		MESSAGE_REQ_MSG_QUEUEOPENASYNC;

	memcpy (&req_lib_msg_queueopenasync.queue_name,
		queueName, sizeof (SaNameT));
	memcpy (&queueInstance->queue_name,
		queueName, sizeof (SaNameT));

	req_lib_msg_queueopenasync.queue_handle = queueHandle;
	req_lib_msg_queueopenasync.open_flags = openFlags;
	req_lib_msg_queueopenasync.invocation = invocation;

	if (creationAttributes != NULL) {
		memcpy (&req_lib_msg_queueopenasync.create_attrs,
			creationAttributes, sizeof (SaMsgQueueCreationAttributesT));
		memcpy (&queueInstance->create_attrs,
			creationAttributes, sizeof (SaMsgQueueCreationAttributesT));
		req_lib_msg_queueopenasync.create_attrs_flag = 1;
	} else {
		req_lib_msg_queueopenasync.create_attrs_flag = 0;
	}

	iov.iov_base = &req_lib_msg_queueopenasync;
	iov.iov_len = sizeof (struct req_lib_msg_queueopenasync);

	error = coroipcc_msg_send_reply_receive (
		queueInstance->ipc_handle,
		&iov,
		1,
		&res_lib_msg_queueopenasync,
		sizeof (struct res_lib_msg_queueopenasync));

	if (error != SA_AIS_OK) {
		goto error_put_destroy;
	}

	if (res_lib_msg_queueopenasync.header.error != SA_AIS_OK) {
		error = res_lib_msg_queueopenasync.header.error;
		goto error_put_destroy;
	}

	queueInstance->queue_id = res_lib_msg_queueopenasync.queue_id;

	hdb_handle_put (&queueHandleDatabase, queueHandle);
	hdb_handle_put (&msgHandleDatabase, msgHandle);

	return (error);

error_put_destroy:
	hdb_handle_put (&queueHandleDatabase, queueHandle);
error_destroy:
	hdb_handle_destroy (&queueHandleDatabase, queueHandle);
error_put:
	hdb_handle_put (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgQueueClose (
	SaMsgQueueHandleT queueHandle)
{
	struct queueInstance *queueInstance;
	struct req_lib_msg_queueclose req_lib_msg_queueclose;
	struct res_lib_msg_queueclose res_lib_msg_queueclose;
	struct iovec iov;

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueClose\n");

	error = hdb_error_to_sa (hdb_handle_get (&queueHandleDatabase,
		queueHandle, (void *)&queueInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_msg_queueclose.header.size =
		sizeof (struct req_lib_msg_queueclose);
	req_lib_msg_queueclose.header.id =
		MESSAGE_REQ_MSG_QUEUECLOSE;
	req_lib_msg_queueclose.queue_id =
		queueInstance->queue_id;

	memcpy (&req_lib_msg_queueclose.queue_name,
		&queueInstance->queue_name, sizeof (SaNameT));

	iov.iov_base = &req_lib_msg_queueclose;
	iov.iov_len = sizeof (struct req_lib_msg_queueclose);

	error = coroipcc_msg_send_reply_receive (
		queueInstance->ipc_handle,
		&iov,
		1,
		&res_lib_msg_queueclose,
		sizeof (struct res_lib_msg_queueclose));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_msg_queueclose.header.error != SA_AIS_OK) {
		error = res_lib_msg_queueclose.header.error;
		goto error_put;	/* ! */
	}

	/*No Error -> destroy handle*/
	hdb_handle_put (&queueHandleDatabase, queueHandle);
	hdb_handle_destroy (&queueHandleDatabase, queueHandle);

	return (error);
error_put:
	hdb_handle_put (&queueHandleDatabase, queueHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgQueueStatusGet (
	SaMsgHandleT msgHandle,
	const SaNameT *queueName,
	SaMsgQueueStatusT *queueStatus)
{
	struct msgInstance *msgInstance;
	struct req_lib_msg_queuestatusget req_lib_msg_queuestatusget;
	struct res_lib_msg_queuestatusget res_lib_msg_queuestatusget;
	struct iovec iov;

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueStatusGet\n");

	if (queueName == NULL || queueStatus == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_msg_queuestatusget.header.size =
		sizeof (struct req_lib_msg_queuestatusget);
	req_lib_msg_queuestatusget.header.id =
		MESSAGE_REQ_MSG_QUEUESTATUSGET;

	memcpy (&req_lib_msg_queuestatusget.queue_name,
		queueName, sizeof (SaNameT));

	iov.iov_base = &req_lib_msg_queuestatusget;
	iov.iov_len = sizeof (struct req_lib_msg_queuestatusget);

	error = coroipcc_msg_send_reply_receive (
		msgInstance->ipc_handle,
		&iov,
		1,
		&res_lib_msg_queuestatusget,
		sizeof (struct res_lib_msg_queuestatusget));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_msg_queuestatusget.header.error != SA_AIS_OK) {
		error = res_lib_msg_queuestatusget.header.error;
		goto error_put;	/* ! */
	}
	else {
		memcpy (queueStatus,
			&res_lib_msg_queuestatusget.queue_status,
			sizeof (SaMsgQueueStatusT));
	}

error_put:
	hdb_handle_put (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgQueueRetentionTimeSet (
	SaMsgQueueHandleT queueHandle,
	SaTimeT *retentionTime)
{
	struct queueInstance *queueInstance;
	struct req_lib_msg_queueretentiontimeset req_lib_msg_queueretentiontimeset;
	struct res_lib_msg_queueretentiontimeset res_lib_msg_queueretentiontimeset;
	struct iovec iov;

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueRetentionTimeSet\n");

	error = hdb_error_to_sa (hdb_handle_get (&queueHandleDatabase,
		queueHandle, (void *)&queueInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_msg_queueretentiontimeset.header.size =
		sizeof (struct req_lib_msg_queueretentiontimeset);
	req_lib_msg_queueretentiontimeset.header.id =
		MESSAGE_REQ_MSG_QUEUERETENTIONTIMESET;
	req_lib_msg_queueretentiontimeset.queue_id =
		queueInstance->queue_id;

	req_lib_msg_queueretentiontimeset.retention_time = *retentionTime;

	memcpy (&req_lib_msg_queueretentiontimeset.queue_name,
		&queueInstance->queue_name, sizeof (SaNameT));

	iov.iov_base = &req_lib_msg_queueretentiontimeset;
	iov.iov_len = sizeof (struct req_lib_msg_queueretentiontimeset);

	error = coroipcc_msg_send_reply_receive (
		queueInstance->ipc_handle,
		&iov,
		1,
		&res_lib_msg_queueretentiontimeset,
		sizeof (struct res_lib_msg_queueretentiontimeset));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_msg_queueretentiontimeset.header.error != SA_AIS_OK) {
		error = res_lib_msg_queueretentiontimeset.header.error;
		goto error_put;	/* ! */
	}

error_put:
	hdb_handle_put (&queueHandleDatabase, queueHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgQueueUnlink (
	SaMsgHandleT msgHandle,
	const SaNameT *queueName)
{
	struct msgInstance *msgInstance;
	struct req_lib_msg_queueunlink req_lib_msg_queueunlink;
	struct res_lib_msg_queueunlink res_lib_msg_queueunlink;
	struct iovec iov;

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueUnlink\n");

	if (queueName == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_msg_queueunlink.header.size =
		sizeof (struct req_lib_msg_queueunlink);
	req_lib_msg_queueunlink.header.id =
		MESSAGE_REQ_MSG_QUEUEUNLINK;

	memcpy (&req_lib_msg_queueunlink.queue_name,
		queueName, sizeof (SaNameT));

	iov.iov_base = &req_lib_msg_queueunlink;
	iov.iov_len = sizeof (struct req_lib_msg_queueunlink);

	error = coroipcc_msg_send_reply_receive (
		msgInstance->ipc_handle,
		&iov,
		1,
		&res_lib_msg_queueunlink,
		sizeof (struct res_lib_msg_queueunlink));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_msg_queueunlink.header.error != SA_AIS_OK) {
		error = res_lib_msg_queueunlink.header.error;
		goto error_put;	/* ! */
	}

error_put:
	hdb_handle_put (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgQueueGroupCreate (
	SaMsgHandleT msgHandle,
	const SaNameT *queueGroupName,
	SaMsgQueueGroupPolicyT queueGroupPolicy)
{
	struct msgInstance *msgInstance;
	struct req_lib_msg_queuegroupcreate req_lib_msg_queuegroupcreate;
	struct res_lib_msg_queuegroupcreate res_lib_msg_queuegroupcreate;
	struct iovec iov;

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueGroupCreate\n");

	if (queueGroupName == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_msg_queuegroupcreate.header.size =
		sizeof (struct req_lib_msg_queuegroupcreate);
	req_lib_msg_queuegroupcreate.header.id =
		MESSAGE_REQ_MSG_QUEUEGROUPCREATE;

	memcpy (&req_lib_msg_queuegroupcreate.group_name,
		queueGroupName, sizeof (SaNameT));

	req_lib_msg_queuegroupcreate.policy = queueGroupPolicy;

	iov.iov_base = &req_lib_msg_queuegroupcreate;
	iov.iov_len = sizeof (struct req_lib_msg_queuegroupcreate);

	error = coroipcc_msg_send_reply_receive (
		msgInstance->ipc_handle,
		&iov,
		1,
		&res_lib_msg_queuegroupcreate,
		sizeof (struct res_lib_msg_queuegroupcreate));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_msg_queuegroupcreate.header.error != SA_AIS_OK) {
		error = res_lib_msg_queuegroupcreate.header.error;
		goto error_put;	/* ! */
	}

error_put:
	hdb_handle_put (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgQueueGroupInsert (
	SaMsgHandleT msgHandle,
	const SaNameT *queueGroupName,
	const SaNameT *queueName)
{
	struct msgInstance *msgInstance;
	struct req_lib_msg_queuegroupinsert req_lib_msg_queuegroupinsert;
	struct res_lib_msg_queuegroupinsert res_lib_msg_queuegroupinsert;
	struct iovec iov;

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueGroupInsert\n");

	if (queueName == NULL || queueGroupName == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_msg_queuegroupinsert.header.size =
		sizeof (struct req_lib_msg_queuegroupinsert);
	req_lib_msg_queuegroupinsert.header.id =
		MESSAGE_REQ_MSG_QUEUEGROUPINSERT;

	memcpy (&req_lib_msg_queuegroupinsert.group_name,
		queueGroupName, sizeof (SaNameT));
	memcpy (&req_lib_msg_queuegroupinsert.queue_name,
		queueName, sizeof (SaNameT));

	iov.iov_base = &req_lib_msg_queuegroupinsert;
	iov.iov_len = sizeof (struct req_lib_msg_queuegroupinsert);

	error = coroipcc_msg_send_reply_receive (
		msgInstance->ipc_handle,
		&iov,
		1,
		&res_lib_msg_queuegroupinsert,
		sizeof (struct res_lib_msg_queuegroupinsert));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_msg_queuegroupinsert.header.error != SA_AIS_OK) {
		error = res_lib_msg_queuegroupinsert.header.error;
		goto error_put;	/* ! */
	}

error_put:
	hdb_handle_put (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgQueueGroupRemove (
	SaMsgHandleT msgHandle,
	const SaNameT *queueGroupName,
	const SaNameT *queueName)
{
	struct msgInstance *msgInstance;
	struct req_lib_msg_queuegroupremove req_lib_msg_queuegroupremove;
	struct res_lib_msg_queuegroupremove res_lib_msg_queuegroupremove;
	struct iovec iov;

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueGroupRemove\n");

	if (queueName == NULL || queueGroupName == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_msg_queuegroupremove.header.size =
		sizeof (struct req_lib_msg_queuegroupremove);
	req_lib_msg_queuegroupremove.header.id =
		MESSAGE_REQ_MSG_QUEUEGROUPREMOVE;

	memcpy (&req_lib_msg_queuegroupremove.group_name,
		queueGroupName, sizeof (SaNameT));
	memcpy (&req_lib_msg_queuegroupremove.queue_name,
		queueName, sizeof (SaNameT));

	iov.iov_base = &req_lib_msg_queuegroupremove;
	iov.iov_len = sizeof (struct req_lib_msg_queuegroupremove);

	error = coroipcc_msg_send_reply_receive (
		msgInstance->ipc_handle,
		&iov,
		1,
		&res_lib_msg_queuegroupremove,
		sizeof (struct res_lib_msg_queuegroupremove));

	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_msg_queuegroupremove.header.error != SA_AIS_OK) {
		error = res_lib_msg_queuegroupremove.header.error;
		goto error_put;	/* ! */
	}

error_put:
	hdb_handle_put (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgQueueGroupDelete (
	SaMsgHandleT msgHandle,
	const SaNameT *queueGroupName)
{
	struct msgInstance *msgInstance;
	struct req_lib_msg_queuegroupdelete req_lib_msg_queuegroupdelete;
	struct res_lib_msg_queuegroupdelete res_lib_msg_queuegroupdelete;
	struct iovec iov;

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueGroupDelete\n");

	if (queueGroupName == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa(hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_msg_queuegroupdelete.header.size =
		sizeof (struct req_lib_msg_queuegroupdelete);
	req_lib_msg_queuegroupdelete.header.id =
		MESSAGE_REQ_MSG_QUEUEGROUPDELETE;

	memcpy (&req_lib_msg_queuegroupdelete.group_name,
		queueGroupName, sizeof (SaNameT));

	iov.iov_base = &req_lib_msg_queuegroupdelete;
	iov.iov_len = sizeof (struct req_lib_msg_queuegroupdelete);

	error = coroipcc_msg_send_reply_receive (
		msgInstance->ipc_handle,
		&iov,
		1,
		&res_lib_msg_queuegroupdelete,
		sizeof (struct res_lib_msg_queuegroupdelete));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_msg_queuegroupdelete.header.error != SA_AIS_OK) {
		error = res_lib_msg_queuegroupdelete.header.error;
		goto error_put;	/* ! */
	}

error_put:
	hdb_handle_put (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgQueueGroupTrack (
	SaMsgHandleT msgHandle,
	const SaNameT *queueGroupName,
	SaUint8T trackFlags,
	SaMsgQueueGroupNotificationBufferT *notificationBuffer)
{
	struct msgInstance *msgInstance;
	struct req_lib_msg_queuegrouptrack req_lib_msg_queuegrouptrack;
	struct res_lib_msg_queuegrouptrack *res_lib_msg_queuegrouptrack;
	struct iovec iov;

	void * buffer;

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueGroupTrack\n");

	if (queueGroupName == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	if ((notificationBuffer != NULL) &&
	    (notificationBuffer->notification != NULL) &&
	    (notificationBuffer->numberOfItems == 0)) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	if ((notificationBuffer != NULL) &&
	    (notificationBuffer->notification == NULL)) {
		notificationBuffer->numberOfItems = 0;
	}

	if ((trackFlags & SA_TRACK_CHANGES) &&
	    (trackFlags & SA_TRACK_CHANGES_ONLY)) {
		error = SA_AIS_ERR_BAD_FLAGS;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	if ((trackFlags == SA_TRACK_CURRENT) && (notificationBuffer != NULL) &&
	    (msgInstance->callbacks.saMsgQueueGroupTrackCallback == NULL))
	{
		error = SA_AIS_ERR_INIT;
		goto error_put;
	}

	req_lib_msg_queuegrouptrack.header.size =
		sizeof (struct req_lib_msg_queuegrouptrack);
	req_lib_msg_queuegrouptrack.header.id =
		MESSAGE_REQ_MSG_QUEUEGROUPTRACK;

	memcpy (&req_lib_msg_queuegrouptrack.group_name,
		queueGroupName, sizeof (SaNameT));

	req_lib_msg_queuegrouptrack.track_flags = trackFlags;
	req_lib_msg_queuegrouptrack.buffer_flag = (notificationBuffer != NULL);

	iov.iov_base = &req_lib_msg_queuegrouptrack;
	iov.iov_len = sizeof (struct req_lib_msg_queuegrouptrack);

	error = coroipcc_msg_send_reply_receive_in_buf_get (
		msgInstance->ipc_handle,
		&iov,
		1,
		&buffer);
	if (error != SA_AIS_OK) {
		goto error_unlock;
	}

	res_lib_msg_queuegrouptrack = buffer;

	if (res_lib_msg_queuegrouptrack->header.error != SA_AIS_OK) {
		error = res_lib_msg_queuegrouptrack->header.error;
		goto error_unlock;
	}

	if ((trackFlags & SA_TRACK_CURRENT) && (notificationBuffer != NULL)) {
		if (res_lib_msg_queuegrouptrack->buffer.numberOfItems > notificationBuffer->numberOfItems) {
			notificationBuffer->numberOfItems =
				res_lib_msg_queuegrouptrack->buffer.numberOfItems;
			error = SA_AIS_ERR_NO_SPACE;
			goto error_unlock;
		}
		notificationBuffer->numberOfItems =
			res_lib_msg_queuegrouptrack->buffer.numberOfItems;
		memcpy (notificationBuffer->notification, ((char *)(buffer) +
			sizeof (struct res_lib_msg_queuegrouptrack)),
			res_lib_msg_queuegrouptrack->buffer.numberOfItems *
			sizeof (SaMsgQueueGroupNotificationT));
	}
	error = coroipcc_msg_send_reply_receive_in_buf_put (
		msgInstance->ipc_handle);

error_unlock:
error_put:
	hdb_handle_put (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgQueueGroupTrackStop (
	SaMsgHandleT msgHandle,
	const SaNameT *queueGroupName)
{
	struct msgInstance *msgInstance;
	struct req_lib_msg_queuegrouptrackstop req_lib_msg_queuegrouptrackstop;
	struct res_lib_msg_queuegrouptrackstop res_lib_msg_queuegrouptrackstop;
	struct iovec iov;

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueGroupTrackStop\n");

	if (queueGroupName == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_msg_queuegrouptrackstop.header.size =
		sizeof (struct req_lib_msg_queuegrouptrackstop);
	req_lib_msg_queuegrouptrackstop.header.id =
		MESSAGE_REQ_MSG_QUEUEGROUPTRACKSTOP;

	memcpy (&req_lib_msg_queuegrouptrackstop.group_name,
		queueGroupName, sizeof (SaNameT));

	iov.iov_base = &req_lib_msg_queuegrouptrackstop;
	iov.iov_len = sizeof (struct req_lib_msg_queuegrouptrackstop);

	error = coroipcc_msg_send_reply_receive (
		msgInstance->ipc_handle,
		&iov,
		1,
		&res_lib_msg_queuegrouptrackstop,
		sizeof (struct res_lib_msg_queuegrouptrackstop));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_msg_queuegrouptrackstop.header.error != SA_AIS_OK) {
		error = res_lib_msg_queuegrouptrackstop.header.error;
		goto error_put;	/* ! */
	}

error_put:
	hdb_handle_put (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgQueueGroupNotificationFree (
	SaMsgHandleT msgHandle,
	SaMsgQueueGroupNotificationT *notification)
{
	struct msgInstance *msgInstance;
	struct req_lib_msg_queuegroupnotificationfree req_lib_msg_queuegroupnotificationfree;
	struct res_lib_msg_queuegroupnotificationfree res_lib_msg_queuegroupnotificationfree;
	struct iovec iov;

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueGroupNotificationfree\n");

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_msg_queuegroupnotificationfree.header.size =
		sizeof (struct req_lib_msg_queuegroupnotificationfree);
	req_lib_msg_queuegroupnotificationfree.header.id =
		MESSAGE_REQ_MSG_QUEUEGROUPNOTIFICATIONFREE;

	iov.iov_base = &req_lib_msg_queuegroupnotificationfree;
	iov.iov_len = sizeof (struct req_lib_msg_queuegroupnotificationfree);

	error = coroipcc_msg_send_reply_receive (
		msgInstance->ipc_handle,
		&iov,
		1,
		&res_lib_msg_queuegroupnotificationfree,
		sizeof (struct res_lib_msg_queuegroupnotificationfree));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_msg_queuegroupnotificationfree.header.error != SA_AIS_OK) {
		error = res_lib_msg_queuegroupnotificationfree.header.error;
		goto error_put;	/* ! */
	}

error_put:
	hdb_handle_put (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgMessageSend (
	SaMsgHandleT msgHandle,
	const SaNameT *destination,
	const SaMsgMessageT *message,
	SaTimeT timeout)
{
	struct msgInstance *msgInstance;
	struct req_lib_msg_messagesend req_lib_msg_messagesend;
	struct res_lib_msg_messagesend res_lib_msg_messagesend;
	struct iovec iov[2];

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgMessageSend\n");

	if (destination == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	if (message == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	if (message->priority > SA_MSG_MESSAGE_LOWEST_PRIORITY)
	{
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_msg_messagesend.header.size =
		sizeof (struct req_lib_msg_messagesend) + message->size;
	req_lib_msg_messagesend.header.id =
		MESSAGE_REQ_MSG_MESSAGESEND;

	memcpy (&req_lib_msg_messagesend.destination, destination,
		sizeof (SaNameT));
	memcpy (&req_lib_msg_messagesend.message, message,
		sizeof (SaMsgMessageT));

	req_lib_msg_messagesend.timeout = timeout;

	iov[0].iov_base = &req_lib_msg_messagesend;
	iov[0].iov_len = sizeof (struct req_lib_msg_messagesend);
	iov[1].iov_base = message->data;
	iov[1].iov_len = message->size;

	error = coroipcc_msg_send_reply_receive (
		msgInstance->ipc_handle,
		iov,
		2,
		&res_lib_msg_messagesend,
		sizeof (struct res_lib_msg_messagesend));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_msg_messagesend.header.error != SA_AIS_OK) {
		error = res_lib_msg_messagesend.header.error;
		goto error_put;	/* ! */
	}

error_put:
	hdb_handle_put (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgMessageSendAsync (
	SaMsgHandleT msgHandle,
	SaInvocationT invocation,
	const SaNameT *destination,
	const SaMsgMessageT *message,
	SaMsgAckFlagsT ackFlags)
{
	struct msgInstance *msgInstance;
	struct req_lib_msg_messagesendasync req_lib_msg_messagesendasync;
	struct res_lib_msg_messagesendasync res_lib_msg_messagesendasync;
	struct iovec iov[2];

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgMessageSendAsync\n");

	if (destination == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	if (message == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	if ((ackFlags & SA_MSG_MESSAGE_DELIVERED_ACK) &&
	    (msgInstance->callbacks.saMsgMessageDeliveredCallback == NULL))
	{
		error = SA_AIS_ERR_INIT;
		goto error_exit;
	}

	req_lib_msg_messagesendasync.header.size =
		sizeof (struct req_lib_msg_messagesendasync) + message->size;
	req_lib_msg_messagesendasync.header.id =
		MESSAGE_REQ_MSG_MESSAGESENDASYNC;

	memcpy (&req_lib_msg_messagesendasync.destination, destination,
		sizeof (SaNameT));
	memcpy (&req_lib_msg_messagesendasync.message, message,
		sizeof (SaMsgMessageT));

	req_lib_msg_messagesendasync.invocation = invocation;

	iov[0].iov_base = &req_lib_msg_messagesendasync;
	iov[0].iov_len = sizeof (struct req_lib_msg_messagesendasync);
	iov[1].iov_base = message->data;
	iov[1].iov_len = message->size;

	error = coroipcc_msg_send_reply_receive (
		msgInstance->ipc_handle,
		iov,
		2,
		&res_lib_msg_messagesendasync,
		sizeof (struct res_lib_msg_messagesendasync));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_msg_messagesendasync.header.error != SA_AIS_OK) {
		error = res_lib_msg_messagesendasync.header.error;
		goto error_put;	/* ! */
	}

error_put:
	hdb_handle_put (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgMessageGet (
	SaMsgQueueHandleT queueHandle,
	SaMsgMessageT *message,
	SaTimeT *sendTime,
	SaMsgSenderIdT *senderId,
	SaTimeT timeout)
{
	struct queueInstance *queueInstance;
	struct req_lib_msg_messageget req_lib_msg_messageget;
	struct res_lib_msg_messageget *res_lib_msg_messageget;
	struct iovec iov;
	hdb_handle_t ipc_handle;

	void * buffer;

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgMessageGet\n");

	if (message == NULL || senderId == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa(hdb_handle_get (&queueHandleDatabase,
		queueHandle, (void *)&queueInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = coroipcc_service_connect (
		COROSYNC_SOCKET_NAME,
		MSG_SERVICE,
		IPC_REQUEST_SIZE,
		IPC_RESPONSE_SIZE,
		IPC_DISPATCH_SIZE,
		&ipc_handle);
	if (error != SA_AIS_OK) {
		goto error_hdb_put;
	}

	req_lib_msg_messageget.header.size =
		sizeof (struct req_lib_msg_messageget);
	req_lib_msg_messageget.header.id =
		MESSAGE_REQ_MSG_MESSAGEGET;

	req_lib_msg_messageget.queue_id = queueInstance->queue_id;
	req_lib_msg_messageget.pid = (SaUint32T)(getpid());
	req_lib_msg_messageget.timeout = timeout;

	memcpy (&req_lib_msg_messageget.queue_name,
		&queueInstance->queue_name, sizeof (SaNameT));

	iov.iov_base = &req_lib_msg_messageget;
	iov.iov_len = sizeof (struct req_lib_msg_messageget);

	error = coroipcc_msg_send_reply_receive_in_buf_get (
		ipc_handle,
		&iov,
		1,
		&buffer);
	if (error != SA_AIS_OK) {
		goto error_disconnect;
	}

	res_lib_msg_messageget = buffer;

	if (res_lib_msg_messageget->header.error != SA_AIS_OK) {
		error = res_lib_msg_messageget->header.error;
		goto error_ipc_put;
	}

	if (message->data == NULL) {
		message->size = res_lib_msg_messageget->message.size;
		message->data = malloc (message->size);
		if (message->data == NULL) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_ipc_put;
		}
	}
	else {
		if (res_lib_msg_messageget->message.size > message->size) {
			error = SA_AIS_ERR_NO_SPACE;
			goto error_ipc_put;
		}
	}

	memcpy (message->data, ((char *)(buffer) +
		sizeof (struct res_lib_msg_messageget)),
		res_lib_msg_messageget->message.size);

	if (sendTime != NULL) {
		*sendTime = res_lib_msg_messageget->send_time;
	}

	*senderId = res_lib_msg_messageget->sender_id;

error_ipc_put:
	coroipcc_msg_send_reply_receive_in_buf_put (ipc_handle);
error_disconnect:
	coroipcc_service_disconnect (ipc_handle);
error_hdb_put:
	hdb_handle_put (&queueHandleDatabase, queueHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgMessageDataFree (
	SaMsgHandleT msgHandle,
	void *data)
{
	struct msgInstance *msgInstance;
	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgMessageDataFree\n");

	error = hdb_error_to_sa(hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	if (data == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	free (data);

error_exit:
	return (error);
}

SaAisErrorT
saMsgMessageCancel (
	SaMsgQueueHandleT queueHandle)
{
	struct queueInstance *queueInstance;
	struct req_lib_msg_messagecancel req_lib_msg_messagecancel;
	struct res_lib_msg_messagecancel res_lib_msg_messagecancel;
	struct iovec iov;

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgMessageCancel\n");

	error = hdb_error_to_sa (hdb_handle_get (&queueHandleDatabase,
		queueHandle, (void *)&queueInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_msg_messagecancel.header.size =
		sizeof (struct req_lib_msg_messagecancel);
	req_lib_msg_messagecancel.header.id =
		MESSAGE_REQ_MSG_MESSAGECANCEL;

	req_lib_msg_messagecancel.queue_id = queueInstance->queue_id;
	req_lib_msg_messagecancel.pid = (SaUint32T)(getpid());

	memcpy (&req_lib_msg_messagecancel.queue_name,
		&queueInstance->queue_name, sizeof (SaNameT));

	iov.iov_base = &req_lib_msg_messagecancel;
	iov.iov_len = sizeof (struct req_lib_msg_messagecancel);

	error = coroipcc_msg_send_reply_receive (
		queueInstance->ipc_handle,
		&iov,
		1,
		&res_lib_msg_messagecancel,
		sizeof (struct res_lib_msg_messagecancel));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_msg_messagecancel.header.error != SA_AIS_OK) {
		error = res_lib_msg_messagecancel.header.error;
		goto error_put;	/* ! */
	}

error_put:
	hdb_handle_put (&queueHandleDatabase, queueHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgMessageSendReceive (
	SaMsgHandleT msgHandle,
	const SaNameT *destination,
	const SaMsgMessageT *sendMessage,
	SaMsgMessageT *receiveMessage,
	SaTimeT *replySendTime,
	SaTimeT timeout)
{
	struct msgInstance *msgInstance;
	struct req_lib_msg_messagesendreceive req_lib_msg_messagesendreceive;
	struct res_lib_msg_messagesendreceive *res_lib_msg_messagesendreceive;
	struct iovec iov[2];
	hdb_handle_t ipc_handle;

	void * buffer;

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgMessageSendReceive\n");

	if (destination == NULL || sendMessage == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = coroipcc_service_connect (
		COROSYNC_SOCKET_NAME,
		MSG_SERVICE,
		IPC_REQUEST_SIZE,
		IPC_RESPONSE_SIZE,
		IPC_DISPATCH_SIZE,
		&ipc_handle);
	if (error != SA_AIS_OK) {
		goto error_hdb_put;
	}

	req_lib_msg_messagesendreceive.header.size =
		sizeof (struct req_lib_msg_messagesendreceive) + sendMessage->size;
	req_lib_msg_messagesendreceive.header.id =
		MESSAGE_REQ_MSG_MESSAGESENDRECEIVE;

	req_lib_msg_messagesendreceive.timeout = timeout;

	memcpy (&req_lib_msg_messagesendreceive.destination,
		destination, sizeof (SaNameT));
	memcpy (&req_lib_msg_messagesendreceive.message,
		sendMessage, sizeof (SaMsgMessageT));

	iov[0].iov_base = &req_lib_msg_messagesendreceive;
	iov[0].iov_len = sizeof (struct req_lib_msg_messagesendreceive);
	iov[1].iov_base = sendMessage->data;
	iov[1].iov_len = sendMessage->size;

	error = coroipcc_msg_send_reply_receive_in_buf_get (
		ipc_handle,
		iov,
		2,
		&buffer);
	if (error != SA_AIS_OK) {
		goto error_disconnect;
	}

	res_lib_msg_messagesendreceive = buffer;

	if (res_lib_msg_messagesendreceive->header.error != SA_AIS_OK) {
		error = res_lib_msg_messagesendreceive->header.error;
		goto error_ipc_put;
	}

	if (receiveMessage->data == NULL) {
		receiveMessage->size = res_lib_msg_messagesendreceive->message.size;
		receiveMessage->data = malloc (receiveMessage->size);
		if (receiveMessage->data == NULL) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_ipc_put;
		}
	}
	else {
		if (res_lib_msg_messagesendreceive->message.size > receiveMessage->size) {
			error = SA_AIS_ERR_NO_SPACE;
			goto error_ipc_put;
		}
	}

	memcpy (receiveMessage->data, ((char *)(buffer) +
		sizeof (struct res_lib_msg_messagesendreceive)),
		res_lib_msg_messagesendreceive->message.size);

	if (replySendTime != NULL) {
		*replySendTime = res_lib_msg_messagesendreceive->reply_time;
	}

error_ipc_put:
	coroipcc_msg_send_reply_receive_in_buf_put (ipc_handle);
error_disconnect:
	coroipcc_service_disconnect (ipc_handle);
error_hdb_put:
	hdb_handle_put (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgMessageReply (
	SaMsgHandleT msgHandle,
	const SaMsgMessageT *replyMessage,
	const SaMsgSenderIdT *senderId,
	SaTimeT timeout)
{
	struct msgInstance *msgInstance;
	struct req_lib_msg_messagereply req_lib_msg_messagereply;
	struct res_lib_msg_messagereply res_lib_msg_messagereply;
	struct iovec iov[2];

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgMessageReply\n");

	if (replyMessage == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_msg_messagereply.header.size =
		sizeof (struct req_lib_msg_messagereply) + replyMessage->size;
	req_lib_msg_messagereply.header.id =
		MESSAGE_REQ_MSG_MESSAGEREPLY;

	memcpy (&req_lib_msg_messagereply.reply_message,
		replyMessage, sizeof (SaMsgMessageT));

	req_lib_msg_messagereply.sender_id = *senderId;
	req_lib_msg_messagereply.timeout = timeout;

	iov[0].iov_base = &req_lib_msg_messagereply;
	iov[0].iov_len = sizeof (struct req_lib_msg_messagereply);
	iov[1].iov_base = replyMessage->data;
	iov[1].iov_len = replyMessage->size;

	error = coroipcc_msg_send_reply_receive (
		msgInstance->ipc_handle,
		iov,
		2,
		&res_lib_msg_messagereply,
		sizeof (struct res_lib_msg_messagereply));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_msg_messagereply.header.error != SA_AIS_OK) {
		error = res_lib_msg_messagereply.header.error;
		goto error_put;	/* ! */
	}

error_put:
	hdb_handle_put (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgMessageReplyAsync (
	SaMsgHandleT msgHandle,
	SaInvocationT invocation,
	const SaMsgMessageT *replyMessage,
	const SaMsgSenderIdT *senderId,
	SaMsgAckFlagsT ackFlags)
{
	struct msgInstance *msgInstance;
	struct req_lib_msg_messagereplyasync req_lib_msg_messagereplyasync;
	struct res_lib_msg_messagereplyasync res_lib_msg_messagereplyasync;
	struct iovec iov[2];

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgMessageReply\n");

	if (replyMessage == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	if ((ackFlags & SA_MSG_MESSAGE_DELIVERED_ACK) &&
	    (msgInstance->callbacks.saMsgMessageDeliveredCallback == NULL))
	{
		error = SA_AIS_ERR_INIT;
		goto error_exit;
	}

	req_lib_msg_messagereplyasync.header.size =
		sizeof (struct req_lib_msg_messagereplyasync) + replyMessage->size;
	req_lib_msg_messagereplyasync.header.id =
		MESSAGE_REQ_MSG_MESSAGEREPLYASYNC;

	memcpy (&req_lib_msg_messagereplyasync.reply_message,
		replyMessage, sizeof (SaMsgMessageT));

	req_lib_msg_messagereplyasync.sender_id = *senderId;
	req_lib_msg_messagereplyasync.invocation = invocation;

	iov[0].iov_base = &req_lib_msg_messagereplyasync;
	iov[0].iov_len = sizeof (struct req_lib_msg_messagereplyasync);
	iov[1].iov_base = replyMessage->data;
	iov[1].iov_len = replyMessage->size;

	error = coroipcc_msg_send_reply_receive (
		msgInstance->ipc_handle,
		iov,
		2,
		&res_lib_msg_messagereplyasync,
		sizeof (struct res_lib_msg_messagereplyasync));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_msg_messagereplyasync.header.error != SA_AIS_OK) {
		error = res_lib_msg_messagereplyasync.header.error;
		goto error_put;	/* ! */
	}

error_put:
	hdb_handle_put (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgQueueCapacityThresholdSet (
	SaMsgQueueHandleT queueHandle,
	const SaMsgQueueThresholdsT *thresholds)
{
	struct queueInstance *queueInstance;
	struct req_lib_msg_queuecapacitythresholdset req_lib_msg_queuecapacitythresholdset;
	struct res_lib_msg_queuecapacitythresholdset res_lib_msg_queuecapacitythresholdset;
	struct iovec iov;

	int i;

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueCapacityThresholdSet\n");

	if (thresholds == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&queueHandleDatabase,
		queueHandle, (void *)&queueInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
		if ((thresholds->capacityAvailable[i]  > thresholds->capacityReached[i]) ||
		    (thresholds->capacityReached[i] > queueInstance->create_attrs.size[i]))
		{
			error = SA_AIS_ERR_INVALID_PARAM;
			goto error_exit;
		}
	}

	req_lib_msg_queuecapacitythresholdset.header.size =
		sizeof (struct req_lib_msg_queuecapacitythresholdset);
	req_lib_msg_queuecapacitythresholdset.header.id =
		MESSAGE_REQ_MSG_QUEUECAPACITYTHRESHOLDSET;
	req_lib_msg_queuecapacitythresholdset.queue_id =
		queueInstance->queue_id;

	memcpy (&req_lib_msg_queuecapacitythresholdset.queue_name,
		&queueInstance->queue_name, sizeof (SaNameT));
	memcpy (&req_lib_msg_queuecapacitythresholdset.thresholds,
		thresholds, sizeof (SaMsgQueueThresholdsT));

	iov.iov_base = &req_lib_msg_queuecapacitythresholdset;
	iov.iov_len = sizeof (struct req_lib_msg_queuecapacitythresholdset);

	error = coroipcc_msg_send_reply_receive (
		queueInstance->ipc_handle,
		&iov,
		1,
		&res_lib_msg_queuecapacitythresholdset,
		sizeof (struct res_lib_msg_queuecapacitythresholdset));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_msg_queuecapacitythresholdset.header.error != SA_AIS_OK) {
		error = res_lib_msg_queuecapacitythresholdset.header.error;
		goto error_put;	/* ! */
	}

error_put:
	hdb_handle_put (&queueHandleDatabase, queueHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgQueueCapacityThresholdGet (
	SaMsgQueueHandleT queueHandle,
	SaMsgQueueThresholdsT *thresholds)
{
	struct queueInstance *queueInstance;
	struct req_lib_msg_queuecapacitythresholdget req_lib_msg_queuecapacitythresholdget;
	struct res_lib_msg_queuecapacitythresholdget res_lib_msg_queuecapacitythresholdget;
	struct iovec iov;

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgQueueCapacityThresholdGet\n");

	if (thresholds == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&queueHandleDatabase,
		queueHandle, (void *)&queueInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_msg_queuecapacitythresholdget.header.size =
		sizeof (struct req_lib_msg_queuecapacitythresholdget);
	req_lib_msg_queuecapacitythresholdget.header.id =
		MESSAGE_REQ_MSG_QUEUECAPACITYTHRESHOLDGET;
	req_lib_msg_queuecapacitythresholdget.queue_id =
		queueInstance->queue_id;

	memcpy (&req_lib_msg_queuecapacitythresholdget.queue_name,
		&queueInstance->queue_name, sizeof (SaNameT));

	iov.iov_base = &req_lib_msg_queuecapacitythresholdget;
	iov.iov_len = sizeof (struct req_lib_msg_queuecapacitythresholdget);

	error = coroipcc_msg_send_reply_receive (
		queueInstance->ipc_handle,
		&iov,
		1,
		&res_lib_msg_queuecapacitythresholdget,
		sizeof (struct res_lib_msg_queuecapacitythresholdget));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_msg_queuecapacitythresholdget.header.error != SA_AIS_OK) {
		error = res_lib_msg_queuecapacitythresholdget.header.error;
		goto error_put;	/* ! */
	}

	memcpy (thresholds, &res_lib_msg_queuecapacitythresholdget.thresholds,
		sizeof (SaMsgQueueThresholdsT));

error_put:
	hdb_handle_put (&queueHandleDatabase, queueHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgMetadataSizeGet (
	SaMsgHandleT msgHandle,
	SaUint32T *metadataSize)
{
	struct msgInstance *msgInstance;
	struct req_lib_msg_metadatasizeget req_lib_msg_metadatasizeget;
	struct res_lib_msg_metadatasizeget res_lib_msg_metadatasizeget;
	struct iovec iov;

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgMetadataSizeGet\n");

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_msg_metadatasizeget.header.size =
		sizeof (struct req_lib_msg_metadatasizeget);
	req_lib_msg_metadatasizeget.header.id =
		MESSAGE_REQ_MSG_METADATASIZEGET;

	iov.iov_base = &req_lib_msg_metadatasizeget;
	iov.iov_len = sizeof (struct req_lib_msg_metadatasizeget);

	error = coroipcc_msg_send_reply_receive (
		msgInstance->ipc_handle,
		&iov,
		1,
		&res_lib_msg_metadatasizeget,
		sizeof (struct res_lib_msg_metadatasizeget));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_msg_metadatasizeget.header.error != SA_AIS_OK) {
		error = res_lib_msg_metadatasizeget.header.error;
		goto error_put;
	}

error_put:
	hdb_handle_put (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}

SaAisErrorT
saMsgLimitGet (
	SaMsgHandleT msgHandle,
	SaMsgLimitIdT limitId,
	SaLimitValueT *limitValue)
{
	struct msgInstance *msgInstance;
	struct req_lib_msg_limitget req_lib_msg_limitget;
	struct res_lib_msg_limitget res_lib_msg_limitget;
	struct iovec iov;

	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saMsgLimitGet\n");

	if (limitValue == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa (hdb_handle_get (&msgHandleDatabase,
		msgHandle, (void *)&msgInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_msg_limitget.header.size =
		sizeof (struct req_lib_msg_limitget);
	req_lib_msg_limitget.header.id =
		MESSAGE_REQ_MSG_LIMITGET;
	req_lib_msg_limitget.limit_id = limitId;

	iov.iov_base = &req_lib_msg_limitget;
	iov.iov_len = sizeof (struct req_lib_msg_limitget);

	error = coroipcc_msg_send_reply_receive (
		msgInstance->ipc_handle,
		&iov,
		1,
		&res_lib_msg_limitget,
		sizeof (struct res_lib_msg_limitget));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (res_lib_msg_limitget.header.error != SA_AIS_OK) {
		error = res_lib_msg_limitget.header.error;
		goto error_put;
	}

	(*limitValue).uint64Value = res_lib_msg_limitget.value;

error_put:
	hdb_handle_put (&msgHandleDatabase, msgHandle);
error_exit:
	return (error);
}
