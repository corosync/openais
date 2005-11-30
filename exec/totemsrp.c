#define TRANSMITS_ALLOWED 16
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

/*
 * The first version of this code was based upon Yair Amir's PhD thesis:
 *	http://www.cs.jhu.edu/~yairamir/phd.ps) (ch4,5). 
 *
 * The current version of totemsrp implements the Totem protocol specified in:
 * 	http://citeseer.ist.psu.edu/amir95totem.html
 *
 * The deviations from the above published protocols are:
 * - encryption of message contents with SOBER128
 * - authentication of meessage contents with SHA1/HMAC
 * - token hold mode where token doesn't rotate on unused ring - reduces cpu
 *   usage on 1.6ghz xeon from 35% to less then .1 % as measured by top
 */

#include <assert.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/un.h>
#include <sys/sysinfo.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/sockios.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sched.h>
#include <time.h>
#include <sys/time.h>
#include <sys/poll.h>

#include "aispoll.h"
#include "totemsrp.h"
#include "totemrrp.h"
#include "wthread.h"
#include "../include/queue.h"
#include "../include/sq.h"
#include "../include/list.h"
#include "hdb.h"
#include "swab.h"

#include "crypto.h"

#define LOCALHOST_IP					inet_addr("127.0.0.1")
#define QUEUE_RTR_ITEMS_SIZE_MAX		256 /* allow 512 retransmit items */
#define RETRANS_MESSAGE_QUEUE_SIZE_MAX	500 /* allow 500 messages to be queued */
#define RECEIVED_MESSAGE_QUEUE_SIZE_MAX	500 /* allow 500 messages to be queued */
#define MAXIOVS					5	
#define RETRANSMIT_ENTRIES_MAX			30
#define MISSING_MCAST_WINDOW			128

/*
 * Rollover handling:
 * SEQNO_START_MSG is the starting sequence number after a new configuration
 *	This should remain zero, unless testing overflow in which case
 *	0x7ffff000 and 0xfffff000 are good starting values.
 *
 * SEQNO_START_TOKEN is the starting sequence number after a new configuration
 *	for a token.  This should remain zero, unless testing overflow in which
 *	case 07fffff00 or 0xffffff00 are good starting values.
 *
 * SEQNO_START_MSG is the starting sequence number after a new configuration
 *	This should remain zero, unless testing overflow in which case
 *	0x7ffff000 and 0xfffff000 are good values to start with
 */
#define SEQNO_START_MSG 0x0
#define SEQNO_START_TOKEN 0x0
//#define SEQNO_START_MSG 0xfffffe00
//#define SEQNO_START_TOKEN 0xfffffe00

/*
 * we compare incoming messages to determine if their endian is
 * different - if so convert them
 *
 * do not change
 */
#define ENDIAN_LOCAL					 0xff22

enum message_type {
	MESSAGE_TYPE_ORF_TOKEN = 0,			/* Ordering, Reliability, Flow (ORF) control Token */
	MESSAGE_TYPE_MCAST = 1,				/* ring ordered multicast message */
	MESSAGE_TYPE_MEMB_MERGE_DETECT = 2,	/* merge rings if there are available rings */
	MESSAGE_TYPE_MEMB_JOIN = 3, 		/* membership join message */
	MESSAGE_TYPE_MEMB_COMMIT_TOKEN = 4,	/* membership commit token */
	MESSAGE_TYPE_TOKEN_HOLD_CANCEL = 5,	/* cancel the holding of the token */
};

/* 
 * New membership algorithm local variables
 */
struct consensus_list_item {
	struct totem_ip_address addr;
	int set;
};


struct token_callback_instance {
	struct list_head list;
	int (*callback_fn) (enum totem_callback_token_type type, void *);
	enum totem_callback_token_type callback_type;
	int delete;
	void *data;
};


struct totemsrp_socket {
	int mcast;
	int token;
};

struct message_header {
	char type;
	char encapsulated;
	unsigned short endian_detector;
	unsigned int nodeid;
} __attribute__((packed));

struct mcast {
	struct message_header header;
	unsigned int seq;
	int this_seqno;
	struct memb_ring_id ring_id;
	struct totem_ip_address source;
	int guarantee;
} __attribute__((packed));

/*
 * MTU - multicast message header - IP header - UDP header
 *
 * On lossy switches, making use of the DF UDP flag can lead to loss of
 * forward progress.  So the packets must be fragmented by a higher layer
 *
 * This layer can only handle packets of MTU size.
 */
#define FRAGMENT_SIZE (FRAME_SIZE_MAX - sizeof (struct mcast) - 20 - 8)

struct rtr_item  {
	struct memb_ring_id ring_id;
	unsigned int seq;
}__attribute__((packed));

struct orf_token {
	struct message_header header;
	unsigned int seq;
	unsigned int token_seq;
	unsigned int aru;
	struct totem_ip_address aru_addr;
	struct memb_ring_id ring_id; 
	short int fcc;
	int retrans_flg;
	int rtr_list_entries;
	struct rtr_item rtr_list[0];
}__attribute__((packed));

struct memb_join {
	struct message_header header;
	struct totem_ip_address proc_list[PROCESSOR_COUNT_MAX];
	int proc_list_entries;
	struct totem_ip_address failed_list[PROCESSOR_COUNT_MAX];
	int failed_list_entries;
	unsigned long long ring_seq;
} __attribute__((packed));

struct memb_merge_detect {
	struct message_header header;
	struct memb_ring_id ring_id;
} __attribute__((packed));

struct token_hold_cancel {
	struct message_header header;
	struct memb_ring_id ring_id;
} __attribute__((packed));

struct memb_commit_token_memb_entry {
	struct memb_ring_id ring_id;
	unsigned int aru;
	unsigned int high_delivered;
	int received_flg;
}__attribute__((packed));

struct memb_commit_token {
	struct message_header header;
	unsigned int token_seq;
	struct memb_ring_id ring_id;
	unsigned int retrans_flg;
	int memb_index;
	int addr_entries;
	struct totem_ip_address addr[PROCESSOR_COUNT_MAX];
	struct memb_commit_token_memb_entry memb_list[PROCESSOR_COUNT_MAX];
}__attribute__((packed));

struct message_item {
	struct mcast *mcast;
	struct iovec iovec[MAXIOVS];
	int iov_len;
};

struct sort_queue_item {
	struct iovec iovec[MAXIOVS];
	int iov_len;
};

struct orf_token_mcast_thread_state {
	char iobuf[9000];
	prng_state prng_state;
};

enum memb_state {
	MEMB_STATE_OPERATIONAL = 1,
	MEMB_STATE_GATHER = 2,
	MEMB_STATE_COMMIT = 3,
	MEMB_STATE_RECOVERY = 4
};

struct totemsrp_instance {
	int first_run;

	/*
	 * Flow control mcasts and remcasts on last and current orf_token
	 */
	int fcc_remcast_last;

	int fcc_mcast_last;

	int fcc_mcast_current;

	int fcc_remcast_current;

	struct consensus_list_item consensus_list[PROCESSOR_COUNT_MAX];

	int consensus_list_entries;

	struct totem_ip_address my_proc_list[PROCESSOR_COUNT_MAX];

	struct totem_ip_address my_failed_list[PROCESSOR_COUNT_MAX];

	struct totem_ip_address my_new_memb_list[PROCESSOR_COUNT_MAX];

	struct totem_ip_address my_trans_memb_list[PROCESSOR_COUNT_MAX];

	struct totem_ip_address my_memb_list[PROCESSOR_COUNT_MAX];

	struct totem_ip_address my_deliver_memb_list[PROCESSOR_COUNT_MAX];

	struct totem_ip_address my_nodeid_lookup_list[PROCESSOR_COUNT_MAX];

	int my_proc_list_entries;

	int my_failed_list_entries;

	int my_new_memb_entries;

	int my_trans_memb_entries;

	int my_memb_entries;

	int my_deliver_memb_entries;

	int my_nodeid_lookup_entries;

	struct memb_ring_id my_ring_id;

	struct memb_ring_id my_old_ring_id;

	int my_aru_count;

	int my_merge_detect_timeout_outstanding;

	unsigned int my_last_aru;

	int my_seq_unchanged;

	int my_received_flg;

	unsigned int my_high_seq_received;

	unsigned int my_install_seq;

	int my_rotation_counter;

	int my_set_retrans_flg;

	int my_retrans_flg_count;

	unsigned int my_high_ring_delivered;
	
	int heartbeat_timeout;

	/*
	 * Queues used to order, deliver, and recover messages
	 */
	struct queue new_message_queue;

	struct queue retrans_message_queue;

	struct sq regular_sort_queue;

	struct sq recovery_sort_queue;

	/*
	 * Received up to and including
	 */
	unsigned int my_aru;

	unsigned int my_high_delivered;

	struct list_head token_callback_received_listhead;

	struct list_head token_callback_sent_listhead;

	char *orf_token_retransmit; // sizeof (struct orf_token) + sizeof (struct rtr_item) * RETRANSMIT_ENTRIES_MAX];

	int orf_token_retransmit_size;

	unsigned int my_token_seq;

	/*
	 * Timers
	 */
	poll_timer_handle timer_orf_token_timeout;

	poll_timer_handle timer_orf_token_retransmit_timeout;

	poll_timer_handle timer_orf_token_hold_retransmit_timeout;

	poll_timer_handle timer_merge_detect_timeout;

	poll_timer_handle memb_timer_state_gather_join_timeout;

	poll_timer_handle memb_timer_state_gather_consensus_timeout;

	poll_timer_handle memb_timer_state_commit_timeout;

	poll_timer_handle timer_heartbeat_timeout;

	/*
	 * Function and data used to log messages
	 */
	int totemsrp_log_level_security;

	int totemsrp_log_level_error;

	int totemsrp_log_level_warning;

	int totemsrp_log_level_notice;

	int totemsrp_log_level_debug;

	void (*totemsrp_log_printf) (int level, char *format, ...);

	enum memb_state memb_state;

	struct totem_ip_address my_id;

	struct totem_ip_address next_memb;

	char iov_buffer[FRAME_SIZE_MAX];

	struct iovec totemsrp_iov_recv;

	poll_handle totemsrp_poll_handle;

	/*
	 * Function called when new message received
	 */
	int (*totemsrp_recv) (char *group, struct iovec *iovec, int iov_len);

	struct totem_ip_address mcast_address;

	void (*totemsrp_deliver_fn) (
		struct totem_ip_address *source_addr,
		struct iovec *iovec,
		int iov_len,
		int endian_conversion_required);

	void (*totemsrp_confchg_fn) (
		enum totem_configuration_type configuration_type,
		struct totem_ip_address *member_list, int member_list_entries,
		struct totem_ip_address *left_list, int left_list_entries,
		struct totem_ip_address *joined_list, int joined_list_entries,
		struct memb_ring_id *ring_id);

	int global_seqno;

	int my_token_held;

	unsigned long long token_ring_id_seq;

	unsigned int last_released;

	unsigned int set_aru;

	int old_ring_state_saved;

	int old_ring_state_aru;

	unsigned int old_ring_state_high_seq_received;

	int ring_saved;

	unsigned int my_last_seq;

	struct timeval tv_old;

	totemrrp_handle totemrrp_handle;

	struct totem_config *totem_config;

	int use_heartbeat;
};

struct message_handlers {
	int count;
	int (*handler_functions[6]) (
		struct totemsrp_instance *instance,
		struct totem_ip_address *system_from,
		void *msg,
		int msg_len,
		int endian_conversion_needed);
};

/*
 * forward decls
 */
static int message_handler_orf_token (
	struct totemsrp_instance *instance,
	struct totem_ip_address *system_from,
	void *msg,
	int msg_len,
	int endian_conversion_needed);

static int message_handler_mcast (
	struct totemsrp_instance *instance,
	struct totem_ip_address *system_from,
	void *msg,
	int msg_len,
	int endian_conversion_needed);

static int message_handler_memb_merge_detect (
	struct totemsrp_instance *instance,
	struct totem_ip_address *system_from,
	void *msg,
	int msg_len,
	int endian_conversion_needed);

static int message_handler_memb_join (
	struct totemsrp_instance *instance,
	struct totem_ip_address *system_from,
	void *msg,
	int msg_len,
	int endian_conversion_needed);

static int message_handler_memb_commit_token (
	struct totemsrp_instance *instance,
	struct totem_ip_address *system_from,
	void *msg,
	int msg_len,
	int endian_conversion_needed);

static int message_handler_token_hold_cancel (
	struct totemsrp_instance *instance,
	struct totem_ip_address *system_from,
	void *msg,
	int msg_len,
	int endian_conversion_needed);

static void memb_ring_id_create_or_load (struct totemsrp_instance *, struct memb_ring_id *);

static void token_callbacks_execute (struct totemsrp_instance *instance, enum totem_callback_token_type type);
static void memb_state_gather_enter (struct totemsrp_instance *instance);
static void messages_deliver_to_app (struct totemsrp_instance *instance, int skip, unsigned int end_point);
static int orf_token_mcast (struct totemsrp_instance *instance, struct orf_token *oken,
	int fcc_mcasts_allowed, struct totem_ip_address *system_from);
static void messages_free (struct totemsrp_instance *instance, unsigned int token_aru);

static void memb_ring_id_store (struct totemsrp_instance *instance, struct memb_commit_token *commit_token);
static void memb_state_commit_token_update (struct totemsrp_instance *instance, struct memb_commit_token *memb_commit_token);
static int memb_state_commit_token_send (struct totemsrp_instance *instance, struct memb_commit_token *memb_commit_token);
static void memb_state_commit_token_create (struct totemsrp_instance *instance, struct memb_commit_token *commit_token);
static int token_hold_cancel_send (struct totemsrp_instance *instance);
static void orf_token_endian_convert (struct orf_token *in, struct orf_token *out);
static void memb_commit_token_endian_convert (struct memb_commit_token *in, struct memb_commit_token *out);
static void memb_join_endian_convert (struct memb_join *in, struct memb_join *out);
static void mcast_endian_convert (struct mcast *in, struct mcast *out);
static void timer_function_orf_token_timeout (void *data);
static void timer_function_heartbeat_timeout (void *data);
static void timer_function_token_retransmit_timeout (void *data);
static void timer_function_token_hold_retransmit_timeout (void *data);
static void timer_function_merge_detect_timeout (void *data);

