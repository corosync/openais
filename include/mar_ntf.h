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
#ifndef AIS_MAR_NTF_H_DEFINED
#define AIS_MAR_NTF_H_DEFINED

#include <corosync/swab.h>
#include "saAis.h"
#include "saNtf.h"
#include "mar_sa.h"
#include <corosync/mar_gen.h>
#include "ipc_ntf.h"

/* return cur_offset */
static inline int mar_notification_header (
	mar_ntf_notification_header_t *mar_hdr,
	SaNtfNotificationHeaderT *ntfHdr, char *start_of_data_old)
{
	int cur_offset = 0;
	char *start_of_data = ((char*)mar_hdr + sizeof (mar_ntf_notification_header_t));

	/* setup the public ntf struct to point into our struct */
	/* first fixed size fields */
	ntfHdr->eventType           = &mar_hdr->event_type;
	ntfHdr->notificationObject  = malloc (sizeof (SaNameT));
	ntfHdr->notifyingObject     = malloc (sizeof (SaNameT));
	ntfHdr->notificationClassId = malloc (sizeof (SaNtfClassIdT));
	ntfHdr->eventTime           = (SaTimeT*)&mar_hdr->event_time;
	ntfHdr->notificationId      = (SaNtfIdentifierT*)&mar_hdr->notification_id;

	/* now variable lengthed fields */
	mar_hdr->correlated_notifications.arrayVal.numElements = ntfHdr->numCorrelatedNotifications;
	mar_hdr->correlated_notifications.arrayVal.elementSize = sizeof (SaNtfIdentifierT);
	mar_hdr->correlated_notifications.arrayVal.arrayOffset = cur_offset;
	if (ntfHdr->numCorrelatedNotifications == 0) {
		ntfHdr->correlatedNotifications = NULL;
	} else {
		ntfHdr->correlatedNotifications = (SaNtfIdentifierT*)(start_of_data + cur_offset);
	}
	cur_offset += sizeof (SaNtfIdentifierT) * ntfHdr->numCorrelatedNotifications;

	ntfHdr->additionalText = start_of_data + cur_offset;
	mar_hdr->additional_text.ptrVal.dataOffset = cur_offset;
	mar_hdr->additional_text.ptrVal.dataSize = ntfHdr->lengthAdditionalText;

	cur_offset += ntfHdr->lengthAdditionalText * sizeof (char);
/*
	mar_hdr->additional_info.value.arrayVal.numElements = ntfHdr->numAdditionalInfo;
	mar_hdr->additional_info.value.arrayVal.elementSize = sizeof (mar_ntf_attribute_t);
	mar_hdr->additional_info.value.arrayVal.arrayOffset = cur_offset;
	ntfHdr->additionalInfo = malloc (sizeof (SaNtfAdditionalInfoT) * ntfHdr->numAdditionalInfo);
*/
	cur_offset += ntfHdr->numAdditionalInfo * sizeof (mar_ntf_attribute_t);
	return cur_offset;
}

static inline void marshall_AdditionalInfoT_to_attribute_t (
	mar_ntf_attribute_t * dest, SaNtfAdditionalInfoT * src)
{
	dest->id = src->infoId;
	dest->type = src->infoType;
	switch (src->infoType) {
		case SA_NTF_VALUE_UINT8:
			dest->value.uint8Val = src->infoValue.uint8Val;
			break;
		case SA_NTF_VALUE_INT8:
			dest->value.int8Val = src->infoValue.int8Val;
			break;
		case SA_NTF_VALUE_UINT16:
			dest->value.uint16Val = src->infoValue.uint16Val;
			break;
		case SA_NTF_VALUE_INT16:
			dest->value.int16Val = src->infoValue.int16Val;
			break;
		case SA_NTF_VALUE_UINT32:
			dest->value.uint32Val = src->infoValue.uint32Val;
			break;
		case SA_NTF_VALUE_INT32:
			dest->value.int32Val = src->infoValue.int32Val;
			break;
//		case SA_NTF_VALUE_FLOAT:
//			dest->value.floatVal = src->infoValue.floatVal;
//			break;
		case SA_NTF_VALUE_UINT64:
			dest->value.uint64Val = src->infoValue.uint64Val;
			break;
		case SA_NTF_VALUE_INT64:
			dest->value.int64Val = src->infoValue.int64Val;
			break;
//		case SA_NTF_VALUE_DOUBLE:
//			dest->value.doubleVal = src->infoValue.doubleVal;
//			break;
		default:
			break;
	}
}


