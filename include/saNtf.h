/*
 * Copyright (c) 2009, Allied Telesis Labs, New Zealand
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

#ifndef SANTF_H_DEFINED
#define SANTF_H_DEFINED

#ifdef __cplusplus
extern "C" {
#endif

typedef SaUint64T SaNtfHandleT;
typedef SaUint64T SaNtfNotificationHandleT;
typedef SaUint64T SaNtfNotificationFilterHandleT;
typedef SaUint64T SaNtfReadHandleT;
typedef SaUint32T SaNtfSubscriptionIdT;
typedef SaUint64T SaNtfEventTypeBitmapT;
typedef SaUint16T SaNtfElementIdT;

#define SA_NTF_FILTER_HANDLE_NULL ((SaNtfNotificationFilterHandleT) 0)
#define SA_NTF_NOTIFICATIONS_TYPE_MASK 0xF000

typedef enum {
    SA_NTF_TYPE_OBJECT_CREATE_DELETE = 0x1000,
    SA_NTF_TYPE_ATTRIBUTE_CHANGE = 0x2000,
    SA_NTF_TYPE_STATE_CHANGE = 0x3000,
    SA_NTF_TYPE_ALARM = 0x4000,
    SA_NTF_TYPE_SECURITY_ALARM = 0x5000,
    SA_NTF_TYPE_MISCELLANEOUS = 0x6000
} SaNtfNotificationTypeT;

typedef enum {
    SA_NTF_OBJECT_NOTIFICATIONS_START = SA_NTF_TYPE_OBJECT_CREATE_DELETE,
    SA_NTF_OBJECT_CREATION,
    SA_NTF_OBJECT_DELETION,
    SA_NTF_ATTRIBUTE_NOTIFICATIONS_START = SA_NTF_TYPE_ATTRIBUTE_CHANGE,
    SA_NTF_ATTRIBUTE_ADDED,
    SA_NTF_ATTRIBUTE_REMOVED,
    SA_NTF_ATTRIBUTE_CHANGED,
    SA_NTF_ATTRIBUTE_RESET,
    SA_NTF_STATE_CHANGE_NOTIFICATIONS_START = SA_NTF_TYPE_STATE_CHANGE,
    SA_NTF_OBJECT_STATE_CHANGE,
    SA_NTF_ALARM_NOTIFICATIONS_START = SA_NTF_TYPE_ALARM,
    SA_NTF_ALARM_COMMUNICATION,
    SA_NTF_ALARM_QOS,
    SA_NTF_ALARM_PROCESSING,
    SA_NTF_ALARM_EQUIPMENT,
    SA_NTF_ALARM_ENVIRONMENT,
    SA_NTF_SECURITY_ALARM_NOTIFICATIONS_START = SA_NTF_TYPE_SECURITY_ALARM,
    SA_NTF_INTEGRITY_VIOLATION,
    SA_NTF_OPERATION_VIOLATION,
    SA_NTF_PHYSICAL_VIOLATION,
    SA_NTF_SECURITY_SERVICE_VIOLATION,
    SA_NTF_TIME_VIOLATION,
    SA_NTF_MISCELLANEOUS_NOTIFICATIONS_START = SA_NTF_TYPE_MISCELLANEOUS,
    SA_NTF_APPLICATION_EVENT,
    SA_NTF_ADMIN_OPERATION_START,
    SA_NTF_ADMIN_OPERATION_END,
    SA_NTF_CONFIG_UPDATE_START,
    SA_NTF_CONFIG_UPDATE_END,
    SA_NTF_ERROR_REPORT,
    SA_NTF_ERROR_CLEAR,
    SA_NTF_HPI_EVENT_RESOURCE,
    SA_NTF_HPI_EVENT_SENSOR,
    SA_NTF_HPI_EVENT_WATCHDOG,
    SA_NTF_HPI_EVENT_DIMI,
    SA_NTF_HPI_EVENT_FUMI,
    SA_NTF_HPI_EVENT_OTHER
} SaNtfEventTypeT;

typedef enum {
     SA_NTF_VALUE_UINT8,     
     SA_NTF_VALUE_INT8,      
     SA_NTF_VALUE_UINT16,    
     SA_NTF_VALUE_INT16,     
     SA_NTF_VALUE_UINT32,    
     SA_NTF_VALUE_INT32,     
     SA_NTF_VALUE_FLOAT,     
     SA_NTF_VALUE_UINT64,    
     SA_NTF_VALUE_INT64,     
     SA_NTF_VALUE_DOUBLE,    
     SA_NTF_VALUE_LDAP_NAME, 
     SA_NTF_VALUE_STRING,    
     SA_NTF_VALUE_IPADDRESS, 
     SA_NTF_VALUE_BINARY,    
     SA_NTF_VALUE_ARRAY      
} SaNtfValueTypeT;

typedef enum {
     SA_NTF_OBJECT_OPERATION     = 1,
     SA_NTF_MANAGEMENT_OPERATION = 2,
     SA_NTF_UNKNOWN_OPERATION    = 3
} SaNtfSourceIndicatorT;

typedef enum {
	SA_NTF_ADAPTER_ERROR,
	SA_NTF_APPLICATION_SUBSYSTEM_FAILURE,
	SA_NTF_BANDWIDTH_REDUCED,
	SA_NTF_CALL_ESTABLISHMENT_ERROR,
	SA_NTF_COMMUNICATIONS_PROTOCOL_ERROR,
	SA_NTF_COMMUNICATIONS_SUBSYSTEM_FAILURE,
	SA_NTF_CONFIGURATION_OR_CUSTOMIZATION_ERROR,
	SA_NTF_CONGESTION,
	SA_NTF_CORRUPT_DATA,
	SA_NTF_CPU_CYCLES_LIMIT_EXCEEDED,
	SA_NTF_DATASET_OR_MODEM_ERROR,
	SA_NTF_DEGRADED_SIGNAL,
	SA_NTF_D_T_E,
	SA_NTF_ENCLOSURE_DOOR_OPEN,
	SA_NTF_EQUIPMENT_MALFUNCTION,
	SA_NTF_EXCESSIVE_VIBRATION,
	SA_NTF_FILE_ERROR,
	SA_NTF_FIRE_DETECTED,
	SA_NTF_FLOOD_DETECTED,
	SA_NTF_FRAMING_ERROR,
	SA_NTF_HEATING_OR_VENTILATION_OR_COOLING_SYSTEM_PROBLEM,
	SA_NTF_HUMIDITY_UNACCEPTABLE,
	SA_NTF_INPUT_OUTPUT_DEVICE_ERROR,
	SA_NTF_INPUT_DEVICE_ERROR,
	SA_NTF_L_A_N_ERROR,
	SA_NTF_LEAK_DETECTED,
	SA_NTF_LOCAL_NODE_TRANSMISSION_ERROR,
	SA_NTF_LOSS_OF_FRAME,
	SA_NTF_LOSS_OF_SIGNAL,
	SA_NTF_MATERIAL_SUPPLY_EXHAUSTED,
	SA_NTF_MULTIPLEXER_PROBLEM,
	SA_NTF_OUT_OF_MEMORY,
	SA_NTF_OUTPUT_DEVICE_ERROR,
	SA_NTF_PERFORMANCE_DEGRADED,
	SA_NTF_POWER_PROBLEM,
	SA_NTF_PRESSURE_UNACCEPTABLE,
	SA_NTF_PROCESSOR_PROBLEM,
	SA_NTF_PUMP_FAILURE,
	SA_NTF_QUEUE_SIZE_EXCEEDED,
	SA_NTF_RECEIVE_FAILURE,
	SA_NTF_RECEIVER_FAILURE,
	SA_NTF_REMOTE_NODE_TRANSMISSION_ERROR,
	SA_NTF_RESOURCE_AT_OR_NEARING_CAPACITY,
	SA_NTF_RESPONSE_TIME_EXCESSIVE,
	SA_NTF_RETRANSMISSION_RATE_EXCESSIVE,
	SA_NTF_SOFWARE_ERROR,
	SA_NTF_SOFWARE_PROGRAM_ABNORMALLY_TERMINATED,
	SA_NTF_SOFTWARE_PROGRAM_ERROR,
	SA_NTF_STORAGE_CAPACITY_PROBLEM,
	SA_NTF_TEMPERATURE_UNACCEPTABLE,
	SA_NTF_THRESHOLD_CROSSED,
	SA_NTF_TIMING_PROBLEM,
	SA_NTF_TOXIC_LEAK_DETECTED,
	SA_NTF_TRANSMIT_FAILURE,
	SA_NTF_TRANSMITTER_FAILURE,
	SA_NTF_UNDERLYING_RESOURCE_UNAVAILABLE,
	SA_NTF_VERSION_MISMATCH,
	SA_NTF_AUTHENTICATION_FAILURE,
	SA_NTF_BREACH_OF_CONFIDENTIALITY,
	SA_NTF_CABLE_TAMPER,
	SA_NTF_DELAYED_INFORMATION,
	SA_NTF_DENIAL_OF_SERVICE,
	SA_NTF_DUPLICATE_INFORMATION,
	SA_NTF_INFORMATION_MISSING,
	SA_NTF_INFORMATION_MODIFICATION_DETECTED,
	SA_NTF_INFORMATION_OUT_OF_SEQUENCE,
	SA_NTF_INTRUSION_DETECTION,
	SA_NTF_KEY_EXPIRED,
	SA_NTF_NON_REPUDIATION_FAILURE,
	SA_NTF_OUT_OF_HOURS_ACTIVITY,
	SA_NTF_OUT_OF_SERVICE,
	SA_NTF_PROCEDURAL_ERROR,
	SA_NTF_UNAUTHORIZED_ACCESS_ATTEMPT,
	SA_NTF_UNEXPECTED_INFORMATION,
	SA_NTF_UNSPECIFIED_REASON
} SaNtfProbableCauseT;


typedef enum {
     SA_NTF_SEVERITY_CLEARED, /* alarm notification, only */
     SA_NTF_SEVERITY_INDETERMINATE,
     SA_NTF_SEVERITY_WARNING,
     SA_NTF_SEVERITY_MINOR,
     SA_NTF_SEVERITY_MAJOR,
     SA_NTF_SEVERITY_CRITICAL
} SaNtfSeverityT;

