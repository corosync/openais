/*
 * Copyright (c) 2004 Mark Haverkamp
 * Copyright (c) 2004 Open Source Development Lab
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
 * - Neither the name of the Open Source Development Lab nor the names of its
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

//#define DEBUG
//#define EVT_EVENT_LIST_CHECK
//#define EVT_ALLOC_CHECK
//#define NO_DUPLICATES
#include <sys/types.h>
#include <malloc.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../include/ais_types.h"
#include "../include/ais_msg.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "aispoll.h"
#include "mempool.h"
#include "parse.h"
#include "main.h"
#include "print.h"
#include "gmi.h"
#include "hdb.h"
#include "clm.h"
#include "evt.h"

static int message_handler_req_lib_activatepoll (struct conn_info *conn_info, 
		void *message);
static int lib_evt_open_channel(struct conn_info *conn_info, void *message);
static int lib_evt_close_channel(struct conn_info *conn_info, void *message);
static int lib_evt_event_subscribe(struct conn_info *conn_info, 
		void *message);
static int lib_evt_event_unsubscribe(struct conn_info *conn_info, 
		void *message);
static int lib_evt_event_publish(struct conn_info *conn_info, void *message);
static int lib_evt_event_clear_retentiontime(struct conn_info *conn_info, 
		void *message);
static int lib_evt_event_data_get(struct conn_info *conn_info, 
		void *message);
static int evt_conf_change(
		enum gmi_configuration_type configuration_type,	
		struct sockaddr_in *member_list, int member_list_entries,
		struct sockaddr_in *left_list, int left_list_entries,
		struct sockaddr_in *joined_list, int joined_list_entries);

static int evt_initialize(struct conn_info *conn_info, void *msg);
static int evt_finalize(struct conn_info *conn_info);
static int evt_exec_init(void);

static struct libais_handler evt_libais_handlers[] = {
	{
	.libais_handler_fn = 	message_handler_req_lib_activatepoll,
	.response_size = 		sizeof(struct res_lib_activatepoll),
	.response_id = 			MESSAGE_RES_LIB_ACTIVATEPOLL,
	.gmi_prio = 			GMI_PRIO_RECOVERY
	},
	{
	.libais_handler_fn = 	lib_evt_open_channel,
	.response_size = 		sizeof(struct res_evt_channel_open),
	.response_id = 			MESSAGE_RES_EVT_OPEN_CHANNEL,
	.gmi_prio = 			GMI_PRIO_MED
	},
	{
	.libais_handler_fn = 	lib_evt_close_channel,
	.response_size = 		sizeof(struct res_evt_channel_close),
	.response_id = 			MESSAGE_RES_EVT_CLOSE_CHANNEL,
	.gmi_prio = 			GMI_PRIO_RECOVERY
	},
	{
	.libais_handler_fn = 	lib_evt_event_subscribe,
	.response_size = 		sizeof(struct res_evt_event_subscribe),
	.response_id = 			MESSAGE_RES_EVT_SUBSCRIBE,
	.gmi_prio = 			GMI_PRIO_RECOVERY
	},
	{
	.libais_handler_fn = 	lib_evt_event_unsubscribe,
	.response_size = 		sizeof(struct res_evt_event_unsubscribe),
	.response_id = 			MESSAGE_RES_EVT_UNSUBSCRIBE,
	.gmi_prio = 			GMI_PRIO_RECOVERY
	},
	{
	.libais_handler_fn = 	lib_evt_event_publish,
	.response_size = 		sizeof(struct res_evt_event_publish),
	.response_id = 			MESSAGE_RES_EVT_PUBLISH,
	.gmi_prio = 			GMI_PRIO_LOW
	},
	{
	.libais_handler_fn = 	lib_evt_event_clear_retentiontime,
	.response_size = 		sizeof(struct res_evt_event_clear_retentiontime),
	.response_id = 			MESSAGE_REQ_EVT_CLEAR_RETENTIONTIME,
	.gmi_prio = 			GMI_PRIO_MED
	},
	{
	.libais_handler_fn = 	lib_evt_event_data_get,
	.response_size = 		sizeof(struct lib_event_data),
	.response_id = 			MESSAGE_RES_EVT_EVENT_DATA,
	.gmi_prio = 			GMI_PRIO_RECOVERY
	},
};

	
static int evt_remote_evt(void *msg, struct in_addr source_addr);
static int evt_remote_chan_op(void *msg, struct in_addr source_addr);

static int (*evt_exec_handler_fns[]) (void *m, struct in_addr s) = {
	evt_remote_evt,
	evt_remote_chan_op
};

struct service_handler evt_service_handler = {
	.libais_handlers			= evt_libais_handlers,
	.libais_handlers_count		= sizeof(evt_libais_handlers) /
									sizeof(struct libais_handler),
	.aisexec_handler_fns		= evt_exec_handler_fns,
	.aisexec_handler_fns_count	= sizeof(evt_exec_handler_fns) /
									sizeof(int (*)),
	.confchg_fn					= evt_conf_change,
	.libais_init_fn				= evt_initialize,
	.libais_exit_fn				= evt_finalize,
	.exec_init_fn				= evt_exec_init
};

static gmi_recovery_plug_handle evt_recovery_plug_handle;

/* 
 * list of all retained events 
 * 	struct event_data
 */
static DECLARE_LIST_INIT(retained_list);

/*
 * list of all event channel information
 *	struct event_svr_channel_instance
 */
static DECLARE_LIST_INIT(esc_head);

/* 
 * list of all active event conn_info structs.
 */
static DECLARE_LIST_INIT(ci_head);

#define min(a,b) ((a) < (b) ? (a) : (b))

/*
 * Throttle event delivery to applications to keep
 * the exec from using too much memory if the app is 
 * slow to process its events.
 */
#define MAX_EVT_DELIVERY_QUEUE	1000
#define MIN_EVT_QUEUE_RESUME	(MAX_EVT_DELIVERY_QUEUE / 2)

#define LOST_PUB "EVENT_SERIVCE"
#define LOST_CHAN "LOST EVENT"
/*
 * Event to send when the delivery queue gets too full
 */
char lost_evt[] = SA_EVT_LOST_EVENT;
static int dropped_event_size;
static struct event_data *dropped_event;
struct evt_pattern {
	SaEvtEventPatternT	pat;
	char 	str[sizeof(lost_evt)];
};
static struct evt_pattern dropped_pattern = {
		.pat	= 	{&dropped_pattern.str[0], 
					sizeof(lost_evt)},
		.str = {SA_EVT_LOST_EVENT}
};

SaNameT lost_chan = {
	.value = LOST_CHAN,
	.length = sizeof(LOST_CHAN)
};

SaNameT dropped_publisher = {
	.value = LOST_PUB,
	.length = sizeof(LOST_PUB)
};

struct event_svr_channel_open;
struct event_svr_channel_subscr;

/*
 * Structure to contain global channel releated information
 *
 * esc_channel_name:	The name of this channel.
 * esc_open_chans:		list of opens of this channel.
 * 						(event_svr_channel_open.eco_entry)
 * esc_entry:			links to other channels. (used by esc_head)
 */
struct event_svr_channel_instance {
	SaNameT				esc_channel_name;
	struct list_head 	esc_open_chans;
	struct list_head 	esc_entry;
};

/*
 * has the event data in the correct format to send to the library API 
 * with aditional field for accounting.
 *
 * ed_ref_count:		how many other strutures are referencing.
 * ed_retained:			retained event list.
 * ed_timer_handle:		Timer handle for retained event expiration.
 * ed_delivered:		arrays of open channel pointers that this event
 * 						has been delivered to. (only used for events 
 * 						with a retention time).
 * ed_delivered_count:	Number of entries available in ed_delivered.
 * ed_delivered_next:	Next free spot in ed_delivered
 * ed_my_chan:			pointer to the global channel instance associated
 * 						with this event.
 * ed_event:			The event data formatted to be ready to send.
 */
struct event_data {
	uint32_t			    			ed_ref_count;
	struct list_head		    		ed_retained;
	poll_timer_handle 					ed_timer_handle;
	struct event_svr_channel_open 	    **ed_delivered;
	uint32_t			    			ed_delivered_count;
	uint32_t			    			ed_delivered_next;
	struct event_svr_channel_instance   *ed_my_chan;
	struct lib_event_data 		    	ed_event;
};

/*
 * Contains a list of pending events to be delivered to a subscribed 
 * application.
 *
 * cel_chan_handle:	associated library channel handle
 * cel_sub_id:		associated library subscription ID
 * cel_event:		event structure to deliver.
 * cel_entry:		list of pending events 
 * 					(struct event_server_instance.esi_events)
 */
struct chan_event_list {
	uint32_t			cel_chan_handle;
	uint32_t			cel_sub_id;
	struct event_data* 	cel_event;
	struct list_head 	cel_entry;
};

/*
 * Contains information about each open for a given channel
 *
 * eco_flags:			How the channel was opened.
 * eco_lib_handle:		channel handle in the app.  Used for event delivery.
 * eco_my_handle:		the handle used to access this data structure.
 * eco_channel:			Pointer to global channel info.
 * eco_entry:			links to other opeinings of this channel.
 * eco_instance_entry:	links to other channel opeinings for the 
 * 						associated server instance.
 * eco_subscr:			head of list of sbuscriptions for this channel open.
 * 						(event_svr_channel_subscr.ecs_entry)
 * eco_conn_info:		refrence to EvtInitialize who owns this open.
 */
