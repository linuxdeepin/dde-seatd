// Copyright (C) 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: MIT

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__FreeBSD__)
#include <sys/ucred.h>
#include <sys/un.h>
#endif

#if defined(__NetBSD__)
#include <sys/un.h>
#endif

#include "client.h"
#include "control.h"
#include "log.h"
#include "poller.h"
#include "seat.h"
#include "server.h"

static int control_client_handle_connection(int fd, uint32_t mask, void *data);

static int prepare_control_fd(int fd) {
	int flags;
	if ((flags = fcntl(fd, F_GETFD)) == -1 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
		return -1;
	}
	if ((flags = fcntl(fd, F_GETFL)) == -1) {
		return -1;
	}
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
		return -1;
	}
	return 0;
}

static int get_peer(int fd, pid_t *pid, uid_t *uid, gid_t *gid) {
#if defined(__linux__)
	struct ucred cred;
	socklen_t len = sizeof cred;
	if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == -1) {
		return -1;
	}
	*pid = cred.pid;
	*uid = cred.uid;
	*gid = cred.gid;
	return 0;
#elif defined(__NetBSD__)
	struct unpcbid cred;
	socklen_t len = sizeof cred;
	if (getsockopt(fd, 0, LOCAL_PEEREID, &cred, &len) == -1) {
		return -1;
	}
	*pid = cred.unp_pid;
	*uid = cred.unp_euid;
	*gid = cred.unp_egid;
	return 0;
#elif defined(__FreeBSD__)
	struct xucred cred;
	socklen_t len = sizeof cred;
	if (getsockopt(fd, 0, LOCAL_PEERCRED, &cred, &len) == -1) {
		return -1;
	}
#if __FreeBSD_version >= 1300030 || (__FreeBSD_version >= 1202506 && __FreeBSD_version < 1300000)
	*pid = cred.cr_pid;
#else
	*pid = -1;
#endif
	*uid = cred.cr_uid;
	*gid = cred.cr_ngroups > 0 ? cred.cr_groups[0] : (gid_t)-1;
	return 0;
#else
#error Unsupported platform
#endif
}

static void control_client_destroy(struct control_client *client) {
	if (client == NULL) {
		return;
	}
	if (client->event_source != NULL) {
		event_source_fd_destroy(client->event_source);
		client->event_source = NULL;
	}
	linked_list_remove(&client->link);
	connection_close_fds(&client->connection);
	if (client->connection.fd != -1) {
		close(client->connection.fd);
		client->connection.fd = -1;
	}
	free(client);
}

static int control_client_flush(struct control_client *client) {
	int ret = connection_flush(&client->connection);
	if (ret == -1 && errno == EAGAIN) {
		return event_source_fd_update(client->event_source,
					      EVENT_READABLE | EVENT_WRITABLE);
	}
	if (ret >= 0) {
		return event_source_fd_update(client->event_source, EVENT_READABLE);
	}
	return -1;
}

static int control_send_error(struct control_client *client, int error_code) {
	struct control_header header = {
		.opcode = CONTROL_ERROR,
		.size = sizeof(struct control_error_message),
	};
	struct control_error_message msg = {
		.error = error_code,
	};
	if (connection_put(&client->connection, &header, sizeof(header)) == -1 ||
	    connection_put(&client->connection, &msg, sizeof(msg)) == -1) {
		return -1;
	}
	return control_client_flush(client);
}

static int control_send_message(struct control_client *client, uint16_t opcode,
				const void *payload, uint16_t payload_size) {
	struct control_header header = {
		.opcode = opcode,
		.size = payload_size,
	};
	if (connection_put(&client->connection, &header, sizeof(header)) == -1 ||
	    connection_put(&client->connection, payload, payload_size) == -1) {
		return -1;
	}
	return control_client_flush(client);
}

static int control_send_group_vt_created(struct control_client *client, pid_t owner_pid, int vt) {
	struct control_group_vt_created_event event = {
		.owner_pid = owner_pid,
		.vt = vt,
	};
	return control_send_message(client, CONTROL_GROUP_VT_CREATED, &event, sizeof(event));
}

static int control_send_vt_change(struct control_client *client, int old_vt, int new_vt) {
	struct control_vt_change_event event = {
		.old_vt = old_vt,
		.new_vt = new_vt,
	};
	return control_send_message(client, CONTROL_VT_CHANGED, &event, sizeof(event));
}

static struct client *control_find_owner_client(struct server *server, pid_t owner_pid) {
	for (struct linked_list *seat_elem = server->seats.next; seat_elem != &server->seats;
	     seat_elem = seat_elem->next) {
		struct seat *seat = (struct seat *)seat_elem;
		for (struct linked_list *client_elem = seat->clients.next;
		     client_elem != &seat->clients; client_elem = client_elem->next) {
			struct client *client = (struct client *)client_elem;
			if (client->pid == owner_pid) {
				return client;
			}
		}
	}
	return NULL;
}