typedef enum {
     SA_NTF_TREND_MORE_SEVERE,
     SA_NTF_TREND_NO_CHANGE,
     SA_NTF_TREND_LESS_SEVERE
} SaNtfSeverityTrendT;


typedef union {
	/* The first few are fixed size data types*/
	SaUint8T   uint8Val; /* SA_NTF_VALUE_UINT8 */
	SaInt8T    int8Val;  /* SA_NTF_VALUE_INT8 */
	SaUint16T uint16Val; /* SA_NTF_VALUE_UINT16 */
	SaInt16T   int16Val; /* SA_NTF_VALUE_INT16 */
	SaUint32T uint32Val; /* SA_NTF_VALUE_UINT32 */
	SaInt32T   int32Val; /* SA_NTF_VALUE_INT32 */
	SaFloatT   floatVal; /* SA_NTF_VALUE_FLOAT */
	SaUint64T uint64Val; /* SA_NTF_VALUE_UINT64 */
	SaInt64T   int64Val; /* SA_NTF_VALUE_INT64 */
	SaDoubleT doubleVal; /* SA_NTF_VALUE_DOUBLE */
	/* This struct can represent variable length fields like *
	 * LDAP names, strings, IP addresses, and binary data. *
	 * It may be used only in conjunction with the data type *
	 * values SA_NTF_VALUE_LDAP_NAME, SA_NTF_VALUE_STRING, *
	 * SA_NTF_VALUE_IPADDRESS, and SA_NTF_VALUE_BINARY. *
	 * This field shall not be directly accessed. *
	 * To initialize this structure and to set a pointer to the *
	 * real data, use saNtfPtrValAllocate(). The function *
	 * saNtfPtrValGet() shall be used for retrieval of the *
	 * real data. *
	 */
	struct {
		SaUint16T dataOffset;
		SaUint16T dataSize;
	} ptrVal;
	/* This struct represents sets of data having identical type *
	 * like notification identifiers, attributes, and so on.*
	 * It may only be used in conjunction with the data type value *
	 * SA_NTF_VALUE_ARRAY. The functions SaNtfArrayValAllocate() *
	 * or SaNtfArrayValGet() shall be used to get a pointer for *
	 * accessing the real data. Direct access is not allowed. *
	 */
	struct {
		SaUint16T arrayOffset;
		SaUint16T numElements;
		SaUint16T elementSize;
	} arrayVal;
} SaNtfValueT;