struct event_svr_channel_open {
	uint8_t								eco_flags;
	uint32_t							eco_lib_handle;
	uint32_t							eco_my_handle;
	struct event_svr_channel_instance 	*eco_channel;
	struct list_head 					eco_entry;
	struct list_head 					eco_instance_entry;
	struct list_head 					eco_subscr;
	struct conn_info					*eco_conn_info;
};

/*
 * Contains information about each channel subscription
 *
 * ecs_open_chan:		Link to our open channel.
 * ecs_sub_id:			Subscription ID.
 * ecs_filter_count:	number of filters in ecs_filters
 * ecs_filters:			filters for determining event delivery.
 * ecs_entry:			Links to other subscriptions to this channel opening.
 */
struct event_svr_channel_subscr {
	struct event_svr_channel_open	*ecs_open_chan;
	uint32_t						ecs_sub_id;
	SaEvtEventFilterArrayT			*ecs_filters;
	struct list_head 				ecs_entry;
};


/*
 * Member node data
 * mn_node_info:		cluster node info from membership
 * mn_last_evt_id:		last seen event ID for this node
 * mn_started:			Indicates that event service has started
 * 						on this node.
 * mn_next:				pointer to the next node in the hash chain.
 */
struct member_node_data {
	SaClmClusterNodeT	mn_node_info;
	SaEvtEventIdT		mn_last_evt_id;
	SaClmNodeIdT		mn_started;
	struct member_node_data	*mn_next;
};

/*
 * Get the time of day and convert to nanoseconds
 */
static SaTimeT clustTimeNow(void)
{
	struct timeval tv;
	SaTimeT time_now;

	if (gettimeofday(&tv, 0)) {
		return 0ULL;
	}

	time_now = (SaTimeT)(tv.tv_sec) * 1000000000ULL;
	time_now += (SaTimeT)(tv.tv_usec) * 1000ULL;

	return time_now;
}

/*
 * Take the filters we received from the application via the library and 
 * make them into a real SaEvtEventFilterArrayT
 */
static SaErrorT evtfilt_to_aisfilt(struct req_evt_event_subscribe *req,
		SaEvtEventFilterArrayT **evtfilters)
{

	SaEvtEventFilterArrayT *filta = 
			(SaEvtEventFilterArrayT *)req->ics_filter_data;
	SaEvtEventFilterArrayT *filters;
	SaEvtEventFilterT *filt = (void *)filta + sizeof(SaEvtEventFilterArrayT);
	SaUint8T *str = (void *)filta + sizeof(SaEvtEventFilterArrayT) + 
			(sizeof(SaEvtEventFilterT) * filta->filtersNumber);
	int i;
	int j;

	filters = malloc(sizeof(SaEvtEventFilterArrayT));
	if (!filters) {
		return SA_ERR_NO_MEMORY;
	}

	filters->filtersNumber = filta->filtersNumber;
	filters->filters = malloc(sizeof(SaEvtEventFilterT) * 
				filta->filtersNumber);
	if (!filters->filters) {
			free(filters);
			return SA_ERR_NO_MEMORY;
	}

	for (i = 0; i < filters->filtersNumber; i++) {
		filters->filters[i].filter.pattern = 
			malloc(filt[i].filter.patternSize);

		if (!filters->filters[i].filter.pattern) {
				for (j = 0; j < i; j++) {
						free(filters->filters[j].filter.pattern);
				}
				free(filters->filters);
				free(filters);
				return SA_ERR_NO_MEMORY;
		}
		filters->filters[i].filter.patternSize = 
			filt[i].filter.patternSize;
		memcpy(filters->filters[i].filter.pattern,
				str, filters->filters[i].filter.patternSize);
		filters->filters[i].filterType = filt[i].filterType;
		str += filters->filters[i].filter.patternSize;
	}

	*evtfilters = filters;
	
	return SA_OK;
}

/*
 * Free up filter data
 */
static void free_filters(SaEvtEventFilterArrayT *fp)
{
	int i;

	for (i = 0; i < fp->filtersNumber; i++) {
		free(fp->filters[i].filter.pattern);
	}

	free(fp->filters);
	free(fp);
}

/*
 * Look up a channel in the global channel list
 */
static struct event_svr_channel_instance *
find_channel(SaNameT *chan_name)
{
	struct list_head *l;
	struct event_svr_channel_instance *eci;

	for (l = esc_head.next; l != &esc_head; l = l->next) {

		eci = list_entry(l, struct event_svr_channel_instance, esc_entry);
		if (chan_name->length != eci->esc_channel_name.length) {
			continue;
		}
		if (memcmp(chan_name->value, eci->esc_channel_name.value, 
					chan_name->length) != 0) {
			continue;
		}
		return eci;
	}
	return 0;
}

/*
 * Create and initialize a channel instance structure
 */
static struct event_svr_channel_instance *create_channel(SaNameT *cn)
{
	struct event_svr_channel_instance *eci;
	eci = (struct event_svr_channel_instance *) malloc(sizeof(*eci));
	if (!eci) {
		return (eci);
	}

	memset(eci, 0, sizeof(eci));
	list_init(&eci->esc_entry);
	list_init(&eci->esc_open_chans);
	eci->esc_channel_name.length = 
		cn->length;
	memcpy(eci->esc_channel_name.value, cn->value, cn->length);
	list_add(&eci->esc_entry, &esc_head);

	return eci;
}

/*
 * Return a pointer to the global channel information.
 * Possibly create the channel structure and notify remote nodes
 * of channel creation.
 */
static SaErrorT evt_open_channel(SaNameT *cn, SaUint8T flgs,
			SaTimeT timeout, struct event_svr_channel_instance **eci,
			struct libevt_ci *esip)
{
	struct event_svr_channel_instance *ecp;
	struct req_evt_chan_command cpkt;
	struct iovec chn_iovec;
	int res;
	SaErrorT ret;

	ret = SA_OK;

	*eci = find_channel(cn);

	/*
	 * No need to send anything to the cluster since we're already
	 * receiving messages for this channel.
	 */
	if (*eci) {
		goto chan_open_end;
	}

	/*
	 * If the create flag set, we can make the channel.  Otherwise,
	 * it's an error since we're notified of channels being created and
	 * opened.
	 */
	if (flgs & SA_EVT_CHANNEL_CREATE) {
		*eci = create_channel(cn);
		ecp = *eci;
	} else {
		ret = SA_ERR_NOT_EXIST;
		goto chan_open_end;
	}

	/*
	 * create the channel packet to send. Tell the rest of the cluster
	 * that we've created the channel.
	 */
	memset(&cpkt, 0, sizeof(cpkt));
	cpkt.chc_head.id = MESSAGE_REQ_EXEC_EVT_CHANCMD;
	cpkt.chc_head.size = sizeof(cpkt);
	cpkt.chc_op = MESSAGE_REQ_EVT_OPEN_CHANNEL;
	cpkt.u.chc_chan = *cn;
	chn_iovec.iov_base = &cpkt;
	chn_iovec.iov_len = cpkt.chc_head.size;
	res = gmi_mcast (&aisexec_groupname, &chn_iovec, 1, GMI_PRIO_MED);
	if (res != 0) {
			ret = SA_ERR_SYSTEM;
	}

chan_open_end:
	return ret;

}

#ifdef NO_DUPLICATES
/*
 * Node data access functions.  Used to detect and filter duplicate
 * delivery of messages.
 *
 * add_node: 	Add a new member node to our list.
 * remove_node:	Remove a node that left membership.
 * find_node:	Given the node ID return a pointer to node information.
 *
 * TODO: There is a problem when receiving config updates.  When we get the
 * TODO:	update, the cluster node table hasn't been updated yet and we
 * TODO:	can't find the node to put in this list.
 *
 */
#define NODE_HASH_SIZE 256
static struct member_node_data *nl[NODE_HASH_SIZE] = {0};
inline int 
hash_node_id(SaClmNodeIdT node_id)
{
	return node_id & (NODE_HASH_SIZE - 1);
}

static struct member_node_data **lookup_node(SaClmNodeIdT node_id)
{
	int index = hash_node_id(node_id);
	struct member_node_data **nlp;

	nlp = &nl[index];
	for (nlp = &nl[index]; *nlp; nlp = &((*nlp)->mn_next)) {
		if ((*nlp)->mn_node_info.nodeId == node_id) {
			break;
		}
	}

	return nlp;
}

static struct member_node_data *
evt_find_node(SaClmNodeIdT node_id)
{
	struct member_node_data **nlp;

	nlp = lookup_node(node_id);

	if (!nlp) {
		log_printf(LOG_LEVEL_DEBUG, "find_node: Got NULL nlp?\n");
		return 0;
	}

	return *nlp;
}

static SaErrorT
evt_add_node(SaClmClusterNodeT *ni) 
{
	struct member_node_data **nlp;
	struct member_node_data *nl;
	SaErrorT err = SA_ERR_EXIST;

	nlp = lookup_node(ni->nodeId);

	if (!nlp) {
		log_printf(LOG_LEVEL_DEBUG, "add_node: Got NULL nlp?\n");
		goto an_out;
	}

	if (*nlp) {
		goto an_out;
	}

	*nlp = malloc(sizeof(struct member_node_data));
	if (!nlp) {
			return SA_ERR_NO_MEMORY;
	}
	nl = *nlp;
	if (nl) {
		memset(nl, 0, sizeof(*nl));
		err = SA_OK;
	}

	nl->mn_node_info.nodeId = ni->nodeId;
	nl->mn_node_info.nodeAddress = ni->nodeAddress;
	nl->mn_node_info.nodeName = ni->nodeName;
	nl->mn_node_info.clusterName = ni->clusterName;
	nl->mn_node_info.member = ni->member;
	nl->mn_node_info.bootTimestamp = ni->bootTimestamp;

an_out:
	return err;
}

