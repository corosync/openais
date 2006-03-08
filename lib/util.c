/*
 * vi: set autoindent tabstop=4 shiftwidth=4 :
 *
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
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

SaErrorT
saServiceConnect (
	int *fdOut,
	enum service_types service)
{
	int fd;
	int result;
	struct sockaddr_un address;
	struct req_lib_response_init req_lib_response_init;
	struct res_lib_response_init res_lib_response_init;
	SaErrorT error;
	gid_t egid;

	/*
	 * Allow set group id binaries to be authenticated
	 */
	egid = getegid();
	setregid (egid, -1);

	memset (&address, 0, sizeof (struct sockaddr_un));
	address.sun_family = PF_UNIX;
	strcpy (address.sun_path + 1, "libais.socket");
	fd = socket (PF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		return (SA_AIS_ERR_NO_RESOURCES);
	}
	result = connect (fd, (struct sockaddr *)&address, sizeof (address));
	if (result == -1) {
		return (SA_AIS_ERR_TRY_AGAIN);
	}

	req_lib_response_init.resdis_header.size = sizeof (req_lib_response_init);
	req_lib_response_init.resdis_header.id = MESSAGE_REQ_RESPONSE_INIT;
	req_lib_response_init.resdis_header.service = service;

	error = saSendRetry (fd, &req_lib_response_init,
		sizeof (struct req_lib_response_init), MSG_NOSIGNAL);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}
	error = saRecvRetry (fd, &res_lib_response_init,
		sizeof (struct res_lib_response_init), MSG_WAITALL | MSG_NOSIGNAL);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	/*
	 * Check for security errors
	 */
	if (res_lib_response_init.header.error != SA_AIS_OK) {
		error = res_lib_response_init.header.error;
		goto error_exit;
	}

	*fdOut = fd;
	return (SA_AIS_OK);
error_exit:
	close (fd);
	return (error);
}

SaErrorT
saServiceConnectTwo (
	int *responseOut,
	int *callbackOut,
	enum service_types service)
{
	int responseFD;
	int callbackFD;
	int result;
	struct sockaddr_un address;
	struct req_lib_response_init req_lib_response_init;
	struct res_lib_response_init res_lib_response_init;
	struct req_lib_dispatch_init req_lib_dispatch_init;
	struct res_lib_dispatch_init res_lib_dispatch_init;
	SaErrorT error;
	gid_t egid;

	/*
	 * Allow set group id binaries to be authenticated
	 */
	egid = getegid();
	setregid (egid, -1);

	memset (&address, 0, sizeof (struct sockaddr_un));
	address.sun_family = PF_UNIX;
	strcpy (address.sun_path + 1, "libais.socket");
	responseFD = socket (PF_UNIX, SOCK_STREAM, 0);
	if (responseFD == -1) {
		return (SA_AIS_ERR_NO_RESOURCES);
	}
	result = connect (responseFD, (struct sockaddr *)&address, sizeof (address));
	if (result == -1) {
		return (SA_AIS_ERR_TRY_AGAIN);
	}

	req_lib_response_init.resdis_header.size = sizeof (req_lib_response_init);
	req_lib_response_init.resdis_header.id = MESSAGE_REQ_RESPONSE_INIT;
	req_lib_response_init.resdis_header.service = service;

	error = saSendRetry (responseFD, &req_lib_response_init,
		sizeof (struct req_lib_response_init),
		MSG_NOSIGNAL);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}
	error = saRecvRetry (responseFD, &res_lib_response_init,
		sizeof (struct res_lib_response_init),
		MSG_WAITALL | MSG_NOSIGNAL);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	/*
	 * Check for security errors
	 */
	if (res_lib_response_init.header.error != SA_AIS_OK) {
		error = res_lib_response_init.header.error;
		goto error_exit;
	}

	*responseOut = responseFD;

