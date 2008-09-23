/*
 * Copyright (c) 2006-2007 Red Hat, Inc.
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
#include "flow.h"
#include "tlist.h"
#include "ipc.h"
#include "flow.h"
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

/*
 * When there are this many entries left in a queue, turn on flow control
 */
#define FLOW_CONTROL_ENTRIES_ENABLE 400

/*
 * When there are this many entries in a queue, turn off flow control
 */
#define FLOW_CONTROL_ENTRIES_DISABLE 64


static unsigned int g_gid_valid = 0;

static totempg_groups_handle ipc_handle;

static pthread_mutex_t conn_io_list_mutex = PTHREAD_MUTEX_INITIALIZER;

DECLARE_LIST_INIT (conn_io_list_head);

static void (*ipc_serialize_lock_fn) (void);

static void (*ipc_serialize_unlock_fn) (void);

struct outq_item {
	void *msg;
	size_t mlen;
};

enum conn_io_state {
	CONN_IO_STATE_INITIALIZING,
	CONN_IO_STATE_AUTHENTICATED,
	CONN_IO_STATE_INIT_FAILED
};

enum conn_info_state {
	CONN_INFO_STATE_INITIALIZING,
	CONN_INFO_STATE_ACTIVE,
	CONN_INFO_STATE_DISCONNECT_REQUESTED,
	CONN_INFO_STATE_DISCONNECTED
};

struct conn_info;

struct conn_io {
	int fd;				/* File descriptor  */
	unsigned int events;		/* events polled for by file descriptor */
	pthread_t thread;		/* thread identifier */
	pthread_attr_t thread_attr;	/* thread attribute */
	char *inb;			/* Input buffer for non-blocking reads */
	int inb_nextheader;		/* Next message header starts here */
	int inb_start;			/* Start location of input buffer */
	int inb_inuse;			/* Bytes currently stored in input buffer */
	struct queue outq;		/* Circular queue for outgoing requests */
	int byte_start;			/* Byte to start sending from in head of queue */
	unsigned int fcc;		/* flow control local count */
	enum conn_io_state state;	/* state of this conn_io connection */
	struct conn_info *conn_info;	/* connection information combining multiple conn_io structs */
	unsigned int refcnt;		/* reference count for conn_io data structure */
	pthread_mutex_t mutex;
	unsigned int service;
	struct list_head list;
};


struct conn_info {
	enum conn_info_state state;			/* State of this connection */
	enum service_types service;		/* Type of service so dispatch knows how to route message */
	void *private_data;			/* library connection private data */
	unsigned int flow_control_handle;	/* flow control identifier */
	unsigned int flow_control_enabled;	/* flow control enabled bit */
	enum openais_flow_control flow_control;	/* Does this service use IPC flow control */
	pthread_mutex_t flow_control_mutex;
	unsigned int flow_control_local_count;		/* flow control local count */
        int (*lib_exit_fn) (void *conn);
	pthread_mutex_t mutex;
	struct conn_io *conn_io_response;
	struct conn_io *conn_io_dispatch;
	unsigned int refcnt;
};

static void *prioritized_poll_thread (void *conn_io_in);
static int conn_io_outq_flush (struct conn_io *conn_io);
static void conn_io_deliver (struct conn_io *conn_io);
//static void ipc_flow_control (struct conn_info *conn_info);
static inline void conn_info_destroy (struct conn_info *conn_info);
static void conn_io_destroy (struct conn_io *conn_io);
static int conn_io_send (struct conn_io *conn_io, void *msg, int mlen); 
static inline struct conn_info *conn_info_create (void);
static int conn_io_found (struct conn_io *conn_io_to_match);

static int response_init_send (struct conn_io *conn_io, void *message);
static int dispatch_init_send (struct conn_io *conn_io, void *message);

 /*
  * IPC Initializers
  */

static int conn_io_refcnt_value (struct conn_io *conn_io)
{
	unsigned int refcnt;

	pthread_mutex_lock (&conn_io->mutex);
	refcnt = conn_io->refcnt;
	pthread_mutex_unlock (&conn_io->mutex);

	return (refcnt);
}

static void conn_io_refcnt_inc (struct conn_io *conn_io)
{
	pthread_mutex_lock (&conn_io->mutex);
	conn_io->refcnt += 1;
	pthread_mutex_unlock (&conn_io->mutex);
}

static int conn_io_refcnt_dec (struct conn_io *conn_io)
{
	unsigned int refcnt;

	pthread_mutex_lock (&conn_io->mutex);
	conn_io->refcnt -= 1;
	refcnt = conn_io->refcnt;
	pthread_mutex_unlock (&conn_io->mutex);

	return (refcnt);
}

static void conn_info_refcnt_inc (struct conn_info *conn_info)
{
	/*
	 * Connection not fully initialized yet
	 */
	if (conn_info == NULL) {
		return;
	}
	pthread_mutex_lock (&conn_info->mutex);
	conn_info->refcnt += 1;
	pthread_mutex_unlock (&conn_info->mutex);
}