void main_deliver_fn (
	void *context,
	struct totem_ip_address *system_from,
	void *msg,
	int msg_len);

void main_iface_change_fn (
	void *context,
	struct totem_ip_address *iface_address);

/*
 * All instances in one database
 */
static struct saHandleDatabase totemsrp_instance_database = {
	.handleCount				= 0,
	.handles					= 0,
	.handleInstanceDestructor	= 0
};
struct message_handlers totemsrp_message_handlers = {
	6,
	{
		message_handler_orf_token,
		message_handler_mcast,
		message_handler_memb_merge_detect,
		message_handler_memb_join,
		message_handler_memb_commit_token,
		message_handler_token_hold_cancel
	}
};

void totemsrp_instance_initialize (struct totemsrp_instance *instance)
{
	memset (instance, 0, sizeof (struct totemsrp_instance));

	list_init (&instance->token_callback_received_listhead);

	list_init (&instance->token_callback_sent_listhead);

	instance->my_received_flg = 1;

	instance->my_token_seq = SEQNO_START_TOKEN - 1;

	instance->orf_token_retransmit = malloc (15000);

	instance->memb_state = MEMB_STATE_OPERATIONAL;

	instance->set_aru = -1;

	instance->my_aru = SEQNO_START_MSG;

	instance->my_high_seq_received = SEQNO_START_MSG;

	instance->my_high_delivered = SEQNO_START_MSG;
}

void main_token_seqid_get (
	void *msg,
	unsigned int *seqid,
	unsigned int *token_is)
{
	struct orf_token *token = (struct orf_token *)msg;

	*seqid = 0;
	*token_is = 0;
	if (token->header.type == MESSAGE_TYPE_ORF_TOKEN) {
		*seqid = token->seq;
		*token_is = 1;
	}
}

/*
 * Exported interfaces
 */
int totemsrp_initialize (
	poll_handle poll_handle,
	totemsrp_handle *handle,
	struct totem_config *totem_config,

	void (*deliver_fn) (
		struct totem_ip_address *source_addr,
		struct iovec *iovec,
		int iov_len,
		int endian_conversion_required),

	void (*confchg_fn) (
		enum totem_configuration_type configuration_type,
		struct totem_ip_address *member_list, int member_list_entries,
		struct totem_ip_address *left_list, int left_list_entries,
		struct totem_ip_address *joined_list, int joined_list_entries,
		struct memb_ring_id *ring_id))
{
	struct totemsrp_instance *instance;
	SaErrorT error;

	error = saHandleCreate (&totemsrp_instance_database,
		sizeof (struct totemsrp_instance), handle);
	if (error != SA_OK) {
		goto error_exit;
	}
	error = saHandleInstanceGet (&totemsrp_instance_database, *handle,
		(void *)&instance);
	if (error != SA_OK) {
		goto error_destroy;
	}

	totemsrp_instance_initialize (instance);

	instance->totem_config = totem_config;

	/*
	 * Configure logging
	 */
	instance->totemsrp_log_level_security = totem_config->totem_logging_configuration.log_level_security;
	instance->totemsrp_log_level_error = totem_config->totem_logging_configuration.log_level_error;
	instance->totemsrp_log_level_warning = totem_config->totem_logging_configuration.log_level_warning;
	instance->totemsrp_log_level_notice = totem_config->totem_logging_configuration.log_level_notice;
	instance->totemsrp_log_level_debug = totem_config->totem_logging_configuration.log_level_debug;
	instance->totemsrp_log_printf = totem_config->totem_logging_configuration.log_printf;

	/*
	 * Initialize local variables for totemsrp
	 */
	totemip_copy (&instance->mcast_address, &totem_config->mcast_addr);

	memset (&instance->next_memb, 0, sizeof (struct totem_ip_address));
	memset (instance->iov_buffer, 0, FRAME_SIZE_MAX);

	/*
	 * Display totem configuration
	 */
	instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
		"Token Timeout (%d ms) retransmit timeout (%d ms)\n",
		totem_config->token_timeout, totem_config->token_retransmit_timeout);
	instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
		"token hold (%d ms) retransmits before loss (%d retrans)\n",
		totem_config->token_hold_timeout, totem_config->token_retransmits_before_loss_const);
	instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
		"join (%d ms) consensus (%d ms) merge (%d ms)\n",
		totem_config->join_timeout, totem_config->consensus_timeout,
		totem_config->merge_timeout);
	instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
		"downcheck (%d ms) fail to recv const (%d msgs)\n",
		totem_config->downcheck_timeout, totem_config->fail_to_recv_const);
	instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
		"seqno unchanged const (%d rotations) Maximum network MTU %d\n", totem_config->seqno_unchanged_const, totem_config->net_mtu);
	instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
		"send threads (%d threads)\n", totem_config->threads);
	instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
		"heartbeat_failures_allowed (%d)\n", totem_config->heartbeat_failures_allowed);
	instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
		"max_network_delay (%d ms)\n", totem_config->max_network_delay);


	queue_init (&instance->retrans_message_queue, RETRANS_MESSAGE_QUEUE_SIZE_MAX,
		sizeof (struct message_item));

	sq_init (&instance->regular_sort_queue,
		QUEUE_RTR_ITEMS_SIZE_MAX, sizeof (struct sort_queue_item), 0);

	sq_init (&instance->recovery_sort_queue,
		QUEUE_RTR_ITEMS_SIZE_MAX, sizeof (struct sort_queue_item), 0);

	instance->totemsrp_poll_handle = poll_handle;

	instance->totemsrp_deliver_fn = deliver_fn;

	instance->totemsrp_confchg_fn = confchg_fn;
	instance->use_heartbeat = 1;

	if ( totem_config->heartbeat_failures_allowed == 0 ) {
		instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
			"HeartBeat is Disabled. To enable set heartbeat_failures_allowed > 0\n");
		instance->use_heartbeat = 0;
	}

	if (instance->use_heartbeat) {
		instance->heartbeat_timeout 
			= (totem_config->heartbeat_failures_allowed) * totem_config->token_retransmit_timeout 
				+ totem_config->max_network_delay;

		if (instance->heartbeat_timeout >= totem_config->token_timeout) {
			instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
				"total heartbeat_timeout (%d ms) is not less than token timeout (%d ms)\n", 
				instance->heartbeat_timeout,
				totem_config->token_timeout);
			instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
				"heartbeat_timeout = heartbeat_failures_allowed * token_retransmit_timeout + max_network_delay\n");
			instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
				"heartbeat timeout should be less than the token timeout. HeartBeat is Diabled !!\n");
			instance->use_heartbeat = 0;
		}
		else {
			instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
                		"total heartbeat_timeout (%d ms)\n", instance->heartbeat_timeout);
		}
	}
	
	totemrrp_initialize (
		poll_handle,
		&instance->totemrrp_handle,
		totem_config,
		instance,
		main_deliver_fn,
		main_iface_change_fn,
		main_token_seqid_get);

	/*
	 * Must have net_mtu adjusted by totemrrp_initialize first
	 */

	queue_init (&instance->new_message_queue,
		(MESSAGE_SIZE_MAX / (totem_config->net_mtu - 25) /* for totempg_mcat header */),
		sizeof (struct message_item));

	return (0);

error_destroy:
	saHandleDestroy (&totemsrp_instance_database, *handle);

error_exit:
	return (-1);
}

void totemsrp_finalize (
	totemsrp_handle handle)
{
	struct totemsrp_instance *instance;
	SaErrorT error;

	error = saHandleInstanceGet (&totemsrp_instance_database, handle,
		(void *)&instance);
	if (error != SA_OK) {
		return;
	}

	saHandleInstancePut (&totemsrp_instance_database, handle);
}

/*
 * Set operations for use by the membership algorithm
 */
static void memb_consensus_reset (struct totemsrp_instance *instance)
{
	instance->consensus_list_entries = 0;
}

static void memb_set_subtract (
        struct totem_ip_address *out_list, int *out_list_entries,
        struct totem_ip_address *one_list, int one_list_entries,
        struct totem_ip_address *two_list, int two_list_entries)
{
	int found = 0;
	int i;
	int j;

	*out_list_entries = 0;

	for (i = 0; i < one_list_entries; i++) {
		for (j = 0; j < two_list_entries; j++) {
			if (totemip_equal(&one_list[i], &two_list[j])) {
				found = 1;
				break;
			}
		}
		if (found == 0) {
			totemip_copy(&out_list[*out_list_entries], &one_list[i]);
			*out_list_entries = *out_list_entries + 1;
		}
		found = 0;
	}
}

/*
 * Set consensus for a specific processor
 */
static void memb_consensus_set (
	struct totemsrp_instance *instance,
	struct totem_ip_address *addr)
{
	int found = 0;
	int i;

	for (i = 0; i < instance->consensus_list_entries; i++) {
		if (totemip_equal(addr, &instance->consensus_list[i].addr)) {
			found = 1;
			break; /* found entry */
		}
	}
	totemip_copy(&instance->consensus_list[i].addr, addr);
	instance->consensus_list[i].set = 1;
	if (found == 0) {
		instance->consensus_list_entries++;
	}
	return;
}

/*
 * Is consensus set for a specific processor
 */
static int memb_consensus_isset (
	struct totemsrp_instance *instance,
	struct totem_ip_address *addr)
{
	int i;

	for (i = 0; i < instance->consensus_list_entries; i++) {
		if (totemip_equal(addr, &instance->consensus_list[i].addr)) {
			return (instance->consensus_list[i].set);
		}
	}
	return (0);
}

/*
 * Is consensus agreed upon based upon consensus database
 */
static int memb_consensus_agreed (
	struct totemsrp_instance *instance)
{
	struct totem_ip_address token_memb[PROCESSOR_COUNT_MAX];
	int token_memb_entries = 0;
	int agreed = 1;
	int i;

	memb_set_subtract (token_memb, &token_memb_entries,
		instance->my_proc_list, instance->my_proc_list_entries,
		instance->my_failed_list, instance->my_failed_list_entries);

	for (i = 0; i < token_memb_entries; i++) {
		if (memb_consensus_isset (instance, &token_memb[i]) == 0) {
			agreed = 0;
			break;
		}
	}
	assert (token_memb_entries >= 1);

	return (agreed);
}

static void memb_consensus_notset (
	struct totemsrp_instance *instance,
	struct totem_ip_address *no_consensus_list,
	int *no_consensus_list_entries,
	struct totem_ip_address *comparison_list,
	int comparison_list_entries)
{
	int i;

	*no_consensus_list_entries = 0;

	for (i = 0; i < instance->my_proc_list_entries; i++) {
		if (memb_consensus_isset (instance, &instance->my_proc_list[i]) == 0) {
			totemip_copy(&no_consensus_list[*no_consensus_list_entries], &instance->my_proc_list[i]);
			*no_consensus_list_entries = *no_consensus_list_entries + 1;
		}
	}
}

/*
 * Is set1 equal to set2 Entries can be in different orders
 */
static int memb_set_equal (struct totem_ip_address *set1, int set1_entries,
	struct totem_ip_address *set2, int set2_entries)
{
	int i;
	int j;

	int found = 0;

	if (set1_entries != set2_entries) {
		return (0);
	}
	for (i = 0; i < set2_entries; i++) {
		for (j = 0; j < set1_entries; j++) {
			if (totemip_equal(&set1[j], &set2[i])) {
				found = 1;
				break;
			}
		}
		if (found == 0) {
			return (0);
		}
		found = 0;
	}
	return (1);
}

/*
 * Is subset fully contained in fullset
 */
static int memb_set_subset (
	struct totem_ip_address *subset, int subset_entries,
	struct totem_ip_address *fullset, int fullset_entries)
{
	int i;
	int j;
	int found = 0;

	if (subset_entries > fullset_entries) {
		return (0);
	}
	for (i = 0; i < subset_entries; i++) {
		for (j = 0; j < fullset_entries; j++) {
			if (totemip_equal(&subset[i], &fullset[j])) {
				found = 1;
			}
		}
		if (found == 0) {
			return (0);
		}
		found = 1;
	}
	return (1);
}

/*
 * merge subset into fullset taking care not to add duplicates
 */
static void memb_set_merge (
	struct totem_ip_address *subset, int subset_entries,
	struct totem_ip_address *fullset, int *fullset_entries)
{
	int found = 0;
	int i;
	int j;

	for (i = 0; i < subset_entries; i++) {
		for (j = 0; j < *fullset_entries; j++) {
			if (totemip_equal(&fullset[j], &subset[i])) {
				found = 1;
				break;
			}	
		}
		if (found == 0) {
			totemip_copy(&fullset[j], &subset[i]);
			*fullset_entries = *fullset_entries + 1;
		}
		found = 0;
	}
	return;
}

static void memb_set_and (
        struct totem_ip_address *set1, int set1_entries,
        struct totem_ip_address *set2, int set2_entries,
        struct totem_ip_address *and, int *and_entries)
{
	int i;
	int j;
	int found = 0;

	*and_entries = 0;

	for (i = 0; i < set2_entries; i++) {
		for (j = 0; j < set1_entries; j++) {
			if (totemip_equal(&set1[j], &set2[i])) {
				found = 1;
				break;
			}
		}
		if (found) {
			totemip_copy(&and[*and_entries], &set1[j]);
			*and_entries = *and_entries + 1;
		}
		found = 0;
	}
	return;
}

static void memb_set_print (
	char *string,
        struct totem_ip_address *list,
	int list_entries)
{
	int i;
	printf ("List '%s' contains %d entries:\n", string, list_entries);

	for (i = 0; i < list_entries; i++) {
		printf ("addr %s\n", totemip_print (&list[i]));
	}
}

static void reset_token_retransmit_timeout (struct totemsrp_instance *instance)
{
	poll_timer_delete (instance->totemsrp_poll_handle,
		instance->timer_orf_token_retransmit_timeout);
	poll_timer_add (instance->totemsrp_poll_handle,
		instance->totem_config->token_retransmit_timeout,
		(void *)instance,
		timer_function_token_retransmit_timeout,
		&instance->timer_orf_token_retransmit_timeout);

}

