/*
 * Copyright (c) 2008 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Ryan O'Hara (rohara@redhat.com)
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

#ifndef IPC_TMR_H_DEFINED
#define IPC_TMR_H_DEFINED

#include "saAis.h"
#include "saTmr.h"

#include <corosync/ipc_gen.h>

enum req_lib_tmr_timer_types {
	MESSAGE_REQ_TMR_TIMERSTART = 0,
	MESSAGE_REQ_TMR_TIMERRESCHEDULE = 1,
	MESSAGE_REQ_TMR_TIMERCANCEL = 2,
	MESSAGE_REQ_TMR_PERIODICTIMERSKIP = 3,
	MESSAGE_REQ_TMR_TIMERREMAININGTIMEGET = 4,
	MESSAGE_REQ_TMR_TIMERATTRIBUTESGET = 5,
	MESSAGE_REQ_TMR_TIMEGET = 6,
	MESSAGE_REQ_TMR_CLOCKTICKGET = 7,
	MESSAGE_REQ_TMR_TIMEREXPIREDCALLBACK = 8,
};

enum res_lib_tmr_timer_types {
	MESSAGE_RES_TMR_TIMERSTART = 0,
	MESSAGE_RES_TMR_TIMERRESCHEDULE = 1,
	MESSAGE_RES_TMR_TIMERCANCEL = 2,
	MESSAGE_RES_TMR_PERIODICTIMERSKIP = 3,
	MESSAGE_RES_TMR_TIMERREMAININGTIMEGET = 4,
	MESSAGE_RES_TMR_TIMERATTRIBUTESGET = 5,
	MESSAGE_RES_TMR_TIMEGET = 6,
	MESSAGE_RES_TMR_CLOCKTICKGET = 7,
	MESSAGE_RES_TMR_TIMEREXPIREDCALLBACK = 8,
};

struct req_lib_tmr_timerstart {
	mar_req_header_t header;
	SaTmrTimerAttributesT timer_attributes;
	void *timer_data;
};

struct res_lib_tmr_timerstart {
	mar_res_header_t header;
	SaTmrTimerIdT timer_id;
	SaTimeT call_time;
};

struct req_lib_tmr_timerreschedule {
	mar_req_header_t header;
	SaTmrTimerIdT timer_id;
	SaTmrTimerAttributesT timer_attributes;
};

struct res_lib_tmr_timerreschedule {
	mar_res_header_t header;
	SaTimeT call_time;
};

struct req_lib_tmr_timercancel {
	mar_req_header_t header;
	SaTmrTimerIdT timer_id;
};

struct res_lib_tmr_timercancel {
	mar_res_header_t header;
	void *timer_data;
};

struct req_lib_tmr_periodictimerskip {
	mar_req_header_t header;
	SaTmrTimerIdT timer_id;
};

struct res_lib_tmr_periodictimerskip {
	mar_res_header_t header;
};

struct req_lib_tmr_timerremainingtimeget {
	mar_req_header_t header;
	SaTmrTimerIdT timer_id;
};

struct res_lib_tmr_timerremainingtimeget {
	mar_res_header_t header;
	SaTimeT remaining_time;
};

struct req_lib_tmr_timerattributesget {
	mar_req_header_t header;
	SaTmrTimerIdT timer_id;
};

struct res_lib_tmr_timerattributesget {
	mar_res_header_t header;
	SaTmrTimerAttributesT timer_attributes;
};

struct req_lib_tmr_timeget {
	mar_req_header_t header;
};

struct res_lib_tmr_timeget {
	mar_res_header_t header;
	SaTimeT current_time;
};

struct req_lib_tmr_clocktickget {
	mar_req_header_t header;
};

struct res_lib_tmr_clocktickget {
	mar_res_header_t header;
	SaTimeT clock_tick;
};

struct req_lib_tmr_timerexpiredcallback {
	mar_res_header_t header;
};

struct res_lib_tmr_timerexpiredcallback {
	mar_res_header_t header;
	SaTmrTimerIdT timer_id;
	SaUint64T expiration_count;
	void *timer_data;
};

#endif /* IPC_TMR_H_DEFINED  */