static void conn_info_refcnt_dec (struct conn_info *conn_info)
{
	int refcnt;

	/*
	 * Connection not fully initialized yet
	 */
	if (conn_info == NULL) {
		return;
	}
	pthread_mutex_lock (&conn_info->mutex);
	conn_info->refcnt -= 1;
	refcnt = conn_info->refcnt;
	assert (refcnt >= 0);
	pthread_mutex_unlock (&conn_info->mutex);

	if (refcnt == 0) {
		conn_info_destroy (conn_info);
	}
}

void openais_conn_info_refcnt_dec (void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;

	conn_info_refcnt_dec (conn_info);
}

void openais_conn_info_refcnt_inc (void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;

	conn_info_refcnt_inc (conn_info);
}
	
static int (*ais_init_service[]) (struct conn_io *conn_io, void *message) = {
	response_init_send,
	dispatch_init_send
};

static void disconnect_request (struct conn_info *conn_info)
{
unsigned int res;
	/*
	 * connection not fully active yet
	 */
	if (conn_info == NULL) {
		return;
	}
	/*
	 * We only want to decrement the reference count on these two
	 * conn_io contexts one time
	 */
	if (conn_info->state != CONN_INFO_STATE_ACTIVE) {
		return;
	}
	res = conn_io_refcnt_dec (conn_info->conn_io_response);
	res = conn_io_refcnt_dec (conn_info->conn_io_dispatch);
	conn_info->state = CONN_INFO_STATE_DISCONNECT_REQUESTED;
}

static int response_init_send (
	struct conn_io *conn_io,
	void *message)
{
	SaAisErrorT error = SA_AIS_ERR_ACCESS;
	uintptr_t cinfo = (uintptr_t)conn_io;
	mar_req_lib_response_init_t *req_lib_response_init = (mar_req_lib_response_init_t *)message;
	mar_res_lib_response_init_t res_lib_response_init;

	if (conn_io->state == CONN_IO_STATE_AUTHENTICATED) {
		error = SA_AIS_OK;
		conn_io->service = req_lib_response_init->resdis_header.service;
	}
	res_lib_response_init.header.size = sizeof (mar_res_lib_response_init_t);
	res_lib_response_init.header.id = MESSAGE_RES_INIT;
	res_lib_response_init.header.error = error;
	res_lib_response_init.conn_info = (mar_uint64_t)cinfo;

	conn_io_send (
		conn_io,
		&res_lib_response_init,
		sizeof (res_lib_response_init));

	if (error == SA_AIS_ERR_ACCESS) {
		conn_io_destroy (conn_io);
		return (-1);
	}

	return (0);
}

/*
 * This is called iwth ipc_serialize_lock_fn() called
 * Therefore there are no races with the destruction of the conn_io
 * data structure
 */
static int dispatch_init_send (
	struct conn_io *conn_io,
	void *message)
{
	SaAisErrorT error = SA_AIS_ERR_ACCESS;
	uintptr_t cinfo;
	mar_req_lib_dispatch_init_t *req_lib_dispatch_init = (mar_req_lib_dispatch_init_t *)message;
	mar_res_lib_dispatch_init_t res_lib_dispatch_init;
	struct conn_io *msg_conn_io;
	struct conn_info *conn_info;
	unsigned int service;

	service = req_lib_dispatch_init->resdis_header.service;
	cinfo = (uintptr_t)req_lib_dispatch_init->conn_info;
	msg_conn_io = (struct conn_io *)cinfo;

	/*
	 * The response IPC connection has disconnected already for
	 * some reason and is no longer referenceable in the system
	 */
	if (conn_io->state == CONN_IO_STATE_AUTHENTICATED) {
		/*
		 * If the response conn_io isn't found, it disconnected.
		 * Hence, a full connection cannot be made and this connection
		 * should be aborted by the poll thread
		 */
		if (conn_io_found (msg_conn_io) == 0) {
			error = SA_AIS_ERR_TRY_AGAIN;
			conn_io->state = CONN_IO_STATE_INIT_FAILED;
		} else
		/*
		 * If no service is found for the requested library service,
		 * the proper service handler isn't loaded and this connection
		 * should be aborted by the poll thread
		 */
		if (ais_service[service] == NULL) {
			error = SA_AIS_ERR_NOT_SUPPORTED;
			conn_io->state = CONN_IO_STATE_INIT_FAILED;
		} else {
			error = SA_AIS_OK;
		}

		/*
		 * The response and dispatch conn_io structures are available.
		 * Attempt to allocate the appropriate memory for the private
		 * data area
		 */
		if (error == SA_AIS_OK) {
			int private_data_size;

			conn_info = conn_info_create ();
			private_data_size = ais_service[service]->private_data_size;
			if (private_data_size) {
				conn_info->private_data = malloc (private_data_size);

				/*
				 * No private data could be allocated so
				 * request the poll thread to abort
				 */
				if (conn_info->private_data == NULL) {
					conn_io->state = CONN_IO_STATE_INIT_FAILED;
					error = SA_AIS_ERR_NO_MEMORY;
				} else {
					memset (conn_info->private_data, 0, private_data_size);
				}
			} else {
				conn_info->private_data = NULL;
			}
		}
	}

	res_lib_dispatch_init.header.size = sizeof (mar_res_lib_dispatch_init_t);
	res_lib_dispatch_init.header.id = MESSAGE_RES_INIT;
	res_lib_dispatch_init.header.error = error;

	if (error != SA_AIS_OK) {
		conn_io_send (
			conn_io,
			&res_lib_dispatch_init,
			sizeof (res_lib_dispatch_init));

		return (-1);
	}

	/*
	 * connect both dispatch and response conn_ios into the conn_info
	 * data structure
	 */
	conn_info->state = CONN_INFO_STATE_ACTIVE;
	conn_info->lib_exit_fn = ais_service[service]->lib_exit_fn;
	conn_info->conn_io_response = msg_conn_io;
	conn_info->conn_io_response->conn_info = conn_info;
	conn_info->conn_io_dispatch = conn_io;
	conn_info->service = service;
	conn_io->service = service;
	conn_io->conn_info = conn_info;
	ais_service[conn_info->service]->lib_init_fn (conn_info);

	conn_info->flow_control = ais_service[conn_info->service]->flow_control;
	if (ais_service[conn_info->service]->flow_control == OPENAIS_FLOW_CONTROL_REQUIRED) {
		openais_flow_control_ipc_init (
			&conn_info->flow_control_handle,
			conn_info->service);

	}

	/*
	 * Tell the library the IPC connections are configured
	 */
	conn_io_send (
		conn_io,
		&res_lib_dispatch_init,
		sizeof (res_lib_dispatch_init));
	return (0);
}

