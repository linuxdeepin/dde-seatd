// Copyright (C) 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: MIT

#ifndef _SEATD_CONTROL_H
#define _SEATD_CONTROL_H

#include <stdint.h>
#include <sys/types.h>

#include "connection.h"
#include "linked_list.h"

#define CONTROL_NAME_MAX 64

struct event_source_fd;
struct server;

enum control_opcode {
	CONTROL_CREATE_GROUP_VT = 1,
	CONTROL_DESTROY_GROUP_VT = 2,
	CONTROL_GROUP_VT_CREATED = 100,
	CONTROL_VT_CHANGED = 101,
	CONTROL_ERROR = 255,
};

struct control_header {
	uint16_t opcode;
	uint16_t size;
};

struct control_create_group_vt_request {
	int32_t owner_pid;
	int32_t vt;
	char user[CONTROL_NAME_MAX];
	char session[CONTROL_NAME_MAX];
};

struct control_destroy_group_vt_request {
	int32_t vt;
};

struct control_group_vt_created_event {
	int32_t owner_pid;
	int32_t vt;
};

struct control_vt_change_event {
	int32_t old_vt;
	int32_t new_vt;
};

struct control_error_message {
	int32_t error;
};

struct control_client {
	struct linked_list link; // server::control_clients
	struct server *server;
	struct event_source_fd *event_source;
	struct connection connection;
	pid_t pid;
	uid_t uid;
	gid_t gid;
};

int control_handle_connection(int fd, uint32_t mask, void *data);
void control_broadcast_vt_change(struct server *server, int old_vt, int new_vt);

#endif
