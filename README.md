# dde-seatd, seatd and libseat

## DDE fork

`dde-seatd` is a DDE-specific `seatd` daemon fork for DDM + Treeland. It keeps
the seatd/libseat protocol compatible with upstream, but installs a daemon named
`dde-seatd` and defaults its socket to `/run/dde-seatd.sock` so it can coexist
with a stock `seatd.service` using `/run/seatd.sock`.

The fork keeps the upstream install surface, but renames the conflicting
artifacts so it can coexist with stock `seatd`: the helper binary is installed
as `dde-seatd-launch`, the library/pkg-config pair use DDE-specific names, and
the public header is installed under `include/dde-seatd/`.
For Treeland or other wlroots compositors that should talk to this daemon, keep
using the normal libseat seatd backend and point it at the DDE socket:

```bash
LIBSEAT_BACKEND=seatd SEATD_SOCK=/run/dde-seatd.sock treeland
```

The behavioral extension is a separate DDE control socket used by DDM:

- `-s <path>` selects the libseat-compatible socket.
- `-c <path>` enables the DDE control socket.
- DDM can register additional grouped VTs for an existing libseat client by
  passing the owner process pid over the control socket.
- The packaged `dde-seatd.service` starts the daemon with
  `-s /run/dde-seatd.sock -c /run/dde-seatd-control.sock` so DDM can always
  reach both sockets with the expected paths.

This lets a compositor keep DRM/input ownership for group-internal user VT
switches while `dde-seatd` still performs normal VT-bound disable/enable for
switches outside the group.

A minimal seat management daemon, and a universal seat management library.

Currently supports Linux and FreeBSD, and has experimental NetBSD support.

## What is seat management?

Seat management takes care of mediating access to shared devices (graphics, input), without requiring the applications needing access to be root.

## What's in the box?

### seatd

A seat management daemon, that does everything it needs to do. Nothing more, nothing less. Depends only on libc.

### libseat

A seat management library allowing applications to use whatever seat management is available.

Supports:
- seatd
- (e)logind
- embedded seatd for standalone operation

Each backend can be compile-time included and is runtime auto-detected or manually selected with the `LIBSEAT_BACKEND` environment variable.

Which backend is in use is transparent to the application, providing a simple common interface.

## Why not (e)logind?

systemd-logind is not portable, and being part of the systemd project, it cannot be used in an environment not based on or compatible with systemd.

elogind tries to isolate systemd-logind from systemd through brute-force. This requires actively fighting against upstream design decisions for deep integration, and the efforts must be repeated every time one syncs with upstream. And even after all this work, one is left with nothing but a hackjob.

Why spend time isolating logind and keeping up with upstream when we could instead create something better with less work?

## Why does libseat support (e)logind?

[In order to not be part of the problem](https://xkcd.com/927/). We will not displace systemd-logind anytime soon, so for user shells like [sway](https://github.com/swaywm/sway), seatd joins the ranks of logind and direct session management for things they need to support.

Instead of giving user shell developers more work, libseat aims to make supporting seatd less work than what they're currently implementing. This is done by taking care of all the seat management needs with multiple backends, providing not only seatd support, but replacing the existing logind and direct seat management implementations.

## How to discuss

Go to [#kennylevinsen @ irc.libera.chat](ircs://irc.libera.chat/#kennylevinsen) to discuss, or use [~kennylevinsen/seatd-devel@lists.sr.ht](https://lists.sr.ht/~kennylevinsen/seatd-devel).
