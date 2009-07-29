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
#ifndef IPC_NTF_H_DEFINED
#define IPC_NTF_H_DEFINED

typedef union {
	mar_uint8_t  uint8Val; 
	mar_int8_t    int8Val;  
	mar_uint16_t uint16Val; 
	mar_int16_t   int16Val; 
	mar_uint32_t uint32Val; 
	mar_int32_t   int32Val; 
	/*mar_float_t   floatVal;*/ 
	mar_uint64_t uint64Val; 
	mar_int64_t   int64Val; 
	/*mar_double   doubleVal;*/ 
	struct {
		mar_uint16_t dataOffset;
		mar_uint16_t dataSize;
	} ptrVal;
	struct {
		mar_uint16_t arrayOffset;
		mar_uint16_t numElements;
		mar_uint16_t elementSize;
	} arrayVal;

} mar_any_value_u;

typedef struct {
     mar_uint32_t vendor_id __attribute__((aligned(8)));
     mar_uint16_t major_id  __attribute__((aligned(8)));
     mar_uint16_t minor_id  __attribute__((aligned(8)));
} mar_ntf_class_id_t;

typedef struct {
     mar_uint16_t    id    __attribute__((aligned(8)));
     mar_uint32_t    type  __attribute__((aligned(8)));
     mar_any_value_u value __attribute__((aligned(8)));
} mar_ntf_attribute_t;

typedef struct {

	mar_uint32_t        event_type               __attribute__((aligned(8)));
	mar_uint64_t        notification_id          __attribute__((aligned(8)));
	mar_name_t          notification_object      __attribute__((aligned(8)));
	mar_name_t          notifying_object         __attribute__((aligned(8)));
	mar_ntf_class_id_t  notification_class_id    __attribute__((aligned(8)));
	mar_time_t          event_time               __attribute__((aligned(8)));
	mar_any_value_u     correlated_notifications __attribute__((aligned(8)));
	mar_any_value_u     additional_text          __attribute__((aligned(8)));
//	mar_ntf_attribute_t additional_info          __attribute__((aligned(8)));

} mar_ntf_notification_header_t __attribute__((aligned(8)));

typedef struct {
	coroipc_request_header_t      header              __attribute__((aligned(8)));
	mar_ntf_notification_header_t notification_header __attribute__((aligned(8)));
} mar_ntf_misc_notification_t;

typedef struct {
	coroipc_request_header_t      header              __attribute__((aligned(8)));
	mar_ntf_notification_header_t notification_header __attribute__((aligned(8)));
	mar_uint16_t                  num_attributes      __attribute__((aligned(8)));
	mar_uint32_t                  source_indicator    __attribute__((aligned(8)));
	mar_any_value_u               object_attributes   __attribute__((aligned(8)));
} mar_ntf_object_notification_t;

typedef struct {
	coroipc_request_header_t      header              __attribute__((aligned(8)));
	mar_ntf_notification_header_t notification_header __attribute__((aligned(8)));
} mar_ntf_attribute_notification_t;

typedef struct {
     mar_uint16_t state_id          __attribute__((aligned(8)));
     SaBoolT      old_state_present __attribute__((aligned(8)));
     mar_uint64_t old_state         __attribute__((aligned(8)));
     mar_uint64_t new_state         __attribute__((aligned(8)));
} mar_ntf_state_change_t;

typedef struct {
	coroipc_request_header_t      header              __attribute__((aligned(8)));
	mar_ntf_notification_header_t notification_header __attribute__((aligned(8)));
	SaNtfSourceIndicatorT         source_indicator    __attribute__((aligned(8)));
	mar_any_value_u               changed_states      __attribute__((aligned(8)));
} mar_ntf_state_notification_t;

typedef struct {
	coroipc_request_header_t      header              __attribute__((aligned(8)));
	mar_ntf_notification_header_t notification_header __attribute__((aligned(8)));
	SaNtfProbableCauseT           probable_cause      __attribute__((aligned(8)));
    SaNtfSeverityT                perceived_severity  __attribute__((aligned(8)));
    SaNtfSeverityTrendT           trend               __attribute__((aligned(8)));
/*
    ntf->specificProblems = malloc (sizeof (SaNtfSpecificProblemT) *numSpecificProblems);
    ntf->thresholdInformation = malloc (sizeof (SaNtfThresholdInformationT));
	ntf->monitoredAttributes = malloc (sizeof (SaNtfAttributeT) * numMonitoredAttributes);
    ntf->proposedRepairActions = malloc (sizeof (SaNtfProposedRepairActionT) * numProposedRepairActions);
*/
} mar_ntf_alarm_notification_t;

