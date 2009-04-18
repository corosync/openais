/*
 * Copyright (c) 2008 Red Hat Software, Inc.
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
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>

#include "saAis.h"
#include "saLck.h"

static void testLckResourceOpenCallback (
	SaInvocationT invocation,
	SaLckResourceHandleT lockResourceHandle,
	SaAisErrorT error)
{
	printf ("testLckResourceOpenCallback\n");
}

static void testLckLockGrantCallback (
	SaInvocationT invocation,
	SaLckLockStatusT lockStatus,
	SaAisErrorT error)
{
	printf ("testLckLockGrantCallback\n");
}

static void testLckLockWaiterCallback (
	SaLckWaiterSignalT waiterSignal,
        SaLckLockIdT lockId,
        SaLckLockModeT modeHeld,
        SaLckLockModeT modeRequested)
{
	printf ("testLckLockWaiterCallback\n");
}

static void testLckResourceUnlockCallback (
	SaInvocationT invocation,
        SaAisErrorT error)
{
	printf ("testLckResourceUnlockCallback\n");
}

static SaLckCallbacksT callbacks = {
	.saLckResourceOpenCallback	= testLckResourceOpenCallback,
	.saLckLockGrantCallback		= testLckLockGrantCallback,
	.saLckLockWaiterCallback	= testLckLockWaiterCallback,
	.saLckResourceUnlockCallback	= testLckResourceUnlockCallback
};

static SaVersionT version = { 'B', 1, 1 };

static void setSaNameT (SaNameT *name, const char *str) {
	strncpy ((char *)name->value, str, SA_MAX_NAME_LENGTH);
	if (strlen ((char *)name->value) > SA_MAX_NAME_LENGTH) {
		name->length = SA_MAX_NAME_LENGTH;
	} else {
		name->length = strlen (str);
	}
}

int main (void)
{
	int result;

	SaLckHandleT handle;
	SaLckLockIdT lock_id;
	SaLckLockStatusT status;

	SaLckResourceHandleT resource_handle_a;
	SaLckResourceHandleT resource_handle_b;
	SaLckResourceHandleT resource_handle_c;

	SaNameT resource_name_a;
	SaNameT resource_name_b;
	SaNameT resource_name_c;

	result = saLckInitialize (&handle, &callbacks, &version);

	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saLckInitialize\n", result);
		exit (1);
	}

	setSaNameT (&resource_name_a, "test_resource_a");
	setSaNameT (&resource_name_b, "test_resource_b");
	setSaNameT (&resource_name_c, "test_resource_c");

	/*
	 * Open resources
	 */
	result = saLckResourceOpen (handle, &resource_name_a,
				    SA_LCK_RESOURCE_CREATE, SA_TIME_ONE_SECOND,
				    &resource_handle_a);
	printf ("[DEBUG]: (%d) saLckResourceOpen { %s }\n",
		result, (char *)(resource_name_a.value));

	result = saLckResourceOpen (handle, &resource_name_b,
				    SA_LCK_RESOURCE_CREATE, SA_TIME_ONE_SECOND,
				    &resource_handle_b);
	printf ("[DEBUG]: (%d) saLckResourceOpen { %s }\n",
		result, (char *)(resource_name_b.value));

	result = saLckResourceOpen (handle, &resource_name_c,
				    SA_LCK_RESOURCE_CREATE, SA_TIME_ONE_SECOND,
				    &resource_handle_c);
	printf ("[DEBUG]: (%d) saLckResourceOpen { %s }\n",
		result, (char *)(resource_name_c.value));

	/*
	 * Add resource locks to resource "A"
	 */
	result = saLckResourceLock (resource_handle_a, &lock_id,
				   SA_LCK_PR_LOCK_MODE, SA_LCK_LOCK_ORPHAN,
				   55, SA_TIME_END, &status);
	printf ("[DEBUG]: (%d) saLckResourceLock { %s } [ id=%x status=%d ]\n",
		result, (char *)(resource_name_a.value), (unsigned int)(lock_id), status);

	result = saLckResourceLock (resource_handle_a, &lock_id,
				   SA_LCK_PR_LOCK_MODE, SA_LCK_LOCK_ORPHAN,
				   55, SA_TIME_END, &status);
	printf ("[DEBUG]: (%d) saLckResourceLock { %s } [ id=%x status=%d ]\n",
		result, (char *)(resource_name_a.value), (unsigned int)(lock_id), status);

	result = saLckResourceLock (resource_handle_a, &lock_id,
				   SA_LCK_PR_LOCK_MODE, SA_LCK_LOCK_ORPHAN,
				   55, SA_TIME_END, &status);
	printf ("[DEBUG]: (%d) saLckResourceLock { %s } [ id=%x status=%d ]\n",
		result, (char *)(resource_name_a.value), (unsigned int)(lock_id), status);

	/*
	 * Add resource locks to resource "B"
	 */
	result = saLckResourceLock (resource_handle_b, &lock_id,
				   SA_LCK_PR_LOCK_MODE, SA_LCK_LOCK_ORPHAN,
				   55, SA_TIME_END, &status);
	printf ("[DEBUG]: (%d) saLckResourceLock { %s } [ id=%x status=%d ]\n",
		result, (char *)(resource_name_b.value), (unsigned int)(lock_id), status);

	result = saLckResourceLock (resource_handle_b, &lock_id,
				   SA_LCK_PR_LOCK_MODE, SA_LCK_LOCK_ORPHAN,
				   55, SA_TIME_END, &status);
	printf ("[DEBUG]: (%d) saLckResourceLock { %s } [ id=%x status=%d ]\n",
		result, (char *)(resource_name_b.value), (unsigned int)(lock_id), status);

	result = saLckResourceLock (resource_handle_b, &lock_id,
				   SA_LCK_PR_LOCK_MODE, SA_LCK_LOCK_ORPHAN,
				   55, SA_TIME_END, &status);
	printf ("[DEBUG]: (%d) saLckResourceLock { %s } [ id=%x status=%d ]\n",
		result, (char *)(resource_name_b.value), (unsigned int)(lock_id), status);

	/*
	 * Add resource locks to resource "C"
	 */
	result = saLckResourceLock (resource_handle_c, &lock_id,
				   SA_LCK_PR_LOCK_MODE, SA_LCK_LOCK_ORPHAN,
				   55, SA_TIME_END, &status);
	printf ("[DEBUG]: (%d) saLckResourceLock { %s } [ id=%x status=%d ]\n",
		result, (char *)(resource_name_c.value), (unsigned int)(lock_id), status);

	result = saLckResourceLock (resource_handle_c, &lock_id,
				   SA_LCK_PR_LOCK_MODE, SA_LCK_LOCK_ORPHAN,
				   55, SA_TIME_END, &status);
	printf ("[DEBUG]: (%d) saLckResourceLock { %s } [ id=%x status=%d ]\n",
		result, (char *)(resource_name_c.value), (unsigned int)(lock_id), status);

	result = saLckResourceLock (resource_handle_c, &lock_id,
				   SA_LCK_PR_LOCK_MODE, SA_LCK_LOCK_ORPHAN,
				   55, SA_TIME_END, &status);
	printf ("[DEBUG]: (%d) saLckResourceLock { %s } [ id=%x status=%d ]\n",
		result, (char *)(resource_name_c.value), (unsigned int)(lock_id), status);

	sleep (30);

	return (0);
}
