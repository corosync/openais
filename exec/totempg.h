/*
 * Copyright (c) 2003-2005 MontaVista Software, Inc.
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

#ifndef TOTEMPG_H_DEFINED
#define TOTEMPG_H_DEFINED

#include <netinet/in.h>
#include "aispoll.h"
#include "totemsrp.h"

#define MESSAGE_SIZE_MAX		256000

#define TOTEMPG_PRIO_RECOVERY	0
#define TOTEMPG_PRIO_HIGH		1
#define TOTEMPG_PRIO_MED		2
#define TOTEMPG_PRIO_LOW		3

#define TOTEMPG_AGREED			0
#define TOTEMPG_SAFE			1

enum totempg_configuration_type {
	TOTEMPG_CONFIGURATION_REGULAR,
	TOTEMPG_CONFIGURATION_TRANSITIONAL	
};

/*
 * This represents an interface that TOTEMPG binds to
 */
struct totempg_interface {
	struct sockaddr_in bindnet;
	struct sockaddr_in boundto;
};

extern poll_handle *totempg_poll_handle;

/*
 * Totem Single Ring Protocol
 * depends on poll abstraction, POSIX, IPV4
 */
/*
 * Initialize the logger
 */
void totempg_log_printf_init (
	void (*log_printf) (int , char *, ...),
	int log_level_security,
	int log_level_error,
	int log_level_warning,
	int log_level_notice,
	int log_level_debug);

/*
 * Initialize the group messaging interface
 */
int totempg_initialize (
	struct sockaddr_in *sockaddr_mcast,
	struct totempg_interface *interfaces,
	int interface_count,
	poll_handle *poll_handle,
	unsigned char *private_key,
	int private_key_len,
	void *member_private,
	int member_private_len,
	void (*deliver_fn) (
		struct in_addr source_addr,
		struct iovec *iovec,
		int iov_len,
		int endian_conversion_required),
	void (*confchg_fn) (
		enum totempg_configuration_type configuration_type,
		struct in_addr *member_list, void *member_list_private, 
			int member_list_entries,
		struct in_addr *left_list, void *left_list_private,
			int left_list_entries,
		struct in_addr *joined_list, void *joined_list_private,
			int joined_list_entries));

/*
 * Multicast a message
 */
int totempg_mcast (
	struct iovec *iovec,
	int iov_len,
	int guarantee,
	int priority);

/*
 * Determine if a message of msg_size could be queued
 */
int totempg_send_ok (
	int priority,
	int msg_size);

enum totempg_callback_token_type {
        TOTEMPG_CALLBACK_TOKEN_RECEIVED = 1,
        TOTEMPG_CALLBACK_TOKEN_SENT = 2
};
	
void totempg_token_callback_destroy (void *handle);

int totempg_token_callback_create (void **handle_out,
	enum totempg_callback_token_type type,
	int delete,
	int (*callback_fn) (enum totempg_callback_token_type type, void *),
	void *data);

#endif /* TOTEMPG_H_DEFINED */
