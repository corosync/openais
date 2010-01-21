/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
 * Copyright (c) 2006 Red Hat, Inc.
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
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>

#include <corosync/corotypes.h>
#include <corosync/coroipc_types.h>
#include <corosync/coroipcc.h>
#include <corosync/corodefs.h>
#include <corosync/hdb.h>

#include "../include/saAis.h"
#include <corosync/mar_gen.h>
#include <corosync/list.h>
#include "../include/saCkpt.h"
#include "../include/ipc_ckpt.h"
#include "../include/mar_ckpt.h"
#include "../include/mar_sa.h"

#include "util.h"

/*
 * Data structure for instance data
 */
struct ckptInstance {
	hdb_handle_t handle;
	SaCkptCallbacksT callbacks;
	int finalize;
	SaCkptHandleT ckptHandle;
	struct list_head checkpoint_list;
};

struct ckptCheckpointInstance {
	hdb_handle_t handle;
	SaCkptHandleT ckptHandle;
	SaCkptCheckpointHandleT checkpointHandle;
	SaCkptCheckpointOpenFlagsT checkpointOpenFlags;
	SaNameT checkpointName;
	unsigned int checkpointId;
	struct list_head list;
	struct list_head section_iteration_list_head;
};

struct ckptSectionIterationInstance {
	hdb_handle_t handle;
	SaCkptSectionIterationHandleT sectionIterationHandle;
	SaNameT checkpointName;
        SaSizeT maxSectionIdSize;
	struct list_head sectionIdListHead;
	hdb_handle_t executive_iteration_handle;
	struct list_head list;
};

/*
 * All CKPT instances in this database
 */
DECLARE_HDB_DATABASE(ckptHandleDatabase,NULL);

DECLARE_HDB_DATABASE(checkpointHandleDatabase,NULL);

DECLARE_HDB_DATABASE(ckptSectionIterationHandleDatabase,NULL);

/*
 * Versions supported
 */
static SaVersionT ckptVersionsSupported[] = {
	{ 'B', 1, 1 }
};

static struct saVersionDatabase ckptVersionDatabase = {
	sizeof (ckptVersionsSupported) / sizeof (SaVersionT),
	ckptVersionsSupported
};

struct iteratorSectionIdListEntry {
	struct list_head list;
	unsigned char data[0];
};


/*
 * Implementation
 */

static void ckptSectionIterationInstanceFinalize (struct ckptSectionIterationInstance *ckptSectionIterationInstance)
{
	struct iteratorSectionIdListEntry *iteratorSectionIdListEntry;
	struct list_head *sectionIdIterationList;
	struct list_head *sectionIdIterationListNext;
	/*
	 * iterate list of section ids for this iterator to free the allocated memory
	 * be careful to cache next pointer because free removes memory from use
	 */
	for (sectionIdIterationList = ckptSectionIterationInstance->sectionIdListHead.next,
		sectionIdIterationListNext = sectionIdIterationList->next;
		sectionIdIterationList != &ckptSectionIterationInstance->sectionIdListHead;
		sectionIdIterationList = sectionIdIterationListNext,
		sectionIdIterationListNext = sectionIdIterationList->next) {

		iteratorSectionIdListEntry = list_entry (sectionIdIterationList,
			struct iteratorSectionIdListEntry, list);

		free (iteratorSectionIdListEntry);
	}

	list_del (&ckptSectionIterationInstance->list);

	hdb_handle_destroy (&ckptSectionIterationHandleDatabase,
		ckptSectionIterationInstance->sectionIterationHandle);
}

static void ckptCheckpointInstanceFinalize (struct ckptCheckpointInstance *ckptCheckpointInstance)
{
	struct ckptSectionIterationInstance *sectionIterationInstance;
	struct list_head *sectionIterationList;
	struct list_head *sectionIterationListNext;

	for (sectionIterationList = ckptCheckpointInstance->section_iteration_list_head.next,
		sectionIterationListNext = sectionIterationList->next;
		sectionIterationList != &ckptCheckpointInstance->section_iteration_list_head;
		sectionIterationList = sectionIterationListNext,
		sectionIterationListNext = sectionIterationList->next) {

		sectionIterationInstance = list_entry (sectionIterationList,
			struct ckptSectionIterationInstance, list);

		ckptSectionIterationInstanceFinalize (sectionIterationInstance);
	}

	list_del (&ckptCheckpointInstance->list);

	hdb_handle_destroy (&checkpointHandleDatabase, ckptCheckpointInstance->checkpointHandle);
}

static void ckptInstanceFinalize (struct ckptInstance *ckptInstance)
{
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct list_head *checkpointInstanceList;
	struct list_head *checkpointInstanceListNext;

	for (checkpointInstanceList = ckptInstance->checkpoint_list.next,
		checkpointInstanceListNext = checkpointInstanceList->next;
		checkpointInstanceList != &ckptInstance->checkpoint_list;
		checkpointInstanceList = checkpointInstanceListNext,
		checkpointInstanceListNext = checkpointInstanceList->next) {

		ckptCheckpointInstance = list_entry (checkpointInstanceList,
			struct ckptCheckpointInstance, list);

		ckptCheckpointInstanceFinalize (ckptCheckpointInstance);
	}

	hdb_handle_destroy (&ckptHandleDatabase, ckptInstance->ckptHandle);
}

/**
 * @defgroup saCkpt SAF AIS Checkpoint API
 * @ingroup saf
 *
 * @{
 */

