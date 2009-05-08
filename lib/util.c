/*
 * vi: set autoindent tabstop=4 shiftwidth=4 :
 *
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/un.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <assert.h>
#include <sys/shm.h>
#include <sys/sem.h>

#include "../include/saAis.h"
#include "../include/ipc_gen.h"
#include "util.h"

enum SA_HANDLE_STATE {
	SA_HANDLE_STATE_EMPTY,
	SA_HANDLE_STATE_PENDINGREMOVAL,
	SA_HANDLE_STATE_ACTIVE
};

struct saHandle {
	int state;
	void *instance;
	int refCount;
	uint32_t check;
};

struct ipc_segment {
	int fd;
	int shmid;
	int semid;
	int flow_control_state;
	struct shared_memory *shared_memory;
	uid_t euid;
};


#if defined(OPENAIS_LINUX)
/* SUN_LEN is broken for abstract namespace 
 */
#define AIS_SUN_LEN(a) sizeof(*(a))

static char *socketname = "libais.socket";
#else
#define AIS_SUN_LEN(a) SUN_LEN(a)

static char *socketname = "/var/run/libais.socket";
#endif

#ifdef SO_NOSIGPIPE
void socket_nosigpipe(int s)
{
	int on = 1;
	setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (void *)&on, sizeof(on));
}
#endif 

static int
openais_send (
	int s,
	const void *msg,
	size_t len)
{
	int result;
	struct msghdr msg_send;
	struct iovec iov_send;
	char *rbuf = (char *)msg;
	int processed = 0;

	msg_send.msg_iov = &iov_send;
	msg_send.msg_iovlen = 1;
	msg_send.msg_name = 0;
	msg_send.msg_namelen = 0;
	msg_send.msg_control = 0;
	msg_send.msg_controllen = 0;
	msg_send.msg_flags = 0;

retry_send:
	iov_send.iov_base = (void *)&rbuf[processed];
	iov_send.iov_len = len - processed;

	result = sendmsg (s, &msg_send, MSG_NOSIGNAL);

	/*
	 * return immediately on any kind of syscall error that maps to
	 * SA_AIS_ERR if no part of message has been sent
	 */
	if (result == -1 && processed == 0) {
		if (errno == EINTR) {
			goto error_exit;
		}
		if (errno == EAGAIN) {
			goto error_exit;
		}
		if (errno == EFAULT) {
			goto error_exit;
		}
	}

	/*
	 * retry read operations that are already started except
	 * for fault in that case, return ERR_LIBRARY
	 */
	if (result == -1 && processed > 0) {
		if (errno == EINTR) {
			goto retry_send;
		}
		if (errno == EAGAIN) {
			goto retry_send;
		}
		if (errno == EFAULT) {
			goto error_exit;
		}
	}

	/*
	 * return ERR_LIBRARY on any other syscall error
	 */
	if (result == -1) {
		goto error_exit;
	}

	processed += result;
	if (processed != len) {
		goto retry_send;
	}

	return (0);

error_exit:
	return (-1);
}

static int
openais_recv (
	int s,
	void *msg,
	size_t len)
{
	int error = 0;
	int result;
	struct msghdr msg_recv;
	struct iovec iov_recv;
	char *rbuf = (char *)msg;
	int processed = 0;

	msg_recv.msg_iov = &iov_recv;
	msg_recv.msg_iovlen = 1;
	msg_recv.msg_name = 0;
	msg_recv.msg_namelen = 0;
	msg_recv.msg_control = 0;
	msg_recv.msg_controllen = 0;
	msg_recv.msg_flags = 0;

retry_recv:
	iov_recv.iov_base = (void *)&rbuf[processed];
	iov_recv.iov_len = len - processed;

	result = recvmsg (s, &msg_recv, MSG_NOSIGNAL|MSG_WAITALL);
	if (result == -1 && errno == EINTR) {
		goto retry_recv;
	}
	if (result == -1 && errno == EAGAIN) {
		goto retry_recv;
	}
#if defined(OPENAIS_SOLARIS) || defined(OPENAIS_BSD) || defined(OPENAIS_DARWIN)
	/* On many OS poll never return POLLHUP or POLLERR.
	 * EOF is detected when recvmsg return 0.
	 */
	if (result == 0) {
		error = -1;
		goto error_exit;
	}
#endif
	if (result == -1 || result == 0) {
		error = -1;
		goto error_exit;
	}
	processed += result;
	if (processed != len) {
		goto retry_recv;
	}
	assert (processed == len);
error_exit:
	return (0);
}

