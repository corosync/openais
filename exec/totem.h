/*
 * Copyright (c) 2005 MontaVista Software, Inc.
 * Copyright (c) 2006-2007 Red Hat, Inc.
 * Copyright (c) 2006 Sun Microsystems, Inc.
 *
 * Author: Steven Dake (sdake@redhat.com)
 *
 * All rights reserved.
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
#ifndef TOTEM_H_DEFINED
#define TOTEM_H_DEFINED
#include "totemip.h"

#define MESSAGE_SIZE_MAX	1024*1024 /* (1MB) */
#define PROCESSOR_COUNT_MAX	384
#define FRAME_SIZE_MAX		9000
#define TRANSMITS_ALLOWED	16
#define SEND_THREADS_MAX	16
#define INTERFACE_MAX		2

struct totem_interface {
	struct totem_ip_address bindnet;
	struct totem_ip_address boundto;
	struct totem_ip_address mcast_addr;
	uint16_t ip_port;
};

struct totem_logging_configuration {
	void (*log_printf) (char *, int, int, char *, ...) __attribute__((format(printf, 4, 5)));
	int log_level_security;
	int log_level_error;
	int log_level_warning;
	int log_level_notice;
	int log_level_debug;
};

struct totem_config {
	int version;

	/*
	 * network
	 */
	struct totem_interface *interfaces;
	int interface_count;
	unsigned int node_id;
	unsigned int clear_node_high_bit;

	/*
	 * key information
	 */
	unsigned char private_key[128];

	int private_key_len;

	/*
	 * Totem configuration parameters
	 */
	unsigned int token_timeout;

	unsigned int token_retransmit_timeout;

	unsigned int token_hold_timeout;

	unsigned int token_retransmits_before_loss_const;

	unsigned int join_timeout;

	unsigned int send_join_timeout;

	unsigned int consensus_timeout;

	unsigned int merge_timeout;

	unsigned int downcheck_timeout;

	unsigned int fail_to_recv_const;

	unsigned int seqno_unchanged_const;

	unsigned int rrp_token_expired_timeout;

	unsigned int rrp_problem_count_timeout;

	unsigned int rrp_problem_count_threshold;

	char rrp_mode[64];

	struct totem_logging_configuration totem_logging_configuration;

	unsigned int secauth;

	unsigned int net_mtu;

	unsigned int threads;
	
	unsigned int heartbeat_failures_allowed;
	
	unsigned int max_network_delay;

	unsigned int window_size;

	unsigned int max_messages;

	char *vsf_type;
};

enum totem_configuration_type {
	TOTEM_CONFIGURATION_REGULAR,
	TOTEM_CONFIGURATION_TRANSITIONAL	
};

enum totem_callback_token_type {
	TOTEM_CALLBACK_TOKEN_RECEIVED = 1,
	TOTEM_CALLBACK_TOKEN_SENT = 2
};

struct memb_ring_id {
	struct totem_ip_address rep;
	unsigned long long seq;
} __attribute__((packed));


#endif /* TOTEM_H_DEFINED */
