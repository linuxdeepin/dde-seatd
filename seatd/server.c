#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/vt.h>
#include <sys/ioctl.h>
#endif

#include "client.h"
#include "control.h"
#include "log.h"
#include "poller.h"
#include "seat.h"
#include "server.h"
#include "terminal.h"

static int server_handle_vt_acq(int signal, void *data);
static int server_handle_vt_rel(int signal, void *data);
static int server_handle_kill(int signal, void *data);
#if defined(__linux__)
static int server_handle_vt_event(int fd, uint32_t mask, void *data);
static void *server_vt_event_loop(void *data);
static int create_vt_event_pipe(int pipefd[2]);
#endif
static int set_cloexec(int fd);
static int set_nonblock(int fd);

int server_init(struct server *server) {
	if (poller_init(&server->poller) == -1) {
		log_errorf("could not initialize poller: %s", strerror(errno));
		return -1;
	}

	linked_list_init(&server->seats);
	linked_list_init(&server->idle_clients);
	linked_list_init(&server->control_clients);
	server->vt_event_fd = -1;
	server->vt_event_pipe[0] = -1;
	server->vt_event_pipe[1] = -1;
	server->vt_event_thread_started = false;
	atomic_store(&server->vt_event_running, false);

	if (poller_add_signal(&server->poller, SIGUSR1, server_handle_vt_rel, server) == NULL ||
	    poller_add_signal(&server->poller, SIGUSR2, server_handle_vt_acq, server) == NULL ||
	    poller_add_signal(&server->poller, SIGINT, server_handle_kill, server) == NULL ||
	    poller_add_signal(&server->poller, SIGTERM, server_handle_kill, server) == NULL) {
		server_finish(server);
		return -1;
	}

	char *vtenv = getenv("SEATD_VTBOUND");

	// TODO: create more seats:
	struct seat *seat = seat_create("seat0", vtenv == NULL || strcmp(vtenv, "1") == 0);
	if (seat == NULL) {
		server_finish(server);
		return -1;
	}

	linked_list_insert(&server->seats, &seat->link);

#if defined(__linux__)
	server->vt_event_fd = terminal_open(0);
	if (server->vt_event_fd != -1 && create_vt_event_pipe(server->vt_event_pipe) == 0) {
		if (set_nonblock(server->vt_event_pipe[0]) == 0 &&
		    poller_add_fd(&server->poller, server->vt_event_pipe[0], EVENT_READABLE,
				  server_handle_vt_event, server) != NULL) {
			atomic_store(&server->vt_event_running, true);
			if (pthread_create(&server->vt_event_thread, NULL, server_vt_event_loop,
					   server) != 0) {
				atomic_store(&server->vt_event_running, false);
				log_errorf("Could not start VT event thread: %s", strerror(errno));
			} else {
				server->vt_event_thread_started = true;
			}
		}
	} else {
		log_info("VT event watcher unavailable; relying on VT signals only");
	}
#endif

	server->running = true;
	return 0;
}

void server_finish(struct server *server) {
	assert(server);
#if defined(__linux__)
	atomic_store(&server->vt_event_running, false);
	if (server->vt_event_thread_started) {
		pthread_cancel(server->vt_event_thread);
		pthread_join(server->vt_event_thread, NULL);
		server->vt_event_thread_started = false;
	}
	if (server->vt_event_fd != -1) {
		close(server->vt_event_fd);
		server->vt_event_fd = -1;
	}
	if (server->vt_event_pipe[0] != -1) {
		close(server->vt_event_pipe[0]);
		server->vt_event_pipe[0] = -1;
	}
	if (server->vt_event_pipe[1] != -1) {
		close(server->vt_event_pipe[1]);
		server->vt_event_pipe[1] = -1;
	}
#endif
	while (!linked_list_empty(&server->control_clients)) {
		struct control_client *client = (struct control_client *)server->control_clients.next;
		linked_list_remove(&client->link);
		if (client->event_source != NULL) {
			event_source_fd_destroy(client->event_source);
		}
		connection_close_fds(&client->connection);
		close(client->connection.fd);
		free(client);
	}
	while (!linked_list_empty(&server->idle_clients)) {
		struct client *client = (struct client *)server->idle_clients.next;
		client_destroy(client);
	}
	while (!linked_list_empty(&server->seats)) {
		struct seat *seat = (struct seat *)server->seats.next;
		seat_destroy(seat);
	}
	poller_finish(&server->poller);
}

struct seat *server_get_seat(struct server *server, const char *seat_name) {
	for (struct linked_list *elem = server->seats.next; elem != &server->seats;
	     elem = elem->next) {
		struct seat *seat = (struct seat *)elem;
		if (strcmp(seat->seat_name, seat_name) == 0) {
			return seat;
		}
	}
	return NULL;
}

