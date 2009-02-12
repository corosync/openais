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

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <arpa/inet.h>

#include <inttypes.h> /* for development only */

#include <corosync/ipc_gen.h>
#include <corosync/mar_gen.h>
#include <corosync/swab.h>
#include <corosync/list.h>
#include <corosync/engine/coroapi.h>
#include <corosync/engine/logsys.h>
#include <corosync/lcr/lcr_comp.h>

#include "../include/saAis.h"
#include "../include/saTmr.h"
#include "../include/ipc_tmr.h"

LOGSYS_DECLARE_SUBSYS ("TMR", LOG_INFO);

struct timer {
	SaTmrTimerIdT timer_id;
	SaTmrTimerAttributesT timer_attributes;
	corosync_timer_handle_t timer_handle;
	mar_message_source_t source;
	SaUint64T expiration_count;
	SaUint64T timer_skip;
	SaTimeT call_time;
	void *timer_data;
	struct list_head list;
};

DECLARE_LIST_INIT(timer_list_head);

static struct corosync_api_v1 *api;

static int tmr_exec_init_fn (struct corosync_api_v1 *);
static int tmr_lib_init_fn (void *conn);
static int tmr_lib_exit_fn (void *conn);

static void message_handler_req_lib_tmr_timerstart (
	void *conn,
	void *msg);

static void message_handler_req_lib_tmr_timerreschedule (
	void *conn,
	void *msg);

static void message_handler_req_lib_tmr_timercancel (
	void *conn,
	void *msg);

static void message_handler_req_lib_tmr_periodictimerskip (
	void *conn,
	void *msg);

static void message_handler_req_lib_tmr_timerremainingtimeget (
	void *conn,
	void *msg);

static void message_handler_req_lib_tmr_timerattributesget (
	void *conn,
	void *msg);

static void message_handler_req_lib_tmr_timeget (
	void *conn,
	void *msg);

static void message_handler_req_lib_tmr_clocktickget (
	void *conn,
	void *msg);

static struct corosync_api_v1 *api;

struct tmr_pd {
	struct list_head timer_list;
	struct list_head timer_cleanup_list;
};

