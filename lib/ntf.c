/*
 * Copyright (c) 2009 Allied Telesis Labs, New Zealand.
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

#include <assert.h>
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

#include <saAis.h>
#include <saNtf.h>
#include "../include/ipc_ntf.h"
#include "../include/mar_ntf.h"
#include "util.h"

struct ntf_message_overlay {
	coroipc_request_header_t header __attribute__((aligned(8)));
	char data[4096];
};

void ntfHandleInstanceDestructor (void *instance);

DECLARE_HDB_DATABASE(ntfHandleDatabase,NULL);

static SaVersionT ntfVersionsSupported[] = {
	{ 'A', 3, 1 }
};

static struct saVersionDatabase ntfVersionDatabase = {
	sizeof (ntfVersionsSupported) / sizeof (SaVersionT),
	ntfVersionsSupported
};

void ntfHandleInstanceDestructor (void *instance)
{
/*
	ntf_instance_t *ntfInstance = instance;

	pthread_mutex_destroy (&ntfInstance->response_mutex);
	pthread_mutex_destroy (&ntfInstance->dispatch_mutex);
*/
}

SaAisErrorT
saNtfInitialize_3 (
		SaNtfHandleT *ntfHandle,
		const SaNtfCallbacksT_3 *callbacks,
		SaVersionT *version)
{
	ntf_instance_t *ntfInstance;
	SaAisErrorT error = SA_AIS_OK;

	if (ntfHandle == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = saVersionVerify (&ntfVersionDatabase, version);
	if (error != SA_AIS_OK) {
		goto error_no_destroy;
	}

	error = hdb_error_to_sa (hdb_handle_create (&ntfHandleDatabase,
		sizeof (ntf_instance_t), ntfHandle));
	if (error != SA_AIS_OK) {
		goto error_no_destroy;
	}

	error = hdb_error_to_sa (hdb_handle_get (&ntfHandleDatabase,
		*ntfHandle, (void *)&ntfInstance));
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	error = coroipcc_service_connect (
		COROSYNC_SOCKET_NAME,
		NTF_SERVICE,
		IPC_REQUEST_SIZE,
		IPC_RESPONSE_SIZE,
		IPC_DISPATCH_SIZE,
		&ntfInstance->ipc_handle);
	if (error != SA_AIS_OK) {
		goto error_put_destroy;
		printf("%s -> error!\n",__func__);
	}

	if (callbacks != NULL) {
		memcpy (&ntfInstance->callbacks, callbacks, sizeof (SaNtfCallbacksT_3));
	} else {
		memset (&ntfInstance->callbacks, 0, sizeof (SaNtfCallbacksT_3));
	}

	ntfInstance->ntf_handle = *ntfHandle;

	hdb_handle_put (&ntfHandleDatabase, *ntfHandle);

	return (SA_AIS_OK);

		printf("%s -> error!\n",__func__);
error_put_destroy:
	hdb_handle_put (&ntfHandleDatabase, *ntfHandle);
error_destroy:
	hdb_handle_destroy (&ntfHandleDatabase, *ntfHandle);
error_no_destroy:
	return (error);
}

SaAisErrorT
saNtfSelectionObjectGet (
	const SaNtfHandleT ntfHandle,
	SaSelectionObjectT *selectionObject)
{
	ntf_instance_t *ntfInstance;
	SaAisErrorT error = SA_AIS_OK;
	int fd;

	if (selectionObject == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_sa (hdb_handle_get (&ntfHandleDatabase,
		ntfHandle, (void *)&ntfInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	error = coroipcc_fd_get (ntfInstance->ipc_handle, &fd);

	*selectionObject = fd;

	hdb_handle_put (&ntfHandleDatabase, ntfHandle);

	return (error);
}

SaAisErrorT
saNtfDispatch (
	SaNtfHandleT ntfHandle,
	SaDispatchFlagsT dispatchFlags)
{
	SaAisErrorT error = SA_AIS_OK;
	ntf_instance_t *ntfInstance;
	SaNtfCallbacksT_3 callbacks;
	SaNtfNotificationsT_3 ntf;
	coroipc_response_header_t *dispatch_data;
	mar_ntf_misc_notification_t *mar_hdr;
	int timeout = 1;
	int cont = 1;

	if (dispatchFlags != SA_DISPATCH_ONE &&
	    dispatchFlags != SA_DISPATCH_ALL &&
	    dispatchFlags != SA_DISPATCH_BLOCKING)
	{
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_sa (hdb_handle_get (&ntfHandleDatabase,
		ntfHandle, (void *)&ntfInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	if (dispatchFlags == SA_DISPATCH_ALL) {
		timeout = 0;
	}

	do {

		error = coroipcc_dispatch_get (
			ntfInstance->ipc_handle,
			(void **)&dispatch_data,
			timeout);
		if (error != CS_OK) {
			goto error_put;
		}

		if (dispatch_data == NULL) {
			if (dispatchFlags == CPG_DISPATCH_ALL) {
				break;
			} else {
				continue;
			}
		}

		memcpy (&callbacks, &ntfInstance->callbacks,
			sizeof (ntfInstance->callbacks));

		switch (dispatch_data->id) {
			case MESSAGE_RES_NTF_DISPATCH:
				if (callbacks.saNtfNotificationCallback == NULL) {
					printf ("[DEBUG]: %s:%d callback null\n", __func__, __LINE__);
					continue;
				}
				mar_hdr = (mar_ntf_misc_notification_t *)dispatch_data;
				unmar_notification_header (&mar_hdr->notification_header,
					&ntf.notification.miscellaneousNotification.notificationHeader,
					(char*)mar_hdr + sizeof (mar_ntf_misc_notification_t));

				callbacks.saNtfNotificationCallback (34, &ntf);
				break;
			default:
				printf ("[DEBUG]: %s:%d default switch(%d) \n",
					 __func__, __LINE__, dispatch_data->id);
				break;
		}
		coroipcc_dispatch_put (ntfInstance->ipc_handle);

		switch (dispatchFlags)
		{
		case SA_DISPATCH_ONE:
			cont = 0;
			break;
		case SA_DISPATCH_ALL:
			break;
		case SA_DISPATCH_BLOCKING:
			break;
		}
	} while (cont);

error_put:
	hdb_handle_put (&ntfHandleDatabase, ntfHandle);
error_exit:
	return (error);
}

SaAisErrorT
saNtfFinalize (
	SaNtfHandleT ntfHandle)
{
	ntf_instance_t *ntfInstance;
	SaAisErrorT error = SA_AIS_OK;

	error = hdb_error_to_sa (hdb_handle_get (&ntfHandleDatabase,
		ntfHandle, (void *)&ntfInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	if (ntfInstance->finalize) {
		hdb_handle_put (&ntfHandleDatabase, ntfHandle);
		return (SA_AIS_ERR_BAD_HANDLE);
	}

	ntfInstance->finalize = 1;

	/* ntfInstanceFinalize (ntfInstance); */

	error = coroipcc_service_disconnect (ntfInstance->ipc_handle); /* ? */

	hdb_handle_put (&ntfHandleDatabase, ntfHandle);

	return (SA_AIS_OK);
}



SaAisErrorT
saNtfMiscellaneousNotificationAllocate (
		SaNtfHandleT ntfHandle,
		SaNtfMiscellaneousNotificationT * ntf,
		SaUint16T numCorrelatedNotifications,
		SaUint16T lengthAdditionalText,
		SaUint16T numAdditionalInfo,
		SaInt16T variableDataSize)
{
	ntf_instance_t *ntfInstance;
	notification_holder_t *holder;
	mar_ntf_notification_header_t *hdr;
	SaAisErrorT error;
	int size = 0;
	int cur_offset = 0;

	error = hdb_error_to_sa (hdb_handle_get (&ntfHandleDatabase,
		ntfHandle, (void *)&ntfInstance));
	if (error != SA_AIS_OK) {
		return error;
	}
	error = hdb_error_to_sa (hdb_handle_create (&ntfHandleDatabase,
				sizeof (notification_holder_t), &ntf->notificationHandle));
	if (error != SA_AIS_OK) {
		hdb_error_to_sa (hdb_handle_put (&ntfHandleDatabase, ntfHandle));
		return error;
	}
	error = hdb_error_to_sa (hdb_handle_get (&ntfHandleDatabase,
		ntf->notificationHandle, (void *)&holder));
	if (error != SA_AIS_OK) {
		hdb_handle_put (&ntfHandleDatabase, ntfHandle);
		hdb_handle_destroy (&ntfHandleDatabase, ntf->notificationHandle);
		return error;
	}
	holder->notificationType = SA_NTF_TYPE_MISCELLANEOUS;
	holder->ntfInstance = ntfInstance;
	holder->sa_notification.misc = ntf;

	ntf->notificationHeader.numCorrelatedNotifications = numCorrelatedNotifications;
	ntf->notificationHeader.lengthAdditionalText = lengthAdditionalText;
	ntf->notificationHeader.numAdditionalInfo = numAdditionalInfo;

	size  = sizeof (mar_ntf_misc_notification_t);
	size += sizeof (SaNtfIdentifierT) * numCorrelatedNotifications;
	size += sizeof (char) * lengthAdditionalText;
	size += sizeof (mar_ntf_attribute_t) * numAdditionalInfo;

	error = coroipcc_zcb_alloc (ntfInstance->ipc_handle,
		(void**)&holder->notification.misc,
		size, 0);
	if (error != CS_OK)
		goto exit_put;

	holder->notification.misc->header.size = size;
	holder->notification.misc->header.id = MESSAGE_REQ_NTF_SEND;
	hdr = &holder->notification.misc->notification_header;

	cur_offset = mar_notification_header (hdr, &ntf->notificationHeader,
			((char*)holder->notification.misc + sizeof (mar_ntf_misc_notification_t)));
exit_put:
	hdb_handle_put (&ntfHandleDatabase, ntf->notificationHandle);
	hdb_handle_put (&ntfHandleDatabase, ntfHandle);
	return SA_AIS_OK;

}

SaAisErrorT
saNtfStateChangeNotificationAllocate_3 (
		SaNtfHandleT ntfHandle,
		SaNtfStateChangeNotificationT_3 * ntf,
		SaUint16T numCorrelatedNotifications,
		SaUint16T lengthAdditionalText,
		SaUint16T numAdditionalInfo,
		SaUint16T numStateChanges,
		SaInt16T variableDataSize)
{
	ntf_instance_t *ntfInstance;
	notification_holder_t *holder;
	mar_ntf_notification_header_t *hdr;
	SaAisErrorT error;
	int size = 0;
	int cur_offset = 0;

	error = hdb_error_to_sa (hdb_handle_get (&ntfHandleDatabase,
		ntfHandle, (void *)&ntfInstance));
	if (error != SA_AIS_OK) {
		return error;
	}
	error = hdb_error_to_sa (hdb_handle_create (&ntfHandleDatabase,
				sizeof (notification_holder_t), &ntf->notificationHandle));
	if (error != SA_AIS_OK) {
		hdb_handle_put (&ntfHandleDatabase, ntfHandle);
		return error;
	}
	error = hdb_error_to_sa (hdb_handle_get (&ntfHandleDatabase, ntf->notificationHandle, (void *)&holder));
	if (error != SA_AIS_OK) {
		hdb_handle_put (&ntfHandleDatabase, ntfHandle);
		hdb_handle_destroy (&ntfHandleDatabase, ntf->notificationHandle);
		return error;
	}
	holder->notificationType = SA_NTF_TYPE_STATE_CHANGE;
	holder->ntfInstance = ntfInstance;
	holder->sa_notification.state = ntf;

	ntf->notificationHeader.numCorrelatedNotifications = numCorrelatedNotifications;
	ntf->notificationHeader.lengthAdditionalText = lengthAdditionalText;
	ntf->notificationHeader.numAdditionalInfo = numAdditionalInfo;
	ntf->numStateChanges = numStateChanges;

	size  = sizeof (mar_ntf_state_notification_t);
	size += sizeof (SaNtfIdentifierT) * numCorrelatedNotifications;
	size += sizeof (char) * lengthAdditionalText;
	size += sizeof (mar_ntf_attribute_t) * numAdditionalInfo;
	
	size += sizeof (mar_ntf_state_change_t) * numStateChanges;

	error = coroipcc_zcb_alloc (ntfInstance->ipc_handle,
		(void**)&holder->notification.state,
		size, 0);
	if (error != CS_OK)
		goto exit_put;

	holder->notification.state->header.size = size;
	holder->notification.state->header.id = MESSAGE_REQ_NTF_SEND;
	hdr = &holder->notification.state->notification_header;

	cur_offset = mar_notification_header (hdr, &ntf->notificationHeader,
			((char*)holder->notification.state + sizeof (mar_ntf_misc_notification_t)));

	ntf->sourceIndicator = &holder->notification.state->source_indicator;

	ntf->changedStates = malloc (numStateChanges * sizeof (SaNtfStateChangeT_3));

	holder->notification.state->changed_states.arrayVal.numElements = numStateChanges;
	holder->notification.state->changed_states.arrayVal.elementSize = sizeof (mar_ntf_state_change_t);
	holder->notification.state->changed_states.arrayVal.arrayOffset = cur_offset;

exit_put:
	hdb_handle_put (&ntfHandleDatabase, ntf->notificationHandle);
	hdb_handle_put (&ntfHandleDatabase, ntfHandle);
	return SA_AIS_OK;
}


SaAisErrorT
saNtfObjectCreateDeleteNotificationAllocate (
		SaNtfHandleT ntfHandle,
		SaNtfObjectCreateDeleteNotificationT * ntf,
		SaUint16T numCorrelatedNotifications,
		SaUint16T lengthAdditionalText,
		SaUint16T numAdditionalInfo,
		SaUint16T numAttributes,
		SaInt16T variableDataSize)
{
	ntf_instance_t *ntfInstance;
	notification_holder_t *holder;
	mar_ntf_notification_header_t *hdr;
	SaAisErrorT error;
	int size = 0;
	int cur_offset = 0;

	error = hdb_error_to_sa (hdb_handle_get (&ntfHandleDatabase,
		ntfHandle, (void *)&ntfInstance));
	if (error != SA_AIS_OK) {
		return error;
	}
	error = hdb_error_to_sa (hdb_handle_create (&ntfHandleDatabase,
				sizeof (notification_holder_t), &ntf->notificationHandle));
	if (error != SA_AIS_OK) {
		hdb_handle_put (&ntfHandleDatabase, ntfHandle);
		return error;
	}
	error = hdb_error_to_sa (hdb_handle_get (&ntfHandleDatabase, ntf->notificationHandle, (void *)&holder));
	if (error != SA_AIS_OK) {
		hdb_handle_put (&ntfHandleDatabase, ntfHandle);
		hdb_handle_destroy (&ntfHandleDatabase, ntf->notificationHandle);
		return error;
	}
	ntf->notificationHeader.numCorrelatedNotifications = numCorrelatedNotifications;
	ntf->notificationHeader.lengthAdditionalText = lengthAdditionalText;
	ntf->notificationHeader.numAdditionalInfo = numAdditionalInfo;
//	ntf->numStateChanges = numStateChanges;

//	ntf->changedStates = malloc (numStateChanges * sizeof (SaNtfStateChangeT_3));

	holder->notificationType = SA_NTF_TYPE_STATE_CHANGE;
	holder->ntfInstance = ntfInstance;

	size  = sizeof (mar_ntf_object_notification_t);
	size += sizeof (SaNtfIdentifierT) * numCorrelatedNotifications;
	size += sizeof (char) * lengthAdditionalText;
	size += sizeof (SaNtfAdditionalInfoT) * numAdditionalInfo;
//	size += sizeof (SaNtfStateChangeT_3) * numStateChanges;

	holder->notification.object = malloc (size);

	holder->notification.object->header.size = size;
	holder->notification.object->header.id = MESSAGE_REQ_NTF_SEND;
	hdr = &holder->notification.object->notification_header;

	cur_offset = mar_notification_header (hdr, &ntf->notificationHeader,
			((char*)holder->notification.object + sizeof (mar_ntf_object_notification_t)));

//	ntf->sourceIndicator = &holder->notification.object->source_indicator;
/*
	holder->notification.state->changed_states.arrayVal.numElements = numStateChanges;
	holder->notification.state->changed_states.arrayVal.elementSize = sizeof (SaNtfStateChangeT_3);
	holder->notification.state->changed_states.arrayVal.arrayOffset = cur_offset;

*/
	hdb_handle_put (&ntfHandleDatabase, ntf->notificationHandle);
	hdb_handle_put (&ntfHandleDatabase, ntfHandle);
	return SA_AIS_OK;
}


SaAisErrorT
saNtfAlarmNotificationAllocate (
		SaNtfHandleT ntfHandle,
		SaNtfAlarmNotificationT * ntf,
		SaUint16T numCorrelatedNotifications,
		SaUint16T lengthAdditionalText,
		SaUint16T numAdditionalInfo,
		SaUint16T numSpecificProblems,
		SaUint16T numMonitoredAttributes,
		SaUint16T numProposedRepairActions,
		SaInt16T variableDataSize)
{
	ntf_instance_t *ntfInstance;
	notification_holder_t *holder;
	mar_ntf_notification_header_t *hdr;
	SaAisErrorT error;
	int size = 0;
	int cur_offset = 0;

	error = hdb_error_to_sa (hdb_handle_get (&ntfHandleDatabase,
		ntfHandle, (void *)&ntfInstance));
	if (error != SA_AIS_OK) {
		return error;
	}
	error = hdb_error_to_sa (hdb_handle_create (&ntfHandleDatabase,
				sizeof (notification_holder_t), &ntf->notificationHandle));
	if (error != SA_AIS_OK) {
		hdb_handle_put (&ntfHandleDatabase, ntfHandle);
		return error;
	}
	error = hdb_error_to_sa (hdb_handle_get (&ntfHandleDatabase, ntf->notificationHandle, (void *)&holder));
	if (error != SA_AIS_OK) {
		hdb_handle_put (&ntfHandleDatabase, ntfHandle);
		hdb_handle_destroy (&ntfHandleDatabase, ntf->notificationHandle);
		return error;
	}
	holder->notificationType = SA_NTF_TYPE_STATE_CHANGE;
	holder->ntfInstance = ntfInstance;
	holder->sa_notification.alarm = ntf;

	ntf->notificationHeader.numCorrelatedNotifications = numCorrelatedNotifications;
	ntf->notificationHeader.lengthAdditionalText = lengthAdditionalText;
	ntf->notificationHeader.numAdditionalInfo = numAdditionalInfo;

	ntf->numSpecificProblems = numSpecificProblems;
	ntf->numMonitoredAttributes = numMonitoredAttributes;
	ntf->numProposedRepairActions = numProposedRepairActions;



	size  = sizeof (mar_ntf_state_notification_t);
	size += sizeof (SaNtfIdentifierT) * numCorrelatedNotifications;
	size += sizeof (char) * lengthAdditionalText;
	size += sizeof (mar_ntf_attribute_t) * numAdditionalInfo;
	
//	size += sizeof (mar_ntf_state_change_t) * numStateChanges;

	error = coroipcc_zcb_alloc (ntfInstance->ipc_handle,
		(void**)&holder->notification.alarm,
		size, 0);
	if (error != CS_OK)
		goto exit_put;

	holder->notification.alarm->header.size = size;
	holder->notification.alarm->header.id = MESSAGE_REQ_NTF_SEND;
	hdr = &holder->notification.alarm->notification_header;

	cur_offset = mar_notification_header (hdr, &ntf->notificationHeader,
			((char*)holder->notification.alarm + sizeof (mar_ntf_alarm_notification_t)));



	ntf->probableCause = &holder->notification.alarm->probable_cause;
//    ntf->specificProblems = malloc (sizeof (SaNtfSpecificProblemT) *numSpecificProblems);
    ntf->perceivedSeverity = &holder->notification.alarm->perceived_severity;
    ntf->trend = &holder->notification.alarm->trend;
//    ntf->thresholdInformation = malloc (sizeof (SaNtfThresholdInformationT));
//	ntf->monitoredAttributes = malloc (sizeof (SaNtfAttributeT) * numMonitoredAttributes);
//    ntf->proposedRepairActions = malloc (sizeof (SaNtfProposedRepairActionT) * numProposedRepairActions);

/*

	holder->notification.alarm->changed_states.arrayVal.numElements = numStateChanges;
	holder->notification.alarm->changed_states.arrayVal.elementSize = sizeof (mar_ntf_state_change_t);
	holder->notification.alarm->changed_states.arrayVal.arrayOffset = cur_offset;
*/
exit_put:
	hdb_handle_put (&ntfHandleDatabase, ntf->notificationHandle);
	hdb_handle_put (&ntfHandleDatabase, ntfHandle);
	return SA_AIS_OK;

}

SaAisErrorT
saNtfNotificationFree (SaNtfNotificationHandleT notificationHandle)
{
	SaAisErrorT error;
	notification_holder_t* ntf_pt;

	error = hdb_error_to_sa (hdb_handle_get (&ntfHandleDatabase, notificationHandle, (void *)&ntf_pt));
	if (error != SA_AIS_OK) {
		printf ("%s:%d res=%d\n",__FILE__,__LINE__, error);
		return SA_AIS_ERR_BAD_HANDLE;
	}
	free (ntf_pt->sa_notification.misc->notificationHeader.notificationObject);
	free (ntf_pt->sa_notification.misc->notificationHeader.notifyingObject);
	free (ntf_pt->sa_notification.misc->notificationHeader.notificationClassId);
//	free (ntf_pt->sa_notification.misc->notificationHeader.additionalInfo);

	if (ntf_pt->notificationType == SA_NTF_TYPE_STATE_CHANGE) {
		free (ntf_pt->sa_notification.state->changedStates);
	}
	coroipcc_zcb_free (notificationHandle, ntf_pt->notification.misc);

	hdb_handle_put (&ntfHandleDatabase, notificationHandle);
	return hdb_handle_destroy (&ntfHandleDatabase, notificationHandle);
}

SaAisErrorT 
saNtfNotificationSend (SaNtfNotificationHandleT notificationHandle)
{
	notification_holder_t         *ntf_pt;
	res_lib_ntf_send_t             res;
	SaNtfNotificationHeaderT      *saHdr;
	mar_ntf_notification_header_t *hdr;
	SaAisErrorT error;
	int         i;

	error = hdb_error_to_sa (
		hdb_handle_get (&ntfHandleDatabase,	notificationHandle, (void *)&ntf_pt));

	if (error != SA_AIS_OK) {
		printf ("%s:%d res=%d\n",__FILE__,__LINE__, error);
		goto error_put;
	}

	/* copy any fields that could have different packing attributes */
	saHdr = &ntf_pt->sa_notification.misc->notificationHeader;
	hdr = &ntf_pt->notification.misc->notification_header;

	hdr->notification_class_id.vendor_id = saHdr->notificationClassId->vendorId;
	hdr->notification_class_id.major_id = saHdr->notificationClassId->majorId; 
	hdr->notification_class_id.minor_id = saHdr->notificationClassId->minorId; 

	marshall_SaNameT_to_mar_name_t (&hdr->notification_object, saHdr->notificationObject);
	marshall_SaNameT_to_mar_name_t (&hdr->notifying_object, saHdr->notifyingObject);

//	for (i = 0; i < ntf_pt->sa_notification.misc->notificationHeader.numAdditionalInfo; i++) {
		/* currently only fixed types */
//		marshall_AdditionalInfoT_to_attribute_t (&hdr->additional_info[i], &saHdr->additionalInfo[i]); 
//	}

	if (ntf_pt->notificationType == SA_NTF_TYPE_STATE_CHANGE) {
		mar_ntf_state_notification_t * sn = ntf_pt->notification.state;
		mar_ntf_state_change_t * sc = (mar_ntf_state_change_t *)((char*)&sn->changed_states + sn->changed_states.arrayVal.arrayOffset);
		SaNtfStateChangeT_3 * saSc = ntf_pt->sa_notification.state->changedStates;

		for (i = 0; i < ntf_pt->sa_notification.state->numStateChanges; i++) {
     		sc[i].state_id          = saSc[i].stateId;
     		sc[i].old_state_present = saSc[i].oldStatePresent;
     		sc[i].old_state         = saSc[i].oldState;
     		sc[i].new_state         = saSc[i].newState;
		}
	}

	ntf_pt->notification.misc->header.id = MESSAGE_REQ_NTF_SEND;

	error = coroipcc_zcb_msg_send_reply_receive (
		ntf_pt->ntfInstance->ipc_handle,
		ntf_pt->notification.misc,
		&res,
		sizeof (res_lib_ntf_send_t));

	printf ("%s:%d error=%d, send-id=%d, rx-id=%d size=%d\n",
			__FILE__,__LINE__, error, 
			ntf_pt->notification.misc->header.id,
			res.header.id,
			ntf_pt->notification.misc->header.size);

error_put:
	hdb_handle_put (&ntfHandleDatabase, notificationHandle);

	return (error);
}

/* -------------------------------------------------------- */
/*   F I L T E R S */


SaAisErrorT
saNtfMiscellaneousNotificationFilterAllocate (
		SaNtfHandleT ntfHandle,
		SaNtfMiscellaneousNotificationFilterT *flt,
		SaUint16T numEventTypes,
		SaUint16T numNotificationObjects,
		SaUint16T numNotifyingObjects,
		SaUint16T numNotificationClassIds)
{
	ntf_instance_t *ntfInstance;
	SaAisErrorT error;
	notification_filter_holder_t *flt_pt;
	mar_ntf_filter_misc_t * hdr;
	int size;

	error = hdb_error_to_sa (hdb_handle_get (&ntfHandleDatabase, ntfHandle, (void *)&ntfInstance));

	error = hdb_error_to_sa (hdb_handle_create (&ntfHandleDatabase,
							sizeof (notification_filter_holder_t),
							&flt->notificationFilterHandle));
	if (error != SA_AIS_OK) {
		goto error_no_destroy;
	}

	error = hdb_error_to_sa (hdb_handle_get (&ntfHandleDatabase,
				flt->notificationFilterHandle, (void*)&flt_pt));
	if (error == SA_AIS_OK) {
		flt_pt->ntfInstance = ntfInstance;
		flt_pt->notificationType = SA_NTF_TYPE_MISCELLANEOUS;
		flt_pt->sa_filter_pt.misc = flt;

		flt->notificationFilterHeader.numEventTypes = numEventTypes;
		flt->notificationFilterHeader.numNotificationObjects = numNotificationObjects;
		flt->notificationFilterHeader.numNotifyingObjects = numNotifyingObjects;
		flt->notificationFilterHeader.numNotificationClassIds = numNotificationClassIds;

		size = sizeof (mar_ntf_filter_misc_t);

		size += (sizeof (mar_uint32_t)       * numEventTypes);
		size += (sizeof (mar_name_t)         * numNotificationObjects);
		size += (sizeof (mar_name_t)         * numNotifyingObjects);
		size += (sizeof (mar_ntf_class_id_t) * numNotificationClassIds);

		hdr = malloc (size);
		hdr->header.id = MESSAGE_REQ_NTF_FLT_SUBSCRIBE;
		hdr->header.size = size;

		flt_pt->filter_pt.misc = hdr;

		flt->notificationFilterHeader.eventTypes = malloc (numEventTypes * sizeof (SaNtfEventTypeT));
		flt->notificationFilterHeader.notificationObjects = malloc (sizeof (SaNameT) * numNotificationObjects);
		flt->notificationFilterHeader.notifyingObjects = malloc (sizeof (SaNameT) * numNotifyingObjects);
		flt->notificationFilterHeader.notificationClassIds = malloc (sizeof (SaNtfClassIdT) * numNotificationClassIds);

		hdb_handle_put (&ntfHandleDatabase, flt->notificationFilterHandle);
	}
error_no_destroy:
	hdb_handle_put (&ntfHandleDatabase, ntfHandle);
	return (error);
}

/* TODO */
SaAisErrorT
saNtfStateChangeNotificationFilterAllocate_2 (
		SaNtfHandleT ntfHandle,
		SaNtfStateChangeNotificationFilterT_2 *notificationFilter,
		SaUint16T numEventTypes,
		SaUint16T numNotificationObjects,
		SaUint16T numNotifyingObjects,
		SaUint16T numNotificationClassIds,
		SaUint16T numSourceIndicators,
		SaUint16T numChangedStates)
{
	return SA_AIS_ERR_NOT_SUPPORTED;
}

static SaAisErrorT
send_subscription (SaNtfNotificationFilterHandleT fh,
		SaNtfSubscriptionIdT sid)
{
	notification_filter_holder_t *flt_pt;
	struct iovec iov;
	SaAisErrorT error;
	res_lib_ntf_flt_subscribe_t res;

	if (fh == SA_NTF_FILTER_HANDLE_NULL) {
		return SA_AIS_OK;
	}
	error = hdb_error_to_sa (hdb_handle_get (&ntfHandleDatabase, fh, (void*)&flt_pt));
	
	mar_ntf_filter_holder (flt_pt);

	iov.iov_base = flt_pt->filter_pt.misc;
	iov.iov_len = flt_pt->filter_pt.misc->header.size;

	pthread_mutex_lock (&flt_pt->ntfInstance->response_mutex);

	printf("%s()\n",__func__);
	error = coroipcc_msg_send_reply_receive (flt_pt->ntfInstance->ipc_handle,
		&iov, 1,	&res, sizeof (res_lib_ntf_flt_subscribe_t));
	printf ("%s:%d error=%d, id=%d\n",__FILE__,__LINE__, error, res.header.id);

	pthread_mutex_unlock (&flt_pt->ntfInstance->response_mutex);

	error = hdb_handle_put (&ntfHandleDatabase, fh);

	return error;
}


/* TODO */
SaAisErrorT
saNtfNotificationSubscribe_3 (
     const SaNtfNotificationTypeFilterHandlesT_3 *nfhs,
     SaNtfSubscriptionIdT sid)
{

	send_subscription (nfhs->objectCreateDeleteFilterHandle, sid);
	send_subscription (nfhs->attributeChangeFilterHandle, sid);
	send_subscription (nfhs->stateChangeFilterHandle, sid);
	send_subscription (nfhs->alarmFilterHandle, sid);
	send_subscription (nfhs->securityAlarmFilterHandle, sid);
	send_subscription (nfhs->miscellaneousFilterHandle, sid);

	return SA_AIS_OK;
}

SaAisErrorT
saNtfNotificationUnsubscribe_2 (
		SaNtfHandleT ntfHandle,
		SaNtfSubscriptionIdT subscriptionId)
{
	SaAisErrorT error;
	req_lib_ntf_flt_unsubscribe_t req;
	res_lib_ntf_flt_unsubscribe_t res;
	ntf_instance_t *ntfInstance;
	struct iovec iov[1];

	error = hdb_error_to_sa (hdb_handle_get (&ntfHandleDatabase,
				ntfHandle, (void*)&ntfInstance));

	req.header.id = MESSAGE_REQ_NTF_FLT_SUBSCRIBE;
	req.header.size = sizeof (req_lib_ntf_flt_unsubscribe_t);
	req.id = subscriptionId;

	pthread_mutex_lock (&ntfInstance->response_mutex);

	error = coroipcc_msg_send_reply_receive (ntfInstance->ipc_handle,
		iov, 1,
		&res, sizeof (res_lib_ntf_flt_subscribe_t));
	printf ("%s:%d error=%d, id=%d\n",__FILE__,__LINE__, error, res.header.id);

	pthread_mutex_unlock (&ntfInstance->response_mutex);

	error = hdb_handle_put (&ntfHandleDatabase, ntfHandle);

	return res.header.error;
}

/* TODO */
SaAisErrorT
saNtfNotificationFilterFree (
		SaNtfNotificationFilterHandleT notificationFilterHandle)
{
	return SA_AIS_ERR_NOT_SUPPORTED;
}


