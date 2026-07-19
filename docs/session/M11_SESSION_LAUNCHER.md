# Milestone 11 Session Launcher

`glasswyrm-session` is the installed, unprivileged orchestrator for the
Milestone 11 three-process desktop. It starts `gwm`, `gwcomp`, and
`glasswyrmd` in dependency order, then optionally starts one initial X11
client. The three runtime executables are resolved through `PATH` and are
executed directly; the launcher never invokes a shell.

Milestone 12 extends the same launcher with opt-in game-profile and renderer
arguments. It does not create a second session mode or change device
ownership, readiness, supervision, or shutdown semantics.

Milestone 13 adds an explicit headless backend and output-model forwarding.
The default backend remains DRM, so an invocation that omits all M13 options
produces the historical DRM child command lines. The launcher still resolves
executables through `PATH`, calls them directly, and never invokes a shell.

Milestone 14 adds explicit VRR forwarding without changing device ownership or
supervision. Historical invocations omit all VRR arguments and produce the
same child command lines.

The launcher is installed only when all three runtime components are enabled.
It does not acquire devices, change ownership or permissions, or act as a
session broker. Its caller must already be able to open the selected DRM
primary node, VT, and input devices.

## Command line

```text
glasswyrm-session
  --runtime-dir PATH
  --display N
  [--backend drm|headless]
  [--output-model]
  [--control-socket PATH]
  [--scale-protocol]
  [--vrr-protocol]
  drm: --drm-device PATH --tty PATH --connector NAME
       --mode WIDTHxHEIGHT[@MILLIHZ] --input-device PATH ...
  headless: [--headless-output NAME[:WIDTHxHEIGHT[@MILLIHZ]]] ...
            [--headless-vrr NAME=MIN-MILLIHZ-MAX-MILLIHZ] ...
  [--xkb-layout NAME]
  [--xkb-model NAME]
  [--xkb-variant NAME]
  [--xkb-options LIST]
  [--drm-api auto|atomic|legacy]
  [--mirror-dump-dir PATH]
  [--scene-manifest PATH]
  [--drm-report PATH]
  [--vrr-report PATH]
  [--x11-trace PATH]
  [--game-compat]
  [--disable-extension NAME] ...
  [--renderer software|gles|auto]
  [--renderer-report PATH]
  [--client PROGRAM ARG...]
```

`--client` consumes the remainder of the command line. Client arguments are
preserved exactly, including whitespace and shell metacharacters. The client
receives `DISPLAY=:N` while inheriting the launcher's remaining environment.

The renderer defaults to `software`. `--renderer gles` is a forced selection
and fails if the compositor build or EGL initialization cannot provide it;
`auto` may select the documented fallback and records the reason when a report
path is supplied. The renderer-report target must satisfy `gwcomp`'s secure
new-file rules.

`--game-compat` forwards the opt-in Milestone 12 server profile. Each
`--disable-extension NAME` is forwarded in original order and is rejected
without `--game-compat`. The server itself validates names against the static
registry. These arguments require an experimental build; historical launcher
invocations continue to start the software-content profile without extensions.

`--backend headless` forbids DRM device, TTY, connector, mode, DRM API, mirror
dump, and DRM report options. Repeatable `--headless-output` values are
forwarded in order to `gwcomp`; without one, the compositor selects its
historical default output. Headless sessions do not require input devices.

`--output-model` forwards the M13 profile to `glasswyrmd`. Unless
`--control-socket` supplies an absolute or otherwise valid explicit Unix-socket
path, the launcher uses `PATH/control.sock` inside its private runtime
directory. `--control-socket` and `--scale-protocol` both require
`--output-model`; the scale protocol additionally requires an experimental
build. `--game-compat` may be combined with the M13 options.

`--vrr-protocol` also requires `--output-model` and an experimental build.
Repeatable `--headless-vrr` values require the headless backend and a matching
named `--headless-output`; they configure simulation only and never imply a
physical capability. `--vrr-report` is forwarded to `gwcomp` for either
backend and must differ from `--drm-report`. Both reports use secure new-file
semantics.

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

An M12 software-renderer session uses the same device and process boundary:

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
  --game-compat \
  --renderer software \
  --renderer-report /var/tmp/glasswyrm-renderer.jsonl \
  --client /path/to/m12_sdl_probe
```

The fixed acceptance harness also launches a forced-GLES session and a
`--disable-extension MIT-SHM` fallback session. Those are evidence profiles,
not a recommendation to disable extensions in ordinary development runs.

An M13 two-output headless development session can be launched without DRM,
TTY, or input-device access:

```sh
glasswyrm-session \
  --runtime-dir /run/glasswyrm-headless \
  --display 99 \
  --backend headless \
  --headless-output LEFT:800x600@60000 \
  --headless-output RIGHT:800x600@60000 \
  --output-model \
  --scale-protocol \
  --renderer software \
  --client /path/to/x11-client
```

An M14 simulated session adds only explicit options and remains GPU/VT-free:

```sh
glasswyrm-session \
  --runtime-dir /run/glasswyrm-headless-vrr \
  --display 99 \
  --backend headless \
  --headless-output HEADLESS-1:1920x1080@60000 \
  --headless-vrr HEADLESS-1=40000-60000 \
  --output-model --vrr-protocol \
  --vrr-report /var/tmp/glasswyrm-headless-vrr.jsonl \
  --renderer software \
  --client /path/to/m14_vrr_client
```

## Runtime and readiness

The absolute runtime directory is created with mode `0700`. An existing
directory is accepted only when it is owned by the effective user and already
has mode `0700`; symlinks and occupied socket paths are rejected. The fixed
private peer paths are:

```text
PATH/gwm.sock
PATH/gwcomp.sock
PATH/control.sock    # generated for --output-model unless overridden
```

Startup proceeds only after each dependency exposes its socket:

1. start `gwm`, then wait for `PATH/gwm.sock`;
2. start the selected headless or DRM `gwcomp`, then wait for
   `PATH/gwcomp.sock`; in output-model mode, compositor readiness follows its
   validated inventory publication;
3. start software-content `glasswyrmd`, adding real input for a DRM session and
   requested game/output/scale profiles, then wait for the generated or
   explicit control socket when enabled and `/tmp/.X11-unix/XN`;
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