static struct corosync_lib_handler tmr_lib_engine[] =
{
	{
		.lib_handler_fn		= message_handler_req_lib_tmr_timerstart,
		.response_size		= sizeof (struct req_lib_tmr_timerstart),
		.response_id		= MESSAGE_RES_TMR_TIMERSTART,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_tmr_timerreschedule,
		.response_size		= sizeof (struct req_lib_tmr_timerreschedule),
		.response_id		= MESSAGE_RES_TMR_TIMERRESCHEDULE,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_tmr_timercancel,
		.response_size		= sizeof (struct req_lib_tmr_timercancel),
		.response_id		= MESSAGE_RES_TMR_TIMERCANCEL,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_tmr_periodictimerskip,
		.response_size		= sizeof (struct req_lib_tmr_periodictimerskip),
		.response_id		= MESSAGE_RES_TMR_PERIODICTIMERSKIP,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_tmr_timerremainingtimeget,
		.response_size		= sizeof (struct req_lib_tmr_timerremainingtimeget),
		.response_id		= MESSAGE_RES_TMR_TIMERREMAININGTIMEGET,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_tmr_timerattributesget,
		.response_size		= sizeof (struct req_lib_tmr_timerattributesget),
		.response_id		= MESSAGE_RES_TMR_TIMERATTRIBUTESGET,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_tmr_timeget,
		.response_size		= sizeof (struct req_lib_tmr_timeget),
		.response_id		= MESSAGE_RES_TMR_TIMEGET,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_tmr_clocktickget,
		.response_size		= sizeof (struct req_lib_tmr_clocktickget),
		.response_id		= MESSAGE_RES_TMR_CLOCKTICKGET,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
};

struct corosync_service_engine tmr_service_engine = {
	.name			= "openais timer service A.01.01",
	.id			= TMR_SERVICE,
	.private_data_size	= sizeof (struct tmr_pd),
	.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED,
	.lib_init_fn		= tmr_lib_init_fn,
	.lib_exit_fn		= tmr_lib_exit_fn,
	.lib_engine		= tmr_lib_engine,
	.lib_engine_count	= sizeof (tmr_lib_engine) / sizeof (struct corosync_lib_handler),
	.exec_init_fn		= tmr_exec_init_fn,
	.exec_dump_fn		= NULL,
	.exec_engine		= NULL,
	.exec_engine_count	= 0,
	.confchg_fn		= NULL,
	.sync_init		= NULL,
	.sync_process		= NULL,
	.sync_activate		= NULL,
	.sync_abort		= NULL,
};

static struct corosync_service_engine *tmr_get_engine_ver0 (void);

static struct corosync_service_engine_iface_ver0 tmr_service_engine_iface = {
	.corosync_get_service_engine_ver0 = tmr_get_engine_ver0
};

static struct lcr_iface openais_tmr_ver0[1] = {
	{
		.name			= "openais_tmr",
		.version		= 0,
		.versions_replace	= 0,
		.versions_replace_count	= 0,
		.dependencies		= 0,
		.dependency_count	= 0,
		.constructor		= NULL,
		.destructor		= NULL,
		.interfaces		= NULL,
	}
};

static struct lcr_comp tmr_comp_ver0 = {
	.iface_count	= 1,
	.ifaces		= openais_tmr_ver0
};

static struct corosync_service_engine *tmr_get_engine_ver0 (void)
{
	return (&tmr_service_engine);
}

__attribute__ ((constructor)) static void register_this_component (void) {
	lcr_interfaces_set (&openais_tmr_ver0[0], &tmr_service_engine_iface);
	lcr_component_register (&tmr_comp_ver0);
}

static int tmr_exec_init_fn (struct corosync_api_v1 *corosync_api)
{
	api = corosync_api;

	return (0);
}

static int tmr_lib_init_fn (void *conn)
{
	return (0);
}

static int tmr_lib_exit_fn (void *conn)
{
	struct tmr_pd *tmr_pd = (struct tmr_pd *) api->ipc_private_data_get (conn);

	list_init (&tmr_pd->timer_list);
	list_init (&tmr_pd->timer_cleanup_list);

	return (0);
}

SaTimeT tmr_time_now (void)
{
	struct timeval tv;
	SaTimeT time;

	if (gettimeofday (&tv, 0)) {
		return (0ULL);
	}

	time = (SaTimeT)(tv.tv_sec) * 1000000000ULL;
	time += (SaTimeT)(tv.tv_usec) * 1000ULL;

	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "[DEBUG]: tmr_time_now %"PRIu64"\n", time);

	return (time);
}

static void tmr_timer_expired (void *data)
{
	struct timer *timer = (struct timer *)data;
	struct res_lib_tmr_timerexpiredcallback res_lib_tmr_timerexpiredcallback;

	timer->expiration_count += 1;

	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "[DEBUG]: tmr_timer_expired { id=%u }\n",
		    (unsigned int)(timer->timer_id));

	res_lib_tmr_timerexpiredcallback.header.size =
		sizeof (struct res_lib_tmr_timerexpiredcallback);
	res_lib_tmr_timerexpiredcallback.header.id =
		MESSAGE_RES_TMR_TIMEREXPIREDCALLBACK;
	res_lib_tmr_timerexpiredcallback.header.error = SA_AIS_OK; /* FIXME */

	res_lib_tmr_timerexpiredcallback.timer_id = timer->timer_id;
	res_lib_tmr_timerexpiredcallback.timer_data = timer->timer_data;
	res_lib_tmr_timerexpiredcallback.expiration_count = timer->expiration_count;

	if (timer->timer_skip == 0) {
		api->ipc_conn_send_response (
			api->ipc_conn_partner_get (timer->source.conn),
			&res_lib_tmr_timerexpiredcallback,
			sizeof (struct res_lib_tmr_timerexpiredcallback));
	}
	else {
		/* DEBUG */
		log_printf (LOG_LEVEL_NOTICE, "[DEBUG]: skipping timer { id=%u }\n",
			    (unsigned int)(timer->timer_id));

		timer->timer_skip -= 1;
	}

	if (timer->timer_attributes.timerPeriodDuration > 0) {
		switch (timer->timer_attributes.type) {
		case SA_TIME_ABSOLUTE:
			api->timer_add_absolute (
				timer->timer_attributes.timerPeriodDuration,
				(void *)(timer), tmr_timer_expired,
				&timer->timer_handle);
			break;
		case SA_TIME_DURATION:
			api->timer_add_duration (
				timer->timer_attributes.timerPeriodDuration,
				(void *)(timer), tmr_timer_expired,
				&timer->timer_handle);
			break;
		default:
			break;
		}
	}

	return;
}