static int handle_create_group_vt(struct control_client *client) {
	struct control_create_group_vt_request request;
	if (connection_get(&client->connection, &request, sizeof(request)) == -1) {
		return 0;
	}

	request.user[sizeof(request.user) - 1] = '\0';
	request.session[sizeof(request.session) - 1] = '\0';

	int ret = 0;
	struct client *owner = control_find_owner_client(client->server, request.owner_pid);
	if (owner == NULL || owner->seat == NULL) {
		ret = control_send_error(client, ESRCH);
		return ret;
	}

	int vt = seat_create_group_vt(owner->seat, owner, request.vt, request.user,
				      request.session);
	if (vt == -1) {
		return control_send_error(client, errno);
	}

	return control_send_group_vt_created(client, owner->pid, vt);
}

static int handle_destroy_group_vt(struct control_client *client) {
	struct control_destroy_group_vt_request request;
	if (connection_get(&client->connection, &request, sizeof(request)) == -1) {
		return 0;
	}

	struct seat *seat = server_get_seat(client->server, "seat0");
	if (seat == NULL) {
		return control_send_error(client, ENOENT);
	}
	pid_t owner_pid = -1;
	if (seat_get_group_vt_owner_pid(seat, request.vt, &owner_pid) == -1) {
		return control_send_error(client, errno);
	}
	if (owner_pid != client->pid) {
		return control_send_error(client, EPERM);
	}
	if (seat_destroy_group_vt(seat, request.vt) == -1) {
		return control_send_error(client, errno);
	}
	return 0;
}

static int control_client_handle_opcode(struct control_client *client, uint16_t opcode,
					uint16_t size) {
	switch (opcode) {
	case CONTROL_CREATE_GROUP_VT:
		if (size != sizeof(struct control_create_group_vt_request)) {
			return control_send_error(client, EPROTO);
		}
		return handle_create_group_vt(client);
	case CONTROL_DESTROY_GROUP_VT:
		if (size != sizeof(struct control_destroy_group_vt_request)) {
			return control_send_error(client, EPROTO);
		}
		return handle_destroy_group_vt(client);
	default:
		return control_send_error(client, EPROTO);
	}
}

static int control_client_handle_connection(int fd, uint32_t mask, void *data) {
	(void)fd;

	struct control_client *client = data;
	if (mask & EVENT_ERROR) {
		goto fail;
	}
	if (mask & EVENT_HANGUP) {
		goto fail;
	}

	if (mask & EVENT_WRITABLE) {
		int len = connection_flush(&client->connection);
		if (len == -1 && errno != EAGAIN) {
			goto fail;
		}
		if (len >= 0 &&
		    event_source_fd_update(client->event_source, EVENT_READABLE) == -1) {
			goto fail;
		}
	}

	if (mask & EVENT_READABLE) {
		int len = connection_read(&client->connection);
		if (len == -1 && errno != EAGAIN) {
			goto fail;
		}
		if (len == 0) {
			goto fail;
		}

		struct control_header header;
		while (connection_get(&client->connection, &header, sizeof(header)) != -1) {
			if (connection_pending(&client->connection) < header.size) {
				connection_restore(&client->connection, sizeof(header));
				break;
			}
			if (control_client_handle_opcode(client, header.opcode, header.size) == -1) {
				goto fail;
			}
		}
	}

	return 0;

fail:
	control_client_destroy(client);
	return -1;
}

int control_handle_connection(int fd, uint32_t mask, void *data) {
	struct server *server = data;
	if (mask & (EVENT_ERROR | EVENT_HANGUP)) {
		log_error("Control socket received an error");
		return -1;
	}
	if ((mask & EVENT_READABLE) == 0) {
		return 0;
	}

	int new_fd = accept(fd, NULL, NULL);
	if (new_fd == -1) {
		log_errorf("Could not accept control connection: %s", strerror(errno));
		return 0;
	}
	if (prepare_control_fd(new_fd) != 0) {
		log_errorf("Could not prepare control connection: %s", strerror(errno));
		close(new_fd);
		return 0;
	}

	pid_t pid = -1;
	uid_t uid = (uid_t)-1;
	gid_t gid = (gid_t)-1;
	if (get_peer(new_fd, &pid, &uid, &gid) == -1) {
		log_errorf("Could not query control peer credentials: %s", strerror(errno));
		close(new_fd);
		return 0;
	}
	if (uid != geteuid()) {
		log_errorf("Rejecting control peer pid %d with uid %d", pid, uid);
		close(new_fd);
		return 0;
	}

	struct control_client *client = calloc(1, sizeof(*client));
	if (client == NULL) {
		close(new_fd);
		return 0;
	}

	client->server = server;
	client->connection.fd = new_fd;
	client->pid = pid;
	client->uid = uid;
	client->gid = gid;
	linked_list_insert(&server->control_clients, &client->link);
	client->event_source = poller_add_fd(&server->poller, new_fd, EVENT_READABLE,
					     control_client_handle_connection, client);
	if (client->event_source == NULL) {
		log_errorf("Could not add control client socket to poller: %s", strerror(errno));
		control_client_destroy(client);
		return 0;
	}

	log_infof("New control client connected (pid: %d, uid: %d, gid: %d)", pid, uid, gid);
	return 0;
}

void control_broadcast_vt_change(struct server *server, int old_vt, int new_vt) {
	struct linked_list *elem = server->control_clients.next;
	while (elem != &server->control_clients) {
		struct control_client *client = (struct control_client *)elem;
		elem = elem->next;
		if (control_send_vt_change(client, old_vt, new_vt) == -1) {
			control_client_destroy(client);
		}
	}
}
