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

#include <inttypes.h>

#include <corosync/ipc_gen.h>
#include <corosync/mar_gen.h>
#include <corosync/swab.h>
#include <corosync/list.h>
#include <corosync/hdb.h>
#include <corosync/engine/coroapi.h>
#include <corosync/engine/logsys.h>
#include <corosync/lcr/lcr_comp.h>

#include "../include/saAis.h"
#include "../include/saTmr.h"
#include "../include/ipc_tmr.h"

LOGSYS_DECLARE_SUBSYS ("TMR", LOG_INFO);

struct timer_instance {
	SaTmrTimerIdT timer_id;
	SaTmrTimerAttributesT timer_attributes;
	SaUint64T expiration_count;
	SaUint64T timer_skip;
	SaTimeT call_time;
	corosync_timer_handle_t timer_handle;
	mar_message_source_t source;
	void *timer_data;
	struct list_head cleanup_list;
};

static struct hdb_handle_database timer_hdb = {
	.handle_count	= 0,
	.handles	= 0,
	.iterator	= 0,
	.mutex		= PTHREAD_MUTEX_INITIALIZER
};

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
	struct tmr_pd *tmr_pd = (struct tmr_pd *) api->ipc_private_data_get (conn);

	list_init (&tmr_pd->timer_list);
	list_init (&tmr_pd->timer_cleanup_list);

	return (0);
}

static int tmr_lib_exit_fn (void *conn)
{
	struct timer_instance *timer_instance;
	struct list_head *cleanup_list;
	struct tmr_pd *tmr_pd = (struct tmr_pd *) api->ipc_private_data_get (conn);

	cleanup_list = tmr_pd->timer_cleanup_list.next;

	while (!list_empty (&tmr_pd->timer_cleanup_list))
	{
		timer_instance = list_entry (cleanup_list, struct timer_instance, cleanup_list);

		/* DEBUG */
		log_printf (LOG_LEVEL_NOTICE, "[DEBUG]: cleanup timer { id=0x%04x }\n",
			    (unsigned int)(timer_instance->timer_id));

		api->timer_delete (timer_instance->timer_handle);

		list_del (&timer_instance->cleanup_list);

		hdb_handle_destroy (&timer_hdb, timer_instance->timer_id);
		hdb_handle_put (&timer_hdb, timer_instance->timer_id);

		cleanup_list = tmr_pd->timer_cleanup_list.next;
	}

	return (0);
}

static void tmr_timer_expired (void *data)
{
	struct timer_instance *timer_instance = (struct timer_instance *)data;
	struct res_lib_tmr_timerexpiredcallback res_lib_tmr_timerexpiredcallback;

	timer_instance->expiration_count += 1;

	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "[DEBUG]: tmr_timer_expired { id=0x%04x }\n",
		    (unsigned int)(timer_instance->timer_id));

	res_lib_tmr_timerexpiredcallback.header.size =
		sizeof (struct res_lib_tmr_timerexpiredcallback);
	res_lib_tmr_timerexpiredcallback.header.id =
		MESSAGE_RES_TMR_TIMEREXPIREDCALLBACK;
	res_lib_tmr_timerexpiredcallback.header.error = SA_AIS_OK; /* FIXME */

	res_lib_tmr_timerexpiredcallback.timer_id = timer_instance->timer_id;
	res_lib_tmr_timerexpiredcallback.timer_data = timer_instance->timer_data;
	res_lib_tmr_timerexpiredcallback.expiration_count = timer_instance->expiration_count;

	if (timer_instance->timer_skip == 0) {
		api->ipc_dispatch_send (
			timer_instance->source.conn,
			&res_lib_tmr_timerexpiredcallback,
			sizeof (struct res_lib_tmr_timerexpiredcallback));
	}
	else {
		/* DEBUG */
		log_printf (LOG_LEVEL_NOTICE, "[DEBUG]: skipping timer { id=0x%04x }\n",
			    (unsigned int)(timer_instance->timer_id));

		timer_instance->timer_skip -= 1;
	}

	if (timer_instance->timer_attributes.timerPeriodDuration > 0) {
		switch (timer_instance->timer_attributes.type) {
		/*
		 * We don't really need a switch statement here,
		 * since periodic timers will always get recreated
		 * as duration timers.
		 */
		case SA_TIME_ABSOLUTE:
			api->timer_add_absolute (
				timer_instance->timer_attributes.timerPeriodDuration,
				(void *)(timer_instance), tmr_timer_expired,
				&timer_instance->timer_handle);
			break;
		case SA_TIME_DURATION:
			api->timer_add_duration (
				timer_instance->timer_attributes.timerPeriodDuration,
				(void *)(timer_instance), tmr_timer_expired,
				&timer_instance->timer_handle);
			break;
		default:
			break;
		}
	}

	return;
}

