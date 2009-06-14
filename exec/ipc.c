/*
 * Copyright (c) 2006-2009 Red Hat, Inc.
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
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
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
#include <sys/wait.h>
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
#if defined(HAVE_GETPEERUCRED)
#include <ucred.h>
#endif

#include <sys/shm.h>
#include <sys/sem.h>

#include "swab.h"
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
#include "tlist.h"
#include "ipc.h"
#include "service.h"
#include "sync.h"
#include "swab.h"
#include "objdb.h"
#include "config.h"
#include "tlist.h"
#define LOG_SERVICE LOG_SERVICE_IPC
#include "print.h"

#include "util.h"

#ifdef OPENAIS_SOLARIS
#define MSG_NOSIGNAL 0
#endif

#define SERVER_BACKLOG 5

#define MSG_SEND_LOCKED		0
#define MSG_SEND_UNLOCKED	1

static unsigned int g_gid_valid = 0;

static void (*ipc_serialize_lock_fn) (void);

static void (*ipc_serialize_unlock_fn) (void);

DECLARE_LIST_INIT (conn_info_list_head);

struct outq_item {
	void *msg;
	size_t mlen;
	struct list_head list;
};

#if defined(_SEM_SEMUN_UNDEFINED)
union semun {
	int val;
	struct semid_ds *buf;
	unsigned short int *array;
	struct seminfo *__buf;
};
#endif

enum conn_state {
	CONN_STATE_THREAD_INACTIVE = 0,
	CONN_STATE_THREAD_ACTIVE = 1,
	CONN_STATE_THREAD_REQUEST_EXIT = 2,
	CONN_STATE_THREAD_DESTROYED = 3,
	CONN_STATE_LIB_EXIT_CALLED = 4,
	CONN_STATE_DISCONNECT_INACTIVE = 5
};

struct conn_info {
	int fd;
	pthread_t thread;
	pthread_attr_t thread_attr;
	unsigned int service;
	enum conn_state state;
	int notify_flow_control_enabled;
	int refcount;
	key_t shmkey;
	key_t semkey;
	int shmid;
	int semid;
	unsigned int pending_semops;
	pthread_mutex_t mutex;
	struct shared_memory *mem;
	struct list_head outq_head;
	void *private_data;
	int (*lib_exit_fn) (void *conn);
	struct list_head list;
	char setup_msg[sizeof (mar_req_setup_t)];
	unsigned int setup_bytes_read;
};

static int shared_mem_dispatch_bytes_left (struct conn_info *conn_info);

static void outq_flush (struct conn_info *conn_info);

static int priv_change (struct conn_info *conn_info);

static void ipc_disconnect (struct conn_info *conn_info);

static int ipc_thread_active (void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	int retval = 0;

	pthread_mutex_lock (&conn_info->mutex);
	if (conn_info->state == CONN_STATE_THREAD_ACTIVE) {
		retval = 1;
	}
	pthread_mutex_unlock (&conn_info->mutex);
	return (retval);
}

static int ipc_thread_exiting (void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	int retval = 1;

	pthread_mutex_lock (&conn_info->mutex);
	if (conn_info->state == CONN_STATE_THREAD_INACTIVE) {
		retval = 0;
	} else
	if (conn_info->state == CONN_STATE_THREAD_ACTIVE) {
		retval = 0;
	}
	pthread_mutex_unlock (&conn_info->mutex);
	return (retval);
}

/*
 * returns 0 if should be called again, -1 if finished
 */
