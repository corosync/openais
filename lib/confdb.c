/*
 * Copyright (c) 2008-2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield (ccaulfie@redhat.com)
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
/*
 * Provides access to data in the openais object database
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <errno.h>

#include "../include/saAis.h"
#include "../include/confdb.h"
#include "../include/ipc_confdb.h"
#include "../include/mar_gen.h"
#include "util.h"

struct confdb_inst {
	void *ipc_ctx;
	int finalize;
	pthread_mutex_t response_mutex;
};

static void confdb_instance_destructor (void *instance);

static struct saHandleDatabase confdb_handle_t_db = {
	.handleCount		        = 0,
	.handles			= 0,
	.mutex				= PTHREAD_MUTEX_INITIALIZER,
	.handleInstanceDestructor	= confdb_instance_destructor
};

/*
 * Clean up function for a confdb instance (confdb_initialize) handle
 */
static void confdb_instance_destructor (void *instance)
{
	struct confdb_inst *confdb_inst = instance;

	pthread_mutex_destroy (&confdb_inst->response_mutex);
}

/**
 * @defgroup confdb_openais
 * @ingroup openais
 *
 * @{
 */

confdb_error_t confdb_initialize (
	confdb_handle_t *handle,
	confdb_callbacks_t *callbacks)
{
	SaAisErrorT error;
	struct confdb_inst *confdb_inst;

	error = saHandleCreate (&confdb_handle_t_db, sizeof (struct confdb_inst), handle);
	if (error != SA_AIS_OK) {
		goto error_no_destroy;
	}

	error = saHandleInstanceGet (&confdb_handle_t_db, *handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		goto error_destroy;
	}

	error = openais_service_connect (CONFDB_SERVICE, &confdb_inst->ipc_ctx);

	if (error != SA_AIS_OK)
		goto error_put_destroy;

	pthread_mutex_init (&confdb_inst->response_mutex, NULL);

	saHandleInstancePut (&confdb_handle_t_db, *handle);

	return (SA_AIS_OK);

error_put_destroy:
	saHandleInstancePut (&confdb_handle_t_db, *handle);
error_destroy:
	saHandleDestroy (&confdb_handle_t_db, *handle);
error_no_destroy:
	return (error);
}

confdb_error_t confdb_finalize (
	confdb_handle_t handle)
{
	struct confdb_inst *confdb_inst;
	SaAisErrorT error;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	pthread_mutex_lock (&confdb_inst->response_mutex);

	/*
	 * Another thread has already started finalizing
	 */
	if (confdb_inst->finalize) {
		pthread_mutex_unlock (&confdb_inst->response_mutex);
		saHandleInstancePut (&confdb_handle_t_db, handle);
		return (CONFDB_ERR_BAD_HANDLE);
	}

	confdb_inst->finalize = 1;

	openais_service_disconnect (confdb_inst->ipc_ctx);

	pthread_mutex_unlock (&confdb_inst->response_mutex);

	saHandleDestroy (&confdb_handle_t_db, handle);

	saHandleInstancePut (&confdb_handle_t_db, handle);

	return (CONFDB_OK);
}

confdb_error_t confdb_object_create (
	confdb_handle_t handle,
	unsigned int parent_object_handle,
	void *object_name,
	int object_name_len,
	unsigned int *object_handle)
{
	confdb_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov[2];
	struct req_lib_confdb_object_create req_lib_confdb_object_create;
	struct res_lib_confdb_object_create res_lib_confdb_object_create;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_confdb_object_create.header.size = sizeof (struct req_lib_confdb_object_create);
	req_lib_confdb_object_create.header.id = MESSAGE_REQ_CONFDB_OBJECT_CREATE;
	req_lib_confdb_object_create.parent_object_handle = parent_object_handle;
	memcpy(req_lib_confdb_object_create.object_name.value, object_name, object_name_len);
	req_lib_confdb_object_create.object_name.length = object_name_len;

	iov[0].iov_base = (char *)&req_lib_confdb_object_create;
	iov[0].iov_len = sizeof (struct req_lib_confdb_object_create);

	pthread_mutex_lock (&confdb_inst->response_mutex);

	error = openais_msg_send_reply_receive (confdb_inst->ipc_ctx, iov, 1,
		&res_lib_confdb_object_create, sizeof (struct res_lib_confdb_object_create));

	pthread_mutex_unlock (&confdb_inst->response_mutex);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res_lib_confdb_object_create.header.error;
	*object_handle = res_lib_confdb_object_create.object_handle;

error_exit:
	saHandleInstancePut (&confdb_handle_t_db, handle);

	return (error);
}