/*
 * Create a connection data structure
 */
static inline struct conn_info *conn_info_create (void)
{
	struct conn_info *conn_info;

	conn_info = malloc (sizeof (struct conn_info));
	if (conn_info == 0) {
		return (NULL);
	}

	memset (conn_info, 0, sizeof (struct conn_info));

	conn_info->refcnt = 2;
	pthread_mutex_init (&conn_info->mutex, NULL);
	conn_info->state = CONN_INFO_STATE_INITIALIZING;

	return (conn_info);
}

static inline void conn_info_destroy (struct conn_info *conn_info)
{
	if (conn_info->private_data) {
		free (conn_info->private_data);
	}
	pthread_mutex_destroy (&conn_info->mutex);
	free (conn_info);
}

static int conn_io_create (int fd)
{
	int res;
	struct conn_io *conn_io;

	conn_io = malloc (sizeof (struct conn_io));
	if (conn_io == NULL) {
		return (-1);
	}
	memset (conn_io, 0, sizeof (struct conn_io));

	res = queue_init (&conn_io->outq, SIZEQUEUE,
		sizeof (struct outq_item));
	if (res != 0) {
		return (-1);
	}

	conn_io->inb = malloc (sizeof (char) * SIZEINB);
	if (conn_io->inb == NULL) {
		queue_free (&conn_io->outq);
		return (-1);
	}

	conn_io->fd = fd;
	conn_io->events = POLLIN|POLLNVAL;
	conn_io->refcnt = 1;
	conn_io->service = SOCKET_SERVICE_INIT;
	conn_io->state = CONN_IO_STATE_INITIALIZING;

	pthread_attr_init (&conn_io->thread_attr);

	pthread_mutex_init (&conn_io->mutex, NULL);

	/*
	 * IA64 needs more stack space then other arches
	 */
#if defined(__ia64__)
	pthread_attr_setstacksize (&conn_io->thread_attr, 400000);
#else
	pthread_attr_setstacksize (&conn_io->thread_attr, 200000);
#endif

	pthread_attr_setdetachstate (&conn_io->thread_attr, PTHREAD_CREATE_DETACHED);

	res = pthread_create (&conn_io->thread, &conn_io->thread_attr,
		prioritized_poll_thread, conn_io);

	list_init (&conn_io->list);

	pthread_mutex_lock (&conn_io_list_mutex);
	list_add (&conn_io->list, &conn_io_list_head);
	pthread_mutex_unlock (&conn_io_list_mutex);
	return (res);
}

static void conn_io_destroy (struct conn_io *conn_io)
{
	struct outq_item *outq_item;

	/*
	 * Free the outq queued items
	 */
	while (!queue_is_empty (&conn_io->outq)) {
		outq_item = queue_item_get (&conn_io->outq);
		free (outq_item->msg);
		queue_item_remove (&conn_io->outq);
	}

	queue_free (&conn_io->outq);
	free (conn_io->inb);
	close (conn_io->fd);
	pthread_mutex_lock (&conn_io_list_mutex);
	list_del (&conn_io->list);
	pthread_mutex_unlock (&conn_io_list_mutex);
	
	pthread_attr_destroy (&conn_io->thread_attr);
	pthread_mutex_destroy (&conn_io->mutex);
	free (conn_io);
}