typedef struct {
     SaNtfElementIdT attributeId;
     SaNtfValueTypeT attributeType;
     SaNtfValueT attributeValue;
} SaNtfAttributeT;

#define SA_NTF_OBJECT_CREATION_BIT            0x01
#define SA_NTF_OBJECT_DELETION_BIT            0x02
#define SA_NTF_ATTRIBUTE_ADDED_BIT            0x04
#define SA_NTF_ATTRIBUTE_REMOVED_BIT          0x08
#define SA_NTF_ATTRIBUTE_CHANGED_BIT          0x10
#define SA_NTF_ATTRIBUTE_RESET_BIT            0x20
#define SA_NTF_OBJECT_STATE_CHANGE_BIT        0x40
#define SA_NTF_ALARM_COMMUNICATION_BIT        0x80
#define SA_NTF_ALARM_QOS_BIT                  0x100
#define SA_NTF_ALARM_PROCESSING_BIT           0x200
#define SA_NTF_ALARM_EQUIPMENT_BIT            0x400
#define SA_NTF_ALARM_ENVIRONMENT_BIT          0x800
#define SA_NTF_INTEGRITY_VIOLATION_BIT        0x1000
#define SA_NTF_OPERATION_VIOLATION_BIT        0x2000
#define SA_NTF_PHYSICAL_VIOLATION_BIT         0x4000
#define SA_NTF_SECURITY_SERVICE_VIOLATION_BIT 0x8000
#define SA_NTF_TIME_VIOLATION_BIT             0x10000
#define SA_NTF_ADMIN_OPERATION_START_BIT      0x20000
#define SA_NTF_ADMIN_OPERATION_END_BIT        0x40000
#define SA_NTF_CONFIG_UPDATE_START_BIT        0x80000
#define SA_NTF_CONFIG_UPDATE_END_BIT          0x100000
#define SA_NTF_ERROR_REPORT_BIT               0x200000
#define SA_NTF_ERROR_CLEAR_BIT                0x400000
#define SA_NTF_HPI_EVENT_RESOURCE_BIT         0x800000
#define SA_NTF_HPI_EVENT_SENSOR_BIT           0x1000000
#define SA_NTF_HPI_EVENT_WATCHDOG_BIT         0x2000000
#define SA_NTF_HPI_EVENT_DIMI_BIT             0x4000000
#define SA_NTF_HPI_EVENT_FUMI_BIT             0x8000000
#define SA_NTF_HPI_EVENT_OTHER_BIT            0x10000000
#define SA_NTF_APPLICATION_EVENT_BIT          0x100000000000