static inline void unmar_notification_header (
	mar_ntf_notification_header_t *mar_hdr,
	SaNtfNotificationHeaderT *ntfHdr, char *start_of_data)
{
	int cur_offset = 0;

	ntfHdr->eventType           = &mar_hdr->event_type;
	ntfHdr->notificationObject  = (SaNameT*)&mar_hdr->notification_object;
	ntfHdr->notifyingObject     = (SaNameT*)&mar_hdr->notifying_object;
	ntfHdr->notificationClassId = &mar_hdr->notification_class_id;
	ntfHdr->eventTime           = (SaTimeT*)&mar_hdr->event_time;
	ntfHdr->notificationId      = &mar_hdr->notification_id;

	/* now variable lengthed fields */
	ntfHdr->numCorrelatedNotifications = mar_hdr->correlated_notifications.arrayVal.numElements;
	cur_offset = mar_hdr->correlated_notifications.arrayVal.arrayOffset;
	if (ntfHdr->numCorrelatedNotifications == 0) {
		ntfHdr->correlatedNotifications = NULL;
	} else {
		ntfHdr->correlatedNotifications = (SaNtfIdentifierT*)(start_of_data + cur_offset);
	}

	cur_offset = mar_hdr->additional_text.ptrVal.dataOffset;
	ntfHdr->additionalText = start_of_data + cur_offset;
	ntfHdr->lengthAdditionalText = mar_hdr->additional_text.ptrVal.dataSize;

	/* TODO do the addition info better */
/*
	cur_offset = mar_hdr->additional_info.value.arrayVal.arrayOffset;
	ntfHdr->numAdditionalInfo = mar_hdr->additional_info.value.arrayVal.numElements;
	if (ntfHdr->numAdditionalInfo == 0) {
		ntfHdr->additionalInfo = NULL;
	} else {
		ntfHdr->additionalInfo = (SaNtfAdditionalInfoT*)(start_of_data + cur_offset);
	}
*/
}

static inline void
marshall_NtfClassIdT_to_mar_ntf_class_id_t (mar_ntf_class_id_t * class_id, SaNtfClassIdT * classId)
{
	class_id->vendor_id = classId->vendorId;
	class_id->major_id = classId->majorId;
	class_id->minor_id = classId->minorId;
}

/*
 * marshalling code for sending & receiving filters 
 */
static inline void
mar_ntf_filter_holder (notification_filter_holder_t *flt_pt)
{
	SaNtfNotificationFilterHeaderT *nfh_pt = &flt_pt->sa_filter_pt.misc->notificationFilterHeader;
	mar_ntf_filter_header_t * mar_req_pt = &flt_pt->filter_pt.misc->filter_header;
	coroipc_request_header_t * header = &flt_pt->filter_pt.misc->header;
	char* offset = (char*)flt_pt->filter_pt.misc + sizeof (mar_ntf_filter_misc_t); /*TODO*/
	int i;

	mar_req_pt->type = flt_pt->notificationType;
	mar_req_pt->event_types.arrayVal.arrayOffset = offset - (char*)&mar_req_pt->event_types.arrayVal;
	mar_req_pt->event_types.arrayVal.numElements = nfh_pt->numEventTypes;
	mar_req_pt->event_types.arrayVal.elementSize = sizeof (mar_uint32_t);

	memcpy (offset, nfh_pt->eventTypes, (sizeof (SaNtfEventTypeT) * nfh_pt->numEventTypes));
	offset += (sizeof (mar_uint32_t) * nfh_pt->numEventTypes);

	mar_req_pt->notification_objects.arrayVal.arrayOffset = offset - (char*)&mar_req_pt->notification_objects.arrayVal;
	mar_req_pt->notification_objects.arrayVal.numElements = nfh_pt->numNotificationObjects;
	mar_req_pt->notification_objects.arrayVal.elementSize = sizeof (mar_name_t);

	for (i = 0; i < nfh_pt->numNotificationObjects; i++) {
		marshall_SaNameT_to_mar_name_t ((mar_name_t *)offset, &nfh_pt->notificationObjects[i]);
		offset += sizeof (mar_name_t);
	}

	mar_req_pt->notifying_objects.arrayVal.arrayOffset = offset - (char*)&mar_req_pt->notifying_objects.arrayVal;
	mar_req_pt->notifying_objects.arrayVal.numElements = nfh_pt->numNotifyingObjects;
	mar_req_pt->notifying_objects.arrayVal.elementSize = sizeof (mar_name_t);

	for (i = 0; i < nfh_pt->numNotifyingObjects; i++) {
		marshall_SaNameT_to_mar_name_t ((mar_name_t *)offset, &nfh_pt->notifyingObjects[i]);
		offset += sizeof (mar_name_t);
	}

	mar_req_pt->notification_class_ids.arrayVal.arrayOffset = offset - (char*)&mar_req_pt->notification_class_ids.arrayVal;
	mar_req_pt->notification_class_ids.arrayVal.numElements = nfh_pt->numNotificationClassIds;
	mar_req_pt->notification_class_ids.arrayVal.elementSize = sizeof (mar_ntf_class_id_t);

	for (i = 0; i < nfh_pt->numNotificationClassIds; i++) {
		marshall_NtfClassIdT_to_mar_ntf_class_id_t ((mar_ntf_class_id_t *)offset, &nfh_pt->notificationClassIds[i]);
		offset += sizeof (mar_ntf_class_id_t);
	}

}

#endif /* AIS_MAR_NTF_H_DEFINED */