static int 
priv_change_send (struct ipc_segment *ipc_segment)
{
	char buf_req;
	mar_req_priv_change req_priv_change;
	unsigned int res;
	
	req_priv_change.euid = geteuid();
	/*
	 * Don't resend request unless euid has changed
	*/
	if (ipc_segment->euid == req_priv_change.euid) {
		return (0);
	}
	req_priv_change.egid = getegid();

	buf_req = MESSAGE_REQ_CHANGE_EUID;
	res = openais_send (ipc_segment->fd, &buf_req, 1);
	if (res == -1) {
		return (-1);
	}

	res = openais_send (ipc_segment->fd, &req_priv_change,
		sizeof (req_priv_change));
	if (res == -1) {
		return (-1);
	}

	ipc_segment->euid = req_priv_change.euid;
	return (0);
}
	
	
#if defined(_SEM_SEMUN_UNDEFINED)
union semun {
	int val;
	struct semid_ds *buf;
	unsigned short int *array;
	struct seminfo *__buf;
};
#endif

SaAisErrorT
openais_service_connect (
	enum service_types service,
	void **shmseg)
{
	int request_fd;
	struct sockaddr_un address;
	SaAisErrorT error;
	struct ipc_segment *ipc_segment;
	key_t shmkey = 0;
	key_t semkey = 0;
	int res;
	mar_req_setup_t req_setup;
	mar_res_setup_t res_setup;
	union semun semun;

	res_setup.error = SA_AIS_ERR_LIBRARY;

	request_fd = socket (PF_UNIX, SOCK_STREAM, 0);
	if (request_fd == -1) {
		return (-1);
	}

	memset (&address, 0, sizeof (struct sockaddr_un));
#if defined(OPENAIS_BSD) || defined(OPENAIS_DARWIN)
	address.sun_len = sizeof(struct sockaddr_un);
#endif
	address.sun_family = PF_UNIX;
#if defined(OPENAIS_LINUX)
	strcpy (address.sun_path + 1, socketname);
#else
	strcpy (address.sun_path, socketname);
#endif
	res = connect (request_fd, (struct sockaddr *)&address,
		AIS_SUN_LEN(&address));
	if (res == -1) {
		close (request_fd);
		return (SA_AIS_ERR_TRY_AGAIN);
	}

	ipc_segment = malloc (sizeof (struct ipc_segment));
	if (ipc_segment == NULL) {
		close (request_fd);
		return (-1);
	}
	bzero (ipc_segment, sizeof (struct ipc_segment));

	/*
	 * Allocate a shared memory segment
	 */
	do {
		shmkey = random();
		ipc_segment->shmid = shmget (shmkey, sizeof (struct shared_memory),
			IPC_CREAT|IPC_EXCL|0600);
	} while (ipc_segment->shmid == -1);

	/*
	 * Allocate a semaphore segment
	 */
	do {
		semkey = random();
		ipc_segment->semid = semget (semkey, 3, IPC_CREAT|IPC_EXCL|0600);
		ipc_segment->euid = geteuid ();
	} while (ipc_segment->semid == -1);

	/*
	 * Attach to shared memory segment
	 */
	ipc_segment->shared_memory = shmat (ipc_segment->shmid, NULL, 0);
	if (ipc_segment->shared_memory == (void *)-1) {
		goto error_exit;
	}
	
	semun.val = 0;
	res = semctl (ipc_segment->semid, 0, SETVAL, semun);
	if (res != 0) {
		goto error_exit;
	}

	res = semctl (ipc_segment->semid, 1, SETVAL, semun);
	if (res != 0) {
		goto error_exit;
	}

	res = semctl (ipc_segment->semid, 2, SETVAL, semun);
	if (res != 0) {
		goto error_exit;
	}

	req_setup.shmkey = shmkey;
	req_setup.semkey = semkey;
	req_setup.service = service;

	error = openais_send (request_fd, &req_setup, sizeof (mar_req_setup_t));
	if (error != 0) {
		goto error_exit;
	}
	error = openais_recv (request_fd, &res_setup, sizeof (mar_res_setup_t));
	if (error != 0) {
		goto error_exit;
	}

	ipc_segment->fd = request_fd;
	ipc_segment->flow_control_state = 0;
	*shmseg = ipc_segment;

	/*
	 * Something go wrong with server
	 * Cleanup all
	 */
	if (res_setup.error == SA_AIS_ERR_TRY_AGAIN) {
		goto error_exit;
	}

	return (res_setup.error);

error_exit:
	close (request_fd);
	if (ipc_segment->shmid > 0)
		shmctl (ipc_segment->shmid, IPC_RMID, NULL);
	if (ipc_segment->semid > 0)
		semctl (ipc_segment->semid, 0, IPC_RMID);
	return (res_setup.error);
}

