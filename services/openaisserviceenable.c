/*
 * Copyright (c) 2006-2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield (ccaulfie@redhat.com)
 * Author: Jan Friesse (jfriesse@redhat.com)
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
#include <string.h>

#include <corosync/engine/objdb.h>
#include <corosync/engine/config.h>
#include <corosync/lcr/lcr_comp.h>

static char error_reason[512];

struct service_engine {
	const char *name;
	const char *ver;
};

static struct service_engine service_engines[] = {
	{ "openais_clm", "0" },
	{ "openais_evt", "0" },
	{ "openais_ckpt", "0" },
	{ "openais_amf", "0" },
	{ "openais_msg", "0" },
	{ "openais_lck", "0" },
	{ "openais_tmr", "0" },
};

static const char *exluded_service_engines[] = {
	"openais_amf"
};

static int openais_service_enable_worker (
	struct objdb_iface_ver0 *objdb,
	const char **error_string,
	const char **excluded_services,
	const int no_excluded_services)
{
	unsigned int i;
	unsigned int j;
	int include_service;

	hdb_handle_t object_handle;

	for (i = 0; i < sizeof (service_engines) / sizeof (struct service_engine); i++) {
		include_service = 1;

		for (j = 0; no_excluded_services && j < no_excluded_services; j++) {
			if (strcmp (service_engines[i].name, excluded_services[j]) == 0) {
				include_service = 0;
				break;
			}
		}

		if (include_service) {
			objdb->object_create(OBJECT_PARENT_HANDLE, &object_handle,
				"service", strlen("service"));
			objdb->object_key_create(object_handle, "name", strlen("name"),
				service_engines[i].name, strlen(service_engines[i].name)+ 1);
			objdb->object_key_create(object_handle, "ver", strlen("ver"),
				service_engines[i].ver, 2);
		}
	}

	sprintf (error_reason, "Successfully configured openais services to load\n");
	*error_string = error_reason;

	return (0);
}

static int openais_service_enable_experimental (
	struct objdb_iface_ver0 *objdb,
	const char **error_string)
{
	return openais_service_enable_worker (objdb, error_string, NULL, 0);
}

static int openais_service_enable_stable (
	struct objdb_iface_ver0 *objdb,
	const char **error_string)
{
	return openais_service_enable_worker (objdb, error_string, exluded_service_engines, 1);
}
/*
 * Dynamic Loader definition
 */

struct config_iface_ver0 serviceenable_stable_iface_ver0 = {
	.config_readconfig      = openais_service_enable_stable,
	.config_writeconfig 	= NULL,
	.config_reloadconfig 	= NULL
};

struct config_iface_ver0 serviceenable_experimental_iface_ver0 = {
	.config_readconfig      = openais_service_enable_experimental,
	.config_writeconfig 	= NULL,
	.config_reloadconfig 	= NULL
};

struct lcr_iface openais_serviceenable_ver0[2] = {
	{
		.name				= "openaisserviceenablestable",
		.version			= 0,
		.versions_replace		= 0,
		.versions_replace_count		= 0,
		.dependencies			= 0,
		.dependency_count		= 0,
		.constructor			= NULL,
		.destructor			= NULL,
		.interfaces			= NULL,
	},
	{
		.name				= "openaisserviceenableexperimental",
		.version			= 0,
		.versions_replace		= 0,
		.versions_replace_count		= 0,
		.dependencies			= 0,
		.dependency_count		= 0,
		.constructor			= NULL,
		.destructor			= NULL,
		.interfaces			= NULL,
	}
};

struct openais_service_handler *serviceenable_get_handler_ver0 (void);

struct lcr_comp serviceenable_comp_ver0 = {
	.iface_count				= 2,
	.ifaces					= openais_serviceenable_ver0
};


#ifdef OPENAIS_SOLARIS
void corosync_lcr_component_register (void);

void corosync_lcr_component_register (void)
{
#else
__attribute__ ((constructor)) static void corosync_lcr_component_register (void)
{
#endif
        lcr_interfaces_set (&openais_serviceenable_ver0[0], &serviceenable_stable_iface_ver0);
        lcr_interfaces_set (&openais_serviceenable_ver0[1], &serviceenable_experimental_iface_ver0);

	lcr_component_register (&serviceenable_comp_ver0);
}
