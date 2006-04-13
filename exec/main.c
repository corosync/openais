/*
 * vi: set autoindent tabstop=4 shiftwidth=4 :
 *
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
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
#define LOG_SERVICE LOG_SERVICE_MAIN
#include "print.h"

#include "util.h"

#define SERVER_BACKLOG 5

int ais_uid = 0;
int gid_valid = 0;

static unsigned int service_count = 32;

struct outq_item {
	void *msg;
	size_t mlen;
};

enum conn_state {
	CONN_STATE_ACTIVE,
	CONN_STATE_DISCONNECTING,
	CONN_STATE_DISCONNECTING_DELAYED
};

struct conn_info {
	int fd;			/* File descriptor  */
	enum conn_state state;	/* State of this connection */
	char *inb;		/* Input buffer for non-blocking reads */
	int inb_nextheader;	/* Next message header starts here */
	int inb_start;		/* Start location of input buffer */
	int inb_inuse;		/* Bytes currently stored in input buffer */
	struct queue outq;	/* Circular queue for outgoing requests */
	int byte_start;		/* Byte to start sending from in head of queue */
	enum service_types service;/* Type of service so dispatch knows how to route message */
	int authenticated;	/* Is this connection authenticated? */
	void *private_data;	/* library connection private data */
	struct conn_info *conn_info_partner;	/* partner connection dispatch<->response */
	int should_exit_fn;	/* Should call the exit function when closing this ipc */
};
SaClmClusterNodeT *(*main_clm_get_by_nodeid) (unsigned int node_id);

 /*
  * IPC Initializers
  */
static int dispatch_init_send_response (struct conn_info *conn_info, void *message);

static int response_init_send_response (struct conn_info *conn_info, void *message);

static int (*ais_init_service[]) (struct conn_info *conn_info, void *message) = {
	response_init_send_response,
	dispatch_init_send_response
};

static int poll_handler_libais_deliver (poll_handle handle, int fd, int revent, void *data, unsigned int *prio);


static inline struct conn_info *conn_info_create (int fd) {
	struct conn_info *conn_info;
	int res;

	conn_info = malloc (sizeof (struct conn_info));
	if (conn_info == 0) {
		return (0);
	}

	memset (conn_info, 0, sizeof (struct conn_info));
	res = queue_init (&conn_info->outq, SIZEQUEUE,
		sizeof (struct outq_item));
	if (res != 0) {
		free (conn_info);
		return (0);
	}
	conn_info->inb = malloc (sizeof (char) * SIZEINB);
	if (conn_info->inb == 0) {
		queue_free (&conn_info->outq);
		free (conn_info);
		return (0);
	}

	conn_info->state = CONN_STATE_ACTIVE;
	conn_info->fd = fd;
	conn_info->service = SOCKET_SERVICE_INIT;
	return (conn_info);
}

#ifdef COMPILE_OUT
static void sigusr2_handler (int num)
{
	int i;

	for (i = 0; i < AIS_SERVICE_HANDLERS_COUNT; i++) {
		if (ais_service[i]->exec_dump_fn) {
			ais_service[i]->exec_dump_fn ();
		 }
	}

	signal (SIGUSR2 ,sigusr2_handler);
	return;
}
#endif

struct totem_ip_address *this_ip;
struct totem_ip_address this_non_loopback_ip;
#define LOCALHOST_IP inet_addr("127.0.0.1")

#if defined(OPENAIS_LINUX)
/* SUN_LEN is broken for abstract namespace
 */
#define AIS_SUN_LEN(a) sizeof(*(a))

char *socketname = "libais.socket";
#else
#define AIS_SUN_LEN(a) SUN_LEN(a)

char *socketname = "/var/run/libais.socket";
#endif

totempg_groups_handle openais_group_handle;

struct totempg_group openais_group = {
	.group		= "a",
	.group_len	= 1
};


static int libais_connection_active (struct conn_info *conn_info)
{
	return (conn_info->state == CONN_STATE_ACTIVE);
}

