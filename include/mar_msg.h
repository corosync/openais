/*
 * Copyright (C) 2009 Red Hat, Inc.
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

#ifndef AIS_MAR_MSG_H_DEFINED
#define AIS_MAR_MSG_H_DEFINED

#include <corosync/mar_gen.h>
#include "mar_sa.h"
#include "saAis.h"
#include "saMsg.h"

/*
 * SaMsgQueueHandleT
 */
typedef mar_uint64_t mar_msg_queue_handle_t;

static inline void swab_mar_msg_queue_handle_t (
	mar_msg_queue_handle_t *to_swab)
{
	swab_mar_uint64_t (to_swab);
}

/*
 * SaMsgSenderIdT
 */
typedef mar_uint64_t mar_msg_sender_id_t;

static inline void swab_mar_msg_sender_id_t (
	mar_msg_sender_id_t *to_swab)
{
	swab_mar_uint64_t (to_swab);
}

/*
 * SaMsgAckFlagsT
 */
typedef mar_uint32_t mar_msg_ack_flags_t;

static inline void swab_mar_msg_ack_flags_t (
	mar_msg_ack_flags_t *to_swab)
{
	swab_mar_uint32_t (to_swab);
}

/*
 * SaMsgQueueCreationFlagsT
 */
typedef mar_uint32_t mar_msg_queue_creation_flags_t;

static inline void swab_mar_msg_queue_creation_flags_t (
	mar_msg_queue_creation_flags_t *to_swab)
{
	swab_mar_uint32_t (to_swab);
}

/*
 * SaMsgQueueCreationAttributesT
 */
typedef struct {
	mar_msg_queue_creation_flags_t creation_flags __attribute__((aligned(8)));
	mar_size_t size[SA_MSG_MESSAGE_LOWEST_PRIORITY + 1] __attribute__((aligned(8)));
	mar_time_t retention_time __attribute__((aligned(8)));
} mar_msg_queue_creation_attributes_t;

static inline void swab_mar_msg_queue_creation_attributes_t (
	mar_msg_queue_creation_attributes_t *to_swab)
{
	int i;

	swab_mar_msg_queue_creation_flags_t (&to_swab->creation_flags);
	swab_mar_time_t (&to_swab->retention_time);

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
		swab_mar_size_t (&to_swab->size[i]);
	}
}

static inline void marshall_from_mar_msg_queue_creation_attributes_t (
	SaMsgQueueCreationAttributesT *dst,
	mar_msg_queue_creation_attributes_t *src)
{
	int i;

	memset (dst, 0, sizeof (SaMsgQueueCreationAttributesT));

	dst->creationFlags = src->creation_flags;
	dst->retentionTime = src->retention_time;

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
		dst->size[i] = src->size[i];
	}
}

static inline void marshall_to_mar_msg_queue_creation_attributes_t (
	mar_msg_queue_creation_attributes_t *dst,
	SaMsgQueueCreationAttributesT *src)
{
	int i;

	memset (dst, 0, sizeof (mar_msg_queue_creation_attributes_t));

	dst->creation_flags = src->creationFlags;
	dst->retention_time = src->retentionTime;

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
		dst->size[i] = src->size[i];
	}
}

/*
 * SaMsgQueueOpenFlagsT
 */
typedef mar_uint32_t mar_msg_queue_open_flags_t;

static inline void swab_mar_queue_open_flags_t (
	mar_msg_queue_open_flags_t *to_swab)
{
	swab_mar_uint32_t (to_swab);
}

/*
 * SaMsgQueueUsageT
 */
typedef struct {
	mar_size_t queue_size __attribute__((aligned(8)));
	mar_size_t queue_used __attribute__((aligned(8)));
	mar_uint32_t number_of_messages __attribute__((aligned(8)));
} mar_msg_queue_usage_t;

static inline void swab_mar_msg_queue_usage_t (
	mar_msg_queue_usage_t *to_swab)
{
	swab_mar_size_t (&to_swab->queue_size);
	swab_mar_size_t (&to_swab->queue_used);
	swab_mar_uint32_t (&to_swab->number_of_messages);
}

/*
 * SaMsgQueueStatusT
 */