static inline int conn_info_destroy (struct conn_info *conn_info)
{
	unsigned int res;
	void *retval;

	list_del (&conn_info->list);
	list_init (&conn_info->list);

	if (conn_info->state == CONN_STATE_THREAD_REQUEST_EXIT) {
		res = pthread_join (conn_info->thread, &retval);
		conn_info->state = CONN_STATE_THREAD_DESTROYED;
		return (0);
	}

	if (conn_info->state == CONN_STATE_THREAD_INACTIVE ||
		conn_info->state == CONN_STATE_DISCONNECT_INACTIVE) {
		list_del (&conn_info->list);
		close (conn_info->fd);
		free (conn_info);
		return (-1);
	}

	if (conn_info->state == CONN_STATE_THREAD_ACTIVE) {
		pthread_kill (conn_info->thread, SIGUSR1);
		return (0);
	}

	ipc_serialize_lock_fn();
	/*
	 * Retry library exit function if busy
	 */
	if (conn_info->state == CONN_STATE_THREAD_DESTROYED) {
		res = ais_service[conn_info->service]->lib_exit_fn (conn_info);
		if (res == -1) {
			ipc_serialize_unlock_fn();
			return (0);
		} else {
			conn_info->state = CONN_STATE_LIB_EXIT_CALLED;
		}
	}

	pthread_mutex_lock (&conn_info->mutex);
	if (conn_info->refcount > 0) {
		pthread_mutex_unlock (&conn_info->mutex);
		ipc_serialize_unlock_fn();
		return (0);
	}
	list_del (&conn_info->list);
	pthread_mutex_unlock (&conn_info->mutex);

	/*
	 * Destroy shared memory segment and semaphore
	 */
	shmdt (conn_info->mem);
	res = shmctl (conn_info->shmid, IPC_RMID, NULL);
	semctl (conn_info->semid, 0, IPC_RMID);

	/*
	 * Free allocated data needed to retry exiting library IPC connection
	 */
	if (conn_info->private_data) {
		free (conn_info->private_data);
	}
	close (conn_info->fd);
	free (conn_info);
	ipc_serialize_unlock_fn();
	return (-1);
}

struct res_overlay {
	mar_res_header_t header __attribute__((aligned(8)));
	char buf[4096];
};

static void
record_proc_state(const char *file, const char *function, int line, const char *assert_condition)
{
	int rc = 0;
	int pid = 0;
	int status = 0;

	pid=fork();
	switch(pid) {
		case -1:
			break;

		case 0:	/* Child */
			abort();
			break;

		default: /* Parent */
			log_printf(LOG_LEVEL_ERROR, 
				   "%s: Forked child %d to record non-fatal error at %s:%d : %s",
				   function, pid, file, line, assert_condition);
			do {
			    rc = waitpid(pid, &status, 0);
			    
			} while(rc < 0 && errno == EINTR);
			break;

	}
}

static void *pthread_ipc_consumer (void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	struct sembuf sop;
	int res;
	mar_req_header_t *header;
	struct res_overlay res_overlay;
	struct iovec send_ok_joined_iovec;
	int send_ok = 0;
	int reserved_msgs = 0;
	int flow_control = 0;

	for (;;) {
		sop.sem_num = 0;
		sop.sem_op = -1;
		sop.sem_flg = 0;
retry_semop:
		if (ipc_thread_active (conn_info) == 0) {
			openais_conn_refcount_dec (conn_info);
			pthread_exit (0);
		}
		res = semop (conn_info->semid, &sop, 1);
		if ((res == -1) && (errno == EINTR || errno == EAGAIN)) {
			goto retry_semop;
		} else
		if ((res == -1) && (errno == EINVAL || errno == EIDRM)) {
			openais_conn_refcount_dec (conn_info);
			pthread_exit (0);
		}

		openais_conn_refcount_inc (conn_info);

		header = (mar_req_header_t *)conn_info->mem->req_buffer;

		send_ok_joined_iovec.iov_base = (char *)header;
		send_ok_joined_iovec.iov_len = header->size;
		reserved_msgs = totempg_groups_joined_reserve (
			openais_group_handle,
			&send_ok_joined_iovec, 1);

		/* Sanity check service and header.id */
		if(conn_info->service < 0
		   || conn_info->service >= SERVICE_HANDLER_MAXIMUM_COUNT
		   || ais_service[conn_info->service] == NULL) {
		    log_printf (LOG_LEVEL_ERROR, "Invalid service requested: %d\n", conn_info->service);
		    record_proc_state(__FILE__, __FUNCTION__, __LINE__, "Invalid service");
		    continue;
		    
		} else if(header->id < 0
		    || header->id >= ais_service[conn_info->service]->lib_service_count) {
		    log_printf (LOG_LEVEL_ERROR, "Invalid subtype (%d) requested for service: %d\n",
				header->id, conn_info->service);
		    record_proc_state(__FILE__, __FUNCTION__, __LINE__, "Invalid subtype");
		    continue;
		}
		   
		send_ok = 1;
		flow_control = ais_service[conn_info->service]->lib_service[header->id].flow_control;
		if(send_ok && sync_primary_designated() != 1) {
		    send_ok = 0;

		} else if(send_ok
		   && flow_control == OPENAIS_FLOW_CONTROL_REQUIRED
		   && (reserved_msgs == 0 || sync_in_process() != 0)) {
		    send_ok = 0;
		}

		if (send_ok) {
 			ipc_serialize_lock_fn();
			ais_service[conn_info->service]->lib_service[header->id].lib_handler_fn (conn_info, header);
 			ipc_serialize_unlock_fn();
		} else {
			/*
			 * Overload, tell library to retry
			 */
			res_overlay.header.size =
					ais_service[conn_info->service]->lib_service[header->id].response_size;
			res_overlay.header.id =
				ais_service[conn_info->service]->lib_service[header->id].response_id;
			res_overlay.header.error = SA_AIS_ERR_TRY_AGAIN;
			openais_response_send (conn_info, &res_overlay, 
				res_overlay.header.size);
		}

		totempg_groups_joined_release (reserved_msgs);
		openais_conn_refcount_dec (conn);
	}
	pthread_exit (0);
}

