
/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>

#include <corosync/coroipcc.h>
#include <corosync/coroipc_types.h>
#include <corosync/corodefs.h>
#include <corosync/hdb.h>

#include <saAis.h>
#include <saAmf.h>
#include <ipc_amf.h>

#include "util.h"

/*
 * Data structure for instance data
 */
struct amfInstance {
	hdb_handle_t handle;
	SaAmfCallbacksT callbacks;
	SaNameT compName;
	int compRegistered;
	int finalize;
};

/*
 * All instances in one database
 */
DECLARE_HDB_DATABASE(amfHandleDatabase,NULL);

/*
 * Versions supported
 */
static SaVersionT amfVersionsSupported[] = {
	{ 'B', 1, 1 }
};

static struct saVersionDatabase amfVersionDatabase = {
	sizeof (amfVersionsSupported) / sizeof (SaVersionT),
	amfVersionsSupported
};

/*
 * Implementation
 */

SaAisErrorT
saAmfInitialize (
	SaAmfHandleT *amfHandle,
	const SaAmfCallbacksT *amfCallbacks,
	SaVersionT *version)
{
	struct amfInstance *amfInstance;
	SaAisErrorT error = SA_AIS_OK;

	error = saVersionVerify (&amfVersionDatabase, (SaVersionT *)version);
	if (error != SA_AIS_OK) {
		goto error_no_destroy;
	}

	error = hdb_error_to_sa(hdb_handle_create (&amfHandleDatabase, sizeof (struct amfInstance), amfHandle));
	if (error != SA_AIS_OK) {
		goto error_no_destroy;
	}

	error = hdb_error_to_sa(hdb_handle_get (&amfHandleDatabase, *amfHandle, (void *)&amfInstance));
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	error = coroipcc_service_connect (
		COROSYNC_SOCKET_NAME,
		CPG_SERVICE,
		IPC_REQUEST_SIZE,
		IPC_RESPONSE_SIZE,
		IPC_DISPATCH_SIZE,
		&amfInstance->handle);
	if (error != SA_AIS_OK) {
		goto error_put_destroy;
	}

	memcpy (&amfInstance->callbacks, amfCallbacks, sizeof (SaAmfCallbacksT));

	hdb_handle_put (&amfHandleDatabase, *amfHandle);

	return (SA_AIS_OK);

error_put_destroy:
	hdb_handle_put (&amfHandleDatabase, *amfHandle);
error_destroy:
	hdb_handle_destroy (&amfHandleDatabase, *amfHandle);
error_no_destroy:
	return (error);
}