static void libais_disconnect_delayed (struct conn_info *conn_info)
{
	conn_info->state = CONN_STATE_DISCONNECTING_DELAYED;
	conn_info->conn_info_partner->state = CONN_STATE_DISCONNECTING_DELAYED;
}

static int libais_disconnect (struct conn_info *conn_info)
{
	int res = 0;
	struct outq_item *outq_item;

	if (conn_info->should_exit_fn &&
		ais_service[conn_info->service]->lib_exit_fn) {

		res = ais_service[conn_info->service]->lib_exit_fn (conn_info);
	}

	/*
	 * Call library exit handler and free private data
	 */
	if (conn_info->conn_info_partner &&
		conn_info->conn_info_partner->should_exit_fn &&
		ais_service[conn_info->conn_info_partner->service]->lib_exit_fn) {

		res = ais_service[conn_info->conn_info_partner->service]->lib_exit_fn (conn_info->conn_info_partner);
		if (conn_info->private_data) {
			free (conn_info->private_data);
		}
	}

	/*
	 * Close the library connection and free its
	 * data if it hasn't already been freed
	 */
	if (conn_info->state != CONN_STATE_DISCONNECTING) {
		conn_info->state = CONN_STATE_DISCONNECTING;

		close (conn_info->fd);

		/*
		 * Free the outq queued items
		 */
		while (!queue_is_empty (&conn_info->outq)) {
			outq_item = queue_item_get (&conn_info->outq);
			free (outq_item->msg);
			queue_item_remove (&conn_info->outq);
		}

		queue_free (&conn_info->outq);
		free (conn_info->inb);
	}

	/*
	 * Close the library connection and free its
	 * data if it hasn't already been freed
	 */
	if (conn_info->conn_info_partner &&
		conn_info->conn_info_partner->state != CONN_STATE_DISCONNECTING) {

		conn_info->conn_info_partner->state = CONN_STATE_DISCONNECTING;

		close (conn_info->conn_info_partner->fd);

		/*
		 * Free the outq queued items
		 */
		while (!queue_is_empty (&conn_info->conn_info_partner->outq)) {
			outq_item = queue_item_get (&conn_info->conn_info_partner->outq);
			free (outq_item->msg);
			queue_item_remove (&conn_info->conn_info_partner->outq);
		}

		queue_free (&conn_info->conn_info_partner->outq);
		if (conn_info->conn_info_partner->inb) {
			free (conn_info->conn_info_partner->inb);
		}
	}

	/*
	 * If exit_fn didn't request a retry,
	 * free the conn_info structure
	 */
	if (res != -1) {
		if (conn_info->conn_info_partner) {
			poll_dispatch_delete (aisexec_poll_handle,
				conn_info->conn_info_partner->fd);
		}
		poll_dispatch_delete (aisexec_poll_handle, conn_info->fd);

		free (conn_info->conn_info_partner);
		free (conn_info);
	}

	/*
	 * Inverse res from libais exit fn handler
	 */
	return (res != -1 ? -1 : 0);
}

