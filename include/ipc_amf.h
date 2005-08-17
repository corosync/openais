/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
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
#ifndef AIS_IPC_AMF_H_DEFINED
#define AIS_IPC_AMF_H_DEFINED

#include <netinet/in.h>
#include "ipc_gen.h"
#include "saAis.h"
#include "ais_amf.h"

enum req_amf_response_interfaces {
	MESSAGE_REQ_AMF_RESPONSE_SAAMFHEALTHCHECKCALLBACK = 1,
	MESSAGE_REQ_AMF_RESPONSE_SAAMFREADINESSSTATESETCALLBACK,
	MESSAGE_REQ_AMF_RESPONSE_SAAMFCOMPONENTTERMINATECALLBACK,
	MESSAGE_REQ_AMF_RESPONSE_SAAMFCSISETCALLBACK,
	MESSAGE_REQ_AMF_RESPONSE_SAAMFCSIREMOVECALLBACK,
	MESSAGE_REQ_AMF_RESPONSE_SAAMFEXTERNALCOMPONENTRESTARTCALLBACK,
	MESSAGE_REQ_AMF_RESPONSE_SAAMFEXTERNALCOMPONENTCONTROLCALLBACK,
	MESSAGE_REQ_AMF_RESPONSE_SAAMFPENDINGOPERATIONCONFIRMCALLBACK
};

enum req_amf_types {
	MESSAGE_REQ_AMF_COMPONENTREGISTER = 0,
	MESSAGE_REQ_AMF_COMPONENTUNREGISTER,
	MESSAGE_REQ_AMF_READINESSSTATEGET,
	MESSAGE_REQ_AMF_HASTATEGET,
	MESSAGE_REQ_AMF_PROTECTIONGROUPTRACKSTART,
	MESSAGE_REQ_AMF_PROTECTIONGROUPTRACKSTOP,
	MESSAGE_REQ_AMF_ERRORREPORT,
	MESSAGE_REQ_AMF_ERRORCANCELALL,
	MESSAGE_REQ_AMF_STOPPINGCOMPLETE,
	MESSAGE_REQ_AMF_RESPONSE,
	MESSAGE_REQ_AMF_COMPONENTCAPABILITYMODELGET
};

enum res_lib_amf_types {
	MESSAGE_RES_AMF_COMPONENTREGISTER = 0,
	MESSAGE_RES_AMF_COMPONENTUNREGISTER,
	MESSAGE_RES_AMF_READINESSSTATEGET,
	MESSAGE_RES_AMF_HASTATEGET,
	MESSAGE_RES_AMF_HEALTHCHECKCALLBACK,
	MESSAGE_RES_AMF_READINESSSTATESETCALLBACK,
	MESSAGE_RES_AMF_COMPONENTTERMINATECALLBACK,
	MESSAGE_RES_AMF_CSISETCALLBACK,
	MESSAGE_RES_AMF_CSIREMOVECALLBACK,
	MESSAGE_RES_AMF_PROTECTIONGROUPTRACKSTART,
	MESSAGE_RES_AMF_PROTECTIONGROUPTRACKCALLBACK,
	MESSAGE_RES_AMF_PROTECTIONGROUPTRACKSTOP,
	MESSAGE_RES_AMF_COMPONENTCAPABILITYMODELGET,
	MESSAGE_RES_AMF_ERRORREPORT,
	MESSAGE_RES_AMF_ERRORCANCELALL,
	MESSAGE_RES_AMF_STOPPINGCOMPLETE,
	MESSAGE_RES_AMF_RESPONSE
};

struct req_lib_amf_componentregister {
	struct req_header header;
	SaNameT compName;
	SaNameT proxyCompName;
} __attribute__((packed));

struct res_lib_amf_componentregister {
	struct res_header header;
};

struct req_lib_amf_componentunregister {
	struct req_header header;
	SaNameT compName;
	SaNameT proxyCompName;
};

struct res_lib_amf_componentunregister {
	struct res_header header;
};

struct req_amf_readinessstateget {
	struct req_header header;
	SaNameT compName;
};

struct res_lib_amf_readinessstateget {
	struct res_header header;
	SaAmfReadinessStateT readinessState;
};

struct res_lib_amf_healthcheckcallback {
	struct res_header header;
	int instance;
	SaInvocationT invocation;
	SaNameT compName;
	SaAmfHealthcheckT checkType;
};

struct res_lib_amf_readinessstatesetcallback {
	struct res_header header;
	SaInvocationT invocation;
	SaNameT compName;
	SaAmfReadinessStateT readinessState;
};

struct res_lib_amf_componentterminatecallback {
	struct res_header header;
	SaInvocationT invocation;
	SaNameT compName;
};

struct req_amf_hastateget {
	struct req_header header;
	SaNameT compName;
	SaNameT csiName;
};

struct res_lib_amf_hastateget {
	struct res_header header;
	SaAmfHAStateT haState;
};

struct res_lib_amf_csisetcallback {
	struct res_header header;
	SaInvocationT invocation;
	SaNameT compName;
	SaNameT csiName;
	SaAmfCSIFlagsT csiFlags;
	SaAmfHAStateT haState;
	SaNameT activeCompName;
	SaAmfCSITransitionDescriptorT transitionDescriptor;
};

struct res_lib_amf_csiremovecallback {
	struct res_header header;
	SaInvocationT invocation;
	SaNameT compName;
	SaNameT csiName;
	SaAmfCSIFlagsT csiFlags;
};

struct req_amf_protectiongrouptrackstart {
	struct req_header header;
	SaNameT csiName;
	SaUint8T trackFlags;
	SaAmfProtectionGroupNotificationT *notificationBufferAddress;
	SaUint32T numberOfItems;
};

struct res_lib_amf_protectiongrouptrackstart {
	struct res_header header;
};
	

struct req_amf_protectiongrouptrackstop {
	struct req_header header;
	SaNameT csiName;
};

struct res_lib_amf_protectiongrouptrackstop {
	struct res_header header;
};

struct res_lib_amf_protectiongrouptrackcallback {
	struct res_header header;
	SaNameT csiName;
	SaAmfProtectionGroupNotificationT *notificationBufferAddress;
	SaUint32T numberOfItems;
	SaUint32T numberOfMembers;
	SaUint32T error;
	SaAmfProtectionGroupNotificationT notificationBuffer[0];
};

struct req_lib_amf_errorreport {
	struct req_header header;
	SaNameT reportingComponent;
	SaNameT erroneousComponent;
	SaTimeT errorDetectionTime;
	SaAmfErrorDescriptorT errorDescriptor;
	SaAmfAdditionalDataT additionalData;
};

struct res_lib_amf_errorreport {
	struct res_header header;
};

struct req_lib_amf_errorcancelall {
	struct req_header header;
	SaNameT compName;
};

struct res_lib_amf_errorcancelall {
	struct res_header header;
};

struct req_amf_response {
	struct req_header header;
	SaInvocationT invocation;
	SaErrorT error;
};

struct res_lib_amf_response {
	struct res_header header;
};

struct req_amf_stoppingcomplete {
	struct req_header header;
	SaInvocationT invocation;
	SaErrorT error;
};

struct res_lib_amf_stoppingcomplete {
	struct res_header header;
};

struct req_amf_componentcapabilitymodelget {
	struct req_header header;
	SaNameT compName;
};

struct res_lib_amf_componentcapabilitymodelget {
	struct res_header header;
	SaAmfComponentCapabilityModelT componentCapabilityModel;
};

#endif /* AIS_IPC_AMF_H_DEFINED */