static SaErrorT
evt_remove_node(SaClmClusterNodeT *ni) 
{
	struct member_node_data **nlp;
	struct member_node_data *nl;
	SaErrorT err = SA_ERR_NOT_EXIST;

	nlp = lookup_node(ni->nodeId);

	if (!nlp) {
		log_printf(LOG_LEVEL_DEBUG, "remove_node: Got NULL nlp?\n");
		goto an_out;
	}

	if (!(*nlp)) {
		goto an_out;
	}

	nl = *nlp;

	*nlp = nl->mn_next;
	free(*nlp);
	err = SA_OK;

an_out:
	return err;
}
#endif


/*
 * Send our retained events to the specified node id.
 * Called when a remote event server starts up and opens a channel
 * that has retained events that we published.
 *
 * TODO: Fill me in
 */
static void send_retained(SaNameT *cn, SaClmNodeIdT node_id)
{
	log_printf(LOG_LEVEL_DEBUG, 
					"TODO: Send retained messages for %s to 0x%x\n", 
					cn->value, node_id);
}

/*
 * purge retained events from the specified node id.
 * Called when a remote event server terminates.
 *
 * TODO: Fill me in
 */
static void purge_retained(SaClmNodeIdT node_id)
{
	log_printf(LOG_LEVEL_DEBUG, "TODO: Purge retained messages for node 0x%x\n", node_id);
}

#ifdef NO_DUPLICATES
/*
 * See if we've already seen a message with this ID from 
 * this node.  Return 0 for not seen, 1 for seen.
 * We also bump the last seen event for the next time. So only call this 
 * once per event being proccessed.
 */
static int is_duplicate_event(struct lib_event_data *evtpkt, 
				SaClmClusterNodeT *cn)
{
	struct member_node_data *nd;


	/*
	 * Look up the node and check the largest event ID that we've seen.
	 * Since event IDs are increasing and are delivered in order from
	 * a given publisher, we just need to check that this ID is
	 * greater than the last one that we saw.
	 */
	nd = evt_find_node(evtpkt->led_publisher_node_id);
	if (!nd) {
		log_printf(LOG_LEVEL_DEBUG, "Node ID 0x%x not found for event %llx\n",
				evtpkt->led_publisher_node_id, evtpkt->led_event_id);
		evt_add_node(cn);
		return 0;
	}

	/*
	 * This shouldn't happen
	 */
	if ((nd->mn_last_evt_id >= evtpkt->led_event_id) && 
						(evtpkt->led_event_id & 0xffffffffull) != 0ull) {
		log_printf(LOG_LEVEL_NOTICE, 
			"Event out of order for node ID 0x%x\n",
				evtpkt->led_publisher_node_id);
		log_printf(LOG_LEVEL_NOTICE, 
			"last event ID 0x%llx, current event ID 0x%llx\n",
				nd->mn_last_evt_id, evtpkt->led_event_id);
		return 1;
	}

	/*
	 * This is probably OK, but here for debugging purposes
	 */
	if(((nd->mn_last_evt_id & 0xffffffff) > 0) && (nd->mn_last_evt_id < 
										(evtpkt->led_event_id -1))) {
		log_printf(LOG_LEVEL_NOTICE,
			"Event sequence skipped for node ID 0x%x\n",
			evtpkt->led_publisher_node_id);
		log_printf(LOG_LEVEL_NOTICE, 
			"last event ID 0x%llx, current event ID 0x%llx\n",
				nd->mn_last_evt_id, evtpkt->led_event_id);
	}
	nd->mn_last_evt_id = evtpkt->led_event_id;

	return 0;
}
#endif

/*
 * Send a message to the app to wake it up if it is polling
 */
static int message_handler_req_lib_activatepoll(struct conn_info *conn_info, 
		void *message)
{
	struct res_lib_activatepoll res;

	res.header.error = SA_OK;
	res.header.size = sizeof (struct res_lib_activatepoll);
	res.header.id = MESSAGE_RES_LIB_ACTIVATEPOLL;
	libais_send_response(conn_info, &res, sizeof(res));

	return (0);
}

/*
 * event id generating code.  We use the node ID for this node for the
 * upper 32 bits of the event ID to make sure that we can generate a cluster
 * wide unique event ID for a given event.
 */
static SaEvtEventIdT base_id = 0;
SaErrorT set_event_id(SaClmNodeIdT node_id)
{
	SaErrorT err = SA_OK;
	if (base_id) {
		err =  SA_ERR_EXIST;
	}
	base_id = (SaEvtEventIdT)node_id << 32;
	return err;
}

static SaErrorT get_event_id(uint64_t *event_id)
{
	*event_id = base_id++;
	return SA_OK;
}

/*
static uint32_t evt_alloc = 0;
static uint32_t evt_free = 0;
*/
/*
 * Free up an event structure if it isn't being used anymore.
 */
static void
free_event_data(struct event_data *edp)
{
	if (--edp->ed_ref_count) {
		return;
	}
	log_printf(LOG_LEVEL_DEBUG, "Freeing event ID: 0x%llx\n", 
			edp->ed_event.led_event_id);
	if (edp->ed_delivered) {
		free(edp->ed_delivered);
	}

#ifdef EVT_ALLOC_CHECK
	evt_free++;
	if ((evt_free % 1000) == 0) {
			log_printf(LOG_LEVEL_NOTICE, "evt alloc: %u, evt free: %u\n",
							evt_alloc, evt_free);
	}
#endif
	free(edp);
}

/*
 * Timer handler to delete expired events.
 *
 */
static void
event_retention_timeout(void *data)
{
	struct event_data *edp = data;
	log_printf(LOG_LEVEL_DEBUG, "Event ID %llx expired\n", 
					edp->ed_event.led_event_id);
	list_del(&edp->ed_retained);
	list_init(&edp->ed_retained);
	free_event_data(edp);
}

/*
 * clear a particular event's retention time.
 * This will free the event as long as it isn't being
 * currently used.
 *
 */
static void
clear_retention_time(SaEvtEventIdT event_id)
{
	struct event_data *edp;
	struct list_head *l, *nxt;
	int ret;

	log_printf(LOG_LEVEL_DEBUG, "Search for Event ID %llx\n", event_id);
	for(l = retained_list.next; l != &retained_list; l = nxt) {
		nxt = l->next;
		edp = list_entry(l, struct event_data, ed_retained);
		if (edp->ed_event.led_event_id != event_id) {
				continue;
		}

		log_printf(LOG_LEVEL_DEBUG, 
							"Clear retention time for Event ID %llx\n", 
				edp->ed_event.led_event_id);
		ret = poll_timer_delete(aisexec_poll_handle, edp->ed_timer_handle);
		if (ret != 0 ) {
			log_printf(LOG_LEVEL_ERROR, "Error expiring event ID %llx\n",
							edp->ed_event.led_event_id);
			return;
		}
		edp->ed_event.led_retention_time = 0;
		list_del(&edp->ed_retained);
		list_init(&edp->ed_retained);
		free_event_data(edp);
		break;
	}
}

/*
 * Remove specified channel from event delivery list
 */
static void
remove_delivered_channel(struct event_svr_channel_open *eco)
{
	int i;
	struct list_head *l;
	struct event_data *edp;

	for (l = retained_list.next; l != &retained_list; l = l->next) {
		edp = list_entry(l, struct event_data, ed_retained);

		for (i = 0; i < edp->ed_delivered_next; i++) {
			if (edp->ed_delivered[i] == eco) {
				edp->ed_delivered_next--;
				if (edp->ed_delivered_next == i) {
					break;
				}
				memmove(&edp->ed_delivered[i],
					&edp->ed_delivered[i+1],
					&edp->ed_delivered[edp->ed_delivered_next] - 
					   &edp->ed_delivered[i]);
				break;
			}
		}
	}
	return;
}

/*
 * If there is a retention time, add this open channel to the event so 
 * we can check if we've already delivered this message later if a new
 * subscription matches.
 */
#define DELIVER_SIZE 8
static void
evt_delivered(struct event_data *evt, struct event_svr_channel_open *eco)
{
	if (!evt->ed_event.led_retention_time) {
		return;
	}

	log_printf(LOG_LEVEL_DEBUG, "delivered ID %llx to eco %p\n", 
			evt->ed_event.led_event_id, eco);
	if (evt->ed_delivered_count == evt->ed_delivered_next) {
		evt->ed_delivered = realloc(evt->ed_delivered,
			DELIVER_SIZE * sizeof(struct event_svr_channel_open *));
		memset(evt->ed_delivered + evt->ed_delivered_next, 0, 
			DELIVER_SIZE * sizeof(struct event_svr_channel_open *));
		evt->ed_delivered_next = evt->ed_delivered_count;
		evt->ed_delivered_count += DELIVER_SIZE;
	}

	evt->ed_delivered[evt->ed_delivered_next++] = eco;
}

/*
 * Check to see if an event has already been delivered to this open channel
 */