static int conn_io_found (struct conn_io *conn_io_to_match)
{
	struct list_head *list;
	struct conn_io *conn_io;

	for (list = conn_io_list_head.next; list != &conn_io_list_head;
		list = list->next) {
	
		conn_io = list_entry (list, struct conn_io, list);
		if (conn_io == conn_io_to_match) {
			return (1);
		}
	}

	return (0);
}

/*
 * This thread runs in a specific thread priority mode to handle
 * I/O requests from or to the library
 */
static void *prioritized_poll_thread (void *conn_io_in)
{
	struct conn_io *conn_io = (struct conn_io *)conn_io_in;
	struct conn_info *conn_info = NULL;
	struct pollfd ufd;
	int fds;
	struct sched_param sched_param;
	int res;

	sched_param.sched_priority = 99;
	res = pthread_setschedparam (conn_io->thread, SCHED_RR, &sched_param);

	ufd.fd = conn_io->fd;
	for (;;) {
retry_poll:
		conn_info = conn_io->conn_info;
		conn_io_refcnt_inc (conn_io);
		conn_info_refcnt_inc (conn_info);

		ufd.events = conn_io->events;
		ufd.revents = 0;
		fds = poll (&ufd, 1, -1);
		if (fds == -1) {
			conn_io_refcnt_dec (conn_io);
			conn_info_refcnt_dec (conn_info);
			goto retry_poll;
		}

		ipc_serialize_lock_fn ();

		if (fds == 1 && ufd.revents) {
			if (ufd.revents & (POLLERR|POLLHUP)) {
				disconnect_request (conn_info);
				conn_info_refcnt_dec (conn_info);
				conn_io_refcnt_dec (conn_io);
				/*
				 * If conn_info not set, wait for it to be set
				 * else break out of for loop
				 */
				if (conn_info == NULL) {
					ipc_serialize_unlock_fn ();
					continue;
				} else {
					ipc_serialize_unlock_fn ();
					break;
				}
			}

			if (conn_info && conn_info->state == CONN_INFO_STATE_DISCONNECT_REQUESTED) {
				conn_info_refcnt_dec (conn_info);
				conn_io_refcnt_dec (conn_io);
				ipc_serialize_unlock_fn ();
				break;
			}
			
			if (ufd.revents & POLLOUT) {
				conn_io_outq_flush (conn_io);
			}

			if ((ufd.revents & POLLIN) == POLLIN) {
				conn_io_deliver (conn_io);
			}

			/*
			 * IPC initializiation failed because response fd
			 * disconnected before it was linked to dispatch fd
			 */
			if (conn_io->state == CONN_IO_STATE_INIT_FAILED) {
				conn_io_destroy (conn_io);
				conn_info_refcnt_dec (conn_info);
				ipc_serialize_unlock_fn ();
				pthread_exit (0);
			}
			/*
			 * IPC initializiation failed because response fd
			 * disconnected before it was linked to dispatch fd
			 */
			if (conn_io->state == CONN_IO_STATE_INIT_FAILED) {
				break;
			}

//			ipc_flow_control (conn_info);

		}

		ipc_serialize_unlock_fn ();

		conn_io_refcnt_dec (conn_io);
		conn_info_refcnt_dec (conn_info);
	}

	ipc_serialize_lock_fn ();

	/*
	 * IPC initializiation failed because response fd
	 * disconnected before it was linked to dispatch fd
	 */
	if (conn_io->conn_info == NULL || conn_io->state == CONN_IO_STATE_INIT_FAILED) {
		conn_io_destroy (conn_io);
		conn_info_refcnt_dec (conn_info);
		ipc_serialize_unlock_fn ();
		pthread_exit (0);
	}

	conn_info = conn_io->conn_info;

	/*
	 * This is the response conn_io
	 */
	if (conn_info->conn_io_response == conn_io) {
		for (;;) {
			if (conn_io_refcnt_value (conn_io) == 0) {
				conn_io->conn_info = NULL;
				conn_io_destroy (conn_io);
				conn_info_refcnt_dec (conn_info);
				ipc_serialize_unlock_fn ();
				pthread_exit (0);
			}
		usleep (1000);
	printf ("sleep 1\n");
		}
	} /* response conn_io */

	/*
	 * This is the dispatch conn_io
	 */
	if (conn_io->conn_info->conn_io_dispatch == conn_io) {
		ipc_serialize_unlock_fn ();
		for (;;) {
			ipc_serialize_lock_fn ();
			if (conn_io_refcnt_value (conn_io) == 0) {
				res = 0; // TODO
				/*
				 * Execute the library exit function
				 */
				if (conn_io->conn_info->lib_exit_fn) {
					res = conn_io->conn_info->lib_exit_fn (conn_info);
				}
				if (res == 0) {
					if (conn_io->conn_info->flow_control_enabled == 1) {
//						openais_flow_control_disable (
//							conn_info->flow_control_handle);
					}
					conn_io->conn_info = NULL;
					conn_io_destroy (conn_io);
					conn_info_refcnt_dec (conn_info);
					ipc_serialize_unlock_fn ();
					pthread_exit (0);
				}
			} /* refcnt == 0 */
			ipc_serialize_unlock_fn ();
			usleep (1000);
		} /* for (;;) */
	} /* dispatch conn_io */

	/*
	 * This code never reached
	 */
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


#ifdef COMPILOE_OUT
static void ipc_flow_control (struct conn_info *conn_info)
{
	unsigned int entries_used;
	unsigned int entries_usedhw;
	unsigned int flow_control_local_count;
	unsigned int fcc;

	/*
	 * Determine FCC variable and printing variables
	 */
	entries_used = queue_used (&conn_info->outq);
	if (conn_info->conn_info_partner &&
		queue_used (&conn_info->conn_info_partner->outq) > entries_used) {
		entries_used = queue_used (&conn_info->conn_info_partner->outq);
	}
	entries_usedhw = queue_usedhw (&conn_info->outq);
	if (conn_info->conn_info_partner &&
		queue_usedhw (&conn_info->conn_info_partner->outq) > entries_used) {
		entries_usedhw = queue_usedhw (&conn_info->conn_info_partner->outq);
	}
	flow_control_local_count = conn_info->flow_control_local_count;
	if (conn_info->conn_info_partner &&
		conn_info->conn_info_partner->flow_control_local_count > flow_control_local_count) {
		flow_control_local_count = conn_info->conn_info_partner->flow_control_local_count;
	}

	fcc = entries_used;
	if (flow_control_local_count > fcc) {
		fcc = flow_control_local_count;
	}
	/*
	 * IPC group-wide flow control
	 */
	if (conn_info->flow_control == OPENAIS_FLOW_CONTROL_REQUIRED) {
		if (conn_info->flow_control_enabled == 0 &&
			((fcc + FLOW_CONTROL_ENTRIES_ENABLE) > SIZEQUEUE)) {

			log_printf (LOG_LEVEL_NOTICE, "Enabling flow control [%d/%d] - [%d].\n",
				entries_usedhw, SIZEQUEUE,
				flow_control_local_count);
			openais_flow_control_enable (conn_info->flow_control_handle);
			conn_info->flow_control_enabled = 1;
			conn_info->conn_info_partner->flow_control_enabled = 1;
		}
		if (conn_info->flow_control_enabled == 1 &&

			fcc <= FLOW_CONTROL_ENTRIES_DISABLE) {

			log_printf (LOG_LEVEL_NOTICE, "Disabling flow control [%d/%d] - [%d].\n",
				entries_usedhw, SIZEQUEUE,
				flow_control_local_count);
			openais_flow_control_disable (conn_info->flow_control_handle);
			conn_info->flow_control_enabled = 0;
			conn_info->conn_info_partner->flow_control_enabled = 0;
		}
	}
}
#endif

static int conn_io_outq_flush (struct conn_io *conn_io) {
	struct queue *outq;
	int res = 0;
	struct outq_item *queue_item;
	struct msghdr msg_send;
	struct iovec iov_send;
	char *msg_addr;

	outq = &conn_io->outq;

	msg_send.msg_iov = &iov_send;
	msg_send.msg_name = 0;
	msg_send.msg_namelen = 0;
	msg_send.msg_iovlen = 1;
#ifndef OPENAIS_SOLARIS
	msg_send.msg_control = 0;
	msg_send.msg_controllen = 0;
	msg_send.msg_flags = 0;
#else
	msg_send.msg_accrights = 0;
	msg_send.msg_accrightslen = 0;
#endif

	pthread_mutex_lock (&conn_io->mutex);
	while (!queue_is_empty (outq)) {
		queue_item = queue_item_get (outq);
		msg_addr = (char *)queue_item->msg;
		msg_addr = &msg_addr[conn_io->byte_start];

		iov_send.iov_base = msg_addr;
		iov_send.iov_len = queue_item->mlen - conn_io->byte_start;

retry_sendmsg:
		res = sendmsg (conn_io->fd, &msg_send, MSG_NOSIGNAL);
		if (res == -1 && errno == EINTR) {
			goto retry_sendmsg;
		}
		if (res == -1 && errno == EAGAIN) {
			pthread_mutex_unlock (&conn_io->mutex);
			return (0);
		}
		if (res == -1 && errno == EPIPE) {
			disconnect_request (conn_io->conn_info);
			pthread_mutex_unlock (&conn_io->mutex);
			return (0);
		}
		if (res == -1) {
			assert (0); /* some other unhandled error here */
		}
		if (res + conn_io->byte_start != queue_item->mlen) {
			conn_io->byte_start += res;

			pthread_mutex_unlock (&conn_io->mutex);
			return (0);
		}

		/*
		 * Message sent, try sending another message
		 */
		queue_item_remove (outq);
		conn_io->byte_start = 0;
		free (queue_item->msg);
	} /* while queue not empty */

	if (queue_is_empty (outq)) {
		conn_io->events = POLLIN|POLLNVAL;
	}

	pthread_mutex_unlock (&conn_io->mutex);
	return (0);
}



struct res_overlay {
	mar_res_header_t header __attribute((aligned(8)));
	char buf[4096];
};

static void conn_io_deliver (struct conn_io *conn_io)
{
	int res;
	mar_req_header_t *header;
	int service;
	struct msghdr msg_recv;
	struct iovec iov_recv;
#ifdef OPENAIS_LINUX
	struct cmsghdr *cmsg;
	char cmsg_cred[CMSG_SPACE (sizeof (struct ucred))];
	struct ucred *cred;
	int on = 0;
#endif
	int send_ok = 0;
	int send_ok_joined = 0;
	struct iovec send_ok_joined_iovec;
	struct res_overlay res_overlay;

	msg_recv.msg_iov = &iov_recv;
	msg_recv.msg_iovlen = 1;
	msg_recv.msg_name = 0;
	msg_recv.msg_namelen = 0;
#ifndef OPENAIS_SOLARIS
	msg_recv.msg_flags = 0;

	if (conn_io->state == CONN_IO_STATE_AUTHENTICATED) {
		msg_recv.msg_control = 0;
		msg_recv.msg_controllen = 0;
	} else {
#ifdef OPENAIS_LINUX
		msg_recv.msg_control = (void *)cmsg_cred;
		msg_recv.msg_controllen = sizeof (cmsg_cred);
#else
		{
			uid_t euid;
			gid_t egid;
			                
			euid = -1; egid = -1;
			if (getpeereid(conn_io->fd, &euid, &egid) != -1 &&
			    (euid == 0 || egid == g_gid_valid)) {
					conn_io->state = CONN_IO_STATE_AUTHENTICATED;
			}
			if (conn_io->state == CONN_IO_STATE_INITIALIZING) {
				log_printf (LOG_LEVEL_SECURITY, "Connection not authenticated because gid is %d, expecting %d\n", egid, g_gid_valid);
			}
		}
#endif
	}

#else /* OPENAIS_SOLARIS */
	msg_recv.msg_accrights = 0;
	msg_recv.msg_accrightslen = 0;


	if (! conn_info->authenticated) {
#ifdef HAVE_GETPEERUCRED
		ucred_t *uc;
		uid_t euid = -1;
		gid_t egid = -1;

		if (getpeerucred (conn_info->fd, &uc) == 0) {
			euid = ucred_geteuid (uc);
			egid = ucred_getegid (uc);
			if ((euid == 0) || (egid == g_gid_valid)) {
				conn_info->authenticated = 1;
			}
			ucred_free(uc);
		}
		if (conn_info->authenticated == 0) {
			log_printf (LOG_LEVEL_SECURITY, "Connection not authenticated because gid is %d, expecting %d\n", (int)egid, g_gid_valid);
 		}
 #else
 		log_printf (LOG_LEVEL_SECURITY, "Connection not authenticated "
 			"because platform does not support "
 			"authentication with sockets, continuing "
 			"with a fake authentication\n");
 		conn_info->authenticated = 1;
 #endif
 	}
 #endif
	iov_recv.iov_base = &conn_io->inb[conn_io->inb_start];
	iov_recv.iov_len = (SIZEINB) - conn_io->inb_start;
	if (conn_io->inb_inuse == SIZEINB) {
		return;
	}

retry_recv:
	res = recvmsg (conn_io->fd, &msg_recv, MSG_NOSIGNAL);
	if (res == -1 && errno == EINTR) {
		goto retry_recv;
	} else
	if (res == -1 && errno != EAGAIN) {
		return;
	} else
	if (res == 0) {
#if defined(OPENAIS_SOLARIS) || defined(OPENAIS_BSD) || defined(OPENAIS_DARWIN)
		/* On many OS poll never return POLLHUP or POLLERR.
		 * EOF is detected when recvmsg return 0.
		 */
		disconnect_request (conn_io);
#endif
		return;
	}

	/*
	 * Authenticate if this connection has not been authenticated
	 */
#ifdef OPENAIS_LINUX
	if (conn_io->state == CONN_IO_STATE_INITIALIZING) {
		cmsg = CMSG_FIRSTHDR (&msg_recv);
		assert (cmsg);
		cred = (struct ucred *)CMSG_DATA (cmsg);
		if (cred) {
			if (cred->uid == 0 || cred->gid == g_gid_valid) {
				setsockopt(conn_io->fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof (on));
				conn_io->state = CONN_IO_STATE_AUTHENTICATED;
			}
		}
		if (conn_io->state == CONN_IO_STATE_INITIALIZING) {
			log_printf (LOG_LEVEL_SECURITY, "Connection not authenticated because gid is %d, expecting %d\n", cred->gid, g_gid_valid);
		}
	}
#endif
	/*
	 * Dispatch all messages received in recvmsg that can be dispatched
	 * sizeof (mar_req_header_t) needed at minimum to do any processing
	 */
	conn_io->inb_inuse += res;
	conn_io->inb_start += res;

	while (conn_io->inb_inuse >= sizeof (mar_req_header_t) && res != -1) {
		header = (mar_req_header_t *)&conn_io->inb[conn_io->inb_start - conn_io->inb_inuse];

		if (header->size > conn_io->inb_inuse) {
			break;
		}
		service = conn_io->service;

		/*
		 * If this service is in init phase, initialize service
		 * else handle message using service service
		 */
		if (conn_io->service == SOCKET_SERVICE_INIT) {
			res = ais_init_service[header->id] (conn_io, header);
		} else  {
			/*
			 * Not an init service, but a standard service
			 */
			if (header->id < 0 || header->id > ais_service[service]->lib_service_count) {
				log_printf (LOG_LEVEL_SECURITY, "Invalid header id is %d min 0 max %d\n",
				header->id, ais_service[service]->lib_service_count);
				return ;
			}

			/*
			 * If flow control is required of the library handle, determine that
			 * openais is not in synchronization and that totempg has room available
			 * to queue a message, otherwise tell the library we are busy and to
			 * try again later
			 */
			send_ok_joined_iovec.iov_base = (char *)header;
			send_ok_joined_iovec.iov_len = header->size;
			send_ok_joined = totempg_groups_send_ok_joined (openais_group_handle,
				&send_ok_joined_iovec, 1);

			send_ok =
				(sync_primary_designated() == 1) && (
				(ais_service[service]->lib_service[header->id].flow_control == OPENAIS_FLOW_CONTROL_NOT_REQUIRED) ||
				((ais_service[service]->lib_service[header->id].flow_control == OPENAIS_FLOW_CONTROL_REQUIRED) &&
				(send_ok_joined) &&
				(sync_in_process() == 0)));

			if (send_ok) {
				ais_service[service]->lib_service[header->id].lib_handler_fn(conn_io->conn_info, header);
			} else {

				/*
				 * Overload, tell library to retry
				 */
				res_overlay.header.size =
					ais_service[service]->lib_service[header->id].response_size;
				res_overlay.header.id =
					ais_service[service]->lib_service[header->id].response_id;
				res_overlay.header.error = SA_AIS_ERR_TRY_AGAIN;
				conn_io_send (
					conn_io,
					&res_overlay,
					res_overlay.header.size);
			}
		}
		conn_io->inb_inuse -= header->size;
	} /* while */

	if (conn_io->inb_inuse == 0) {
		conn_io->inb_start = 0;
	} else
// BUG	if (connections[conn_io->fd].inb_start + connections[conn_io->fd].inb_inuse >= SIZEINB) {
	if (conn_io->inb_start >= SIZEINB) {
		/*
		 * If in buffer is full, move it back to start
		 */
		memmove (conn_io->inb,
			&conn_io->inb[conn_io->inb_start - conn_io->inb_inuse],
			sizeof (char) * conn_io->inb_inuse);
		conn_io->inb_start = conn_io->inb_inuse;
	}

	return;
}

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

	res = conn_io_create (new_fd);
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

static void ipc_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{
}

void openais_ipc_init (
	void (*serialize_lock_fn) (void),
	void (*serialize_unlock_fn) (void),
	unsigned int gid_valid)
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
                POLLIN, 0, poll_handler_accept);

	g_gid_valid = gid_valid;

	/*
	 * Reset internal state of flow control when
	 * configuration change occurs
	 */
	res = totempg_groups_initialize (
		&ipc_handle,
		NULL,
		ipc_confchg_fn);
}


