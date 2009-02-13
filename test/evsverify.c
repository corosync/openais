/*
 * Copyright (c) 2009 Red Hat, Inc.
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
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

#include "saAis.h"
#include "evs.h"
#include "../exec/crypto.h"

char *delivery_string;
struct msg {
	unsigned int msg_size;
	unsigned char sha1[20];
	unsigned char buffer[0];
};

int deliveries = 0;
void evs_deliver_fn (
	unsigned int nodeid,
	void *m,
	int msg_len)
{
	struct msg *msg2 = (struct msg *)m;
	unsigned char sha1_compare[20];
	hash_state sha1_hash;
	unsigned int i;

	sha1_init (&sha1_hash);
	sha1_process (&sha1_hash, msg2->buffer, msg2->msg_size);
	sha1_done (&sha1_hash, sha1_compare);
	if (memcmp (sha1_compare, msg2->sha1, 20) != 0) {
		printf ("Received incorrectly signed message of size %d - fatal error in system\n", msg2->msg_size);
		exit (1);
	}
	printf ("Received correctly signed message of size %d\n", msg2->msg_size);
	deliveries++;
}

void evs_confchg_fn (
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries)
{
	int i;

	printf ("CONFIGURATION CHANGE\n");
	printf ("--------------------\n");
	printf ("New configuration\n");
	for (i = 0; i < member_list_entries; i++) {
                printf ("%x\n", member_list[i]);
	}
	printf ("Members Left:\n");
	for (i = 0; i < left_list_entries; i++) {
                printf ("%x\n", left_list[i]);
	}
	printf ("Members Joined:\n");
	for (i = 0; i < joined_list_entries; i++) {
                printf ("%x\n", joined_list[i]);
	}
}

evs_callbacks_t callbacks = {
	evs_deliver_fn,
	evs_confchg_fn
};

struct evs_group groups[3] = {
	{ "key1" },
	{ "key2" },
	{ "key3" }
};

struct msg msg;

unsigned char buffer[200000];
int main (void)
{
	evs_handle_t handle;
	SaAisErrorT result;
	unsigned int i = 0, j;
	int fd;
	unsigned int member_list[32];
	unsigned int local_nodeid;
	unsigned int member_list_entries = 32;
	struct msg msg;
	hash_state sha1_hash;
	struct iovec iov[2];

	result = evs_initialize (&handle, &callbacks);
	if (result != EVS_OK) {
		printf ("Couldn't initialize EVS service %d\n", result);
		exit (0);
	}
	
	result = evs_membership_get (handle, &local_nodeid,
		member_list, &member_list_entries);
	printf ("Current membership from evs_membership_get entries %d\n",
		member_list_entries);
	for (i = 0; i < member_list_entries; i++) {
		printf ("member [%d] is %x\n", i, member_list[i]);
	}
	printf ("local processor from evs_membership_get %x\n", local_nodeid);

	printf ("Init result %d\n", result);
	result = evs_join (handle, groups, 3);
	printf ("Join result %d\n", result);
	result = evs_leave (handle, &groups[0], 1);
	printf ("Leave result %d\n", result);
	delivery_string = "evs_mcast_joined";

	iov[0].iov_base = &msg;
	iov[0].iov_len = sizeof (struct msg);
	iov[1].iov_base = buffer;

	/*
	 * Demonstrate evs_mcast_joined
	 */
	for (i = 0; i < 1000000000; i++) {
		msg.msg_size = 99 + rand() % 100000;
		iov[1].iov_len = msg.msg_size;
		for (j = 0; j < msg.msg_size; j++) {
			buffer[j] = j + msg.msg_size;
		}

		sprintf ((char *)buffer,
			"evs_mcast_joined: This is message %12d", i);
		sha1_init (&sha1_hash);
		sha1_process (&sha1_hash, buffer,
			msg.msg_size);
		sha1_done (&sha1_hash, msg.sha1);
try_again_one:
		result = evs_mcast_joined (handle, EVS_TYPE_AGREED,
			iov, 2);
		if (result == EVS_ERR_TRY_AGAIN) {
			goto try_again_one;
		}
		result = evs_dispatch (handle, EVS_DISPATCH_ALL);
	}

	evs_fd_get (handle, &fd);
	
	evs_finalize (handle);

	return (0);
}
