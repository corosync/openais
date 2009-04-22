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
#include <saTmr.h>

#include <corosync/coroipcc.h>
#include <corosync/list.h>
#include <corosync/ipc_gen.h>

#include "../include/ipc_tmr.h"
#include "util.h"

struct tmrInstance {
	void *ipc_ctx;
	SaTmrCallbacksT callbacks;
	int finalize;
	SaTmrHandleT tmrHandle;
	pthread_mutex_t response_mutex;
	pthread_mutex_t dispatch_mutex;
};

void tmrHandleInstanceDestructor (void *instance);

DECLARE_SAHDB_DATABASE(tmrHandleDatabase,tmrHandleInstanceDestructor);

static SaVersionT tmrVersionsSupported[] = {
	{ 'A', 1, 1 }
};

static struct saVersionDatabase tmrVersionDatabase = {
	sizeof (tmrVersionsSupported) / sizeof (SaVersionT),
	tmrVersionsSupported
};

void tmrHandleInstanceDestructor (void *instance)
{
	struct tmrInstance *tmrInstance = instance;

	pthread_mutex_destroy (&tmrInstance->response_mutex);
	pthread_mutex_destroy (&tmrInstance->dispatch_mutex);
}

#ifdef COMPILE_OUT
static void tmrInstanceFinalize (struct tmrInstance *tmrInstance)
{
	return;
}
#endif /* COMPILE_OUT */

