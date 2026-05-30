dde-seatd-launch(1)

# NAME

dde-seatd-launch - Start a process with its own dde-seatd instance

# SYNOPSIS

*dde-seatd-launch* [options] [--] command

# OPTIONS

*-l <loglevel>*
	Log-level to pass to dde-seatd. See *dde-seatd*(1) for information about
	available log-levels.

*-h*
	Show help message and quit.

*-v*
	Show the version number and quit.

# DESCRIPTION

dde-seatd-launch starts a dde-seatd instance with a dedicated socket path, waits for it
to be ready, and starts the specified command with SEATD_SOCK set
appropriately. Once the specified command terminates, the dde-seatd instance is
also terminated.

dde-seatd requires root privileges to perform its tasks. This can be achieved
through SUID of dde-seatd-launch or by running dde-seatd-launch as root. dde-seatd-launch
will drop privileges from the effective user to the real user before running
the specified command. If the real user is root, this is simply a noop. You
should only run dde-seatd-launch as root if you intend for the specified command to
run as root as well.

dde-seatd-launch serves a similar purpose to the libseat "builtin" backend, but is
superior to it for two reasons:
. The specified command never runs as root
. The standard dde-seatd executable and DDE libseat-compatible backend is used

# EXIT STATUS

dde-seatd-launch exits with the status of its child. When the child terminates on
a signal _N_, dde-seatd-launch exits with the status 128 + _N_.

If dde-seatd-launch fails because of another error, it exits with a non-zero
status.

# SEE ALSO

The DDE libseat-compatible library, *<libseat.h>*, *dde-seatd*(1)

# AUTHORS

Maintained by Kenny Levinsen <contact@kl.wtf>, who is assisted by other
open-source contributors. For more information about seatd development, see
https://sr.ht/~kennylevinsen/seatd.
