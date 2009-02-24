/*
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Copyright (c) 2006-2007 Red Hat, Inc..
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
#include <pthread.h>
#include <assert.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sched.h>
#include <time.h>

#include "../include/saAis.h"
#include "../include/list.h"
#include "../include/queue.h"
#include "../lcr/lcr_ifact.h"
#include "poll.h"
#include "totempg.h"
#include "totemsrp.h"
#include "mempool.h"
#include "mainconfig.h"
#include "totemconfig.h"
#include "main.h"
#include "service.h"
#include "sync.h"
#include "swab.h"
#include "objdb.h"
#include "config.h"
#include "ipc.h"
#include "timer.h"
#include "print.h"
#include "util.h"
#include "version.h"

#define SERVER_BACKLOG 5

static int ais_uid = 0;

static int gid_valid = 0;

static unsigned int service_count = 32;

static pthread_mutex_t serialize_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct totem_logging_configuration totem_logging_configuration;

static char delivery_data[MESSAGE_SIZE_MAX];

static struct objdb_iface_ver0 *objdb = NULL;

SaClmClusterNodeT *(*main_clm_get_by_nodeid) (unsigned int node_id);

static void sigusr2_handler (int num)
{
	int i;

	for (i = 0; ais_service[i]; i++) {
		if (ais_service[i]->exec_dump_fn) {
			ais_service[i]->exec_dump_fn ();
		}
	}
}

static void sigsegv_handler (int num)
{
	signal (SIGSEGV, SIG_DFL);
	log_flush ();
	raise (SIGSEGV);
}

static void sigabrt_handler (int num)
{
	signal (SIGABRT, SIG_DFL);
	log_flush ();
	raise (SIGABRT);
}


#define LOCALHOST_IP inet_addr("127.0.0.1")

totempg_groups_handle openais_group_handle;

struct totempg_group openais_group = {
	.group		= "a",
	.group_len	= 1
};

static void *aisexec_exit (void *arg)
{
	if(objdb) {
		openais_service_unlink_all (objdb);
	}

#ifdef DEBUG_MEMPOOL
	int stats_inuse[MEMPOOL_GROUP_SIZE];
	int stats_avail[MEMPOOL_GROUP_SIZE];
	int stats_memoryused[MEMPOOL_GROUP_SIZE];
	int i;

	mempool_getstats (stats_inuse, stats_avail, stats_memoryused);
	log_printf (LOG_LEVEL_DEBUG, "Memory pools:\n");
	for (i = 0; i < MEMPOOL_GROUP_SIZE; i++) {
	log_printf (LOG_LEVEL_DEBUG, "order %d size %d inuse %d avail %d memory used %d\n",
		i, 1<<i, stats_inuse[i], stats_avail[i], stats_memoryused[i]);
	}
#endif

	log_flush ();
	poll_stop (0);
	totempg_finalize ();
	openais_ipc_exit ();
	openais_exit_error (AIS_DONE_EXIT);

	/* never reached */
	return NULL;
}

pthread_t aisexec_exit_thread;
static void init_shutdown(void *data) 
{
    pthread_create (&aisexec_exit_thread, NULL, aisexec_exit, NULL);
}

static poll_timer_handle shutdown_handle;
static void sigquit_handler (int num)
{
    /* avoid creating threads from within the interrupt context */
    poll_timer_add (aisexec_poll_handle, 500, NULL, init_shutdown, &shutdown_handle);
}

void sigintr_handler (int signum)
{
    poll_timer_add (aisexec_poll_handle, 500, NULL, init_shutdown, &shutdown_handle);
}

static int pool_sizes[] = { 0, 0, 0, 0, 0, 4096, 0, 1, 0, /* 256 */
					1024, 0, 1, 4096, 0, 0, 0, 0, /* 65536 */
					1, 1, 1, 1, 1, 1, 1, 1, 1 };

void serialize_mutex_lock (void)
{
	pthread_mutex_lock (&serialize_mutex);
}

void serialize_mutex_unlock (void)
{
	pthread_mutex_unlock (&serialize_mutex);
}


static void openais_sync_completed (void)
{
}

static int openais_sync_callbacks_retrieve (int sync_id,
	struct sync_callbacks *callbacks)
{
	unsigned int ais_service_index;
	unsigned int ais_services_found = 0;
	