static int cleanup_send_response (struct conn_info *conn_info) {
	struct queue *outq;
	int res = 0;
	struct outq_item *queue_item;
	struct msghdr msg_send;
	struct iovec iov_send;
	char *msg_addr;

	if (!libais_connection_active (conn_info)) {
		return (-1);
	}
	outq = &conn_info->outq;

	msg_send.msg_iov = &iov_send;
	msg_send.msg_name = 0;
	msg_send.msg_namelen = 0;
	msg_send.msg_iovlen = 1;
	msg_send.msg_control = 0;
	msg_send.msg_controllen = 0;
	msg_send.msg_flags = 0;

	while (!queue_is_empty (outq)) {
		queue_item = queue_item_get (outq);
		msg_addr = (char *)queue_item->msg;
		msg_addr = &msg_addr[conn_info->byte_start];

		iov_send.iov_base = msg_addr;
		iov_send.iov_len = queue_item->mlen - conn_info->byte_start;

retry_sendmsg:
		res = sendmsg (conn_info->fd, &msg_send, MSG_NOSIGNAL);
		if (res == -1 && errno == EINTR) {
			goto retry_sendmsg;
		}
		if (res == -1 && errno == EAGAIN) {
			break; /* outgoing kernel queue full */
		}
		if (res == -1) {
			return (-1); /* message couldn't be sent */
		}
		if (res + conn_info->byte_start != queue_item->mlen) {
			conn_info->byte_start += res;
			break;
		}

		/*
		 * Message sent, try sending another message
		 */
		queue_item_remove (outq);
		conn_info->byte_start = 0;
		free (queue_item->msg);
	} /* while queue not empty */

	if (queue_is_empty (outq)) {
		poll_dispatch_modify (aisexec_poll_handle, conn_info->fd,
			POLLIN|POLLNVAL, poll_handler_libais_deliver, 0);
	}
	return (0);
}