typedef struct {
	coroipc_request_header_t      header              __attribute__((aligned(8)));
	mar_ntf_notification_header_t notification_header __attribute__((aligned(8)));
} mar_ntf_security_notification_t;


typedef struct {
	hdb_handle_t ipc_handle;
	SaNtfCallbacksT_3 callbacks;
	int finalize;
	SaNtfHandleT ntf_handle;
	pthread_mutex_t response_mutex;
	pthread_mutex_t dispatch_mutex;
} ntf_instance_t;

typedef struct notification_holder {
	ntf_instance_t     *ntfInstance;
	SaNtfNotificationTypeT notificationType;
	union
	{
		SaNtfMiscellaneousNotificationT      *misc;
		SaNtfObjectCreateDeleteNotificationT *object;
		SaNtfAttributeChangeNotificationT    *attribute;
		SaNtfStateChangeNotificationT_3      *state;
		SaNtfAlarmNotificationT              *alarm;
		SaNtfSecurityAlarmNotificationT      *security;
	} sa_notification;
	union
	{
		mar_ntf_misc_notification_t      *misc;
		mar_ntf_object_notification_t    *object;
		mar_ntf_attribute_notification_t *attribute;
		mar_ntf_state_notification_t     *state;
		mar_ntf_alarm_notification_t     *alarm;
		mar_ntf_security_notification_t  *security;
	} notification;
} notification_holder_t;

typedef enum {
	MESSAGE_REQ_NTF_SEND,
	/*MESSAGE_REQ_NTF_SEND_WITH_ID,*/
	MESSAGE_REQ_NTF_FLT_SUBSCRIBE,
} req_lib_ntf_types_t;

typedef enum {
	MESSAGE_RES_NTF_SEND_WITH_ID,
	MESSAGE_RES_NTF_SEND,
	MESSAGE_RES_NTF_FLT_SUBSCRIBE,

	MESSAGE_RES_NTF_DISPATCH,
} res_lib_ntf_types_t;



typedef struct {
	coroipc_response_header_t header __attribute__((aligned(8)));
} res_lib_ntf_send_t;




/*
 * marshalling types for filters
 * --------------------------------------------------------------------------
 */

typedef struct {
	mar_uint16_t    type                   __attribute__((aligned(8)));
	mar_any_value_u event_types            __attribute__((aligned(8)));
	mar_any_value_u notification_objects   __attribute__((aligned(8)));
	mar_any_value_u notifying_objects      __attribute__((aligned(8)));
	mar_any_value_u notification_class_ids __attribute__((aligned(8)));
} mar_ntf_filter_header_t __attribute__((aligned(8)));

typedef struct {
	coroipc_request_header_t header        __attribute__((aligned(8)));
	mar_ntf_filter_header_t  filter_header __attribute__((aligned(8)));
} mar_ntf_filter_misc_t;

typedef struct {
	coroipc_request_header_t header        __attribute__((aligned(8)));
	mar_ntf_filter_header_t  filter_header __attribute__((aligned(8)));
} mar_ntf_filter_state_t;

typedef struct notification_filter_holder_s {
	ntf_instance_t *ntfInstance;
	SaNtfNotificationTypeT notificationType;
	SaNtfSubscriptionIdT subscriptionId;
	union
	{
#if 0
		SaNtfObjectCreateDeleteNotificationFilterT  *objectCreateDeleteFilter;
		SaNtfAttributeChangeNotificationFilterT     *attributeChangeFilter;
		SaNtfStateChangeNotificationFilterT_3       *stateChangeFilter;
		SaNtfAlarmNotificationFilterT               *alarmFilter;
		SaNtfSecurityAlarmNotificationFilterT       *securityAlarmFilter;
#endif
		SaNtfMiscellaneousNotificationFilterT       *misc;
	} sa_filter_pt;
	union
	{
		mar_ntf_filter_misc_t * misc;
		mar_ntf_filter_state_t * state;
	} filter_pt;
} notification_filter_holder_t;

typedef struct {
	coroipc_response_header_t header __attribute__((aligned(8)));
} res_lib_ntf_flt_subscribe_t;


typedef struct {
	coroipc_request_header_t header __attribute__((aligned(8)));
	mar_uint32_t id __attribute__((aligned(8)));
} req_lib_ntf_flt_unsubscribe_t;


typedef struct {
	coroipc_response_header_t header __attribute__((aligned(8)));
} res_lib_ntf_flt_unsubscribe_t;

#endif /* IPC_NTF_H_DEFINED */

