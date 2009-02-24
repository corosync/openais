/*
 * Copyright (c) 2008 Red Hat, Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>

#include "saAis.h"
#include "saTmr.h"

#define TMR_10_SECONDS (SaTimeT)(10000000000)
#define TMR_20_SECONDS (SaTimeT)(20000000000)
#define TMR_30_SECONDS (SaTimeT)(30000000000)

void TimerExpiredCallback (
	SaTmrTimerIdT timerId,
	const void *timerData,
	SaUint64T expirationCount)
{
	/* DEBUG */
	printf ("[DEBUG]: TimerExpiredCallback\n");
	printf ("[DEBUG]:\t timerId = %"PRIu64"\n", timerId);
	printf ("[DEBUG]:\t timerData = %p\n", timerData);
	printf ("[DEBUG]:\t expirationCount = %"PRIu64"\n", expirationCount);

	return;
}

SaTmrCallbacksT callbacks = {
	.saTmrTimerExpiredCallback = TimerExpiredCallback
};

SaVersionT version = { 'A', 1, 1 };

void *tmr_dispatch (void *data)
{
	SaTmrHandleT *handle = (SaTmrHandleT *)data;

	saTmrDispatch (*handle, SA_DISPATCH_BLOCKING);

	return (0);
}

int main (void)
{
	SaTmrHandleT handle;
	SaSelectionObjectT select_obj;
	SaTmrTimerAttributesT attrs;
	SaTmrTimerAttributesT attrs_a = { SA_TIME_DURATION, TMR_30_SECONDS, 0 };
	SaTmrTimerAttributesT attrs_b = { SA_TIME_DURATION, TMR_30_SECONDS, 0 };
	SaTmrTimerAttributesT new_attrs_a = { SA_TIME_DURATION, TMR_10_SECONDS, TMR_10_SECONDS };
	SaTmrTimerAttributesT new_attrs_b = { SA_TIME_DURATION, TMR_20_SECONDS, TMR_20_SECONDS };
	SaTmrTimerIdT id_a;
	SaTmrTimerIdT id_b;
	SaTimeT current_time;
	SaTimeT clock_tick;
	SaTimeT call_time;
	SaTimeT expire_time;

	void *data_a = (void *)(0xDEADBEEF);
	void *data_b = (void *)(0xDECAFBAD);
	void *cancel_data = NULL;

	pthread_t dispatch_thread;

	int result;

	result = saTmrInitialize (&handle, &callbacks, &version);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrInitialize\n", result);
		exit (result);
	}

	pthread_create (&dispatch_thread, NULL, &tmr_dispatch, &handle);

	saTmrSelectionObjectGet (handle, &select_obj);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrSelectionObjectGet\n", result);
		exit (result);
	}

	result = saTmrTimeGet (handle, &current_time);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimeGet\n", result);
	}
	else {
		printf ("[DEBUG]: current_time = %"PRIu64"\n", current_time);
	}

	result = saTmrClockTickGet (handle, &clock_tick);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrClockTickGet\n", result);
	}
	else {
		printf ("[DEBUG]: clock_tick = %"PRIu64"\n", clock_tick);
	}

	result = saTmrTimerStart (handle, &attrs_a, data_a, &id_a, &call_time);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerStart\n", result);
	}
	else {
		printf ("[DEBUG]: saTmrTimerStart { id=%u }\n", (unsigned int)id_a);
		printf ("[DEBUG]:\t callTime = %"PRIu64"\n", call_time);
	}

	result = saTmrTimerStart (handle, &attrs_b, data_b, &id_b, &call_time);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerStart\n", result);
	}
	else {
		printf ("[DEBUG]: saTmrTimerStart { id=%u }\n", (unsigned int)id_b);
		printf ("[DEBUG]:\t callTime = %"PRIu64"\n", call_time);
	}

	/* SLEEP */
	sleep (10);

	result = saTmrTimerAttributesGet (handle, id_a, &attrs);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerAttributesGet\n", result);
	}
	else {
		printf ("[DEBUG]: id=%u attributes:\n", (unsigned int)id_a);
		printf ("[DEBUG]:\t type=%"PRIu32"\n", attrs.type);
		printf ("[DEBUG]:\t expire=%"PRIu64"\n", attrs.initialExpirationTime);
		printf ("[DEBUG]:\t duration=%"PRIu64"\n", attrs.timerPeriodDuration);
	}

	result = saTmrTimerAttributesGet (handle, id_b, &attrs);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerAttributesGet\n", result);
	}
	else {
		printf ("[DEBUG]: id=%u attributes:\n", (unsigned int)id_b);
		printf ("[DEBUG]:\t type=%"PRIu32"\n", attrs.type);
		printf ("[DEBUG]:\t expire=%"PRIu64"\n", attrs.initialExpirationTime);
		printf ("[DEBUG]:\t duration=%"PRIu64"\n", attrs.timerPeriodDuration);
	}

	/* SLEEP */
	sleep (10);

	result = saTmrTimerRemainingTimeGet (handle, id_a, &expire_time);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerRemainingTimeGet\n", result);
	}
	else {
		printf ("[DEBUG]:\t id=%u expire=%"PRIu64"\n",
			(unsigned int)id_a, expire_time);
	}

	result = saTmrTimerRemainingTimeGet (handle, id_b, &expire_time);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerRemainingTimeGet\n", result);
	}
	else {
		printf ("[DEBUG]:\t id=%u expire=%"PRIu64"\n",
			(unsigned int)id_b, expire_time);
	}

	result = saTmrTimerReschedule (handle, id_a, &new_attrs_a, &call_time);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerReschedule\n", result);
	}

	result = saTmrTimerReschedule (handle, id_b, &new_attrs_b, &call_time);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerReschedule\n", result);
	}

	result = saTmrTimerAttributesGet (handle, id_a, &attrs);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerAttributesGet\n", result);
	}
	else {
		printf ("[DEBUG]: id=%u attributes:\n", (unsigned int)id_a);
		printf ("[DEBUG]:\t type=%"PRIu32"\n", attrs.type);
		printf ("[DEBUG]:\t expire=%"PRIi64"\n", attrs.initialExpirationTime);
		printf ("[DEBUG]:\t duration=%"PRIi64"\n", attrs.timerPeriodDuration);
	}

	result = saTmrTimerAttributesGet (handle, id_b, &attrs);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerAttributesGet\n", result);
	}
	else {
		printf ("[DEBUG]: id=%u attributes:\n", (unsigned int)id_b);
		printf ("[DEBUG]:\t type=%"PRIu32"\n", attrs.type);
		printf ("[DEBUG]:\t expire=%"PRIi64"\n", attrs.initialExpirationTime);
		printf ("[DEBUG]:\t duration=%"PRIi64"\n", attrs.timerPeriodDuration);
	}

	/* SLEEP */
	sleep (30);

	result = saTmrTimerCancel (handle, id_b, &cancel_data);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerCancel\n", result);
		exit (1);
	}
	else {
		printf ("[DEBUG]:\t id=%u data=%p\n",
			(unsigned int)id_b, cancel_data);
	}

	/* SLEEP */
	sleep (30);

	result = saTmrFinalize (handle);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrFinalize\n", result);
		exit (1);
	}

	return (0);
}