static int
evt_already_delivered(struct event_data *evt, 
		struct event_svr_channel_open *eco)
{
	int i;

	if (!evt->ed_event.led_retention_time) {
		return 0;
	}

	log_printf(LOG_LEVEL_DEBUG, "Deliver count: %d deliver_next %d\n", 
		evt->ed_delivered_count, evt->ed_delivered_next);
	for (i = 0; i < evt->ed_delivered_next; i++) {
		log_printf(LOG_LEVEL_DEBUG, "Checking ID %llx delivered %p eco %p\n", 
			evt->ed_event.led_event_id, evt->ed_delivered[i], eco);
		if (evt->ed_delivered[i] == eco) {
			return 1;
		}
	}
	return 0;
}

/*
 * Compare a filter to a given pattern.
 * return SA_OK if the pattern matches a filter
 */
static SaErrorT
filter_match(SaEvtEventPatternT *ep, SaEvtEventFilterT *ef)
{
	int ret;
	ret = SA_ERR_FAILED_OPERATION;

	switch (ef->filterType) {
	case SA_EVT_PREFIX_FILTER:
		if (ef->filter.patternSize > ep->patternSize) {
			break;
		}
		if (strncmp(ef->filter.pattern, ep->pattern,
					ef->filter.patternSize) == 0) {
			ret = SA_OK;
		}
		break;
	case SA_EVT_SUFFIX_FILTER:
		if (ef->filter.patternSize > ep->patternSize) {
			break;
		}
		if (strncmp(ef->filter.pattern, 
			&ep->pattern[ep->patternSize - ef->filter.patternSize],
					ef->filter.patternSize) == 0) {
			ret = SA_OK;
		}
		
		break;
	case SA_EVT_EXACT_FILTER:
		if (ef->filter.patternSize != ep->patternSize) {
			break;
		}
		if (strncmp(ef->filter.pattern, ep->pattern,
					ef->filter.patternSize) == 0) {
			ret = SA_OK;
		}
		break;
	case SA_EVT_PASS_ALL_FILTER:
		ret = SA_OK;
		break;
	default:
		break;
	}
	return ret;
}

/*
 * compare the event's patterns with the subscription's filter rules.
 * SA_OK is returned if the event matches the filter rules.
 */
static SaErrorT
event_match(struct event_data *evt, 
			struct event_svr_channel_subscr *ecs)
{
	SaEvtEventFilterT *ef;
	SaEvtEventPatternT *ep;
	uint32_t filt_count;
	SaErrorT ret =  SA_OK;
	int i;

	ep = (SaEvtEventPatternT *)(&evt->ed_event.led_body[0]);
	ef = ecs->ecs_filters->filters;
	filt_count = min(ecs->ecs_filters->filtersNumber, 
			evt->ed_event.led_patterns_number);

	for (i = 0; i < filt_count; i++) {
		ret = filter_match(ep, ef);
		if (ret != SA_OK) {
			break;
		}
		ep++;
		ef++;
	}
	return ret;
}

/*
 * Scan undelivered pending events and either remove them if no subscription
 * filters match anymore or re-assign them to another matching subscription
 */
static void
filter_undelivered_events(struct event_svr_channel_open *op_chan)
{
	struct event_svr_channel_open *eco;
	struct event_svr_channel_instance *eci;
	struct event_svr_channel_subscr *ecs;
	struct chan_event_list *cel;
	struct libevt_ci *esip = &op_chan->eco_conn_info->ais_ci.u.libevt_ci;
	struct list_head *l, *nxt;
	struct list_head *l1, *l2;
	int i;

	eci = op_chan->eco_channel;

	/*
	 * Scan each of the priority queues for messages
	 */
	for (i = SA_EVT_HIGHEST_PRIORITY; i <= SA_EVT_LOWEST_PRIORITY; i++) {
		/*
		 * examine each message queued for delivery
		 */
		for (l = esip->esi_events[i].next; l != &esip->esi_events[i]; l = nxt) {
			nxt = l->next;
			cel = list_entry(l, struct chan_event_list, cel_entry);
			/*
		 	 * Check open channels
		 	 */
			 for (l1 = eci->esc_open_chans.next; 
								l1 != &eci->esc_open_chans; l1 = l1->next) {
				 eco = list_entry(l1, struct event_svr_channel_open, eco_entry);

				 /*
				  * See if this channel open instance belongs
				  * to this evtinitialize instance
				  */
				 if (eco->eco_conn_info != op_chan->eco_conn_info) {
					 continue;
				 }

				 /*
				  * See if enabled to receive
				  */
				 if (!(eco->eco_flags & SA_EVT_CHANNEL_SUBSCRIBER)) {
					continue;
				 }

				 /*
				  * Check subscriptions
				  */
				 for (l2 = eco->eco_subscr.next; 
									l2 != &eco->eco_subscr; l2 = l2->next) {
					 ecs = list_entry(l2, 
								struct event_svr_channel_subscr, ecs_entry);
					 if (event_match(cel->cel_event, ecs) == SA_OK) {
						 /*
						  * Something still matches.  
						  * We'll assign it to
						  * the new subscription.
						  */
						 cel->cel_sub_id = ecs->ecs_sub_id;
						 cel->cel_chan_handle = eco->eco_lib_handle;
						 goto next_event;
					 }
				 }
			 }
			 /*
			  * No subscription filter matches anymore.  We
			  * can delete this event.
			  */
			 list_del(&cel->cel_entry);
			 list_init(&cel->cel_entry);
			 esip->esi_nevents--;
		
#ifdef EVT_EVENT_LIST_CHECK
			 if (esip->esi_nevents < 0) {
				log_printf(LOG_LEVEL_NOTICE, "event count went negative\n");
				esip->esi_nevents = 0;
			 }
#endif
			 free_event_data(cel->cel_event);
			 free(cel);
next_event:
			 continue;
		 }
	}
}

/*
 * Notify the library of a pending event
 */
static void __notify_event(struct conn_info *conn_info)
{
	struct res_evt_event_data res;
	struct libevt_ci *esip = &conn_info->ais_ci.u.libevt_ci;

	log_printf(LOG_LEVEL_DEBUG, "DELIVER: notify\n");
	if (esip->esi_nevents != 0) {
		res.evd_head.size = sizeof(res);
		res.evd_head.id = MESSAGE_RES_EVT_AVAILABLE;
		res.evd_head.error = SA_OK;
		libais_send_response(conn_info, &res, sizeof(res));
	}

}
inline void notify_event(struct conn_info *conn_info)
{
	struct libevt_ci *esip = &conn_info->ais_ci.u.libevt_ci;

	/*
	 * Give the library a kick if there aren't already
	 * events queued for delivery.
	 */
	if (esip->esi_nevents++ == 0) {
		__notify_event(conn_info);
	}
}

/*
 * sends/queues up an event for a subscribed channel.
 */
static void
deliver_event(struct event_data *evt, 
		struct event_svr_channel_open *eco,
		struct event_svr_channel_subscr *ecs)
{
	struct chan_event_list *ep;
	struct libevt_ci *esip = &eco->eco_conn_info->ais_ci.u.libevt_ci;
	SaEvtEventPriorityT evt_prio = evt->ed_event.led_priority;
	struct chan_event_list *cel;
	int do_deliver_event = 0;
	int do_deliver_warning = 0;
	int i;

	if (evt_prio > SA_EVT_LOWEST_PRIORITY) {
		evt_prio = SA_EVT_LOWEST_PRIORITY;
	}

	/*
	 * Delivery queue check.
	 * - If the queue is blocked, see if we've sent enough messages to
	 *   unblock it.
	 * - If it isn't blocked, see if this message will put us over the top.
	 * - If we can't deliver this message, see if we can toss some lower
	 *   priority message to make room for this one.
	 * - If we toss any messages, queue up an event of SA_EVT_LOST_EVENT_PATTERN
	 *   to let the application know that we dropped some messages.
	 */
	if (esip->esi_queue_blocked) {
		if (esip->esi_nevents < MIN_EVT_QUEUE_RESUME) {
			esip->esi_queue_blocked = 0;
			log_printf(LOG_LEVEL_DEBUG, "unblock\n");
		}
	}

	if (!esip->esi_queue_blocked && 
							(esip->esi_nevents >= MAX_EVT_DELIVERY_QUEUE)) {
		log_printf(LOG_LEVEL_DEBUG, "block\n");
		esip->esi_queue_blocked = 1;
		do_deliver_warning = 1;
	}

	if (esip->esi_queue_blocked) {
		do_deliver_event = 0;
		for (i = SA_EVT_LOWEST_PRIORITY; i > evt_prio; i--) {
			if (!list_empty(&esip->esi_events[i])) {
				/*
				 * Get the last item on the list, so we drop the most 
				 * recent lowest priority event.
				 */
				cel = list_entry(esip->esi_events[i].prev, 
											struct chan_event_list, cel_entry);
				log_printf(LOG_LEVEL_DEBUG, "Drop 0x%0llx\n",
					cel->cel_event->ed_event.led_event_id);
				list_del(&cel->cel_entry);
				free_event_data(cel->cel_event);
				free(cel);
				esip->esi_nevents--;
				do_deliver_event = 1;
				break;
			}
		}
	} else {
		do_deliver_event = 1;
	}

