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

#include <stdio.h>
#include <string.h>
#include "saAis.h"
#include "saNtf.h"

static void print_notification_header (const SaNtfNotificationHeaderT *nh);
static void print_notification (const SaNtfNotificationsT_3 *n);

SaVersionT version = { 'A', 3, 1 };
static void test_notification_callback(SaNtfSubscriptionIdT subscriptionId,
		const SaNtfNotificationsT_3 *notification);

SaNtfCallbacksT_3 ntfCallbacks = {
    .saNtfNotificationCallback = test_notification_callback,
};

static void setSaNameT (SaNameT *name, const char *str) {
	name->length = strlen (str);
	strcpy ((char *)name->value, str);
}

static void
print_notification_header (const SaNtfNotificationHeaderT *nh)
{
int i;
    printf ("[NTF-HDR]: eventType:%d\n", *nh->eventType);
    printf ("[NTF-HDR]: notificationObject:%s\n", nh->notificationObject->value);
    printf ("[NTF-HDR]: notifyingObject:%s\n", nh->notifyingObject->value);
    printf ("[NTF-HDR]: numCorrelatedNotifications:%d\n", nh->numCorrelatedNotifications);
    printf ("[NTF-HDR]: lengthAdditionalText:%d\n", nh->lengthAdditionalText);
    //printf ("[NTF-HDR]: eventTime:%lld\n", *nh->eventTime);
    printf ("[NTF-HDR]: numAdditionalInfo:%d\n", nh->numAdditionalInfo);
    printf ("[NTF-HDR]: notificationId:%lld\n", *nh->notificationId);
    printf ("[NTF-HDR]: additionalText:%s\n", nh->additionalText);
    for (i = 0; i < nh->numCorrelatedNotifications; i++) {
        printf ("[NTF-HDR]: %d:%lld\n", i, nh->correlatedNotifications[i]);
    }
//	SaNtfClassIdT *notificationClassId;
//	SaNtfAdditionalInfoT *additionalInfo;
}

static void
print_notification (const SaNtfNotificationsT_3 *n)
{
	const SaNtfNotificationHeaderT *nh;
	SaNtfStateChangeT_3 *sc;
	int i;
	const SaNtfStateChangeNotificationT_3 *scn;

	printf ("[NTF]: type:%d\n", n->notificationType);
	switch (n->notificationType) {
		case SA_NTF_TYPE_OBJECT_CREATE_DELETE:
			nh = &n->notification.objectCreateDeleteNotification.notificationHeader;
			break;
		case SA_NTF_TYPE_ATTRIBUTE_CHANGE:
			nh = &n->notification.attributeChangeNotification.notificationHeader;
			break;
		case SA_NTF_TYPE_STATE_CHANGE:
			nh = &n->notification.stateChangeNotification.notificationHeader;
			break;
		case SA_NTF_TYPE_ALARM:
			nh = &n->notification.alarmNotification.notificationHeader;
			break;
		case SA_NTF_TYPE_SECURITY_ALARM:
			nh = &n->notification.securityAlarmNotification.notificationHeader;
			break;
		case SA_NTF_TYPE_MISCELLANEOUS:
		default:
			nh = &n->notification.miscellaneousNotification.notificationHeader;
			break;
	}

	print_notification_header (nh);

	switch (n->notificationType) {
		case SA_NTF_TYPE_STATE_CHANGE:
			scn = &n->notification.stateChangeNotification;
			printf("[NTF-SC]: type:%d nsc %d\n", n->notificationType, scn->numStateChanges);
			for (i = 0; i < scn->numStateChanges; i++) {
				sc = &scn->changedStates[i];
				if (sc->oldStatePresent == SA_TRUE) {
					printf("[NTF-SC]: id:%d os:%lld ns:%lld \n",
						sc->stateId, sc->oldState, sc->newState);
				} else {
					printf("[NTF-SC]: id:%d ns:%lld \n",
						sc->stateId, sc->newState);
				}
			}
			break;

		case SA_NTF_TYPE_MISCELLANEOUS:
		default:
			break;
	}
}


