#define _BSD_SOURCE
/*
 * Copyright (c) 2006 Red Hat, Inc.
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
#include <string.h>
#include <signal.h>
#include <unistd.h>
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

#include "saAis.h"
#include "cpg.h"

#ifdef OPENAIS_SOLARIS
#define timersub(a, b, result)						\
    do {								\
	(result)->tv_sec = (a)->tv_sec - (b)->tv_sec;			\
	(result)->tv_usec = (a)->tv_usec - (b)->tv_usec;		\
	if ((result)->tv_usec < 0) {					\
	    --(result)->tv_sec;						\
	    (result)->tv_usec += 1000000;				\
	}								\
    } while (0)
#endif

int alarm_notice;

void cpg_bm_confchg_fn (
	cpg_handle_t handle,
	struct cpg_name *group_name,
	struct cpg_address *member_list, int member_list_entries,
	struct cpg_address *left_list, int left_list_entries,
	struct cpg_address *joined_list, int joined_list_entries)
{
}

unsigned int write_count;

void cpg_bm_deliver_fn (
        cpg_handle_t handle,
        struct cpg_name *group_name,
        uint32_t nodeid,
        uint32_t pid,
        void *msg,
        int msg_len)
{
	write_count++;
}

cpg_callbacks_t callbacks = {
	.cpg_deliver_fn 	= cpg_bm_deliver_fn,
	.cpg_confchg_fn		= cpg_bm_confchg_fn
};

char data[500000];

void cpg_benchmark (
	cpg_handle_t handle,
	int write_size)
{
	struct timeval tv1, tv2, tv_elapsed;
	struct iovec iov;
	unsigned int res;
	cpg_flow_control_state_t flow_control_state;

	alarm_notice = 0;
	iov.iov_base = data;
	iov.iov_len = write_size;

	write_count = 0;
	alarm (10);

	gettimeofday (&tv1, NULL);
	do {
		/*
		 * Test checkpoint write
		 */
		cpg_flow_control_state_get (handle, &flow_control_state);
		if (flow_control_state == CPG_FLOW_CONTROL_DISABLED) {
retry:
			res = cpg_mcast_joined (handle, CPG_TYPE_AGREED, &iov, 1);
			if (res == CPG_ERR_TRY_AGAIN) {
				goto retry;
			}
		}
		res = cpg_dispatch (handle, CPG_DISPATCH_ALL);
		if (res != CPG_OK) {
			printf ("cpg dispatch returned error %d\n", res);
			exit (1);
		}
	} while (alarm_notice == 0);
	gettimeofday (&tv2, NULL);
	timersub (&tv2, &tv1, &tv_elapsed);

	printf ("%5d messages received ", write_count);
	printf ("%5d bytes per write ", write_size);
	printf ("%7.3f Seconds runtime ", 
		(tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
	printf ("%9.3f TP/s ",
		((float)write_count) /  (tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
	printf ("%7.3f MB/s.\n", 
		((float)write_count) * ((float)write_size) /  ((tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)) * 1000000.0));
}

void sigalrm_handler (int num)
{
	alarm_notice = 1;
}

static struct cpg_name group_name = {
	.value = "cpg_bm",
	.length = 6
};

int main (void) {
	cpg_handle_t handle;
	unsigned int size = 1;
	int i;
	unsigned int res;
	
	signal (SIGALRM, sigalrm_handler);
	res = cpg_initialize (&handle, &callbacks);
	if (res != CPG_OK) {
		printf ("cpg_initialize failed with result %d\n", res);
		exit (1);
	}
	
	res = cpg_join (handle, &group_name);
	if (res != CPG_OK) {
		printf ("cpg_join failed with result %d\n", res);
		exit (1);
	}

	for (i = 0; i < 50; i++) { /* number of repetitions - up to 50k */
		cpg_benchmark (handle, size);
		size += 1000;
	}

	res = cpg_finalize (handle);
	if (res != CPG_OK) {
		printf ("cpg_join failed with result %d\n", res);
		exit (1);
	}
	return (0);
}
