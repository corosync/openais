#define _BSD_SOURCE
/*
 * Copyright (c) 2004 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@mvista.com)
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
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ais_types.h"
#include "evs.h"

void evs_deliver_fn (struct in_addr source_addr, void *msg, int msg_len)
{
//	printf ("Delivering message %s\n", buf);
}

void evs_confchg_fn (
	struct in_addr *member_list, int member_list_entries,
	struct in_addr *left_list, int left_list_entries,
	struct in_addr *joined_list, int joined_list_entries)
{
	int i;

	printf ("CONFIGURATION CHANGE\n");
	printf ("--------------------\n");
	printf ("New configuration\n");
	for (i = 0; i < member_list_entries; i++) {
		printf ("%s\n", inet_ntoa (member_list[i]));
	}
	printf ("Members Left:\n");
	for (i = 0; i < left_list_entries; i++) {
		printf ("%s\n", inet_ntoa (left_list[i]));
	}
	printf ("Members Joined:\n");
	for (i = 0; i < joined_list_entries; i++) {
		printf ("%s\n", inet_ntoa (joined_list[i]));
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

char buffer[200000];

struct iovec iov = {
	.iov_base = buffer,
	.iov_len = sizeof (buffer)
};

void ckpt_benchmark (evs_handle_t handle,
	int write_count, int write_size)
{
	struct timeval tv1, tv2, tv_elapsed;
	evs_error_t result;
	int i = 0;

	gettimeofday (&tv1, NULL);

	iov.iov_len = write_size;
	for (i = 0; i < write_count; i++) {
		sprintf (buffer, "This is message %d\n", i);
try_again:
		result = evs_mcast_joined (&handle, EVS_TYPE_AGREED, EVS_PRIO_LOW, &iov, 1);
		if (result == EVS_ERR_TRY_AGAIN) {
			goto try_again;
		}
	
		result = evs_dispatch (&handle, EVS_DISPATCH_ALL);
	}
	gettimeofday (&tv2, NULL);
	timersub (&tv2, &tv1, &tv_elapsed);

	printf ("%5d Writes ", write_count);
	printf ("%5d bytes per write ", write_size);
	printf ("%7.3f Seconds runtime ", 
		(tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
	printf ("%9.3f TP/s ",
		((float)write_count) /  (tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
	printf ("%7.3f MB/s.\n", 
		((float)write_count) * ((float)write_size) /  ((tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)) * 1000000.0));
}

int main (void) {
	int size;
	int count;
	int i;
	evs_error_t result;
	evs_handle_t handle;
	
	result = evs_initialize (&handle, &callbacks);
	printf ("Init result %d\n", result);
	result = evs_join (&handle, groups, 3);
	printf ("Join result %d\n", result);
	result = evs_leave (&handle, &groups[0], 1);
	printf ("Leave result %d\n", result);

	count = 100000;
	size = 1300;

	for (i = 0; i < 35; i++) { /* number of repetitions */
		ckpt_benchmark (handle, count, size);
		/*
		 * Adjust count to 95% of previous count
		 * Adjust bytes to write per checkpoint up by 1500
		 */
		count = (((float)count) * 0.80);
		size += 100;
	}
	return (0);
}