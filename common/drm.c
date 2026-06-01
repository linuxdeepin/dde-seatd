#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#if defined(__has_include)
#if __has_include(<drm/drm.h>)
#include <drm/drm.h>
#elif __has_include(<libdrm/drm.h>)
#include <libdrm/drm.h>
#else
#define DDE_SEATD_NEEDS_DRM_FALLBACK 1
#endif
#else
#define DDE_SEATD_NEEDS_DRM_FALLBACK 1
#endif

#ifdef DDE_SEATD_NEEDS_DRM_FALLBACK
typedef uint32_t drm_magic_t;

struct drm_auth {
	drm_magic_t magic;
};

#define DRM_IOCTL_BASE 'd'
#define DRM_IO(nr) _IO(DRM_IOCTL_BASE, nr)
#define DRM_IOW(nr, type) _IOW(DRM_IOCTL_BASE, nr, type)
#define DRM_IOCTL_AUTH_MAGIC DRM_IOW(0x11, struct drm_auth)
#define DRM_IOCTL_SET_MASTER DRM_IO(0x1e)
#define DRM_IOCTL_DROP_MASTER DRM_IO(0x1f)
#endif

#include "drm.h"

#define STRLEN(s)                 ((sizeof(s) / sizeof(s[0])) - 1)
#define STR_HAS_PREFIX(prefix, s) (strncmp(prefix, s, STRLEN(prefix)) == 0)

int drm_set_master(int fd) {
	return ioctl(fd, DRM_IOCTL_SET_MASTER, 0);
}

int drm_drop_master(int fd) {
	return ioctl(fd, DRM_IOCTL_DROP_MASTER, 0);
}

int drm_is_master(int fd) {
	struct drm_auth auth = {
		.magic = 0,
	};

	if (ioctl(fd, DRM_IOCTL_AUTH_MAGIC, &auth) == -1) {
		if (errno == EACCES) {
			return 0;
		}
		if (errno == EINVAL) {
			return 1;
		}
		return -1;
	}

	return 1;
}

#if defined(__linux__) || defined(__NetBSD__)
int path_is_drm(const char *path) {
	if (STR_HAS_PREFIX("/dev/dri/", path))
		return 1;
	return 0;
}
#elif defined(__FreeBSD__)
int path_is_drm(const char *path) {
	if (STR_HAS_PREFIX("/dev/dri/", path))
		return 1;
	/* Some drivers have /dev/dri/X symlinked to /dev/drm/X */
	if (STR_HAS_PREFIX("/dev/drm/", path))
		return 1;
	return 0;
}
#else
#error Unsupported platform
#endif