SaAisErrorT
openais_service_disconnect (
	void *ipc_context)
{
	struct ipc_segment *ipc_segment = (struct ipc_segment *)ipc_context;

	shutdown (ipc_segment->fd, SHUT_RDWR);
	close (ipc_segment->fd);
	shmdt (ipc_segment->shared_memory);
	free (ipc_segment);
	return (SA_AIS_OK);
}

int
openais_dispatch_flow_control_get (
        void *ipc_context)
{
	struct ipc_segment *ipc_segment = (struct ipc_segment *)ipc_context;

	return (ipc_segment->flow_control_state);
}


int
openais_fd_get (void *ipc_ctx)
{
	struct ipc_segment *ipc_segment = (struct ipc_segment *)ipc_ctx;

	return (ipc_segment->fd);
}

static void memcpy_swrap (
	void *dest, void *src, int len, unsigned int *read)
{
	char *dest_chr = (char *)dest;
	char *src_chr = (char *)src;

	unsigned int first_read;
	unsigned int second_read;

	first_read = len;
	second_read = 0;

	if (len + *read >= DISPATCH_SIZE) {
		first_read = DISPATCH_SIZE - *read;
		second_read = (len + *read) % DISPATCH_SIZE;
	}
	memcpy (dest_chr, &src_chr[*read], first_read);
	if (second_read) {
		memcpy (&dest_chr[first_read], src_chr,
			second_read);
	}
	*read = (*read + len) % (DISPATCH_SIZE);
}
int original_flow = -1;

int
openais_dispatch_recv (void *ipc_ctx, void *data, int timeout)
{
	struct pollfd ufds;
	struct sembuf sop;
	int poll_events;
	mar_res_header_t *header;
	char buf;
	struct ipc_segment *ipc_segment = (struct ipc_segment *)ipc_ctx;
	int res;
	unsigned int my_read;
	char buf_two = 1;

	ufds.fd = ipc_segment->fd;
	ufds.events = POLLIN;
	ufds.revents = 0;

retry_poll:
	poll_events = poll (&ufds, 1, timeout);
	if (poll_events == -1 && errno == EINTR) {
		goto retry_poll;
	} else 
	if (poll_events == -1) {
		return (-1);
	} else
	if (poll_events == 0) {
		return (0);
	}
	if (poll_events == 1 && (ufds.revents & (POLLERR|POLLHUP))) {
		return (-1);
	}
retry_recv:
	res = recv (ipc_segment->fd, &buf, 1, 0);
	if (res == -1 && errno == EINTR) {
		goto retry_recv;
	} else
	if (res == -1) {
		return (-1);
	}
	if (res == 0) {
		return (-1);
	}
	ipc_segment->flow_control_state = 0;
	if (buf == 1 || buf == 2) {
		ipc_segment->flow_control_state = 1;
	}
	/*
	 * Notify executive to flush any pending dispatch messages
	 */
	if (ipc_segment->flow_control_state) {
		buf_two = MESSAGE_REQ_OUTQ_FLUSH;
		res = openais_send (ipc_segment->fd, &buf_two, 1);
		assert (res == 0); //TODO
	}
	/*
	 * This is just a notification of flow control starting at the addition
	 * of a new pending message, not a message to dispatch
	 */
	if (buf == 2) {
		return (0);
	}
	if (buf == 3) {
		return (0);
	}

	sop.sem_num = 2;
	sop.sem_op = -1;
	sop.sem_flg = 0;

retry_semop:
	res = semop (ipc_segment->semid, &sop, 1);
	if (res == -1 && errno == EINTR) {
		goto retry_semop;
	} else
	if (res == -1 && errno == EACCES) {
		priv_change_send (ipc_segment);
		goto retry_semop;
	} else
	if (res == -1) {
		return (-1);
	}
	
	if (ipc_segment->shared_memory->read + sizeof (mar_res_header_t) >= DISPATCH_SIZE) {
		my_read = ipc_segment->shared_memory->read;
		memcpy_swrap (data,
			ipc_segment->shared_memory->dispatch_buffer,
			sizeof (mar_res_header_t),
			&ipc_segment->shared_memory->read);
		header = (mar_res_header_t *)data;
		memcpy_swrap (
			data + sizeof (mar_res_header_t),
			ipc_segment->shared_memory->dispatch_buffer,
			header->size - sizeof (mar_res_header_t),
			&ipc_segment->shared_memory->read);
	} else {
		header = (mar_res_header_t *)&ipc_segment->shared_memory->dispatch_buffer[ipc_segment->shared_memory->read];
		memcpy_swrap (
			data,
			ipc_segment->shared_memory->dispatch_buffer,
			header->size,
			&ipc_segment->shared_memory->read);
	}

	return (1);
}