static void start_merge_detect_timeout (struct totemsrp_instance *instance)
{
	if (instance->my_merge_detect_timeout_outstanding == 0) {
		poll_timer_add (instance->totemsrp_poll_handle,
			instance->totem_config->merge_timeout,
			(void *)instance,
			timer_function_merge_detect_timeout,
			&instance->timer_merge_detect_timeout);

		instance->my_merge_detect_timeout_outstanding = 1;
	}
}

static void cancel_merge_detect_timeout (struct totemsrp_instance *instance)
{
	poll_timer_delete (instance->totemsrp_poll_handle, instance->timer_merge_detect_timeout);
	instance->my_merge_detect_timeout_outstanding = 0;
}

/*
 * ring_state_* is used to save and restore the sort queue
 * state when a recovery operation fails (and enters gather)
 */
static void old_ring_state_save (struct totemsrp_instance *instance)
{
	if (instance->old_ring_state_saved == 0) {
		instance->old_ring_state_saved = 1;
		instance->old_ring_state_aru = instance->my_aru;
		instance->old_ring_state_high_seq_received = instance->my_high_seq_received;
		instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
			"Saving state aru %x high seq received %x\n",
			instance->my_aru, instance->my_high_seq_received);
	}
}

static void ring_save (struct totemsrp_instance *instance)
{
	if (instance->ring_saved == 0) {
		instance->ring_saved = 1;
		memcpy (&instance->my_old_ring_id, &instance->my_ring_id,
			sizeof (struct memb_ring_id));
	}
}

static void ring_reset (struct totemsrp_instance *instance)
{
	instance->ring_saved = 0;
}

static void ring_state_restore (struct totemsrp_instance *instance)
{
	if (instance->old_ring_state_saved) {
		totemip_zero_set(&instance->my_ring_id.rep);
		instance->my_aru = instance->old_ring_state_aru;
		instance->my_high_seq_received = instance->old_ring_state_high_seq_received;
		instance->totemsrp_log_printf (instance->totemsrp_log_level_debug,
			"Restoring instance->my_aru %x my high seq received %x\n",
			instance->my_aru, instance->my_high_seq_received);
	}
}

static void old_ring_state_reset (struct totemsrp_instance *instance)
{
	instance->old_ring_state_saved = 0;
}

static void reset_token_timeout (struct totemsrp_instance *instance) {
	poll_timer_delete (instance->totemsrp_poll_handle, instance->timer_orf_token_timeout);
	poll_timer_add (instance->totemsrp_poll_handle,
		instance->totem_config->token_timeout,
		(void *)instance,
		timer_function_orf_token_timeout,
		&instance->timer_orf_token_timeout);
}

static void reset_heartbeat_timeout (struct totemsrp_instance *instance) {
        poll_timer_delete (instance->totemsrp_poll_handle, instance->timer_heartbeat_timeout);
        poll_timer_add (instance->totemsrp_poll_handle,
                instance->heartbeat_timeout,
                (void *)instance,
                timer_function_heartbeat_timeout,
                &instance->timer_heartbeat_timeout);
}


static void cancel_token_timeout (struct totemsrp_instance *instance) {
	poll_timer_delete (instance->totemsrp_poll_handle, instance->timer_orf_token_timeout);
}

static void cancel_heartbeat_timeout (struct totemsrp_instance *instance) {
	poll_timer_delete (instance->totemsrp_poll_handle, instance->timer_heartbeat_timeout);
}

static void cancel_token_retransmit_timeout (struct totemsrp_instance *instance)
{
	poll_timer_delete (instance->totemsrp_poll_handle, instance->timer_orf_token_retransmit_timeout);
}

static void start_token_hold_retransmit_timeout (struct totemsrp_instance *instance)
{
	poll_timer_add (instance->totemsrp_poll_handle,
		instance->totem_config->token_hold_timeout,
		(void *)instance,
		timer_function_token_hold_retransmit_timeout,
		&instance->timer_orf_token_hold_retransmit_timeout);
}

static void cancel_token_hold_retransmit_timeout (struct totemsrp_instance *instance)
{
	poll_timer_delete (instance->totemsrp_poll_handle,
		instance->timer_orf_token_hold_retransmit_timeout);
}

static void memb_state_consensus_timeout_expired (
		struct totemsrp_instance *instance)
{
        struct totem_ip_address no_consensus_list[PROCESSOR_COUNT_MAX];
	int no_consensus_list_entries;

	if (memb_consensus_agreed (instance)) {
		memb_consensus_reset (instance);

		memb_consensus_set (instance, &instance->my_id);

		reset_token_timeout (instance); // REVIEWED
	} else {
		memb_consensus_notset (instance, no_consensus_list,
			&no_consensus_list_entries,
			instance->my_proc_list, instance->my_proc_list_entries);

		memb_set_merge (no_consensus_list, no_consensus_list_entries,
			instance->my_failed_list, &instance->my_failed_list_entries);

		memb_state_gather_enter (instance);
	}
}

static void memb_join_message_send (struct totemsrp_instance *instance);

static void memb_merge_detect_transmit (struct totemsrp_instance *instance);

/*
 * Timers used for various states of the membership algorithm
 */
static void timer_function_orf_token_timeout (void *data)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)data;

	instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
		"The token was lost in state %d from timer %x\n", instance->memb_state, data);
	switch (instance->memb_state) {
		case MEMB_STATE_OPERATIONAL:
			totemrrp_iface_check (instance->totemrrp_handle);
			memb_state_gather_enter (instance);
			break;

		case MEMB_STATE_GATHER:
			memb_state_consensus_timeout_expired (instance);
			memb_state_gather_enter (instance);
			break;

		case MEMB_STATE_COMMIT:
			memb_state_gather_enter (instance);
			break;
		
		case MEMB_STATE_RECOVERY:
			ring_state_restore (instance);
			memb_state_gather_enter (instance);
			break;
	}
}

static void timer_function_heartbeat_timeout (void *data)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)data;
	instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
		"HeartBeat Timer expired Invoking token loss mechanism in state %d \n", instance->memb_state);
	timer_function_orf_token_timeout(data);
}

static void memb_timer_function_state_gather (void *data)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)data;

	switch (instance->memb_state) {
	case MEMB_STATE_OPERATIONAL:
	case MEMB_STATE_RECOVERY:
		assert (0); /* this should never happen */
		break;
	case MEMB_STATE_GATHER:
	case MEMB_STATE_COMMIT:
		memb_join_message_send (instance);

		/*
		 * Restart the join timeout
		`*/
		poll_timer_delete (instance->totemsrp_poll_handle, instance->memb_timer_state_gather_join_timeout);
	
		poll_timer_add (instance->totemsrp_poll_handle,
			instance->totem_config->join_timeout,
			(void *)instance,
			memb_timer_function_state_gather,
			&instance->memb_timer_state_gather_join_timeout);
		break;
	}
}

static void memb_timer_function_gather_consensus_timeout (void *data)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)data;
	memb_state_consensus_timeout_expired (instance);
}

static void deliver_messages_from_recovery_to_regular (struct totemsrp_instance *instance)
{
	unsigned int i;
	struct sort_queue_item *recovery_message_item;
	struct sort_queue_item regular_message_item;
	unsigned int range = 0;
	int res;
	void *ptr;
	struct mcast *mcast;

	instance->totemsrp_log_printf (instance->totemsrp_log_level_debug,
		"recovery to regular %x-%x\n", SEQNO_START_MSG + 1, instance->my_aru);

	range = instance->my_aru - SEQNO_START_MSG;
	/*
	 * Move messages from recovery to regular sort queue
	 */
// todo should i be initialized to 0 or 1 ?
	for (i = 1; i <= range; i++) {
		res = sq_item_get (&instance->recovery_sort_queue,
			i + SEQNO_START_MSG, &ptr);
		if (res != 0) {
			continue;
		}
		recovery_message_item = (struct sort_queue_item *)ptr;

		/*
		 * Convert recovery message into regular message
		 */
		if (recovery_message_item->iov_len > 1) {
			mcast = recovery_message_item->iovec[1].iov_base;
			memcpy (&regular_message_item.iovec[0],
				&recovery_message_item->iovec[1],
				sizeof (struct iovec) * recovery_message_item->iov_len);
		} else {
			mcast = recovery_message_item->iovec[0].iov_base;
			if (mcast->header.encapsulated == 1) {
				/*
				 * Message is a recovery message encapsulated
				 * in a new ring message
				 */
				regular_message_item.iovec[0].iov_base =
					recovery_message_item->iovec[0].iov_base + sizeof (struct mcast);
				regular_message_item.iovec[0].iov_len =
				recovery_message_item->iovec[0].iov_len - sizeof (struct mcast);
				regular_message_item.iov_len = 1;
				mcast = regular_message_item.iovec[0].iov_base;
			} else {
				continue; /* TODO this case shouldn't happen */
				/*
				 * Message is originated on new ring and not
				 * encapsulated
				 */
				regular_message_item.iovec[0].iov_base =
					recovery_message_item->iovec[0].iov_base;
				regular_message_item.iovec[0].iov_len =
				recovery_message_item->iovec[0].iov_len;
			}
		}

		instance->totemsrp_log_printf (instance->totemsrp_log_level_debug,
			"comparing if ring id is for this processors old ring seqno %d\n",
			 mcast->seq);

		/*
		 * Only add this message to the regular sort
		 * queue if it was originated with the same ring
		 * id as the previous ring
		 */
		if (memcmp (&instance->my_old_ring_id, &mcast->ring_id,
			sizeof (struct memb_ring_id)) == 0) {

			regular_message_item.iov_len = recovery_message_item->iov_len;
			res = sq_item_inuse (&instance->regular_sort_queue, mcast->seq);
			if (res == 0) {
				sq_item_add (&instance->regular_sort_queue,
					&regular_message_item, mcast->seq);
				if (sq_lt_compare (instance->old_ring_state_high_seq_received, mcast->seq)) {
					instance->old_ring_state_high_seq_received = mcast->seq;
				}
			}
		} else {
			instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
				"-not adding msg with seq no %x\n", mcast->seq);
		}
	}
}

/*
 * Change states in the state machine of the membership algorithm
 */
static void memb_state_operational_enter (struct totemsrp_instance *instance)
{
	struct totem_ip_address joined_list[PROCESSOR_COUNT_MAX];
	int joined_list_entries = 0;
	struct totem_ip_address left_list[PROCESSOR_COUNT_MAX];
	int left_list_entries = 0;
	unsigned int aru_save;

	old_ring_state_reset (instance);
	ring_reset (instance);
	deliver_messages_from_recovery_to_regular (instance);

	instance->totemsrp_log_printf (instance->totemsrp_log_level_debug,
		"Delivering to app %x to %x\n",
		instance->my_high_delivered + 1, instance->old_ring_state_high_seq_received);

	aru_save = instance->my_aru;
	instance->my_aru = instance->old_ring_state_aru;

	messages_deliver_to_app (instance, 0, instance->old_ring_state_high_seq_received);

	/*
	 * Calculate joined and left list
	 */
	memb_set_subtract (left_list, &left_list_entries,
		instance->my_memb_list, instance->my_memb_entries,
		instance->my_trans_memb_list, instance->my_trans_memb_entries);

	memb_set_subtract (joined_list, &joined_list_entries,
		instance->my_new_memb_list, instance->my_new_memb_entries,
		instance->my_trans_memb_list, instance->my_trans_memb_entries);

	/*
	 * Deliver transitional configuration to application
	 */
	instance->totemsrp_confchg_fn (TOTEM_CONFIGURATION_TRANSITIONAL,
		instance->my_trans_memb_list, instance->my_trans_memb_entries,
		left_list, left_list_entries,
		0, 0, &instance->my_ring_id);
		
// TODO we need to filter to ensure we only deliver those
// messages which are part of instance->my_deliver_memb
	messages_deliver_to_app (instance, 1, instance->old_ring_state_high_seq_received);

	instance->my_aru = aru_save;

	/*
	 * Deliver regular configuration to application
	 */
	instance->totemsrp_confchg_fn (TOTEM_CONFIGURATION_REGULAR,
		instance->my_new_memb_list, instance->my_new_memb_entries,
		0, 0,
		joined_list, joined_list_entries, &instance->my_ring_id);

	/*
	 * Install new membership
	 */
	instance->my_memb_entries = instance->my_new_memb_entries;
	memcpy (instance->my_memb_list, instance->my_new_memb_list,
		sizeof (struct totem_ip_address) * instance->my_memb_entries);
	instance->last_released = 0;
	instance->my_set_retrans_flg = 0;
	/*
	 * The recovery sort queue now becomes the regular
	 * sort queue.  It is necessary to copy the state
	 * into the regular sort queue.
	 */
	sq_copy (&instance->regular_sort_queue, &instance->recovery_sort_queue);
	instance->my_last_aru = SEQNO_START_MSG;
	sq_items_release (&instance->regular_sort_queue, SEQNO_START_MSG - 1);

	instance->my_proc_list_entries = instance->my_new_memb_entries;
	memcpy (instance->my_proc_list, instance->my_new_memb_list,
		sizeof (struct totem_ip_address) * instance->my_memb_entries);

	instance->my_failed_list_entries = 0;
	instance->my_high_delivered = instance->my_aru;
// TODO the recovery messages are leaked

	instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
		"entering OPERATIONAL state.\n");
	instance->memb_state = MEMB_STATE_OPERATIONAL;

	return;
}