typedef struct {
	mar_msg_queue_creation_flags_t creation_flags __attribute__((aligned(8)));
	mar_time_t retention_time __attribute__((aligned(8)));
	mar_time_t close_time __attribute__((aligned(8)));
	mar_msg_queue_usage_t queue_usage[SA_MSG_MESSAGE_LOWEST_PRIORITY + 1] __attribute__((aligned(8)));
} mar_msg_queue_status_t;

static inline void swab_mar_msg_queue_status_t (
	mar_msg_queue_status_t *to_swab)
{
	int i;

	swab_mar_msg_queue_creation_flags_t (&to_swab->creation_flags);
	swab_mar_time_t (&to_swab->retention_time);
	swab_mar_time_t (&to_swab->close_time);

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i < SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
		swab_mar_msg_queue_usage_t (&to_swab->queue_usage[i]);
	}
}

/*
 * SaMsgQueueGroupPolicyT
 */
typedef mar_uint32_t mar_msg_queue_group_policy_t;

static inline void swab_mar_msg_queue_group_policy_t (
	mar_msg_queue_group_policy_t *to_swab)
{
	swab_mar_uint32_t (to_swab);
}

/*
 * SaMsgQueueGroupChangesT
 */
typedef mar_uint32_t mar_msg_queue_group_changes_t;

static inline void swab_mar_msg_queue_group_changes_t (
	mar_msg_queue_group_changes_t *to_swab)
{
	swab_mar_uint32_t (to_swab);
}

/*
 * SaMsgQueueGroupMemberT
 */
typedef struct {
	mar_name_t queue_name __attribute__((aligned(8)));
} mar_msg_queue_group_member_t;

static inline void swab_mar_msg_queue_group_member_t (
	mar_msg_queue_group_member_t *to_swab)
{
	swab_mar_name_t (&to_swab->queue_name);
}

static inline void marshall_from_msg_queue_group_member_t (
	SaMsgQueueGroupMemberT *dst,
	mar_msg_queue_group_member_t *src)
{
	marshall_mar_name_t_to_SaNameT (&dst->queueName, &src->queue_name);
}

static inline void marshall_to_msg_queue_group_member_t (
	mar_msg_queue_group_member_t *dst,
	SaMsgQueueGroupMemberT *src)
{
	marshall_SaNameT_to_mar_name_t (&dst->queue_name, &src->queueName);
}

/*
 * SaMsgQueueGroupNotificationT
 */
typedef struct {
	mar_msg_queue_group_member_t member __attribute__((aligned(8)));
	mar_msg_queue_group_changes_t change __attribute__((aligned(8)));
} mar_msg_queue_group_notification_t;

static inline void swab_mar_msg_queue_group_notification_t (
	mar_msg_queue_group_notification_t *to_swab)
{
	swab_mar_msg_queue_group_member_t (&to_swab->member);
	swab_mar_msg_queue_group_changes_t (&to_swab->change);
}

static inline void marshall_from_mar_msg_queue_group_notification_t (
	SaMsgQueueGroupNotificationT *dst,
	mar_msg_queue_group_notification_t *src)
{
	marshall_from_msg_queue_group_member_t (&dst->member, &src->member);
	dst->change = src->change;
}

static inline void marshall_to_mar_msg_queue_group_notification_t (
	mar_msg_queue_group_notification_t *dst,
	SaMsgQueueGroupNotificationT *src)
{
	marshall_to_msg_queue_group_member_t (&dst->member, &src->member);
	dst->change = src->change;
}

/*
 * SaMsgQueueGroupNotificationBufferT
 */
typedef struct {
	mar_uint32_t number_of_items __attribute__((aligned(8)));
	mar_msg_queue_group_notification_t *notification __attribute__((aligned(8)));
	mar_msg_queue_group_policy_t queue_group_policy __attribute__((aligned(8)));
} mar_msg_queue_group_notification_buffer_t;

static inline void swab_mar_msg_queue_group_notification_buffer_t (
	mar_msg_queue_group_notification_buffer_t *to_swab)
{
	swab_mar_uint32_t (&to_swab->number_of_items);
/* 	to_swab->notification */
	swab_mar_msg_queue_group_policy_t (&to_swab->queue_group_policy);
}