	/*
	 * Queue the event for delivery
	 */
	if (do_deliver_event) {
		evt->ed_ref_count++;
		ep = malloc(sizeof(*ep));
		if (!ep) {
			log_printf(LOG_LEVEL_WARNING, 
						"Memory allocation error, can't deliver event\n");
			return;
		}
		ep->cel_chan_handle = eco->eco_lib_handle;
		ep->cel_sub_id = ecs->ecs_sub_id;
		list_init(&ep->cel_entry);
		ep->cel_event = evt;
		list_add_tail(&ep->cel_entry, &esip->esi_events[evt_prio]);
		evt_delivered(evt, eco);
		notify_event(eco->eco_conn_info);
	} 

	/*
	 * If we dropped an event, queue this so that the application knows
	 * what has happened.
	 */
	if (do_deliver_warning) {
		struct event_data *ed;
		ed = malloc(dropped_event_size);
		if (!ed) {
			log_printf(LOG_LEVEL_WARNING, 
						"Memory allocation error, can't deliver event\n");
			return;
		}
		log_printf(LOG_LEVEL_DEBUG, "Warn 0x%0llx\n", 
								evt->ed_event.led_event_id);
		memcpy(ed, dropped_event, dropped_event_size);
		ed->ed_event.led_publish_time = clustTimeNow();
		list_init(&ed->ed_retained);

		ep = malloc(sizeof(*ep));
		if (!ep) {
			log_printf(LOG_LEVEL_WARNING, 
						"Memory allocation error, can't deliver event\n");
			return;
		}
		ep->cel_chan_handle = eco->eco_lib_handle;
		ep->cel_sub_id = ecs->ecs_sub_id;
		list_init(&ep->cel_entry);
		ep->cel_event = ed;
		list_add_tail(&ep->cel_entry, &esip->esi_events[SA_EVT_HIGHEST_PRIORITY]);
		notify_event(eco->eco_conn_info);
	}
}

/*
 * Take an event received from the network and fix it up to be usable.
 * - fix up pointers for pattern list.
 * - fill in some channel info
 */
static struct event_data *
make_local_event(struct lib_event_data *p, 
			struct event_svr_channel_instance *eci)
{
	struct event_data *ed;
	SaEvtEventPatternT *eps;
	SaUint8T *str;
	uint32_t ed_size;
	int i;

	ed_size = sizeof(*ed) + p->led_user_data_offset + p->led_user_data_size;
	ed = malloc(ed_size);
	if (!ed) {
			return 0;
	}
	memset(ed, 0, ed_size);
	list_init(&ed->ed_retained);
	ed->ed_my_chan = eci;

	/*
	 * Fill in lib_event_data and make the pattern pointers valid
	 */
	memcpy(&ed->ed_event, p, sizeof(*p) + 
					p->led_user_data_offset + p->led_user_data_size);

	eps = (SaEvtEventPatternT *)ed->ed_event.led_body;  
	str = ed->ed_event.led_body + 
			(ed->ed_event.led_patterns_number * sizeof(SaEvtEventPatternT));
	for (i = 0; i < ed->ed_event.led_patterns_number; i++) {
		eps->pattern = str;
		str += eps->patternSize;
		eps++;
	}

#ifdef EVT_ALLOC_CHECK
	evt_alloc++;
	if ((evt_alloc % 1000) == 0) {
			log_printf(LOG_LEVEL_NOTICE, "evt alloc: %u, evt free: %u\n",
							evt_alloc, evt_free);
	}
#endif
	ed->ed_ref_count++;
	return ed;
}

/*
 * Set an event to be retained.
 */
static void retain_event(struct event_data *evt)
{
	uint32_t ret;
	int msec_in_future;

	evt->ed_ref_count++;
	list_add_tail(&evt->ed_retained, &retained_list);
	/*
	 * Time in nanoseconds - convert to miliseconds
	 */
	msec_in_future = (uint32_t)((evt->ed_event.led_retention_time) / 1000000ULL);
	ret = poll_timer_add(aisexec_poll_handle,
					msec_in_future,
					evt,
					event_retention_timeout,
					&evt->ed_timer_handle);
	if (ret != 0) {
		log_printf(LOG_LEVEL_ERROR, "retention of event id 0x%llx failed\n",
				evt->ed_event.led_event_id);
	} else {
		log_printf(LOG_LEVEL_DEBUG, "Retain event ID 0x%llx\n", 
					evt->ed_event.led_event_id);
	}
}

/*
 * Scan the subscription list and look for the specified subsctiption ID.
 * Only look for the ID in subscriptions that are associated with the 
 * saEvtInitialize associated with the specified open channel.
 */
static struct event_svr_channel_subscr *find_subscr(
		struct event_svr_channel_open *open_chan, SaEvtSubscriptionIdT sub_id)
{
	struct event_svr_channel_instance *eci;
	struct event_svr_channel_subscr *ecs;
	struct event_svr_channel_open	*eco;
	struct list_head *l, *l1;
	struct conn_info* conn_info = open_chan->eco_conn_info;

	eci = open_chan->eco_channel;

	/*
	 * Check for subscription id already in use.
	 * Subscriptions are unique within saEvtInitialize (Callback scope).
	 */
    for (l = eci->esc_open_chans.next; l != &eci->esc_open_chans; l = l->next) {
		eco = list_entry(l, struct event_svr_channel_open, eco_entry);
		/*
		 * Don't bother with open channels associated with another 
		 * EvtInitialize
		 */
		if (eco->eco_conn_info != conn_info) {
			continue;
		}

		for (l1 = eco->eco_subscr.next; l1 != &eco->eco_subscr; l1 = l1->next) {
			ecs = list_entry(l1, struct event_svr_channel_subscr, ecs_entry);
			if (ecs->ecs_sub_id == sub_id) {
				return ecs;
			}
		}
	}
	return 0;
}

/*
 * Handler for saEvtInitialize
 */
static int evt_initialize(struct conn_info *conn_info, void *msg)
{
	struct res_lib_init res;
	struct libevt_ci *libevt_ci = &conn_info->ais_ci.u.libevt_ci;
	int i;

	
	res.header.size = sizeof (struct res_lib_init);
	res.header.id = MESSAGE_RES_INIT;
	res.header.error = SA_OK;

	log_printf(LOG_LEVEL_DEBUG, "saEvtInitialize request.\n");
	if (!conn_info->authenticated) {
		log_printf(LOG_LEVEL_DEBUG, "event service: Not authenticated\n");
		res.header.error = SA_ERR_SECURITY;
		libais_send_response(conn_info, &res, sizeof(res));
		return -1;
	}

	memset(libevt_ci, 0, sizeof(*libevt_ci));
	list_init(&libevt_ci->esi_open_chans);
	for (i = SA_EVT_HIGHEST_PRIORITY; i <= SA_EVT_LOWEST_PRIORITY; i++) {
		list_init(&libevt_ci->esi_events[i]);
	}
	conn_info->service = SOCKET_SERVICE_EVT;
	list_init (&conn_info->conn_list);
	list_add_tail(&conn_info->conn_list, &ci_head);
	libais_send_response (conn_info, &res, sizeof(res));

	return 0;
}

/*
 * Handler for saEvtChannelOpen
 */
static int lib_evt_open_channel(struct conn_info *conn_info, void *message)
{
	uint32_t handle;
	SaErrorT error;
	struct req_evt_channel_open *req;
	struct res_evt_channel_open res;
	struct event_svr_channel_instance 	*eci;
	struct event_svr_channel_open		*eco;
	struct libevt_ci *esip = &conn_info->ais_ci.u.libevt_ci;

	req = message;


	log_printf(LOG_LEVEL_DEBUG, "saEvtChannelOpen (Open channel request)\n");
	log_printf(LOG_LEVEL_DEBUG, 
			"handle 0x%x, to 0x%llx\n",
			req->ico_c_handle,
			req->ico_timeout);
	log_printf(LOG_LEVEL_DEBUG, "flags %x, channel name(%d)  %s\n",
			req->ico_open_flag,
			req->ico_channel_name.length,
			req->ico_channel_name.value);
	/*
	 * Create a handle to give back to the caller to associate
	 * with this channel open instance.
	 */
	error = saHandleCreate(&esip->esi_hdb, sizeof(*eco), &handle);
	if (error != SA_OK) {
		goto open_return;
	}
	error = saHandleInstanceGet(&esip->esi_hdb, handle, (void**)&eco);
	if (error != SA_OK) {
		goto open_return;
	}

	/*
	 * Open the channel.
	 *
	 */
	error = evt_open_channel(&req->ico_channel_name, 
			req->ico_open_flag, req->ico_timeout, &eci, esip);

	if (error != SA_OK) {
		saHandleDestroy(&esip->esi_hdb, handle);
		goto open_put;
	}

	/*
	 * Initailize and link into the global channel structure.
	 */
	list_init(&eco->eco_subscr);
	list_init(&eco->eco_entry);
	list_init(&eco->eco_instance_entry);
	eco->eco_flags = req->ico_open_flag;
	eco->eco_channel = eci;
	eco->eco_lib_handle = req->ico_c_handle;
	eco->eco_my_handle = handle;
	eco->eco_conn_info = conn_info;
	list_add_tail(&eco->eco_entry, &eci->esc_open_chans);
	list_add_tail(&eco->eco_instance_entry, &esip->esi_open_chans);

	/*
	 * respond back with a handle to access this channel
	 * open instance for later subscriptions, etc.
	 */
open_put:
	saHandleInstancePut(&esip->esi_hdb, handle);
open_return:
	res.ico_head.size = sizeof(res);
	res.ico_head.id = MESSAGE_RES_EVT_OPEN_CHANNEL;
	res.ico_head.error = error;
	res.ico_channel_handle = handle;
	libais_send_response (conn_info, &res, sizeof(res));

	return 0;
}