static void test_notification_callback(SaNtfSubscriptionIdT subscriptionId,
		const SaNtfNotificationsT_3 *notification)
{
	print_notification (notification);
}

static void
send_misc (SaNtfHandleT ntfHandle)
{
	SaAisErrorT res;
	SaNtfMiscellaneousNotificationT ntf;
	char msg[128];
	int msg_len;

	msg_len = snprintf(msg, 128, "%s:%d", __FILE__, __LINE__);

	/* allocate the notification */
	res = saNtfMiscellaneousNotificationAllocate (ntfHandle,
			&ntf, 0, msg_len+1, 0, 0);
	printf ("%s:%d res=%d\n",__FILE__,__LINE__, res);

	/* fill in the fields */
	strcpy (ntf.notificationHeader.additionalText, msg);
	ntf.notificationHeader.additionalText[msg_len] = '\0';
	*ntf.notificationHeader.eventType = SA_NTF_APPLICATION_EVENT;
	setSaNameT (ntf.notificationHeader.notificationObject, "little_tail");
	setSaNameT (ntf.notificationHeader.notifyingObject, "send_misc()");
	ntf.notificationHeader.notificationClassId->vendorId = 1;
	ntf.notificationHeader.notificationClassId->majorId = 1;
	ntf.notificationHeader.notificationClassId->minorId = 1;

	*ntf.notificationHeader.eventTime = 554433;
	*ntf.notificationHeader.notificationId = 9805;

	res = saNtfNotificationSend (ntf.notificationHandle);
	printf ("%s:%d res=%d\n",__FILE__,__LINE__, res);

	setSaNameT (ntf.notificationHeader.notificationObject, "a=25,b=46,r=890");

	res = saNtfNotificationSend (ntf.notificationHandle);
	printf ("%s:%d res=%d\n",__FILE__,__LINE__, res);

	*ntf.notificationHeader.eventType = SA_NTF_CONFIG_UPDATE_START;
	setSaNameT (ntf.notificationHeader.notifyingObject, "a=red,b=blue6,r=green");

	res = saNtfNotificationSend (ntf.notificationHandle);
	printf ("%s:%d res=%d\n",__FILE__,__LINE__, res);

	ntf.notificationHeader.notificationClassId->vendorId = 44;
	ntf.notificationHeader.notificationClassId->majorId = 34;
	ntf.notificationHeader.notificationClassId->minorId = 24;

	res = saNtfNotificationSend (ntf.notificationHandle);
	printf ("%s:%d res=%d\n",__FILE__,__LINE__, res);

	/* free the notification. */
	saNtfNotificationFree (ntf.notificationHandle);
}