static void memb_state_gather_enter (struct totemsrp_instance *instance)
{
	memb_set_merge (&instance->my_id, 1,
		instance->my_proc_list, &instance->my_proc_list_entries);

	memb_join_message_send (instance);

	/*
	 * Restart the join timeout
	 */
	poll_timer_delete (instance->totemsrp_poll_handle, instance->memb_timer_state_gather_join_timeout);

	poll_timer_add (instance->totemsrp_poll_handle,
		instance->totem_config->join_timeout,
		(void *)instance,
		memb_timer_function_state_gather,
		&instance->memb_timer_state_gather_join_timeout);

	/*
	 * Restart the consensus timeout
	 */
	poll_timer_delete (instance->totemsrp_poll_handle,
		instance->memb_timer_state_gather_consensus_timeout);

	poll_timer_add (instance->totemsrp_poll_handle,
		instance->totem_config->consensus_timeout,
		(void *)instance,
		memb_timer_function_gather_consensus_timeout,
		&instance->memb_timer_state_gather_consensus_timeout);

	/*
	 * Cancel the token loss and token retransmission timeouts
	 */
	cancel_token_retransmit_timeout (instance); // REVIEWED
	cancel_token_timeout (instance); // REVIEWED
	cancel_merge_detect_timeout (instance);

	memb_consensus_reset (instance);

	memb_consensus_set (instance, &instance->my_id);

	instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
		"entering GATHER state.\n");

	instance->memb_state = MEMB_STATE_GATHER;

	return;
}

static void timer_function_token_retransmit_timeout (void *data);

static void memb_state_commit_enter (
	struct totemsrp_instance *instance,
	struct memb_commit_token *commit_token)
{
	ring_save (instance);

	old_ring_state_save (instance); 

	memb_state_commit_token_update (instance, commit_token);

	memb_state_commit_token_send (instance, commit_token);

	memb_ring_id_store (instance, commit_token);

	poll_timer_delete (instance->totemsrp_poll_handle, instance->memb_timer_state_gather_join_timeout);

	instance->memb_timer_state_gather_join_timeout = 0;

	poll_timer_delete (instance->totemsrp_poll_handle, instance->memb_timer_state_gather_consensus_timeout);

	instance->memb_timer_state_gather_consensus_timeout = 0;

	reset_token_timeout (instance); // REVIEWED
	reset_token_retransmit_timeout (instance); // REVIEWED

	instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
		"entering COMMIT state.\n");

	instance->memb_state = MEMB_STATE_COMMIT;

	return;
}

static void memb_state_recovery_enter (
	struct totemsrp_instance *instance,
	struct memb_commit_token *commit_token)
{
	int i;
#ifdef COMPILE_OUT
	int local_received_flg = 1;
#endif
	unsigned int low_ring_aru;
	unsigned int range = 0;
	unsigned int messages_originated = 0;
	char is_originated[4096];
	char not_originated[4096];
	char seqno_string_hex[10];

	instance->my_high_ring_delivered = 0;

	sq_reinit (&instance->recovery_sort_queue, SEQNO_START_MSG);
	queue_reinit (&instance->retrans_message_queue);

	low_ring_aru = instance->old_ring_state_high_seq_received;

	memb_state_commit_token_send (instance, commit_token);

	instance->my_token_seq = SEQNO_START_TOKEN - 1;

	/*
	 * Build regular configuration
	 */
	instance->my_new_memb_entries = commit_token->addr_entries;

 	totemrrp_processor_count_set (
		instance->totemrrp_handle,
		commit_token->addr_entries);

	memcpy (instance->my_new_memb_list, commit_token->addr,
		sizeof (struct totem_ip_address) * instance->my_new_memb_entries);

	/*
	 * Build transitional configuration
	 */
	memb_set_and (instance->my_new_memb_list, instance->my_new_memb_entries,
		instance->my_memb_list, instance->my_memb_entries,
		instance->my_trans_memb_list, &instance->my_trans_memb_entries);

	for (i = 0; i < instance->my_new_memb_entries; i++) {
		instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
			"position [%d] member %s:\n", i, totemip_print (&commit_token->addr[i]));
		instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
			"previous ring seq %lld rep %s\n",
			commit_token->memb_list[i].ring_id.seq,
			totemip_print (&commit_token->memb_list[i].ring_id.rep));

		instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
			"aru %x high delivered %x received flag %d\n",
			commit_token->memb_list[i].aru,
			commit_token->memb_list[i].high_delivered,
			commit_token->memb_list[i].received_flg);

		assert (!totemip_zero_check(&commit_token->memb_list[i].ring_id.rep));
	}
	/*
	 * Determine if any received flag is false
	 */
#ifdef COMPILE_OUT
	for (i = 0; i < commit_token->addr_entries; i++) {
		if (memb_set_subset (&instance->my_new_memb_list[i], 1,
			instance->my_trans_memb_list, instance->my_trans_memb_entries) &&

			commit_token->memb_list[i].received_flg == 0) {
#endif
			instance->my_deliver_memb_entries = instance->my_trans_memb_entries;
			memcpy (instance->my_deliver_memb_list, instance->my_trans_memb_list,
				sizeof (struct totem_ip_address) * instance->my_trans_memb_entries);
#ifdef COMPILE_OUT
			local_received_flg = 0;
			break;
		}
	}
#endif
//	if (local_received_flg == 0) {
		/*
		 * Calculate my_low_ring_aru, instance->my_high_ring_delivered for the transitional membership
		 */
		for (i = 0; i < commit_token->addr_entries; i++) {
			if (memb_set_subset (&instance->my_new_memb_list[i], 1,
				instance->my_deliver_memb_list,
				 instance->my_deliver_memb_entries) &&

			memcmp (&instance->my_old_ring_id,
				&commit_token->memb_list[i].ring_id,
				sizeof (struct memb_ring_id)) == 0) {
	
				if (low_ring_aru == 0 ||
					sq_lt_compare (commit_token->memb_list[i].aru, low_ring_aru)) {

					low_ring_aru = commit_token->memb_list[i].aru;
				}
				if (sq_lt_compare (instance->my_high_ring_delivered, commit_token->memb_list[i].high_delivered)) {
					instance->my_high_ring_delivered = commit_token->memb_list[i].high_delivered;
				}
			}
		}
		/*
		 * Copy all old ring messages to instance->retrans_message_queue
		 */
		instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
			"copying all old ring messages from %x-%x.\n",
			low_ring_aru + 1, instance->old_ring_state_high_seq_received);
		strcpy (not_originated, "Not Originated for recovery: ");
		strcpy (is_originated, "Originated for recovery: ");
			
		range = instance->old_ring_state_high_seq_received - low_ring_aru;
		assert (range < 1024);
		for (i = 1; i <= range; i++) {

			struct sort_queue_item *sort_queue_item;
			struct message_item message_item;
			void *ptr;
			int res;

			sprintf (seqno_string_hex, "%x ", low_ring_aru + i);
			res = sq_item_get (&instance->regular_sort_queue,
				low_ring_aru + i, &ptr);
			if (res != 0) {
				strcat (not_originated, seqno_string_hex);
				continue;
			}
			strcat (is_originated, seqno_string_hex);
			sort_queue_item = ptr;
			assert (sort_queue_item->iov_len > 0);
			assert (sort_queue_item->iov_len <= MAXIOVS);
			messages_originated++;
			memset (&message_item, 0, sizeof (struct message_item));
// TODO LEAK
			message_item.mcast = malloc (sizeof (struct mcast));
			assert (message_item.mcast);
			memcpy (message_item.mcast, sort_queue_item->iovec[0].iov_base,
				sizeof (struct mcast));
			memcpy (&message_item.mcast->ring_id, &instance->my_ring_id,
				sizeof (struct memb_ring_id));
			message_item.mcast->header.encapsulated = 1;
			message_item.mcast->header.nodeid = instance->my_id.nodeid;
			assert (message_item.mcast->header.nodeid);
			message_item.iov_len = sort_queue_item->iov_len;
			memcpy (&message_item.iovec, &sort_queue_item->iovec, sizeof (struct iovec) *
				sort_queue_item->iov_len);
			queue_item_add (&instance->retrans_message_queue, &message_item);
		}
		instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
			"Originated %d messages in RECOVERY.\n", messages_originated);
		strcat (not_originated, "\n");
		strcat (is_originated, "\n");
		instance->totemsrp_log_printf (instance->totemsrp_log_level_notice, is_originated);
		instance->totemsrp_log_printf (instance->totemsrp_log_level_notice, not_originated);
//	}

	instance->my_aru = SEQNO_START_MSG;
	instance->my_aru_count = 0;
	instance->my_seq_unchanged = 0;
	instance->my_high_seq_received = SEQNO_START_MSG;
	instance->my_install_seq = SEQNO_START_MSG;
	instance->last_released = SEQNO_START_MSG;

	reset_token_timeout (instance); // REVIEWED
	reset_token_retransmit_timeout (instance); // REVIEWED

	instance->memb_state = MEMB_STATE_RECOVERY;
	return;
}

int totemsrp_new_msg_signal (totemsrp_handle handle)
{
	struct totemsrp_instance *instance;
	SaErrorT error;

	error = saHandleInstanceGet (&totemsrp_instance_database, handle,
		(void *)&instance);
	if (error != SA_OK) {
		goto error_exit;
	}

	token_hold_cancel_send (instance);

	saHandleInstancePut (&totemsrp_instance_database, handle);
	return (0);
error_exit:
	return (-1);
}

int totemsrp_mcast (
	totemsrp_handle handle,
	struct iovec *iovec,
	int iov_len,
	int guarantee)
{
	int i;
	int j;
	struct message_item message_item;
	struct totemsrp_instance *instance;
	SaErrorT error;

	error = saHandleInstanceGet (&totemsrp_instance_database, handle,
		(void *)&instance);
	if (error != SA_OK) {
		goto error_exit;
	}
	
	if (queue_is_full (&instance->new_message_queue)) {
		return (-1);
	}
	for (j = 0, i = 0; i < iov_len; i++) {
		j+= iovec[i].iov_len;
	}

	memset (&message_item, 0, sizeof (struct message_item));

	/*
	 * Allocate pending item
	 */
// TODO LEAK
	message_item.mcast = malloc (sizeof (struct mcast));
	if (message_item.mcast == 0) {
		goto error_mcast;
	}

	/*
	 * Set mcast header
	 */
	message_item.mcast->header.type = MESSAGE_TYPE_MCAST;
	message_item.mcast->header.endian_detector = ENDIAN_LOCAL;
	message_item.mcast->header.encapsulated = 2;
	message_item.mcast->header.nodeid = instance->my_id.nodeid;
	assert (message_item.mcast->header.nodeid);

	message_item.mcast->guarantee = guarantee;
	totemip_copy(&message_item.mcast->source, &instance->my_id);

	for (i = 0; i < iov_len; i++) {
// TODO LEAK
		message_item.iovec[i].iov_base = malloc (iovec[i].iov_len);

		if (message_item.iovec[i].iov_base == 0) {
			goto error_iovec;
		}

		memcpy (message_item.iovec[i].iov_base, iovec[i].iov_base,
			iovec[i].iov_len);

		message_item.iovec[i].iov_len = iovec[i].iov_len;
	}

	message_item.iov_len = iov_len;

	instance->totemsrp_log_printf (instance->totemsrp_log_level_debug, "mcasted message added to pending queue\n");
	queue_item_add (&instance->new_message_queue, &message_item);

	saHandleInstancePut (&totemsrp_instance_database, handle);
	return (0);

error_iovec:
	saHandleInstancePut (&totemsrp_instance_database, handle);
	for (j = 0; j < i; j++) {
		free (message_item.iovec[j].iov_base);
	}
	return (-1);

error_mcast:
	saHandleInstancePut (&totemsrp_instance_database, handle);

error_exit:
	return (0);
}

/*
 * Determine if there is room to queue a new message
 */
int totemsrp_avail (totemsrp_handle handle)
{
	int avail;
	struct totemsrp_instance *instance;
	SaErrorT error;

	error = saHandleInstanceGet (&totemsrp_instance_database, handle,
		(void *)&instance);
	if (error != SA_OK) {
		goto error_exit;
	}

	queue_avail (&instance->new_message_queue, &avail);

	saHandleInstancePut (&totemsrp_instance_database, handle);

	return (avail);

error_exit:
	return (0);
}

/*
 * ORF Token Management
 */
/* 
 * Recast message to mcast group if it is available
 */
static int orf_token_remcast (
	struct totemsrp_instance *instance,
	int seq)
{
	struct sort_queue_item *sort_queue_item;
	int res;
	void *ptr;

	struct sq *sort_queue;

	if (instance->memb_state == MEMB_STATE_RECOVERY) {
		sort_queue = &instance->recovery_sort_queue;
	} else {
		sort_queue = &instance->regular_sort_queue;
	}

	res = sq_in_range (sort_queue, seq);
	if (res == 0) {
printf ("sq not in range\n");
		return (-1);
	}
	
	/*
	 * Get RTR item at seq, if not available, return
	 */
	res = sq_item_get (sort_queue, seq, &ptr);
	if (res != 0) {
		return -1;
	}

	sort_queue_item = ptr;

	totemrrp_mcast_noflush_send (instance->totemrrp_handle,
		sort_queue_item->iovec,
		sort_queue_item->iov_len);

	return (0);
}


/*
 * Free all freeable messages from ring
 */
static void messages_free (
	struct totemsrp_instance *instance,
	unsigned int token_aru)
{
	struct sort_queue_item *regular_message;
	unsigned int i, j;
	int res;
	int log_release = 0;
	unsigned int release_to;
	unsigned int range = 0;

//printf ("aru %x last aru %x my high delivered %x last releaed %x\n",
//		token_aru, instance->my_last_aru, instance->my_high_delivered, instance->last_released);

	release_to = token_aru;
	if (sq_lt_compare (instance->my_last_aru, release_to)) {
		release_to = instance->my_last_aru;
	}
	if (sq_lt_compare (instance->my_high_delivered, release_to)) {
		release_to = instance->my_high_delivered;
	}

	/*
	 * Ensure we dont try release before an already released point
	 */
	if (sq_lt_compare (release_to, instance->last_released)) {
		return;
	}

	range = release_to - instance->last_released;
	assert (range < 1024);

	/*
	 * Release retransmit list items if group aru indicates they are transmitted
	 */
	for (i = 1; i <= range; i++) {
		void *ptr;

		res = sq_item_get (&instance->regular_sort_queue,
			instance->last_released + i, &ptr);
		if (res == 0) {
			regular_message = ptr;
			for (j = 0; j < regular_message->iov_len; j++) {
				free (regular_message->iovec[j].iov_base);
			}
		}
		sq_items_release (&instance->regular_sort_queue,
			instance->last_released + i);

		log_release = 1;
	}
	instance->last_released += range;

 	if (log_release) {
		instance->totemsrp_log_printf (instance->totemsrp_log_level_debug,
			"releasing messages up to and including %x\n", release_to);
	}
}

