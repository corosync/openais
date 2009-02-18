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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTIBUTORS "AS IS"
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
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include "../include/saAis.h"
#include "../include/ipc_gen.h"
#include "../include/ipc_confdb.h"
#include "../include/mar_gen.h"
#include "../lcr/lcr_comp.h"
#include "main.h"
#include "ipc.h"
#include "objdb.h"
#include "service.h"
#include "ipc.h"
#include "print.h"

static struct objdb_iface_ver0 *global_objdb;

static int confdb_exec_init_fn (struct objdb_iface_ver0 *objdb);

static int confdb_lib_init_fn (void *conn);
static int confdb_lib_exit_fn (void *conn);

static void message_handler_req_lib_confdb_object_create (void *conn, void *message);
static void message_handler_req_lib_confdb_object_destroy (void *conn, void *message);
static void message_handler_req_lib_confdb_object_find (void *conn, void *message);
static void message_handler_req_lib_confdb_object_find_reset (void *conn, void *message);

static void message_handler_req_lib_confdb_key_create (void *conn, void *message);
static void message_handler_req_lib_confdb_key_get (void *conn, void *message);


/*
 * Library Handler Definition
 */
static struct openais_lib_handler confdb_lib_service[] =
{
	{ /* 0 */
		.lib_handler_fn				= message_handler_req_lib_confdb_object_create,
		.response_size				= sizeof (mar_res_header_t),
		.response_id				= MESSAGE_RES_CONFDB_OBJECT_CREATE,
		.flow_control				= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn				= message_handler_req_lib_confdb_object_destroy,
		.response_size				= sizeof (mar_res_header_t),
		.response_id				= MESSAGE_RES_CONFDB_OBJECT_DESTROY,
		.flow_control				= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn				= message_handler_req_lib_confdb_object_find,
		.response_size				= sizeof (struct res_lib_confdb_object_find),
		.response_id				= MESSAGE_RES_CONFDB_OBJECT_FIND,
		.flow_control				= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn				= message_handler_req_lib_confdb_key_create,
		.response_size				= sizeof (mar_res_header_t),
		.response_id				= MESSAGE_RES_CONFDB_KEY_CREATE,
		.flow_control				= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn				= message_handler_req_lib_confdb_key_get,
		.response_size				= sizeof (struct res_lib_confdb_key_get),
		.response_id				= MESSAGE_RES_CONFDB_KEY_GET,
		.flow_control				= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 5 */
		.lib_handler_fn				= message_handler_req_lib_confdb_object_find_reset,
		.response_size				= sizeof (struct res_lib_confdb_object_find_reset),
		.response_id				= MESSAGE_RES_CONFDB_OBJECT_FIND_RESET,
		.flow_control				= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	}
};


struct openais_service_handler confdb_service_handler = {
	.name				        = (unsigned char *)"openais cluster config database access v1.01",
	.id					= CONFDB_SERVICE,
	.private_data_size			= 0,
	.flow_control				= OPENAIS_FLOW_CONTROL_NOT_REQUIRED,
	.lib_init_fn				= confdb_lib_init_fn,
	.lib_exit_fn				= confdb_lib_exit_fn,
	.lib_service				= confdb_lib_service,
	.lib_service_count			= sizeof (confdb_lib_service) / sizeof (struct openais_lib_handler),
	.exec_init_fn				= confdb_exec_init_fn,
};

/*
 * Dynamic loader definition
 */
static struct openais_service_handler *confdb_get_service_handler_ver0 (void);

static struct openais_service_handler_iface_ver0 confdb_service_handler_iface = {
	.openais_get_service_handler_ver0		= confdb_get_service_handler_ver0
};

static struct lcr_iface openais_confdb_ver0[1] = {
	{
		.name				= "openais_confdb",
		.version			= 0,
		.versions_replace		= 0,
		.versions_replace_count         = 0,
		.dependencies			= 0,
		.dependency_count		= 0,
		.constructor			= NULL,
		.destructor			= NULL,
		.interfaces			= NULL
	}
};

static struct lcr_comp confdb_comp_ver0 = {
	.iface_count			= 1,
	.ifaces			        = openais_confdb_ver0
};


static struct openais_service_handler *confdb_get_service_handler_ver0 (void)
{
	return (&confdb_service_handler);
}

__attribute__ ((constructor)) static void confdb_comp_register (void) {
        lcr_interfaces_set (&openais_confdb_ver0[0], &confdb_service_handler_iface);

	lcr_component_register (&confdb_comp_ver0);
}

static int confdb_exec_init_fn (struct objdb_iface_ver0 *objdb)
{
	global_objdb = objdb;
	return 0;
}

static int confdb_lib_init_fn (void *conn)
{
	log_printf(LOG_LEVEL_DEBUG, "lib_init_fn: conn=%p\n", conn);
	return (0);
}

static int confdb_lib_exit_fn (void *conn)
{

	log_printf(LOG_LEVEL_DEBUG, "exit_fn for conn=%p\n", conn);
	return (0);
}

static void message_handler_req_lib_confdb_object_create (void *conn, void *message)
{
	struct req_lib_confdb_object_create *req_lib_confdb_object_create = (struct req_lib_confdb_object_create *)message;
	struct res_lib_confdb_object_create res_lib_confdb_object_create;
	unsigned int object_handle;
	int ret = SA_AIS_OK;

	if (global_objdb->object_create(req_lib_confdb_object_create->parent_object_handle,
					&object_handle,
					req_lib_confdb_object_create->object_name.value,
					req_lib_confdb_object_create->object_name.length))
		ret = SA_AIS_ERR_ACCESS;

	res_lib_confdb_object_create.object_handle = object_handle;
	res_lib_confdb_object_create.header.size = sizeof(res_lib_confdb_object_create);
	res_lib_confdb_object_create.header.id = MESSAGE_RES_CONFDB_OBJECT_CREATE;
	res_lib_confdb_object_create.header.error = ret;
	openais_response_send(conn, &res_lib_confdb_object_create, sizeof(res_lib_confdb_object_create));
}