static struct timer *tmr_find_timer (
	struct list_head *head,
	SaTmrTimerIdT timer_id)
{
	struct list_head *list;
	struct timer *timer;

	for (list = head->next; list != head; list = list->next)
	{
		timer = list_entry (list, struct timer, list);

		if (timer->timer_id == timer_id) {
			return (timer);
		}
	}
	return (0);
}

static void message_handler_req_lib_tmr_timerstart (
	void *conn,
	void *msg)
{
	struct req_lib_tmr_timerstart *req_lib_tmr_timerstart =
		(struct req_lib_tmr_timerstart *)msg;
	struct res_lib_tmr_timerstart res_lib_tmr_timerstart;
	SaAisErrorT error = SA_AIS_OK;
	struct timer *timer = NULL;

	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "LIB request: saTmrTimerStart { id=%u }\n",
		    (unsigned int)(req_lib_tmr_timerstart->timer_id));

	timer = tmr_find_timer (&timer_list_head,
		req_lib_tmr_timerstart->timer_id);
	if (timer == NULL) {
		timer = malloc (sizeof (struct timer));
		if (timer == NULL) {
			error = SA_AIS_ERR_NO_MEMORY;
			goto error_exit;
		}
		memset (timer, 0, sizeof (struct timer));

		timer->timer_id = req_lib_tmr_timerstart->timer_id;
		timer->call_time = req_lib_tmr_timerstart->call_time;

		memcpy (&timer->timer_attributes,
			&req_lib_tmr_timerstart->timer_attributes,
			sizeof (SaTmrTimerAttributesT));

		api->ipc_source_set (&timer->source, conn);

		list_init (&timer->list);
		list_add (&timer->list, &timer_list_head);

		switch (timer->timer_attributes.type)
		{
		case SA_TIME_ABSOLUTE:
			api->timer_add_absolute (
				timer->timer_attributes.initialExpirationTime,
				(void *)(timer), tmr_timer_expired,
				&timer->timer_handle);
			break;
		case SA_TIME_DURATION:
			api->timer_add_duration (
				timer->timer_attributes.initialExpirationTime,
				(void *)(timer), tmr_timer_expired,
				&timer->timer_handle);
			break;
		default:
			/*
			 * This case is handled in the library.
			 */
			break;
		}
	}

error_exit:

	res_lib_tmr_timerstart.header.size =
		sizeof (struct res_lib_tmr_timerstart);
	res_lib_tmr_timerstart.header.id =
		MESSAGE_RES_TMR_TIMERSTART;
	res_lib_tmr_timerstart.header.error = error;

	api->ipc_conn_send_response (conn,
		&res_lib_tmr_timerstart,
		sizeof (struct res_lib_tmr_timerstart));
}