extern int openais_conn_send_response (
	void *conn,
	void *msg,
	int mlen)
{
	struct queue *outq;
	char *cmsg;
	int res = 0;
	int queue_empty;
	struct outq_item *queue_item;
	struct outq_item queue_item_out;
	struct msghdr msg_send;
	struct iovec iov_send;
	char *msg_addr;
	struct conn_info *conn_info = (struct conn_info *)conn;

	if (!libais_connection_active (conn_info)) {
		return (-1);
	}
	outq = &conn_info->outq;

	msg_send.msg_iov = &iov_send;
	msg_send.msg_name = 0;
	msg_send.msg_namelen = 0;
	msg_send.msg_iovlen = 1;
	msg_send.msg_control = 0;
	msg_send.msg_controllen = 0;
	msg_send.msg_flags = 0;

	if (queue_is_full (outq)) {
		/*
		 * Start a disconnect if we have not already started one
		 * and report that the outgoing queue is full
		 */
		log_printf (LOG_LEVEL_ERROR, "Library queue is full, disconnecting library connection.\n");
		libais_disconnect_delayed (conn_info);
		return (-1);
	}
	while (!queue_is_empty (outq)) {
		queue_item = queue_item_get (outq);
		msg_addr = (char *)queue_item->msg;
		msg_addr = &msg_addr[conn_info->byte_start];

		iov_send.iov_base = msg_addr;
		iov_send.iov_len = queue_item->mlen - conn_info->byte_start;

retry_sendmsg:
		res = sendmsg (conn_info->fd, &msg_send, MSG_NOSIGNAL);
		if (res == -1 && errno == EINTR) {
			goto retry_sendmsg;
		}
		if (res == -1 && errno == EAGAIN) {
			break; /* outgoing kernel queue full */
		}
		if (res == -1) {
			break; /* some other error, stop trying to send message */
		}
		if (res + conn_info->byte_start != queue_item->mlen) {
			conn_info->byte_start += res;
			break;
		}

		/*
		 * Message sent, try sending another message
		 */
		queue_item_remove (outq);
		conn_info->byte_start = 0;
		free (queue_item->msg);
	} /* while queue not empty */

	res = -1;

	queue_empty = queue_is_empty (outq);
	/*
	 * Send requested message
	 */
	if (queue_empty) {

		iov_send.iov_base = msg;
		iov_send.iov_len = mlen;
retry_sendmsg_two:
		res = sendmsg (conn_info->fd, &msg_send, MSG_NOSIGNAL);
		if (res == -1 && errno == EINTR) {
			goto retry_sendmsg_two;
		}
		if (res == -1 && errno == EAGAIN) {
			conn_info->byte_start = 0;
			poll_dispatch_modify (aisexec_poll_handle, conn_info->fd,
				POLLIN|POLLNVAL, poll_handler_libais_deliver, 0);
		}
		if (res != -1) {
			if (res + conn_info->byte_start != mlen) {
				conn_info->byte_start += res;
				res = -1;
			} else {
				conn_info->byte_start = 0;
				poll_dispatch_modify (aisexec_poll_handle, conn_info->fd,
					POLLIN|POLLNVAL, poll_handler_libais_deliver, 0);
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
			libais_disconnect_delayed (conn_info);
			return (-1);
		}
		queue_item_out.msg = cmsg;
		queue_item_out.mlen = mlen;
		memcpy (cmsg, msg, mlen);
		queue_item_add (outq, &queue_item_out);

		poll_dispatch_modify (aisexec_poll_handle, conn_info->fd,
			POLLOUT|POLLIN|POLLNVAL, poll_handler_libais_deliver, 0);
	}
	return (0);
}

static int poll_handler_libais_accept (
	poll_handle handle,
	int fd,
	int revent,
	void *data,
	unsigned int *prio)
{
	socklen_t addrlen;
	struct conn_info *conn_info;
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

	conn_info = conn_info_create (new_fd);
	if (conn_info == 0) {
		close (new_fd);
		return (0); /* This is an error, but -1 would indicate disconnect from poll */
	}

	poll_dispatch_add (aisexec_poll_handle, new_fd, POLLIN|POLLNVAL, conn_info,
		poll_handler_libais_deliver, 0);
	return (0);
}

static int dispatch_init_send_response (struct conn_info *conn_info, void *message)
{
	SaAisErrorT error = SA_AIS_ERR_ACCESS;
	struct req_lib_dispatch_init *req_lib_dispatch_init = (struct req_lib_dispatch_init *)message;
	struct res_lib_dispatch_init res_lib_dispatch_init;
	struct conn_info *msg_conn_info;

	if (conn_info->authenticated) {
		conn_info->service = req_lib_dispatch_init->resdis_header.service;
		if (!ais_service[req_lib_dispatch_init->resdis_header.service])
			error = SA_AIS_ERR_NOT_SUPPORTED;
		else
			error = SA_AIS_OK;

		conn_info->conn_info_partner = (struct conn_info *)req_lib_dispatch_init->conn_info;

		msg_conn_info = (struct conn_info *)req_lib_dispatch_init->conn_info;
		msg_conn_info->conn_info_partner = conn_info;

		if (error == SA_AIS_OK) {
			int private_data_size;

			private_data_size = ais_service[req_lib_dispatch_init->resdis_header.service]->private_data_size;
			if (private_data_size) {
				conn_info->private_data = malloc (private_data_size);

				conn_info->conn_info_partner->private_data = conn_info->private_data;
				if (conn_info->private_data == NULL) {
					error = SA_AIS_ERR_NO_MEMORY;
				} else {
					memset (conn_info->private_data, 0, private_data_size);
				}
			} else {
				conn_info->private_data = NULL;
				conn_info->conn_info_partner->private_data = NULL;
			}
		}

	res_lib_dispatch_init.header.size = sizeof (struct res_lib_dispatch_init);
	res_lib_dispatch_init.header.id = MESSAGE_RES_INIT;
	res_lib_dispatch_init.header.error = error;

	openais_conn_send_response (
		conn_info,
		&res_lib_dispatch_init,
		sizeof (res_lib_dispatch_init));

	if (error != SA_AIS_OK) {
		return (-1);
	}

	}

	conn_info->should_exit_fn = 1;
	ais_service[req_lib_dispatch_init->resdis_header.service]->lib_init_fn (conn_info);
	return (0);
}

void *openais_conn_partner_get (void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;

	return ((void *)conn_info->conn_info_partner);
}

void *openais_conn_private_data_get (void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;

	return ((void *)conn_info->private_data);
}

static int response_init_send_response (struct conn_info *conn_info, void *message)
{
	SaAisErrorT error = SA_AIS_ERR_ACCESS;
	struct req_lib_response_init *req_lib_response_init = (struct req_lib_response_init *)message;
	struct res_lib_response_init res_lib_response_init;

	if (conn_info->authenticated) {
		conn_info->service = req_lib_response_init->resdis_header.service;
		error = SA_AIS_OK;
	}
	res_lib_response_init.header.size = sizeof (struct res_lib_response_init);
	res_lib_response_init.header.id = MESSAGE_RES_INIT;
	res_lib_response_init.header.error = error;
	res_lib_response_init.conn_info = (unsigned long)conn_info;

	openais_conn_send_response (
		conn_info,
		&res_lib_response_init,
		sizeof (res_lib_response_init));

	if (error == SA_AIS_ERR_ACCESS) {
		return (-1);
	}
	conn_info->should_exit_fn = 0;
	return (0);
}

struct res_overlay {
	struct res_header header;
	char buf[4096];
};

static int poll_handler_libais_deliver (poll_handle handle, int fd, int revent, void *data, unsigned int *prio)
{
	int res;
	struct conn_info *conn_info = (struct conn_info *)data;
	struct req_header *header;
	int service;
	struct msghdr msg_recv;
	struct iovec iov_recv;
#ifdef OPENAIS_LINUX
	struct cmsghdr *cmsg;
	char cmsg_cred[CMSG_SPACE (sizeof (struct ucred))];
	struct ucred *cred;
	int on = 0;
#else
	uid_t euid;
	gid_t egid;
#endif
	int send_ok = 0;
	int send_ok_joined = 0;
	struct iovec send_ok_joined_iovec;
	struct res_overlay res_overlay;

	if (revent & (POLLERR|POLLHUP)) {
		res = libais_disconnect (conn_info);
		return (res);
	}

	/*
	 * Handle delayed disconnections
	 */
	if (conn_info->state == CONN_STATE_DISCONNECTING_DELAYED) {
		res = libais_disconnect (conn_info);
		return (res);
	}

	if (conn_info->state == CONN_STATE_DISCONNECTING) {
		return (0);
	}

	if (revent & POLLOUT) {
		cleanup_send_response (conn_info);
	}

	if ((revent & POLLIN) == 0) {
		return (0);
	}

	msg_recv.msg_iov = &iov_recv;
	msg_recv.msg_iovlen = 1;
	msg_recv.msg_name = 0;
	msg_recv.msg_namelen = 0;
	msg_recv.msg_flags = 0;

	if (conn_info->authenticated) {
		msg_recv.msg_control = 0;
		msg_recv.msg_controllen = 0;
	} else {
#ifdef OPENAIS_LINUX
		msg_recv.msg_control = (void *)cmsg_cred;
		msg_recv.msg_controllen = sizeof (cmsg_cred);
#else
		euid = -1; egid = -1;
		if (getpeereid(fd, &euid, &egid) != -1 &&
		    (euid == 0 || egid == gid_valid)) {
				conn_info->authenticated = 1;
		}
		if (conn_info->authenticated == 0) {
			log_printf (LOG_LEVEL_SECURITY, "Connection not authenticated because gid is %d, expecting %d\n", egid, gid_valid);
		}
#endif
	}

	iov_recv.iov_base = &conn_info->inb[conn_info->inb_start];
	iov_recv.iov_len = (SIZEINB) - conn_info->inb_start;
	assert (iov_recv.iov_len != 0);

retry_recv:
	res = recvmsg (fd, &msg_recv, MSG_NOSIGNAL);
	if (res == -1 && errno == EINTR) {
		goto retry_recv;
	} else
	if (res == -1 && errno != EAGAIN) {
		goto error_disconnect;
	} else
	if (res == 0) {
		goto error_disconnect;
		return (-1);
	}

	/*
	 * Authenticate if this connection has not been authenticated
	 */
#ifdef OPENAIS_LINUX
	if (conn_info->authenticated == 0) {
		cmsg = CMSG_FIRSTHDR (&msg_recv);
		cred = (struct ucred *)CMSG_DATA (cmsg);
		if (cred) {
			if (cred->uid == 0 || cred->gid == gid_valid) {
				setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof (on));
				conn_info->authenticated = 1;
			}
		}
		if (conn_info->authenticated == 0) {
			log_printf (LOG_LEVEL_SECURITY, "Connection not authenticated because gid is %d, expecting %d\n", cred->gid, gid_valid);
		}
	}
#endif
	/*
	 * Dispatch all messages received in recvmsg that can be dispatched
	 * sizeof (struct req_header) needed at minimum to do any processing
	 */
	conn_info->inb_inuse += res;
	conn_info->inb_start += res;

	while (conn_info->inb_inuse >= sizeof (struct req_header) && res != -1) {
		header = (struct req_header *)&conn_info->inb[conn_info->inb_start - conn_info->inb_inuse];

		if (header->size > conn_info->inb_inuse) {
			break;
		}
		service = conn_info->service;

		/*
		 * If this service is in init phase, initialize service
		 * else handle message using service service
		 */
		if (service == SOCKET_SERVICE_INIT) {
			res = ais_init_service[header->id] (conn_info, header);
// TODO error in init_two_fn needs to be handled
		} else  {
			/*
			 * Not an init service, but a standard service
			 */
			if (header->id < 0 || header->id > ais_service[service]->lib_service_count) {
				log_printf (LOG_LEVEL_SECURITY, "Invalid header id is %d min 0 max %d\n",
				header->id, ais_service[service]->lib_service_count);
				res = -1;
				goto error_disconnect;
			}

			/*
			 * If flow control is required of the library handle, determine that
			 * openais is not in synchronization and that totempg has room available
			 * to queue a message, otherwise tell the library we are busy and to
			 * try again later
			 */
			send_ok_joined_iovec.iov_base = header;
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
		//		*prio = 0;
				ais_service[service]->lib_service[header->id].lib_handler_fn(conn_info, header);
			} else {
		//		*prio = (*prio) + 1;

				/*
				 * Overload, tell library to retry
				 */
				res_overlay.header.size =
					ais_service[service]->lib_service[header->id].response_size;
				res_overlay.header.id =
					ais_service[service]->lib_service[header->id].response_id;
				res_overlay.header.error = SA_AIS_ERR_TRY_AGAIN;
				openais_conn_send_response (
					conn_info,
					&res_overlay,
					res_overlay.header.size);
			}
		}
		conn_info->inb_inuse -= header->size;
	} /* while */

	if (conn_info->inb_inuse == 0) {
		conn_info->inb_start = 0;
	} else