static int
req_setup_send (
	struct conn_info *conn_info,
	int error)
{
	mar_res_setup_t res_setup;
	res_setup.error = error;
	unsigned int res;

retry_send:
	res = send (conn_info->fd, &res_setup, sizeof (mar_res_setup_t), MSG_WAITALL);
	if (res == -1 && errno == EINTR) {
		goto retry_send;
	} else
	if (res == -1 && errno == EAGAIN) {
		goto retry_send;
	}
	return (0);
}

static int
req_setup_recv (
	struct conn_info *conn_info)
{
	int res;
	struct msghdr msg_recv;
	struct iovec iov_recv;
#ifdef OPENAIS_LINUX
	struct cmsghdr *cmsg;
	char cmsg_cred[CMSG_SPACE (sizeof (struct ucred))];
	struct ucred *cred;
	int off = 0;
	int on = 1;
#endif

	msg_recv.msg_iov = &iov_recv;
	msg_recv.msg_iovlen = 1;
	msg_recv.msg_name = 0;
	msg_recv.msg_namelen = 0;
#ifdef OPENAIS_LINUX
	msg_recv.msg_control = (void *)cmsg_cred;
	msg_recv.msg_controllen = sizeof (cmsg_cred);
#endif

#ifdef PORTABILITY_WORK_TODO
#ifdef OPENAIS_SOLARIS
	msg_recv.msg_flags = 0;
	uid_t euid;
	gid_t egid;
		                
	euid = -1;
	egid = -1;
	if (getpeereid(conn_info->fd, &euid, &egid) != -1 &&
	    (euid == 0 || egid == g_gid_valid)) {
		if (conn_info->state == CONN_IO_STATE_INITIALIZING) {
			log_printf (LOG_LEVEL_SECURITY, "Connection not authenticated because gid is %d, expecting %d\n", egid, g_gid_valid);
			return (-1);
		}
	}
	msg_recv.msg_accrights = 0;
	msg_recv.msg_accrightslen = 0;
#else /* OPENAIS_SOLARIS */

#ifdef HAVE_GETPEERUCRED
	ucred_t *uc;
	uid_t euid = -1;
	gid_t egid = -1;

	if (getpeerucred (conn_info->fd, &uc) == 0) {
		euid = ucred_geteuid (uc);
		egid = ucred_getegid (uc);
		if ((euid == 0) || (egid == g_gid_valid) || ais_security_valid (euid, egid)) {
			conn_info->authenticated = 1;
		}
		ucred_free(uc);
	}
	if (conn_info->authenticated == 0) {
		log_printf (LOG_LEVEL_SECURITY, "Connection not authenticated because gid is %d, expecting %d\n", (int)egid, g_gid_valid);
 	}
#else /* HAVE_GETPEERUCRED */
 	log_printf (LOG_LEVEL_SECURITY, "Connection not authenticated "
 		"because platform does not support "
 		"authentication with sockets, continuing "
 		"with a fake authentication\n");
#endif /* HAVE_GETPEERUCRED */
#endif /* OPENAIS_SOLARIS */

#endif

#ifdef OPENAIS_LINUX
	iov_recv.iov_base = &conn_info->setup_msg[conn_info->setup_bytes_read];
	iov_recv.iov_len = sizeof (mar_req_setup_t) - conn_info->setup_bytes_read;
	setsockopt(conn_info->fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof (on));
#endif

retry_recv:
	res = recvmsg (conn_info->fd, &msg_recv, MSG_NOSIGNAL);
	if (res == -1 && errno == EINTR) {
		goto retry_recv;
	} else
	if (res == -1 && errno != EAGAIN) {
		return (0);
	} else
	if (res == 0) {
#if defined(OPENAIS_SOLARIS) || defined(OPENAIS_BSD) || defined(OPENAIS_DARWIN)
		/* On many OS poll never return POLLHUP or POLLERR.
		 * EOF is detected when recvmsg return 0.
		 */
		ipc_disconnect (conn_info);
#endif
		return (-1);
	}
	conn_info->setup_bytes_read += res;

#ifdef OPENAIS_LINUX

