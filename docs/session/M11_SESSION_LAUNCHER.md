# Milestone 11 Session Launcher

`glasswyrm-session` is the installed, unprivileged orchestrator for the
Milestone 11 three-process desktop. It starts `gwm`, `gwcomp`, and
`glasswyrmd` in dependency order, then optionally starts one initial X11
client. The three runtime executables are resolved through `PATH` and are
executed directly; the launcher never invokes a shell.

The launcher is installed only when all three runtime components are enabled.
It does not acquire devices, change ownership or permissions, or act as a
session broker. Its caller must already be able to open the selected DRM
primary node, VT, and input devices.

## Command line

```text
glasswyrm-session
  --runtime-dir PATH
  --display N
  --drm-device PATH
  --tty PATH
  --connector NAME
  --mode WIDTHxHEIGHT[@MILLIHZ]
  --input-device PATH ...
  [--xkb-layout NAME]
  [--xkb-model NAME]
  [--xkb-variant NAME]
  [--xkb-options LIST]
  [--drm-api auto|atomic|legacy]
  [--mirror-dump-dir PATH]
  [--scene-manifest PATH]
  [--drm-report PATH]
  [--x11-trace PATH]
  [--client PROGRAM ARG...]
```

`--client` consumes the remainder of the command line. Client arguments are
preserved exactly, including whitespace and shell metacharacters. The client
receives `DISPLAY=:N` while inheriting the launcher's remaining environment.

For example:

```sh
glasswyrm-session \
  --runtime-dir /run/glasswyrm \
  --display 99 \
  --drm-device /dev/dri/card0 \
  --tty /dev/tty2 \
  --connector Virtual-1 \
  --mode 1024x768@60000 \
  --input-device /dev/input/event4 \
  --input-device /dev/input/event5 \
  --drm-api atomic \
  --mirror-dump-dir /var/tmp/glasswyrm-mirror \
  --scene-manifest /var/tmp/glasswyrm-scenes.jsonl \
  --drm-report /var/tmp/glasswyrm-drm.jsonl \
  --x11-trace /var/tmp/glasswyrm-x11.jsonl \
  --client xterm -geometry 80x24+80+80
```

## Runtime and readiness

The absolute runtime directory is created with mode `0700`. An existing
directory is accepted only when it is owned by the effective user and already
has mode `0700`; symlinks and occupied socket paths are rejected. The fixed
private peer paths are:

```text
PATH/gwm.sock
PATH/gwcomp.sock
```

Startup proceeds only after each dependency exposes its socket:

1. start `gwm`, then wait for `PATH/gwm.sock`;
2. start DRM `gwcomp`, then wait for `PATH/gwcomp.sock`;
3. start real-input, software-content `glasswyrmd`, then wait for
   `/tmp/.X11-unix/XN`;
4. start the optional initial client.

Each readiness wait has a ten-second monotonic deadline. A process exit,
spawn error, or timeout aborts startup and returns a nonzero status.
If a readiness path already exists when its process is spawned, the launcher
waits for that path to be replaced before accepting it. This prevents a socket
left by an interrupted session from releasing the next dependency early.

## Supervision and shutdown

The launcher remains in the foreground. A required process exit is a session
failure even when that process reports status zero. A successful optional
client exit ends the session successfully; a failed client status is returned.

`SIGINT` and `SIGTERM` are forwarded in reverse dependency order: client,
`glasswyrmd`, `gwcomp`, then `gwm`. Each child receives a bounded grace period
before `SIGKILL`, and every child is reaped. This preserves compositor-owned
DRM/VT restoration and server-owned input teardown without leaving zombie
processes. The launcher itself never opens the DRM, VT, or input devices.
