/*
 * Copyright (c) 2009, Allied Telesis Labs, New Zealand.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld (angus.salkeld@gmail.com)
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
#include <time.h>
#include <arpa/inet.h>
#include <inttypes.h>

#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include <corosync/coroipcc.h>
#include <corosync/corodefs.h>
#include <corosync/mar_gen.h>
#include <corosync/swab.h>
#include <corosync/list.h>
#include <corosync/hdb.h>
#include <corosync/engine/coroapi.h>
#include <corosync/engine/logsys.h>
#include <corosync/lcr/lcr_comp.h>

#include "../include/saAis.h"
#include "../include/saNtf.h"
#include "../include/ipc_ntf.h"
#include "../include/mar_ntf.h"
#include "ntf.h"

LOGSYS_DECLARE_SUBSYS ("NTF");

static struct corosync_api_v1 *api;

static int ntf_exec_init_fn (struct corosync_api_v1 *);
static int ntf_lib_init_fn (void *conn);
static int ntf_lib_exit_fn (void *conn);
static SaAisErrorT ntf_state_change_notification_alloc (
		SaNtfHandleT ntfHandle,
		SaNtfStateChangeNotificationT_3 * notification,
		SaUint16T numCorrelatedNotifications,
		SaUint16T lengthAdditionalText,
		SaUint16T numAdditionalInfo,
		SaUint16T numStateChanges,
		SaInt16T variableDataSize);
static SaAisErrorT ntf_notification_free (SaNtfNotificationHandleT notificationHandle);
static SaAisErrorT ntf_notification_send (SaNtfNotificationHandleT notificationHandle);
static void message_handler_req_lib_ntf_send (void *conn, const void *msg);
static void message_handler_req_lib_ntf_flt_subscribe (void *conn, const void *msg);

enum ntf_message_req_types {
	MESSAGE_REQ_EXEC_NTF_SEND = 0,
};

struct ntf_filter
{
	struct list_head list;
	struct ntf_pd *private;
	mar_ntf_filter_misc_t *request;
};
static int ntf_filter_match (struct ntf_filter *filter, mar_ntf_misc_notification_t *ntf);

static void message_handler_req_exec_ntf_send (const void *msg, unsigned int nodeid);

static struct corosync_lib_handler ntf_lib_engine[] =
{
	{
		.lib_handler_fn		= message_handler_req_lib_ntf_send,
		.flow_control		= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{
		.lib_handler_fn		= message_handler_req_lib_ntf_flt_subscribe,
		.flow_control		= CS_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
};

static struct corosync_exec_handler ntf_exec_engine[] = {
	{
		.exec_handler_fn = message_handler_req_exec_ntf_send,
	},
};

/* functions for AIS services to use to generate notifications */
static struct openais_ntf_services_api_ver1 ntf_service_api_ver1 = {
	.state_change_notification_alloc = ntf_state_change_notification_alloc,
	.notification_free = ntf_notification_free,
	.notification_send = ntf_notification_send,
};

struct ntf_pd {
	struct list_head list;
	void *conn;
};

struct corosync_service_engine ntf_service_engine = {
	.name				= "openais ntf service A.03.01",
	.id					= NTF_SERVICE,
	.private_data_size	= sizeof (struct ntf_pd),
	.flow_control		= CS_LIB_FLOW_CONTROL_NOT_REQUIRED,
	.lib_init_fn		= ntf_lib_init_fn,
	.lib_exit_fn		= ntf_lib_exit_fn,
	.lib_engine			= ntf_lib_engine,
	.lib_engine_count	= sizeof (ntf_lib_engine) / sizeof (struct corosync_lib_handler),
	.exec_init_fn		= ntf_exec_init_fn,
	.exec_dump_fn		= NULL,
	.exec_engine		= ntf_exec_engine,
	.exec_engine_count	= sizeof (ntf_exec_engine) / sizeof (struct corosync_exec_handler),
	.confchg_fn			= NULL,
	.sync_init			= NULL,
	.sync_process		= NULL,
	.sync_activate		= NULL,
	.sync_abort			= NULL,
};

static struct corosync_service_engine *ntf_get_engine_ver0 (void);

static struct corosync_service_engine_iface_ver0 ntf_service_engine_iface = {
	.corosync_get_service_engine_ver0 = ntf_get_engine_ver0
};

