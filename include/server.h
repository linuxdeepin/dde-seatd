#ifndef _SEATD_SERVER_H
#define _SEATD_SERVER_H

#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>

#include "linked_list.h"
#include "poller.h"

struct client;

struct server {
	bool running;
	struct poller poller;

	struct linked_list seats;
	struct linked_list idle_clients;
	struct linked_list control_clients;

	atomic_bool vt_event_running;
	bool vt_event_thread_started;
	int vt_event_fd;
	int vt_event_pipe[2];
	pthread_t vt_event_thread;
};

int server_init(struct server *server);
void server_finish(struct server *server);

struct seat *server_get_seat(struct server *server, const char *seat_name);

int server_handle_connection(int fd, uint32_t mask, void *data);
int server_add_client(struct server *server, int fd);

#endif