	for (ais_service_index = 0;
		ais_service_index < SERVICE_HANDLER_MAXIMUM_COUNT;
		ais_service_index++) {

		if (ais_service[ais_service_index] != NULL) {
			if (ais_services_found == sync_id) {
				break;
			}
			ais_services_found += 1;
		}
	}
	if (ais_service_index == SERVICE_HANDLER_MAXIMUM_COUNT) {
		memset (callbacks, 0, sizeof (struct sync_callbacks));
		return (-1);
	}
	callbacks->name = ais_service[ais_service_index]->name;
	callbacks->sync_init = ais_service[ais_service_index]->sync_init;
	callbacks->sync_process = ais_service[ais_service_index]->sync_process;
	callbacks->sync_activate = ais_service[ais_service_index]->sync_activate;
	callbacks->sync_abort = ais_service[ais_service_index]->sync_abort;
	return (0);
}

static struct memb_ring_id aisexec_ring_id;

static void confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{
	int i;

	memcpy (&aisexec_ring_id, ring_id, sizeof (struct memb_ring_id));

	/*
	 * Call configuration change for all services
	 */
	for (i = 0; i < service_count; i++) {
		if (ais_service[i] && ais_service[i]->confchg_fn) {
			ais_service[i]->confchg_fn (configuration_type,
				member_list, member_list_entries,
				left_list, left_list_entries,
				joined_list, joined_list_entries, ring_id);
		}
	}
}

static void aisexec_uid_determine (struct main_config *main_config)
{
	struct passwd *passwd;

	passwd = getpwnam(main_config->user);
	if (passwd == 0) {
		log_printf (LOG_LEVEL_ERROR, "ERROR: The '%s' user is not found in /etc/passwd, please read the documentation.\n", main_config->user);
		openais_exit_error (AIS_DONE_UID_DETERMINE);
	}
	ais_uid = passwd->pw_uid;
	endpwent();
}

static void aisexec_gid_determine (struct main_config *main_config)
{
	struct group *group;
	group = getgrnam (main_config->group);
	if (group == 0) {
		log_printf (LOG_LEVEL_ERROR, "ERROR: The '%s' group is not found in /etc/group, please read the documentation.\n", group->gr_name);
		openais_exit_error (AIS_DONE_GID_DETERMINE);
	}
	gid_valid = group->gr_gid;
	endgrent();
}

static void aisexec_priv_drop (void)
{
return;
	setuid (ais_uid);
	setegid (ais_uid);
}

static void aisexec_mempool_init (void)
{
	int res;

	res = mempool_init (pool_sizes);
	if (res == ENOMEM) {
		log_printf (LOG_LEVEL_ERROR, "Couldn't allocate memory pools, not enough memory");
		openais_exit_error (AIS_DONE_MEMPOOL_INIT);
	}
}

static void aisexec_tty_detach (void)
{
	/*
	 * Disconnect from TTY if this is not a debug run
	 */
	switch (fork ()) {
		case -1:
			openais_exit_error (AIS_DONE_FORK);
			break;
		case 0:
			/*
			 * child which is disconnected, run this process
			 */
/* 			setset(); */
			close (0);
			close (1);
			close (2);
			break;
		default:
			exit (0);
			break;
	}
}

static void aisexec_setscheduler (void)
{
#if defined(OPENAIS_BSD) || defined(OPENAIS_LINUX)
	struct sched_param sched_param;
	int res;

	res = sched_get_priority_max (SCHED_RR);
	if (res != -1) {
		sched_param.sched_priority = res;
		res = sched_setscheduler (0, SCHED_RR, &sched_param);
		if (res == -1) {
			log_printf (LOG_LEVEL_WARNING, "Could not set SCHED_RR at priority %d: %s\n",
				sched_param.sched_priority, strerror (errno));
		}
	} else
		log_printf (LOG_LEVEL_WARNING, "Could not get maximum scheduler priority: %s\n", strerror (errno));
#else
	log_printf(LOG_LEVEL_WARNING, "Scheduler priority left to default value (no OS support)\n");
#endif
}