static struct lcr_iface openais_ntf_ver0[2] = {
	{
		.name					= "openais_ntf",
		.version				= 0,
		.versions_replace		= 0,
		.versions_replace_count	= 0,
		.dependencies			= 0,
		.dependency_count		= 0,
		.constructor			= NULL,
		.destructor				= NULL,
		.interfaces				= NULL,
	},
	{
		.name					= "openais_ntf_services_api",
		.version				= 0,
		.versions_replace		= 0,
		.versions_replace_count = 0,
		.dependencies			= 0,
		.dependency_count		= 0,
		.constructor			= NULL,
		.destructor				= NULL,
		.interfaces				= NULL
	}
};

DECLARE_LIST_INIT(library_notification_send_listhead);

DECLARE_LIST_INIT(library_filter_listhead);

DECLARE_LIST_INIT(ntf_msg_listhead);
static struct hdb_handle_database ntf_hdb;

struct ntf_msg_entry {
	struct list_head list;
	struct req_lib_ntf_send *ntf;
};

static struct lcr_comp ntf_comp_ver0 = {
	.iface_count	= 2,
	.ifaces		= openais_ntf_ver0
};

static struct corosync_service_engine *ntf_get_engine_ver0 (void)
{
	return (&ntf_service_engine);
}

__attribute__ ((constructor)) static void register_this_component (void)
{
	lcr_interfaces_set (&openais_ntf_ver0[0], &ntf_service_engine_iface);
	lcr_interfaces_set (&openais_ntf_ver0[1], &ntf_service_api_ver1);
	lcr_component_register (&ntf_comp_ver0);
}

static int ntf_exec_init_fn (struct corosync_api_v1 *corosync_api)
{
	api = corosync_api;
	//log_printf (LOGSYS_LEVEL_DEBUG, "%s\n", __func__);

	hdb_create (&ntf_hdb);

	return (0);
}

static int ntf_lib_init_fn (void *conn)
{
	struct ntf_pd *ntf_pd = (struct ntf_pd *)api->ipc_private_data_get (conn);
	log_printf (LOGSYS_LEVEL_DEBUG, "Got request to initalize notification service.\n");

	list_init (&ntf_pd->list);
	ntf_pd->conn = conn;

	return (0);
}

static int ntf_lib_exit_fn (void *conn)
{
	struct ntf_pd *ntf_pd = (struct ntf_pd *)api->ipc_private_data_get (conn);

	log_printf(LOGSYS_LEVEL_DEBUG, "%s\n", __func__);

	/*
	 * Delete track entry if there is one
	 */
	list_del (&ntf_pd->list);
	ntf_pd->conn = conn;

	return (0);
}

static void ntf_send_notification_to_library (mar_ntf_misc_notification_t *ntf)
{
	struct list_head *list;
	struct ntf_filter * ntf_filter;

	log_printf(LOGSYS_LEVEL_DEBUG, "%s\n", __func__);
	for (list = library_filter_listhead.next;
			list != &library_filter_listhead;
			list = list->next) {

		ntf_filter = list_entry (list, struct ntf_filter, list);
		if (ntf_filter_match (ntf_filter, ntf) == 0) {

			api->ipc_dispatch_send (ntf_filter->private->conn,
					ntf,
					ntf->header.size);
		}
	}
}

typedef struct  {
	coroipc_request_header_t header;
	mar_message_source_t source;
} req_exec_ntf_wrapper_t ;

static void message_handler_req_exec_ntf_send (const void *msg, unsigned int nodeid)
{
	res_lib_ntf_send_t res;
	req_exec_ntf_wrapper_t *wrapper = (req_exec_ntf_wrapper_t *)msg;
	mar_ntf_misc_notification_t *ntf = ((char*)msg + sizeof (req_exec_ntf_wrapper_t));

	ENTER ();


	if (api->ipc_source_is_local (&wrapper->source)) {

		res.header.id = MESSAGE_RES_NTF_SEND;
		res.header.size = sizeof (res_lib_ntf_send_t);
		res.header.error = SA_AIS_OK;
		api->ipc_response_send (wrapper->source.conn, &res,
				sizeof (res_lib_ntf_send_t));
	}
	ntf_send_notification_to_library (ntf);
}

/**
 * receive a notification from the application
 */
