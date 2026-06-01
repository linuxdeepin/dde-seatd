dde-seatd(1)

# NAME

dde-seatd - A seat management daemon

# SYNOPSIS

*dde-seatd* [options]

# OPTIONS

*-h*
	Show help message and quit.

*-n <fd>*
	FD to notify readiness on. A single newline will be written and the fd
	closed when seatd is ready to serve requests. This is compatible with
	s6's notification protocol.

*-s <path>*
	Override the libseat-compatible socket path. If not specified, dde-seatd
	uses the compile-time default path.

*-c <path>*
	Enable the DDE control socket at the specified path.

*-u <user>*
	User to own the seatd socket.

*-g <group>*
	Group to own the seatd socket.

*-l <loglevel>*
	Log-level to use. Must be one of debug, info, error or silent. Defaults
	to error.

*-v*
	Show the version number and quit.

# DESCRIPTION

dde-seatd provides central seat management, mediating access to shared resources
such as displays and input devices in a multi-session, multi-seat environment.

seatd operates over a UNIX domain socket, with *libseat* providing the
client-side of the protocol.

The libseat-compatible socket path defaults to the compile-time location, but
can be overridden with *-s*. When *-c* is specified, dde-seatd also exposes a
separate DDE control socket used by DDM to manage grouped VTs.

# ENVIRONMENT

*SEATD_VTBOUND*
	If set to "0", the seat will not be bound to a VT.

# SEE ALSO

The DDE libseat-compatible library, *<libseat.h>*, *dde-seatd-launch*(1)

# AUTHORS

Maintained by Kenny Levinsen <contact@kl.wtf>, who is assisted by other
open-source contributors. For more information about seatd development, see
https://sr.ht/~kennylevinsen/seatd.
