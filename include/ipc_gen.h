/*
 * Copyright (c) 2002-2005 MontaVista Software, Inc.
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
#ifndef IPC_GEN_H_DEFINED
#define IPC_GEN_H_DEFINED

enum req_init_types {
	MESSAGE_REQ_EVS_INIT,
	MESSAGE_REQ_CLM_INIT,
	MESSAGE_REQ_AMF_INIT,
	MESSAGE_REQ_CKPT_INIT,
	MESSAGE_REQ_EVT_INIT
};

enum res_init_types {
	MESSAGE_RES_INIT
};

#define	MESSAGE_REQ_LIB_ACTIVATEPOLL 0
#define	MESSAGE_RES_LIB_ACTIVATEPOLL 50

enum nodeexec_message_types {
		MESSAGE_REQ_EXEC_SYNC_BARRIER = 0,
		MESSAGE_REQ_EXEC_EVS_MCAST = 1,
		MESSAGE_REQ_EXEC_CLM_NODEJOIN = 2,
		MESSAGE_REQ_EXEC_AMF_COMPONENTREGISTER = 3,
		MESSAGE_REQ_EXEC_AMF_COMPONENTUNREGISTER = 4,
		MESSAGE_REQ_EXEC_AMF_ERRORREPORT = 5,
		MESSAGE_REQ_EXEC_AMF_ERRORCANCELALL = 6,
		MESSAGE_REQ_EXEC_AMF_READINESSSTATESET = 7,
		MESSAGE_REQ_EXEC_AMF_HASTATESET = 8,
		MESSAGE_REQ_EXEC_CKPT_CHECKPOINTOPEN = 9,
		MESSAGE_REQ_EXEC_CKPT_CHECKPOINTCLOSE = 10,
		MESSAGE_REQ_EXEC_CKPT_CHECKPOINTUNLINK = 11,
		MESSAGE_REQ_EXEC_CKPT_CHECKPOINTRETENTIONDURATIONSET = 12,
		MESSAGE_REQ_EXEC_CKPT_CHECKPOINTRETENTIONDURATIONEXPIRE = 13,
		MESSAGE_REQ_EXEC_CKPT_SECTIONCREATE = 14,
		MESSAGE_REQ_EXEC_CKPT_SECTIONDELETE = 15,
		MESSAGE_REQ_EXEC_CKPT_SECTIONEXPIRATIONTIMESET = 16,
		MESSAGE_REQ_EXEC_CKPT_SECTIONWRITE = 17,
		MESSAGE_REQ_EXEC_CKPT_SECTIONOVERWRITE = 18,
		MESSAGE_REQ_EXEC_CKPT_SECTIONREAD = 19,
		MESSAGE_REQ_EXEC_EVT_EVENTDATA = 20,
		MESSAGE_REQ_EXEC_EVT_CHANCMD = 21,
		MESSAGE_REQ_EXEC_EVT_RECOVERY_EVENTDATA = 22
};

struct req_header {
	int size;
	int id;
} __attribute__((packed));

struct res_header {
	int size;
	int id;
	SaErrorT error;
};

struct message_source {
    struct conn_info *conn_info;
    struct in_addr in_addr;
} __attribute__((packed));

#endif /* IPC_GEN_H_DEFINED */