static void update_aru (
	struct totemsrp_instance *instance)
{
	unsigned int i;
	int res;
	struct sq *sort_queue;
	unsigned int range;
	unsigned int my_aru_saved = 0;

	if (instance->memb_state == MEMB_STATE_RECOVERY) {
		sort_queue = &instance->recovery_sort_queue;
	} else {
		sort_queue = &instance->regular_sort_queue;
	}

	range = instance->my_high_seq_received - instance->my_aru;
	if (range > 1024) {
		return;
	}

	my_aru_saved = instance->my_aru;
	for (i = 1; i <= range; i++) {

		void *ptr;

		res = sq_item_get (sort_queue, my_aru_saved + i, &ptr);
		/*
		 * If hole, stop updating aru
		 */
		if (res != 0) {
			break;
		}
	}
	instance->my_aru += i - 1;


//	instance->totemsrp_log_printf (instance->totemsrp_log_level_debug,
//		"setting received flag to FALSE %d %d\n",
//		instance->my_aru, instance->my_high_seq_received);
	instance->my_received_flg = 0;
	if (instance->my_aru == instance->my_high_seq_received) {
//		instance->totemsrp_log_printf (instance->totemsrp_log_level_debug,
//			"setting received flag to TRUE %d %d\n",
//			instance->my_aru, instance->my_high_seq_received);
		instance->my_received_flg = 1;
	}
}

/*
 * Multicasts pending messages onto the ring (requires orf_token possession)
 */
static int orf_token_mcast (
	struct totemsrp_instance *instance,
	struct orf_token *token,
	int fcc_mcasts_allowed,
	struct totem_ip_address *system_from)
{
	struct message_item *message_item = 0;
	struct queue *mcast_queue;
	struct sq *sort_queue;
	struct sort_queue_item sort_queue_item;
	struct sort_queue_item *sort_queue_item_ptr;
	struct mcast *mcast;

	if (instance->memb_state == MEMB_STATE_RECOVERY) {
		mcast_queue = &instance->retrans_message_queue;
		sort_queue = &instance->recovery_sort_queue;
		reset_token_retransmit_timeout (instance); // REVIEWED
	} else {
		mcast_queue = &instance->new_message_queue;
		sort_queue = &instance->regular_sort_queue;
	}

	for (instance->fcc_mcast_current = 0; instance->fcc_mcast_current < fcc_mcasts_allowed; instance->fcc_mcast_current++) {
		if (queue_is_empty (mcast_queue)) {
			break;
		}
		message_item = (struct message_item *)queue_item_get (mcast_queue);
		/* preincrement required by algo */
		if (instance->old_ring_state_saved &&
			(instance->memb_state == MEMB_STATE_GATHER ||
			instance->memb_state == MEMB_STATE_COMMIT)) {

			instance->totemsrp_log_printf (instance->totemsrp_log_level_debug,
				"not multicasting at seqno is %d\n",
			token->seq);
			return (0);
		}

		message_item->mcast->seq = ++token->seq;
		message_item->mcast->this_seqno = instance->global_seqno++;

		/*
		 * Build IO vector
		 */
		memset (&sort_queue_item, 0, sizeof (struct sort_queue_item));
		sort_queue_item.iovec[0].iov_base = message_item->mcast;
		sort_queue_item.iovec[0].iov_len = sizeof (struct mcast);
	
		mcast = sort_queue_item.iovec[0].iov_base;
	
		memcpy (&sort_queue_item.iovec[1], message_item->iovec,
			message_item->iov_len * sizeof (struct iovec));

		memcpy (&mcast->ring_id, &instance->my_ring_id, sizeof (struct memb_ring_id));

		sort_queue_item.iov_len = message_item->iov_len + 1;

		assert (sort_queue_item.iov_len < 16);

		/*
		 * Add message to retransmit queue
		 */
		sort_queue_item_ptr = sq_item_add (sort_queue,
			&sort_queue_item, message_item->mcast->seq);

		totemrrp_mcast_noflush_send (instance->totemrrp_handle,
			sort_queue_item_ptr->iovec,
			sort_queue_item_ptr->iov_len);
		
		/*
		 * Delete item from pending queue
		 */
		queue_item_remove (mcast_queue);
	}


	assert (instance->fcc_mcast_current < 100);

	/*
	 * If messages mcasted, deliver any new messages to totempg
	 */
	instance->my_high_seq_received = token->seq;
		
	update_aru (instance);
	/*
	 * Return 1 if more messages are available for single node clusters
	 */
	return (instance->fcc_mcast_current);
}

/*
 * Remulticasts messages in orf_token's retransmit list (requires orf_token)
 * Modify's orf_token's rtr to include retransmits required by this process
 */
static int orf_token_rtr (
	struct totemsrp_instance *instance,
	struct orf_token *orf_token,
	int *fcc_allowed)
{
	unsigned int res;
	unsigned int i, j;
	unsigned int found;
	unsigned int total_entries;
	struct sq *sort_queue;
	struct rtr_item *rtr_list;
	unsigned int range = 0;
	char retransmit_msg[1024];
	char value[64];

	if (instance->memb_state == MEMB_STATE_RECOVERY) {
		sort_queue = &instance->recovery_sort_queue;
	} else {
		sort_queue = &instance->regular_sort_queue;
	}

	rtr_list = &orf_token->rtr_list[0];
	strcpy (retransmit_msg, "Retransmit List: ");
	if (orf_token->rtr_list_entries) {
		instance->totemsrp_log_printf (instance->totemsrp_log_level_debug,
			"Retransmit List %d\n", orf_token->rtr_list_entries);
		for (i = 0; i < orf_token->rtr_list_entries; i++) {
			sprintf (value, "%x ", rtr_list[i].seq);
			strcat (retransmit_msg, value);
		}
		strcat (retransmit_msg, "\n");
		instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
			"%s", retransmit_msg);
	}

	total_entries = orf_token->rtr_list_entries;

	/*
	 * Retransmit messages on orf_token's RTR list from RTR queue
	 */
	for (instance->fcc_remcast_current = 0, i = 0;
		instance->fcc_remcast_current <= *fcc_allowed && i < orf_token->rtr_list_entries;) {

		/*
		 * If this retransmit request isn't from this configuration,
		 * try next rtr entry
		 */
 		if (memcmp (&rtr_list[i].ring_id, &instance->my_ring_id,
			sizeof (struct memb_ring_id)) != 0) {

			i += 1;
			continue;
		}

		res = orf_token_remcast (instance, rtr_list[i].seq);
		if (res == 0) {
			/*
			 * Multicasted message, so no need to copy to new retransmit list
			 */
			orf_token->rtr_list_entries -= 1;
			assert (orf_token->rtr_list_entries >= 0);
			memmove (&rtr_list[i], &rtr_list[i + 1],
				sizeof (struct rtr_item) * (orf_token->rtr_list_entries));

			instance->fcc_remcast_current++;
		} else {
			i += 1;
		}
	}
	*fcc_allowed = *fcc_allowed - instance->fcc_remcast_current - 1;

	/*
	 * Add messages to retransmit to RTR list
	 * but only retry if there is room in the retransmit list
	 */
//printf ("high seq %x aru %x\n", instance->my_high_seq_received, instance->my_aru);
	range = instance->my_high_seq_received - instance->my_aru;
	assert (range < 100000);

	for (i = 1; (orf_token->rtr_list_entries < RETRANSMIT_ENTRIES_MAX) &&
		(i <= range); i++) {

		/*
		 * Ensure message is within the sort queue range
		 */
		res = sq_in_range (sort_queue, instance->my_aru + i);
		if (res == 0) {
			break;
		}

		/*
		 * Find if a message is missing from this processor
		 */
		res = sq_item_inuse (sort_queue, instance->my_aru + i);
		if (res == 0) {
			/*
			 * Determine if missing message is already in retransmit list
			 */
			found = 0;
			for (j = 0; j < orf_token->rtr_list_entries; j++) {
				if (instance->my_aru + i == rtr_list[j].seq) {
					found = 1;
				}
			}
			if (found == 0) {
				/*
				 * Missing message not found in current retransmit list so add it
				 */
				memcpy (&rtr_list[orf_token->rtr_list_entries].ring_id,
					&instance->my_ring_id, sizeof (struct memb_ring_id));
				rtr_list[orf_token->rtr_list_entries].seq = instance->my_aru + i;
				orf_token->rtr_list_entries++;
			}
		}
	}
	return (instance->fcc_remcast_current);
}

static void token_retransmit (struct totemsrp_instance *instance) {

	totemrrp_token_send (instance->totemrrp_handle,
		&instance->next_memb,
		instance->orf_token_retransmit,
		instance->orf_token_retransmit_size);
}

/*
 * Retransmit the regular token if no mcast or token has
 * been received in retransmit token period retransmit
 * the token to the next processor
 */
static void timer_function_token_retransmit_timeout (void *data)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)data;

	switch (instance->memb_state) {
	case MEMB_STATE_GATHER:
		break;
	case MEMB_STATE_COMMIT:
		break;
	case MEMB_STATE_OPERATIONAL:
	case MEMB_STATE_RECOVERY:
		token_retransmit (instance);
		reset_token_retransmit_timeout (instance); // REVIEWED
		break;
	}
}

static void timer_function_token_hold_retransmit_timeout (void *data)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)data;

	switch (instance->memb_state) {
	case MEMB_STATE_GATHER:
		break;
	case MEMB_STATE_COMMIT:
		break;
	case MEMB_STATE_OPERATIONAL:
	case MEMB_STATE_RECOVERY:
		token_retransmit (instance);
		break;
	}
}

static void timer_function_merge_detect_timeout(void *data)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)data;

	instance->my_merge_detect_timeout_outstanding = 0;

	switch (instance->memb_state) {
	case MEMB_STATE_OPERATIONAL:
		if (totemip_equal(&instance->my_ring_id.rep, &instance->my_id)) {
			memb_merge_detect_transmit (instance);
		}
		break;
	case MEMB_STATE_GATHER:
	case MEMB_STATE_COMMIT:
	case MEMB_STATE_RECOVERY:
		break;
	}
}

/*
 * Send orf_token to next member (requires orf_token)
 */
static int token_send (
	struct totemsrp_instance *instance,
	struct orf_token *orf_token,
	int forward_token)
{
	int res = 0;
	int iov_len = sizeof (struct orf_token) +
		(orf_token->rtr_list_entries * sizeof (struct rtr_item));

	memcpy (instance->orf_token_retransmit, orf_token, iov_len);
	instance->orf_token_retransmit_size = iov_len;
	orf_token->header.nodeid = instance->my_id.nodeid;
	assert (orf_token->header.nodeid);

	if (forward_token == 0) {
		return (0);
	}

	totemrrp_token_send (instance->totemrrp_handle,
		&instance->next_memb,
		orf_token,
		iov_len);

	return (res);
}

static int token_hold_cancel_send (struct totemsrp_instance *instance)
{
	struct token_hold_cancel token_hold_cancel;

	/*
	 * Only cancel if the token is currently held
	 */
	if (instance->my_token_held == 0) {
		return (0);
	}
	instance->my_token_held = 0;

	/*
	 * Build message
	 */
	token_hold_cancel.header.type = MESSAGE_TYPE_TOKEN_HOLD_CANCEL;
	token_hold_cancel.header.endian_detector = ENDIAN_LOCAL;
	token_hold_cancel.header.nodeid = instance->my_id.nodeid;
	assert (token_hold_cancel.header.nodeid);

	memcpy (&token_hold_cancel.ring_id, &instance->my_ring_id,
		sizeof (struct memb_ring_id));

	totemrrp_mcast_flush_send (
		instance->totemrrp_handle,
		&token_hold_cancel,
		sizeof (struct token_hold_cancel));

	return (0);
}
//AAA

static int orf_token_send_initial (struct totemsrp_instance *instance)
{
	struct orf_token orf_token;
	int res;

	orf_token.header.type = MESSAGE_TYPE_ORF_TOKEN;
	orf_token.header.endian_detector = ENDIAN_LOCAL;
	orf_token.header.encapsulated = 0;
	orf_token.header.nodeid = instance->my_id.nodeid;
	assert (orf_token.header.nodeid);
	orf_token.seq = 0;
	orf_token.seq = SEQNO_START_MSG;
	orf_token.token_seq = SEQNO_START_TOKEN;
	orf_token.retrans_flg = 1;
	instance->my_set_retrans_flg = 1;
/*
	if (queue_is_empty (&instance->retrans_message_queue) == 1) {
		orf_token.retrans_flg = 0;
	} else {
		orf_token.retrans_flg = 1;
		instance->my_set_retrans_flg = 1;
	}
*/
		
	orf_token.aru = 0;
	orf_token.aru = SEQNO_START_MSG - 1;
	totemip_copy(&orf_token.aru_addr, &instance->my_id);

	memcpy (&orf_token.ring_id, &instance->my_ring_id, sizeof (struct memb_ring_id));
	orf_token.fcc = 0;

	orf_token.rtr_list_entries = 0;

	res = token_send (instance, &orf_token, 1);

	return (res);
}

static void memb_state_commit_token_update (
	struct totemsrp_instance *instance,
	struct memb_commit_token *memb_commit_token)
{
	int memb_index_this;

	memb_index_this = (memb_commit_token->memb_index + 1) % memb_commit_token->addr_entries;
	memcpy (&memb_commit_token->memb_list[memb_index_this].ring_id,
		&instance->my_old_ring_id, sizeof (struct memb_ring_id));
	assert (!totemip_zero_check(&instance->my_old_ring_id.rep));

	memb_commit_token->memb_list[memb_index_this].aru = instance->old_ring_state_aru;
	/*
	 *  TODO high delivered is really instance->my_aru, but with safe this
	 * could change?
	 */
	memb_commit_token->memb_list[memb_index_this].high_delivered = instance->my_high_delivered;
	memb_commit_token->memb_list[memb_index_this].received_flg = instance->my_received_flg;