static void aisexec_mlockall (void)
{
#if !defined(OPENAIS_BSD)
	int res;
#endif
	struct rlimit rlimit;

	rlimit.rlim_cur = RLIM_INFINITY;
	rlimit.rlim_max = RLIM_INFINITY;
	setrlimit (RLIMIT_MEMLOCK, &rlimit);

#if defined(OPENAIS_BSD)
	/* under FreeBSD a process with locked page cannot call dlopen
	 * code disabled until FreeBSD bug i386/93396 was solved
	 */
	log_printf (LOG_LEVEL_WARNING, "Could not lock memory of service to avoid page faults\n");
#else
	res = mlockall (MCL_CURRENT | MCL_FUTURE);
	if (res == -1) {
		log_printf (LOG_LEVEL_WARNING, "Could not lock memory of service to avoid page faults: %s\n", strerror (errno));
	};
#endif
}


static void deliver_fn (
	unsigned int nodeid,
	struct iovec *iovec,
	int iov_len,
	int endian_conversion_required)
{
	mar_req_header_t *header;
	int pos = 0;
	int i;
	int service;
	int fn_id;

	/*
	 * Build buffer without iovecs to make processing easier
	 * This is only used for messages which are multicast with iovecs
	 * and self-delivered.  All other mechanisms avoid the copy.
	 */
	if (iov_len > 1) {
		for (i = 0; i < iov_len; i++) {
			memcpy (&delivery_data[pos], iovec[i].iov_base, iovec[i].iov_len);
			pos += iovec[i].iov_len;
			assert (pos < MESSAGE_SIZE_MAX);
		}
		header = (mar_req_header_t *)delivery_data;
	} else {
		header = (mar_req_header_t *)iovec[0].iov_base;
	}
	if (endian_conversion_required) {
		header->id = swab32 (header->id);
		header->size = swab32 (header->size);
	}

//	assert(iovec->iov_len == header->size);

	/*
	 * Call the proper executive handler
	 */
	service = header->id >> 16;
	fn_id = header->id & 0xffff;

	if(service >= SERVICE_HANDLER_MAXIMUM_COUNT
	   || ais_service[service] == NULL) {
	    /* The component is no longer loaded */
	    return;
	}
	
	if (endian_conversion_required) {
		ais_service[service]->exec_service[fn_id].exec_endian_convert_fn
			(header);
	}

	ais_service[service]->exec_service[fn_id].exec_handler_fn
		(header, nodeid);
}

