/*
 * Copyright (c) 2008-2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Ryan O'Hara (rohara@redhat.com)
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
#include <saTmr.h>

#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include <corosync/coroipcc.h>
#include <corosync/corodefs.h>
#include <corosync/hdb.h>
#include <corosync/list.h>

#include "../include/ipc_tmr.h"
#include "util.h"

struct tmrInstance {
	hdb_handle_t ipc_handle;
	SaTmrCallbacksT callbacks;
	int finalize;
	SaTmrHandleT tmrHandle;
};

DECLARE_HDB_DATABASE (tmrHandleDatabase, NULL);

static SaVersionT tmrVersionsSupported[] = {
	{ 'A', 1, 1 }
};

static struct saVersionDatabase tmrVersionDatabase = {
	sizeof (tmrVersionsSupported) / sizeof (SaVersionT),
	tmrVersionsSupported
};

SaAisErrorT
saTmrInitialize (
	SaTmrHandleT *tmrHandle,
	const SaTmrCallbacksT *timerCallbacks,
	SaVersionT *version)
{
	struct tmrInstance *tmrInstance;
	SaAisErrorT error = SA_AIS_OK;

	if (tmrHandle == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = saVersionVerify (&tmrVersionDatabase, version);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = hdb_error_to_sa(hdb_handle_create (&tmrHandleDatabase, sizeof (struct tmrInstance), tmrHandle));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = hdb_error_to_sa(hdb_handle_get (&tmrHandleDatabase, *tmrHandle, (void *)&tmrInstance));
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	error = coroipcc_service_connect (
		COROSYNC_SOCKET_NAME,
		TMR_SERVICE,
		IPC_REQUEST_SIZE,
		IPC_RESPONSE_SIZE,
		IPC_DISPATCH_SIZE,
		&tmrInstance->ipc_handle);
	if (error != SA_AIS_OK) {
		goto error_put_destroy;
	}

	if (timerCallbacks != NULL) {
		memcpy (&tmrInstance->callbacks, timerCallbacks, sizeof (SaTmrCallbacksT));
	} else {
		memset (&tmrInstance->callbacks, 0, sizeof (SaTmrCallbacksT));
	}

	tmrInstance->tmrHandle = *tmrHandle;

	hdb_handle_put (&tmrHandleDatabase, *tmrHandle);

	return (SA_AIS_OK);

error_put_destroy:
	hdb_handle_put (&tmrHandleDatabase, *tmrHandle);
error_destroy:
	hdb_handle_destroy (&tmrHandleDatabase, *tmrHandle);
error_exit:
	return (error);
}

SaAisErrorT
saTmrSelectionObjectGet (
	SaTmrHandleT tmrHandle,
	SaSelectionObjectT *selectionObject)
{
	struct tmrInstance *tmrInstance;
	SaAisErrorT error = SA_AIS_OK;
	int fd;

	if (selectionObject == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa(hdb_handle_get (&tmrHandleDatabase, tmrHandle, (void *)&tmrInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = coroipcc_fd_get (tmrInstance->ipc_handle, &fd);
	*selectionObject = fd;

	hdb_handle_put (&tmrHandleDatabase, tmrHandle);

error_exit:
	return (error);
}

SaAisErrorT
saTmrDispatch (
	SaTmrHandleT tmrHandle,
	SaDispatchFlagsT dispatchFlags)
{
	SaTmrCallbacksT callbacks;
	SaAisErrorT error = SA_AIS_OK;
	struct tmrInstance *tmrInstance;
	coroipc_response_header_t *dispatch_data;
	int timeout = 1;
	int cont = 1;

	struct res_lib_tmr_timerexpiredcallback *res_lib_tmr_timerexpiredcallback;

	if (dispatchFlags != SA_DISPATCH_ONE &&
	    dispatchFlags != SA_DISPATCH_ALL &&
	    dispatchFlags != SA_DISPATCH_BLOCKING)
	{
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa(hdb_handle_get (&tmrHandleDatabase, tmrHandle,
		(void *)&tmrInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	if (dispatchFlags == SA_DISPATCH_ALL) {
		timeout = 0;
	}

	do {
		error = coroipcc_dispatch_get (
			tmrInstance->ipc_handle,
			(void **)&dispatch_data,
			timeout);
		if (error == CS_ERR_BAD_HANDLE) {
			error = CS_OK;
			goto error_put;
		}
		if (error == CS_ERR_TRY_AGAIN) {
			error = CS_OK;
			if (dispatchFlags == CPG_DISPATCH_ALL) {
				break;
			} else {
				continue;
			}
		}
		if (error != CS_OK) {
			goto error_put;
		}

		memcpy (&callbacks, &tmrInstance->callbacks,
			sizeof (tmrInstance->callbacks));

		switch (dispatch_data->id) {
		case MESSAGE_RES_TMR_TIMEREXPIREDCALLBACK:
			if (callbacks.saTmrTimerExpiredCallback == NULL) {
				break;
			}

			res_lib_tmr_timerexpiredcallback =
				(struct res_lib_tmr_timerexpiredcallback *) dispatch_data;

			callbacks.saTmrTimerExpiredCallback (
				res_lib_tmr_timerexpiredcallback->timer_id,
				res_lib_tmr_timerexpiredcallback->timer_data,
				res_lib_tmr_timerexpiredcallback->expiration_count);

			break;
		default:
			break;
		}
		coroipcc_dispatch_put (tmrInstance->ipc_handle);

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
	hdb_handle_put (&tmrHandleDatabase, tmrHandle);
error_exit:
	return (error);
}

SaAisErrorT
saTmrFinalize (
	SaTmrHandleT tmrHandle)
{
	struct tmrInstance *tmrInstance;
	SaAisErrorT error = SA_AIS_OK;

	error = hdb_error_to_sa(hdb_handle_get (&tmrHandleDatabase, tmrHandle, (void *)&tmrInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	if (tmrInstance->finalize) {
		hdb_handle_put (&tmrHandleDatabase, tmrHandle);
		error = SA_AIS_ERR_BAD_HANDLE;
		goto error_exit;
	}

	tmrInstance->finalize = 1;

	coroipcc_service_disconnect (tmrInstance->ipc_handle);

	hdb_handle_put (&tmrHandleDatabase, tmrHandle);

error_exit:
	return (SA_AIS_OK);
}

SaAisErrorT
saTmrTimerStart (
	SaTmrHandleT tmrHandle,
	const SaTmrTimerAttributesT *timerAttributes,
	const void *timerData,
	SaTmrTimerIdT *timerId,
	SaTimeT *callTime)
{
	struct tmrInstance *tmrInstance;
	SaAisErrorT error = SA_AIS_OK;

	struct req_lib_tmr_timerstart req_lib_tmr_timerstart;
	struct res_lib_tmr_timerstart res_lib_tmr_timerstart;
	struct iovec iov;

	if ((timerAttributes == NULL) || (callTime == NULL) || (timerId == NULL)) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	if ((timerAttributes->type != SA_TIME_ABSOLUTE) &&
	    (timerAttributes->type != SA_TIME_DURATION)) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	if (timerAttributes->initialExpirationTime < 0) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	if (timerAttributes->timerPeriodDuration < 0) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa(hdb_handle_get (&tmrHandleDatabase, tmrHandle, (void *)&tmrInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	if (tmrInstance->callbacks.saTmrTimerExpiredCallback == NULL) {
		error = SA_AIS_ERR_INIT;
		goto error_put;
	}

	req_lib_tmr_timerstart.header.size =
		sizeof (struct req_lib_tmr_timerstart);
	req_lib_tmr_timerstart.header.id =
		MESSAGE_REQ_TMR_TIMERSTART;

	memcpy (&req_lib_tmr_timerstart.timer_attributes,
		timerAttributes, sizeof (SaTmrTimerAttributesT));

	req_lib_tmr_timerstart.timer_data = (void *)timerData;

	iov.iov_base = (void *)&req_lib_tmr_timerstart;
	iov.iov_len = sizeof (struct req_lib_tmr_timerstart);

	error = coroipcc_msg_send_reply_receive (tmrInstance->ipc_handle,
		&iov,
		1,
		&res_lib_tmr_timerstart,
		sizeof (struct res_lib_tmr_timerstart));

	*timerId = (SaTmrTimerIdT)(res_lib_tmr_timerstart.timer_id);
	*callTime = (SaTimeT)(res_lib_tmr_timerstart.call_time);

	if (res_lib_tmr_timerstart.header.error != SA_AIS_OK) {
		error = res_lib_tmr_timerstart.header.error;
	}

error_put:
	hdb_handle_put (&tmrHandleDatabase, tmrHandle);
error_exit:
	return (error);
}

SaAisErrorT
saTmrTimerReschedule (
	SaTmrHandleT tmrHandle,
	SaTmrTimerIdT timerId,
	const SaTmrTimerAttributesT *timerAttributes,
	SaTimeT *callTime)
{
	struct tmrInstance *tmrInstance;
	SaAisErrorT error = SA_AIS_OK;

	struct req_lib_tmr_timerreschedule req_lib_tmr_timerreschedule;
	struct res_lib_tmr_timerreschedule res_lib_tmr_timerreschedule;
	struct iovec iov;

	if ((timerAttributes == NULL) || (callTime == NULL)) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	if ((timerAttributes->type != SA_TIME_ABSOLUTE) &&
	    (timerAttributes->type != SA_TIME_DURATION))
	{
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa(hdb_handle_get (&tmrHandleDatabase, tmrHandle, (void *)&tmrInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_tmr_timerreschedule.header.size =
		sizeof (struct req_lib_tmr_timerreschedule);
	req_lib_tmr_timerreschedule.header.id =
		MESSAGE_REQ_TMR_TIMERRESCHEDULE;

	req_lib_tmr_timerreschedule.timer_id = timerId;

	memcpy (&req_lib_tmr_timerreschedule.timer_attributes,
		timerAttributes, sizeof (SaTmrTimerAttributesT));

	iov.iov_base = (void *)&req_lib_tmr_timerreschedule,
	iov.iov_len = sizeof (struct req_lib_tmr_timerreschedule);

	error = coroipcc_msg_send_reply_receive (tmrInstance->ipc_handle,
		&iov,
		1,
		&res_lib_tmr_timerreschedule,
		sizeof (struct res_lib_tmr_timerreschedule));

	if (res_lib_tmr_timerreschedule.header.error != SA_AIS_OK) {
		error = res_lib_tmr_timerreschedule.header.error;
	}

	hdb_handle_put (&tmrHandleDatabase, tmrHandle);

error_exit:
	return (error);
}

SaAisErrorT
saTmrTimerCancel (
	SaTmrHandleT tmrHandle,
	SaTmrTimerIdT timerId,
	void **timerDataP)
{
	struct tmrInstance *tmrInstance;
	SaAisErrorT error = SA_AIS_OK;

	struct req_lib_tmr_timercancel req_lib_tmr_timercancel;
	struct res_lib_tmr_timercancel res_lib_tmr_timercancel;
	struct iovec iov;

	if (timerDataP == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa(hdb_handle_get (&tmrHandleDatabase, tmrHandle, (void *)&tmrInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_tmr_timercancel.header.size =
		sizeof (struct req_lib_tmr_timercancel);
	req_lib_tmr_timercancel.header.id =
		MESSAGE_REQ_TMR_TIMERCANCEL;

	req_lib_tmr_timercancel.timer_id = timerId;

	iov.iov_base = (void *)&req_lib_tmr_timercancel,
	iov.iov_len = sizeof (struct req_lib_tmr_timercancel);

	error = coroipcc_msg_send_reply_receive (tmrInstance->ipc_handle,
		&iov,
		1,
		&res_lib_tmr_timercancel,
		sizeof (struct res_lib_tmr_timercancel));

	if (res_lib_tmr_timercancel.header.error != SA_AIS_OK) {
		error = res_lib_tmr_timercancel.header.error;
	}

	if (error == SA_AIS_OK) {
		*timerDataP = res_lib_tmr_timercancel.timer_data;
	}

error_exit:
	return (error);
}

SaAisErrorT
saTmrPeriodicTimerSkip (
	SaTmrHandleT tmrHandle,
	SaTmrTimerIdT timerId)
{
	struct tmrInstance *tmrInstance;
	SaAisErrorT error = SA_AIS_OK;

	struct req_lib_tmr_periodictimerskip req_lib_tmr_periodictimerskip;
	struct res_lib_tmr_periodictimerskip res_lib_tmr_periodictimerskip;
	struct iovec iov;

	error = hdb_error_to_sa(hdb_handle_get (&tmrHandleDatabase, tmrHandle, (void *)&tmrInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_tmr_periodictimerskip.header.size =
		sizeof (struct req_lib_tmr_periodictimerskip);
	req_lib_tmr_periodictimerskip.header.id =
		MESSAGE_REQ_TMR_PERIODICTIMERSKIP;

	req_lib_tmr_periodictimerskip.timer_id = timerId;

	iov.iov_base = (void *)&req_lib_tmr_periodictimerskip,
	iov.iov_len = sizeof (struct req_lib_tmr_periodictimerskip);

	error = coroipcc_msg_send_reply_receive (tmrInstance->ipc_handle,
		&iov,
		1,
		&res_lib_tmr_periodictimerskip,
		sizeof (struct res_lib_tmr_periodictimerskip));

	if (res_lib_tmr_periodictimerskip.header.error != SA_AIS_OK) {
		error = res_lib_tmr_periodictimerskip.header.error;
	}

error_exit:
	return (error);
}

SaAisErrorT
saTmrTimerRemainingTimeGet (
	SaTmrHandleT tmrHandle,
	SaTmrTimerIdT timerId,
	SaTimeT *remainingTime)
{
	struct tmrInstance *tmrInstance;
	SaAisErrorT error = SA_AIS_OK;

	struct req_lib_tmr_timerremainingtimeget req_lib_tmr_timerremainingtimeget;
	struct res_lib_tmr_timerremainingtimeget res_lib_tmr_timerremainingtimeget;
	struct iovec iov;

	if (remainingTime == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa(hdb_handle_get (&tmrHandleDatabase, tmrHandle, (void *)&tmrInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_tmr_timerremainingtimeget.header.size =
		sizeof (struct req_lib_tmr_timerremainingtimeget);
	req_lib_tmr_timerremainingtimeget.header.id =
		MESSAGE_REQ_TMR_TIMERREMAININGTIMEGET;

	req_lib_tmr_timerremainingtimeget.timer_id = timerId;

	iov.iov_base = (void *)&req_lib_tmr_timerremainingtimeget,
	iov.iov_len = sizeof (struct req_lib_tmr_timerremainingtimeget);

	error = coroipcc_msg_send_reply_receive (tmrInstance->ipc_handle,
		&iov,
		1,
		&res_lib_tmr_timerremainingtimeget,
		sizeof (struct res_lib_tmr_timerremainingtimeget));

	if (res_lib_tmr_timerremainingtimeget.header.error != SA_AIS_OK) {
		error = res_lib_tmr_timerremainingtimeget.header.error;
	}

	*remainingTime = res_lib_tmr_timerremainingtimeget.remaining_time;

error_exit:
	return (error);
}

SaAisErrorT
saTmrTimerAttributesGet (
	SaTmrHandleT tmrHandle,
	SaTmrTimerIdT timerId,
	SaTmrTimerAttributesT *timerAttributes)
{
	struct tmrInstance *tmrInstance;
	SaAisErrorT error = SA_AIS_OK;

	struct req_lib_tmr_timerattributesget req_lib_tmr_timerattributesget;
	struct res_lib_tmr_timerattributesget res_lib_tmr_timerattributesget;
	struct iovec iov;

	if (timerAttributes == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa(hdb_handle_get (&tmrHandleDatabase, tmrHandle, (void *)&tmrInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_tmr_timerattributesget.header.size =
		sizeof (struct req_lib_tmr_timerattributesget);
	req_lib_tmr_timerattributesget.header.id =
		MESSAGE_REQ_TMR_TIMERATTRIBUTESGET;

	req_lib_tmr_timerattributesget.timer_id = timerId;

	iov.iov_base = (void *)&req_lib_tmr_timerattributesget,
	iov.iov_len = sizeof (struct req_lib_tmr_timerattributesget);

	error = coroipcc_msg_send_reply_receive (tmrInstance->ipc_handle,
		&iov,
		1,
		&res_lib_tmr_timerattributesget,
		sizeof (struct res_lib_tmr_timerattributesget));

	if (res_lib_tmr_timerattributesget.header.error != SA_AIS_OK) {
		error = res_lib_tmr_timerattributesget.header.error;
	}

	memcpy (timerAttributes, &res_lib_tmr_timerattributesget.timer_attributes,
		sizeof (SaTmrTimerAttributesT));

error_exit:
	return (error);
}

SaAisErrorT
saTmrTimeGet (
	SaTmrHandleT tmrHandle,
	SaTimeT *currentTime)
{
	struct tmrInstance *tmrInstance;
	SaAisErrorT error = SA_AIS_OK;

	struct req_lib_tmr_timeget req_lib_tmr_timeget;
	struct res_lib_tmr_timeget res_lib_tmr_timeget;
	struct iovec iov;

	if (currentTime == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa(hdb_handle_get (&tmrHandleDatabase, tmrHandle, (void *)&tmrInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_tmr_timeget.header.size =
		sizeof (struct req_lib_tmr_timeget);
	req_lib_tmr_timeget.header.id =
		MESSAGE_REQ_TMR_TIMEGET;

	iov.iov_base = (void *)&req_lib_tmr_timeget,
	iov.iov_len = sizeof (struct req_lib_tmr_timeget);

	error = coroipcc_msg_send_reply_receive (tmrInstance->ipc_handle,
		&iov,
		1,
		&res_lib_tmr_timeget,
		sizeof (struct res_lib_tmr_timeget));

	if (res_lib_tmr_timeget.header.error != SA_AIS_OK) {
		error = res_lib_tmr_timeget.header.error;
	}

	memcpy (currentTime, &res_lib_tmr_timeget.current_time,
		sizeof (SaTimeT));

error_exit:
	return (error);
}

SaAisErrorT
saTmrClockTickGet (
	SaTmrHandleT tmrHandle,
	SaTimeT *clockTick)
{
	struct tmrInstance *tmrInstance;
	SaAisErrorT error = SA_AIS_OK;

	struct req_lib_tmr_clocktickget req_lib_tmr_clocktickget;
	struct res_lib_tmr_clocktickget res_lib_tmr_clocktickget;
	struct iovec iov;

	if (clockTick == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = hdb_error_to_sa(hdb_handle_get (&tmrHandleDatabase, tmrHandle, (void *)&tmrInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_tmr_clocktickget.header.size =
		sizeof (struct req_lib_tmr_clocktickget);
	req_lib_tmr_clocktickget.header.id =
		MESSAGE_REQ_TMR_CLOCKTICKGET;

	iov.iov_base = (void *)&req_lib_tmr_clocktickget,
	iov.iov_len = sizeof (struct req_lib_tmr_clocktickget);

	error = coroipcc_msg_send_reply_receive (tmrInstance->ipc_handle,
		&iov,
		1,
		&res_lib_tmr_clocktickget,
		sizeof (struct res_lib_tmr_clocktickget));

	if (res_lib_tmr_clocktickget.header.error != SA_AIS_OK) {
		error = res_lib_tmr_clocktickget.header.error;
	}

	memcpy (clockTick, &res_lib_tmr_clocktickget.clock_tick,
		sizeof (SaTimeT));

error_exit:
	return (error);
}