SaAisErrorT
saCkptInitialize (
	SaCkptHandleT *ckptHandle,
	const SaCkptCallbacksT *callbacks,
	SaVersionT *version)
{
	struct ckptInstance *ckptInstance;
	SaAisErrorT error = SA_AIS_OK;

	if (ckptHandle == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = saVersionVerify (&ckptVersionDatabase, version);
	if (error != SA_AIS_OK) {
		goto error_no_destroy;
	}

	error = hdb_error_to_sa(hdb_handle_create (&ckptHandleDatabase, sizeof (struct ckptInstance),
		ckptHandle));
	if (error != SA_AIS_OK) {
		goto error_no_destroy;
	}

	error = hdb_error_to_sa(hdb_handle_get (&ckptHandleDatabase, *ckptHandle,
		(void *)&ckptInstance));
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	error = coroipcc_service_connect (
		COROSYNC_SOCKET_NAME,
		CKPT_SERVICE,
		IPC_REQUEST_SIZE,
		IPC_RESPONSE_SIZE,
		IPC_DISPATCH_SIZE,
		&ckptInstance->handle);
	if (error != SA_AIS_OK) {
		goto error_put_destroy;
	}

	if (callbacks) {
		memcpy (&ckptInstance->callbacks, callbacks, sizeof (SaCkptCallbacksT));
	} else {
		memset (&ckptInstance->callbacks, 0, sizeof (SaCkptCallbacksT));
	}

	list_init (&ckptInstance->checkpoint_list);

	ckptInstance->ckptHandle = *ckptHandle;

	hdb_handle_put (&ckptHandleDatabase, *ckptHandle);

	return (SA_AIS_OK);

error_put_destroy:
	hdb_handle_put (&ckptHandleDatabase, *ckptHandle);
error_destroy:
	hdb_handle_destroy (&ckptHandleDatabase, *ckptHandle);
error_no_destroy:
	return (error);
}

SaAisErrorT
saCkptSelectionObjectGet (
	const SaCkptHandleT ckptHandle,
	SaSelectionObjectT *selectionObject)
{
	struct ckptInstance *ckptInstance;
	SaAisErrorT error;
	int fd;

	if (selectionObject == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}
	error = hdb_error_to_sa(hdb_handle_get (&ckptHandleDatabase, ckptHandle, (void *)&ckptInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	error = coroipcc_fd_get (ckptInstance->handle, &fd);
	*selectionObject = fd;

	hdb_handle_put (&ckptHandleDatabase, ckptHandle);

	return (error);
}

SaAisErrorT
saCkptDispatch (
	const SaCkptHandleT ckptHandle,
	SaDispatchFlagsT dispatchFlags)
{
	int timeout = -1;
	SaCkptCallbacksT callbacks;
	SaAisErrorT error;
	struct ckptInstance *ckptInstance;
	int cont = 1; /* always continue do loop except when set to 0 */
	coroipc_response_header_t *dispatch_data;
	struct res_lib_ckpt_checkpointopenasync *res_lib_ckpt_checkpointopenasync;
	struct res_lib_ckpt_checkpointsynchronizeasync *res_lib_ckpt_checkpointsynchronizeasync;
	struct ckptCheckpointInstance *ckptCheckpointInstance;

	if (dispatchFlags != SA_DISPATCH_ONE &&
		dispatchFlags != SA_DISPATCH_ALL &&
		dispatchFlags != SA_DISPATCH_BLOCKING) {

		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_sa(hdb_handle_get (&ckptHandleDatabase, ckptHandle,
		(void *)&ckptInstance));
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
			ckptInstance->handle,
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
		* Make copy of callbacks, message data, unlock instance,
		* and call callback. A risk of this dispatch method is that
		* the callback routines may operate at the same time that
		* CkptFinalize has been called in another thread.
		*/
		memcpy (&callbacks, &ckptInstance->callbacks,
			sizeof(ckptInstance->callbacks));

		/*
		 * Dispatch incoming response
		 */
		switch (dispatch_data->id) {
		case MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTOPENASYNC:
			if (callbacks.saCkptCheckpointOpenCallback == NULL) {
				break;
			}
			res_lib_ckpt_checkpointopenasync = (struct res_lib_ckpt_checkpointopenasync *) dispatch_data;

			/*
			 * This instance get/listadd/put required so that close
			 * later has the proper list of checkpoints
			 */
			if (res_lib_ckpt_checkpointopenasync->header.error == SA_AIS_OK) {
				error = hdb_error_to_sa(hdb_handle_get (&checkpointHandleDatabase,
					res_lib_ckpt_checkpointopenasync->checkpoint_handle,
					(void *)&ckptCheckpointInstance));

				assert (error == SA_AIS_OK); /* should only be valid handles here */
				ckptCheckpointInstance->checkpointId =
					res_lib_ckpt_checkpointopenasync->ckpt_id;

				/*
				 * open succeeded without error
				 */
				list_init (&ckptCheckpointInstance->list);
				list_init (&ckptCheckpointInstance->section_iteration_list_head);
				list_add (&ckptCheckpointInstance->list,
					&ckptInstance->checkpoint_list);

				callbacks.saCkptCheckpointOpenCallback(
					res_lib_ckpt_checkpointopenasync->invocation,
					res_lib_ckpt_checkpointopenasync->checkpoint_handle,
					res_lib_ckpt_checkpointopenasync->header.error);
				hdb_handle_put (&checkpointHandleDatabase,
					res_lib_ckpt_checkpointopenasync->checkpoint_handle);
			} else {
				/*
				 * open failed with error
				 */
				callbacks.saCkptCheckpointOpenCallback(
					res_lib_ckpt_checkpointopenasync->invocation,
					-1,
					res_lib_ckpt_checkpointopenasync->header.error);
			}
			break;

		case MESSAGE_RES_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZEASYNC:
			if (callbacks.saCkptCheckpointSynchronizeCallback == NULL) {
				break;
			}

			res_lib_ckpt_checkpointsynchronizeasync = (struct res_lib_ckpt_checkpointsynchronizeasync *) dispatch_data;

			callbacks.saCkptCheckpointSynchronizeCallback(
				res_lib_ckpt_checkpointsynchronizeasync->invocation,
				res_lib_ckpt_checkpointsynchronizeasync->header.error);
			break;
		default:
			break;
		}
		coroipcc_dispatch_put (ckptInstance->handle);

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
	hdb_handle_put(&ckptHandleDatabase, ckptHandle);
error_exit:
	return (error);
}

SaAisErrorT
saCkptFinalize (
	const SaCkptHandleT ckptHandle)
{
	struct ckptInstance *ckptInstance;
	SaAisErrorT error;

	error = hdb_error_to_sa(hdb_handle_get (&ckptHandleDatabase, ckptHandle,
		(void *)&ckptInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	/*
	 * Another thread has already started finalizing
	 */
	if (ckptInstance->finalize) {
		hdb_handle_put (&ckptHandleDatabase, ckptHandle);
		return (SA_AIS_ERR_BAD_HANDLE);
	}

	ckptInstance->finalize = 1;

	coroipcc_service_disconnect (ckptInstance->handle);

	ckptInstanceFinalize (ckptInstance);

	hdb_handle_put (&ckptHandleDatabase, ckptHandle);

	return (SA_AIS_OK);
}

SaAisErrorT
saCkptCheckpointOpen (
	SaCkptHandleT ckptHandle,
	const SaNameT *checkpointName,
	const SaCkptCheckpointCreationAttributesT *checkpointCreationAttributes,
	SaCkptCheckpointOpenFlagsT checkpointOpenFlags,
	SaTimeT timeout,
	SaCkptCheckpointHandleT *checkpointHandle)
{
	SaAisErrorT error;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct ckptInstance *ckptInstance;
	struct req_lib_ckpt_checkpointopen req_lib_ckpt_checkpointopen;
	struct res_lib_ckpt_checkpointopen res_lib_ckpt_checkpointopen;
	struct iovec iov;

	if (checkpointHandle == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	if (checkpointName == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	if (checkpointOpenFlags & ~(SA_CKPT_CHECKPOINT_READ|SA_CKPT_CHECKPOINT_WRITE|SA_CKPT_CHECKPOINT_CREATE)) {
		return (SA_AIS_ERR_BAD_FLAGS);
	}

	if ((checkpointOpenFlags & SA_CKPT_CHECKPOINT_CREATE) &&
		checkpointCreationAttributes == NULL) {

		return (SA_AIS_ERR_INVALID_PARAM);
	}

	if (((checkpointOpenFlags & SA_CKPT_CHECKPOINT_CREATE) == 0) &&
		checkpointCreationAttributes != NULL) {

		return (SA_AIS_ERR_INVALID_PARAM);
	}

	if (checkpointCreationAttributes &&
		(checkpointCreationAttributes->checkpointSize >
		(checkpointCreationAttributes->maxSections * checkpointCreationAttributes->maxSectionSize))) {

		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_sa(hdb_handle_get (&ckptHandleDatabase, ckptHandle,
		(void *)&ckptInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = hdb_error_to_sa(hdb_handle_create (&checkpointHandleDatabase,
		sizeof (struct ckptCheckpointInstance), checkpointHandle));
	if (error != SA_AIS_OK) {
		goto error_put_ckpt;
	}

	error = hdb_error_to_sa(hdb_handle_get (&checkpointHandleDatabase,
		*checkpointHandle, (void *)&ckptCheckpointInstance));
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	ckptCheckpointInstance->handle = ckptInstance->handle;

	ckptCheckpointInstance->ckptHandle = ckptHandle;
	ckptCheckpointInstance->checkpointHandle = *checkpointHandle;
	ckptCheckpointInstance->checkpointOpenFlags = checkpointOpenFlags;
	list_init (&ckptCheckpointInstance->section_iteration_list_head);

	req_lib_ckpt_checkpointopen.header.size = sizeof (struct req_lib_ckpt_checkpointopen);
	req_lib_ckpt_checkpointopen.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTOPEN;
	marshall_SaNameT_to_mar_name_t (&req_lib_ckpt_checkpointopen.checkpoint_name,
		(SaNameT *)checkpointName);
	memcpy (&ckptCheckpointInstance->checkpointName, checkpointName, sizeof (SaNameT));
	req_lib_ckpt_checkpointopen.async_call = 0;
	req_lib_ckpt_checkpointopen.invocation = 0;
	req_lib_ckpt_checkpointopen.fail_with_error = SA_AIS_OK;
	req_lib_ckpt_checkpointopen.checkpoint_creation_attributes_set = 0;
	if (checkpointCreationAttributes) {
		marshall_to_mar_ckpt_checkpoint_creation_attributes_t (
			&req_lib_ckpt_checkpointopen.checkpoint_creation_attributes,
			(SaCkptCheckpointCreationAttributesT *)checkpointCreationAttributes);
		req_lib_ckpt_checkpointopen.checkpoint_creation_attributes_set = 1;
	}
	req_lib_ckpt_checkpointopen.checkpoint_open_flags = checkpointOpenFlags;

	iov.iov_base = (void *)&req_lib_ckpt_checkpointopen;
	iov.iov_len = sizeof (struct req_lib_ckpt_checkpointopen);

	error = coroipcc_msg_send_reply_receive (
		ckptInstance->handle,
		&iov,
		1,
		&res_lib_ckpt_checkpointopen,
		sizeof (struct res_lib_ckpt_checkpointopen));

	if (error != SA_AIS_OK) {
		goto error_put_destroy;
	}
	if (res_lib_ckpt_checkpointopen.header.error != SA_AIS_OK) {
		error = res_lib_ckpt_checkpointopen.header.error;
		goto error_put_destroy;
	}
	ckptCheckpointInstance->checkpointId =
		res_lib_ckpt_checkpointopen.ckpt_id;

	hdb_handle_put (&checkpointHandleDatabase, *checkpointHandle);

	hdb_handle_put (&ckptHandleDatabase, ckptHandle);

	list_init (&ckptCheckpointInstance->list);

	list_add (&ckptCheckpointInstance->list, &ckptInstance->checkpoint_list);
	return (error);

error_put_destroy:
	hdb_handle_put (&checkpointHandleDatabase, *checkpointHandle);
error_destroy:
	hdb_handle_destroy (&checkpointHandleDatabase, *checkpointHandle);
error_put_ckpt:
	hdb_handle_put (&ckptHandleDatabase, ckptHandle);
error_exit:
	return (error);
}

SaAisErrorT
saCkptCheckpointOpenAsync (
	const SaCkptHandleT ckptHandle,
	SaInvocationT invocation,
	const SaNameT *checkpointName,
	const SaCkptCheckpointCreationAttributesT *checkpointCreationAttributes,
	SaCkptCheckpointOpenFlagsT checkpointOpenFlags)
{
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct ckptInstance *ckptInstance;
	SaCkptCheckpointHandleT checkpointHandle;
	SaAisErrorT error;
	struct req_lib_ckpt_checkpointopen req_lib_ckpt_checkpointopen;
	struct res_lib_ckpt_checkpointopenasync res_lib_ckpt_checkpointopenasync;
	struct iovec iov;
	SaAisErrorT failWithError = SA_AIS_OK;

	if (checkpointName == NULL) {
		failWithError = SA_AIS_ERR_INVALID_PARAM;
	} else
	if (checkpointOpenFlags &
		~(SA_CKPT_CHECKPOINT_READ|SA_CKPT_CHECKPOINT_WRITE|SA_CKPT_CHECKPOINT_CREATE)) {
		failWithError = SA_AIS_ERR_BAD_FLAGS;
	} else
	if ((checkpointOpenFlags & SA_CKPT_CHECKPOINT_CREATE) &&
		checkpointCreationAttributes == NULL) {

		failWithError = SA_AIS_ERR_INVALID_PARAM;
	} else
	if (((checkpointOpenFlags & SA_CKPT_CHECKPOINT_CREATE) == 0) &&
		checkpointCreationAttributes != NULL) {

		failWithError = SA_AIS_ERR_INVALID_PARAM;
	} else
	if (checkpointCreationAttributes &&
		(checkpointCreationAttributes->checkpointSize >
		(checkpointCreationAttributes->maxSections * checkpointCreationAttributes->maxSectionSize))) {

		failWithError = SA_AIS_ERR_INVALID_PARAM;
	}

	error = hdb_error_to_sa(hdb_handle_get (&ckptHandleDatabase, ckptHandle,
		(void *)&ckptInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	if (ckptInstance->callbacks.saCkptCheckpointOpenCallback == NULL) {
		error = SA_AIS_ERR_INIT;
		goto error_put_ckpt;
	}

	error = hdb_error_to_sa(hdb_handle_create (&checkpointHandleDatabase,
		sizeof (struct ckptCheckpointInstance), &checkpointHandle));
	if (error != SA_AIS_OK) {
		goto error_put_ckpt;
	}

	error = hdb_error_to_sa(hdb_handle_get (&checkpointHandleDatabase, checkpointHandle,
		(void *)&ckptCheckpointInstance));
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	ckptCheckpointInstance->handle = ckptInstance->handle;
	ckptCheckpointInstance->ckptHandle = ckptHandle;
	ckptCheckpointInstance->checkpointHandle = checkpointHandle;
	ckptCheckpointInstance->checkpointOpenFlags = checkpointOpenFlags;
	if (failWithError == SA_AIS_OK) {
		memcpy (&ckptCheckpointInstance->checkpointName, checkpointName,
			sizeof (SaNameT));
		marshall_SaNameT_to_mar_name_t (&req_lib_ckpt_checkpointopen.checkpoint_name,
			(SaNameT *)checkpointName);
	}

	req_lib_ckpt_checkpointopen.header.size = sizeof (struct req_lib_ckpt_checkpointopen);
	req_lib_ckpt_checkpointopen.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTOPEN;
	req_lib_ckpt_checkpointopen.async_call = 1;
	req_lib_ckpt_checkpointopen.invocation = invocation;
	req_lib_ckpt_checkpointopen.fail_with_error = failWithError;
	req_lib_ckpt_checkpointopen.checkpoint_creation_attributes_set = 0;
	if (checkpointCreationAttributes) {
		marshall_to_mar_ckpt_checkpoint_creation_attributes_t (
			&req_lib_ckpt_checkpointopen.checkpoint_creation_attributes,
			(SaCkptCheckpointCreationAttributesT *)checkpointCreationAttributes);
		req_lib_ckpt_checkpointopen.checkpoint_creation_attributes_set = 1;
	}

	req_lib_ckpt_checkpointopen.checkpoint_open_flags = checkpointOpenFlags;
	req_lib_ckpt_checkpointopen.checkpoint_handle = checkpointHandle;

	iov.iov_base = (void *)&req_lib_ckpt_checkpointopen;
	iov.iov_len = sizeof (struct req_lib_ckpt_checkpointopen);

	error = coroipcc_msg_send_reply_receive (
		ckptInstance->handle,
		&iov,
		1,
		&res_lib_ckpt_checkpointopenasync,
		sizeof (struct res_lib_ckpt_checkpointopenasync));

	if (error != SA_AIS_OK) {
		goto error_put_destroy;
	}

	if (res_lib_ckpt_checkpointopenasync.header.error != SA_AIS_OK) {
		error = res_lib_ckpt_checkpointopenasync.header.error;
		goto error_put_destroy;
	}

	hdb_handle_put (&checkpointHandleDatabase, checkpointHandle);

	hdb_handle_put (&ckptHandleDatabase, ckptHandle);

	return (SA_AIS_OK);

error_put_destroy:
	hdb_handle_put (&checkpointHandleDatabase, checkpointHandle);
error_destroy:
	hdb_handle_destroy (&checkpointHandleDatabase, checkpointHandle);
error_put_ckpt:
	hdb_handle_put (&ckptHandleDatabase, ckptHandle);
error_exit:
	return (error);
}

SaAisErrorT
saCkptCheckpointClose (
	SaCkptCheckpointHandleT checkpointHandle)
{
	struct req_lib_ckpt_checkpointclose req_lib_ckpt_checkpointclose;
	struct res_lib_ckpt_checkpointclose res_lib_ckpt_checkpointclose;
	SaAisErrorT error;
	struct iovec iov;
	struct ckptCheckpointInstance *ckptCheckpointInstance;

	error = hdb_error_to_sa(hdb_handle_get (&checkpointHandleDatabase, checkpointHandle,
		(void *)&ckptCheckpointInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_ckpt_checkpointclose.header.size = sizeof (struct req_lib_ckpt_checkpointclose);
	req_lib_ckpt_checkpointclose.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTCLOSE;
	marshall_SaNameT_to_mar_name_t (&req_lib_ckpt_checkpointclose.checkpoint_name,
		&ckptCheckpointInstance->checkpointName);
	req_lib_ckpt_checkpointclose.ckpt_id =
		ckptCheckpointInstance->checkpointId;

	iov.iov_base = (void *)&req_lib_ckpt_checkpointclose;
	iov.iov_len = sizeof (struct req_lib_ckpt_checkpointclose);

	error = coroipcc_msg_send_reply_receive (
		ckptCheckpointInstance->handle,
		&iov,
		1,
		&res_lib_ckpt_checkpointclose,
		sizeof (struct res_lib_ckpt_checkpointclose));

	if (error == SA_AIS_OK) {
		error = res_lib_ckpt_checkpointclose.header.error;
	}

	if (error == SA_AIS_OK) {
		ckptCheckpointInstanceFinalize (ckptCheckpointInstance);
	}

	hdb_handle_put (&checkpointHandleDatabase, checkpointHandle);

	return (error);
}

SaAisErrorT
saCkptCheckpointUnlink (
	SaCkptHandleT ckptHandle,
	const SaNameT *checkpointName)
{
	SaAisErrorT error;
	struct iovec iov;
	struct ckptInstance *ckptInstance;
	struct req_lib_ckpt_checkpointunlink req_lib_ckpt_checkpointunlink;
	struct res_lib_ckpt_checkpointunlink res_lib_ckpt_checkpointunlink;

	if (checkpointName == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}
	error = hdb_error_to_sa(hdb_handle_get (&ckptHandleDatabase, ckptHandle, (void *)&ckptInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_ckpt_checkpointunlink.header.size = sizeof (struct req_lib_ckpt_checkpointunlink);
	req_lib_ckpt_checkpointunlink.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTUNLINK;
	marshall_SaNameT_to_mar_name_t (&req_lib_ckpt_checkpointunlink.checkpoint_name,
		(SaNameT *)checkpointName);

	iov.iov_base = (void *)&req_lib_ckpt_checkpointunlink;
	iov.iov_len = sizeof (struct req_lib_ckpt_checkpointunlink);

	error = coroipcc_msg_send_reply_receive (
		ckptInstance->handle,
		&iov,
		1,
		&res_lib_ckpt_checkpointunlink,
		sizeof (struct res_lib_ckpt_checkpointunlink));

	hdb_handle_put (&ckptHandleDatabase, ckptHandle);

	return (error == SA_AIS_OK ? res_lib_ckpt_checkpointunlink.header.error : error);

}

SaAisErrorT
saCkptCheckpointRetentionDurationSet (
	SaCkptCheckpointHandleT checkpointHandle,
	SaTimeT retentionDuration)
{
	SaAisErrorT error;
	struct iovec iov;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_checkpointretentiondurationset req_lib_ckpt_checkpointretentiondurationset;
	struct res_lib_ckpt_checkpointretentiondurationset res_lib_ckpt_checkpointretentiondurationset;

	error = hdb_error_to_sa(hdb_handle_get (&checkpointHandleDatabase, checkpointHandle,
		(void *)&ckptCheckpointInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_ckpt_checkpointretentiondurationset.header.size = sizeof (struct req_lib_ckpt_checkpointretentiondurationset);
	req_lib_ckpt_checkpointretentiondurationset.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTRETENTIONDURATIONSET;

	req_lib_ckpt_checkpointretentiondurationset.retention_duration = retentionDuration;
	marshall_SaNameT_to_mar_name_t (&req_lib_ckpt_checkpointretentiondurationset.checkpoint_name,
		&ckptCheckpointInstance->checkpointName);
	req_lib_ckpt_checkpointretentiondurationset.ckpt_id =
		ckptCheckpointInstance->checkpointId;

	iov.iov_base = (void *)&req_lib_ckpt_checkpointretentiondurationset;
	iov.iov_len = sizeof (struct req_lib_ckpt_checkpointretentiondurationset);

	error = coroipcc_msg_send_reply_receive (
		ckptCheckpointInstance->handle,
		&iov,
		1,
		&res_lib_ckpt_checkpointretentiondurationset,
		sizeof (struct res_lib_ckpt_checkpointretentiondurationset));

	hdb_handle_put (&checkpointHandleDatabase, checkpointHandle);
	return (error == SA_AIS_OK ? res_lib_ckpt_checkpointretentiondurationset.header.error : error);
}

SaAisErrorT
saCkptActiveReplicaSet (
	SaCkptCheckpointHandleT checkpointHandle)
{
	SaAisErrorT error;
	struct iovec iov;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_activereplicaset req_lib_ckpt_activereplicaset;
	struct res_lib_ckpt_activereplicaset res_lib_ckpt_activereplicaset;

	error = hdb_error_to_sa(hdb_handle_get (&checkpointHandleDatabase, checkpointHandle,
		 (void *)&ckptCheckpointInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	if ((ckptCheckpointInstance->checkpointOpenFlags & SA_CKPT_CHECKPOINT_WRITE) == 0) {
		error = SA_AIS_ERR_ACCESS;
		goto error_put;
	}

	req_lib_ckpt_activereplicaset.header.size = sizeof (struct req_lib_ckpt_activereplicaset);
	req_lib_ckpt_activereplicaset.header.id = MESSAGE_REQ_CKPT_ACTIVEREPLICASET;
	marshall_SaNameT_to_mar_name_t (&req_lib_ckpt_activereplicaset.checkpoint_name,
		&ckptCheckpointInstance->checkpointName);
	req_lib_ckpt_activereplicaset.ckpt_id =
		ckptCheckpointInstance->checkpointId;

	iov.iov_base = (void *)&req_lib_ckpt_activereplicaset;
	iov.iov_len = sizeof (struct req_lib_ckpt_activereplicaset);

	error = coroipcc_msg_send_reply_receive (
		ckptCheckpointInstance->handle,
		&iov,
		1,
		&res_lib_ckpt_activereplicaset,
		sizeof (struct res_lib_ckpt_activereplicaset));

error_put:
	hdb_handle_put (&checkpointHandleDatabase, checkpointHandle);

	return (error == SA_AIS_OK ? res_lib_ckpt_activereplicaset.header.error : error);
}

SaAisErrorT
saCkptCheckpointStatusGet (
	SaCkptCheckpointHandleT checkpointHandle,
	SaCkptCheckpointDescriptorT *checkpointStatus)
{
	SaAisErrorT error;
	struct iovec iov;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_checkpointstatusget req_lib_ckpt_checkpointstatusget;
	struct res_lib_ckpt_checkpointstatusget res_lib_ckpt_checkpointstatusget;

	if (checkpointStatus == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}
	error = hdb_error_to_sa(hdb_handle_get (&checkpointHandleDatabase, checkpointHandle,
		(void *)&ckptCheckpointInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_ckpt_checkpointstatusget.header.size = sizeof (struct req_lib_ckpt_checkpointstatusget);
	req_lib_ckpt_checkpointstatusget.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTSTATUSGET;

	marshall_SaNameT_to_mar_name_t (&req_lib_ckpt_checkpointstatusget.checkpoint_name,
		&ckptCheckpointInstance->checkpointName);
	req_lib_ckpt_checkpointstatusget.ckpt_id =
		ckptCheckpointInstance->checkpointId;

	iov.iov_base = (void *)&req_lib_ckpt_checkpointstatusget;
	iov.iov_len = sizeof (struct req_lib_ckpt_checkpointstatusget);

	error = coroipcc_msg_send_reply_receive (ckptCheckpointInstance->handle,
		&iov,
		1,
		&res_lib_ckpt_checkpointstatusget,
		sizeof (struct res_lib_ckpt_checkpointstatusget));

	marshall_from_mar_ckpt_checkpoint_descriptor_t (
		checkpointStatus,
		&res_lib_ckpt_checkpointstatusget.checkpoint_descriptor);

	hdb_handle_put (&checkpointHandleDatabase, checkpointHandle);

	return (error == SA_AIS_OK ? res_lib_ckpt_checkpointstatusget.header.error : error);
}

SaAisErrorT
saCkptSectionCreate (
	SaCkptCheckpointHandleT checkpointHandle,
	SaCkptSectionCreationAttributesT *sectionCreationAttributes,
	const void *initialData,
	SaUint32T initialDataSize)
{
	SaAisErrorT error;
	struct iovec iov[3];
	int iov_len;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_sectioncreate req_lib_ckpt_sectioncreate;
	struct res_lib_ckpt_sectioncreate res_lib_ckpt_sectioncreate;

	if (sectionCreationAttributes == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	if (initialData == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_sa(hdb_handle_get (&checkpointHandleDatabase, checkpointHandle,
		(void *)&ckptCheckpointInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	if ((ckptCheckpointInstance->checkpointOpenFlags & SA_CKPT_CHECKPOINT_WRITE) == 0) {
		error = SA_AIS_ERR_ACCESS;
		goto error_exit;
	}

	req_lib_ckpt_sectioncreate.header.size =
		sizeof (struct req_lib_ckpt_sectioncreate) +
		sectionCreationAttributes->sectionId->idLen +
		initialDataSize;

	req_lib_ckpt_sectioncreate.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONCREATE;
	req_lib_ckpt_sectioncreate.id_len = sectionCreationAttributes->sectionId->idLen;
	req_lib_ckpt_sectioncreate.expiration_time = sectionCreationAttributes->expirationTime;
	req_lib_ckpt_sectioncreate.initial_data_size = initialDataSize;

	marshall_SaNameT_to_mar_name_t (&req_lib_ckpt_sectioncreate.checkpoint_name,
		&ckptCheckpointInstance->checkpointName);
	req_lib_ckpt_sectioncreate.ckpt_id =
		ckptCheckpointInstance->checkpointId;


	iov[0].iov_base = (void *)&req_lib_ckpt_sectioncreate;
	iov[0].iov_len = sizeof (struct req_lib_ckpt_sectioncreate);
	iov_len = 1;
	if (sectionCreationAttributes->sectionId->id) {
		iov[1].iov_base = (void *)sectionCreationAttributes->sectionId->id;
		iov[1].iov_len = sectionCreationAttributes->sectionId->idLen;
		iov_len = 2;
	}
	if (initialDataSize) {
		iov[iov_len].iov_base = (void *)initialData;
		iov[iov_len].iov_len = initialDataSize;
		iov_len++;
	}

	error = coroipcc_msg_send_reply_receive (
		ckptCheckpointInstance->handle,
		iov,
		iov_len,
		&res_lib_ckpt_sectioncreate,
		sizeof (struct res_lib_ckpt_sectioncreate));

error_exit:
	hdb_handle_put (&checkpointHandleDatabase, checkpointHandle);

	return (error == SA_AIS_OK ? res_lib_ckpt_sectioncreate.header.error : error);
}


SaAisErrorT
saCkptSectionDelete (
	SaCkptCheckpointHandleT checkpointHandle,
	const SaCkptSectionIdT *sectionId)
{
	SaAisErrorT error;
	struct iovec iov[2];
	int iov_len;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_sectiondelete req_lib_ckpt_sectiondelete;
	struct res_lib_ckpt_sectiondelete res_lib_ckpt_sectiondelete;

	if (sectionId == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_sa(hdb_handle_get (&checkpointHandleDatabase, checkpointHandle,
		(void *)&ckptCheckpointInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	if ((ckptCheckpointInstance->checkpointOpenFlags & SA_CKPT_CHECKPOINT_WRITE) == 0) {
		error = SA_AIS_ERR_ACCESS;
		goto error_put;
	}

	req_lib_ckpt_sectiondelete.header.size = sizeof (struct req_lib_ckpt_sectiondelete) + sectionId->idLen;
	req_lib_ckpt_sectiondelete.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONDELETE;
	req_lib_ckpt_sectiondelete.id_len = sectionId->idLen;

	marshall_SaNameT_to_mar_name_t (
		&req_lib_ckpt_sectiondelete.checkpoint_name,
		&ckptCheckpointInstance->checkpointName);
	req_lib_ckpt_sectiondelete.ckpt_id =
		ckptCheckpointInstance->checkpointId;

	iov[0].iov_base = (void *)&req_lib_ckpt_sectiondelete;
	iov[0].iov_len = sizeof (struct req_lib_ckpt_sectiondelete);
	iov_len = 1;
	if (sectionId->idLen) {
		iov[1].iov_base = (void *)sectionId->id;
		iov[1].iov_len = sectionId->idLen;
		iov_len = 2;
	}

	error = coroipcc_msg_send_reply_receive (ckptCheckpointInstance->handle,
		iov,
		iov_len,
		&res_lib_ckpt_sectiondelete,
		sizeof (struct res_lib_ckpt_sectiondelete));

error_put:
	hdb_handle_put (&checkpointHandleDatabase, checkpointHandle);
	return (error == SA_AIS_OK ? res_lib_ckpt_sectiondelete.header.error : error);
}

SaAisErrorT
saCkptSectionExpirationTimeSet (
	SaCkptCheckpointHandleT checkpointHandle,
	const SaCkptSectionIdT *sectionId,
	SaTimeT expirationTime)
{
	SaAisErrorT error;
	struct iovec iov[2];
	unsigned int iov_len;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_sectionexpirationtimeset req_lib_ckpt_sectionexpirationtimeset;
	struct res_lib_ckpt_sectionexpirationtimeset res_lib_ckpt_sectionexpirationtimeset;

	if (sectionId == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_sa(hdb_handle_get (&checkpointHandleDatabase, checkpointHandle,
		(void *)&ckptCheckpointInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	if ((ckptCheckpointInstance->checkpointOpenFlags & SA_CKPT_CHECKPOINT_WRITE) == 0) {
		error = SA_AIS_ERR_ACCESS;
		goto error_put;
	}


	req_lib_ckpt_sectionexpirationtimeset.header.size = sizeof (struct req_lib_ckpt_sectionexpirationtimeset) + sectionId->idLen;
	req_lib_ckpt_sectionexpirationtimeset.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONEXPIRATIONTIMESET;
	req_lib_ckpt_sectionexpirationtimeset.id_len = sectionId->idLen;
	req_lib_ckpt_sectionexpirationtimeset.expiration_time = expirationTime;

	marshall_SaNameT_to_mar_name_t (&req_lib_ckpt_sectionexpirationtimeset.checkpoint_name,
		&ckptCheckpointInstance->checkpointName);
	req_lib_ckpt_sectionexpirationtimeset.ckpt_id =
		ckptCheckpointInstance->checkpointId;

	iov[0].iov_base = (void *)&req_lib_ckpt_sectionexpirationtimeset;
	iov[0].iov_len = sizeof (struct req_lib_ckpt_sectionexpirationtimeset);
	iov_len = 1;
	if (sectionId->idLen) {
		iov[1].iov_base = (void *)sectionId->id;
		iov[1].iov_len = sectionId->idLen;
		iov_len = 2;
	}

	error = coroipcc_msg_send_reply_receive (
		ckptCheckpointInstance->handle,
		iov,
		iov_len,
		&res_lib_ckpt_sectionexpirationtimeset,
		sizeof (struct res_lib_ckpt_sectionexpirationtimeset));

error_put:
	hdb_handle_put (&checkpointHandleDatabase, checkpointHandle);

	return (error == SA_AIS_OK ? res_lib_ckpt_sectionexpirationtimeset.header.error : error);
}

SaAisErrorT
saCkptSectionIterationInitialize (
	SaCkptCheckpointHandleT checkpointHandle,
	SaCkptSectionsChosenT sectionsChosen,
	SaTimeT expirationTime,
	SaCkptSectionIterationHandleT *sectionIterationHandle)
{
	SaAisErrorT error;
	struct iovec iov;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct ckptSectionIterationInstance *ckptSectionIterationInstance;
	struct req_lib_ckpt_sectioniterationinitialize req_lib_ckpt_sectioniterationinitialize;
	struct res_lib_ckpt_sectioniterationinitialize res_lib_ckpt_sectioniterationinitialize;

	if (sectionIterationHandle == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	if (sectionsChosen != SA_CKPT_SECTIONS_FOREVER &&
		sectionsChosen != SA_CKPT_SECTIONS_LEQ_EXPIRATION_TIME &&
		sectionsChosen != SA_CKPT_SECTIONS_GEQ_EXPIRATION_TIME &&
		sectionsChosen != SA_CKPT_SECTIONS_CORRUPTED &&
		sectionsChosen != SA_CKPT_SECTIONS_ANY) {

		return (SA_AIS_ERR_INVALID_PARAM);
	}
	error = hdb_error_to_sa(hdb_handle_get (&checkpointHandleDatabase, checkpointHandle,
		(void *)&ckptCheckpointInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	error = hdb_error_to_sa(hdb_handle_create (&ckptSectionIterationHandleDatabase,
		sizeof (struct ckptSectionIterationInstance), sectionIterationHandle));
	if (error != SA_AIS_OK) {
		goto error_put_checkpoint_db;
	}

	error = hdb_error_to_sa(hdb_handle_get (&ckptSectionIterationHandleDatabase,
		*sectionIterationHandle, (void *)&ckptSectionIterationInstance));
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	ckptSectionIterationInstance->handle = ckptCheckpointInstance->handle;
	ckptSectionIterationInstance->sectionIterationHandle = *sectionIterationHandle;

	memcpy (&ckptSectionIterationInstance->checkpointName,
		&ckptCheckpointInstance->checkpointName, sizeof (SaNameT));

	list_init (&ckptSectionIterationInstance->list);

	list_add (&ckptSectionIterationInstance->list,
		&ckptCheckpointInstance->section_iteration_list_head);

	/*
	 * Setup section id list for iterator next
	 */
	list_init (&ckptSectionIterationInstance->sectionIdListHead);

	req_lib_ckpt_sectioniterationinitialize.header.size = sizeof (struct req_lib_ckpt_sectioniterationinitialize);
	req_lib_ckpt_sectioniterationinitialize.header.id = MESSAGE_REQ_CKPT_SECTIONITERATIONINITIALIZE;
	req_lib_ckpt_sectioniterationinitialize.sections_chosen = sectionsChosen;
	req_lib_ckpt_sectioniterationinitialize.expiration_time = expirationTime;
	marshall_SaNameT_to_mar_name_t (
		&req_lib_ckpt_sectioniterationinitialize.checkpoint_name,
		&ckptCheckpointInstance->checkpointName);
	req_lib_ckpt_sectioniterationinitialize.ckpt_id =
		ckptCheckpointInstance->checkpointId;

	iov.iov_base = (void *)&req_lib_ckpt_sectioniterationinitialize;
	iov.iov_len = sizeof (struct req_lib_ckpt_sectioniterationinitialize);

	error = coroipcc_msg_send_reply_receive (ckptSectionIterationInstance->handle,
		&iov,
		1,
		&res_lib_ckpt_sectioniterationinitialize,
		sizeof (struct res_lib_ckpt_sectioniterationinitialize));

	if (error != SA_AIS_OK) {
		goto error_put_destroy;
	}

	ckptSectionIterationInstance->executive_iteration_handle =
		res_lib_ckpt_sectioniterationinitialize.iteration_handle;
	ckptSectionIterationInstance->maxSectionIdSize =
		res_lib_ckpt_sectioniterationinitialize.max_section_id_size;

	hdb_handle_put (&ckptSectionIterationHandleDatabase, *sectionIterationHandle);
	hdb_handle_put (&checkpointHandleDatabase, checkpointHandle);

	return (res_lib_ckpt_sectioniterationinitialize.header.error);

error_put_destroy:
	hdb_handle_put (&ckptSectionIterationHandleDatabase, *sectionIterationHandle);
error_destroy:
	hdb_handle_destroy (&ckptSectionIterationHandleDatabase, *sectionIterationHandle);
error_put_checkpoint_db:
	hdb_handle_put (&checkpointHandleDatabase, checkpointHandle);
	return (error);
}

SaAisErrorT
saCkptSectionIterationNext (
	SaCkptSectionIterationHandleT sectionIterationHandle,
	SaCkptSectionDescriptorT *sectionDescriptor)
{
	SaAisErrorT error;
	struct iovec iov;
	struct ckptSectionIterationInstance *ckptSectionIterationInstance;
	struct req_lib_ckpt_sectioniterationnext req_lib_ckpt_sectioniterationnext;
	struct res_lib_ckpt_sectioniterationnext *res_lib_ckpt_sectioniterationnext;
	struct iteratorSectionIdListEntry *iteratorSectionIdListEntry;
	void *return_address;

	if (sectionDescriptor == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_sa(hdb_handle_get (&ckptSectionIterationHandleDatabase,
		sectionIterationHandle, (void *)&ckptSectionIterationInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}
	/*
	 * Allocate section id storage area
	 */
	iteratorSectionIdListEntry = malloc (sizeof (struct list_head) +
		ckptSectionIterationInstance->maxSectionIdSize);
	if (iteratorSectionIdListEntry == 0) {
		error = SA_AIS_ERR_NO_MEMORY;
		goto error_put_nounlock;
	}

	req_lib_ckpt_sectioniterationnext.header.size = sizeof (struct req_lib_ckpt_sectioniterationnext);
	req_lib_ckpt_sectioniterationnext.header.id = MESSAGE_REQ_CKPT_SECTIONITERATIONNEXT;
	req_lib_ckpt_sectioniterationnext.iteration_handle = ckptSectionIterationInstance->executive_iteration_handle;

	iov.iov_base = (void *)&req_lib_ckpt_sectioniterationnext;
	iov.iov_len = sizeof (struct req_lib_ckpt_sectioniterationnext);

	error = coroipcc_msg_send_reply_receive_in_buf_get (
		ckptSectionIterationInstance->handle,
		&iov,
		1,
		&return_address);
	res_lib_ckpt_sectioniterationnext = return_address;

	if (error != SA_AIS_OK) {
		goto error_put_unlock;
	}

	marshall_from_mar_ckpt_section_descriptor_t (
		sectionDescriptor,
		&res_lib_ckpt_sectioniterationnext->section_descriptor);

	sectionDescriptor->sectionId.id = &iteratorSectionIdListEntry->data[0];

	memcpy (sectionDescriptor->sectionId.id,
		((char *)res_lib_ckpt_sectioniterationnext) +
		sizeof (struct res_lib_ckpt_sectioniterationnext),
		res_lib_ckpt_sectioniterationnext->header.size -
		sizeof (struct res_lib_ckpt_sectioniterationnext));


	error = (error == SA_AIS_OK ? res_lib_ckpt_sectioniterationnext->header.error : error);

	coroipcc_msg_send_reply_receive_in_buf_put (
		ckptSectionIterationInstance->handle);

	/*
	 * Add to persistent memory list for this sectioniterator
	 */
	if (error == SA_AIS_OK) {
		list_init (&iteratorSectionIdListEntry->list);
		list_add (&iteratorSectionIdListEntry->list, &ckptSectionIterationInstance->sectionIdListHead);
	}

error_put_unlock:
	if (error != SA_AIS_OK) {
		free (iteratorSectionIdListEntry);
	}

error_put_nounlock:
	hdb_handle_put (&ckptSectionIterationHandleDatabase, sectionIterationHandle);

error_exit:
	return (error);
}

SaAisErrorT
saCkptSectionIterationFinalize (
	SaCkptSectionIterationHandleT sectionIterationHandle)
{
	SaAisErrorT error;
	struct iovec iov;
	struct ckptSectionIterationInstance *ckptSectionIterationInstance;
	struct req_lib_ckpt_sectioniterationfinalize req_lib_ckpt_sectioniterationfinalize;
	struct res_lib_ckpt_sectioniterationfinalize res_lib_ckpt_sectioniterationfinalize;

	error = hdb_error_to_sa(hdb_handle_get (&ckptSectionIterationHandleDatabase,
		sectionIterationHandle, (void *)&ckptSectionIterationInstance));
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	req_lib_ckpt_sectioniterationfinalize.header.size = sizeof (struct req_lib_ckpt_sectioniterationfinalize);
	req_lib_ckpt_sectioniterationfinalize.header.id = MESSAGE_REQ_CKPT_SECTIONITERATIONFINALIZE;
	req_lib_ckpt_sectioniterationfinalize.iteration_handle = ckptSectionIterationInstance->executive_iteration_handle;

	iov.iov_base = (void *)&req_lib_ckpt_sectioniterationfinalize;
	iov.iov_len = sizeof (struct req_lib_ckpt_sectioniterationfinalize);

	error = coroipcc_msg_send_reply_receive (ckptSectionIterationInstance->handle,
		&iov,
		1,
		&res_lib_ckpt_sectioniterationfinalize,
		sizeof (struct res_lib_ckpt_sectioniterationfinalize));

	if (error != SA_AIS_OK) {
		goto error_put;
	}

	ckptSectionIterationInstanceFinalize (ckptSectionIterationInstance);

	hdb_handle_put (&ckptSectionIterationHandleDatabase, sectionIterationHandle);

	return (res_lib_ckpt_sectioniterationfinalize.header.error);

error_put:
	hdb_handle_put (&ckptSectionIterationHandleDatabase, sectionIterationHandle);
error_exit:
	return (error);
}

SaAisErrorT
saCkptCheckpointWrite (
	SaCkptCheckpointHandleT checkpointHandle,
	const SaCkptIOVectorElementT *ioVector,
	SaUint32T numberOfElements,
	SaUint32T *erroneousVectorIndex)
{
	SaAisErrorT error = SA_AIS_OK;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_sectionwrite req_lib_ckpt_sectionwrite;
	struct res_lib_ckpt_sectionwrite res_lib_ckpt_sectionwrite;
	int i;
	struct iovec iov[3];
	int iov_len = 0;
	int iov_idx;

	if (ioVector == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_sa(hdb_handle_get (&checkpointHandleDatabase, checkpointHandle,
		(void *)&ckptCheckpointInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	if ((ckptCheckpointInstance->checkpointOpenFlags & SA_CKPT_CHECKPOINT_WRITE) == 0) {
		error = SA_AIS_ERR_ACCESS;
		goto error_put;
	}
	req_lib_ckpt_sectionwrite.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONWRITE;

	/*
	 * Make sure ioVector is valid
	 */
	for (i = 0; i < numberOfElements; i++) {
		if (ioVector[i].dataSize == 0) {
			*erroneousVectorIndex = i;
			error = SA_AIS_ERR_INVALID_PARAM;
			goto error_put;
		}
		if (ioVector[i].dataBuffer == NULL) {
			*erroneousVectorIndex = i;
			error = SA_AIS_ERR_INVALID_PARAM;
			goto error_put;
		}
	}

	for (i = 0; i < numberOfElements; i++) {

		req_lib_ckpt_sectionwrite.header.size = sizeof (struct req_lib_ckpt_sectionwrite) + ioVector[i].sectionId.idLen + ioVector[i].dataSize;

		req_lib_ckpt_sectionwrite.data_offset = ioVector[i].dataOffset;
		req_lib_ckpt_sectionwrite.data_size = ioVector[i].dataSize;
		req_lib_ckpt_sectionwrite.id_len = ioVector[i].sectionId.idLen;

		marshall_SaNameT_to_mar_name_t (&req_lib_ckpt_sectionwrite.checkpoint_name,
			&ckptCheckpointInstance->checkpointName);
		req_lib_ckpt_sectionwrite.ckpt_id =
			ckptCheckpointInstance->checkpointId;

		iov_len = 0;
		iov[0].iov_base = (char *)&req_lib_ckpt_sectionwrite;
		iov[0].iov_len = sizeof (struct req_lib_ckpt_sectionwrite);
		iov_idx = 1;

		if (ioVector[i].sectionId.idLen) {
			iov[iov_idx].iov_base = (char *)ioVector[i].sectionId.id;
			iov[iov_idx].iov_len = ioVector[i].sectionId.idLen;
			iov_idx++;
		}
		iov[iov_idx].iov_base = ioVector[i].dataBuffer;
		iov[iov_idx].iov_len = ioVector[i].dataSize;
		iov_idx++;

		error = coroipcc_msg_send_reply_receive (
			ckptCheckpointInstance->handle,
			iov,
			iov_idx,
			&res_lib_ckpt_sectionwrite,
			sizeof (struct res_lib_ckpt_sectionwrite));

		if (res_lib_ckpt_sectionwrite.header.error == SA_AIS_ERR_TRY_AGAIN) {
			error = SA_AIS_ERR_TRY_AGAIN;
			goto error_exit;
		}
		/*
		 * If error, report back erroneous index
		 */
		if (res_lib_ckpt_sectionwrite.header.error != SA_AIS_OK) {
			if (erroneousVectorIndex) {
				*erroneousVectorIndex = i;
			}
			goto error_exit;
		}
	}

error_exit:
error_put:
	hdb_handle_put (&checkpointHandleDatabase, checkpointHandle);

	return (error == SA_AIS_OK ? res_lib_ckpt_sectionwrite.header.error : error);
}

SaAisErrorT
saCkptSectionOverwrite (
	SaCkptCheckpointHandleT checkpointHandle,
	const SaCkptSectionIdT *sectionId,
	const void *dataBuffer,
	SaSizeT dataSize)
{
	SaAisErrorT error;
	struct iovec iov[3];
	int iov_idx;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_sectionoverwrite req_lib_ckpt_sectionoverwrite;
	struct res_lib_ckpt_sectionoverwrite res_lib_ckpt_sectionoverwrite;

	if (dataBuffer == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	if (sectionId == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_sa(hdb_handle_get (&checkpointHandleDatabase, checkpointHandle,
		(void *)&ckptCheckpointInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	if ((ckptCheckpointInstance->checkpointOpenFlags & SA_CKPT_CHECKPOINT_WRITE) == 0) {
		return (SA_AIS_ERR_ACCESS);
	}

	req_lib_ckpt_sectionoverwrite.header.size = sizeof (struct req_lib_ckpt_sectionoverwrite) + sectionId->idLen + dataSize;
	req_lib_ckpt_sectionoverwrite.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONOVERWRITE;
	req_lib_ckpt_sectionoverwrite.id_len = sectionId->idLen;
	req_lib_ckpt_sectionoverwrite.data_size = dataSize;
	marshall_SaNameT_to_mar_name_t (&req_lib_ckpt_sectionoverwrite.checkpoint_name,
		&ckptCheckpointInstance->checkpointName);
	req_lib_ckpt_sectionoverwrite.ckpt_id =
		ckptCheckpointInstance->checkpointId;

	/*
	 * Build request IO Vector
	 */
	iov[0].iov_base = (void *)&req_lib_ckpt_sectionoverwrite;
	iov[0].iov_len = sizeof (struct req_lib_ckpt_sectionoverwrite);
	iov_idx = 1;
	if (sectionId->idLen) {
		iov[iov_idx].iov_base = (void *)sectionId->id;
		iov[iov_idx].iov_len = sectionId->idLen;
		iov_idx += 1;
	}
	iov[iov_idx].iov_base = (void *)dataBuffer;
	iov[iov_idx].iov_len = dataSize;
	if (dataSize) {
		iov_idx += 1;
	}

	error = coroipcc_msg_send_reply_receive (ckptCheckpointInstance->handle,
		iov,
		iov_idx,
		&res_lib_ckpt_sectionoverwrite,
		sizeof (struct res_lib_ckpt_sectionoverwrite));

	hdb_handle_put (&checkpointHandleDatabase, checkpointHandle);

	return (error == SA_AIS_OK ? res_lib_ckpt_sectionoverwrite.header.error : error);
}

SaAisErrorT
saCkptCheckpointRead (
	SaCkptCheckpointHandleT checkpointHandle,
	SaCkptIOVectorElementT *ioVector,
	SaUint32T numberOfElements,
	SaUint32T *erroneousVectorIndex)
{
	SaAisErrorT error = SA_AIS_OK;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_sectionread req_lib_ckpt_sectionread;
	struct res_lib_ckpt_sectionread *res_lib_ckpt_sectionread = NULL;
	char *source_char;
	unsigned int copy_bytes;
	int i;
	struct iovec iov[3];
	int source_length;
	void *return_address;

	if (ioVector == NULL) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	error = hdb_error_to_sa(hdb_handle_get (&checkpointHandleDatabase, checkpointHandle,
		(void *)&ckptCheckpointInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	if ((ckptCheckpointInstance->checkpointOpenFlags & SA_CKPT_CHECKPOINT_READ) == 0) {
		return (SA_AIS_ERR_ACCESS);
	}

	req_lib_ckpt_sectionread.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_SECTIONREAD;

	for (i = 0; i < numberOfElements; i++) {
		req_lib_ckpt_sectionread.header.size =
			sizeof (struct req_lib_ckpt_sectionread) +
			ioVector[i].sectionId.idLen;

		req_lib_ckpt_sectionread.id_len = ioVector[i].sectionId.idLen;
		req_lib_ckpt_sectionread.data_offset = ioVector[i].dataOffset;
		req_lib_ckpt_sectionread.data_size = ioVector[i].dataSize;

		marshall_SaNameT_to_mar_name_t (&req_lib_ckpt_sectionread.checkpoint_name,
			&ckptCheckpointInstance->checkpointName);
		req_lib_ckpt_sectionread.ckpt_id =
			ckptCheckpointInstance->checkpointId;

		iov[0].iov_base = (void *)&req_lib_ckpt_sectionread;
		iov[0].iov_len = sizeof (struct req_lib_ckpt_sectionread);
		iov[1].iov_base = (void *)ioVector[i].sectionId.id;
		iov[1].iov_len = ioVector[i].sectionId.idLen;

		coroipcc_msg_send_reply_receive_in_buf_get (
			ckptCheckpointInstance->handle,
			iov,
			2,
			&return_address);
		res_lib_ckpt_sectionread = return_address;

		source_char = ((char *)(res_lib_ckpt_sectionread)) +
			sizeof (struct res_lib_ckpt_sectionread);

		source_length = res_lib_ckpt_sectionread->header.size -
			sizeof (struct res_lib_ckpt_sectionread);

		/*
		 * Receive checkpoint section data
		 */
		if (ioVector[i].dataBuffer == 0) {
			ioVector[i].dataBuffer =
				malloc (source_length);
			if (ioVector[i].dataBuffer == NULL) {
				error = SA_AIS_ERR_NO_MEMORY;
				goto error_exit;
			}
			ioVector[i].dataSize = source_length;
		}

		copy_bytes = source_length;
		if (source_length > 0) {
			if (copy_bytes > ioVector[i].dataSize) {
				copy_bytes = ioVector[i].dataSize;
			}
			memcpy (((char *)ioVector[i].dataBuffer) +
				ioVector[i].dataOffset,
				source_char, copy_bytes);
		}
		if (res_lib_ckpt_sectionread->header.error != SA_AIS_OK) {
			goto error_exit;
		}

		/*
		 * Report back bytes of data read
		 */
		ioVector[i].readSize = copy_bytes;
		coroipcc_msg_send_reply_receive_in_buf_put (
			ckptCheckpointInstance->handle);
	}

error_exit:
	hdb_handle_put (&checkpointHandleDatabase, checkpointHandle);

	if (error != SA_AIS_OK && erroneousVectorIndex) {
		*erroneousVectorIndex = i;
	}
	if (res_lib_ckpt_sectionread) {
		return (error == SA_AIS_OK ? res_lib_ckpt_sectionread->header.error : error);
	} else  {
		return (error);
	}
}

SaAisErrorT
saCkptCheckpointSynchronize (
	SaCkptCheckpointHandleT checkpointHandle,
	SaTimeT timeout)
{
	SaAisErrorT error;
	struct iovec iov;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_checkpointsynchronize req_lib_ckpt_checkpointsynchronize;
	struct res_lib_ckpt_checkpointsynchronize res_lib_ckpt_checkpointsynchronize;

	if (timeout == 0) {
		return (SA_AIS_ERR_TIMEOUT);
	}

	error = hdb_error_to_sa(hdb_handle_get (&checkpointHandleDatabase, checkpointHandle,
		(void *)&ckptCheckpointInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	if ((ckptCheckpointInstance->checkpointOpenFlags & SA_CKPT_CHECKPOINT_WRITE) == 0) {
		error = SA_AIS_ERR_ACCESS;
		goto error_put;
	}

	req_lib_ckpt_checkpointsynchronize.header.size = sizeof (struct req_lib_ckpt_checkpointsynchronize);
	req_lib_ckpt_checkpointsynchronize.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZE;
	marshall_SaNameT_to_mar_name_t (&req_lib_ckpt_checkpointsynchronize.checkpoint_name,
		&ckptCheckpointInstance->checkpointName);
	req_lib_ckpt_checkpointsynchronize.ckpt_id =
		ckptCheckpointInstance->checkpointId;

	iov.iov_base = (void *)&req_lib_ckpt_checkpointsynchronize;
	iov.iov_len = sizeof (struct req_lib_ckpt_checkpointsynchronize);

	error = coroipcc_msg_send_reply_receive (
		ckptCheckpointInstance->handle,
		&iov,
		1,
		&res_lib_ckpt_checkpointsynchronize,
		sizeof (struct res_lib_ckpt_checkpointsynchronize));

error_put:
	hdb_handle_put (&checkpointHandleDatabase, checkpointHandle);

	return (error == SA_AIS_OK ? res_lib_ckpt_checkpointsynchronize.header.error : error);
}

SaAisErrorT
saCkptCheckpointSynchronizeAsync (
	SaCkptCheckpointHandleT checkpointHandle,
	SaInvocationT invocation)
{
	SaAisErrorT error;
	struct iovec iov;
	struct ckptInstance *ckptInstance;
	struct ckptCheckpointInstance *ckptCheckpointInstance;
	struct req_lib_ckpt_checkpointsynchronizeasync req_lib_ckpt_checkpointsynchronizeasync;
	struct res_lib_ckpt_checkpointsynchronizeasync res_lib_ckpt_checkpointsynchronizeasync;

	error = hdb_error_to_sa(hdb_handle_get (&checkpointHandleDatabase, checkpointHandle,
		(void *)&ckptCheckpointInstance));
	if (error != SA_AIS_OK) {
		return (error);
	}

	if ((ckptCheckpointInstance->checkpointOpenFlags & SA_CKPT_CHECKPOINT_WRITE) == 0) {
		error = SA_AIS_ERR_ACCESS;
		goto error_put;
	}

	error = hdb_error_to_sa(hdb_handle_get (&ckptHandleDatabase, ckptCheckpointInstance->ckptHandle,
		(void *)&ckptInstance));
	if (error != SA_AIS_OK) {
		goto error_put;
	}

	if (ckptInstance->callbacks.saCkptCheckpointSynchronizeCallback == NULL) {
		hdb_handle_put (&ckptHandleDatabase, ckptCheckpointInstance->ckptHandle);
		error = SA_AIS_ERR_INIT;
		goto error_put;
	}

	hdb_handle_put (&ckptHandleDatabase, ckptCheckpointInstance->ckptHandle);

	req_lib_ckpt_checkpointsynchronizeasync.header.size = sizeof (struct req_lib_ckpt_checkpointsynchronizeasync);
	req_lib_ckpt_checkpointsynchronizeasync.header.id = MESSAGE_REQ_CKPT_CHECKPOINT_CHECKPOINTSYNCHRONIZEASYNC;
	marshall_SaNameT_to_mar_name_t (
		&req_lib_ckpt_checkpointsynchronizeasync.checkpoint_name,
		&ckptCheckpointInstance->checkpointName);
	req_lib_ckpt_checkpointsynchronizeasync.ckpt_id =
		ckptCheckpointInstance->checkpointId;
	req_lib_ckpt_checkpointsynchronizeasync.invocation = invocation;

	iov.iov_base = (void *)&req_lib_ckpt_checkpointsynchronizeasync;
	iov.iov_len = sizeof (struct req_lib_ckpt_checkpointsynchronizeasync);

	error = coroipcc_msg_send_reply_receive (
		ckptCheckpointInstance->handle,
		&iov,
		1,
		&res_lib_ckpt_checkpointsynchronizeasync,
		sizeof (struct res_lib_ckpt_checkpointsynchronizeasync));

error_put:
	hdb_handle_put (&checkpointHandleDatabase, checkpointHandle);

	return (error == SA_AIS_OK ? res_lib_ckpt_checkpointsynchronizeasync.header.error : error);
}

/** @} */