static void message_handler_req_lib_ntf_send (void *conn, const void *msg)
{
	struct iovec iov[2];
	mar_ntf_misc_notification_t *ntf = msg;
	req_exec_ntf_wrapper_t wrapper;

	ENTER();

	log_printf(LOGSYS_LEVEL_DEBUG, "size:%d, %s %s\n",
		ntf->header.size,
		ntf->notification_header.notification_object.value,
		ntf->notification_header.notifying_object.value);

	wrapper.header.id = SERVICE_ID_MAKE (NTF_SERVICE, MESSAGE_REQ_EXEC_NTF_SEND);
	wrapper.header.size = sizeof (req_exec_ntf_wrapper_t);
	api->ipc_source_set (&wrapper.source, conn);

	iov[0].iov_base = &wrapper;
	iov[0].iov_len = sizeof (req_exec_ntf_wrapper_t);
	iov[1].iov_base = ntf;
	iov[1].iov_len = ntf->header.size;

	assert (api->totem_mcast (iov, 2, TOTEM_AGREED) == 0);

}

static int ntf_filter_match (struct ntf_filter *filter, mar_ntf_misc_notification_t *ntf)
{
	mar_ntf_filter_header_t *fh = &filter->request->filter_header;
	mar_uint32_t *et;
	mar_ntf_class_id_t *cid;
	mar_name_t *name;
	SaBoolT match;
	int i;

	ENTER();

	match = SA_FALSE;
	et = (mar_uint32_t*)((char*)&fh->event_types + fh->event_types.arrayVal.arrayOffset);
	for (i = 0; i < fh->event_types.arrayVal.numElements; i++) {
		if (et[i] == ntf->notification_header.event_type) {
			log_printf(LOGSYS_LEVEL_DEBUG, "event type matches\n");
			match = SA_TRUE;
		}
 	}
	if (match == SA_FALSE && fh->event_types.arrayVal.numElements > 0) return 1;

	match = SA_FALSE;
	cid = (mar_ntf_class_id_t *)((char*)&fh->notification_class_ids + fh->notification_class_ids.arrayVal.arrayOffset);
	for (i = 0; i < fh->notification_class_ids.arrayVal.numElements; i++) {
		if (cid[i].vendor_id == ntf->notification_header.notification_class_id.vendor_id &&
			cid[i].major_id == ntf->notification_header.notification_class_id.major_id &&
			cid[i].minor_id == ntf->notification_header.notification_class_id.minor_id) {
			match = SA_TRUE;
			log_printf(LOGSYS_LEVEL_DEBUG, "class id matches\n");
		}
 	}
	if (match == SA_FALSE && fh->notification_class_ids.arrayVal.numElements > 0) return 1;

	match = SA_FALSE;
	name = (mar_name_t *)((char*)&fh->notification_objects + fh->notification_objects.arrayVal.arrayOffset);
	for (i = 0; i < fh->notification_objects.arrayVal.numElements; i++) {
		if (mar_name_match (&name[i], &ntf->notification_header.notification_object)) {
			match = SA_TRUE;
			log_printf(LOGSYS_LEVEL_DEBUG, "notification_object matches\n");
		}
 	}
	if (match == SA_FALSE && fh->notification_objects.arrayVal.numElements > 0) return 1;

	match = SA_FALSE;
	name = (mar_name_t *)((char*)&fh->notifying_objects + fh->notifying_objects.arrayVal.arrayOffset);
	for (i = 0; i < fh->notifying_objects.arrayVal.numElements; i++) {
		if (mar_name_match (&name[i], &ntf->notification_header.notifying_object)) {
			match = SA_TRUE;
			log_printf(LOGSYS_LEVEL_DEBUG, "notifying_object matches\n");
		}
 	}
	if (match == SA_FALSE && fh->notifying_objects.arrayVal.numElements > 0) return 1;

	return 0;
}


