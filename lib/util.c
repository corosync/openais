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

#include <saAis.h>
#include <corosync/ipc_gen.h>
#include "util.h"

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