static void message_handler_req_lib_tmr_timerreschedule (
	void *conn,
	void *msg)
{
	struct req_lib_tmr_timerreschedule *req_lib_tmr_timerreschedule =
		(struct req_lib_tmr_timerreschedule *)msg;
	struct res_lib_tmr_timerreschedule res_lib_tmr_timerreschedule;
	SaAisErrorT error = SA_AIS_OK;
	struct timer *timer = NULL;

	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "LIB request: saTmrTimerReschedule { id=%u }\n",
		    (unsigned int)(req_lib_tmr_timerreschedule->timer_id));

	timer = tmr_find_timer (&timer_list_head,
		req_lib_tmr_timerreschedule->timer_id);
	if (timer == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	memcpy (&timer->timer_attributes,
		&req_lib_tmr_timerreschedule->timer_attributes,
		sizeof (SaTmrTimerAttributesT));

error_exit:

	res_lib_tmr_timerreschedule.header.size =
		sizeof (struct res_lib_tmr_timerreschedule);
	res_lib_tmr_timerreschedule.header.id =
		MESSAGE_RES_TMR_TIMERRESCHEDULE;
	res_lib_tmr_timerreschedule.header.error = error;

	api->ipc_conn_send_response (conn,
		&res_lib_tmr_timerreschedule,
		sizeof (struct res_lib_tmr_timerreschedule));
}

static void message_handler_req_lib_tmr_timercancel (
	void *conn,
	void *msg)
{
	struct req_lib_tmr_timercancel *req_lib_tmr_timercancel =
		(struct req_lib_tmr_timercancel *)msg;
	struct res_lib_tmr_timercancel res_lib_tmr_timercancel;
	SaAisErrorT error = SA_AIS_OK;
	struct timer *timer = NULL;

	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "LIB request: saTmrTimerCancel\n");

	timer = tmr_find_timer (&timer_list_head,
		req_lib_tmr_timercancel->timer_id);
	if (timer == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	api->timer_delete (timer->timer_handle);
	list_del (&timer->list);
	free (timer);

error_exit:

	res_lib_tmr_timercancel.header.size =
		sizeof (struct res_lib_tmr_timercancel);
	res_lib_tmr_timercancel.header.id =
		MESSAGE_RES_TMR_TIMERCANCEL;
	res_lib_tmr_timercancel.header.error = error;

	api->ipc_conn_send_response (conn,
		&res_lib_tmr_timercancel,
		sizeof (struct res_lib_tmr_timercancel));
}

