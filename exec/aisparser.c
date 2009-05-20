/*
 * Copyright (c) 2006-2007 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Patrick Caulfield (pcaulfie@redhat.com)
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
#include <dirent.h>

#include "../lcr/lcr_comp.h"
#include "objdb.h"
#include "config.h"
#include "mempool.h"
#include "util.h"

static int read_config_file_into_objdb(
	struct objdb_iface_ver0 *objdb,
	char **error_string);
static char error_string_response[512];

#define PCHECK_ADD_SUBSECTION 1
#define PCHECK_ADD_ITEM       2

typedef int (*parser_check_item_f)(struct objdb_iface_ver0 *objdb,
				unsigned int parent_handle,
				int type,
				const char *name,
				char **error_string);

static int aisparser_readconfig (struct objdb_iface_ver0 *objdb, char **error_string)
{
	if (read_config_file_into_objdb(objdb, error_string)) {
		return -1;
	}

	return 0;
}


static char *remove_whitespace(char *string)
{
	char *start = string+strspn(string, " \t");
	char *end = start+(strlen(start))-1;

	while ((*end == ' ' || *end == '\t' || *end == ':' || *end == '{') && end > start)
		end--;
	if (end != start)
		*(end+1) = '\0';

	return start;
}

static int parse_section(FILE *fp,
			 struct objdb_iface_ver0 *objdb,
			 unsigned int parent_handle,
			 char **error_string,
			 parser_check_item_f parser_check_item_call)
{
	char line[512];
	int i;
	char *loc;

	while (fgets (line, 255, fp)) {
		line[strlen(line) - 1] = '\0';
		/*
		 * Clear out white space and tabs
		 */
		for (i = strlen (line) - 1; i > -1; i--) {
			if (line[i] == '\t' || line[i] == ' ') {
				line[i] = '\0';
			} else {
				break;
			}
		}
		/*
		 * Clear out comments and empty lines
		 */
		if (line[0] == '#' || line[0] == '\0') {
			continue;
		}

		/* New section ? */
		if ((loc = strstr_rs (line, "{"))) {
			unsigned int new_parent;
			char *section = remove_whitespace(line);

			loc--;
			*loc = '\0';
			if (parser_check_item_call) {
				if (!parser_check_item_call(objdb, parent_handle, PCHECK_ADD_SUBSECTION,
				    section, error_string))
					    return -1;
			}

			objdb->object_create (parent_handle, &new_parent,
					      section, strlen (section));
			if (parse_section(fp, objdb, new_parent, error_string, parser_check_item_call))
				return -1;
		}

		/* New key/value */
		if ((loc = strstr_rs (line, ":"))) {
			char *key;
			char *value;

			*(loc-1) = '\0';
			key = remove_whitespace(line);
			value = remove_whitespace(loc);
			if (parser_check_item_call) {
				if (!parser_check_item_call(objdb, parent_handle, PCHECK_ADD_ITEM,
				    key, error_string))
					    return -1;
			}
			objdb->object_key_create (parent_handle, key,
				strlen (key),
				value, strlen (value) + 1);
		}

		if ((loc = strstr_rs (line, "}"))) {
			return 0;
		}
	}

	if (parent_handle != OBJECT_PARENT_HANDLE) {
		*error_string = "Missing closing brace";
		return -1;
	}

	return 0;
}

static int parser_check_item_uidgid(struct objdb_iface_ver0 *objdb,
			unsigned int parent_handle,
			int type,
			const char *name,
			char **error_string)
{
	if (type == PCHECK_ADD_SUBSECTION) {
		if (parent_handle != OBJECT_PARENT_HANDLE) {
			*error_string = "uidgid: Can't add second level subsection";
			return 0;
		}

		if (strcmp (name, "uidgid") != 0) {
			*error_string = "uidgid: Can't add subsection different then uidgid";
			return 0;
		}
	}

	if (type == PCHECK_ADD_ITEM) {
		if (!(strcmp (name, "uid") == 0 || strcmp (name, "gid") == 0)) {
			*error_string = "uidgid: Only uid and gid are allowed items";
			return 0;
		}
	}

	return 1;
}

static int read_uidgid_files_into_objdb(
	struct objdb_iface_ver0 *objdb,
	char **error_string)
{
	FILE *fp;
	const char *dirname;
	DIR *dp;
	struct dirent *dirent;
	char filename[PATH_MAX + NAME_MAX + 1];
	int res = 0;

	dirname = "/etc/ais/uidgid.d";
	dp = opendir (dirname);

	if (dp == NULL)
		return 0;

	while ((dirent = readdir (dp))) {
		if (dirent->d_type == DT_REG) {
			snprintf(filename, sizeof (filename), "%s/%s", dirname, dirent->d_name);

			fp = fopen (filename, "r");
			if (fp == NULL) continue;

			res = parse_section(fp, objdb, OBJECT_PARENT_HANDLE, error_string, parser_check_item_uidgid);

			fclose (fp);

			if (res != 0) {
				goto error_exit;
			}
		}
	}

error_exit:
	closedir(dp);

	return res;
}


/* Read config file and load into objdb */
static int read_config_file_into_objdb(
	struct objdb_iface_ver0 *objdb,
	char **error_string)
{
	FILE *fp;
	char *filename;
	char *error_reason = error_string_response;
	int res;

	filename = getenv("OPENAIS_MAIN_CONFIG_FILE");
	if (!filename)
		filename = "/etc/ais/openais.conf";

	fp = fopen (filename, "r");
	if (fp == 0) {
		sprintf (error_reason, "Can't read file %s reason = (%s)\n",
			 filename, strerror (errno));
		*error_string = error_reason;
		return -1;
	}

	res = parse_section(fp, objdb, OBJECT_PARENT_HANDLE, error_string, NULL);

	fclose(fp);

	if (res == 0) {
		res = read_uidgid_files_into_objdb(objdb, error_string);
	}
	return res;
}

/*
 * Dynamic Loader definition
 */

struct config_iface_ver0 aisparser_iface_ver0 = {
	.config_readconfig        = aisparser_readconfig
};

struct lcr_iface openais_aisparser_ver0[1] = {
	{
		.name				= "aisparser",
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

struct openais_service_handler *aisparser_get_handler_ver0 (void);

struct lcr_comp aisparser_comp_ver0 = {
	.iface_count				= 1,
	.ifaces					= openais_aisparser_ver0
};


__attribute__ ((constructor)) static void aisparser_comp_register (void) {
        lcr_interfaces_set (&openais_aisparser_ver0[0], &aisparser_iface_ver0);
	lcr_component_register (&aisparser_comp_ver0);
}


