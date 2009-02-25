/*
 * Copyright (c) 2008 Red Hat, Inc.
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
#include "../include/saAis.h"
#include "../include/saClm.h"

#ifndef CLM_H_DEFINED
#define CLM_H_DEFINED

struct openais_clm_services_api_ver1 {
	SaClmClusterNodeT *(*nodeid_saf_get) (unsigned int node_id);
};

static inline struct openais_clm_services_api_ver1 *
openais_clm_services_api_reference (
	struct corosync_api_v1 *coroapi,
	hdb_handle_t *handle)
{
	static void *clm_services_api_p;
	struct openais_clm_services_api_ver1 *return_api;
	unsigned int res;

	res = coroapi->plugin_interface_reference (
		handle,
		"openais_clm_services_api",
		0,
		&clm_services_api_p,
		0);
	if (res == -1) {
		return (NULL);
	}
	return_api = (struct openais_clm_services_api_ver1 *)clm_services_api_p;
	return (return_api);
}
	
static int inline openais_clm_services_api_release (
	struct corosync_api_v1 *coroapi,
	unsigned int handle)
{
	unsigned int res;

	res = coroapi->plugin_interface_release (handle);
	return (res);
}
	
#endif /* CLM_H_DEFINED */