static void ntf_print_filter_misc (mar_ntf_filter_misc_t *flt_pt)
{
	 
	log_printf (LOGSYS_LEVEL_DEBUG, "header.id:   %d\n", flt_pt->header.id);
	log_printf (LOGSYS_LEVEL_DEBUG, "header.size: %d\n", flt_pt->header.size);

	log_printf (LOGSYS_LEVEL_DEBUG, "filter_header.type: %d\n", flt_pt->filter_header.type);

	log_printf (LOGSYS_LEVEL_DEBUG, "event_types.arrayVal.arrayOffset: %d\n", flt_pt->filter_header.event_types.arrayVal.arrayOffset);
	log_printf (LOGSYS_LEVEL_DEBUG, "event_types.arrayVal.numElements: %d\n", flt_pt->filter_header.event_types.arrayVal.numElements);
	log_printf (LOGSYS_LEVEL_DEBUG, "event_types.arrayVal.elementSize: %d\n", flt_pt->filter_header.event_types.arrayVal.elementSize);

	log_printf (LOGSYS_LEVEL_DEBUG, "notification_objects.arrayVal.arrayOffset: %d\n",
				flt_pt->filter_header.notification_objects.arrayVal.arrayOffset);
	log_printf (LOGSYS_LEVEL_DEBUG, "notification_objects.arrayVal.numElements: %d\n",
				flt_pt->filter_header.notification_objects.arrayVal.numElements);
	log_printf (LOGSYS_LEVEL_DEBUG, "notification_objects.arrayVal.elementSize: %d\n",
				flt_pt->filter_header.notification_objects.arrayVal.elementSize);

	log_printf (LOGSYS_LEVEL_DEBUG, "notifying_objects.arrayVal.arrayOffset: %d\n",
				flt_pt->filter_header.notifying_objects.arrayVal.arrayOffset);
	log_printf (LOGSYS_LEVEL_DEBUG, "notifying_objects.arrayVal.numElements: %d\n",
				flt_pt->filter_header.notifying_objects.arrayVal.numElements);
	log_printf (LOGSYS_LEVEL_DEBUG, "notifying_objects.arrayVal.elementSize: %d\n",
				flt_pt->filter_header.notifying_objects.arrayVal.elementSize);

	log_printf (LOGSYS_LEVEL_DEBUG, "notification_class_ids.arrayVal.arrayOffset: %d\n",
				flt_pt->filter_header.notification_class_ids.arrayVal.arrayOffset);
	log_printf (LOGSYS_LEVEL_DEBUG, "notification_class_ids.arrayVal.numElements: %d\n",
				flt_pt->filter_header.notification_class_ids.arrayVal.numElements);
	log_printf (LOGSYS_LEVEL_DEBUG, "notification_class_ids.arrayVal.elementSize: %d\n",
				flt_pt->filter_header.notification_class_ids.arrayVal.elementSize);

}



static void message_handler_req_lib_ntf_flt_subscribe (void *conn, const void *msg)
{
	mar_ntf_filter_misc_t *req_pt = (mar_ntf_filter_misc_t *)msg;
	res_lib_ntf_flt_subscribe_t res;
	struct ntf_filter *flt;

	log_printf(LOGSYS_LEVEL_DEBUG, "%s\n", __func__);

	//ntf_print_filter_misc (req_pt);

	flt = malloc (sizeof (struct ntf_filter));
	flt->request = malloc (req_pt->header.size);
	list_add_tail(&flt->list, &library_filter_listhead);
	flt->private = (struct ntf_pd *)api->ipc_private_data_get (conn);

	memcpy (flt->request, req_pt, req_pt->header.size);

	res.header.id = MESSAGE_RES_NTF_FLT_SUBSCRIBE;
	res.header.size = sizeof (res_lib_ntf_flt_subscribe_t);
	res.header.error = SA_AIS_OK;
	api->ipc_response_send (conn, &res, sizeof (res_lib_ntf_send_t));
}