/* if I comment out the 4 lines below the executive crashes */
	callbackFD = socket (PF_UNIX, SOCK_STREAM, 0);
	if (callbackFD == -1) {
		return (SA_AIS_ERR_NO_RESOURCES);
	}
	result = connect (callbackFD, (struct sockaddr *)&address, sizeof (address));
	if (result == -1) {
		return (SA_AIS_ERR_TRY_AGAIN);
	}

	req_lib_dispatch_init.resdis_header.size = sizeof (req_lib_dispatch_init);
	req_lib_dispatch_init.resdis_header.id = MESSAGE_REQ_DISPATCH_INIT;
	req_lib_dispatch_init.resdis_header.service = service;

	req_lib_dispatch_init.conn_info = res_lib_response_init.conn_info;

	error = saSendRetry (callbackFD, &req_lib_dispatch_init,
		sizeof (struct req_lib_dispatch_init),
		MSG_NOSIGNAL);
	if (error != SA_AIS_OK) {
		goto error_exit_two;
	}
	error = saRecvRetry (callbackFD, &res_lib_dispatch_init,
		sizeof (struct res_lib_dispatch_init),
		MSG_WAITALL | MSG_NOSIGNAL);
	if (error != SA_AIS_OK) {
		goto error_exit_two;
	}

	/*
	 * Check for security errors
	 */
	if (res_lib_dispatch_init.header.error != SA_AIS_OK) {
		error = res_lib_dispatch_init.header.error;
		goto error_exit;
	}

	*callbackOut = callbackFD;
	return (SA_AIS_OK);

error_exit_two:
	close (callbackFD);
error_exit:
	close (responseFD);
	return (error);
}

SaErrorT
saRecvRetry (
	int s,
	void *msg,
	size_t len,
	int flags)
{
	SaErrorT error = SA_AIS_OK;
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

	result = recvmsg (s, &msg_recv, flags);
	if (result == -1 && errno == EINTR) {
		goto retry_recv;
	}
	if (result == -1 && errno == EAGAIN) {
		goto retry_recv;
	}
	if (result == -1 || result == 0) {
		error = SA_AIS_ERR_LIBRARY;
		goto error_exit;
	}
	processed += result;
	if (processed != len) {
		goto retry_recv;
	}
	assert (processed == len);
error_exit:
	return (error);
}

struct res_overlay {
	struct res_header header;
	char payload[0];
};

SaErrorT
saSendRetry (
	int s,
	const void *msg,
	size_t len,
	int flags)
{
	SaErrorT error = SA_AIS_OK;
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

	result = sendmsg (s, &msg_send, flags);

	/*
	 * return immediately on any kind of syscall error that maps to
	 * SA_AIS_ERR if no part of message has been sent
	 */
	if (result == -1 && processed == 0) {
		if (errno == EINTR) {
			error = SA_AIS_ERR_TRY_AGAIN;
			goto error_exit;
		}
		if (errno == EAGAIN) {
			error = SA_AIS_ERR_TRY_AGAIN;
			goto error_exit;
		}
		if (errno == EFAULT) {
			error = SA_AIS_ERR_INVALID_PARAM;
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
			error = SA_AIS_ERR_LIBRARY;
			goto error_exit;
		}
	}

	/*
	 * return ERR_LIBRARY on any other syscall error
	 */
	if (result == -1) {
		error = SA_AIS_ERR_LIBRARY;
		goto error_exit;
	}

	processed += result;
	if (processed != len) {
		goto retry_send;
	}

error_exit:
	return (error);
}

SaErrorT saSendMsgRetry (
        int s,
        struct iovec *iov,
        int iov_len)
{
	SaErrorT error = SA_AIS_OK;
	int result;
	int total_size = 0;
	int i;
	int csize;
	int csize_cntr;
	int total_sent = 0;
	int iov_len_sendmsg = iov_len;
	struct iovec *iov_sendmsg = iov;
	struct iovec iovec_save;
	int iovec_saved_position = -1;

	struct msghdr msg_send;

	for (i = 0; i < iov_len; i++) {
		total_size += iov[i].iov_len;
	}
	msg_send.msg_iov = iov_sendmsg;
	msg_send.msg_iovlen = iov_len_sendmsg;
	msg_send.msg_name = 0;
	msg_send.msg_namelen = 0;
	msg_send.msg_control = 0;
	msg_send.msg_controllen = 0;
	msg_send.msg_flags = 0;

retry_sendmsg:
	result = sendmsg (s, &msg_send, MSG_NOSIGNAL | MSG_DONTWAIT);
	/*
	 * Can't send now, and message not committed, so don't retry send
	 */
	if (result == -1 && iovec_saved_position == -1) {
		if (errno == EINTR) {
			error = SA_AIS_ERR_TRY_AGAIN;
			goto error_exit;
		}
		if (errno == EAGAIN) {
			error = SA_AIS_ERR_TRY_AGAIN;
			goto error_exit;
		}
		if (errno == EFAULT) {
			error = SA_AIS_ERR_INVALID_PARAM;
			goto error_exit;
		}
	}

	/*
	 * Retry (and block) if portion of message has already been written
	 */
	if (result == -1 && iovec_saved_position != -1) {
		if (errno == EINTR) {
			goto retry_sendmsg;
		}
		if (errno == EAGAIN) {
			goto retry_sendmsg;
		}
		if (errno == EFAULT) {
			error = SA_AIS_ERR_LIBRARY;
			goto error_exit;
		}
	}
	
	/*
	 * ERR_LIBRARY for any other syscall error
	 */
	if (result == -1) {
		error = SA_AIS_ERR_LIBRARY;
		goto error_exit;
	}

	if (iovec_saved_position != -1) {
			memcpy (&iov[iovec_saved_position], &iovec_save, sizeof (struct iovec));
	}

	total_sent += result;
	if (total_sent != total_size) {
		for (i = 0, csize = 0, csize_cntr = 0; i < iov_len; i++) {
			csize += iov[i].iov_len;
			if (csize > total_sent) {
				break;
			}

			csize_cntr += iov[i].iov_len;
		}
		memcpy (&iovec_save, &iov[i], sizeof (struct iovec));
		iovec_saved_position = i;
		iov[i].iov_base = ((unsigned char *)(iov[i].iov_base)) +
			(total_sent - csize_cntr);
		iov[i].iov_len = total_size - total_sent;
		msg_send.msg_iov = &iov[i];
		msg_send.msg_iovlen = iov_len - i;

		goto retry_sendmsg;
	}

error_exit:
	return (error);
}