SaAisErrorT
saAmfSelectionObjectGet (
	SaAmfHandleT amfHandle,
	SaSelectionObjectT *selectionObject)
{
	struct amfInstance *amfInstance;
	SaAisErrorT error;
	int fd;

	error = hdb_error_to_sa(hdb_handle_get (&amfHandleDatabase, amfHandle, (void *)&amfInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	error = coroipcc_fd_get (amfInstance->handle, &fd);
	*selectionObject = fd;

	hdb_handle_put (&amfHandleDatabase, amfHandle);
	return (error);
}


SaAisErrorT
saAmfDispatch (
	SaAmfHandleT amfHandle,
	SaDispatchFlagsT dispatchFlags)
{
	int timeout = -1;
	SaAisErrorT error;
	int cont = 1; /* always continue do loop except when set to 0 */
	struct amfInstance *amfInstance;
	struct res_lib_amf_csisetcallback *res_lib_amf_csisetcallback;

	struct res_lib_amf_healthcheckcallback *res_lib_amf_healthcheckcallback;
	struct res_lib_amf_csiremovecallback *res_lib_amf_csiremovecallback;
	struct res_lib_amf_componentterminatecallback *res_lib_amf_componentterminatecallback;
	SaAmfCallbacksT callbacks;
	coroipc_response_header_t *dispatch_data;

	if (dispatchFlags != SA_DISPATCH_ONE &&
		dispatchFlags != SA_DISPATCH_ALL &&
		dispatchFlags != SA_DISPATCH_BLOCKING) {

		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_sa(hdb_handle_get (&amfHandleDatabase, amfHandle,
		(void *)&amfInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	/*
	 * Timeout instantly for SA_DISPATCH_ALL, otherwise don't timeout
	 * for SA_DISPATCH_BLOCKING or SA_DISPATCH_ONE
	 */
	if (dispatchFlags == SA_DISPATCH_ALL) {
		timeout = 0;
	}

	do {
		error = coroipcc_dispatch_get (
			amfInstance->handle,
			(void **)&dispatch_data,
			timeout);
		if (error == CS_ERR_BAD_HANDLE) {
			error = CS_OK;
			goto error_put;
		}
		if (error == CS_ERR_TRY_AGAIN) {
			error = CS_OK;
			if (dispatchFlags == CPG_DISPATCH_ALL) {
				break; /* exit do while cont is 1 loop */
			} else {
				continue; /* next poll */
			}
		}
		if (error != CS_OK) {
			goto error_put;
		}

		/*
		 * Make copy of callbacks, message data, unlock instance, and call callback
		 * A risk of this dispatch method is that the callback routines may
		 * operate at the same time that amfFinalize has been called in another thread.
		 */

		memcpy (&callbacks, &amfInstance->callbacks, sizeof (SaAmfCallbacksT));

		/*
		 * Dispatch incoming response
		 */

		switch (dispatch_data->id) {

		case MESSAGE_RES_AMF_HEALTHCHECKCALLBACK:
			res_lib_amf_healthcheckcallback = (struct res_lib_amf_healthcheckcallback *)dispatch_data;

			callbacks.saAmfHealthcheckCallback (
				res_lib_amf_healthcheckcallback->invocation,
				&res_lib_amf_healthcheckcallback->compName,
				&res_lib_amf_healthcheckcallback->key);
			break;

		case MESSAGE_RES_AMF_CSISETCALLBACK:
		    {
			SaAmfCSIDescriptorT csi_descriptor;
			SaAmfCSIAttributeT *csi_attribute_array;
			char *attr_buf;
			int i;

			res_lib_amf_csisetcallback = (struct res_lib_amf_csisetcallback *)dispatch_data;


			csi_descriptor.csiFlags = res_lib_amf_csisetcallback->csiFlags;
			memcpy(&csi_descriptor.csiName, &res_lib_amf_csisetcallback->csiName,
			       sizeof(SaNameT));
			csi_descriptor.csiStateDescriptor = res_lib_amf_csisetcallback->csiStateDescriptor;
			csi_descriptor.csiAttr.number = res_lib_amf_csisetcallback->number;

			csi_attribute_array = malloc (sizeof (SaAmfCSIAttributeT) *
									  csi_descriptor.csiAttr.number);

			if (csi_attribute_array == 0) {
			    return SA_AIS_ERR_LIBRARY;
			}
			csi_descriptor.csiAttr.attr = csi_attribute_array;

			attr_buf = res_lib_amf_csisetcallback->csi_attr_buf;

			for (i = 0; i < csi_descriptor.csiAttr.number; i++) {
				csi_attribute_array[i].attrName = (SaUint8T*)attr_buf;

				attr_buf += strlen(attr_buf) + 1;
				csi_attribute_array[i].attrValue = (SaUint8T*)attr_buf;

				attr_buf += strlen(attr_buf) + 1;
			}

			callbacks.saAmfCSISetCallback (
				res_lib_amf_csisetcallback->invocation,
				&res_lib_amf_csisetcallback->compName,
				res_lib_amf_csisetcallback->haState,
				&csi_descriptor);

			free(csi_attribute_array);
			break;
		    }
		case MESSAGE_RES_AMF_CSIREMOVECALLBACK:
			res_lib_amf_csiremovecallback = (struct res_lib_amf_csiremovecallback *)dispatch_data;
			callbacks.saAmfCSIRemoveCallback (
				res_lib_amf_csiremovecallback->invocation,
				&res_lib_amf_csiremovecallback->compName,
				&res_lib_amf_csiremovecallback->csiName,
				res_lib_amf_csiremovecallback->csiFlags);
			break;

		case MESSAGE_RES_AMF_COMPONENTTERMINATECALLBACK:
			res_lib_amf_componentterminatecallback = (struct res_lib_amf_componentterminatecallback *)dispatch_data;
			callbacks.saAmfComponentTerminateCallback (
				res_lib_amf_componentterminatecallback->invocation,
				&res_lib_amf_componentterminatecallback->compName);
			break;

#ifdef COMPILE_OUT
		case MESSAGE_RES_AMF_PROTECTIONGROUPTRACKCALLBACK:
			res_lib_amf_protectiongrouptrackcallback = (struct res_lib_amf_protectiongrouptrackcallback *)dispatch_data;
			memcpy (res_lib_amf_protectiongrouptrackcallback->notificationBufferAddress,
				res_lib_amf_protectiongrouptrackcallback->notificationBuffer,
				res_lib_amf_protectiongrouptrackcallback->numberOfItems * sizeof (SaAmfProtectionGroupNotificationT));
			callbacks.saAmfProtectionGroupTrackCallback(
				&res_lib_amf_protectiongrouptrackcallback->csiName,
				res_lib_amf_protectiongrouptrackcallback->notificationBufferAddress,
				res_lib_amf_protectiongrouptrackcallback->numberOfItems,
				res_lib_amf_protectiongrouptrackcallback->numberOfMembers,
				res_lib_amf_protectiongrouptrackcallback->error);
#endif
			break;
		default:
			coroipcc_dispatch_put (amfInstance->handle);
			error = SA_AIS_ERR_LIBRARY;
			goto error_put;
			break;
		}
		coroipcc_dispatch_put (amfInstance->handle);

		/*
		 * Determine if more messages should be processed
		 */
		switch (dispatchFlags) {
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
	hdb_handle_put (&amfHandleDatabase, amfHandle);
error_exit:
	return (error);
}

SaAisErrorT
saAmfFinalize (
	SaAmfHandleT amfHandle)
{
	struct amfInstance *amfInstance;
	SaAisErrorT error;

	error = hdb_error_to_sa(hdb_handle_get (&amfHandleDatabase, amfHandle, (void *)&amfInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	/*
	 * Another thread has already started finalizing
	 */
	if (amfInstance->finalize) {
		hdb_handle_put (&amfHandleDatabase, amfHandle);
		return (SA_AIS_ERR_BAD_HANDLE);
	}

	amfInstance->finalize = 1;

	coroipcc_service_disconnect (amfInstance->handle);

	hdb_handle_destroy (&amfHandleDatabase, amfHandle);

	hdb_handle_put (&amfHandleDatabase, amfHandle);

	return (error);
}

SaAisErrorT
saAmfComponentRegister (
	SaAmfHandleT amfHandle,
	const SaNameT *compName,
	const SaNameT *proxyCompName)
{
	struct amfInstance *amfInstance;
	SaAisErrorT error;
	struct req_lib_amf_componentregister req_lib_amf_componentregister;
	struct res_lib_amf_componentregister res_lib_amf_componentregister;
	struct iovec iov;

	error = hdb_error_to_sa(hdb_handle_get (&amfHandleDatabase, amfHandle,
		(void *)&amfInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_amf_componentregister.header.size = sizeof (struct req_lib_amf_componentregister);
	req_lib_amf_componentregister.header.id = MESSAGE_REQ_AMF_COMPONENTREGISTER;
	memcpy (&req_lib_amf_componentregister.compName, compName,
		sizeof (SaNameT));
	if (proxyCompName) {
		memcpy (&req_lib_amf_componentregister.proxyCompName,
			proxyCompName, sizeof (SaNameT));
	} else {
		memset (&req_lib_amf_componentregister.proxyCompName, 0,
			sizeof (SaNameT));
	}

	iov.iov_base = (void *)&req_lib_amf_componentregister;
	iov.iov_len = sizeof (struct req_lib_amf_componentregister);

	error = coroipcc_msg_send_reply_receive (amfInstance->handle,
		&iov,
		1,
		&res_lib_amf_componentregister,
		sizeof (struct res_lib_amf_componentregister));

	hdb_handle_put (&amfHandleDatabase, amfHandle);

	if (res_lib_amf_componentregister.header.error == SA_AIS_OK) {
		amfInstance->compRegistered = 1;
		memcpy (&amfInstance->compName, compName, sizeof (SaNameT));
	}
        return (error == SA_AIS_OK ? res_lib_amf_componentregister.header.error : error);
}

SaAisErrorT
saAmfComponentUnregister (
	SaAmfHandleT amfHandle,
	const SaNameT *compName,
	const SaNameT *proxyCompName)
{
	struct req_lib_amf_componentunregister req_lib_amf_componentunregister;
	struct res_lib_amf_componentunregister res_lib_amf_componentunregister;
	struct amfInstance *amfInstance;
	SaAisErrorT error;
	struct iovec iov;

	error = hdb_error_to_sa(hdb_handle_get (&amfHandleDatabase, amfHandle,
		(void *)&amfInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_amf_componentunregister.header.size = sizeof (struct req_lib_amf_componentunregister);
	req_lib_amf_componentunregister.header.id = MESSAGE_REQ_AMF_COMPONENTUNREGISTER;
	memcpy (&req_lib_amf_componentunregister.compName, compName,
		sizeof (SaNameT));
	if (proxyCompName) {
		memcpy (&req_lib_amf_componentunregister.proxyCompName,
			proxyCompName, sizeof (SaNameT));
	} else {
		memset (&req_lib_amf_componentunregister.proxyCompName, 0,
			sizeof (SaNameT));
	}

	iov.iov_base = (void *)&req_lib_amf_componentunregister;
	iov.iov_len = sizeof (struct req_lib_amf_componentunregister);

	error = coroipcc_msg_send_reply_receive (amfInstance->handle,
		&iov,
		1,
		&res_lib_amf_componentunregister,
		sizeof (struct res_lib_amf_componentunregister));

	hdb_handle_put (&amfHandleDatabase, amfHandle);

        return (error == SA_AIS_OK ? res_lib_amf_componentunregister.header.error : error);
}

SaAisErrorT
saAmfComponentNameGet (
	SaAmfHandleT amfHandle,
	SaNameT *compName)
{
	struct amfInstance *amfInstance;
	SaAisErrorT error;
	char *env_value;

	error = hdb_error_to_sa(hdb_handle_get (&amfHandleDatabase, amfHandle,
		(void *)&amfInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	error = SA_AIS_OK;

	env_value = getenv ("SA_AMF_COMPONENT_NAME");
	if (env_value == 0) {
		error = SA_AIS_ERR_NOT_EXIST;
		goto error_exit;
	}

	strncpy ((char *)compName->value, env_value, SA_MAX_NAME_LENGTH-1);
	compName->value[SA_MAX_NAME_LENGTH-1] = '\0';
	compName->length = strlen (env_value);

error_exit:
	hdb_handle_put (&amfHandleDatabase, amfHandle);

	return (error);
}

SaAisErrorT
saAmfPmStart (
	SaAmfHandleT amfHandle,
	const SaNameT *compName,
	SaUint64T processId,
	SaInt32T descendentsTreeDepth,
	SaAmfPmErrorsT pmErrors,
	SaAmfRecommendedRecoveryT recommendedRecovery)
{
	struct req_lib_amf_pmstart req_lib_amf_pmstart;
	struct res_lib_amf_pmstart res_lib_amf_pmstart;
	struct amfInstance *amfInstance;
	SaAisErrorT error;
	struct iovec iov;

	error = hdb_error_to_sa(hdb_handle_get (&amfHandleDatabase, amfHandle,
		(void *)&amfInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_amf_pmstart.header.size = sizeof (struct req_lib_amf_pmstart);
	req_lib_amf_pmstart.header.id = MESSAGE_REQ_AMF_PMSTART;
	memcpy (&req_lib_amf_pmstart.compName, compName,
		sizeof (SaNameT));
	req_lib_amf_pmstart.processId = processId;
	req_lib_amf_pmstart.descendentsTreeDepth = descendentsTreeDepth;
	req_lib_amf_pmstart.pmErrors = pmErrors;
	req_lib_amf_pmstart.recommendedRecovery = recommendedRecovery;

	iov.iov_base = (void *)&req_lib_amf_pmstart;
	iov.iov_len = sizeof (struct req_lib_amf_pmstart);

	error = coroipcc_msg_send_reply_receive (amfInstance->handle,
		&iov,
		1,
		&res_lib_amf_pmstart,
		sizeof (struct res_lib_amf_pmstart));

	hdb_handle_put (&amfHandleDatabase, amfHandle);

        return (error == SA_AIS_OK ? res_lib_amf_pmstart.header.error : error);
}

SaAisErrorT
saAmfPmStop (
	SaAmfHandleT amfHandle,
	const SaNameT *compName,
	SaAmfPmStopQualifierT stopQualifier,
	SaInt64T processId,
	SaAmfPmErrorsT pmErrors)
{
	struct req_lib_amf_pmstop req_lib_amf_pmstop;
	struct res_lib_amf_pmstop res_lib_amf_pmstop;
	struct amfInstance *amfInstance;
	SaAisErrorT error;
	struct iovec iov;

	error = hdb_error_to_sa(hdb_handle_get (&amfHandleDatabase, amfHandle,
		(void *)&amfInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_amf_pmstop.header.size = sizeof (struct req_lib_amf_pmstop);
	req_lib_amf_pmstop.header.id = MESSAGE_REQ_AMF_PMSTOP;
	memcpy (&req_lib_amf_pmstop.compName, compName, sizeof (SaNameT));
	req_lib_amf_pmstop.stopQualifier = stopQualifier;
	req_lib_amf_pmstop.processId = processId;
	req_lib_amf_pmstop.pmErrors = pmErrors;

	iov.iov_base = (void *)&req_lib_amf_pmstop;
	iov.iov_len = sizeof (struct req_lib_amf_pmstop);

	error = coroipcc_msg_send_reply_receive (amfInstance->handle,
		&iov,
		1,
		&res_lib_amf_pmstop,
		sizeof (struct res_lib_amf_pmstop));

	hdb_handle_put (&amfHandleDatabase, amfHandle);

        return (error == SA_AIS_OK ? res_lib_amf_pmstop.header.error : error);
	return (SA_AIS_OK);
}

SaAisErrorT
saAmfHealthcheckStart (
	SaAmfHandleT amfHandle,
	const SaNameT *compName,
	const SaAmfHealthcheckKeyT *healthcheckKey,
	SaAmfHealthcheckInvocationT invocationType,
	SaAmfRecommendedRecoveryT recommendedRecovery)
{
	struct req_lib_amf_healthcheckstart req_lib_amf_healthcheckstart;
	struct res_lib_amf_healthcheckstart res_lib_amf_healthcheckstart;
	struct amfInstance *amfInstance;
	SaAisErrorT error;
	struct iovec iov;

	error = hdb_error_to_sa(hdb_handle_get (&amfHandleDatabase, amfHandle,
		(void *)&amfInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_amf_healthcheckstart.header.size = sizeof (struct req_lib_amf_healthcheckstart);
	req_lib_amf_healthcheckstart.header.id = MESSAGE_REQ_AMF_HEALTHCHECKSTART;
	memcpy (&req_lib_amf_healthcheckstart.compName, compName,
		sizeof (SaNameT));
	memcpy (&req_lib_amf_healthcheckstart.healthcheckKey,
		healthcheckKey, sizeof (SaAmfHealthcheckKeyT));
	req_lib_amf_healthcheckstart.invocationType = invocationType;
	req_lib_amf_healthcheckstart.recommendedRecovery = recommendedRecovery;

	iov.iov_base = (void *)&req_lib_amf_healthcheckstart;
	iov.iov_len = sizeof (struct req_lib_amf_healthcheckstart);

	error = coroipcc_msg_send_reply_receive (amfInstance->handle,
		&iov,
		1,
		&res_lib_amf_healthcheckstart,
		sizeof (struct res_lib_amf_healthcheckstart));

	hdb_handle_put (&amfHandleDatabase, amfHandle);

        return (error == SA_AIS_OK ? res_lib_amf_healthcheckstart.header.error : error);
}

SaAisErrorT
saAmfHealthcheckConfirm (
	SaAmfHandleT amfHandle,
	const SaNameT *compName,
	const SaAmfHealthcheckKeyT *healthcheckKey,
	SaAisErrorT healthcheckResult)
{
	struct req_lib_amf_healthcheckconfirm req_lib_amf_healthcheckconfirm;
	struct res_lib_amf_healthcheckconfirm res_lib_amf_healthcheckconfirm;
	struct amfInstance *amfInstance;
	SaAisErrorT error;
	struct iovec iov;

	error = hdb_error_to_sa(hdb_handle_get (&amfHandleDatabase, amfHandle,
		(void *)&amfInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_amf_healthcheckconfirm.header.size = sizeof (struct req_lib_amf_healthcheckconfirm);
	req_lib_amf_healthcheckconfirm.header.id = MESSAGE_REQ_AMF_HEALTHCHECKCONFIRM;
	memcpy (&req_lib_amf_healthcheckconfirm.compName, compName,
		sizeof (SaNameT));
	memcpy (&req_lib_amf_healthcheckconfirm.healthcheckKey,
		healthcheckKey, sizeof (SaAmfHealthcheckKeyT));
	req_lib_amf_healthcheckconfirm.healthcheckResult = healthcheckResult;

	iov.iov_base = (void *)&req_lib_amf_healthcheckconfirm;
	iov.iov_len = sizeof (struct req_lib_amf_healthcheckconfirm);

	error = coroipcc_msg_send_reply_receive (amfInstance->handle,
		&iov,
		1,
		&res_lib_amf_healthcheckconfirm,
		sizeof (struct res_lib_amf_healthcheckconfirm));

	hdb_handle_put (&amfHandleDatabase, amfHandle);

        return (error == SA_AIS_OK ? res_lib_amf_healthcheckconfirm.header.error : error);
}

SaAisErrorT
saAmfHealthcheckStop (
	SaAmfHandleT amfHandle,
	const SaNameT *compName,
	const SaAmfHealthcheckKeyT *healthcheckKey)
{
	struct req_lib_amf_healthcheckstop req_lib_amf_healthcheckstop;
	struct res_lib_amf_healthcheckstop res_lib_amf_healthcheckstop;
	struct amfInstance *amfInstance;
	struct iovec iov;
	SaAisErrorT error;

	error = hdb_error_to_sa(hdb_handle_get (&amfHandleDatabase, amfHandle,
		(void *)&amfInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_amf_healthcheckstop.header.size = sizeof (struct req_lib_amf_healthcheckstop);
	req_lib_amf_healthcheckstop.header.id = MESSAGE_REQ_AMF_HEALTHCHECKSTOP;
	memcpy (&req_lib_amf_healthcheckstop.compName, compName,
		sizeof (SaNameT));
	memcpy (&req_lib_amf_healthcheckstop.healthcheckKey,
		healthcheckKey, sizeof (SaAmfHealthcheckKeyT));

	iov.iov_base = (void *)&req_lib_amf_healthcheckstop;
	iov.iov_len = sizeof (struct req_lib_amf_healthcheckstop);

	error = coroipcc_msg_send_reply_receive (amfInstance->handle,
		&iov,
		1,
		&res_lib_amf_healthcheckstop,
		sizeof (struct res_lib_amf_healthcheckstop));

	hdb_handle_put (&amfHandleDatabase, amfHandle);

        return (error == SA_AIS_OK ? res_lib_amf_healthcheckstop.header.error : error);
}


SaAisErrorT
saAmfHAStateGet (
	SaAmfHandleT amfHandle,
	const SaNameT *compName,
	const SaNameT *csiName,
	SaAmfHAStateT *haState)
{
	struct amfInstance *amfInstance;
	struct req_lib_amf_hastateget req_lib_amf_hastateget;
	struct res_lib_amf_hastateget res_lib_amf_hastateget;
	struct iovec iov;
	SaAisErrorT error;

	error = hdb_error_to_sa(hdb_handle_get (&amfHandleDatabase, amfHandle,
		(void *)&amfInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	iov.iov_base = (void *)&req_lib_amf_hastateget,
	iov.iov_len = sizeof (struct req_lib_amf_hastateget),

	req_lib_amf_hastateget.header.id = MESSAGE_REQ_AMF_HASTATEGET;
	req_lib_amf_hastateget.header.size = sizeof (struct req_lib_amf_hastateget);
	memcpy (&req_lib_amf_hastateget.compName, compName, sizeof (SaNameT));
	memcpy (&req_lib_amf_hastateget.csiName, csiName, sizeof (SaNameT));

	error = coroipcc_msg_send_reply_receive (amfInstance->handle,
		&iov,
		1,
		&res_lib_amf_hastateget,
		sizeof (struct res_lib_amf_hastateget));

	hdb_handle_put (&amfHandleDatabase, amfHandle);

	if (res_lib_amf_hastateget.header.error == SA_AIS_OK) {
		memcpy (haState, &res_lib_amf_hastateget.haState,
			sizeof (SaAmfHAStateT));
	}
        return (error == SA_AIS_OK ? res_lib_amf_hastateget.header.error : error);
}

SaAisErrorT
saAmfCSIQuiescingComplete (
	SaAmfHandleT amfHandle,
	SaInvocationT invocation,
	SaAisErrorT error)
{
	struct req_lib_amf_csiquiescingcomplete req_lib_amf_csiquiescingcomplete;
	struct res_lib_amf_csiquiescingcomplete res_lib_amf_csiquiescingcomplete;
	struct amfInstance *amfInstance;
	struct iovec iov;
	SaAisErrorT errorResult;

	error = hdb_error_to_sa(hdb_handle_get (&amfHandleDatabase, amfHandle,
		(void *)&amfInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_amf_csiquiescingcomplete.header.size = sizeof (struct req_lib_amf_csiquiescingcomplete);
	req_lib_amf_csiquiescingcomplete.header.id = MESSAGE_REQ_AMF_CSIQUIESCINGCOMPLETE;
	req_lib_amf_csiquiescingcomplete.invocation = invocation;
	req_lib_amf_csiquiescingcomplete.error = error;

	iov.iov_base = (void *)&req_lib_amf_csiquiescingcomplete;
	iov.iov_len = sizeof (struct req_lib_amf_csiquiescingcomplete);

	errorResult = coroipcc_msg_send_reply_receive (amfInstance->handle,
		&iov,
		1,
		&res_lib_amf_csiquiescingcomplete,
		sizeof (struct res_lib_amf_csiquiescingcomplete));

	hdb_handle_put (&amfHandleDatabase, amfHandle);

        return (errorResult == SA_AIS_OK ? res_lib_amf_csiquiescingcomplete.header.error : errorResult);
}

SaAisErrorT
saAmfProtectionGroupTrack (
	SaAmfHandleT amfHandle,
	const SaNameT *csiName,
	SaUint8T trackFlags,
	SaAmfProtectionGroupNotificationBufferT *notificationBuffer)
{
	struct amfInstance *amfInstance;
	struct req_lib_amf_protectiongrouptrack req_lib_amf_protectiongrouptrack;
	struct res_lib_amf_protectiongrouptrack res_lib_amf_protectiongrouptrack;
	struct iovec iov;
	SaAisErrorT error;

	req_lib_amf_protectiongrouptrack.header.size = sizeof (struct req_lib_amf_protectiongrouptrack);
	req_lib_amf_protectiongrouptrack.header.id = MESSAGE_REQ_AMF_PROTECTIONGROUPTRACK;
	memcpy (&req_lib_amf_protectiongrouptrack.csiName, csiName,
		sizeof (SaNameT));
	req_lib_amf_protectiongrouptrack.trackFlags = trackFlags;
	req_lib_amf_protectiongrouptrack.notificationBufferAddress = (SaAmfProtectionGroupNotificationBufferT *)notificationBuffer;

	error = hdb_error_to_sa(hdb_handle_get (&amfHandleDatabase, amfHandle,
		(void *)&amfInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	iov.iov_base = (void *)&req_lib_amf_protectiongrouptrack;
	iov.iov_len = sizeof (struct req_lib_amf_protectiongrouptrack);

	error = coroipcc_msg_send_reply_receive (amfInstance->handle,
		&iov,
		1,
		&res_lib_amf_protectiongrouptrack,
		sizeof (struct res_lib_amf_protectiongrouptrack));

	hdb_handle_put (&amfHandleDatabase, amfHandle);

        return (error == SA_AIS_OK ? res_lib_amf_protectiongrouptrack.header.error : error);
}

SaAisErrorT
saAmfProtectionGroupTrackStop (
	SaAmfHandleT amfHandle,
	const SaNameT *csiName)
{
	struct amfInstance *amfInstance;
	struct req_lib_amf_protectiongrouptrackstop req_lib_amf_protectiongrouptrackstop;
	struct res_lib_amf_protectiongrouptrackstop res_lib_amf_protectiongrouptrackstop;
	struct iovec iov;
	SaAisErrorT error;

	error = hdb_error_to_sa(hdb_handle_get (&amfHandleDatabase, amfHandle,
		(void *)&amfInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_amf_protectiongrouptrackstop.header.size = sizeof (struct req_lib_amf_protectiongrouptrackstop);
	req_lib_amf_protectiongrouptrackstop.header.id = MESSAGE_REQ_AMF_PROTECTIONGROUPTRACKSTOP;
	memcpy (&req_lib_amf_protectiongrouptrackstop.csiName, csiName, sizeof (SaNameT));

	iov.iov_base = (void *)&req_lib_amf_protectiongrouptrackstop,
	iov.iov_len = sizeof (struct req_lib_amf_protectiongrouptrackstop),

	error = coroipcc_msg_send_reply_receive (amfInstance->handle,
		&iov,
		1,
		&res_lib_amf_protectiongrouptrackstop,
		sizeof (struct res_lib_amf_protectiongrouptrackstop));

	hdb_handle_put (&amfHandleDatabase, amfHandle);

        return (error == SA_AIS_OK ? res_lib_amf_protectiongrouptrackstop.header.error : error);
}

SaAisErrorT
saAmfComponentErrorReport (
	SaAmfHandleT amfHandle,
	const SaNameT *erroneousComponent,
	SaTimeT errorDetectionTime,
	SaAmfRecommendedRecoveryT recommendedRecovery,
	SaNtfIdentifierT ntfIdentifier)
{
	struct amfInstance *amfInstance;
	struct req_lib_amf_componenterrorreport req_lib_amf_componenterrorreport;
	struct res_lib_amf_componenterrorreport res_lib_amf_componenterrorreport;
	struct iovec iov;
	SaAisErrorT error;

	error = hdb_error_to_sa(hdb_handle_get (&amfHandleDatabase, amfHandle,
		(void *)&amfInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_amf_componenterrorreport.header.id = MESSAGE_REQ_AMF_COMPONENTERRORREPORT;
	req_lib_amf_componenterrorreport.header.size = sizeof (struct req_lib_amf_componenterrorreport);
	memcpy (&req_lib_amf_componenterrorreport.erroneousComponent, erroneousComponent,
		sizeof (SaNameT));
	req_lib_amf_componenterrorreport.errorDetectionTime = errorDetectionTime;
	req_lib_amf_componenterrorreport.recommendedRecovery = recommendedRecovery;

	iov.iov_base = (void *)&req_lib_amf_componenterrorreport;
	iov.iov_len = sizeof (struct req_lib_amf_componenterrorreport);

	error = coroipcc_msg_send_reply_receive (amfInstance->handle,
		&iov,
		1,
		&res_lib_amf_componenterrorreport,
		sizeof (struct res_lib_amf_componenterrorreport));


	error = res_lib_amf_componenterrorreport.header.error;

	hdb_handle_put (&amfHandleDatabase, amfHandle);

        return (error == SA_AIS_OK ? res_lib_amf_componenterrorreport.header.error : error);
}

SaAisErrorT
saAmfComponentErrorClear (
	SaAmfHandleT amfHandle,
	const SaNameT *compName,
	SaNtfIdentifierT ntfIdentifier)
{
	struct amfInstance *amfInstance;
	struct req_lib_amf_componenterrorclear req_lib_amf_componenterrorclear;
	struct res_lib_amf_componenterrorclear res_lib_amf_componenterrorclear;
	struct iovec iov;
	SaAisErrorT error;

	error = hdb_error_to_sa(hdb_handle_get (&amfHandleDatabase, amfHandle,
		(void *)&amfInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_amf_componenterrorclear.header.id = MESSAGE_REQ_AMF_COMPONENTERRORCLEAR;
	req_lib_amf_componenterrorclear.header.size = sizeof (struct req_lib_amf_componenterrorclear);
	memcpy (&req_lib_amf_componenterrorclear.compName, compName, sizeof (SaNameT));

	iov.iov_base = (void *)&req_lib_amf_componenterrorclear;
	iov.iov_len = sizeof (struct req_lib_amf_componenterrorclear);

	error = coroipcc_msg_send_reply_receive (amfInstance->handle,
		&iov,
		1,
		&res_lib_amf_componenterrorclear,
		sizeof (struct res_lib_amf_componenterrorclear));

	hdb_handle_put (&amfHandleDatabase, amfHandle);

        return (error == SA_AIS_OK ? res_lib_amf_componenterrorclear.header.error : error);
}

SaAisErrorT
saAmfResponse (
	SaAmfHandleT amfHandle,
	SaInvocationT invocation,
	SaAisErrorT error)
{
	struct amfInstance *amfInstance;
	struct req_lib_amf_response req_lib_amf_response;
	struct res_lib_amf_response res_lib_amf_response;
	struct iovec iov;
	SaAisErrorT errorResult;

	errorResult = hdb_error_to_sa(hdb_handle_get (&amfHandleDatabase, amfHandle,
		(void *)&amfInstance));
	if (errorResult != SA_AIS_OK) {
		return (errorResult);
	}

	req_lib_amf_response.header.id = MESSAGE_REQ_AMF_RESPONSE;
	req_lib_amf_response.header.size = sizeof (struct req_lib_amf_response);
	req_lib_amf_response.invocation = invocation;
	req_lib_amf_response.error = error;

	iov.iov_base = (void *)&req_lib_amf_response;
	iov.iov_len = sizeof (struct req_lib_amf_response);

	errorResult = coroipcc_msg_send_reply_receive (
		amfInstance->handle,
		&iov,
		1,
		&res_lib_amf_response,
		sizeof (struct res_lib_amf_response));

	hdb_handle_put (&amfHandleDatabase, amfHandle);

        return (errorResult == SA_AIS_OK ? res_lib_amf_response.header.error : errorResult);
}