typedef struct {
     SaUint32T vendorId;
     SaUint16T majorId;
     SaUint16T minorId;
} SaNtfClassIdT;

typedef struct {
     SaNtfElementIdT infoId;
     SaNtfValueTypeT infoType;
     SaNtfValueT infoValue;
} SaNtfAdditionalInfoT;

typedef struct {
     SaNtfElementIdT problemId;
     /* API user is expected to define this field*/
     SaNtfClassIdT problemClassId;
     /* optional field to identify problemId values        *
      * from other notification class identifiers, needed  *
      * for correlation between clear and non-clear alarms *
      */
     SaNtfValueTypeT problemType;
     SaNtfValueT problemValue;
} SaNtfSpecificProblemT;

typedef struct {
     SaNtfElementIdT thresholdId;
     SaNtfValueTypeT thresholdValueType;
     SaNtfValueT thresholdValue;
     SaNtfValueT thresholdHysteresis;
     SaNtfValueT observedValue;
     SaTimeT armTime;
} SaNtfThresholdInformationT;

typedef struct {
	SaNtfEventTypeT *eventType;
	SaNameT *notificationObject;
	SaNameT *notifyingObject;
	SaNtfClassIdT *notificationClassId;
	SaTimeT *eventTime;
	SaUint16T numCorrelatedNotifications;
	SaUint16T lengthAdditionalText;
	SaUint16T numAdditionalInfo;
	SaNtfIdentifierT *notificationId;
	SaNtfIdentifierT *correlatedNotifications;
	SaStringT additionalText;
	SaNtfAdditionalInfoT *additionalInfo;
} SaNtfNotificationHeaderT;