/*
 * Get the conn info private data
 */
void *openais_conn_private_data_get (void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;

	return (conn_info->private_data);
}

static int conn_io_send (
	struct conn_io *conn_io,
	void *msg,
	int mlen)
{
	char *cmsg;
	int res = 0;
	int queue_empty;
	struct outq_item *queue_item;
	struct outq_item queue_item_out;
	struct msghdr msg_send;
	struct iovec iov_send;
	char *msg_addr;

	if (conn_io == NULL) {
		assert (0);
	}

//	ipc_flow_control (conn_info);

	msg_send.msg_iov = &iov_send;
	msg_send.msg_name = 0;
	msg_send.msg_namelen = 0;
	msg_send.msg_iovlen = 1;
#ifndef OPENAIS_SOLARIS
	msg_send.msg_control = 0;
	msg_send.msg_controllen = 0;
	msg_send.msg_flags = 0;
#else
	msg_send.msg_accrights = 0;
	msg_send.msg_accrightslen = 0;
#endif

	pthread_mutex_lock (&conn_io->mutex);
	if (queue_is_full (&conn_io->outq)) {
		/*
		 * Start a disconnect if we have not already started one
		 * and report that the outgoing queue is full
		 */
		log_printf (LOG_LEVEL_ERROR, "Library queue is full, disconnecting library connection.\n");
		disconnect_request (conn_io->conn_info);
		pthread_mutex_unlock (&conn_io->mutex);
		return (-1);
	}
	while (!queue_is_empty (&conn_io->outq)) {
		queue_item = queue_item_get (&conn_io->outq);
		msg_addr = (char *)queue_item->msg;
		msg_addr = &msg_addr[conn_io->byte_start];

		iov_send.iov_base = msg_addr;
		iov_send.iov_len = queue_item->mlen - conn_io->byte_start;

retry_sendmsg:
		res = sendmsg (conn_io->fd, &msg_send, MSG_NOSIGNAL);
		if (res == -1 && errno == EINTR) {
			goto retry_sendmsg;
		}
		if (res == -1 && errno == EAGAIN) {
			break; /* outgoing kernel queue full */
		}
		if (res == -1 && errno == EPIPE) {
			disconnect_request (conn_io->conn_info);
			pthread_mutex_unlock (&conn_io->mutex);
			return (0);
		}
		if (res == -1) {
//			assert (0);
			break; /* some other error, stop trying to send message */
		}
		if (res + conn_io->byte_start != queue_item->mlen) {
			conn_io->byte_start += res;
			break;
		}

		/*
		 * Message sent, try sending another message
		 */
		queue_item_remove (&conn_io->outq);
		conn_io->byte_start = 0;
		free (queue_item->msg);
	} /* while queue not empty */

	res = -1;

	queue_empty = queue_is_empty (&conn_io->outq);
	/*
	 * Send request message
	 */
	if (queue_empty) {

		iov_send.iov_base = msg;
		iov_send.iov_len = mlen;
retry_sendmsg_two:
		res = sendmsg (conn_io->fd, &msg_send, MSG_NOSIGNAL);
		if (res == -1 && errno == EINTR) {
			goto retry_sendmsg_two;
		}
		if (res == -1 && errno == EAGAIN) {
			conn_io->byte_start = 0;
		}
		if (res != -1) {
			if (res != mlen) {
				conn_io->byte_start += res;
				res = -1;
			} else {
				conn_io->byte_start = 0;
			}
		}
	}

	/*
	 * If res == -1 , errrno == EAGAIN which means kernel queue full
	 */
	if (res == -1)  {
		cmsg = malloc (mlen);
		if (cmsg == 0) {
			log_printf (LOG_LEVEL_ERROR, "Library queue couldn't allocate a message, disconnecting library connection.\n");
			disconnect_request (conn_io->conn_info);
			pthread_mutex_unlock (&conn_io->mutex);
			return (-1);
		}
		queue_item_out.msg = cmsg;
		queue_item_out.mlen = mlen;
		memcpy (cmsg, msg, mlen);
		queue_item_add (&conn_io->outq, &queue_item_out);

		/*
		 * Send a pthread_kill to interrupt the blocked poll syscall
		 * and start a new poll operation in the thread if
		 * POLLOUT is not already set
		 */
		if (conn_io->events != (POLLIN|POLLOUT|POLLNVAL)) {
			conn_io->events = POLLIN|POLLOUT|POLLNVAL;
			pthread_kill (conn_io->thread, SIGUSR1);
		}
	}
	pthread_mutex_unlock (&conn_io->mutex);
	return (0);
}