// BUG	if (connections[fd].inb_start + connections[fd].inb_inuse >= SIZEINB) {
	if (conn_info->inb_start >= SIZEINB) {
		/*
		 * If in buffer is full, move it back to start
		 */
		memmove (conn_info->inb,
			&conn_info->inb[conn_info->inb_start - conn_info->inb_inuse],
			sizeof (char) * conn_info->inb_inuse);
		conn_info->inb_start = conn_info->inb_inuse;
	}

	return (0);

error_disconnect:
	res = libais_disconnect (conn_info);
	return (res);
}

void sigintr_handler (int signum)
{

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

	totempg_finalize ();
	openais_exit_error (AIS_DONE_EXIT);
}


static int pool_sizes[] = { 0, 0, 0, 0, 0, 4096, 0, 1, 0, /* 256 */
					1024, 0, 1, 4096, 0, 0, 0, 0, /* 65536 */
					1, 1, 1, 1, 1, 1, 1, 1, 1 };

static void openais_sync_completed (void)
{
}

static int openais_sync_callbacks_retrieve (int sync_id,
	struct sync_callbacks *callbacks)
{
	if (ais_service[sync_id] == NULL) {
		memset (callbacks, 0, sizeof (struct sync_callbacks));
		return (-1);
	}
	callbacks->name = ais_service[sync_id]->name;
	callbacks->sync_init = ais_service[sync_id]->sync_init;
	callbacks->sync_process = ais_service[sync_id]->sync_process;
	callbacks->sync_activate = ais_service[sync_id]->sync_activate;
	callbacks->sync_abort = ais_service[sync_id]->sync_abort;
	return (0);
}