static inline void marshall_from_mar_msg_queue_group_notification_buffer_t (
	SaMsgQueueGroupNotificationBufferT *dst,
	mar_msg_queue_group_notification_buffer_t *src)
{
	dst->numberOfItems = src->number_of_items;
/* 	dst->notification = src->notification; */
	dst->queueGroupPolicy = src->queue_group_policy;
}

static inline void marshall_to_mar_msg_queue_group_notification_buffer_t (
	mar_msg_queue_group_notification_buffer_t *dst,
	SaMsgQueueGroupNotificationBufferT *src)
{
	dst->number_of_items = src->numberOfItems;
/* 	dst->notification = src->notification; */
	dst->queue_group_policy = src->queueGroupPolicy;
}

/*
 * SaMsgMessageT
 */
typedef struct {
	mar_uint32_t type __attribute__((aligned(8)));
	mar_uint32_t version __attribute__((aligned(8)));
	mar_size_t size __attribute__((aligned(8)));
	mar_name_t sender_name __attribute__((aligned(8)));
	mar_uint8_t priority __attribute__((aligned(8)));
	void *data __attribute__((aligned(8)));
} mar_msg_message_t;

static inline void swab_mar_msg_message_t (
	mar_msg_message_t *to_swab)
{
	swab_mar_uint32_t (&to_swab->type);
	swab_mar_uint32_t (&to_swab->version);
	swab_mar_size_t (&to_swab->size);
/* 	swab_mar_name_t (&to_swab->sender_name); */
	swab_mar_uint8_t (&to_swab->priority);
}

static inline void marshall_from_mar_msg_message_t (
	SaMsgMessageT *dst,
	mar_msg_message_t *src)
{
	memset (dst, 0, sizeof (SaMsgMessageT));

/* 	marshall_mar_name_t_to_SaNameT (&dst->senderName, &src->sender_name); */

	dst->type = src->type;
	dst->version = src->version;
	dst->size = src->size;
	dst->priority = src->priority;
}

static inline void marshall_to_mar_msg_message_t (
	mar_msg_message_t *dst,
	SaMsgMessageT *src)
{
	memset (dst, 0, sizeof (mar_msg_message_t));

/* 	marshall_SaNameT_to_mar_name_t (&dst->sender_name, &src->senderName)); */

	dst->type = src->type;
	dst->version = src->version;
	dst->size = src->size;
	dst->priority = src->priority;
}

/*
 * SaMsgQueueThresholdsT
 */
typedef struct {
	mar_size_t capacity_reached[SA_MSG_MESSAGE_LOWEST_PRIORITY + 1] __attribute__((aligned(8)));;
	mar_size_t capacity_available[SA_MSG_MESSAGE_LOWEST_PRIORITY + 1] __attribute__((aligned(8)));
} mar_msg_queue_thresholds_t;

static inline void swab_mar_msg_queue_thresholds_t (
	mar_msg_queue_thresholds_t *to_swab)
{
	int i;

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
		swab_mar_size_t (&to_swab->capacity_reached[i]);
		swab_mar_size_t (&to_swab->capacity_available[i]);
	}
}

static inline void marshall_from_mar_msg_queue_thresholds_t (
	SaMsgQueueThresholdsT *dst,
	mar_msg_queue_thresholds_t *src)
{
	int i;

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
		dst->capacityReached[i] = src->capacity_reached[i];
		dst->capacityAvailable[i] = src->capacity_available[i];
	}
}

static inline void marshall_to_mar_msg_queue_thresholds_t (
	mar_msg_queue_thresholds_t *dst,
	SaMsgQueueThresholdsT *src)
{
	int i;

	for (i = SA_MSG_MESSAGE_HIGHEST_PRIORITY; i <= SA_MSG_MESSAGE_LOWEST_PRIORITY; i++) {
		dst->capacity_reached[i] = src->capacityReached[i];
		dst->capacity_available[i] = src->capacityAvailable[i];
	}
}

/*
 * SaMsgLimitIdT
 */
typedef mar_uint32_t mar_msg_limit_id_t;

static inline void swab_mar_msg_limit_id_t (
	mar_msg_limit_id_t *to_swab)
{
	swab_mar_uint32_t (to_swab);
}

#endif /* AIS_MAR_MSG_H_DEFINED */