/*
 * Used by the channel close code and by the implicit close
 * when saEvtFinalize is called with channels open.
 */
static void
common_chan_close(struct event_svr_channel_open	*eco, struct libevt_ci *esip)
{
	struct event_svr_channel_subscr *ecs;
	struct list_head *l, *nxt;

	/* 
	 * TODO: do channel close with the rest of the world 
	 */
	log_printf(LOG_LEVEL_DEBUG, "Close channel %s flags 0x%02x\n", 
			eco->eco_channel->esc_channel_name.value,
			eco->eco_flags);

	/*
	 * Unlink the channel open structure.
	 *
	 * Check for subscriptions and deal with them.  In this case
	 * if there are any, we just implicitly unsubscribe.
	 *
	 * When We're done with the channel open data then we can 
	 * remove it's handle (this frees the memory too).
	 *
	 */
	list_del(&eco->eco_entry);
	list_del(&eco->eco_instance_entry);

	for (l = eco->eco_subscr.next; l != &eco->eco_subscr; l = nxt) {
		nxt = l->next;
		ecs = list_entry(l, struct event_svr_channel_subscr, ecs_entry);
		log_printf(LOG_LEVEL_DEBUG, "Unsubscribe ID: %x\n", ecs->ecs_sub_id);
		list_del(&ecs->ecs_entry);
		free(ecs);
		/*
		 * Purge any pending events associated with this subscription
		 * that don't match another subscription.
		 */
		filter_undelivered_events(eco);
	}

	/*
	 * Remove this channel from the retained event's notion 
	 * of who they have been delivered to.
	 */
	remove_delivered_channel(eco);
}

/*
 * Handler for saEvtChannelClose
 */
static int lib_evt_close_channel(struct conn_info *conn_info, void *message)
{
	struct req_evt_channel_close *req;
	struct res_evt_channel_close res;
	struct event_svr_channel_open	*eco;
	struct libevt_ci *esip = &conn_info->ais_ci.u.libevt_ci;
	SaErrorT error;

	req = message;

	log_printf(LOG_LEVEL_DEBUG, "saEvtChannelClose (Close channel request)\n");
	log_printf(LOG_LEVEL_DEBUG, "handle 0x%x\n", req->icc_channel_handle);

	/*
	 * look up the channel handle
	 */
	error = saHandleInstanceGet(&esip->esi_hdb, 
					req->icc_channel_handle, (void**)&eco);

	if (error != SA_OK) {
		goto chan_close_done;
	}

	common_chan_close(eco, esip);
	saHandleDestroy(&esip->esi_hdb, req->icc_channel_handle);
	saHandleInstancePut(&esip->esi_hdb, req->icc_channel_handle);

chan_close_done:
	res.icc_head.size = sizeof(res);
	res.icc_head.id = MESSAGE_RES_EVT_CLOSE_CHANNEL;
	res.icc_head.error = error;
	libais_send_response (conn_info, &res, sizeof(res));

	return 0;
}

/*
 * Subscribe to an event channel.
 *
 * - First look up the channel to subscribe.
 * - Make sure that the subscription ID is not already in use.
 * - Fill in the subscription data structures and add them to the channels
 *      subscription list.
 * - See if there are any events with retetion times that need to be delivered
 *      because of the new subscription.
 */
static char *filter_types[] = {
	"INVALID FILTER TYPE",
	"SA_EVT_PREFIX_FILTER",
	"SA_EVT_SUFFIX_FILTER",
	"SA_EVT_EXACT_FILTER",
	"SA_EVT_PASS_ALL_FILTER",
};

/*
 * saEvtEventSubscribe Handler
 */
static int lib_evt_event_subscribe(struct conn_info *conn_info, void *message)
{
	struct req_evt_event_subscribe *req;
	struct res_evt_event_subscribe res;
	SaEvtEventFilterArrayT *filters;
	SaErrorT error = SA_OK;
	struct event_svr_channel_open	*eco;
	struct event_svr_channel_instance *eci;
	struct event_svr_channel_subscr *ecs;
	struct event_data *evt;
	struct libevt_ci *esip = &conn_info->ais_ci.u.libevt_ci;
	struct list_head *l;
	int i;

	req = message;

	log_printf(LOG_LEVEL_DEBUG, "saEvtEventSubscribe (Subscribe request)\n");
	log_printf(LOG_LEVEL_DEBUG, "subscription Id: 0x%x\n", req->ics_sub_id);

	error = evtfilt_to_aisfilt(req, &filters);

	if (error == SA_OK) {
		log_printf(LOG_LEVEL_DEBUG, "Subscribe filters count %d\n", 
				filters->filtersNumber);
		for (i = 0; i < filters->filtersNumber; i++) {
			log_printf(LOG_LEVEL_DEBUG, "type %s(%d) sz %d, <%s>\n", 
					filter_types[filters->filters[i].filterType],
					filters->filters[i].filterType,
					filters->filters[i].filter.patternSize,
					(filters->filters[i].filter.patternSize) 
						? (char *)filters->filters[i].filter.pattern
						: "");
		}
	}

	if (error != SA_OK) {
		goto subr_done;
	}

	/*
	 * look up the channel handle
	 */
	error = saHandleInstanceGet(&esip->esi_hdb, 
						req->ics_channel_handle, (void**)&eco);
	if (error != SA_OK) {
		goto subr_done;
	}
	
	eci = eco->eco_channel;

	/*
	 * See if the id is already being used
	 */
	ecs = find_subscr(eco, req->ics_sub_id); 
	if (ecs) {
		error = SA_ERR_EXIST;
		goto subr_put;
	}

	ecs = (struct event_svr_channel_subscr *)malloc(sizeof(*ecs));
	if (!ecs) {
		error = SA_ERR_NO_MEMORY;
		goto subr_put;
	}
	ecs->ecs_filters = filters;
	ecs->ecs_sub_id = req->ics_sub_id;
	list_init(&ecs->ecs_entry);
	list_add(&ecs->ecs_entry, &eco->eco_subscr);


	res.ics_head.size = sizeof(res);
	res.ics_head.id = MESSAGE_RES_EVT_SUBSCRIBE;
	res.ics_head.error = error;
	libais_send_response (conn_info, &res, sizeof(res));

	/*
	 * See if an existing event with a retention time
	 * needs to be delivered based on this subscription
	 */
	for (l = retained_list.next; l != &retained_list; l = l->next) {
		evt = list_entry(l, struct event_data, ed_retained);
		log_printf(LOG_LEVEL_DEBUG,
			"Checking event ID %llx chanp %p -- sub chanp %p\n",
			evt->ed_event.led_event_id, evt->ed_my_chan, eci);
		if (evt->ed_my_chan == eci) {
			if (evt_already_delivered(evt, eco)) {
				continue;
			}
			if (event_match(evt, ecs) == SA_OK) {
				log_printf(LOG_LEVEL_DEBUG,
					"deliver event ID: 0x%llx\n", 
						evt->ed_event.led_event_id);
				deliver_event(evt, eco, ecs);
			}
		}
	}
	saHandleInstancePut(&esip->esi_hdb, req->ics_channel_handle);
	return 0;

subr_put:
	saHandleInstancePut(&esip->esi_hdb, req->ics_channel_handle);
subr_done:
	res.ics_head.size = sizeof(res);
	res.ics_head.id = MESSAGE_RES_EVT_SUBSCRIBE;
	res.ics_head.error = error;
	libais_send_response (conn_info, &res, sizeof(res));
	
	return 0;
}

/*
 * saEvtEventUnsubscribe Handler
 */
static int lib_evt_event_unsubscribe(struct conn_info *conn_info, 
		void *message)
{
	struct req_evt_event_unsubscribe *req;
	struct res_evt_event_unsubscribe res;
	struct event_svr_channel_open	*eco;
	struct event_svr_channel_instance *eci;
	struct event_svr_channel_subscr *ecs;
	struct libevt_ci *esip = &conn_info->ais_ci.u.libevt_ci;
	SaErrorT error = SA_OK;

	req = message;

	log_printf(LOG_LEVEL_DEBUG, 
					"saEvtEventUnsubscribe (Unsubscribe request)\n");
	log_printf(LOG_LEVEL_DEBUG, "subscription Id: 0x%x\n", req->icu_sub_id);

	/*
	 * look up the channel handle, get the open channel
	 * data.
	 */
	error = saHandleInstanceGet(&esip->esi_hdb, 
						req->icu_channel_handle, (void**)&eco);
	if (error != SA_OK) {
		goto unsubr_done;
	}
	
	eci = eco->eco_channel;

	/*
	 * Make sure that the id exists.
	 */
	ecs = find_subscr(eco, req->icu_sub_id); 
	if (!ecs) {
		error = SA_ERR_INVALID_PARAM;
		goto unsubr_put;
	}

	list_del(&ecs->ecs_entry);

	log_printf(LOG_LEVEL_DEBUG, 
			"unsubscribe from channel %s subscription ID 0x%x "
			"with %d filters\n", 
			eci->esc_channel_name.value,
			ecs->ecs_sub_id, ecs->ecs_filters->filtersNumber);

	free_filters(ecs->ecs_filters);
	free(ecs);

unsubr_put:
	saHandleInstancePut(&esip->esi_hdb, req->icu_channel_handle);
unsubr_done:
	res.icu_head.size = sizeof(res);
	res.icu_head.id = MESSAGE_RES_EVT_UNSUBSCRIBE;
	res.icu_head.error = error;
	libais_send_response (conn_info, &res, sizeof(res));
	
	return 0;
}