static SaAisErrorT
openais_msg_send (
	void *ipc_context,
	struct iovec *iov,
	int iov_len)
{
	struct ipc_segment *ipc_segment = (struct ipc_segment *)ipc_context;
	struct sembuf sop;
	int i;
	int res;
	int req_buffer_idx = 0;

	for (i = 0; i < iov_len; i++) {
		memcpy (&ipc_segment->shared_memory->req_buffer[req_buffer_idx],
			iov[i].iov_base,
			iov[i].iov_len);
		req_buffer_idx += iov[i].iov_len;
	}
	/*
	 * Signal semaphore #0 indicting a new message from client
	 * to server request queue
	 */
	sop.sem_num = 0;
	sop.sem_op = 1;
	sop.sem_flg = 0;

retry_semop:
	res = semop (ipc_segment->semid, &sop, 1);
	if (res == -1 && errno == EINTR) {
		goto retry_semop;
	} else
	if (res == -1 && errno == EACCES) {
		priv_change_send (ipc_segment);
		goto retry_semop;
	} else
	if (res == -1) {
		return (SA_AIS_ERR_LIBRARY);
	}
	return (SA_AIS_OK);
}

static SaAisErrorT
openais_reply_receive (
	void *ipc_context,
	void *res_msg, int res_len)
{
	struct sembuf sop;
	struct ipc_segment *ipc_segment = (struct ipc_segment *)ipc_context;
	unsigned int res;

	/*
	 * Wait for semaphore #1 indicating a new message from server
	 * to client in the response queue
	 */
	sop.sem_num = 1;
	sop.sem_op = -1;
	sop.sem_flg = 0;

retry_semop:
	res = semop (ipc_segment->semid, &sop, 1);
	if (res == -1 && errno == EINTR) {
		goto retry_semop;
	} else
	if (res == -1 && errno == EACCES) {
		priv_change_send (ipc_segment);
		goto retry_semop;
	} else
	if (res == -1) {
		return (SA_AIS_ERR_LIBRARY);
	}

	memcpy (res_msg, ipc_segment->shared_memory->res_buffer, res_len);
	return (SA_AIS_OK);
}

static SaAisErrorT
openais_reply_receive_in_buf (
	void *ipc_context,
	void **res_msg)
{
	struct sembuf sop;
	struct ipc_segment *ipc_segment = (struct ipc_segment *)ipc_context;
	int res;

	/*
	 * Wait for semaphore #1 indicating a new message from server
	 * to client in the response queue
	 */
	sop.sem_num = 1;
	sop.sem_op = -1;
	sop.sem_flg = 0;

retry_semop:
	res = semop (ipc_segment->semid, &sop, 1);
	if (res == -1 && errno == EINTR) {
		goto retry_semop;
	} else
	if (res == -1 && errno == EACCES) {
		priv_change_send (ipc_segment);
		goto retry_semop;
	} else
	if (res == -1) {
		return (SA_AIS_ERR_LIBRARY);
	}

	*res_msg = (char *)ipc_segment->shared_memory->res_buffer;
	return (SA_AIS_OK);
}