static int server_handle_vt_acq(int signal, void *data) {
	(void)signal;
	struct server *server = data;
	struct seat *seat = server_get_seat(server, "seat0");
	if (seat == NULL) {
		return -1;
	}

	seat_vt_activate(seat);
	return 0;
}

static int server_handle_vt_rel(int signal, void *data) {
	(void)signal;
	struct server *server = data;
	struct seat *seat = server_get_seat(server, "seat0");
	if (seat == NULL) {
		return -1;
	}

	seat_vt_release(seat);
	return 0;
}

static int server_handle_kill(int signal, void *data) {
	(void)signal;
	struct server *server = data;
	server->running = false;
	return 0;
}

static int set_nonblock(int fd) {
	int flags;
	if ((flags = fcntl(fd, F_GETFD)) == -1 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
		log_errorf("Could not set FD_CLOEXEC on socket: %s", strerror(errno));
		return -1;
	}
	if ((flags = fcntl(fd, F_GETFL)) == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
		log_errorf("Could not set O_NONBLOCK on socket: %s", strerror(errno));
		return -1;
	}
	return 0;
}

static int set_cloexec(int fd) {
	int flags;
	if ((flags = fcntl(fd, F_GETFD)) == -1 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
		log_errorf("Could not set FD_CLOEXEC on socket: %s", strerror(errno));
		return -1;
	}
	return 0;
}

struct vt_event_msg {
	int old_vt;
	int new_vt;
};

#if defined(__linux__)
static int create_vt_event_pipe(int pipefd[2]) {
#if defined(O_CLOEXEC)
	if (pipe2(pipefd, O_CLOEXEC) == 0) {
		return 0;
	}
	if (errno != ENOSYS && errno != EINVAL) {
		return -1;
	}
#endif

	if (pipe(pipefd) == -1) {
		return -1;
	}
	if (set_cloexec(pipefd[0]) == -1 || set_cloexec(pipefd[1]) == -1) {
		close(pipefd[0]);
		close(pipefd[1]);
		pipefd[0] = -1;
		pipefd[1] = -1;
		return -1;
	}
	return 0;
}

static void *server_vt_event_loop(void *data) {
	struct server *server = data;
	while (atomic_load(&server->vt_event_running)) {
		struct vt_event event = {
			.event = VT_EVENT_SWITCH,
		};
		if (ioctl(server->vt_event_fd, VT_WAITEVENT, &event) == -1) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		if ((event.event & VT_EVENT_SWITCH) == 0) {
			continue;
		}
		struct vt_event_msg msg = {
			.old_vt = (int)event.oldev,
			.new_vt = (int)event.newev,
		};
		/*
		 * Keep the writer blocking so VT switch events are not dropped when the
		 * main thread drains the read side a bit later.
		 */
		if (write(server->vt_event_pipe[1], &msg, sizeof(msg)) == -1) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
	}
	return NULL;
}

static int server_handle_vt_event(int fd, uint32_t mask, void *data) {
	struct server *server = data;
	if (mask & (EVENT_ERROR | EVENT_HANGUP)) {
		return -1;
	}
	if ((mask & EVENT_READABLE) == 0) {
		return 0;
	}

	struct vt_event_msg msg;
	while (read(fd, &msg, sizeof(msg)) == (ssize_t)sizeof(msg)) {
		struct seat *seat = server_get_seat(server, "seat0");
		if (seat != NULL) {
			seat_handle_vt_event(seat, msg.old_vt, msg.new_vt);
		}
	}
	return 0;
}
#endif

int server_add_client(struct server *server, int fd) {
	if (set_nonblock(fd) != 0) {
		log_errorf("Could not prepare new client socket: %s", strerror(errno));
		close(fd);
		return -1;
	}

	struct client *client = client_create(server, fd);
	if (client == NULL) {
		log_errorf("Could not create client: %s", strerror(errno));
		close(fd);
		return -1;
	}

	client->event_source =
		poller_add_fd(&server->poller, fd, EVENT_READABLE, client_handle_connection, client);
	if (client->event_source == NULL) {
		log_errorf("Could not add client socket to poller: %s", strerror(errno));
		client_destroy(client);
		return -1;
	}
	log_infof("New client connected (pid: %d, uid: %d, gid: %d)", client->pid, client->uid,
		  client->gid);
	return 0;
}

int server_handle_connection(int fd, uint32_t mask, void *data) {
	struct server *server = data;
	if (mask & (EVENT_ERROR | EVENT_HANGUP)) {
		shutdown(fd, SHUT_RDWR);
		server->running = false;
		log_error("Server socket received an error");
		return -1;
	}

	if (mask & EVENT_READABLE) {
		int new_fd = accept(fd, NULL, NULL);
		if (new_fd == -1) {
			log_errorf("Could not accept client connection: %s", strerror(errno));
			return 0;
		}

		if (server_add_client(server, new_fd) == -1) {
			return 0;
		}
	}
	return 0;
}