int main (int argc, char **argv)
{
	char *error_string;
	struct main_config main_config;
	struct totem_config totem_config;
	unsigned int objdb_handle;
	unsigned int config_handle;
	unsigned int config_version = 0;
	void *objdb_p;
	struct config_iface_ver0 *config;
	void *config_p;
	char *config_iface;
	int res;
 	int totem_log_service;
 	log_init ("MAIN");
	unsigned int background;
	int ch;

	background = 1;

	while ((ch = getopt (argc, argv, "fp")) != EOF) {
		switch (ch) {
			case 'f':
				background = 0;
				break;
			default:
				fprintf (stderr, "Usage:\n");
				fprintf (stderr, "	-f	: Start application in forground.\n");
				return EXIT_FAILURE;
		}
	}

	if (background) {
		aisexec_tty_detach ();
	}

	log_printf (LOG_LEVEL_NOTICE, "AIS Executive Service RELEASE '%s'\n", RELEASE_VERSION);
	log_printf (LOG_LEVEL_NOTICE, "Copyright (C) 2002-2006 MontaVista Software, Inc and contributors.\n");
	log_printf (LOG_LEVEL_NOTICE, "Copyright (C) 2006 Red Hat, Inc.\n");

	signal (SIGINT, sigintr_handler);
	signal (SIGUSR2, sigusr2_handler);
	signal (SIGSEGV, sigsegv_handler);
	signal (SIGABRT, sigabrt_handler);
	signal (SIGQUIT, sigquit_handler);
	
	openais_timer_init (
		serialize_mutex_lock,
		serialize_mutex_unlock);

	log_printf (LOG_LEVEL_NOTICE, "AIS Executive Service: started and ready to provide service.\n");

	aisexec_poll_handle = poll_create (
		serialize_mutex_lock,
		serialize_mutex_unlock);

	/*
	 * Load the object database interface
	 */
	res = lcr_ifact_reference (
		&objdb_handle,
		"objdb",
		0,
		&objdb_p,
		0);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, "AIS Executive couldn't open configuration object database component.\n");
		openais_exit_error (AIS_DONE_OBJDB);
	}

	objdb = (struct objdb_iface_ver0 *)objdb_p;

	objdb->objdb_init ();

	/*
	 * Bootstrap in the default configuration parser or use
	 * the openais default built in parser if the configuration parser
	 * isn't overridden
	 */
	config_iface = getenv("OPENAIS_DEFAULT_CONFIG_IFACE");
	if (!config_iface) {
		config_iface = "aisparser";
	}

	res = lcr_ifact_reference (
		&config_handle,
		config_iface,
		config_version,
		&config_p,
		0);

	config = (struct config_iface_ver0 *)config_p;
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, "AIS Executive couldn't open configuration component.\n");
		openais_exit_error (AIS_DONE_MAINCONFIGREAD);
	}

	res = config->config_readconfig(objdb, &error_string);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, error_string);
		openais_exit_error (AIS_DONE_MAINCONFIGREAD);
	}

	res = openais_main_config_read (objdb, &error_string, &main_config);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, error_string);
		openais_exit_error (AIS_DONE_MAINCONFIGREAD);
	}

	res = totem_config_read (objdb, &totem_config, &error_string);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, error_string);
		openais_exit_error (AIS_DONE_MAINCONFIGREAD);
	}

	res = totem_config_keyread (objdb, &totem_config, &error_string);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, error_string);
		openais_exit_error (AIS_DONE_MAINCONFIGREAD);
	}

	res = totem_config_validate (&totem_config, &error_string);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, error_string);
		openais_exit_error (AIS_DONE_MAINCONFIGREAD);
	}

	res = log_setup (&error_string, &main_config);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, error_string);
		openais_exit_error (AIS_DONE_LOGSETUP);
	}

	aisexec_uid_determine (&main_config);

	aisexec_gid_determine (&main_config);

	/*
	 * Set round robin realtime scheduling with priority 99
	 * Lock all memory to avoid page faults which may interrupt
	 * application healthchecking
	 */
	aisexec_setscheduler ();

	aisexec_mlockall ();

	totem_config.totem_logging_configuration = totem_logging_configuration;
	totem_log_service = _log_init ("TOTEM");
  	totem_config.totem_logging_configuration.log_level_security = mkpri (LOG_LEVEL_SECURITY, totem_log_service);
	totem_config.totem_logging_configuration.log_level_error = mkpri (LOG_LEVEL_ERROR, totem_log_service);
	totem_config.totem_logging_configuration.log_level_warning = mkpri (LOG_LEVEL_WARNING, totem_log_service);
	totem_config.totem_logging_configuration.log_level_notice = mkpri (LOG_LEVEL_NOTICE, totem_log_service);
	totem_config.totem_logging_configuration.log_level_debug = mkpri (LOG_LEVEL_DEBUG, totem_log_service);
	totem_config.totem_logging_configuration.log_printf = internal_log_printf;

	/*
	 * if totempg_initialize doesn't have root priveleges, it cannot
	 * bind to a specific interface.  This only matters if
	 * there is more then one interface in a system, so
	 * in this case, only a warning is printed
	 */
	/*
	 * Join multicast group and setup delivery
	 *  and configuration change functions
	 */
	totempg_initialize (
		aisexec_poll_handle,
		&totem_config);

	totempg_groups_initialize (
		&openais_group_handle,
		deliver_fn,
		confchg_fn);

	totempg_groups_join (
		openais_group_handle,
		&openais_group,
		1);

	/*
	 * This must occur after totempg is initialized because "this_ip" must be set
	 */
	res = openais_service_defaults_link_and_init (objdb);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, "Could not initialize default services\n");
		openais_exit_error (AIS_DONE_INIT_SERVICES);
	}


	sync_register (openais_sync_callbacks_retrieve, openais_sync_completed,
		totem_config.vsf_type);


	/*
	 * Drop root privleges to user 'ais'
	 * TODO: Don't really need full root capabilities;
	 *       needed capabilities are:
	 * CAP_NET_RAW (bindtodevice)
	 * CAP_SYS_NICE (setscheduler)
	 * CAP_IPC_LOCK (mlockall)
	 */
	aisexec_priv_drop ();

	aisexec_mempool_init ();

	openais_ipc_init (gid_valid);

	/*
	 * Start main processing loop
	 */
	poll_run (aisexec_poll_handle);

	return (0);
}