	cmsg = CMSG_FIRSTHDR (&msg_recv);
	assert (cmsg);
	cred = (struct ucred *)CMSG_DATA (cmsg);
	if (cred) {
		if (cred->uid == 0 || cred->gid == g_gid_valid || ais_security_valid (cred->uid, cred->gid)) {
		} else {
			ipc_disconnect (conn_info);
			log_printf (LOG_LEVEL_SECURITY,
				"Connection with uid %d and gid %d not authenticated\n",
				cred->uid, cred->gid);
			return (-1);
		}
	}
#endif
	if (conn_info->setup_bytes_read == sizeof (mar_req_setup_t)) {
#ifdef OPENAIS_LINUX
		setsockopt(conn_info->fd, SOL_SOCKET, SO_PASSCRED,
			&off, sizeof (off));
#endif
		return (1);
	}
	return (0);
}

static int poll_handler_connection (
	poll_handle handle,
	int fd,
	int revent,
	void *data)
{
	mar_req_setup_t *req_setup;
	struct conn_info *conn_info = (struct conn_info *)data;
	int res;
	char buf;


	if (ipc_thread_exiting (conn_info)) {
		return conn_info_destroy (conn_info);
	}

	/*
	 * If an error occurs, request exit
	 */
	if (revent & (POLLERR|POLLHUP)) {
		ipc_disconnect (conn_info);
		return (0);
	}

	/*
	 * Read the header and process it
	 */
	if (conn_info->service == SOCKET_SERVICE_INIT && (revent & POLLIN)) {
		/*
		 * Receive in a nonblocking fashion the request
		 * IF security invalid, send TRY_AGAIN, otherwise
		 * send OK
		 */
		res = req_setup_recv (conn_info);
		if (res == -1) {
			req_setup_send (conn_info, SA_AIS_ERR_SECURITY);
		}
		if (res != 1) {
			return (0);
		}
		req_setup_send (conn_info, SA_AIS_OK);

		pthread_mutex_init (&conn_info->mutex, NULL);
		req_setup = (mar_req_setup_t *)conn_info->setup_msg;
		conn_info->shmkey = req_setup->shmkey;
		conn_info->semkey = req_setup->semkey;
		conn_info->service = req_setup->service;
		conn_info->refcount = 0;
		conn_info->notify_flow_control_enabled = 0;
		conn_info->setup_bytes_read = 0;

		conn_info->shmid = shmget (conn_info->shmkey,
			sizeof (struct shared_memory), 0600);
		conn_info->mem = shmat (conn_info->shmid, NULL, 0);
		conn_info->semid = semget (conn_info->semkey, 3, 0600);
		conn_info->pending_semops = 0;

		/*
		 * ipc thread is the only reference at startup
		 */
		conn_info->refcount = 1; 
		conn_info->state = CONN_STATE_THREAD_ACTIVE;

		conn_info->private_data = malloc (ais_service[conn_info->service]->private_data_size);
		memset (conn_info->private_data, 0,
			ais_service[conn_info->service]->private_data_size);
		ais_service[conn_info->service]->lib_init_fn (conn_info);


		pthread_attr_init (&conn_info->thread_attr);
		/*
		* IA64 needs more stack space then other arches
		*/
		#if defined(__ia64__)
		pthread_attr_setstacksize (&conn_info->thread_attr, 400000);
		#else
		pthread_attr_setstacksize (&conn_info->thread_attr, 200000);
		#endif

		pthread_attr_setdetachstate (&conn_info->thread_attr, PTHREAD_CREATE_JOINABLE);
		res = pthread_create (&conn_info->thread,
			&conn_info->thread_attr,
			pthread_ipc_consumer,
			conn_info);

		/*
		 * Security check - disallow multiple configurations of
		 * the ipc connection
		 */
		if (conn_info->service == SOCKET_SERVICE_INIT) {
			conn_info->service = -1;
		}
	} else
	if (revent & POLLIN) {
		openais_conn_refcount_inc (conn_info);
		res = recv (fd, &buf, 1, MSG_NOSIGNAL);
		if (res == 1) {
			switch (buf) {
			case MESSAGE_REQ_OUTQ_FLUSH:
				outq_flush (conn_info);
				break;
			case MESSAGE_REQ_CHANGE_EUID:
				if (priv_change (conn_info) == -1) {
					ipc_disconnect (conn_info);
				}
				break;
			default:
				res = 0;
				break;
			}
		}
#if defined(OPENAIS_SOLARIS) || defined(OPENAIS_BSD) || defined(OPENAIS_DARWIN)
		/* On many OS poll never return POLLHUP or POLLERR.
		 * EOF is detected when recvmsg return 0.
		 */
		if (res == 0) {
			ipc_disconnect (conn_info);
			openais_conn_refcount_dec (conn_info);
			return (0);
		}
#endif
		openais_conn_refcount_dec (conn_info);
	}

	openais_conn_refcount_inc (conn_info);
	pthread_mutex_lock (&conn_info->mutex);
	if ((conn_info->state == CONN_STATE_THREAD_ACTIVE) && (revent & POLLOUT)) {
		buf = !list_empty (&conn_info->outq_head);
		for (; conn_info->pending_semops;) {
			res = send (conn_info->fd, &buf, 1, MSG_NOSIGNAL);
			if (res == 1) {
				conn_info->pending_semops--;
			} else {
				break;
			}
		}
		if (conn_info->notify_flow_control_enabled) {
			buf = 2;
			res = send (conn_info->fd, &buf, 1, MSG_NOSIGNAL);
			if (res == 1) {
				conn_info->notify_flow_control_enabled = 0;
			}
		}
		if (conn_info->notify_flow_control_enabled == 0 &&
			conn_info->pending_semops == 0) {

			poll_dispatch_modify (aisexec_poll_handle,
				conn_info->fd, POLLIN|POLLNVAL,
				poll_handler_connection);
		}
	}
	pthread_mutex_unlock (&conn_info->mutex);
	openais_conn_refcount_dec (conn_info);

	return (0);
}