/*
 * saEvtEventPublish Handler
 */
static int lib_evt_event_publish(struct conn_info *conn_info, void *message)
{
	struct lib_event_data *req;
	struct res_evt_event_publish res;
	struct libevt_ci *esip = &conn_info->ais_ci.u.libevt_ci;
	struct event_svr_channel_open	*eco;
	struct event_svr_channel_instance *eci;
	SaEvtEventIdT event_id = 0;
	SaErrorT error = SA_OK;
	struct iovec pub_iovec;
	int result;


	req = message;

	log_printf(LOG_LEVEL_DEBUG, "saEvtEventPublish (Publish event request)\n");


	/*
	 * look up and validate open channel info
	 */
	error = saHandleInstanceGet(&esip->esi_hdb, 
						req->led_svr_channel_handle, (void**)&eco);
	if (error != SA_OK) {
		goto pub_done;
	}

	eci = eco->eco_channel;

	/*
	 * modify the request structure for sending event data to subscribed
	 * processes.
	 */
	get_event_id(&event_id);
	req->led_head.id = MESSAGE_REQ_EXEC_EVT_EVENTDATA;
	req->led_chan_name = eci->esc_channel_name;
	req->led_event_id = event_id;

	/*
	 * Distribute the event.
	 * The multicasted event will be picked up and delivered
	 * locally by the local network event receiver.
	 */
	pub_iovec.iov_base = req;
	pub_iovec.iov_len = req->led_head.size;
	result = gmi_mcast (&aisexec_groupname, &pub_iovec, 1, GMI_PRIO_LOW);
	if (result != 0) {
			error = SA_ERR_SYSTEM;
	}

	saHandleInstancePut(&esip->esi_hdb, req->led_svr_channel_handle);
pub_done:
	res.iep_head.size = sizeof(res);
	res.iep_head.id = MESSAGE_RES_EVT_PUBLISH;
	res.iep_head.error = error;
	res.iep_event_id = event_id;
	libais_send_response (conn_info, &res, sizeof(res));

	return 0;
}

/*
 * saEvtEventRetentionTimeClear handler
 */
static int lib_evt_event_clear_retentiontime(struct conn_info *conn_info, 
				void *message)
{
	struct req_evt_event_clear_retentiontime *req;
	struct res_evt_event_clear_retentiontime res;
	struct req_evt_chan_command cpkt;
	struct iovec rtn_iovec;
	SaErrorT error = SA_OK;
	int ret;

	req = message;

	log_printf(LOG_LEVEL_DEBUG, 
		"saEvtEventRetentionTimeClear (Clear event retentiontime request)\n");
	log_printf(LOG_LEVEL_DEBUG, 
		"event ID 0x%llx, chan handle 0x%x\n",
			req->iec_event_id,
			req->iec_channel_handle);

	/*
	 * TODO: Add clear retention time code here
	 */
	memset(&cpkt, 0, sizeof(cpkt));
	cpkt.chc_head.id = MESSAGE_REQ_EXEC_EVT_CHANCMD;
	cpkt.chc_head.size = sizeof(cpkt);
	cpkt.chc_op = MESSAGE_REQ_EVT_CLEAR_RETENTIONTIME;
	cpkt.u.chc_event_id = req->iec_event_id;
	rtn_iovec.iov_base = &cpkt;
	rtn_iovec.iov_len = cpkt.chc_head.size;
	ret = gmi_mcast (&aisexec_groupname, &rtn_iovec, 1, GMI_PRIO_MED);
	if (ret != 0) {
			error = SA_ERR_SYSTEM;
	}

	res.iec_head.size = sizeof(res);
	res.iec_head.id = MESSAGE_REQ_EVT_CLEAR_RETENTIONTIME;
	res.iec_head.error = error;
	libais_send_response (conn_info, &res, sizeof(res));

	return 0;
}

/*
 * Send requested event data to the application
 */
static int lib_evt_event_data_get(struct conn_info *conn_info, void *message)
{
	struct lib_event_data res;
	struct libevt_ci *esip = &conn_info->ais_ci.u.libevt_ci;
	struct chan_event_list *cel;
	struct event_data *edp;
	int i;


	/*
	 * Deliver events in publish order within priority
	 */
	for (i = SA_EVT_HIGHEST_PRIORITY; i <= SA_EVT_LOWEST_PRIORITY; i++) {
		if (!list_empty(&esip->esi_events[i])) {
			cel = list_entry(esip->esi_events[i].next, struct chan_event_list, 
						cel_entry);
			list_del(&cel->cel_entry);
			list_init(&cel->cel_entry);
			esip->esi_nevents--;
			if (esip->esi_queue_blocked && 
					(esip->esi_nevents < MIN_EVT_QUEUE_RESUME)) {
				esip->esi_queue_blocked = 0;
				log_printf(LOG_LEVEL_DEBUG, "unblock\n");
			}
#ifdef EVT_EVENT_LIST_CHECK
			if (esip->esi_nevents < 0) {
				log_printf(LOG_LEVEL_NOTICE, "event count went negative\n");
				if (!list_empty(&esip->esi_events[i])) {
					log_printf(LOG_LEVEL_NOTICE, "event list isn't empty\n");
				}
				esip->esi_nevents = 0;
			}
#endif
			edp = cel->cel_event;
			edp->ed_event.led_lib_channel_handle = cel->cel_chan_handle;
			edp->ed_event.led_sub_id = cel->cel_sub_id;
			edp->ed_event.led_head.id = MESSAGE_RES_EVT_EVENT_DATA;
			edp->ed_event.led_head.error = SA_OK;
			free(cel);
			libais_send_response(conn_info, &edp->ed_event, 
											edp->ed_event.led_head.size);
			free_event_data(edp);
			goto data_get_done;
		} 
	}

	res.led_head.size = sizeof(res.led_head);
	res.led_head.id = MESSAGE_RES_EVT_EVENT_DATA;
	res.led_head.error = SA_ERR_NOT_EXIST;
	libais_send_response(conn_info, &res, res.led_head.size);

	/*
	 * See if there are any events that the app doesn't know about
	 * because the notify pipe was full.
	 */
data_get_done:
	if (esip->esi_nevents) {
		__notify_event(conn_info);
	}
	return 0;
}

/*
 * Called when there is a configuration change in the cluster.
 * This function looks at any joiners and leavers and updates the evt
 * node list.  The node list is used to keep track of event IDs
 * received for each node for the detection of duplicate events.
 */
static int evt_conf_change(
	enum gmi_configuration_type configuration_type,	
	struct sockaddr_in *member_list, int member_list_entries,
	struct sockaddr_in *left_list, int left_list_entries,
	struct sockaddr_in *joined_list, int joined_list_entries)
{
	struct in_addr my_node = {SA_CLM_LOCAL_NODE_ID};
	SaClmClusterNodeT *cn;
#ifdef NO_DUPLICATES
	static int first = 1;
	struct sockaddr_in *add_list;
	SaErrorT error;
	int add_count;

	log_printf(LOG_LEVEL_DEBUG, "Evt conf change\n");
	log_printf(LOG_LEVEL_DEBUG, "m %d, j %d, l %d\n", 
					member_list_entries,
					joined_list_entries,
					left_list_entries);
	/*
	 * Don't seem to be able to tell who joined if we're just coming up. Not all
	 * nodes show up in the join list.  If this is the first time through,
	 * choose the members list to use to add nodes, after that use the join
	 * list.  ALways use the left list for removing nodes.
	 */
	if (first) {
			add_list = member_list;
			add_count = member_list_entries;
			first = 0;
	} else {
			add_list = joined_list;
			add_count = joined_list_entries;
	}

	while (add_count--) {
			log_printf(LOG_LEVEL_DEBUG, 
						"Look up Cluster node for %s\n",
						inet_ntoa(add_list->sin_addr));
			cn = clm_get_by_nodeid(add_list->sin_addr);
			if (!cn) {
				log_printf(LOG_LEVEL_DEBUG, 
							"No Cluster node found for %s\n",
							inet_ntoa(add_list->sin_addr));
			} else {
				log_printf(LOG_LEVEL_DEBUG, "Adding node: %s(0x%x)\n",
								cn->nodeName.value, cn->nodeId);
				error = evt_add_node(cn);
				if (error != SA_OK) {
					log_printf(LOG_LEVEL_DEBUG, 
						"Can't add Cluster node at %s\n",
								inet_ntoa(add_list->sin_addr));
				}
			}
			cn++;
	}

	while (left_list_entries--) {
			log_printf(LOG_LEVEL_DEBUG, 
						"Look up Cluster node for %s\n",
						inet_ntoa(left_list->sin_addr));
			cn = clm_get_by_nodeid(left_list->sin_addr);
			if (!cn) {
				log_printf(LOG_LEVEL_DEBUG, 
					"No Cluster node found for %s\n",
						inet_ntoa(left_list->sin_addr));
			} else {
				log_printf(LOG_LEVEL_DEBUG, "Removing node: %s(0x%x)\n",
								cn->nodeName.value, cn->nodeId);
				error = evt_remove_node(cn);
				if (error != SA_OK) {
					log_printf(LOG_LEVEL_DEBUG, 
						"Can't add Cluster node at %s\n",
								inet_ntoa(left_list->sin_addr));
				}
			}
			cn++;
	}
#endif