	memb_commit_token->header.nodeid = instance->my_id.nodeid;
	assert (memb_commit_token->header.nodeid);
}

static int memb_state_commit_token_send (struct totemsrp_instance *instance,
	struct memb_commit_token *memb_commit_token)
{
	struct iovec iovec;
	int memb_index_this;
	int memb_index_next;

	memb_commit_token->token_seq++;
	memb_index_this = (memb_commit_token->memb_index + 1) % memb_commit_token->addr_entries;
	memb_index_next = (memb_index_this + 1) % memb_commit_token->addr_entries;
	memb_commit_token->memb_index = memb_index_this;


	iovec.iov_base = memb_commit_token;
	iovec.iov_len = sizeof (struct memb_commit_token);

	totemip_copy(&instance->next_memb, &memb_commit_token->addr[memb_index_next]);
	assert (instance->next_memb.nodeid != 0);

	totemrrp_token_send (instance->totemrrp_handle,
		&instance->next_memb,
		memb_commit_token,
		sizeof (struct memb_commit_token));

	return (0);
}


static int memb_lowest_in_config (struct totemsrp_instance *instance)
{
	struct totem_ip_address token_memb[PROCESSOR_COUNT_MAX];
	int token_memb_entries = 0;
	struct totem_ip_address lowest_addr;
	int i;

	memset(&lowest_addr, 0xff, sizeof(lowest_addr));
	lowest_addr.family = instance->mcast_address.family;

	memb_set_subtract (token_memb, &token_memb_entries,
		instance->my_proc_list, instance->my_proc_list_entries,
		instance->my_failed_list, instance->my_failed_list_entries);

	/*
	 * find representative by searching for smallest identifier
	 */
	for (i = 0; i < token_memb_entries; i++) {
		if (totemip_compare(&lowest_addr, &token_memb[i]) > 0) {
			totemip_copy(&lowest_addr, &token_memb[i]);
		}
	}
	return (totemip_equal(&instance->my_id, &lowest_addr));
}


static void memb_state_commit_token_create (
	struct totemsrp_instance *instance,
	struct memb_commit_token *commit_token)
{
	struct totem_ip_address token_memb[PROCESSOR_COUNT_MAX];
	int token_memb_entries = 0;

	instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
		"Creating commit token because I am the rep.\n");

	memb_set_subtract (token_memb, &token_memb_entries,
		instance->my_proc_list, instance->my_proc_list_entries,
		instance->my_failed_list, instance->my_failed_list_entries);

	memset (commit_token, 0, sizeof (struct memb_commit_token));
	commit_token->header.type = MESSAGE_TYPE_MEMB_COMMIT_TOKEN;
	commit_token->header.endian_detector = ENDIAN_LOCAL;
	commit_token->header.encapsulated = 0;
	commit_token->header.nodeid = instance->my_id.nodeid;
	assert (commit_token->header.nodeid);

	totemip_copy(&commit_token->ring_id.rep, &instance->my_id);

	commit_token->ring_id.seq = instance->token_ring_id_seq + 4;
	qsort (token_memb, token_memb_entries, 
		sizeof (struct totem_ip_address), totemip_compare);
	memcpy (commit_token->addr, token_memb,
		token_memb_entries * sizeof (struct totem_ip_address));
	memset (commit_token->memb_list, 0,
		sizeof (struct memb_commit_token_memb_entry) * PROCESSOR_COUNT_MAX);
	commit_token->memb_index = token_memb_entries - 1;
	commit_token->addr_entries = token_memb_entries;
}

static void memb_join_message_send (struct totemsrp_instance *instance)
{
	struct memb_join memb_join;

	memb_join.header.type = MESSAGE_TYPE_MEMB_JOIN;
	memb_join.header.endian_detector = ENDIAN_LOCAL;
	memb_join.header.encapsulated = 0;
	memb_join.header.nodeid = instance->my_id.nodeid;
	assert (memb_join.header.nodeid);

	memb_join.ring_seq = instance->my_ring_id.seq;

	memcpy (memb_join.proc_list, instance->my_proc_list,
		instance->my_proc_list_entries * sizeof (struct totem_ip_address));
	memb_join.proc_list_entries = instance->my_proc_list_entries;

	memcpy (memb_join.failed_list, instance->my_failed_list,
		instance->my_failed_list_entries * sizeof (struct totem_ip_address));
	memb_join.failed_list_entries = instance->my_failed_list_entries;
		
	totemrrp_mcast_flush_send (
		instance->totemrrp_handle,
		&memb_join,
		sizeof (struct memb_join));
}

static void memb_merge_detect_transmit (struct totemsrp_instance *instance) 
{
	struct memb_merge_detect memb_merge_detect;

	memb_merge_detect.header.type = MESSAGE_TYPE_MEMB_MERGE_DETECT;
	memb_merge_detect.header.endian_detector = ENDIAN_LOCAL;
	memb_merge_detect.header.encapsulated = 0;
	memb_merge_detect.header.nodeid = instance->my_id.nodeid;
	assert (memb_merge_detect.header.nodeid);

	memcpy (&memb_merge_detect.ring_id, &instance->my_ring_id,
		sizeof (struct memb_ring_id));

	totemrrp_mcast_flush_send (
		instance->totemrrp_handle,
		&memb_merge_detect,
		sizeof (struct memb_merge_detect));
}

static void memb_ring_id_create_or_load (
	struct totemsrp_instance *instance,
	struct memb_ring_id *memb_ring_id)
{
	int fd;
	int res;
	char filename[256];

	sprintf (filename, "/tmp/ringid_%s",
		totemip_print (&instance->my_id));
	fd = open (filename, O_RDONLY, 0777);
	if (fd > 0) {
		res = read (fd, &memb_ring_id->seq, sizeof (unsigned long long));
		assert (res == sizeof (unsigned long long));
		close (fd);
	} else
	if (fd == -1 && errno == ENOENT) {
		memb_ring_id->seq = 0;
		umask(0);
		fd = open (filename, O_CREAT|O_RDWR, 0777);
		if (fd == -1) {
			printf ("couldn't create file %d %s\n", fd, strerror(errno));
		}
		res = write (fd, &memb_ring_id->seq, sizeof (unsigned long long));
		assert (res == sizeof (unsigned long long));
		close (fd);
	} else {
		instance->totemsrp_log_printf (instance->totemsrp_log_level_warning,
			"Couldn't open %s %s\n", filename, strerror (errno));
	}
	
	totemip_copy(&memb_ring_id->rep, &instance->my_id);
	assert (!totemip_zero_check(&memb_ring_id->rep));
	instance->token_ring_id_seq = memb_ring_id->seq;
}

static void memb_ring_id_store (
	struct totemsrp_instance *instance,
	struct memb_commit_token *commit_token)
{
	char filename[256];
	int fd;
	int res;

	sprintf (filename, "/tmp/ringid_%s",
		totemip_print (&instance->my_id));

	fd = open (filename, O_WRONLY, 0777);
	if (fd == -1) {
		fd = open (filename, O_CREAT|O_RDWR, 0777);
	}
	if (fd == -1) {
		instance->totemsrp_log_printf (instance->totemsrp_log_level_warning,
			"Couldn't store new ring id %llx to stable storage (%s)\n",
				commit_token->ring_id.seq, strerror (errno));
		assert (0);
		return;
	}
	instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
		"Storing new sequence id for ring %d\n", commit_token->ring_id.seq);
	assert (fd > 0);
	res = write (fd, &commit_token->ring_id.seq, sizeof (unsigned long long));
	assert (res == sizeof (unsigned long long));
	close (fd);
	memcpy (&instance->my_ring_id, &commit_token->ring_id, sizeof (struct memb_ring_id));
	instance->token_ring_id_seq = instance->my_ring_id.seq;
}

int totemsrp_callback_token_create (
	totemsrp_handle handle,
	void **handle_out,
	enum totem_callback_token_type type,
	int delete,
	int (*callback_fn) (enum totem_callback_token_type type, void *),
	void *data)
{
	struct token_callback_instance *callback_handle;
	struct totemsrp_instance *instance;
	SaErrorT error;

	error = saHandleInstanceGet (&totemsrp_instance_database, handle,
		(void *)&instance);
	if (error != SA_OK) {
		goto error_exit;
	}

	callback_handle = (struct token_callback_instance *)malloc (sizeof (struct token_callback_instance));
	if (callback_handle == 0) {
		return (-1);
	}
	*handle_out = (void *)callback_handle;
	list_init (&callback_handle->list);
	callback_handle->callback_fn = callback_fn;
	callback_handle->data = data;
	callback_handle->callback_type = type;
	callback_handle->delete = delete;
	switch (type) {
	case TOTEM_CALLBACK_TOKEN_RECEIVED:
		list_add (&callback_handle->list, &instance->token_callback_received_listhead);
		break;
	case TOTEM_CALLBACK_TOKEN_SENT:
		list_add (&callback_handle->list, &instance->token_callback_sent_listhead);
		break;
	}

	saHandleInstancePut (&totemsrp_instance_database, handle);

error_exit:
	return (0);
}

void totemsrp_callback_token_destroy (totemsrp_handle handle, void **handle_out)
{
	struct token_callback_instance *h;

	if (*handle_out) {
 		h = (struct token_callback_instance *)*handle_out;
		list_del (&h->list);
		free (h);
		h = NULL;
		*handle_out = 0;
	}
}

void totem_callback_token_type (struct totemsrp_instance *instance, void *handle)
{
	struct token_callback_instance *token_callback_instance = (struct token_callback_instance *)handle;

	list_del (&token_callback_instance->list);
	free (token_callback_instance);
}

static void token_callbacks_execute (
	struct totemsrp_instance *instance,
	enum totem_callback_token_type type)
{
	struct list_head *list;
	struct list_head *list_next;
	struct list_head *callback_listhead = 0;
	struct token_callback_instance *token_callback_instance;
	int res;
	int del;

	switch (type) {
	case TOTEM_CALLBACK_TOKEN_RECEIVED:
		callback_listhead = &instance->token_callback_received_listhead;
		break;
	case TOTEM_CALLBACK_TOKEN_SENT:
		callback_listhead = &instance->token_callback_sent_listhead;
		break;
	default:
		assert (0);
	}
	
	for (list = callback_listhead->next; list != callback_listhead;
		list = list_next) {

		token_callback_instance = list_entry (list, struct token_callback_instance, list);

		list_next = list->next;
		del = token_callback_instance->delete;
		if (del == 1) {
			list_del (list);
		}

		res = token_callback_instance->callback_fn (
			token_callback_instance->callback_type,
			token_callback_instance->data);
		/*
		 * This callback failed to execute, try it again on the next token
		 */
		if (res == -1 && del == 1) {
			list_add (list, callback_listhead);
		} else	if (del) {
			free (token_callback_instance);
		}
	}
}

/*
 * Message Handlers
 */

/*
 * message handler called when TOKEN message type received
 */