static void ipc_disconnect (struct conn_info *conn_info)
{
	if (conn_info->state == CONN_STATE_THREAD_INACTIVE) {
		conn_info->state = CONN_STATE_DISCONNECT_INACTIVE;
		return;
	}
	if (conn_info->state != CONN_STATE_THREAD_ACTIVE) {
		return;
	}
	pthread_mutex_lock (&conn_info->mutex);
	conn_info->state = CONN_STATE_THREAD_REQUEST_EXIT;
	pthread_mutex_unlock (&conn_info->mutex);

	pthread_kill (conn_info->thread, SIGUSR1);
}

static int conn_info_create (int fd)
{
	struct conn_info *conn_info;

	conn_info = malloc (sizeof (struct conn_info));
	if (conn_info == NULL) {
		return (-1);
	}
	memset (conn_info, 0, sizeof (struct conn_info));

	conn_info->fd = fd;
	conn_info->service = SOCKET_SERVICE_INIT;
	conn_info->state = CONN_STATE_THREAD_INACTIVE;
	list_init (&conn_info->outq_head);
	list_init (&conn_info->list);
	list_add (&conn_info->list, &conn_info_list_head);

        poll_dispatch_add (aisexec_poll_handle, fd, POLLIN|POLLNVAL,
		conn_info, poll_handler_connection);
	return (0);
}

#if defined(OPENAIS_LINUX) || defined(OPENAIS_SOLARIS)
/* SUN_LEN is broken for abstract namespace
 */
#define AIS_SUN_LEN(a) sizeof(*(a))
#else
#define AIS_SUN_LEN(a) SUN_LEN(a)
#endif

#if defined(OPENAIS_LINUX)
char *socketname = "libais.socket";
#else
char *socketname = "/var/run/libais.socket";
#endif