SaAisErrorT
saTmrInitialize (
	SaTmrHandleT *tmrHandle,
	const SaTmrCallbacksT *timerCallbacks,
	SaVersionT *version)
{
	struct tmrInstance *tmrInstance;
	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saTmrInitialize\n");

	if (tmrHandle == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = saVersionVerify (&tmrVersionDatabase, version);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = saHandleCreate (&tmrHandleDatabase, sizeof (struct tmrInstance), tmrHandle);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = saHandleInstanceGet (&tmrHandleDatabase, *tmrHandle, (void *)&tmrInstance);
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	error = coroipcc_service_connect (
		IPC_SOCKET_NAME,
		TMR_SERVICE,
		IPC_REQUEST_SIZE,
		IPC_RESPONSE_SIZE,
		IPC_DISPATCH_SIZE,
		&tmrInstance->ipc_ctx);
	if (error != SA_AIS_OK) {
		goto error_put_destroy;
	}

	if (timerCallbacks != NULL) {
		memcpy (&tmrInstance->callbacks, timerCallbacks, sizeof (SaTmrCallbacksT));
	} else {
		memset (&tmrInstance->callbacks, 0, sizeof (SaTmrCallbacksT));
	}

	tmrInstance->tmrHandle = *tmrHandle;

	pthread_mutex_init (&tmrInstance->response_mutex, NULL);

	saHandleInstancePut (&tmrHandleDatabase, *tmrHandle);

	return (SA_AIS_OK);

error_put_destroy:
	saHandleInstancePut (&tmrHandleDatabase, *tmrHandle);
error_destroy:
	saHandleDestroy (&tmrHandleDatabase, *tmrHandle);
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

	/* DEBUG */
	printf ("[DEBUG]: saTmrSelectionObjectGet\n");

	if (selectionObject == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = saHandleInstanceGet (&tmrHandleDatabase, tmrHandle, (void *)&tmrInstance);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	*selectionObject = coroipcc_fd_get (&tmrInstance->ipc_ctx);

	saHandleInstancePut (&tmrHandleDatabase, tmrHandle);

error_exit:
	return (SA_AIS_OK);
}

SaAisErrorT
saTmrDispatch (
	SaTmrHandleT tmrHandle,
	SaDispatchFlagsT dispatchFlags)
{
	SaTmrCallbacksT callbacks;
	SaAisErrorT error = SA_AIS_OK;
	struct tmrInstance *tmrInstance;
	mar_res_header_t *dispatch_data;
	int dispatch_avail;
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

	error = saHandleInstanceGet (&tmrHandleDatabase, tmrHandle,
		(void *)&tmrInstance);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	if (dispatchFlags == SA_DISPATCH_ALL) {
		timeout = 0;
	}

	do {
		pthread_mutex_lock (&tmrInstance->dispatch_mutex);

		dispatch_avail = coroipcc_dispatch_get (
			tmrInstance->ipc_ctx,
			(void **)&dispatch_data,
			timeout);

		pthread_mutex_unlock (&tmrInstance->dispatch_mutex);

		if (dispatch_avail == 0 && dispatchFlags == SA_DISPATCH_ALL) {
			break;
		}
		else if (dispatch_avail == 0) {
			continue;
		}
		if (dispatch_avail == -1) {
			if (tmrInstance->finalize == 1) {
				error = SA_AIS_OK;
			} else {
				error = SA_AIS_ERR_LIBRARY;
			}
			goto error_put;
		}

		memcpy (&callbacks, &tmrInstance->callbacks,
			sizeof (tmrInstance->callbacks));

		switch (dispatch_data->id) {
		case MESSAGE_RES_TMR_TIMEREXPIREDCALLBACK:
			if (callbacks.saTmrTimerExpiredCallback == NULL) {
				continue;
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
		coroipcc_dispatch_put (tmrInstance->ipc_ctx);

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
	saHandleInstancePut (&tmrHandleDatabase, tmrHandle);
error_exit:
	return (error);
}

SaAisErrorT
saTmrFinalize (
	SaTmrHandleT tmrHandle)
{
	struct tmrInstance *tmrInstance;
	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	printf ("[DEBUG]: saTmrFinalize\n");

	error = saHandleInstanceGet (&tmrHandleDatabase, tmrHandle, (void *)&tmrInstance);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	pthread_mutex_lock (&tmrInstance->response_mutex);

	if (tmrInstance->finalize) {
		pthread_mutex_unlock (&tmrInstance->response_mutex);
		saHandleInstancePut (&tmrHandleDatabase, tmrHandle);
		error = SA_AIS_ERR_BAD_HANDLE;
		goto error_exit;
	}

	tmrInstance->finalize = 1;

	coroipcc_service_disconnect (tmrInstance->ipc_ctx);

	pthread_mutex_unlock (&tmrInstance->response_mutex);

	/* tmrInstanceFinalize (tmrInstance); */

	saHandleInstancePut (&tmrHandleDatabase, tmrHandle);

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

	/* DEBUG */
	printf ("[DEBUG]: saTmrTimerStart { data=%p }\n",
		(void *)(timerData));

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

	error = saHandleInstanceGet (&tmrHandleDatabase, tmrHandle, (void *)&tmrInstance);
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

	iov.iov_base = &req_lib_tmr_timerstart;
	iov.iov_len = sizeof (struct req_lib_tmr_timerstart);

	pthread_mutex_lock (&tmrInstance->response_mutex);

	error = coroipcc_msg_send_reply_receive (tmrInstance->ipc_ctx,
		&iov,
		1,
		&res_lib_tmr_timerstart,
		sizeof (struct res_lib_tmr_timerstart));

	pthread_mutex_unlock (&tmrInstance->response_mutex);

	*timerId = (SaTmrTimerIdT)(res_lib_tmr_timerstart.timer_id);
	*callTime = (SaTimeT)(res_lib_tmr_timerstart.call_time);

	if (res_lib_tmr_timerstart.header.error != SA_AIS_OK) {
		error = res_lib_tmr_timerstart.header.error;
	}

error_put:
	saHandleInstancePut (&tmrHandleDatabase, tmrHandle);
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

	/* DEBUG */
	printf ("[DEBUG]: saTmrTimerReschedule { id=%u }\n",
		(unsigned int)(timerId));

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

	error = saHandleInstanceGet (&tmrHandleDatabase, tmrHandle, (void *)&tmrInstance);
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

	iov.iov_base = &req_lib_tmr_timerreschedule,
	iov.iov_len = sizeof (struct req_lib_tmr_timerreschedule);

	pthread_mutex_lock (&tmrInstance->response_mutex);

	error = coroipcc_msg_send_reply_receive (tmrInstance->ipc_ctx,
		&iov,
		1,
		&res_lib_tmr_timerreschedule,
		sizeof (struct res_lib_tmr_timerreschedule));

	pthread_mutex_unlock (&tmrInstance->response_mutex);

	if (res_lib_tmr_timerreschedule.header.error != SA_AIS_OK) {
		error = res_lib_tmr_timerreschedule.header.error;
	}

	saHandleInstancePut (&tmrHandleDatabase, tmrHandle);

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

	/* DEBUG */
	printf ("[DEBUG]: saTmrTimerCancel { id=%u }\n",
		(unsigned int)(timerId));

	if (timerDataP == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = saHandleInstanceGet (&tmrHandleDatabase, tmrHandle, (void *)&tmrInstance);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_tmr_timercancel.header.size =
		sizeof (struct req_lib_tmr_timercancel);
	req_lib_tmr_timercancel.header.id =
		MESSAGE_REQ_TMR_TIMERCANCEL;

	req_lib_tmr_timercancel.timer_id = timerId;

	iov.iov_base = &req_lib_tmr_timercancel,
	iov.iov_len = sizeof (struct req_lib_tmr_timercancel);

	pthread_mutex_lock (&tmrInstance->response_mutex);

	error = coroipcc_msg_send_reply_receive (tmrInstance->ipc_ctx,
		&iov,
		1,
		&res_lib_tmr_timercancel,
		sizeof (struct res_lib_tmr_timercancel));

	pthread_mutex_unlock (&tmrInstance->response_mutex);

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

	/* DEBUG */
	printf ("[DEBUG]: saTmrPeriodicTimerSkip { id=%u }\n",
		(unsigned int)(timerId));

	error = saHandleInstanceGet (&tmrHandleDatabase, tmrHandle, (void *)&tmrInstance);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_tmr_periodictimerskip.header.size =
		sizeof (struct req_lib_tmr_periodictimerskip);
	req_lib_tmr_periodictimerskip.header.id =
		MESSAGE_REQ_TMR_PERIODICTIMERSKIP;

	req_lib_tmr_periodictimerskip.timer_id = timerId;

	iov.iov_base = &req_lib_tmr_periodictimerskip,
	iov.iov_len = sizeof (struct req_lib_tmr_periodictimerskip);

	pthread_mutex_lock (&tmrInstance->response_mutex);

	error = coroipcc_msg_send_reply_receive (tmrInstance->ipc_ctx,
		&iov,
		1,
		&res_lib_tmr_periodictimerskip,
		sizeof (struct res_lib_tmr_periodictimerskip));

	pthread_mutex_unlock (&tmrInstance->response_mutex);

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

	/* DEBUG */
	printf ("[DEBUG]: saTimerRemainingTimeGet { id=%u }\n",
		(unsigned int)(timerId));

	if (remainingTime == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = saHandleInstanceGet (&tmrHandleDatabase, tmrHandle, (void *)&tmrInstance);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_tmr_timerremainingtimeget.header.size =
		sizeof (struct req_lib_tmr_timerremainingtimeget);
	req_lib_tmr_timerremainingtimeget.header.id =
		MESSAGE_REQ_TMR_TIMERREMAININGTIMEGET;

	req_lib_tmr_timerremainingtimeget.timer_id = timerId;

	iov.iov_base = &req_lib_tmr_timerremainingtimeget,
	iov.iov_len = sizeof (struct req_lib_tmr_timerremainingtimeget);

	pthread_mutex_lock (&tmrInstance->response_mutex);

	error = coroipcc_msg_send_reply_receive (tmrInstance->ipc_ctx,
		&iov,
		1,
		&res_lib_tmr_timerremainingtimeget,
		sizeof (struct res_lib_tmr_timerremainingtimeget));

	pthread_mutex_unlock (&tmrInstance->response_mutex);

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

	/* DEBUG */
	printf ("[DEBUG]: saTmrTimerAttributesGet { id=%u }\n",
		(unsigned int)(timerId));

	if (timerAttributes == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = saHandleInstanceGet (&tmrHandleDatabase, tmrHandle, (void *)&tmrInstance);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_tmr_timerattributesget.header.size =
		sizeof (struct req_lib_tmr_timerattributesget);
	req_lib_tmr_timerattributesget.header.id =
		MESSAGE_REQ_TMR_TIMERATTRIBUTESGET;

	req_lib_tmr_timerattributesget.timer_id = timerId;

	iov.iov_base = &req_lib_tmr_timerattributesget,
	iov.iov_len = sizeof (struct req_lib_tmr_timerattributesget);

	pthread_mutex_lock (&tmrInstance->response_mutex);

	error = coroipcc_msg_send_reply_receive (tmrInstance->ipc_ctx,
		&iov,
		1,
		&res_lib_tmr_timerattributesget,
		sizeof (struct res_lib_tmr_timerattributesget));

	pthread_mutex_unlock (&tmrInstance->response_mutex);

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

	/* DEBUG */
	printf ("[DEBUG]: saTmrTimeGet\n");

	if (currentTime == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = saHandleInstanceGet (&tmrHandleDatabase, tmrHandle, (void *)&tmrInstance);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_tmr_timeget.header.size =
		sizeof (struct req_lib_tmr_timeget);
	req_lib_tmr_timeget.header.id =
		MESSAGE_REQ_TMR_TIMEGET;

	iov.iov_base = &req_lib_tmr_timeget,
	iov.iov_len = sizeof (struct req_lib_tmr_timeget);

	pthread_mutex_lock (&tmrInstance->response_mutex);

	error = coroipcc_msg_send_reply_receive (tmrInstance->ipc_ctx,
		&iov,
		1,
		&res_lib_tmr_timeget,
		sizeof (struct res_lib_tmr_timeget));

	pthread_mutex_unlock (&tmrInstance->response_mutex);

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

	/* DEBUG */
	printf ("[DEBUG]: saTmrClockTickGet\n");

	if (clockTick == NULL) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	error = saHandleInstanceGet (&tmrHandleDatabase, tmrHandle, (void *)&tmrInstance);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_tmr_clocktickget.header.size =
		sizeof (struct req_lib_tmr_clocktickget);
	req_lib_tmr_clocktickget.header.id =
		MESSAGE_REQ_TMR_CLOCKTICKGET;

	iov.iov_base = &req_lib_tmr_clocktickget,
	iov.iov_len = sizeof (struct req_lib_tmr_clocktickget);

	pthread_mutex_lock (&tmrInstance->response_mutex);

	error = coroipcc_msg_send_reply_receive (tmrInstance->ipc_ctx,
		&iov,
		1,
		&res_lib_tmr_clocktickget,
		sizeof (struct res_lib_tmr_clocktickget));

	pthread_mutex_unlock (&tmrInstance->response_mutex);

	if (res_lib_tmr_clocktickget.header.error != SA_AIS_OK) {
		error = res_lib_tmr_clocktickget.header.error;
	}

	memcpy (clockTick, &res_lib_tmr_clocktickget.clock_tick,
		sizeof (SaTimeT));

error_exit:
	return (error);
}