typedef struct {
     SaNtfNotificationHandleT notificationHandle;
     SaNtfNotificationHeaderT notificationHeader;
     SaUint16T numAttributes;
     SaNtfSourceIndicatorT *sourceIndicator;
     SaNtfAttributeT *objectAttributes;
} SaNtfObjectCreateDeleteNotificationT;

typedef struct {
     SaNtfElementIdT attributeId;
     SaNtfValueTypeT attributeType;
     SaBoolT oldAttributePresent;
     SaNtfValueT oldAttributeValue;
     SaNtfValueT newAttributeValue;
} SaNtfAttributeChangeT;


typedef struct {
     SaNtfNotificationHandleT notificationHandle;
     SaNtfNotificationHeaderT notificationHeader;
     SaUint16T numAttributes;
     SaNtfSourceIndicatorT *sourceIndicator;
     SaNtfAttributeChangeT *changedAttributes;
} SaNtfAttributeChangeNotificationT;

typedef struct {
     SaNtfElementIdT stateId;
     SaBoolT oldStatePresent;
     SaUint64T oldState;
     SaUint64T newState;
} SaNtfStateChangeT_3;

typedef struct {
     SaNtfValueTypeT valueType;
     SaNtfValueT value;
} SaNtfSecurityAlarmDetectorT;

typedef struct {
     SaNtfNotificationHandleT notificationHandle;
     SaNtfNotificationHeaderT notificationHeader;
} SaNtfMiscellaneousNotificationT;

typedef struct {
     SaNtfValueTypeT valueType;
     SaNtfValueT value;
} SaNtfServiceUserT;

typedef struct {
     SaNtfNotificationHandleT notificationHandle;
     SaNtfNotificationHeaderT notificationHeader;
     SaNtfProbableCauseT *probableCause;
     SaNtfSeverityT *severity;
     SaNtfSecurityAlarmDetectorT *securityAlarmDetector;
     SaNtfServiceUserT*serviceUser;
     SaNtfServiceUserT *serviceProvider;
} SaNtfSecurityAlarmNotificationT;

typedef struct {
     SaNtfElementIdT actionId;
     /* API user is expected to define this field*/
     SaNtfValueTypeT actionValueType;
     SaNtfValueT actionValue;
} SaNtfProposedRepairActionT;


typedef struct {
     SaNtfNotificationHandleT notificationHandle;
     SaNtfNotificationHeaderT notificationHeader;
     SaUint16T numSpecificProblems;
     SaUint16T numMonitoredAttributes;
     SaUint16T numProposedRepairActions;
     SaNtfProbableCauseT *probableCause;
     SaNtfSpecificProblemT *specificProblems;
     SaNtfSeverityT *perceivedSeverity;
     SaNtfSeverityTrendT *trend;
     SaNtfThresholdInformationT *thresholdInformation;
     SaNtfAttributeT *monitoredAttributes;
     SaNtfProposedRepairActionT *proposedRepairActions;
} SaNtfAlarmNotificationT;


typedef struct {
     SaNtfNotificationHandleT notificationHandle;
     /* A handle to the internal notification structure*/
     SaNtfNotificationHeaderT notificationHeader;
     SaUint16T numStateChanges;
     SaNtfSourceIndicatorT *sourceIndicator;
     SaNtfStateChangeT_3 *changedStates;
} SaNtfStateChangeNotificationT_3;