char delivery_data[MESSAGE_SIZE_MAX];

static void deliver_fn (
	struct totem_ip_address *source_addr,
	struct iovec *iovec,
	int iov_len,
	int endian_conversion_required)
{
	struct req_header *header;
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
		header = (struct req_header *)delivery_data;
	} else {
		header = (struct req_header *)iovec[0].iov_base;
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
	if (endian_conversion_required) {
		ais_service[service]->exec_service[fn_id].exec_endian_convert_fn
			(header);
	}

	ais_service[service]->exec_service[fn_id].exec_handler_fn
		(header, source_addr);
}

static struct memb_ring_id aisexec_ring_id;

static void confchg_fn (
	enum totem_configuration_type configuration_type,
	struct totem_ip_address *member_list, int member_list_entries,
	struct totem_ip_address *left_list, int left_list_entries,
	struct totem_ip_address *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{
	int i;

	memcpy (&aisexec_ring_id, ring_id, sizeof (struct memb_ring_id));

	if (!totemip_localhost_check(this_ip)) {
		totemip_copy(&this_non_loopback_ip, this_ip);
	}

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
}

static void aisexec_gid_determine (struct main_config *main_config)
{
	struct group *group;
	group = getgrnam (main_config->group);
	if (group == 0) {
		log_printf (LOG_LEVEL_ERROR, "ERROR: The '%s' group is not found in /etc/group, please read the documentation.\n", group);
		openais_exit_error (AIS_DONE_GID_DETERMINE);
	}
	gid_valid = group->gr_gid;
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
#define DEBUG
#ifndef DEBUG
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
			break;
		default:
			exit (0);
			break;
	}
#endif
}

