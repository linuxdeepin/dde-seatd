#ifndef _SEATD_SEAT_H
#define _SEATD_SEAT_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "linked_list.h"

struct client;

enum seat_device_type {
	SEAT_DEVICE_TYPE_NORMAL,
	SEAT_DEVICE_TYPE_EVDEV,
	SEAT_DEVICE_TYPE_HIDRAW,
	SEAT_DEVICE_TYPE_DRM,
	SEAT_DEVICE_TYPE_WSCONS,
};

struct seat_device {
	struct linked_list link; // client::devices
	int device_id;
	int fd;
	int ref_cnt;
	bool active;
	char *path;
	enum seat_device_type type;
};

struct seat {
	struct linked_list link; // server::seats
	char *seat_name;
	struct linked_list clients;
	struct client *active_client;
	struct client *next_client;

	bool vt_bound;
	int cur_vt;
	int pending_vt_switch;
	struct linked_list group_vts;
};

struct seat_group_vt {
	struct linked_list link; // seat::group_vts
	struct client *owner;
	int vt;
	char *user;
	char *session;
};

struct seat *seat_create(const char *name, bool vt_bound);
void seat_destroy(struct seat *seat);

int seat_add_client(struct seat *seat, struct client *client);
void seat_remove_client(struct client *client);
int seat_open_client(struct seat *seat, struct client *client);
int seat_ack_disable_client(struct client *client);

struct seat_device *seat_open_device(struct client *client, const char *path);
void seat_close_device(struct client *client, struct seat_device *seat_device);
struct seat_device *seat_find_device(struct client *client, int device_id);

int seat_set_next_session(struct client *client, int session);
int seat_vt_activate(struct seat *seat);
int seat_vt_release(struct seat *seat);
int seat_create_group_vt(struct seat *seat, struct client *owner, int requested_vt,
			 const char *user, const char *session);
int seat_destroy_group_vt(struct seat *seat, int vt);
int seat_get_group_vt_owner_pid(struct seat *seat, int vt, pid_t *owner_pid);
void seat_handle_vt_event(struct seat *seat, int old_vt, int new_vt);

#endif