SaErrorT saSendMsgReceiveReply (
        int s,
        struct iovec *iov,
        int iov_len,
        void *responseMessage,
        int responseLen)
{
	SaErrorT error = SA_AIS_OK;

	error = saSendMsgRetry (s, iov, iov_len);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}
	
	error = saRecvRetry (s, responseMessage, responseLen,
		MSG_WAITALL | MSG_NOSIGNAL);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

error_exit:
	return (error);
}

SaErrorT saSendReceiveReply (
        int s,
        void *requestMessage,
        int requestLen,
        void *responseMessage,
        int responseLen)
{
	SaErrorT error = SA_AIS_OK;

	error = saSendRetry (s, requestMessage, requestLen,
		MSG_NOSIGNAL);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}
	
	error = saRecvRetry (s, responseMessage, responseLen,
		MSG_WAITALL | MSG_NOSIGNAL);
	if (error != SA_AIS_OK) {
		goto error_exit;
	}

error_exit:
	return (error);
}

SaErrorT
saPollRetry (
        struct pollfd *ufds,
        unsigned int nfds,
        int timeout) 
{
	SaErrorT error = SA_AIS_OK;
	int result;

retry_poll:
	result = poll (ufds, nfds, timeout);
	if (result == -1 && errno == EINTR) {
		goto retry_poll;
	}
	if (result == -1) {
		error = SA_AIS_ERR_LIBRARY;
	}

	return (error);
}


SaErrorT
saHandleCreate (
	struct saHandleDatabase *handleDatabase,
	int instanceSize,
	SaUint64T *handleOut)
{
	uint32_t handle;
	uint32_t check;
	void *newHandles;
	int found = 0;
	void *instance;

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
		if (newHandles == 0) {
			pthread_mutex_unlock (&handleDatabase->mutex);
			return (SA_AIS_ERR_NO_MEMORY);
		}
		handleDatabase->handles = newHandles;
	}

	instance = malloc (instanceSize);
	if (instance == 0) {
		return (SA_AIS_ERR_NO_MEMORY);
	}


	check = random();

	memset (instance, 0, instanceSize);

	handleDatabase->handles[handle].state = SA_HANDLE_STATE_ACTIVE;

	handleDatabase->handles[handle].instance = instance;

	handleDatabase->handles[handle].refCount = 1;

	handleDatabase->handles[handle].check = check;

	*handleOut = (SaUint64T)((uint64_t)check << 32 | handle);

	pthread_mutex_unlock (&handleDatabase->mutex);

	return (SA_AIS_OK);
}


SaErrorT
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


SaErrorT
saHandleInstanceGet (
	struct saHandleDatabase *handleDatabase,
	SaUint64T inHandle,
	void **instance)
{ 
	uint32_t check = inHandle >> 32;
	uint32_t handle = inHandle & 0xffffffff;

	SaErrorT error = SA_AIS_OK;
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


SaErrorT
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


SaErrorT
saVersionVerify (
    struct saVersionDatabase *versionDatabase,
	SaVersionT *version)
{
	int i;
	SaErrorT error = SA_AIS_ERR_VERSION;

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