static void message_handler_req_lib_tmr_periodictimerskip (
	void *conn,
	void *msg)
{
	struct req_lib_tmr_periodictimerskip *req_lib_tmr_periodictimerskip =
		(struct req_lib_tmr_periodictimerskip *)msg;
	struct res_lib_tmr_periodictimerskip res_lib_tmr_periodictimerskip;
	SaAisErrorT error = SA_AIS_OK;
	struct timer *timer = NULL;

	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "LIB request: saTmrPeriodicTimerSkip { id=%u }\n",
		    (unsigned int)(req_lib_tmr_periodictimerskip->timer_id));

	timer = tmr_find_timer (&timer_list_head,
		req_lib_tmr_periodictimerskip->timer_id);
	if (timer == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (timer->timer_attributes.timerPeriodDuration == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	timer->timer_skip += 1;

error_exit:

	res_lib_tmr_periodictimerskip.header.size =
		sizeof (struct res_lib_tmr_periodictimerskip);
	res_lib_tmr_periodictimerskip.header.id =
		MESSAGE_RES_TMR_PERIODICTIMERSKIP;
	res_lib_tmr_periodictimerskip.header.error = error;

	api->ipc_conn_send_response (conn,
		&res_lib_tmr_periodictimerskip,
		sizeof (struct res_lib_tmr_periodictimerskip));
}

static void message_handler_req_lib_tmr_timerremainingtimeget (
	void *conn,
	void *msg)
{
	struct req_lib_tmr_timerremainingtimeget *req_lib_tmr_timerremainingtimeget =
		(struct req_lib_tmr_timerremainingtimeget *)msg;
	struct res_lib_tmr_timerremainingtimeget res_lib_tmr_timerremainingtimeget;
	SaAisErrorT error = SA_AIS_OK;
	struct timer *timer = NULL;

	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "LIB request: saTmrTimerRemainingTimeGet { id=%u }\n",
		    (unsigned int)(req_lib_tmr_timerremainingtimeget->timer_id));

	timer = tmr_find_timer (&timer_list_head,
		req_lib_tmr_timerremainingtimeget->timer_id);
	if (timer == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

error_exit:

	res_lib_tmr_timerremainingtimeget.header.size =
		sizeof (struct res_lib_tmr_timerremainingtimeget);
	res_lib_tmr_timerremainingtimeget.header.id =
		MESSAGE_RES_TMR_TIMERREMAININGTIMEGET;
	res_lib_tmr_timerremainingtimeget.header.error = error;

	api->ipc_conn_send_response (conn,
		&res_lib_tmr_timerremainingtimeget,
		sizeof (struct res_lib_tmr_timerremainingtimeget));
}

static void message_handler_req_lib_tmr_timerattributesget (
	void *conn,
	void *msg)
{
	struct req_lib_tmr_timerattributesget *req_lib_tmr_timerattributesget =
		(struct req_lib_tmr_timerattributesget *)msg;
	struct res_lib_tmr_timerattributesget res_lib_tmr_timerattributesget;
	SaAisErrorT error = SA_AIS_OK;
	struct timer *timer = NULL;

	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "LIB request: saTmrTimerAttributesGet { id=%u }\n",
		    (unsigned int)(req_lib_tmr_timerattributesget->timer_id));

	timer = tmr_find_timer (&timer_list_head,
		req_lib_tmr_timerattributesget->timer_id);
	if (timer == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	memcpy (&res_lib_tmr_timerattributesget.timer_attributes,
		&timer->timer_attributes, sizeof (SaTmrTimerAttributesT));

error_exit:

	res_lib_tmr_timerattributesget.header.size =
		sizeof (struct res_lib_tmr_timerattributesget);
	res_lib_tmr_timerattributesget.header.id =
		MESSAGE_RES_TMR_TIMERATTRIBUTESGET;
	res_lib_tmr_timerattributesget.header.error = error;

	api->ipc_conn_send_response (conn,
		&res_lib_tmr_timerattributesget,
		sizeof (struct res_lib_tmr_timerattributesget));
}

static void message_handler_req_lib_tmr_timeget (
	void *conn,
	void *msg)
{
	struct req_lib_tmr_timeget *req_lib_tmr_timeget =
		(struct req_lib_tmr_timeget *)msg;
	struct res_lib_tmr_timeget res_lib_tmr_timeget;
	SaAisErrorT error = SA_AIS_OK;
	SaTimeT current_time;

	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "LIB request: saTmrTimeGet\n");

	current_time = tmr_time_now();

	memcpy (&res_lib_tmr_timeget.current_time,
		&current_time, sizeof (SaTimeT));

error_exit:

	res_lib_tmr_timeget.header.size =
		sizeof (struct res_lib_tmr_timeget);
	res_lib_tmr_timeget.header.id =
		MESSAGE_RES_TMR_TIMEGET;
	res_lib_tmr_timeget.header.error = error;

	api->ipc_conn_send_response (conn,
		&res_lib_tmr_timeget,
		sizeof (struct res_lib_tmr_timeget));
}

static void message_handler_req_lib_tmr_clocktickget (
	void *conn,
	void *msg)
{
	struct req_lib_tmr_clocktickget *req_lib_tmr_clocktickget =
		(struct req_lib_tmr_clocktickget *)msg;
	struct res_lib_tmr_clocktickget res_lib_tmr_clocktickget;
	SaAisErrorT error = SA_AIS_OK;
	SaTimeT clock_tick;

	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "LIB request: saTmrClockTickGet\n");

	clock_tick = (SaTimeT)((1.0 / CLOCKS_PER_SEC) * 1000000000ULL);

	memcpy (&res_lib_tmr_clocktickget.clock_tick,
		&clock_tick, sizeof (SaTimeT));

error_exit:

	res_lib_tmr_clocktickget.header.size =
		sizeof (struct res_lib_tmr_clocktickget);
	res_lib_tmr_clocktickget.header.id =
		MESSAGE_RES_TMR_CLOCKTICKGET;
	res_lib_tmr_clocktickget.header.error = error;

	api->ipc_conn_send_response (conn,
		&res_lib_tmr_clocktickget,
		sizeof (struct res_lib_tmr_clocktickget));
}
