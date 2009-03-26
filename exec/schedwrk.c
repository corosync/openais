/*
 * Copyright (c) 2009 Red Hat, Inc.
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

#include "totempg.h"
#include "../include/hdb.h"

static void (*serialize_lock) (void);
static void (*serialize_unlock) (void);

typedef unsigned int hdb_handle_t;

static struct hdb_handle_database schedwrk_instance_database = {
	.handle_count	= 0,
	.handles	= 0,
	.iterator	= 0,
	.mutex		= PTHREAD_MUTEX_INITIALIZER
};

struct schedwrk_instance {
	int (*schedwrk_fn) (void *);
	void *context;
	void *callback_handle;
};

static int schedwrk_do (enum totem_callback_token_type type, void *context)
{
	hdb_handle_t *handle = (hdb_handle_t *)context;
	struct schedwrk_instance *instance;
	int res;

	res = hdb_handle_get (&schedwrk_instance_database, *handle,
		(void *)&instance);
	if (res != 0) {
		goto error_exit;
	}

	serialize_lock ();
	res = instance->schedwrk_fn (instance->context);
	serialize_unlock ();

	if (res == -1) {
		hdb_handle_destroy (&schedwrk_instance_database, *handle);
	}
        hdb_handle_put (&schedwrk_instance_database, *handle);
	return (res);

error_exit:
	return (-1);
}
	
void schedwrk_init (
	void (*serialize_lock_fn) (void),
	void (*serialize_unlock_fn) (void))
{
	serialize_lock = serialize_lock_fn;
	serialize_unlock = serialize_unlock_fn;
}

int schedwrk_create (
	hdb_handle_t *handle,
	int (schedwrk_fn) (void *),
	void *context)
{
	struct schedwrk_instance *instance;
	int res;

	res = hdb_handle_create (&schedwrk_instance_database,
		sizeof (struct schedwrk_instance), handle);
	if (res != 0) {
		goto error_exit;
	}
	res = hdb_handle_get (&schedwrk_instance_database, *handle,
		(void *)&instance);
	if (res != 0) {
		goto error_destroy;
	}

	totempg_callback_token_create (
		&instance->callback_handle,
		TOTEM_CALLBACK_TOKEN_SENT,
		1,
		schedwrk_do,
		handle);

	instance->schedwrk_fn = schedwrk_fn;
	instance->context = context;

        hdb_handle_put (&schedwrk_instance_database, *handle);

	return (0);

error_destroy:
	hdb_handle_destroy (&schedwrk_instance_database, *handle);

error_exit:
	return (-1);
}

void schedwrk_destroy (hdb_handle_t handle)
{
	hdb_handle_destroy (&schedwrk_instance_database, handle);
}
