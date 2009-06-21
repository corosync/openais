/*
 * Copyright (c) 2008, 2009 Red Hat, Inc.
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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>

#include "saAis.h"
#include "saMsg.h"

static void QueueOpenCallback (
	SaInvocationT invocation,
	SaMsgQueueHandleT queueHandle,
	SaAisErrorT error)
{
	/* DEBUG */
	printf ("[TEST]: testmsg2 (QueueOpenCallback)\n");
	printf ("[TEST]: \t { invocation = %llx }\n",
		(unsigned long long)(invocation));
	printf ("[TEST]: \t { queueHandle = %llx }\n",
		(unsigned long long)(queueHandle));
	printf ("[TEST]: \t { error = %u }\n",
		(unsigned int)(error));
}

static void QueueGroupTrackCallback (
	const SaNameT *queueGroupName,
	const SaMsgQueueGroupNotificationBufferT *notificationBuffer,
	SaUint32T numberOfMembers,
	SaAisErrorT error)
{
	int i = 0;

	/* DEBUG */
	printf ("[TEST]: testmsg2 (QueueGroupTrackCallback)\n");
	printf ("[TEST]: \t { queueGroupName = %s }\n",
		(char *)(queueGroupName->value));
	printf ("[TEST]: \t { numberOfMembers = %u }\n",
		(unsigned int)(numberOfMembers));
	printf ("[TEST]: \t { error = %u }\n",
		(unsigned int)(error));
	printf ("[TEST]: \t { notificationBuffer->numberOfItems = %u }\n",
		(unsigned int)(notificationBuffer->numberOfItems));

	for (i = 0; i < notificationBuffer->numberOfItems; i++) {
		printf ("[TEST]: \t { item #%d => %s (%u) }\n", i,
			(char *)(notificationBuffer->notification[i].member.queueName.value),
			(unsigned int)(notificationBuffer->notification[i].change));
	}
}

static void MessageDeliveredCallback (
	SaInvocationT invocation,
	SaAisErrorT error)
{
	/* DEBUG */
	printf ("[TEST]: testmsg2 (MessageDeliveredCallback)\n");
	printf ("[TEST]: \t { invocation = %llx }\n",
		(unsigned long long)(invocation));
	printf ("[TEST]: \t { error = %u }\n",
		(unsigned int)(error));
}

static void MessageReceivedCallback (
	SaMsgQueueHandleT queueHandle)
{
	/* DEBUG */
	printf ("[TEST]: testmsg2 (MessageReceivedCallback)\n");
	printf ("[TEST]: \t { queueHandle = %llx }\n",
		(unsigned long long)(queueHandle));
}

static SaMsgCallbacksT callbacks = {
	.saMsgQueueOpenCallback		= QueueOpenCallback,
	.saMsgQueueGroupTrackCallback	= QueueGroupTrackCallback,
	.saMsgMessageDeliveredCallback	= MessageDeliveredCallback,
	.saMsgMessageReceivedCallback	= MessageReceivedCallback
};

static SaVersionT version = { 'B', 1, 1 };

static SaMsgQueueCreationAttributesT creation_attributes = {
	SA_MSG_QUEUE_PERSISTENT,
	{ 128000, 128000, 128000 },
	SA_TIME_END
};

static void setSaNameT (SaNameT *name, const char *str) {
	name->length = strlen (str);
	strcpy ((char *)name->value, str);
}

static void setSaMsgMessageT (SaMsgMessageT *message, const char *data) {
	message->type = 1;
	message->version = 2;
	message->size = strlen (data) + 1;
	message->senderName = NULL;
	message->data = strdup (data);
	message->priority = 0;
}