void openais_ipc_flow_control_create (
	void *conn,
	unsigned int service,
	char *id,
	int id_len,
	void (*flow_control_state_set_fn) (void *conn, enum openais_flow_control_state),
	void *context)
{
	struct conn_info *conn_info = (struct conn_info *)conn;

	openais_flow_control_create (
		conn_info->flow_control_handle,
		service,
		id,
		id_len,
		flow_control_state_set_fn,
		context);	
}

void openais_ipc_flow_control_destroy (
	void *conn,
	unsigned int service,
	unsigned char *id,
	int id_len)
{
	struct conn_info *conn_info = (struct conn_info *)conn;

	openais_flow_control_destroy (
		conn_info->flow_control_handle,
		service,
		id,
		id_len);
}

void openais_ipc_flow_control_local_increment (
        void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;

	pthread_mutex_lock (&conn_info->flow_control_mutex);

	conn_info->flow_control_local_count++;

	pthread_mutex_unlock (&conn_info->flow_control_mutex);
}

void openais_ipc_flow_control_local_decrement (
        void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;

	pthread_mutex_lock (&conn_info->flow_control_mutex);

	conn_info->flow_control_local_count--;

	pthread_mutex_unlock (&conn_info->flow_control_mutex);
}


int openais_response_send (void *conn, void *msg, int mlen)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	
	return (conn_io_send (conn_info->conn_io_response, msg, mlen));
}

int openais_dispatch_send (void *conn, void *msg, int mlen)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	
	return (conn_io_send (conn_info->conn_io_dispatch, msg, mlen));
}