confdb_error_t confdb_object_destroy (
	confdb_handle_t handle,
	unsigned int object_handle)
{
	confdb_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov[2];
	struct req_lib_confdb_object_destroy req_lib_confdb_object_destroy;
	mar_res_header_t res;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_confdb_object_destroy.header.size = sizeof (struct req_lib_confdb_object_destroy);
	req_lib_confdb_object_destroy.header.id = MESSAGE_REQ_CONFDB_OBJECT_DESTROY;
	req_lib_confdb_object_destroy.object_handle = object_handle;

	iov[0].iov_base = (char *)&req_lib_confdb_object_destroy;
	iov[0].iov_len = sizeof (struct req_lib_confdb_object_destroy);

	pthread_mutex_lock (&confdb_inst->response_mutex);

	error = openais_msg_send_reply_receive (confdb_inst->ipc_ctx, iov, 1,
		&res, sizeof ( mar_res_header_t));

	pthread_mutex_unlock (&confdb_inst->response_mutex);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res.error;

error_exit:
	saHandleInstancePut (&confdb_handle_t_db, handle);

	return (error);
}


confdb_error_t confdb_key_create (
	confdb_handle_t handle,
	unsigned int parent_object_handle,
	void *key_name,
	int key_name_len,
	void *value,
	int value_len)
{
	confdb_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov[2];
	struct req_lib_confdb_key_create req_lib_confdb_key_create;
	mar_res_header_t res;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_confdb_key_create.header.size = sizeof (struct req_lib_confdb_key_create);
	req_lib_confdb_key_create.header.id = MESSAGE_REQ_CONFDB_KEY_CREATE;
	req_lib_confdb_key_create.object_handle = parent_object_handle;
	memcpy(req_lib_confdb_key_create.key_name.value, key_name, key_name_len);
	req_lib_confdb_key_create.key_name.length = key_name_len;
	memcpy(req_lib_confdb_key_create.value.value, value, value_len);
	req_lib_confdb_key_create.value.length = value_len;

	iov[0].iov_base = (char *)&req_lib_confdb_key_create;
	iov[0].iov_len = sizeof (struct req_lib_confdb_key_create);

	pthread_mutex_lock (&confdb_inst->response_mutex);

	error = openais_msg_send_reply_receive (confdb_inst->ipc_ctx, iov, 1,
		&res, sizeof (res));

	pthread_mutex_unlock (&confdb_inst->response_mutex);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res.error;

error_exit:
	saHandleInstancePut (&confdb_handle_t_db, handle);

	return (error);
}

confdb_error_t confdb_key_get (
	confdb_handle_t handle,
	unsigned int parent_object_handle,
	void *key_name,
	int key_name_len,
	void *value,
	int *value_len)
{
	confdb_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov[2];
	struct req_lib_confdb_key_get req_lib_confdb_key_get;
	struct res_lib_confdb_key_get res_lib_confdb_key_get;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_confdb_key_get.header.size = sizeof (struct req_lib_confdb_key_get);
	req_lib_confdb_key_get.header.id = MESSAGE_REQ_CONFDB_KEY_GET;
	req_lib_confdb_key_get.parent_object_handle = parent_object_handle;
	memcpy(req_lib_confdb_key_get.key_name.value, key_name, key_name_len);
	req_lib_confdb_key_get.key_name.length = key_name_len;

	iov[0].iov_base = (char *)&req_lib_confdb_key_get;
	iov[0].iov_len = sizeof (struct req_lib_confdb_key_get);

	pthread_mutex_lock (&confdb_inst->response_mutex);

	error = openais_msg_send_reply_receive (confdb_inst->ipc_ctx, iov, 1,
		&res_lib_confdb_key_get, sizeof (struct res_lib_confdb_key_get));

	pthread_mutex_unlock (&confdb_inst->response_mutex);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res_lib_confdb_key_get.header.error;
	if (error == SA_AIS_OK) {
		*value_len = res_lib_confdb_key_get.value.length;
		memcpy(value, res_lib_confdb_key_get.value.value, *value_len);
	}

error_exit:
	saHandleInstancePut (&confdb_handle_t_db, handle);

	return (error);
}