static int message_handler_orf_token (
	struct totemsrp_instance *instance,
	struct totem_ip_address *system_from,
	void *msg,
	int msg_len,
	int endian_conversion_needed)
{
	char token_storage[1500];
	char token_convert[1500];
	struct orf_token *token;
	struct orf_token *token_ref = (struct orf_token *)msg;
	int transmits_allowed;
	int forward_token;
	int mcasted;
	unsigned int last_aru;
	unsigned int low_water;

#ifdef GIVEINFO
	struct timeval tv_current;
	struct timeval tv_diff;

gettimeofday (&tv_current, NULL);
timersub (&tv_current, &tv_old, &tv_diff);
memcpy (&tv_old, &tv_current, sizeof (struct timeval));

if ((((float)tv_diff.tv_usec) / 100.0) > 5.0) {
printf ("OTHERS %0.4f ms\n", ((float)tv_diff.tv_usec) / 100.0);
}
#endif

#ifdef RANDOM_DROP
if (random()%100 < 10) {
	return (0);
}
#endif

	/*
	 * Handle merge detection timeout
	 */
	if (token_ref->seq == instance->my_last_seq) {
		start_merge_detect_timeout (instance);
		instance->my_seq_unchanged += 1;
	} else {
		cancel_merge_detect_timeout (instance);
		cancel_token_hold_retransmit_timeout (instance);
		instance->my_seq_unchanged = 0;
	}

	instance->my_last_seq = token_ref->seq;

	/*
	 * Make copy of token and retransmit list in case we have
	 * to flush incoming messages from the kernel queue
	 */
	token = (struct orf_token *)token_storage;
	memcpy (token, msg, sizeof (struct orf_token));
	memcpy (&token->rtr_list[0], msg + sizeof (struct orf_token),
		sizeof (struct rtr_item) * RETRANSMIT_ENTRIES_MAX);

	if (endian_conversion_needed) {
		orf_token_endian_convert (token, (struct orf_token *)token_convert);
		token = (struct orf_token *)token_convert;
	}

	totemrrp_recv_flush (instance->totemrrp_handle);

	/*
	 * Determine if we should hold (in reality drop) the token
	 */
	instance->my_token_held = 0;
	if (totemip_equal(&instance->my_ring_id.rep, &instance->my_id) &&
		instance->my_seq_unchanged > instance->totem_config->seqno_unchanged_const) {
		instance->my_token_held = 1;
	} else
		if (!totemip_equal(&instance->my_ring_id.rep,  &instance->my_id) &&
		instance->my_seq_unchanged >= instance->totem_config->seqno_unchanged_const) {
		instance->my_token_held = 1;
	}

	/*
	 * Hold onto token when there is no activity on ring and
	 * this processor is the ring rep
	 */
	forward_token = 1;
	if (totemip_equal(&instance->my_ring_id.rep, &instance->my_id)) {
		if (instance->my_token_held) {
			forward_token = 0;			
		}
	}

	token_callbacks_execute (instance, TOTEM_CALLBACK_TOKEN_RECEIVED);

	switch (instance->memb_state) {
	case MEMB_STATE_COMMIT:
		 /* Discard token */
		break;

	case MEMB_STATE_OPERATIONAL:
		messages_free (instance, token->aru);
	case MEMB_STATE_GATHER:
		/*
		 * DO NOT add break, we use different free mechanism in recovery state
		 */

	case MEMB_STATE_RECOVERY:
		last_aru = instance->my_last_aru;
		instance->my_last_aru = token->aru;

		/*
		 * Discard tokens from another configuration
		 */
		if (memcmp (&token->ring_id, &instance->my_ring_id,
			sizeof (struct memb_ring_id)) != 0) {

			if ((forward_token)
				&& instance->use_heartbeat) {
				reset_heartbeat_timeout(instance);
			} 
			else {
				cancel_heartbeat_timeout(instance);
			}

			return (0); /* discard token */
		}

		/*
		 * Discard retransmitted tokens
		 */
		if (sq_lte_compare (token->token_seq, instance->my_token_seq)) {
			/*
			 * If this processor receives a retransmitted token, it is sure
		 	 * the previous processor is still alive.  As a result, it can
			 * reset its token timeout.  If some processor previous to that
			 * has failed, it will eventually not execute a reset of the
			 * token timeout, and will cause a reconfiguration to occur.
			 */
			reset_token_timeout (instance);

			if ((forward_token)
				&& instance->use_heartbeat) {
				reset_heartbeat_timeout(instance);
			}
			else {
				cancel_heartbeat_timeout(instance);
			}

			return (0); /* discard token */
		}		
		transmits_allowed = TRANSMITS_ALLOWED;
		mcasted = orf_token_rtr (instance, token, &transmits_allowed);

		if (sq_lt_compare (instance->last_released + MISSING_MCAST_WINDOW, token->seq + TRANSMITS_ALLOWED)) {
			transmits_allowed = 0;
printf ("zero \n");
		}
		mcasted = orf_token_mcast (instance, token, transmits_allowed, system_from);
		if (sq_lt_compare (instance->my_aru, token->aru) ||
			totemip_equal(&instance->my_id, &token->aru_addr) ||
			totemip_zero_check(&token->aru_addr)) {
			
			token->aru = instance->my_aru;
			if (token->aru == token->seq) {
				totemip_zero_set(&token->aru_addr);
			} else {
				totemip_copy(&token->aru_addr, &instance->my_id);
			}
		}
		if (token->aru == last_aru && !totemip_zero_check(&token->aru_addr)) {
			instance->my_aru_count += 1;
		} else {
			instance->my_aru_count = 0;
		}

		if (instance->my_aru_count > instance->totem_config->fail_to_recv_const &&
			!totemip_equal(&token->aru_addr, &instance->my_id)) {
			
printf ("FAILED TO RECEIVE\n");
// TODO if we fail to receive, it may be possible to end with a gather
// state of proc == failed = 0 entries
			memb_set_merge (&token->aru_addr, 1,
				instance->my_failed_list,
				&instance->my_failed_list_entries);

			ring_state_restore (instance);

			memb_state_gather_enter (instance);
		} else {
			instance->my_token_seq = token->token_seq;
			token->token_seq += 1;

			if (instance->memb_state == MEMB_STATE_RECOVERY) {
				/*
				 * instance->my_aru == instance->my_high_seq_received means this processor
				 * has recovered all messages it can recover
				 * (ie: its retrans queue is empty)
				 */
				low_water = instance->my_aru;
				if (sq_lt_compare (last_aru, low_water)) {
					low_water = last_aru;
				}
// TODO is this code right
				if (queue_is_empty (&instance->retrans_message_queue) == 0 ||
					low_water != instance->my_high_seq_received) {

					if (token->retrans_flg == 0) {
						token->retrans_flg = 1;
						instance->my_set_retrans_flg = 1;
					}
				} else
				if (token->retrans_flg == 1 && instance->my_set_retrans_flg) {
					token->retrans_flg = 0;
				}
				instance->totemsrp_log_printf (instance->totemsrp_log_level_debug,
					"token retrans flag is %d my set retrans flag%d retrans queue empty %d count %d, low_water %x aru %x\n", 
					token->retrans_flg, instance->my_set_retrans_flg,
					queue_is_empty (&instance->retrans_message_queue),
					instance->my_retrans_flg_count, low_water, token->aru);
				if (token->retrans_flg == 0) { 
					instance->my_retrans_flg_count += 1;
				} else {
					instance->my_retrans_flg_count = 0;
				}
				if (instance->my_retrans_flg_count == 2) {
					instance->my_install_seq = token->seq;
				}
				instance->totemsrp_log_printf (instance->totemsrp_log_level_debug,
					"install seq %x aru %x high seq received %x\n",
					instance->my_install_seq, instance->my_aru, instance->my_high_seq_received);
				if (instance->my_retrans_flg_count >= 2 && instance->my_aru >= instance->my_install_seq && instance->my_received_flg == 0) {
					instance->my_received_flg = 1;
					instance->my_deliver_memb_entries = instance->my_trans_memb_entries;
					memcpy (instance->my_deliver_memb_list, instance->my_trans_memb_list,
						sizeof (struct totem_ip_address) * instance->my_trans_memb_entries);
				}
				if (instance->my_retrans_flg_count >= 3 && token->aru >= instance->my_install_seq) {
					instance->my_rotation_counter += 1;
				} else {
					instance->my_rotation_counter = 0;
				}
				if (instance->my_rotation_counter == 2) {
				instance->totemsrp_log_printf (instance->totemsrp_log_level_debug,
					"retrans flag count %x token aru %x install seq %x aru %x %x\n",
					instance->my_retrans_flg_count, token->aru, instance->my_install_seq,
					instance->my_aru, token->seq);

					memb_state_operational_enter (instance);
					instance->my_rotation_counter = 0;
					instance->my_retrans_flg_count = 0;
				}
			}
	
			totemrrp_send_flush (instance->totemrrp_handle);
			token_send (instance, token, forward_token); 

#ifdef GIVEINFO
gettimeofday (&tv_current, NULL);
timersub (&tv_current, &tv_old, &tv_diff);
memcpy (&tv_old, &tv_current, sizeof (struct timeval));
if ((((float)tv_diff.tv_usec) / 100.0) > 5.0) {
printf ("I held %0.4f ms\n", ((float)tv_diff.tv_usec) / 100.0);
}
#endif
			if (instance->memb_state == MEMB_STATE_OPERATIONAL) {
				messages_deliver_to_app (instance, 0,
					instance->my_high_seq_received);
			}

			/*
			 * Deliver messages after token has been transmitted
			 * to improve performance
			 */
			reset_token_timeout (instance); // REVIEWED
			reset_token_retransmit_timeout (instance); // REVIEWED
			if (totemip_equal(&instance->my_id, &instance->my_ring_id.rep) &&
				instance->my_token_held == 1) {

				start_token_hold_retransmit_timeout (instance);
			}

			token_callbacks_execute (instance, TOTEM_CALLBACK_TOKEN_SENT);
		}
		break;
	}

	if ((forward_token)
		&& instance->use_heartbeat) {
		reset_heartbeat_timeout(instance);
	}
	else {
		cancel_heartbeat_timeout(instance);
	}

	return (0);
}

static void messages_deliver_to_app (
	struct totemsrp_instance *instance,
	int skip,
	unsigned int end_point)
{
	struct sort_queue_item *sort_queue_item_p;
	unsigned int i;
	int res;
	struct mcast *mcast;
	unsigned int range = 0;
	unsigned int my_high_delivered_stored = 0;

	instance->totemsrp_log_printf (instance->totemsrp_log_level_debug,
		"Delivering %x to %x\n", instance->my_high_delivered,
		end_point);

	range = end_point - instance->my_high_delivered;

	assert (range < 10240);
	my_high_delivered_stored = instance->my_high_delivered;

	/*
	 * Deliver messages in order from rtr queue to pending delivery queue
	 */
	for (i = 1; i <= range; i++) {

		void *ptr = 0;

		/*
		 * If out of range of sort queue, stop assembly
		 */
		res = sq_in_range (&instance->regular_sort_queue,
			my_high_delivered_stored + i);
		if (res == 0) {
			break;
		}

		res = sq_item_get (&instance->regular_sort_queue,
			my_high_delivered_stored + i, &ptr);
		/*
		 * If hole, stop assembly
		 */
		if (res != 0 && skip == 0) {
			break;
		}

		instance->my_high_delivered = my_high_delivered_stored + i;

		if (res != 0) {
			continue;

		}

		sort_queue_item_p = ptr;

		mcast = sort_queue_item_p->iovec[0].iov_base;
		assert (mcast != (struct mcast *)0xdeadbeef);

		/*
		 * Skip messages not originated in instance->my_deliver_memb
		 */
		if (skip &&
			memb_set_subset (&mcast->source,
				1,
				instance->my_deliver_memb_list,
				instance->my_deliver_memb_entries) == 0) {
		instance->my_high_delivered = my_high_delivered_stored + i;

			continue;
		}

		/*
		 * Message found
		 */
		instance->totemsrp_log_printf (instance->totemsrp_log_level_debug,
			"Delivering MCAST message with seq %x to pending delivery queue\n",
			mcast->seq);

		/*
		 * Message is locally originated multicast
		 */
	 	if (sort_queue_item_p->iov_len > 1 &&
			sort_queue_item_p->iovec[0].iov_len == sizeof (struct mcast)) {
			instance->totemsrp_deliver_fn (
				&mcast->source,
				&sort_queue_item_p->iovec[1],
				sort_queue_item_p->iov_len - 1,
				mcast->header.endian_detector != ENDIAN_LOCAL);
		} else {
			sort_queue_item_p->iovec[0].iov_len -= sizeof (struct mcast);
			sort_queue_item_p->iovec[0].iov_base += sizeof (struct mcast);

			instance->totemsrp_deliver_fn (
				&mcast->source,
				sort_queue_item_p->iovec,
				sort_queue_item_p->iov_len,
				mcast->header.endian_detector != ENDIAN_LOCAL);

			sort_queue_item_p->iovec[0].iov_len += sizeof (struct mcast);
			sort_queue_item_p->iovec[0].iov_base -= sizeof (struct mcast);
		}
//TODO	instance->stats_delv += 1;
	}

	instance->my_received_flg = 0;
	if (instance->my_aru == instance->my_high_seq_received) {
//		instance->totemsrp_log_printf (instance->totemsrp_log_level_debug,
//			"setting received flag to TRUE %d %d\n",
//			instance->my_aru, instance->my_high_seq_received);
		instance->my_received_flg = 1;
	}
}

/*
 * recv message handler called when MCAST message type received
 */
static int message_handler_mcast (
	struct totemsrp_instance *instance,
	struct totem_ip_address *system_from,
	void *msg,
	int msg_len,
	int endian_conversion_needed)
{
	struct sort_queue_item sort_queue_item;
	struct sq *sort_queue;
	struct mcast mcast_header;
	

	if (endian_conversion_needed) {
		mcast_endian_convert (msg, &mcast_header);
	} else {
		memcpy (&mcast_header, msg, sizeof (struct mcast));
	}

/*
	if (mcast_header.header.encapsulated == 1) {
		sort_queue = &instance->recovery_sort_queue;
	} else {
		sort_queue = &instance->regular_sort_queue;
	}
*/
	if (instance->memb_state == MEMB_STATE_RECOVERY) {
		sort_queue = &instance->recovery_sort_queue;
	} else {
		sort_queue = &instance->regular_sort_queue;
	}
	assert (msg_len < FRAME_SIZE_MAX);
#ifdef RANDOM_DROP
if (random()%100 < 50) {
	return (0);
}
#endif
        if (!totemip_equal(system_from, &instance->my_id)) {
		cancel_token_retransmit_timeout (instance);
	}

	assert (system_from->nodeid != 0);
	/*
	 * If the message is foreign execute the switch below
	 */
	if (memcmp (&instance->my_ring_id, &mcast_header.ring_id,
		sizeof (struct memb_ring_id)) != 0) {

		switch (instance->memb_state) {
		case MEMB_STATE_OPERATIONAL:
			memb_set_merge (system_from, 1,
				instance->my_proc_list, &instance->my_proc_list_entries);
			memb_state_gather_enter (instance);
			break;

		case MEMB_STATE_GATHER:
			if (!memb_set_subset (system_from,
				1,
				instance->my_proc_list,
				instance->my_proc_list_entries)) {

				memb_set_merge (system_from, 1,
					instance->my_proc_list, &instance->my_proc_list_entries);
				memb_state_gather_enter (instance);
				return (0);
			}
			break;

		case MEMB_STATE_COMMIT:
			/* discard message */
			break;

		case MEMB_STATE_RECOVERY:
			/* discard message */
			break;
		}
		return (0);
	}

	instance->totemsrp_log_printf (instance->totemsrp_log_level_debug,
		"Received ringid(%s:%lld) seq %x\n",
		totemip_print (&mcast_header.ring_id.rep),
		mcast_header.ring_id.seq,
		mcast_header.seq);

	/*
	 * Add mcast message to rtr queue if not already in rtr queue
	 * otherwise free io vectors
	 */
	if (msg_len > 0 && msg_len < FRAME_SIZE_MAX &&
		sq_in_range (sort_queue, mcast_header.seq) && 
		sq_item_inuse (sort_queue, mcast_header.seq) == 0) {

		/*
		 * Allocate new multicast memory block
		 */
// TODO LEAK
		sort_queue_item.iovec[0].iov_base = malloc (msg_len);
		if (sort_queue_item.iovec[0].iov_base == 0) {
			return (-1); /* error here is corrected by the algorithm */
		}
		memcpy (sort_queue_item.iovec[0].iov_base, msg, msg_len);
		sort_queue_item.iovec[0].iov_len = msg_len;
		assert (sort_queue_item.iovec[0].iov_len > 0);
		assert (sort_queue_item.iovec[0].iov_len < FRAME_SIZE_MAX);
		sort_queue_item.iov_len = 1;
		
		if (sq_lt_compare (instance->my_high_seq_received,
			mcast_header.seq)) {
			instance->my_high_seq_received = mcast_header.seq;
		}

		sq_item_add (sort_queue, &sort_queue_item, mcast_header.seq);
	}

	if (instance->memb_state == MEMB_STATE_OPERATIONAL) {
		update_aru (instance);
		messages_deliver_to_app (instance, 0, instance->my_high_seq_received);
	}

/* TODO remove from retrans message queue for old ring in recovery state */
	return (0);
}