static void message_handler_req_lib_tmr_timerstart (
	void *conn,
	void *msg)
{
	struct req_lib_tmr_timerstart *req_lib_tmr_timerstart =
		(struct req_lib_tmr_timerstart *)msg;
	struct res_lib_tmr_timerstart res_lib_tmr_timerstart;
	struct timer_instance *timer_instance = NULL;
	SaAisErrorT error = SA_AIS_OK;

	hdb_handle_t timer_id;
	struct tmr_pd *tmr_pd = (struct tmr_pd *) api->ipc_private_data_get (conn);

	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "LIB request: saTmrTimerStart\n");

	hdb_handle_create (&timer_hdb,
		   sizeof (struct timer_instance),
		   &timer_id);

	hdb_handle_get (&timer_hdb, timer_id,
		(void *)&timer_instance);

	if (timer_instance == NULL) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_exit;
	}

	timer_instance->timer_id = timer_id;
	timer_instance->timer_data = req_lib_tmr_timerstart->timer_data;

	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "[DEBUG]:\t timer_data=%p\n",
		    (void *)(timer_instance->timer_data));

	memcpy (&timer_instance->timer_attributes,
		&req_lib_tmr_timerstart->timer_attributes,
		sizeof (SaTmrTimerAttributesT));

	api->ipc_source_set (&timer_instance->source, conn);

	list_init (&timer_instance->cleanup_list);

	switch (timer_instance->timer_attributes.type)
	{
	case SA_TIME_ABSOLUTE:
		api->timer_add_absolute (
			timer_instance->timer_attributes.initialExpirationTime,
			(void *)(timer_instance), tmr_timer_expired,
			&timer_instance->timer_handle);
		break;
	case SA_TIME_DURATION:
		api->timer_add_duration (
			timer_instance->timer_attributes.initialExpirationTime,
			(void *)(timer_instance), tmr_timer_expired,
			&timer_instance->timer_handle);
		break;
	default:
		/*
		 * This case is handled in the library.
		 */
		break;
	}

	list_add (&timer_instance->cleanup_list, &tmr_pd->timer_cleanup_list);