confdb_error_t confdb_object_find_start (
	confdb_handle_t handle,
	unsigned int parent_object_handle)
{
	struct confdb_inst *confdb_inst;
	struct iovec iov[2];
	struct req_lib_confdb_object_find_reset req_lib_confdb_object_find_reset;
	struct res_lib_confdb_object_find_reset res_lib_confdb_object_find_reset;

	confdb_error_t error = SA_AIS_OK;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_confdb_object_find_reset.header.size = sizeof (struct req_lib_confdb_object_find_reset);
	req_lib_confdb_object_find_reset.header.id = MESSAGE_REQ_CONFDB_OBJECT_FIND_RESET;
	req_lib_confdb_object_find_reset.object_handle = parent_object_handle;

	iov[0].iov_base = (char *)&req_lib_confdb_object_find_reset;
	iov[0].iov_len = sizeof (struct req_lib_confdb_object_find_reset);

	pthread_mutex_lock (&confdb_inst->response_mutex);

	error = openais_msg_send_reply_receive (confdb_inst->ipc_ctx, iov, 1,
		&res_lib_confdb_object_find_reset, sizeof (struct res_lib_confdb_object_find_reset));

	pthread_mutex_unlock (&confdb_inst->response_mutex);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res_lib_confdb_object_find_reset.header.error;

error_exit:

	saHandleInstancePut (&confdb_handle_t_db, handle);

	return error;
}

confdb_error_t confdb_object_find (
	confdb_handle_t handle,
	unsigned int parent_object_handle,
	void *object_name,
	int object_name_len,
	unsigned int *object_handle)
{
	confdb_error_t error;
	struct confdb_inst *confdb_inst;
	struct iovec iov[2];
	struct req_lib_confdb_object_find req_lib_confdb_object_find;
	struct res_lib_confdb_object_find res_lib_confdb_object_find;

	error = saHandleInstanceGet (&confdb_handle_t_db, handle, (void *)&confdb_inst);
	if (error != SA_AIS_OK) {
		return (error);
	}

	req_lib_confdb_object_find.header.size = sizeof (struct req_lib_confdb_object_find);
	req_lib_confdb_object_find.header.id = MESSAGE_REQ_CONFDB_OBJECT_FIND;
	req_lib_confdb_object_find.parent_object_handle = parent_object_handle;
	memcpy(req_lib_confdb_object_find.object_name.value, object_name, object_name_len);
	req_lib_confdb_object_find.object_name.length = object_name_len;

	iov[0].iov_base = (char *)&req_lib_confdb_object_find;
	iov[0].iov_len = sizeof (struct req_lib_confdb_object_find);

	pthread_mutex_lock (&confdb_inst->response_mutex);

	error = openais_msg_send_reply_receive (confdb_inst->ipc_ctx, iov, 1,
		&res_lib_confdb_object_find, sizeof (struct res_lib_confdb_object_find));

	pthread_mutex_unlock (&confdb_inst->response_mutex);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	error = res_lib_confdb_object_find.header.error;
	*object_handle = res_lib_confdb_object_find.object_handle;

error_exit:
	saHandleInstancePut (&confdb_handle_t_db, handle);

	return (error);
}