static void message_handler_req_lib_confdb_object_destroy (void *conn, void *message)
{
	struct req_lib_confdb_object_destroy *req_lib_confdb_object_destroy = (struct req_lib_confdb_object_destroy *)message;
	mar_res_header_t res;
	int ret = SA_AIS_OK;

	if (global_objdb->object_destroy(req_lib_confdb_object_destroy->object_handle))
		ret = SA_AIS_ERR_ACCESS;

	res.size = sizeof(res);
	res.id = MESSAGE_RES_CONFDB_OBJECT_CREATE;
	res.error = ret;
	openais_response_send(conn, &res, sizeof(res));
}


static void message_handler_req_lib_confdb_key_create (void *conn, void *message)
{
	struct req_lib_confdb_key_create *req_lib_confdb_key_create = (struct req_lib_confdb_key_create *)message;
	mar_res_header_t res;
	int ret = SA_AIS_OK;

	if (global_objdb->object_key_create(req_lib_confdb_key_create->object_handle,
					    req_lib_confdb_key_create->key_name.value,
					    req_lib_confdb_key_create->key_name.length,
					    req_lib_confdb_key_create->value.value,
					    req_lib_confdb_key_create->value.length))
		ret = SA_AIS_ERR_ACCESS;

	res.size = sizeof(res);
	res.id = MESSAGE_RES_CONFDB_KEY_CREATE;
	res.error = ret;
	openais_response_send(conn, &res, sizeof(res));
}

static void message_handler_req_lib_confdb_key_get (void *conn, void *message)
{
	struct req_lib_confdb_key_get *req_lib_confdb_key_get = (struct req_lib_confdb_key_get *)message;
	struct res_lib_confdb_key_get res_lib_confdb_key_get;
	int value_len;
	void *value = NULL;
	int ret = SA_AIS_OK;

	log_printf(LOG_LEVEL_DEBUG, "confdb_key_get: conn=%p, name=%s\n", conn, req_lib_confdb_key_get->key_name.value);

	global_objdb->object_key_get(req_lib_confdb_key_get->parent_object_handle,
				     req_lib_confdb_key_get->key_name.value,
				     req_lib_confdb_key_get->key_name.length,
				     &value,
				     &value_len);
	if (value) {
		memcpy(res_lib_confdb_key_get.value.value, value, value_len);
		res_lib_confdb_key_get.value.length = value_len;
	}
	else {
		ret = SA_AIS_ERR_ACCESS;
	}

	res_lib_confdb_key_get.header.size = sizeof(res_lib_confdb_key_get);
	res_lib_confdb_key_get.header.id = MESSAGE_RES_CONFDB_KEY_GET;
	res_lib_confdb_key_get.header.error = ret;
	openais_response_send(conn, &res_lib_confdb_key_get, sizeof(res_lib_confdb_key_get));
}


static void message_handler_req_lib_confdb_object_find (void *conn, void *message)
{
	struct req_lib_confdb_object_find *req_lib_confdb_object_find = (struct req_lib_confdb_object_find *)message;
	struct res_lib_confdb_object_find res_lib_confdb_object_find;
	int ret = SA_AIS_OK;

	log_printf(LOG_LEVEL_DEBUG, "confdb_object_find: conn=%p, name=(%d) '%s'\n", conn, req_lib_confdb_object_find->object_name.length, req_lib_confdb_object_find->object_name.value);

	if (global_objdb->object_find(req_lib_confdb_object_find->parent_object_handle,
				      req_lib_confdb_object_find->object_name.value,
				      req_lib_confdb_object_find->object_name.length,
				      &res_lib_confdb_object_find.object_handle))
		ret = SA_AIS_ERR_ACCESS;

	res_lib_confdb_object_find.header.size = sizeof(res_lib_confdb_object_find);
	res_lib_confdb_object_find.header.id = MESSAGE_RES_CONFDB_OBJECT_FIND;
	res_lib_confdb_object_find.header.error = ret;

	openais_response_send(conn, &res_lib_confdb_object_find, sizeof(res_lib_confdb_object_find));
}


static void message_handler_req_lib_confdb_object_find_reset (void *conn, void *message)
{
	struct req_lib_confdb_object_find_reset *req_lib_confdb_object_find_reset = (struct req_lib_confdb_object_find_reset *)message;
	struct res_lib_confdb_object_find_reset res_lib_confdb_object_find_reset;
	int ret = SA_AIS_OK;

	log_printf(LOG_LEVEL_DEBUG, "confdb_object_find_reset: conn=%p, object=%d\n", conn, req_lib_confdb_object_find_reset->object_handle);

	if (global_objdb->object_find_reset(req_lib_confdb_object_find_reset->object_handle))
		ret = SA_AIS_ERR_ACCESS;

	res_lib_confdb_object_find_reset.header.size = sizeof(res_lib_confdb_object_find_reset);
	res_lib_confdb_object_find_reset.header.id = MESSAGE_RES_CONFDB_OBJECT_FIND_RESET;
	res_lib_confdb_object_find_reset.header.error = ret;

	openais_response_send(conn, &res_lib_confdb_object_find_reset, sizeof(res_lib_confdb_object_find_reset));
}