error_exit:

	res_lib_tmr_timerstart.header.size =
		sizeof (struct res_lib_tmr_timerstart);
	res_lib_tmr_timerstart.header.id =
		MESSAGE_RES_TMR_TIMERSTART;
	res_lib_tmr_timerstart.header.error = error;

	res_lib_tmr_timerstart.timer_id = timer_id;
	res_lib_tmr_timerstart.call_time = (SaTimeT)(api->timer_time_get());

	api->ipc_response_send (conn,
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
	struct timer_instance *timer_instance = NULL;
	SaAisErrorT error = SA_AIS_OK;
	SaTimeT current_time = 0;

	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "LIB request: saTmrTimerReschedule { id=0x%04x }\n",
		    (unsigned int)(req_lib_tmr_timerreschedule->timer_id));

	hdb_handle_get (&timer_hdb,
		(hdb_handle_t)(req_lib_tmr_timerreschedule->timer_id),
		(void *)&timer_instance);
	if (timer_instance == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	current_time = (SaTimeT)(api->timer_time_get());

	if (current_time > req_lib_tmr_timerreschedule->timer_attributes.initialExpirationTime) {
		error = SA_AIS_ERR_INVALID_PARAM;
		goto error_put;
	}

	if (timer_instance->timer_attributes.timerPeriodDuration != 0) {
		if (req_lib_tmr_timerreschedule->timer_attributes.timerPeriodDuration <= 0) {
			error = SA_AIS_ERR_INVALID_PARAM;
			goto error_put;
		}
	}
	else {
		if (req_lib_tmr_timerreschedule->timer_attributes.timerPeriodDuration != 0) {
			error = SA_AIS_ERR_INVALID_PARAM;
			goto error_put;
		}
	}

	memcpy (&timer_instance->timer_attributes,
		&req_lib_tmr_timerreschedule->timer_attributes,
		sizeof (SaTmrTimerAttributesT));

error_put:

	hdb_handle_put (&timer_hdb, (hdb_handle_t)(timer_instance->timer_id));

error_exit:

	res_lib_tmr_timerreschedule.header.size =
		sizeof (struct res_lib_tmr_timerreschedule);
	res_lib_tmr_timerreschedule.header.id =
		MESSAGE_RES_TMR_TIMERRESCHEDULE;
	res_lib_tmr_timerreschedule.header.error = error;

	api->ipc_response_send (conn,
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
	struct timer_instance *timer_instance = NULL;
	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "LIB request: saTmrTimerCancel { id=0x%04x }\n",
		    (unsigned int)(req_lib_tmr_timercancel->timer_id));

	hdb_handle_get (&timer_hdb,
		(hdb_handle_t)(req_lib_tmr_timercancel->timer_id),
		(void *)&timer_instance);
	if (timer_instance == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	res_lib_tmr_timercancel.timer_data = timer_instance->timer_data;

	api->timer_delete (timer_instance->timer_handle);

	list_del (&timer_instance->cleanup_list);

	hdb_handle_destroy (&timer_hdb, (hdb_handle_t)(timer_instance->timer_id));
	hdb_handle_put (&timer_hdb, (hdb_handle_t)(timer_instance->timer_id));

error_exit:

	res_lib_tmr_timercancel.header.size =
		sizeof (struct res_lib_tmr_timercancel);
	res_lib_tmr_timercancel.header.id =
		MESSAGE_RES_TMR_TIMERCANCEL;
	res_lib_tmr_timercancel.header.error = error;

	api->ipc_response_send (conn,
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
	struct timer_instance *timer_instance = NULL;
	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "LIB request: saTmrPeriodicTimerSkip { id=0x%04x }\n",
		    (unsigned int)(req_lib_tmr_periodictimerskip->timer_id));

	hdb_handle_get (&timer_hdb,
		(hdb_handle_t)(req_lib_tmr_periodictimerskip->timer_id),
		(void *)&timer_instance);
	if (timer_instance == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	if (timer_instance->timer_attributes.timerPeriodDuration == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	timer_instance->timer_skip += 1;

	hdb_handle_put (&timer_hdb, (hdb_handle_t)(timer_instance->timer_id));

error_exit:

	res_lib_tmr_periodictimerskip.header.size =
		sizeof (struct res_lib_tmr_periodictimerskip);
	res_lib_tmr_periodictimerskip.header.id =
		MESSAGE_RES_TMR_PERIODICTIMERSKIP;
	res_lib_tmr_periodictimerskip.header.error = error;

	api->ipc_response_send (conn,
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
	struct timer_instance *timer_instance = NULL;
	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "LIB request: saTmrTimerRemainingTimeGet { id=0x%04x }\n",
		    (unsigned int)(req_lib_tmr_timerremainingtimeget->timer_id));

	hdb_handle_get (&timer_hdb,
		(hdb_handle_t)(req_lib_tmr_timerremainingtimeget->timer_id),
		(void *)&timer_instance);
	if (timer_instance == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	res_lib_tmr_timerremainingtimeget.remaining_time =
		(SaTimeT)((api->timer_expire_time_get(timer_instance->timer_handle)) -
			  (api->timer_time_get()));

	hdb_handle_put (&timer_hdb, (hdb_handle_t)(timer_instance->timer_id));

error_exit:

	res_lib_tmr_timerremainingtimeget.header.size =
		sizeof (struct res_lib_tmr_timerremainingtimeget);
	res_lib_tmr_timerremainingtimeget.header.id =
		MESSAGE_RES_TMR_TIMERREMAININGTIMEGET;
	res_lib_tmr_timerremainingtimeget.header.error = error;

	api->ipc_response_send (conn,
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
	struct timer_instance *timer_instance = NULL;
	SaAisErrorT error = SA_AIS_OK;

	/* DEBUG */
	log_printf (LOG_LEVEL_NOTICE, "LIB request: saTmrTimerAttributesGet { id=0x%04x }\n",
		    (unsigned int)(req_lib_tmr_timerattributesget->timer_id));

	hdb_handle_get (&timer_hdb,
		(hdb_handle_t)(req_lib_tmr_timerattributesget->timer_id),
		(void *)&timer_instance);
	if (timer_instance == NULL) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	memcpy (&res_lib_tmr_timerattributesget.timer_attributes,
		&timer_instance->timer_attributes,
		sizeof (SaTmrTimerAttributesT));

	hdb_handle_put (&timer_hdb, (hdb_handle_t)(timer_instance->timer_id));

error_exit:

	res_lib_tmr_timerattributesget.header.size =
		sizeof (struct res_lib_tmr_timerattributesget);
	res_lib_tmr_timerattributesget.header.id =
		MESSAGE_RES_TMR_TIMERATTRIBUTESGET;
	res_lib_tmr_timerattributesget.header.error = error;

	api->ipc_response_send (conn,
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

	current_time = (SaTimeT)(api->timer_time_get());

	memcpy (&res_lib_tmr_timeget.current_time,
		&current_time, sizeof (SaTimeT));

	res_lib_tmr_timeget.header.size =
		sizeof (struct res_lib_tmr_timeget);
	res_lib_tmr_timeget.header.id =
		MESSAGE_RES_TMR_TIMEGET;
	res_lib_tmr_timeget.header.error = error;

	api->ipc_response_send (conn,
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

	api->ipc_response_send (conn,
		&res_lib_tmr_clocktickget,
		sizeof (struct res_lib_tmr_clocktickget));
}