static int poll_handler_accept (
	poll_handle handle,
	int fd,
	int revent,
	void *data)
{
	socklen_t addrlen;
	struct sockaddr_un un_addr;
	int new_fd;
#ifdef OPENAIS_LINUX
	int on = 1;
#endif
	int res;

	addrlen = sizeof (struct sockaddr_un);

retry_accept:
	new_fd = accept (fd, (struct sockaddr *)&un_addr, &addrlen);
	if (new_fd == -1 && errno == EINTR) {
		goto retry_accept;
	}

	if (new_fd == -1) {
		log_printf (LOG_LEVEL_ERROR, "ERROR: Could not accept Library connection: %s\n", strerror (errno));
		return (0); /* This is an error, but -1 would indicate disconnect from poll loop */
	}

	totemip_nosigpipe(new_fd);
	res = fcntl (new_fd, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, "Could not set non-blocking operation on library connection: %s\n", strerror (errno));
		close (new_fd);
		return (0); /* This is an error, but -1 would indicate disconnect from poll loop */
	}

	/*
	 * Valid accept
	 */

	/*
	 * Request credentials of sender provided by kernel
	 */
#ifdef OPENAIS_LINUX
	setsockopt(new_fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof (on));
#endif

	log_printf (LOG_LEVEL_DEBUG, "connection received from libais client %d.\n", new_fd);

	res = conn_info_create (new_fd);
	if (res != 0) {
		close (new_fd);
	}

	return (0);
}
/*
 * Exported functions
 */

int message_source_is_local(mar_message_source_t *source)
{
	int ret = 0;

	assert (source != NULL);
	if (source->nodeid == totempg_my_nodeid_get ()) {
		ret = 1;
	}
	return ret;
}

void message_source_set (
	mar_message_source_t *source,
	void *conn)
{
	assert ((source != NULL) && (conn != NULL));
	memset (source, 0, sizeof (mar_message_source_t));
	source->nodeid = totempg_my_nodeid_get ();
	source->conn = conn;
}

void openais_ipc_init (
	unsigned int gid_valid,
	void (*serialize_lock_fn) (void),
	void (*serialize_unlock_fn) (void))
{
	int libais_server_fd;
	struct sockaddr_un un_addr;
	int res;

	ipc_serialize_lock_fn = serialize_lock_fn;

	ipc_serialize_unlock_fn = serialize_unlock_fn;

	/*
	 * Create socket for libais clients, name socket, listen for connections
	 */
	libais_server_fd = socket (PF_UNIX, SOCK_STREAM, 0);
	if (libais_server_fd == -1) {
		log_printf (LOG_LEVEL_ERROR ,"Cannot create libais client connections socket.\n");
		openais_exit_error (AIS_DONE_LIBAIS_SOCKET);
	};

	totemip_nosigpipe (libais_server_fd);
	res = fcntl (libais_server_fd, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, "Could not set non-blocking operation on server socket: %s\n", strerror (errno));
		openais_exit_error (AIS_DONE_LIBAIS_SOCKET);
	}

#if !defined(OPENAIS_LINUX)
	unlink(socketname);
#endif
	memset (&un_addr, 0, sizeof (struct sockaddr_un));
	un_addr.sun_family = AF_UNIX;
#if defined(OPENAIS_BSD) || defined(OPENAIS_DARWIN)
	un_addr.sun_len = sizeof(struct sockaddr_un);
#endif
#if defined(OPENAIS_LINUX)
	strcpy (un_addr.sun_path + 1, socketname);
#else
	strcpy (un_addr.sun_path, socketname);
#endif

	res = bind (libais_server_fd, (struct sockaddr *)&un_addr, AIS_SUN_LEN(&un_addr));
	if (res) {
		log_printf (LOG_LEVEL_ERROR, "ERROR: Could not bind AF_UNIX: %s.\n", strerror (errno));
		openais_exit_error (AIS_DONE_LIBAIS_BIND);
	}
	listen (libais_server_fd, SERVER_BACKLOG);

        /*
         * Setup libais connection dispatch routine
         */
        poll_dispatch_add (aisexec_poll_handle, libais_server_fd,
                POLLIN|POLLNVAL, 0, poll_handler_accept);

	g_gid_valid = gid_valid;
}

void openais_ipc_exit (void)
{
	struct list_head *list;
	struct conn_info *conn_info;

	for (list = conn_info_list_head.next; list != &conn_info_list_head;
		list = list->next) {

		conn_info = list_entry (list, struct conn_info, list);

		shmdt (conn_info->mem);
		shmctl (conn_info->shmid, IPC_RMID, NULL);
		semctl (conn_info->semid, 0, IPC_RMID);
	
		pthread_kill (conn_info->thread, SIGUSR1);
	}
}

/*
 * Get the conn info private data
 */
void *openais_conn_private_data_get (void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;

	return (conn_info->private_data);
}