typedef struct {
	SaNtfNotificationTypeT notificationType;
	union
	{
		SaNtfObjectCreateDeleteNotificationT  objectCreateDeleteNotification;
		SaNtfAttributeChangeNotificationT     attributeChangeNotification;
		SaNtfStateChangeNotificationT_3       stateChangeNotification;
		SaNtfAlarmNotificationT               alarmNotification;
		SaNtfSecurityAlarmNotificationT       securityAlarmNotification;
		SaNtfMiscellaneousNotificationT       miscellaneousNotification;
	} notification;
} SaNtfNotificationsT_3;

typedef void (*SaNtfStaticSuppressionFilterSetCallbackT_3) (
		SaNtfHandleT ntfHandle,
		SaNtfEventTypeBitmapT eventTypeBitmap);

typedef void (*SaNtfNotificationCallbackT_3) (
		SaNtfSubscriptionIdT subscriptionId,
		const SaNtfNotificationsT_3 *notification);

typedef struct {
	SaNtfNotificationCallbackT_3 saNtfNotificationCallback;
#if 0
	SaNtfNotificationDiscardedCallbackT saNtfNotificationDiscardedCallback;
	SaNtfStaticSuppressionFilterSetCallbackT_3 saNtfStaticSuppressionFilterSetCallback;
#endif
} SaNtfCallbacksT_3;

typedef struct {
	SaUint16T numEventTypes;
	SaNtfEventTypeT *eventTypes;
	SaUint16T numNotificationObjects;
	SaNameT *notificationObjects;
	SaUint16T numNotifyingObjects;
	SaNameT *notifyingObjects;
	SaUint16T numNotificationClassIds;
	SaNtfClassIdT *notificationClassIds;
} SaNtfNotificationFilterHeaderT;

typedef struct {
     SaNtfNotificationFilterHandleT notificationFilterHandle;
     SaNtfNotificationFilterHeaderT notificationFilterHeader;
     SaUint16T numSourceIndicators;
     SaNtfSourceIndicatorT *sourceIndicators;
     SaUint16T numStateChanges;
     SaNtfElementIdT *stateId;
} SaNtfStateChangeNotificationFilterT_2;

typedef struct {
	SaNtfNotificationFilterHandleT objectCreateDeleteFilterHandle;
	SaNtfNotificationFilterHandleT attributeChangeFilterHandle;
	SaNtfNotificationFilterHandleT stateChangeFilterHandle;
	SaNtfNotificationFilterHandleT alarmFilterHandle;
	SaNtfNotificationFilterHandleT securityAlarmFilterHandle;
	SaNtfNotificationFilterHandleT miscellaneousFilterHandle;
} SaNtfNotificationTypeFilterHandlesT_3;

typedef struct {
     SaNtfNotificationFilterHandleT notificationFilterHandle;
     SaNtfNotificationFilterHeaderT notificationFilterHeader;
} SaNtfMiscellaneousNotificationFilterT;

/*
 * life time functions
 *
 */
SaAisErrorT
saNtfInitialize_3 (
		SaNtfHandleT * ntfHandle,
		const SaNtfCallbacksT_3 * ntfCallbacks,
		SaVersionT * version);

SaAisErrorT
saNtfSelectionObjectGet (
		SaNtfHandleT ntfHandle,
		SaSelectionObjectT * selectionObject);

SaAisErrorT
saNtfDispatch (
	SaNtfHandleT ntfHandle,
	SaDispatchFlagsT dispatchFlags);

SaAisErrorT
saNtfFinalize (
	SaNtfHandleT ntfHandle);

/*
 * notification allocation functions
 */
SaAisErrorT
saNtfMiscellaneousNotificationAllocate (
		SaNtfHandleT ntfHandle,
		SaNtfMiscellaneousNotificationT * notification,
		SaUint16T numCorrelatedNotifications,
		SaUint16T lengthAdditionalText,
		SaUint16T numAdditionalInfo,
		SaInt16T variableDataSize);

