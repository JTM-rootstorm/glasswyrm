# Milestone 10 DRM/KMS Backend

## Build and dependency boundary

The backend is Linux-only and opt-in:

```sh
meson setup build-drm -Ddrm_backend=true -Dwerror=true
meson compile -C build-drm
meson test -C build-drm --print-errorlogs
```

`-Ddrm_backend=true` requires `-Dgwcomp=true`,
`-Drender_software=true`, and libdrm with KMS, atomic, page-flip, AddFB2, and
dumb-buffer declarations and link symbols. Meson validates those APIs rather
than claiming a minimum version number that it does not enforce. Mesa, GBM,
EGL, OpenGL, Vulkan, Xorg, Wayland, and libinput are not dependencies.

The default is `drm_backend=false`; that configuration must neither discover
nor link libdrm. `headless_backend` and `drm_backend` can be enabled together.
A DRM-only compositor build uses:

```sh
meson setup build-drm-only \
  -Dheadless_backend=false -Ddrm_backend=true -Dwerror=true
```

## Command line

Direct VT/device ownership:

```sh
./build-drm/src/gwcomp \
  --backend drm \
  --ipc-socket /run/glasswyrm/gwcomp.sock \
  --drm-device /dev/dri/card0 \
  --tty /dev/tty2 \
  --connector Virtual-1 \
  --mode 1024x768 \
  --drm-api auto \
  --mirror-dump-dir /var/tmp/glasswyrm-mirror \
  --drm-report /var/tmp/glasswyrm-drm.jsonl
```

`--mode` accepts `WIDTHxHEIGHT` or `WIDTHxHEIGHT@MILLIHZ`. The options
`--connector`, `--mode`, `--mirror-dump-dir`, and `--drm-report` are optional.
An inherited session uses `--drm-fd N --external-session` and forbids `--tty`.
`--drm-device` and `--drm-fd` are mutually exclusive. `--dump-dir` remains
headless-only.

## Discovery and deterministic selection

Only Linux DRM primary nodes are eligible; render nodes are rejected. The real
device layer opens with `O_RDWR|O_CLOEXEC`, verifies a character device and
primary-node identity, queries driver metadata and `DRM_CAP_DUMB_BUFFER`, then
enumerates connector, encoder/CRTC, mode, and plane state. Universal-plane and
atomic client capabilities are requested only when the configured API permits
them.

Automatic device discovery considers canonical `/dev/dri/card*` primary-node
paths in deterministic order and rejects ambiguity. Connector selection:

1. requires `DRM_MODE_CONNECTED`;
2. rejects writeback and `non-desktop` connectors;
3. requires at least one mode with the compositor's exact width and height;
4. requires a compatible CRTC;
5. rejects an already cloned CRTC route; and
6. rejects automatic ambiguity rather than guessing.

An explicit connector name must identify that same eligible route. With no
`--mode`, the chosen connector's preferred mode wins, followed by larger pixel
area, higher refresh, stable mode name, and clock. With `--mode WIDTHxHEIGHT`,
selection uses exact dimensions and prefers the advertised preferred mode,
then higher refresh, stable mode name, and clock. With an explicit refresh, the
selected mode must be within 1000 millihertz. M10 does not scale between
compositor and scanout dimensions.

## Format, buffers, and limits

The only scanout format is linear `DRM_FORMAT_XRGB8888`. Exactly two KMS dumb
buffers are created with `DRM_IOCTL_MODE_CREATE_DUMB`, registered with
`drmModeAddFB2`, mapped through `DRM_IOCTL_MODE_MAP_DUMB` and `mmap`, and fully
initialized including pitch padding. The canonical visible pixels are copied
in full for every presentation. Damage is evidence metadata, not a partial
scanout-copy optimization.

The software-frame limits remain 4096 by 4096 and 64 MiB. Dimensions,
pitch/size values, and mapping arithmetic are checked. A visible scanout hash
must equal the canonical software-frame hash before submission.

## Presentation state machine

```text
software frame
  -> full copy and hash check
  -> first frame: blocking modeset -> complete
  -> later frame: submit one page flip -> pending
  -> matching DRM event -> verify token/CRTC/sequence
  -> publish report and mirror evidence
  -> promote scene, release buffers, acknowledge producer
```

Only one page flip may be pending. While it is pending, the candidate scene,
attachments, release set, mirror PPM, report record, and software frame remain
uncommitted. The producer socket may be polled but no new compositor contract
is dequeued. A two-second timeout, event error, HUP, wrong token, wrong CRTC,
or missing completion is fatal. The kernel page-flip sequence is diagnostic:
drivers without usable vblank accounting may deliver zero with a valid matching
completion. Connector loss is fatal in M10; hotplug recovery is not implemented.

When a buffered ProtocolServer configures `--scene-manifest`, its serialized
scene record is staged with the presentation. Synchronous modesets publish it
after backend completion; page flips publish it only after the matching event
and backend diagnostics finalize. Publication precedes logical scene promotion
and producer acknowledgement. Rejection, timeout, abandonment, or backend
failure publishes no scene record, and a manifest publication failure is fatal
without releasing the previous committed buffers.

A timeout cannot cancel an already submitted kernel flip. Glasswyrm therefore
keeps the abandoned callback record alive until a late event is consumed or
the DRM FD closes, while discarding all staged compositor and evidence state.

## Diagnostics and reports

`--mirror-dump-dir` publishes the same PPM/manifest shape used by headless
evidence, but only after presentation succeeds. `--drm-report` writes
deterministic JSON-lines records for:

- discovery and selected device capabilities;
- connector, CRTC, plane, mode, buffer geometry, API, and session ownership;
- initial modeset and each completed page flip, including canonical/scanout
  hashes and page-flip sequence;
- VT release/acquire;
- restoration results; and
- fatal stage and bounded reason.

Report and mirror paths use staged publication so unpresented frames do not
appear as accepted evidence. The report's parent directory must already exist
and its target path must not exist; mirror directories may be created, but the
mirror target itself must not be a symbolic link. `gw_drm_probe` is a read-only helper
for discovery and before/active/after KMS snapshots:

```sh
./build-drm/tools/gw_drm_probe \
  --device /dev/dri/card0 --connector Virtual-1 \
  --require-mode 1024x768 --snapshot-state \
  --output /tmp/kms.json
```

Fatal initialization errors exit nonzero before accepting compositor work
where practical. Post-modeset failures stop producer consumption, attempt the
ordered KMS/VT restoration path, record each result, and exit nonzero.

## Deliberate boundary

M10 does not provide acceleration, real input, multiple outputs, clones,
scaling, rotation, cursor or overlay planes, dynamic mode changes, hotplug
recovery, DMA-BUF scanout, format modifiers, explicit synchronization, DPMS,
VRR, HDR, color management, leases, or X11 RANDR.