int openais_response_send (void *conn, void *msg, int mlen)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	struct sembuf sop;
	int res;

	memcpy (conn_info->mem->res_buffer, msg, mlen);
	sop.sem_num = 1;
	sop.sem_op = 1;
	sop.sem_flg = 0;

retry_semop:
	res = semop (conn_info->semid, &sop, 1);
	if ((res == -1) && (errno == EINTR || errno == EAGAIN)) {
		goto retry_semop;
	} else
	if ((res == -1) && (errno == EINVAL || errno == EIDRM)) {
		return (0);
	}
	return (0);
}

int openais_response_iov_send (void *conn, struct iovec *iov, int iov_len)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	struct sembuf sop;
	int res;
	int write_idx = 0;
	int i;

	for (i = 0; i < iov_len; i++) {
		memcpy (&conn_info->mem->res_buffer[write_idx], iov[i].iov_base, iov[i].iov_len);
		write_idx += iov[i].iov_len;
	}

	sop.sem_num = 1;
	sop.sem_op = 1;
	sop.sem_flg = 0;

retry_semop:
	res = semop (conn_info->semid, &sop, 1);
	if ((res == -1) && (errno == EINTR || errno == EAGAIN)) {
		goto retry_semop;
	} else
	if ((res == -1) && (errno == EINVAL || errno == EIDRM)) {
		return (0);
	}
	return (0);
}

static int shared_mem_dispatch_bytes_left (struct conn_info *conn_info)
{
	unsigned int read;
	unsigned int write;
	unsigned int bytes_left;

	read = conn_info->mem->read;
	write = conn_info->mem->write;

	if (read <= write) {
		bytes_left = DISPATCH_SIZE - write + read;
	} else {
		bytes_left = read - write;
	}
	return (bytes_left);
}

int memcpy_dwrap (struct conn_info *conn_info, void *msg, int len)
{
	char *dest_char = (char *)conn_info->mem->dispatch_buffer;
	char *src_char = (char *)msg;
	unsigned int first_write;
	unsigned int second_write;

	first_write = len;
	second_write = 0;
	if (len + conn_info->mem->write >= DISPATCH_SIZE) {
		first_write = DISPATCH_SIZE - conn_info->mem->write;
		second_write = len - first_write;
	}
	memcpy (&dest_char[conn_info->mem->write], src_char, first_write);
	if (second_write) {
		memcpy (dest_char, &src_char[first_write], second_write);
	}
	conn_info->mem->write = (conn_info->mem->write + len) % DISPATCH_SIZE;
	return (0);
}

void msg_send (void *conn, struct iovec *iov, int iov_len, int locked)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	struct sembuf sop;
	int res;
	int i;
	char buf;

	for (i = 0; i < iov_len; i++) {
		memcpy_dwrap (conn_info, iov[i].iov_base, iov[i].iov_len);
	}

	buf = !list_empty (&conn_info->outq_head);
	res = send (conn_info->fd, &buf, 1, MSG_NOSIGNAL);
	if (res == -1 && errno == EAGAIN) {
		if (locked == 0) {
			pthread_mutex_lock (&conn_info->mutex);
		}
		conn_info->pending_semops += 1;
		if (locked == 0) {
			pthread_mutex_unlock (&conn_info->mutex);
		}
        	poll_dispatch_modify (aisexec_poll_handle, conn_info->fd,
			POLLIN|POLLOUT|POLLNVAL, poll_handler_connection);
	} else
	if (res == -1) {
		ipc_disconnect (conn_info);
	}
	sop.sem_num = 2;
	sop.sem_op = 1;
	sop.sem_flg = 0;

retry_semop:
	res = semop (conn_info->semid, &sop, 1);
	if ((res == -1) && (errno == EINTR || errno == EAGAIN)) {
		goto retry_semop;
	} else
	if ((res == -1) && (errno == EINVAL || errno == EIDRM)) {
		return;
	}
}

static void outq_flush (struct conn_info *conn_info) {
	struct list_head *list, *list_next;
	struct outq_item *outq_item;
	unsigned int bytes_left;
	struct iovec iov;
	char buf;
	int res;

	pthread_mutex_lock (&conn_info->mutex);
	if (list_empty (&conn_info->outq_head)) {
		buf = 3;
		res = send (conn_info->fd, &buf, 1, MSG_NOSIGNAL);
		pthread_mutex_unlock (&conn_info->mutex);
		return;
	}
	for (list = conn_info->outq_head.next;
		list != &conn_info->outq_head; list = list_next) {

		list_next = list->next;
		outq_item = list_entry (list, struct outq_item, list);
		bytes_left = shared_mem_dispatch_bytes_left (conn_info);
		if (bytes_left > outq_item->mlen) {
			iov.iov_base = outq_item->msg;
			iov.iov_len = outq_item->mlen;
			msg_send (conn_info, &iov, 1, MSG_SEND_UNLOCKED);
			list_del (list);
			free (iov.iov_base);
			free (outq_item);
		} else {
			break;
		}
	}
	pthread_mutex_unlock (&conn_info->mutex);
}