int main (void)
{
	int result;

	SaSelectionObjectT select_obj;

	SaMsgHandleT handle;
	SaMsgMessageT message;

	SaMsgQueueHandleT queue_handle_a;
	SaMsgQueueHandleT queue_handle_b;
	SaMsgQueueHandleT queue_handle_c;

	SaMsgQueueHandleT queue_handle_x;
	SaMsgQueueHandleT queue_handle_y;
	SaMsgQueueHandleT queue_handle_z;

	SaNameT queue_name_a;
	SaNameT queue_name_b;
	SaNameT queue_name_c;

	SaNameT queue_name_x;
	SaNameT queue_name_y;
	SaNameT queue_name_z;

	SaNameT group_name_one;
	SaNameT group_name_two;

	setSaNameT(&queue_name_a, "QUEUE_A");
	setSaNameT(&queue_name_b, "QUEUE_B");
	setSaNameT(&queue_name_c, "QUEUE_C");

	setSaNameT(&queue_name_x, "QUEUE_X");
	setSaNameT(&queue_name_y, "QUEUE_Y");
	setSaNameT(&queue_name_z, "QUEUE_Z");

	setSaNameT(&group_name_one, "GROUP_ONE");
	setSaNameT(&group_name_two, "GROUP_TWO");

	result = saMsgInitialize (&handle, &callbacks, &version);

	if (result != SA_AIS_OK) {
		printf ("[ERROR]: (%d) saMsgInitialize\n", result);
		exit (1);
	}

	saMsgSelectionObjectGet (handle, &select_obj);

	/*
	* Create message queues
	*/

	result = saMsgQueueOpen (handle, &queue_name_a, &creation_attributes,
				 SA_MSG_QUEUE_CREATE, SA_TIME_END, &queue_handle_a);
	printf ("[DEBUG]: (%d) saMsgQueueOpen { %s }\n",
		result, (char *)(queue_name_a.value));

	result = saMsgQueueOpen (handle, &queue_name_b, &creation_attributes,
				 SA_MSG_QUEUE_CREATE, SA_TIME_END, &queue_handle_b);
	printf ("[DEBUG]: (%d) saMsgQueueOpen { %s }\n",
		result, (char *)(queue_name_b.value));

	result = saMsgQueueOpen (handle, &queue_name_c, &creation_attributes,
				 SA_MSG_QUEUE_CREATE, SA_TIME_END, &queue_handle_c);
	printf ("[DEBUG]: (%d) saMsgQueueOpen { %s }\n",
		result, (char *)(queue_name_c.value));

	result = saMsgQueueOpen (handle, &queue_name_x, &creation_attributes,
				 SA_MSG_QUEUE_CREATE, SA_TIME_END, &queue_handle_x);
	printf ("[DEBUG]: (%d) saMsgQueueOpen { %s }\n",
		result, (char *)(queue_name_x.value));

	result = saMsgQueueOpen (handle, &queue_name_y, &creation_attributes,
				 SA_MSG_QUEUE_CREATE, SA_TIME_END, &queue_handle_y);
	printf ("[DEBUG]: (%d) saMsgQueueOpen { %s }\n",
		result, (char *)(queue_name_y.value));

	result = saMsgQueueOpen (handle, &queue_name_z, &creation_attributes,
				 SA_MSG_QUEUE_CREATE, SA_TIME_END, &queue_handle_z);
	printf ("[DEBUG]: (%d) saMsgQueueOpen { %s }\n",
		result, (char *)(queue_name_z.value));

	/*
	* Create queue groups
	*/

	result = saMsgQueueGroupCreate (handle, &group_name_one,
					SA_MSG_QUEUE_GROUP_ROUND_ROBIN);
	printf ("[DEBUG]: (%d) saMsgQueueGroupCreate { %s }\n",
		result, (char *)(group_name_one.value));

	result = saMsgQueueGroupCreate (handle, &group_name_two,
					SA_MSG_QUEUE_GROUP_ROUND_ROBIN);
	printf ("[DEBUG]: (%d) saMsgQueueGroupCreate { %s }\n",
		result, (char *)(group_name_two.value));

	/*
	* Add queues to GROUP_ONE
	*/

	result = saMsgQueueGroupInsert (handle, &group_name_one, &queue_name_x);
	printf ("[DEBUG]: (%d) saMsgQueueGroupInsert { group: %s + queue: %s }\n",
		result, (char *)(group_name_one.value), (char *)(queue_name_x.value));

	result = saMsgQueueGroupInsert (handle, &group_name_one, &queue_name_y);
	printf ("[DEBUG]: (%d) saMsgQueueGroupInsert { group: %s + queue: %s }\n",
		result, (char *)(group_name_one.value), (char *)(queue_name_y.value));

	result = saMsgQueueGroupInsert (handle, &group_name_one, &queue_name_z);
	printf ("[DEBUG]: (%d) saMsgQueueGroupInsert { group: %s + queue: %s }\n",
		result, (char *)(group_name_one.value), (char *)(queue_name_z.value));

	/*
	* Send messages to QUEUE_A
	*/

	setSaMsgMessageT (&message, "test_msg_A.01");
	result = saMsgMessageSend (handle, &queue_name_a, &message,
				   SA_TIME_ONE_SECOND);
	printf ("[DEBUG]: (%d) saMsgMessageSend { queue: %s + message: %s }\n",
		result, (char *)(queue_name_a.value), (char *)(message.data));

	setSaMsgMessageT (&message, "test_msg_A.02");
	result = saMsgMessageSend (handle, &queue_name_a, &message,
				   SA_TIME_ONE_SECOND);
	printf ("[DEBUG]: (%d) saMsgMessageSend { queue: %s + message: %s }\n",
		result, (char *)(queue_name_a.value), (char *)(message.data));

	setSaMsgMessageT (&message, "test_msg_A.03");
	result = saMsgMessageSend (handle, &queue_name_a, &message,
				   SA_TIME_ONE_SECOND);
	printf ("[DEBUG]: (%d) saMsgMessageSend { queue: %s + message: %s }\n",
		result, (char *)(queue_name_a.value), (char *)(message.data));

	setSaMsgMessageT (&message, "test_msg_A.04");
	result = saMsgMessageSend (handle, &queue_name_a, &message,
				   SA_TIME_ONE_SECOND);
	printf ("[DEBUG]: (%d) saMsgMessageSend { queue: %s + message: %s }\n",
		result, (char *)(queue_name_a.value), (char *)(message.data));

	setSaMsgMessageT (&message, "test_msg_A.05");
	result = saMsgMessageSend (handle, &queue_name_a, &message,
				   SA_TIME_ONE_SECOND);
	printf ("[DEBUG]: (%d) saMsgMessageSend { queue: %s + message: %s }\n",
		result, (char *)(queue_name_a.value), (char *)(message.data));

	/*
	* Send messages to QUEUE_B
	*/

	setSaMsgMessageT (&message, "test_msg_B.01");
	result = saMsgMessageSend (handle, &queue_name_b, &message,
				   SA_TIME_ONE_SECOND);
	printf ("[DEBUG]: (%d) saMsgMessageSend { queue: %s + message: %s }\n",
		result, (char *)(queue_name_b.value), (char *)(message.data));

	setSaMsgMessageT (&message, "test_msg_B.02");
	result = saMsgMessageSend (handle, &queue_name_b, &message,
				   SA_TIME_ONE_SECOND);
	printf ("[DEBUG]: (%d) saMsgMessageSend { queue: %s + message: %s }\n",
		result, (char *)(queue_name_b.value), (char *)(message.data));

	setSaMsgMessageT (&message, "test_msg_B.03");
	result = saMsgMessageSend (handle, &queue_name_b, &message,
				   SA_TIME_ONE_SECOND);
	printf ("[DEBUG]: (%d) saMsgMessageSend { queue: %s + message: %s }\n",
		result, (char *)(queue_name_b.value), (char *)(message.data));

	setSaMsgMessageT (&message, "test_msg_B.04");
	result = saMsgMessageSend (handle, &queue_name_b, &message,
				   SA_TIME_ONE_SECOND);
	printf ("[DEBUG]: (%d) saMsgMessageSend { queue: %s + message: %s }\n",
		result, (char *)(queue_name_b.value), (char *)(message.data));

	setSaMsgMessageT (&message, "test_msg_B.05");
	result = saMsgMessageSend (handle, &queue_name_b, &message,
				   SA_TIME_ONE_SECOND);
	printf ("[DEBUG]: (%d) saMsgMessageSend { queue: %s + message: %s }\n",
		result, (char *)(queue_name_b.value), (char *)(message.data));

	/*
	* Send messages to QUEUE_C
	*/

	setSaMsgMessageT (&message, "test_msg_C.01");
	result = saMsgMessageSend (handle, &queue_name_c, &message,
				   SA_TIME_ONE_SECOND);
	printf ("[DEBUG]: (%d) saMsgMessageSend { queue: %s + message: %s }\n",
		result, (char *)(queue_name_c.value), (char *)(message.data));

	setSaMsgMessageT (&message, "test_msg_C.02");
	result = saMsgMessageSend (handle, &queue_name_c, &message,
				   SA_TIME_ONE_SECOND);
	printf ("[DEBUG]: (%d) saMsgMessageSend { queue: %s + message: %s }\n",
		result, (char *)(queue_name_c.value), (char *)(message.data));

	setSaMsgMessageT (&message, "test_msg_C.03");
	result = saMsgMessageSend (handle, &queue_name_c, &message,
				   SA_TIME_ONE_SECOND);
	printf ("[DEBUG]: (%d) saMsgMessageSend { queue: %s + message: %s }\n",
		result, (char *)(queue_name_c.value), (char *)(message.data));

	setSaMsgMessageT (&message, "test_msg_C.04");
	result = saMsgMessageSend (handle, &queue_name_c, &message,
				   SA_TIME_ONE_SECOND);
	printf ("[DEBUG]: (%d) saMsgMessageSend { queue: %s + message: %s }\n",
		result, (char *)(queue_name_c.value), (char *)(message.data));

	setSaMsgMessageT (&message, "test_msg_C.05");
	result = saMsgMessageSend (handle, &queue_name_c, &message,
				   SA_TIME_ONE_SECOND);
	printf ("[DEBUG]: (%d) saMsgMessageSend { queue: %s + message: %s }\n",
		result, (char *)(queue_name_c.value), (char *)(message.data));

	return (0);
}