SaAisErrorT
openais_msg_send_reply_receive (
	void *ipc_context,
	struct iovec *iov,
	int iov_len,
	void *res_msg,
	int res_len)
{
	SaAisErrorT res;

	res = openais_msg_send (ipc_context, iov, iov_len);
	if (res != SA_AIS_OK) {
		return (res);
	}

	res = openais_reply_receive (ipc_context, res_msg, res_len);
	if (res != SA_AIS_OK) {
		return (res);
	}

	return (SA_AIS_OK);
}

SaAisErrorT
openais_msg_send_reply_receive_in_buf (
	void *ipc_context,
	struct iovec *iov,
	int iov_len,
	void **res_msg)
{
	unsigned int res;

	res = openais_msg_send (ipc_context, iov, iov_len);
	if (res != SA_AIS_OK) {
		return (res);
	}

	res = openais_reply_receive_in_buf (ipc_context, res_msg);
	if (res != SA_AIS_OK) {
		return (res);
	}

	return (SA_AIS_OK);
}

SaAisErrorT
saHandleCreate (
	struct saHandleDatabase *handleDatabase,
	int instanceSize,
	SaUint64T *handleOut)
{
	uint32_t handle;
	uint32_t check;
	void *newHandles = NULL;
	int found = 0;
	void *instance;
	int i;

	pthread_mutex_lock (&handleDatabase->mutex);

	for (handle = 0; handle < handleDatabase->handleCount; handle++) {
		if (handleDatabase->handles[handle].state == SA_HANDLE_STATE_EMPTY) {
			found = 1;
			break;
		}
	}

	if (found == 0) {
		handleDatabase->handleCount += 1;
		newHandles = (struct saHandle *)realloc (handleDatabase->handles,
			sizeof (struct saHandle) * handleDatabase->handleCount);
		if (newHandles == NULL) {
			pthread_mutex_unlock (&handleDatabase->mutex);
			return (SA_AIS_ERR_NO_MEMORY);
		}
		handleDatabase->handles = newHandles;
	}

	instance = malloc (instanceSize);
	if (instance == 0) {
		free (newHandles);
		pthread_mutex_unlock (&handleDatabase->mutex);
		return (SA_AIS_ERR_NO_MEMORY);
	}


	/*
	 * This code makes sure the random number isn't zero
	 * We use 0 to specify an invalid handle out of the 1^64 address space
	 * If we get 0 200 times in a row, the RNG may be broken
	 */
	for (i = 0; i < 200; i++) {
		check = random();
		if (check != 0) {
			break;
		}
	}

	memset (instance, 0, instanceSize);

	handleDatabase->handles[handle].state = SA_HANDLE_STATE_ACTIVE;

	handleDatabase->handles[handle].instance = instance;

	handleDatabase->handles[handle].refCount = 1;

	handleDatabase->handles[handle].check = check;

	*handleOut = (SaUint64T)((uint64_t)check << 32 | handle);

	pthread_mutex_unlock (&handleDatabase->mutex);

	return (SA_AIS_OK);
}


SaAisErrorT
saHandleDestroy (
	struct saHandleDatabase *handleDatabase,
	SaUint64T inHandle)
{
	SaAisErrorT error = SA_AIS_OK;
	uint32_t check = inHandle >> 32;
	uint32_t handle = inHandle & 0xffffffff;

	pthread_mutex_lock (&handleDatabase->mutex);

	if (check != handleDatabase->handles[handle].check) {
		pthread_mutex_unlock (&handleDatabase->mutex);
		error = SA_AIS_ERR_BAD_HANDLE;
		return (error);
	}

	handleDatabase->handles[handle].state = SA_HANDLE_STATE_PENDINGREMOVAL;

	pthread_mutex_unlock (&handleDatabase->mutex);

	saHandleInstancePut (handleDatabase, inHandle);

	return (error);
}