static int priv_change (struct conn_info *conn_info)
{
	mar_req_priv_change req_priv_change;
	unsigned int res;
	union semun semun;
	struct semid_ds ipc_set;
	int i;

retry_recv:
	res = recv (conn_info->fd, &req_priv_change,
		sizeof (mar_req_priv_change),
		MSG_NOSIGNAL);
	if (res == -1 && errno == EINTR) {
		goto retry_recv;
	}
	if (res == -1 && errno == EAGAIN) {
		goto retry_recv;
	}
	if (res == -1 && errno != EAGAIN) {
		return (-1);
	}
#if defined(OPENAIS_SOLARIS) || defined(OPENAIS_BSD) || defined(OPENAIS_DARWIN)
	/* Error on socket, EOF is detected when recv return 0
	 */
	if (res == 0) {
		return (-1);
	}
#endif

	ipc_set.sem_perm.uid = req_priv_change.euid;
	ipc_set.sem_perm.gid = req_priv_change.egid;
	ipc_set.sem_perm.mode = 0600;

	semun.buf = &ipc_set;

	for (i = 0; i < 3; i++) {
		res = semctl (conn_info->semid, 0, IPC_SET, semun);
		if (res == -1) {
			return (-1);
		}
	}
	return (0);
}

static void msg_send_or_queue (void *conn, struct iovec *iov, int iov_len)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	unsigned int bytes_left;
	unsigned int bytes_msg = 0;
	int i;
	struct outq_item *outq_item;
	char *write_buf = 0;

	/*
	 * Exit transmission if the connection is dead
	 */
	if (ipc_thread_active (conn) == 0) {
		return;
	}

	bytes_left = shared_mem_dispatch_bytes_left (conn_info);
	for (i = 0; i < iov_len; i++) {
		bytes_msg += iov[i].iov_len;
	}
	if (bytes_left < bytes_msg || list_empty (&conn_info->outq_head) == 0) {
		outq_item = malloc (sizeof (struct outq_item));
		if (outq_item == NULL) {
			ipc_disconnect (conn);
			return;
		}
		outq_item->msg = malloc (bytes_msg);
		if (outq_item->msg == 0) {
			free (outq_item);
			ipc_disconnect (conn);
			return;
		}

		write_buf = outq_item->msg;
		for (i = 0; i < iov_len; i++) {
			memcpy (write_buf, iov[i].iov_base, iov[i].iov_len);
			write_buf += iov[i].iov_len;
		}
		outq_item->mlen = bytes_msg;
		list_init (&outq_item->list);
		pthread_mutex_lock (&conn_info->mutex);
		if (list_empty (&conn_info->outq_head)) {
			conn_info->notify_flow_control_enabled = 1;
			poll_dispatch_modify (aisexec_poll_handle,
				conn_info->fd, POLLOUT|POLLIN|POLLNVAL,
				poll_handler_connection);
		}
		list_add_tail (&outq_item->list, &conn_info->outq_head);
		pthread_mutex_unlock (&conn_info->mutex);
		return;
	}
	msg_send (conn, iov, iov_len, MSG_SEND_LOCKED);
}

void openais_conn_refcount_inc (void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;

	pthread_mutex_lock (&conn_info->mutex);
	conn_info->refcount++;
	pthread_mutex_unlock (&conn_info->mutex);
}

void openais_conn_refcount_dec (void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;

	pthread_mutex_lock (&conn_info->mutex);
	conn_info->refcount--;
	pthread_mutex_unlock (&conn_info->mutex);
}

int openais_dispatch_send (void *conn, void *msg, int mlen)
{
	struct iovec iov;

	iov.iov_base = msg;
	iov.iov_len = mlen;

	msg_send_or_queue (conn, &iov, 1);
	return (0);
}

int openais_dispatch_iov_send (void *conn, struct iovec *iov, int iov_len)
{
	msg_send_or_queue (conn, iov, iov_len);
	return (0);
}
