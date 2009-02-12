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
	printf ("[DEBUG]:\t timerId = %u\n", timerId);
	printf ("[DEBUG]:\t timerData = %p\n", timerData);
	printf ("[DEBUG]:\t expirationCount = %u\n", expirationCount);

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
	SaTmrTimerAttributesT my_attrs;
	SaTmrTimerAttributesT attrs_a = { SA_TIME_DURATION, TMR_10_SECONDS, TMR_10_SECONDS };
	SaTmrTimerAttributesT attrs_b = { SA_TIME_DURATION, TMR_20_SECONDS, TMR_20_SECONDS };
	SaTmrTimerIdT id_a;
	SaTmrTimerIdT id_b;
	SaTimeT time;
	SaTimeT current_time_a;
	SaTimeT current_time_b;
	SaTimeT delta_time;
	SaTimeT clock_tick;

	pthread_t dispatch_thread;
	int result;
	int i;

	printf ("[DEBUG]: TMR_10_SECONDS = %"PRId64"\n", TMR_10_SECONDS);

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

	result = saTmrTimeGet (handle, &current_time_a);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimeGet\n", result);
	}
	else {
		printf ("[DEBUG]: current_time = %"PRIu64"\n", current_time_a);
	}

	result = saTmrClockTickGet (handle, &clock_tick);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrClockTickGet\n", result);
	}
	else {
		printf ("[DEBUG]: clock_tick = %"PRIu64"\n", clock_tick);
	}

	sleep (10);

	result = saTmrTimeGet (handle, &current_time_b);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimeGet\n", result);
	}
	else {
		printf ("[DEBUG]: current_time = %"PRIu64"\n", current_time_b);
	}

	delta_time = current_time_b - current_time_a;

	printf ("[DEBUG]: delta_time = %"PRIu64"\n", delta_time);

	result = saTmrTimerStart (handle, &attrs_a, NULL, &id_a, &time);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerStart\n", result);
	}
	else {
		printf ("[DEBUG]: saTmrTimerStart { id=%u }\n", id_a);
		printf ("[DEBUG]:\t callTime = %"PRIu64"\n", time);
	}

	result = saTmrTimerAttributesGet (handle, id_a, &my_attrs);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerAttributesGet\n", result);
	}
	else {
		printf ("[DEBUG]: id=%u attributes:\n", id_a);
		printf ("[DEBUG]:\t type=%"PRId64"\n", my_attrs.type);
		printf ("[DEBUG]:\t expire=%"PRIi64"\n", my_attrs.initialExpirationTime);
		printf ("[DEBUG]:\t duration=%"PRIi64"\n", my_attrs.timerPeriodDuration);
	}

	result = saTmrTimerStart (handle, &attrs_b, NULL, &id_b, &time);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerStart\n", result);
	}
	else {
		printf ("[DEBUG]: saTmrTimerStart { id=%u }\n", id_b);
		printf ("[DEBUG]:\t callTime = %"PRIu64"\n", time);
	}

	result = saTmrTimerAttributesGet (handle, id_a, &my_attrs);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerAttributesGet\n", result);
	}
	else {
		printf ("[DEBUG]: id=%u attributes:\n", id_a);
		printf ("[DEBUG]:\t type=%"PRId64"\n", my_attrs.type);
		printf ("[DEBUG]:\t expire=%"PRId64"\n", my_attrs.initialExpirationTime);
		printf ("[DEBUG]:\t duration=%"PRId64"\n", my_attrs.timerPeriodDuration);
	}

	result = saTmrTimerReschedule (handle, id_a, &attrs_a, &time);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerReschedule\n", result);
	}

	result = saTmrTimerAttributesGet (handle, id_a, &my_attrs);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerAttributesGet\n", result);
	}
	else {
		printf ("[DEBUG]: id=%u attributes:\n", id_a);
		printf ("[DEBUG]:\t type=%"PRId64"\n", my_attrs.type);
		printf ("[DEBUG]:\t expire=%"PRId64"\n", my_attrs.initialExpirationTime);
		printf ("[DEBUG]:\t duration=%"PRId64"\n", my_attrs.timerPeriodDuration);
	}

	result = saTmrTimerReschedule (handle, id_b, &attrs_b, &time);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerReschedule\n", result);
	}

	result = saTmrTimerAttributesGet (handle, id_a, &my_attrs);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerAttributesGet\n", result);
	}
	else {
		printf ("[DEBUG]: id=%u attributes:\n", id_a);
		printf ("[DEBUG]:\t type=%"PRId64"\n", my_attrs.type);
		printf ("[DEBUG]:\t expire=%"PRId64"\n", my_attrs.initialExpirationTime);
		printf ("[DEBUG]:\t duration=%"PRId64"\n", my_attrs.timerPeriodDuration);
	}

	sleep (30);

	result = saTmrPeriodicTimerSkip (handle, id_a);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrPeriodicTimerSkip\n", result);
	}

	sleep (60);

	for (i = 0; i < 4; i++) {
		result = saTmrPeriodicTimerSkip (handle, id_a);
		if (result != SA_AIS_OK) {
			printf ("[ERROR]: (%d) saTmrPeriodicTimerSkip\n", result);
		}
	}

	sleep (120);

	result = saTmrTimerCancel (handle, id_a, NULL);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerCancel\n", result);
	}

	result = saTmrTimerReschedule (handle, id_a, &attrs_a, &time);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerReschedule\n", result);
	}

	result = saTmrTimerCancel (handle, id_b, NULL);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerCancel\n", result);
	}

	result = saTmrTimerReschedule (handle, id_b, &attrs_b, &time);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrTimerReschedule\n", result);
	}

	result = saTmrFinalize (handle);
	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saTmrFinalize\n", result);
		exit (1);
	}

	return (0);
}