static SaAisErrorT ntf_state_change_notification_alloc (
		SaNtfHandleT ntfHandle,
		SaNtfStateChangeNotificationT_3 * ntf,
		SaUint16T numCorrelatedNotifications,
		SaUint16T lengthAdditionalText,
		SaUint16T numAdditionalInfo,
		SaUint16T numStateChanges,
		SaInt16T variableDataSize)
{
//	struct notification_holder_s* ntf_pt;
	SaAisErrorT error = SA_AIS_OK;
#if 0
	ntf->notificationHeader.lengthAdditionalText = lengthAdditionalText;
	ntf->notificationHeader.additionalText = malloc (lengthAdditionalText);
	ntf->notificationHeader.numAdditionalInfo = numAdditionalInfo;
	ntf->notificationHeader.additionalInfo = malloc (sizeof (SaNtfAdditionalInfoT) * numAdditionalInfo);
	ntf->notificationHeader.numCorrelatedNotifications = numCorrelatedNotifications;
	ntf->notificationHeader.correlatedNotifications = malloc(sizeof(SaNtfIdentifierT));
	ntf->numStateChanges = numStateChanges;
	ntf->changedStates = malloc (numStateChanges * sizeof(SaNtfStateChangeT_3));

	error = hdb_handle_create (&ntf_hdb, sizeof (struct notification_holder), &ntf->notificationHandle);
	if (error != 0) {
		goto error_exit;
	}

	error = hdb_handle_get (&ntf_hdb, ntf->notificationHandle, (void**)&ntf_pt);
	if (error != 0) {
		hdb_handle_destroy (&ntf_hdb, ntf->notificationHandle);
		goto error_exit;
	}

	ntf_pt->notificationType = SA_NTF_TYPE_STATE_CHANGE;
	ntf_pt->notification_pt.stateChangeNotification = ntf;
error_exit:
#endif
	return (error);
}

SaAisErrorT ntf_notification_free (SaNtfNotificationHandleT notificationHandle)
{
	hdb_handle_put (&ntf_hdb, notificationHandle);
	return hdb_handle_destroy (&ntf_hdb, notificationHandle);
}

SaAisErrorT ntf_notification_send (SaNtfNotificationHandleT notificationHandle)
{
#if 0
	struct notification_holder_s* ntf_pt;
	struct iovec iov[64];
	int num_iovs, i;
	struct req_lib_ntf_send req;
	SaAisErrorT error;
	error = hdb_handle_get (&ntf_hdb, notificationHandle, (void**)&ntf_pt);
	if (error != 0) {
		return error;
	}

	switch (ntf_pt->notificationType)
	{
		case SA_NTF_TYPE_STATE_CHANGE:
			num_iovs = SaNtfNotificationHeaderT_to_iovec (
				&ntf_pt->notification_pt.stateChangeNotification->notificationHeader,
				&req, iov, 64);

			iov[num_iovs].iov_base = &ntf_pt->notification_pt.stateChangeNotification->numStateChanges;
			iov[num_iovs].iov_len = sizeof(mar_uint16_t);
			num_iovs++;
			if (ntf_pt->notification_pt.stateChangeNotification->numStateChanges > 0) {
				iov[num_iovs].iov_base = ntf_pt->notification_pt.stateChangeNotification->changedStates;
				iov[num_iovs].iov_len = (sizeof (SaNtfStateChangeT_3) * ntf_pt->notification_pt.stateChangeNotification->numStateChanges);
				num_iovs++;
			}
			break;

		case SA_NTF_TYPE_MISCELLANEOUS:
			num_iovs = SaNtfNotificationHeaderT_to_iovec (
				&ntf_pt->notification_pt.miscellaneousNotification->notificationHeader,
				&req, iov, 64);
			break;
		case SA_NTF_TYPE_ALARM:
		case SA_NTF_TYPE_SECURITY_ALARM:
		default:
			error = SA_AIS_ERR_INVALID_PARAM;
			goto error_put;
			break;
	}

	req.type = ntf_pt->notificationType;
	req.header.id = MESSAGE_REQ_NTF_SEND;
	req.header.size = 0;
	for (i = 0; i < num_iovs; i++) {
		req.header.size += iov[i].iov_len;
	}
	printf ("%s:%d sending size %d\n",__FILE__,__LINE__, req.header.size);


	struct req_lib_ntf_send *ntf_keeper = malloc(req.header.size);
	struct ntf_msg_entry *entry = malloc(sizeof(struct ntf_msg_entry));

	memcpy(ntf_keeper, &req, req.header.size);
	ntf_keeper->header.id = MESSAGE_RES_NTF_DISPATCH;

	log_printf(LOGSYS_LEVEL_DEBUG, "%s %s %s\n",
		__func__,
		ntf_keeper->notification_header.notification_object.value,
		ntf_keeper->notification_header.notifying_object.value);

	entry->ntf = ntf_keeper;
	list_add_tail(&entry->list, &ntf_msg_listhead);

	ntf_send_notification_to_library (ntf_keeper);

error_put:
	hdb_handle_put (&ntf_hdb, notificationHandle);
	
	return (error);
#endif
	return SA_AIS_OK;
}