static int message_handler_memb_merge_detect (
	struct totemsrp_instance *instance,
	struct totem_ip_address *system_from,
	void *msg,
	int msg_len,
	int endian_conversion_needed)
{
	struct memb_merge_detect *memb_merge_detect = (struct memb_merge_detect *)msg;

	/*
	 * do nothing if this is a merge detect from this configuration
	 */
	if (memcmp (&instance->my_ring_id, &memb_merge_detect->ring_id,
		sizeof (struct memb_ring_id)) == 0) {

		return (0);
	}

	assert (system_from->nodeid != 0);
	/*
	 * Execute merge operation
	 */
	switch (instance->memb_state) {
	case MEMB_STATE_OPERATIONAL:
		memb_set_merge (system_from, 1,
			instance->my_proc_list, &instance->my_proc_list_entries);
		memb_state_gather_enter (instance);
		break;

	case MEMB_STATE_GATHER:
		if (!memb_set_subset (system_from,
			1,
			instance->my_proc_list,
			instance->my_proc_list_entries)) {

			memb_set_merge (system_from, 1,
				instance->my_proc_list, &instance->my_proc_list_entries);
			memb_state_gather_enter (instance);
			return (0);
		}
		break;

	case MEMB_STATE_COMMIT:
		/* do nothing in commit */
		break;

	case MEMB_STATE_RECOVERY:
		/* do nothing in recovery */
		break;
	}
	return (0);
}

static int memb_join_process (
	struct totemsrp_instance *instance,
	struct memb_join *memb_join,
	struct totem_ip_address *system_from)
{
	struct memb_commit_token my_commit_token;

	if (memb_set_equal (memb_join->proc_list,
		memb_join->proc_list_entries,
		instance->my_proc_list,
		instance->my_proc_list_entries) &&

	memb_set_equal (memb_join->failed_list,
		memb_join->failed_list_entries,
		instance->my_failed_list,
		instance->my_failed_list_entries)) {

		memb_consensus_set (instance, system_from);
	
		if (memb_consensus_agreed (instance) &&
			memb_lowest_in_config (instance)) {

			memb_state_commit_token_create (instance, &my_commit_token);
	
			memb_state_commit_enter (instance, &my_commit_token);
		} else {
			return (0);
		}
	} else
	if (memb_set_subset (memb_join->proc_list,
		memb_join->proc_list_entries,
		instance->my_proc_list,
		instance->my_proc_list_entries) &&

		memb_set_subset (memb_join->failed_list,
		memb_join->failed_list_entries,
		instance->my_failed_list,
		instance->my_failed_list_entries)) {

		return (0);
	} else
	if (memb_set_subset (system_from, 1,
		instance->my_failed_list, instance->my_failed_list_entries)) {

		return (0);
	} else {
		memb_set_merge (memb_join->proc_list,
			memb_join->proc_list_entries,
			instance->my_proc_list, &instance->my_proc_list_entries);

		if (memb_set_subset (&instance->my_id, 1,
			memb_join->failed_list, memb_join->failed_list_entries)) {

			memb_set_merge (system_from, 1,
				instance->my_failed_list, &instance->my_failed_list_entries);
		} else {
			memb_set_merge (memb_join->failed_list,
				memb_join->failed_list_entries,
				instance->my_failed_list, &instance->my_failed_list_entries);
		}
		memb_state_gather_enter (instance);
		return (1); /* gather entered */
	}
	return (0); /* gather not entered */
}

static void memb_join_endian_convert (struct memb_join *in, struct memb_join *out)
{
	int i;

	out->header.type = in->header.type;
	out->header.endian_detector = ENDIAN_LOCAL;
	out->header.nodeid = swab32 (in->header.nodeid);
	out->proc_list_entries = swab32 (in->proc_list_entries);
	out->failed_list_entries = swab32 (in->failed_list_entries);
	out->ring_seq = swab64 (in->ring_seq);
	for (i = 0; i < out->proc_list_entries; i++) {
		totemip_copy(&out->proc_list[i], &in->proc_list[i]);
		out->proc_list[i].family = swab16(out->proc_list[i].family);
	}
	for (i = 0; i < out->failed_list_entries; i++) {
		totemip_copy(&out->failed_list[i], &in->failed_list[i]);
		out->failed_list[i].family = swab16(out->failed_list[i].family);
	}
}

static void memb_commit_token_endian_convert (struct memb_commit_token *in, struct memb_commit_token *out)
{
	int i;

	out->header.type = in->header.type;
	out->header.endian_detector = ENDIAN_LOCAL;
	out->header.nodeid = swab32 (in->header.nodeid);
	out->token_seq = swab32 (in->token_seq);
	totemip_copy(&out->ring_id.rep, &in->ring_id.rep);
	out->ring_id.seq = swab64 (in->ring_id.seq);
	out->retrans_flg = swab32 (in->retrans_flg);
	out->memb_index = swab32 (in->memb_index);
	out->addr_entries = swab32 (in->addr_entries);
	for (i = 0; i < out->addr_entries; i++) {
		totemip_copy(&out->addr[i], &in->addr[i]);
		out->addr[i].family = swab16(in->addr[i].family);

		totemip_copy(&out->memb_list[i].ring_id.rep,
			     &in->memb_list[i].ring_id.rep);
		out->memb_list[i].ring_id.rep.family = swab16(in->memb_list[i].ring_id.rep.family);

		out->memb_list[i].ring_id.seq =
			swab64 (in->memb_list[i].ring_id.seq);
		out->memb_list[i].aru = swab32 (in->memb_list[i].aru);
		out->memb_list[i].high_delivered = swab32 (in->memb_list[i].high_delivered);
		out->memb_list[i].received_flg = swab32 (in->memb_list[i].received_flg);
	}
}

static void orf_token_endian_convert (struct orf_token *in, struct orf_token *out)
{
	int i;

	out->header.type = in->header.type;
	out->header.endian_detector = ENDIAN_LOCAL;
	out->header.nodeid = swab32 (in->header.nodeid);
	out->seq = swab32 (in->seq);
	out->token_seq = swab32 (in->token_seq);
	out->aru = swab32 (in->aru);
	totemip_copy(&out->ring_id.rep, &in->ring_id.rep);
	out->ring_id.rep.family = swab16(in->ring_id.rep.family);

	out->ring_id.seq = swab64 (in->ring_id.seq);
	out->fcc = swab32 (in->fcc);
	out->retrans_flg = swab32 (in->retrans_flg);
	out->rtr_list_entries = swab32 (in->rtr_list_entries);
	for (i = 0; i < out->rtr_list_entries; i++) {
		totemip_copy(&out->rtr_list[i].ring_id.rep, &in->rtr_list[i].ring_id.rep);
		out->rtr_list[i].ring_id.rep.family = swab16(in->rtr_list[i].ring_id.rep.family);
		out->rtr_list[i].ring_id.seq = swab64 (in->rtr_list[i].ring_id.seq);
		out->rtr_list[i].seq = swab32 (in->rtr_list[i].seq);
	}
}

static void mcast_endian_convert (struct mcast *in, struct mcast *out)
{
	out->header.type = in->header.type;
	out->header.endian_detector = ENDIAN_LOCAL;
	out->header.nodeid = swab32 (in->header.nodeid);
	out->seq = swab32 (in->seq);
	totemip_copy(&out->ring_id.rep, &in->ring_id.rep);
	out->ring_id.rep.family = swab16(in->ring_id.rep.family);
	out->ring_id.seq = swab64 (in->ring_id.seq);
	out->source = in->source;
	out->guarantee = in->guarantee;
}

static int message_handler_memb_join (
	struct totemsrp_instance *instance,
	struct totem_ip_address *system_from,
	void *msg,
	int msg_len,
	int endian_conversion_needed)
{
	struct memb_join *memb_join;
	struct memb_join memb_join_convert;

	int gather_entered;

	if (endian_conversion_needed) {
		memb_join = &memb_join_convert;
		memb_join_endian_convert (msg, &memb_join_convert);
	} else {
		memb_join = (struct memb_join *)msg;
	}

	memb_set_merge(memb_join->proc_list, memb_join->proc_list_entries,
		       instance->my_nodeid_lookup_list, &instance->my_nodeid_lookup_entries);

	assert (system_from->nodeid != 0);

	if (instance->token_ring_id_seq < memb_join->ring_seq) {
		instance->token_ring_id_seq = memb_join->ring_seq;
	}
	switch (instance->memb_state) {
		case MEMB_STATE_OPERATIONAL:
			gather_entered = memb_join_process (instance,
				memb_join, system_from);
			if (gather_entered == 0) {
				memb_state_gather_enter (instance);
			}
			break;

		case MEMB_STATE_GATHER:
			memb_join_process (instance, memb_join, system_from);
			break;
	
		case MEMB_STATE_COMMIT:
			if (memb_set_subset (system_from,
				1,
				instance->my_new_memb_list,
				instance->my_new_memb_entries) &&

				memb_join->ring_seq >= instance->my_ring_id.seq) {

				memb_join_process (instance, memb_join, system_from);
				memb_state_gather_enter (instance);
			}
			break;

		case MEMB_STATE_RECOVERY:
			if (memb_set_subset (system_from,
				1,
				instance->my_new_memb_list,
				instance->my_new_memb_entries) &&

				memb_join->ring_seq >= instance->my_ring_id.seq) {

				ring_state_restore (instance);

				memb_join_process (instance,memb_join,
					system_from);
				memb_state_gather_enter (instance);
			}
			break;
	}
	return (0);
}

static int message_handler_memb_commit_token (
	struct totemsrp_instance *instance,
	struct totem_ip_address *system_from,
	void *msg,
	int msg_len,
	int endian_conversion_needed)
{
	struct memb_commit_token memb_commit_token_convert;
	struct memb_commit_token *memb_commit_token;
	struct totem_ip_address sub[PROCESSOR_COUNT_MAX];
	int sub_entries;

	
	if (endian_conversion_needed) {
		memb_commit_token = &memb_commit_token_convert;
		memb_commit_token_endian_convert (msg, memb_commit_token);
	} else {
		memb_commit_token = (struct memb_commit_token *)msg;
	}
	
/* TODO do we need to check for a duplicate token?
	if (memb_commit_token->token_seq > 0 &&
		instance->my_token_seq >= memb_commit_token->token_seq) {

		printf ("already received commit token %d %d\n",
			memb_commit_token->token_seq, instance->my_token_seq);
		return (0);
	}
*/
#ifdef RANDOM_DROP
if (random()%100 < 10) {
	return (0);
}
#endif
	switch (instance->memb_state) {
		case MEMB_STATE_OPERATIONAL:
			/* discard token */
			break;

		case MEMB_STATE_GATHER:
			memb_set_subtract (sub, &sub_entries,
				instance->my_proc_list, instance->my_proc_list_entries,
				instance->my_failed_list, instance->my_failed_list_entries);
			
			if (memb_set_equal (memb_commit_token->addr,
				memb_commit_token->addr_entries,
				sub,
				sub_entries) &&

				memb_commit_token->ring_id.seq > instance->my_ring_id.seq) {

				memb_state_commit_enter (instance, memb_commit_token);
			}
			break;

		case MEMB_STATE_COMMIT:
			if (memcmp (&memb_commit_token->ring_id, &instance->my_ring_id,
				sizeof (struct memb_ring_id)) == 0) {
//			 if (memb_commit_token->ring_id.seq == instance->my_ring_id.seq) {
				memb_state_recovery_enter (instance, memb_commit_token);
			}
			break;

		case MEMB_STATE_RECOVERY:
			instance->totemsrp_log_printf (instance->totemsrp_log_level_notice,
				"Sending initial ORF token\n");

			if (totemip_equal(&instance->my_id, &instance->my_ring_id.rep)) {
				// TODO convert instead of initiate
				orf_token_send_initial (instance);
				reset_token_timeout (instance); // REVIEWED
				reset_token_retransmit_timeout (instance); // REVIEWED
			}
			break;
	}
	return (0);
}

static int message_handler_token_hold_cancel (
	struct totemsrp_instance *instance,
	struct totem_ip_address *system_from,
	void *msg,
	int msg_len,
	int endian_conversion_needed)
{
	struct token_hold_cancel *token_hold_cancel = (struct token_hold_cancel *)msg;

	if (memcmp (&token_hold_cancel->ring_id, &instance->my_ring_id,
		sizeof (struct memb_ring_id)) == 0) {

		instance->my_seq_unchanged = 0;
		if (totemip_equal(&instance->my_ring_id.rep, &instance->my_id)) {
			timer_function_token_retransmit_timeout (instance);
		}
	}
	return (0);
}

void main_deliver_fn (
	void *context,
	struct totem_ip_address *system_from,
	void *msg,
	int msg_len)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)context;
	struct message_header *message_header = (struct message_header *)msg;

	if (msg_len < sizeof (struct message_header)) {
		instance->totemsrp_log_printf (instance->totemsrp_log_level_security, "Received message is too short...  ignoring %d.\n", msg_len);
		return;
	}

	system_from->nodeid = message_header->nodeid;
	assert (system_from->nodeid != 0);

	/*
	 * Handle incoming message
	 */
	totemsrp_message_handlers.handler_functions[(int)message_header->type] (
		instance,
		system_from,
		msg,
		msg_len,
		message_header->endian_detector != ENDIAN_LOCAL);
}

void main_iface_change_fn (
	void *context,
	struct totem_ip_address *iface_addr)
{
	struct totemsrp_instance *instance = (struct totemsrp_instance *)context;

	totemip_copy (&instance->my_id, iface_addr);
	assert (instance->my_id.nodeid);

	totemip_copy (&instance->my_memb_list[0], iface_addr);

	if (instance->first_run++ == 0) {
		memb_ring_id_create_or_load (instance, &instance->my_ring_id);
		instance->totemsrp_log_printf (
			instance->totemsrp_log_level_notice,
			"Created or loaded sequence id %lld.%s for this ring.\n",
			instance->my_ring_id.seq,
			totemip_print (&instance->my_ring_id.rep));

	}
	memb_state_gather_enter (instance);
}

void totemsrp_net_mtu_adjust (struct totem_config *totem_config) {
	totem_config->net_mtu -= sizeof (struct mcast);
}

