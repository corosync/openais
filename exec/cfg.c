/*
 * Copyright (c) 2005-2006 MontaVista Software, Inc.
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
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#include "../include/saAis.h"
#include "../include/openaisCfg.h"
#include "../include/ipc_gen.h"
#include "../include/ipc_cfg.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "../lcr/lcr_comp.h"
#include "totempg.h"
#include "aispoll.h"
#include "mempool.h"
#include "util.h"
#include "handlers.h"

#define LOG_SERVICE LOG_SERVICE_AMF
#include "print.h"
#define LOG_LEVEL_FROM_LIB LOG_LEVEL_DEBUG
#define LOG_LEVEL_FROM_GMI LOG_LEVEL_DEBUG
#define LOG_LEVEL_ENTER_FUNC LOG_LEVEL_DEBUG

static void cfg_confchg_fn (
	enum totem_configuration_type configuration_type,
	struct totem_ip_address *member_list, int member_list_entries,
	struct totem_ip_address *left_list, int left_list_entries,
	struct totem_ip_address *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id);

static int cfg_lib_exit_fn (struct conn_info *conn_info);

static int cfg_exec_init_fn (struct openais_config *);

static int cfg_lib_init_fn (struct conn_info *conn_info);

static void message_handler_req_lib_cfg_statetrackstart (struct conn_info *conn_info, void *message);

static void message_handler_req_lib_cfg_statetrackstop (struct conn_info *conn_info, void *message);

static void message_handler_req_lib_cfg_administrativestateset (struct conn_info *conn_info, void *message);

static void message_handler_req_lib_cfg_administrativestateget (struct conn_info *conn_info, void *message);

/*
 * Service Handler Definition
 */
static struct openais_lib_handler cfg_lib_handlers[] =
{
	{ /* 0 */
		.lib_handler_fn		= message_handler_req_lib_cfg_statetrackstart,
		.response_size		= sizeof (struct res_lib_cfg_statetrackstart),
		.response_id		= MESSAGE_RES_CFG_STATETRACKSTART,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn		= message_handler_req_lib_cfg_statetrackstop,
		.response_size		= sizeof (struct res_lib_cfg_statetrackstop),
		.response_id		= MESSAGE_RES_CFG_STATETRACKSTOP,
		.flow_control		= OPENAIS_FLOW_CONTROL_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn		= message_handler_req_lib_cfg_administrativestateset,
		.response_size		= sizeof (struct res_lib_cfg_administrativestateset),
		.response_id		= MESSAGE_RES_CFG_ADMINISTRATIVESTATESET,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn		= message_handler_req_lib_cfg_administrativestateget,
		.response_size		= sizeof (struct res_lib_cfg_administrativestateget),
		.response_id		= MESSAGE_RES_CFG_ADMINISTRATIVESTATEGET,
		.flow_control		= OPENAIS_FLOW_CONTROL_NOT_REQUIRED
	}
};

static struct openais_exec_handler cfg_exec_handlers[] =
{
	{
	}
/*
	message_handler_req_exec_cfg_componentregister,
	message_handler_req_exec_cfg_componentunregister,
	message_handler_req_exec_cfg_componenterrorreport,
	message_handler_req_exec_cfg_componenterrorclear,
*/
};

/*
 * Exports the interface for the service
 */
struct openais_service_handler cfg_service_handler = {
	.name					= (unsigned char*)"openais configuration service",
	.id					= CFG_SERVICE,
	.lib_init_fn				= cfg_lib_init_fn,
	.lib_exit_fn				= cfg_lib_exit_fn,
	.lib_handlers				= cfg_lib_handlers,
	.lib_handlers_count			= sizeof (cfg_lib_handlers) / sizeof (struct openais_lib_handler),
	.exec_init_fn				= cfg_exec_init_fn,
	.exec_handlers				= cfg_exec_handlers,
	.exec_handlers_count			= 0, /* sizeof (cfg_aisexec_handler_fns) / sizeof (openais_exec_handler), */
	.confchg_fn				= cfg_confchg_fn,
};

/*
 * Dynamic Loader definition
 */
#ifdef BUILD_DYNAMIC

struct openais_service_handler *cfg_get_handler_ver0 (void);

struct openais_service_handler_iface_ver0 cfg_service_handler_iface = {
	.openais_get_service_handler_ver0	= cfg_get_handler_ver0
};

struct lcr_iface openais_cfg_ver0[1] = {
	{
		.name				= "openais_cfg",
		.version			= 0,
		.versions_replace		= 0,
		.versions_replace_count		= 0,
		.dependencies			= 0,
		.dependency_count		= 0,
		.constructor			= NULL,
		.destructor			= NULL,
		.interfaces			= (void **)&cfg_service_handler_iface,
	}
};

struct lcr_comp cfg_comp_ver0 = {
	.iface_count				= 1,
	.ifaces					= openais_cfg_ver0
};

extern int lcr_comp_get (struct lcr_comp **component)
{
	*component = &cfg_comp_ver0;
	return (0);
}

struct openais_service_handler *cfg_get_handler_ver0 (void)
{
	return (&cfg_service_handler);
}
#endif /* BUILD_DYNAMIC */

/* IMPL */

static int cfg_exec_init_fn (struct openais_config *openais_config)
{
	return (0);
}
static void cfg_confchg_fn (
	enum totem_configuration_type configuration_type,
	struct totem_ip_address *member_list, int member_list_entries,
	struct totem_ip_address *left_list, int left_list_entries,
	struct totem_ip_address *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{
}

int cfg_lib_exit_fn (struct conn_info *conn_info)
{
	return (0);
}

static int cfg_lib_init_fn (struct conn_info *conn_info)
{
        log_printf (LOG_LEVEL_DEBUG, "Got request to initalize configuration service.\n");

   //     list_init (&conn_info->conn_list);

        return (0);
}

/*
 * Library Interface Implementation
 */
static void message_handler_req_lib_cfg_statetrackstart (struct conn_info *conn_info, void *message)
{
//	struct req_lib_cfg_statetrackstart *req_lib_cfg_statetrackstart = (struct req_lib_cfg_statetrackstart *)message;

	log_printf (LOG_LEVEL_FROM_LIB,
		"Handle : message_handler_req_lib_cfg_statetrackstart()\n");
}

static void message_handler_req_lib_cfg_statetrackstop (struct conn_info *conn_info, void *message)
{
//	struct req_lib_cfg_statetrackstop *req_lib_cfg_statetrackstop = (struct req_lib_cfg_statetrackstop *)message;

	log_printf (LOG_LEVEL_FROM_LIB,
		"Handle : message_handler_req_lib_cfg_administrativestateget()\n");
}

static void message_handler_req_lib_cfg_administrativestateset (struct conn_info *conn_info, void *message)
{
//	struct req_lib_cfg_administrativestateset *req_lib_cfg_administrativestateset = (struct req_lib_cfg_administrativestateset *)message;
	log_printf (LOG_LEVEL_FROM_LIB,
		"Handle : message_handler_req_lib_cfg_administrativestateset()\n");
}
static void message_handler_req_lib_cfg_administrativestateget (struct conn_info *conn_info, void *message)
{
//	struct req_lib_cfg_administrativestateget *req_lib_cfg_administrativestateget = (struct req_lib_cfg_administrativestateget *)message;
	log_printf (LOG_LEVEL_FROM_LIB,
		"Handle : message_handler_req_lib_cfg_administrativestateget()\n");
}