static void
send_state_change (SaNtfHandleT ntfHandle)
{
	SaAisErrorT res = SA_AIS_OK;
	SaNtfStateChangeNotificationT_3 ntf;
	char msg[128];
	int msg_len;
	SaNtfIdentifierT nId = 538;
    SaNtfStateChangeT_3 *sc;

	msg_len = snprintf(msg, 128, "%s:%d", __FILE__, __LINE__);

	res = saNtfStateChangeNotificationAllocate_3 (ntfHandle,
			&ntf, 0, msg_len+1, 0, 2, 0);
	printf ("%s:%d res=%d\n",__FILE__,__LINE__, res);

	/* fill in the fields */
	strcpy (ntf.notificationHeader.additionalText, msg);
	*ntf.notificationHeader.eventType = SA_NTF_OBJECT_STATE_CHANGE;

	setSaNameT (ntf.notificationHeader.notificationObject, __FILE__);
	setSaNameT (ntf.notificationHeader.notifyingObject, "send_state_change()");

	ntf.notificationHeader.notificationClassId->vendorId = 44;
	ntf.notificationHeader.notificationClassId->majorId = 34;
	ntf.notificationHeader.notificationClassId->minorId = 24;

	*ntf.notificationHeader.eventTime = 15243;
	*ntf.notificationHeader.notificationId = 109;

	ntf.changedStates[0].stateId = 8;
	ntf.changedStates[0].oldStatePresent = SA_TRUE;
	ntf.changedStates[0].oldState = 739;
	ntf.changedStates[0].newState = 5;

	ntf.changedStates[1].stateId = 69;
	ntf.changedStates[1].oldStatePresent = SA_TRUE;
	ntf.changedStates[1].oldState = 12942;
	ntf.changedStates[1].newState = 242;
	sc = ntf.changedStates;

	*ntf.sourceIndicator = SA_NTF_MANAGEMENT_OPERATION;
	
	/* send it off */
	for (nId = 1; nId < 12; nId++) {
		sc[0].stateId = nId;
		sc[0].oldState = sc[0].newState;
		sc[0].newState = sc[0].newState += nId;
		sc[1].stateId = nId * 10;
		sc[1].oldState = sc[1].newState;
		sc[1].newState = sc[1].newState += nId;

		res = saNtfNotificationSend (ntf.notificationHandle);
		printf ("%s:%d res=%d\n",__FILE__,__LINE__, res);
	}
	saNtfNotificationFree (ntf.notificationHandle);
}

static void
subscribe (SaNtfHandleT ntfHandle)
{
	SaAisErrorT error;
	SaNtfMiscellaneousNotificationFilterT flt;
	SaNtfNotificationTypeFilterHandlesT_3 nfhs;

	error = saNtfMiscellaneousNotificationFilterAllocate (ntfHandle,
			&flt, 5, 3, 1, 2);

	memset (&nfhs, 0, sizeof (SaNtfNotificationTypeFilterHandlesT_3));
	nfhs.miscellaneousFilterHandle = flt.notificationFilterHandle;

	flt.notificationFilterHeader.eventTypes[0] = SA_NTF_APPLICATION_EVENT;
	flt.notificationFilterHeader.eventTypes[1] = SA_NTF_ADMIN_OPERATION_START;
	flt.notificationFilterHeader.eventTypes[2] = SA_NTF_ADMIN_OPERATION_END;
	flt.notificationFilterHeader.eventTypes[3] = SA_NTF_CONFIG_UPDATE_START;
	flt.notificationFilterHeader.eventTypes[4] = SA_NTF_CONFIG_UPDATE_END;

	setSaNameT (&flt.notificationFilterHeader.notificationObjects[0], "a=2,b=4,r=89");
	setSaNameT (&flt.notificationFilterHeader.notificationObjects[1], "a=12,b=22,r=9");
	setSaNameT (&flt.notificationFilterHeader.notificationObjects[2], "a=25,b=46,r=890");

	setSaNameT (&flt.notificationFilterHeader.notifyingObjects[0], "a=red,b=blue6,r=green");

	flt.notificationFilterHeader.notificationClassIds[0].vendorId = 44;
	flt.notificationFilterHeader.notificationClassIds[0].majorId = 34;
	flt.notificationFilterHeader.notificationClassIds[0].minorId = 24;

	flt.notificationFilterHeader.notificationClassIds[1].vendorId = 67;
	flt.notificationFilterHeader.notificationClassIds[1].majorId = 789;
	flt.notificationFilterHeader.notificationClassIds[1].minorId = 5529;

	error = saNtfNotificationSubscribe_3 (&nfhs, 98701);

	error = saNtfNotificationFilterFree (flt.notificationFilterHandle);
}

int
main (void)
{
	SaNtfHandleT ntfHandle;

	saNtfInitialize_3 (&ntfHandle, &ntfCallbacks, &version);

	subscribe (ntfHandle);

	send_misc (ntfHandle);
	//send_state_change (ntfHandle);

	saNtfDispatch (ntfHandle, SA_DISPATCH_ALL);
	saNtfFinalize (ntfHandle);

	return 0;
}