SaAisErrorT
saNtfStateChangeNotificationAllocate_3 (
		SaNtfHandleT ntfHandle,
		SaNtfStateChangeNotificationT_3 * notification,
		SaUint16T numCorrelatedNotifications,
		SaUint16T lengthAdditionalText,
		SaUint16T numAdditionalInfo,
		SaUint16T numStateChanges,
		SaInt16T variableDataSize);

SaAisErrorT
saNtfObjectCreateDeleteNotificationAllocate (
		SaNtfHandleT ntfHandle,
		SaNtfObjectCreateDeleteNotificationT * notification,
		SaUint16T numCorrelatedNotifications,
		SaUint16T lengthAdditionalText,
		SaUint16T numAdditionalInfo,
		SaUint16T numAttributes,
		SaInt16T variableDataSize);


SaAisErrorT
saNtfAttributeChangeNotificationAllocate (
		SaNtfHandleT ntfHandle,
		SaNtfAttributeChangeNotificationT * notification,
		SaUint16T numCorrelatedNotifications,
		SaUint16T lengthAdditionalText,
		SaUint16T numAdditionalInfo,
		SaUint16T numAttributes,
		SaInt16T variableDataSize);

SaAisErrorT
saNtfAlarmNotificationAllocate (
		SaNtfHandleT ntfHandle,
		SaNtfAlarmNotificationT * notification,
		SaUint16T numCorrelatedNotifications,
		SaUint16T lengthAdditionalText,
		SaUint16T numAdditionalInfo,
		SaUint16T numSpecificProblems,
		SaUint16T numMonitoredAttributes,
		SaUint16T numProposedRepairActions,
		SaInt16T variableDataSize);


SaAisErrorT
saNtfSecurityAlarmNotificationAllocate (
		SaNtfHandleT ntfHandle,
		SaNtfSecurityAlarmNotificationT * notification,
		SaUint16T numCorrelatedNotifications,
		SaUint16T lengthAdditionalText,
		SaUint16T numAdditionalInfo,
		SaInt16T variableDataSize);


/* TODO */
SaAisErrorT
saNtfPtrValAllocate (
		SaNtfNotificationHandleT notificationHandle,
		SaUint16T dataSize,
		void **dataPtr,
		SaNtfValueT * value);

/* TODO */
SaAisErrorT
saNtfArrayValAllocate (
		SaNtfNotificationHandleT notificationHandle,
		SaUint16T numElements,
		SaUint16T elementSize,
		void **arrayPtr,
		SaNtfValueT * value);

/* TODO */
SaAisErrorT
saNtfIdentifierAllocate (
		SaNtfNotificationHandleT notificationHandle,
		SaNtfIdentifierT * notificationIdentifier);

SaAisErrorT
saNtfNotificationSend (
		SaNtfNotificationHandleT notificationHandle);

/* TODO */
SaAisErrorT
saNtfNotificationSendWithId (
		SaNtfNotificationHandleT notificationHandle,
		SaNtfIdentifierT notificationIdentifier);

SaAisErrorT
saNtfNotificationFree (
		SaNtfNotificationHandleT notificationHandle);


/* notification subscriber API
 *
 * -# allocate a filter
 * -# fill in the filter
 * -# subscribe using the filter
 */

/* TODO */
SaAisErrorT
saNtfMiscellaneousNotificationFilterAllocate (
		SaNtfHandleT ntfHandle,
		SaNtfMiscellaneousNotificationFilterT *notificationFilter,
		SaUint16T numEventTypes,
		SaUint16T numNotificationObjects,
		SaUint16T numNotifyingObjects,
		SaUint16T numNotificationClassIds);

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
		SaUint16T numChangedStates);

SaAisErrorT
saNtfNotificationSubscribe_3 (
     const SaNtfNotificationTypeFilterHandlesT_3 *notificationFilterHandles,
     SaNtfSubscriptionIdT subscriptionId);

SaAisErrorT
saNtfNotificationUnsubscribe_2 (
		SaNtfHandleT ntfHandle,
		SaNtfSubscriptionIdT subscriptionId);

/* TODO */
SaAisErrorT
saNtfNotificationFilterFree (
		SaNtfNotificationFilterHandleT notificationFilterHandle);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* SANTF_H_DEFINED */

