/*
 * Copyright (c) 2008-2009 Red Hat Inc
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield <ccaulfie@redhat.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/un.h>

#include "saAis.h"
#include "../exec/objdb.h"
#include "confdb.h"


int main (int argc, char *argv[]) {
	confdb_handle_t handle;
	int result;
	unsigned int totem_handle;
	unsigned int object_handle;
	char key_value[256];
	int value_len;

	result = confdb_initialize (&handle, NULL);
	if (result != SA_AIS_OK) {
		printf ("Could not initialize Cluster Configuration Database API instance error %d\n", result);
		exit (1);
	}


	if (argc == 1) {

		/* Find "totem" and dump bits of it again, to test the direct APIs */
		result = confdb_object_find_start(handle, OBJECT_PARENT_HANDLE);
		if (result != SA_AIS_OK) {
			printf ("Could not start object_find %d\n", result);
			exit (1);
		}

		result = confdb_object_find(handle, OBJECT_PARENT_HANDLE, "totem", strlen("totem"), &totem_handle);
		if (result != SA_AIS_OK) {
			printf ("Could not object_find \"totem\": %d\n", result);
			exit (1);
		}

		result = confdb_key_get(handle, totem_handle, "version", strlen("version"), key_value, &value_len);
		if (result != SA_AIS_OK) {
			printf ("Could not get \"version\" key: %d\n", result);
			exit (1);
		}
		key_value[value_len] = '\0';
		printf("totem.version = '%s'\n", key_value);

		result = confdb_key_get(handle, totem_handle, "secauth", strlen("secauth"), key_value, &value_len);
		if (result != SA_AIS_OK) {
			printf ("Could not get \"secauth\" key: %d\n", result);
			exit (1);
		}
		key_value[value_len] = '\0';
		printf("totem.secauth = '%s'\n", key_value);
	}
	else {
		/* Use argv[1] as the object name, and argv[2...] as the key names and dump what we find
		 * (if anything).
		 */
		int i;

		result = confdb_object_find_start(handle, OBJECT_PARENT_HANDLE);
		if (result != SA_AIS_OK) {
			printf ("Could not start object_find %d\n", result);
			exit (1);
		}

		result = confdb_object_find(handle, OBJECT_PARENT_HANDLE, argv[1], strlen(argv[1]), &object_handle);
		if (result != SA_AIS_OK) {
			printf ("Could not find object \"%s\": %d\n", argv[1], result);
			goto finish;
		}

		i=1;
		while (argv[++i]) {
			result = confdb_key_get(handle, object_handle, argv[i], strlen(argv[i]), key_value, &value_len);
			if (result != SA_AIS_OK) {
				printf ("Could not get \"%s\" : %d\n", argv[i], result);
			}
			else {
				key_value[value_len] = '\0';
				printf("%s.%s = '%s'\n", argv[1], argv[i], key_value);
			}
		}
	}

finish:
	result = confdb_finalize (handle);
	return (0);
}