	/*
	 * Set the base event id
	 */
	if (!base_id) {
		cn = clm_get_by_nodeid(my_node);
		log_printf(LOG_LEVEL_DEBUG, "My node ID 0x%x\n");
		set_event_id(cn->nodeId);
	}

	if (configuration_type == GMI_CONFIGURATION_REGULAR) {
		gmi_recovery_plug_unplug (evt_recovery_plug_handle);
	}
	return 0;
}

/*
 * saEvtFinalize Handler
 */
static int evt_finalize(struct conn_info *conn_info)
{

	struct libevt_ci *esip = &conn_info->ais_ci.u.libevt_ci;
	struct event_svr_channel_open	*eco;
	struct list_head *l, *nxt;

	log_printf(LOG_LEVEL_DEBUG, "saEvtFinalize (Event exit request)\n");
	log_printf(LOG_LEVEL_DEBUG, "saEvtFinalize %d evts on list\n",
			esip->esi_nevents);
	
	/*
	 * Clean up any open channels and associated subscriptions.
	 */
	for (l = esip->esi_open_chans.next; l != &esip->esi_open_chans; l = nxt) {
		nxt = l->next;
		eco = list_entry(l, struct event_svr_channel_open, eco_instance_entry);
		common_chan_close(eco, esip);
		saHandleDestroy(&esip->esi_hdb, eco->eco_my_handle);
	}

#ifdef EVT_EVENT_LIST_CHECK
{
	int i;
	if (esip->esi_nevents) {
		log_printf(LOG_LEVEL_WARNING, 
			"%d Events left on delivery list after finalize\n", 
			esip->esi_nevents);
	}

	for (i = SA_EVT_HIGHEST_PRIORITY; i <= SA_EVT_LOWEST_PRIORITY; i++) {
		if (!list_empty(&esip->esi_events[i])) {
			log_printf(LOG_LEVEL_WARNING, 
				"Events list not empty after finalize\n");
		}
	}
}
#endif

	/*
	 * Delete track entry if there is one
	 */
	list_del (&conn_info->conn_list);

	return 0;
}

/*
 * Called at service start time.
 */
static int evt_exec_init(void)
{

    int res;

	log_printf(LOG_LEVEL_DEBUG, "Evt exec init request\n");
	res = gmi_recovery_plug_create (&evt_recovery_plug_handle);
	if (res != 0) {
		log_printf(LOG_LEVEL_ERROR,
			"Could not create recovery plug for event service.\n");
		return (-1);
	}
	log_printf(LOG_LEVEL_DEBUG, "Evt exec init request\n"); 
	/*
	 * Create an event to be sent when we have to drop messages
	 * for an application.
	 */
	dropped_event_size = sizeof(*dropped_event) + sizeof(dropped_pattern);
	dropped_event = malloc(dropped_event_size);
	if (dropped_event == 0) {
		log_printf(LOG_LEVEL_ERROR, 
					"Memory Allocation Failure, event service not started\n");
		res = gmi_recovery_plug_destroy (evt_recovery_plug_handle);
		errno = ENOMEM;
		return -1;
	}
	memset(dropped_event, 0, sizeof(*dropped_event) + sizeof(dropped_pattern));
	dropped_event->ed_ref_count = 1;
	list_init(&dropped_event->ed_retained);
	dropped_event->ed_event.led_head.size = 
			sizeof(*dropped_event) + sizeof(dropped_pattern);
	dropped_event->ed_event.led_head.error = SA_OK;
	dropped_event->ed_event.led_priority = SA_EVT_HIGHEST_PRIORITY;
	dropped_event->ed_event.led_chan_name = lost_chan;
	dropped_event->ed_event.led_publisher_name = dropped_publisher;
	dropped_event->ed_event.led_patterns_number = 1;
	memcpy(&dropped_event->ed_event.led_body[0], 
					&dropped_pattern, sizeof(dropped_pattern));

	return 0;
}


/*
 * Receive the network event message and distribute it to local subscribers
 */
static int evt_remote_evt(void *msg, struct in_addr source_addr)
{
	/*
	 * - retain events that have a retention time
	 * - Find assocated channel
	 * - Scan list of subscribers
	 * - Apply filters
	 * - Deliver events that pass the filter test
	 */
	struct lib_event_data *evtpkt = msg;
	struct event_svr_channel_instance *eci;
	struct event_svr_channel_open *eco;
	struct event_svr_channel_subscr *ecs;
	struct event_data *evt;
	struct list_head *l, *l1;
	SaClmClusterNodeT *cn;

	log_printf(LOG_LEVEL_DEBUG, "Remote event data received from 0x08%x\n",
					source_addr);

	/*
	 * See where the message came from so that we can set the 
	 * publishing node id in the message before delivery.
	 */
	cn = clm_get_by_nodeid (source_addr);
	if (!cn) {
			/*
			 * TODO: do something here when we can't find the node.
			 */
			log_printf(LOG_LEVEL_DEBUG, "No cluster node for %s\n",
							inet_ntoa(source_addr));
			errno = ENXIO;
			return -1;
	}
	log_printf(LOG_LEVEL_DEBUG, "Cluster node ID 0x%x name %s\n",
					cn->nodeId, cn->nodeName.value);
	evtpkt->led_publisher_node_id = cn->nodeId;

	eci = find_channel(&evtpkt->led_chan_name);

	/*
	 * No one here has this channel open yet.  We can ignore the
	 * message.  When someone does open the channel, any retained messages
	 * will be sent by the originators.
	 */
	if (!eci) {
		return 0;
	}

#ifdef NO_DUPLICATES
	/*
	 * Check for duplicate receipt of message
	 */
	if (is_duplicate_event(evtpkt, cn)) {
		return 0;
	}
#endif

	evt = make_local_event(evtpkt, eci);
	if (!evt) {
		log_printf(LOG_LEVEL_WARNING, 
						"Memory allocation error, can't deliver event\n");
		errno = ENOMEM;
		return -1;
	}
		
	if (evt->ed_event.led_retention_time) {
		retain_event(evt);
	}

	/*
	 * Check open channels
	 */
	for (l = eci->esc_open_chans.next; l != &eci->esc_open_chans; l = l->next) {
		eco = list_entry(l, struct event_svr_channel_open, eco_entry);
		/*
		 * See if enabled to receive
		 */
		if (!(eco->eco_flags & SA_EVT_CHANNEL_SUBSCRIBER)) {
				continue;
		}

		/*
		 * Check subscriptions
		 */
		for (l1 = eco->eco_subscr.next; l1 != &eco->eco_subscr; l1 = l1->next) {
			ecs = list_entry(l1, struct event_svr_channel_subscr, ecs_entry);
			/*
			 * Apply filter rules and deliver if patterns
			 * match filters.
			 * Only deliver one event per open channel
			 */
			if (event_match(evt, ecs) == SA_OK) {
				deliver_event(evt, eco, ecs);
				break;
			}
		}
	}
	free_event_data(evt);


	return 0;
}

/*
 * Receive and process remote event operations.
 * Used to communicate channel opens/closes, clear retention time.
 */
static int evt_remote_chan_op(void *msg, struct in_addr source_addr)
{
	struct req_evt_chan_command *cpkt = msg;
	struct in_addr local_node = {SA_CLM_LOCAL_NODE_ID};
	SaClmClusterNodeT *cn, *my_node;
	struct event_svr_channel_instance *eci;


	log_printf(LOG_LEVEL_DEBUG, "Remote channel operation request\n");
	my_node = clm_get_by_nodeid(local_node);
	cn = clm_get_by_nodeid(source_addr);

	/* 
	 * can ignore messages from me.
	if (my_node->nodeId == cn->nodeId) {
			return 0;
	}
	 */

	switch (cpkt->chc_op) {
	case MESSAGE_REQ_EVT_OPEN_CHANNEL:
		log_printf(LOG_LEVEL_DEBUG, "Creating channel %s for node 0x%x\n",
						cpkt->u.chc_chan.value, cn->nodeId);
		eci = find_channel(&cpkt->u.chc_chan);

		/*
		 * If found, either there was a race opening a channel or
		 * a node joined after a channel was created.  We need to send
		 * him our retained messages to bring him up to date.
		 */
		if (eci) {
			send_retained(&cpkt->u.chc_chan, cn->nodeId);
			break;
		}

		eci = create_channel(&cpkt->u.chc_chan);
		if (!eci) {
				log_printf(LOG_LEVEL_WARNING, "Could not create channel %s\n",
								&cpkt->u.chc_chan.value);
		}

		break;
	case MESSAGE_REQ_EVT_CLOSE_CHANNEL:
		break;
	case MESSAGE_REQ_EVT_CLEAR_RETENTIONTIME:
		log_printf(LOG_LEVEL_DEBUG, "Clear retention time request %llx\n",
				cpkt->u.chc_event_id);	
		clear_retention_time(cpkt->u.chc_event_id);
		break;
	default:
		log_printf(LOG_LEVEL_NOTICE, "Invalid channel operation %d\n",
						cpkt->chc_op);
		break;
	}

	return 0;
}