static void aisexec_libais_bind (int *server_fd)
{
	int libais_server_fd;
	struct sockaddr_un un_addr;
	int res;

	/*
	 * Create socket for libais clients, name socket, listen for connections
	 */
	libais_server_fd = socket (PF_UNIX, SOCK_STREAM, 0);
	if (libais_server_fd == -1) {
		log_printf (LOG_LEVEL_ERROR ,"Cannot create libais client connections socket.\n");
		openais_exit_error (AIS_DONE_LIBAIS_SOCKET);
	};

	totemip_nosigpipe(libais_server_fd);
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

	*server_fd = libais_server_fd;
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

int message_source_is_local(struct message_source *source)
{
	int ret = 0;
	if ((totemip_localhost_check(&source->addr)
	     ||(totemip_equal(&source->addr, &this_non_loopback_ip)))) {
		ret = 1;
	}
	return ret;
}

void message_source_set (
	struct message_source *source,
	void *conn)
{
	totemip_copy(&source->addr, this_ip);
	source->conn = conn;
}


struct totem_logging_configuration totem_logging_configuration;

int main (int argc, char **argv)
{
	int libais_server_fd;
	char *error_string;
	struct main_config main_config;
	struct totem_config totem_config;
	unsigned int objdb_handle;
	unsigned int config_handle;
	unsigned int config_version = 0;
	struct objdb_iface_ver0 *objdb;
	void *objdb_p;
	struct config_iface_ver0 *config;
	void *config_p;
	char *config_iface;
	int res;

	memset(&this_non_loopback_ip, 0, sizeof(struct totem_ip_address));

	totemip_localhost(AF_INET, &this_non_loopback_ip);

	aisexec_poll_handle = poll_create ();

//TODO	signal (SIGUSR2, sigusr2_handler);

	/*
	 * Load the object database interface
	 */
	res = lcr_ifact_reference (
		&objdb_handle,
		"objdb",
		0,
		&objdb_p,
		0);

	objdb = (struct objdb_iface_ver0 *)objdb_p;

	objdb->objdb_init ();

	/* User's bootstrap config service */
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
		log_printf (LOG_LEVEL_NOTICE, "AIS Executive Service: Copyright (C) 2002-2006 MontaVista Software, Inc and contributors.\n");

		log_printf (LOG_LEVEL_ERROR, "can't open configuration module\n");
		openais_exit_error (AIS_DONE_MAINCONFIGREAD);
	}

	res = config->config_readconfig(objdb, &error_string);
	if (res == -1) {
		log_printf (LOG_LEVEL_NOTICE, "AIS Executive Service: Copyright (C) 2002-2006 MontaVista Software, Inc and contributors.\n");

		log_printf (LOG_LEVEL_ERROR, error_string);
		openais_exit_error (AIS_DONE_MAINCONFIGREAD);
	}

	openais_service_default_objdb_set (objdb);

	openais_service_link_all (objdb);

	res = openais_main_config_read (objdb, &error_string, &main_config);
	if (res == -1) {
		log_printf (LOG_LEVEL_NOTICE, "AIS Executive Service: Copyright (C) 2002-2006 MontaVista Software, Inc and contributors.\n");

		log_printf (LOG_LEVEL_ERROR, error_string);
		openais_exit_error (AIS_DONE_MAINCONFIGREAD);
	}

	res = totem_config_read (objdb, &totem_config, &error_string, 3);
	if (res == -1) {
		log_printf (LOG_LEVEL_NOTICE, "AIS Executive Service: Copyright (C) 2002-2006 MontaVista Software, Inc and contributors.\n");
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

	res = log_setup (&error_string, main_config.logmode, main_config.logfile);
	if (res == -1) {
		log_printf (LOG_LEVEL_ERROR, error_string);
		openais_exit_error (AIS_DONE_LOGSETUP);
	}

	aisexec_uid_determine (&main_config);

	aisexec_gid_determine (&main_config);

	log_printf (LOG_LEVEL_NOTICE, "AIS Executive Service: Copyright (C) 2002-2006 MontaVista Software, Inc. and contributors.\n");

	/*
	 * Set round robin realtime scheduling with priority 99
	 * Lock all memory to avoid page faults which may interrupt
	 * application healthchecking
	 */
	aisexec_setscheduler ();

	aisexec_mlockall ();

	totem_config.totem_logging_configuration = totem_logging_configuration;

	totem_config.totem_logging_configuration.log_level_security = mklog (LOG_LEVEL_SECURITY, LOG_SERVICE_GMI);
	totem_config.totem_logging_configuration.log_level_error = mklog (LOG_LEVEL_ERROR, LOG_SERVICE_GMI);
	totem_config.totem_logging_configuration.log_level_warning = mklog (LOG_LEVEL_WARNING, LOG_SERVICE_GMI);
	totem_config.totem_logging_configuration.log_level_notice = mklog (LOG_LEVEL_NOTICE, LOG_SERVICE_GMI);
	totem_config.totem_logging_configuration.log_level_debug = mklog (LOG_LEVEL_DEBUG, LOG_SERVICE_GMI);
	totem_config.totem_logging_configuration.log_printf = internal_log_printf;

	/*
	 * if totempg_initialize doesn't have root priveleges, it cannot
	 * bind to a specific interface.  This only matters if
	 * there is more then one interface in a system, so
	 * in this case, only a warning is printed
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
	this_ip = &totem_config.interfaces[0].boundto;
	openais_service_init_all (service_count, objdb);


	sync_register (openais_sync_callbacks_retrieve, openais_sync_completed);

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

	signal (SIGINT, sigintr_handler);

	aisexec_libais_bind (&libais_server_fd);

	aisexec_tty_detach ();

	log_printf (LOG_LEVEL_NOTICE, "AIS Executive Service: started and ready to receive connections.\n");

	/*
	 * Setup libais connection dispatch routine
	 */
	poll_dispatch_add (aisexec_poll_handle, libais_server_fd,
		POLLIN, 0, poll_handler_libais_accept, 0);

	/*
	 * Join multicast group and setup delivery
	 *  and configuration change functions
	 */

	/*
	 * Start main processing loop
	 */
	poll_run (aisexec_poll_handle);

	return (0);
}