SaAisErrorT
saHandleInstanceGet (
	struct saHandleDatabase *handleDatabase,
	SaUint64T inHandle,
	void **instance)
{ 
	uint32_t check = inHandle >> 32;
	uint32_t handle = inHandle & 0xffffffff;

	SaAisErrorT error = SA_AIS_OK;
	pthread_mutex_lock (&handleDatabase->mutex);

	if (handle >= (SaUint64T)handleDatabase->handleCount) {
		error = SA_AIS_ERR_BAD_HANDLE;
		goto error_exit;
	}
	if (handleDatabase->handles[handle].state != SA_HANDLE_STATE_ACTIVE) {
		error = SA_AIS_ERR_BAD_HANDLE;
		goto error_exit;
	}
	if (check != handleDatabase->handles[handle].check) {
		error = SA_AIS_ERR_BAD_HANDLE;
		goto error_exit;
	}


	*instance = handleDatabase->handles[handle].instance;

	handleDatabase->handles[handle].refCount += 1;

error_exit:
	pthread_mutex_unlock (&handleDatabase->mutex);

	return (error);
}


SaAisErrorT
saHandleInstancePut (
	struct saHandleDatabase *handleDatabase,
	SaUint64T inHandle)
{
	void *instance;
	SaAisErrorT error = SA_AIS_OK;
	uint32_t check = inHandle >> 32;
	uint32_t handle = inHandle & 0xffffffff;

	pthread_mutex_lock (&handleDatabase->mutex);

	if (check != handleDatabase->handles[handle].check) {
		error = SA_AIS_ERR_BAD_HANDLE;
		goto error_exit;
	}

	handleDatabase->handles[handle].refCount -= 1;
	assert (handleDatabase->handles[handle].refCount >= 0);

	if (handleDatabase->handles[handle].refCount == 0) {
		instance = (handleDatabase->handles[handle].instance);
		handleDatabase->handleInstanceDestructor (instance);
		free (instance);
		memset (&handleDatabase->handles[handle], 0, sizeof (struct saHandle));
	}

error_exit:
	pthread_mutex_unlock (&handleDatabase->mutex);

	return (error);
}


SaAisErrorT
saVersionVerify (
    struct saVersionDatabase *versionDatabase,
	SaVersionT *version)
{
	int i;
	SaAisErrorT error = SA_AIS_ERR_VERSION;

	if (version == 0) {
		return (SA_AIS_ERR_INVALID_PARAM);
	}

	/*
	 * Look for a release code that we support.  If we find it then
	 * make sure that the supported major version is >= to the required one.
	 * In any case we return what we support in the version structure.
	 */
	for (i = 0; i < versionDatabase->versionCount; i++) {

		/*
		 * Check if the caller requires and old release code that we don't support.
		 */
		if (version->releaseCode < versionDatabase->versionsSupported[i].releaseCode) {
				break;
		}

		/*
		 * Check if we can support this release code.
		 */
		if (version->releaseCode == versionDatabase->versionsSupported[i].releaseCode) {

			/*
			 * Check if we can support the major version requested.
			 */
			if (versionDatabase->versionsSupported[i].majorVersion >= version->majorVersion) {
				error = SA_AIS_OK;
				break;
			} 

			/*
			 * We support the release code, but not the major version.
			 */
			break;
		}
	}

	/*
	 * If we fall out of the if loop, the caller requires a release code
	 * beyond what we support.
	 */
	if (i == versionDatabase->versionCount) {
		i = versionDatabase->versionCount - 1;
	}

	/*
	 * Tell the caller what we support
	 */
	memcpy(version, &versionDatabase->versionsSupported[i], sizeof(*version));
	return (error);
}

/*
 * Get the time of day and convert to nanoseconds
 */
SaTimeT clustTimeNow(void)
{
	struct timeval tv;
	SaTimeT time_now;

	if (gettimeofday(&tv, 0)) {
		return 0ULL;
	}

	time_now = (SaTimeT)(tv.tv_sec) * 1000000000ULL;
	time_now += (SaTimeT)(tv.tv_usec) * 1000ULL;

	return time_now;
}

