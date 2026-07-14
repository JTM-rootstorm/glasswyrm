# M10 source-layout audit

Status: Final M10 implementation snapshot
Required baseline: `fe0faab39f7a6d28157ee6b96a4f6292a0b7984e`
Final source commit: `33aa503`
Inventory date: 2026-07-14

## Method and exact counts

This inventory covers every non-generated C/C++ source or header under `src/`
(`.c`, `.cc`, `.cpp`, `.cxx`, `.h`, `.hh`, `.hpp`, and
`.hxx`). Physical lines are counted as records, including blank and comment
lines, with `awk 'END { print NR }'`. “Before” is the required baseline commit.
The original per-file inventory below preserves the Phase 1 decomposition
snapshot at `b03cd37`; the final M10 delta inventory then records every source
or header added or changed between that snapshot and `fe2ddde`. Together the
two tables cover every final `src/` C/C++ file. A dash means the file did not
exist at the earlier snapshot.

| Snapshot | Files | Physical lines |
|---|---:|---:|
| Before (`fe0faab`) | 148 | 20970 |
| After Phase 1 | 190 | 21891 |
| Final M10 (`33aa503`) | 240 | 29483 |

The largest new Phase 1 file is
`src/glasswyrmd/request_handlers/lifecycle.cpp` at 390 lines. The largest final
M10 file is `src/tools/drm_probe.cpp` at 549 lines, followed by
`src/backends/drm/presenter.cpp` at 544 lines. Both remain below the 600-line
M10 limit and neither is allowlisted.

## Final M10 delta inventory

This table supersedes the Phase 1 “after” count and description for every path
listed here. Files not listed here are unchanged from the complete Phase 1
inventory below. “Dependencies” are direct project-local quoted includes.

| Path | Phase 1 | Final | Primary responsibility and major symbols | Direct internal dependencies | Final disposition |
|---|---:|---:|---|---|---|
| `src/backends/drm/connector_name.cpp` | - | 53 | Stable connector type, status, and instance naming; `connector_type_name`, `connection_status_name`, `connector_name` | `connector_name.hpp`, `resources.hpp` | new final DRM model |
| `src/backends/drm/connector_name.hpp` | - | 18 | Connector naming declarations | - | new narrow interface |
| `src/backends/drm/connector_selector.cpp` | - | 73 | Connected desktop connector eligibility and ambiguity rejection; `select_connector` | `connector_selector.hpp`, `connector_name.hpp` | new final selector |
| `src/backends/drm/connector_selector.hpp` | - | 37 | Connector selection status/result contract | `resources.hpp` | new narrow interface |
| `src/backends/drm/device.cpp` | - | 113 | Sole-owned DRM FD, direct/adopted session identity, and event-cookie lifetime; `Device` | `device.hpp` | new final device owner |
| `src/backends/drm/device.hpp` | - | 67 | RAII DRM device and session declaration | `drm_api.hpp` | new narrow interface |
| `src/backends/drm/drm_api.hpp` | - | 101 | Injectable device discovery and page-flip event boundary; `DrmApi`, `DeviceSnapshot`, `PageFlipCookie` | `resources.hpp` | new narrow interface |
| `src/backends/drm/drm_report.cpp` | - | 481 | Race-resistant staged JSON-lines evidence publication and serialization; `DrmReport`, `serialize_report_record` | `drm_report.hpp` | new final evidence module |
| `src/backends/drm/drm_report.hpp` | - | 171 | Typed discovery, selection, modeset, flip, VT, restore, and fatal report records | - | new narrow evidence interface |
| `src/backends/drm/dumb_buffer.cpp` | - | 242 | Checked XRGB8888 dumb-buffer allocation, full copy, hash, promotion, and cleanup; `DumbBuffer`, `DumbBufferPair` | `dumb_buffer.hpp`, `resources.hpp` | new final buffer owner |
| `src/backends/drm/dumb_buffer.hpp` | - | 88 | RAII scanout-buffer declarations | `dumb_buffer_api.hpp` | new narrow interface |
| `src/backends/drm/dumb_buffer_api.hpp` | - | 42 | Injectable dumb-buffer syscall boundary; `DumbBufferApi`, `DumbAllocation` | - | new narrow interface |
| `src/backends/drm/fake_drm_api.cpp` | - | 180 | Deterministic device discovery and page-flip event fake; `FakeDrmApi` | `fake_drm_api.hpp` | new test seam implementation |
| `src/backends/drm/fake_drm_api.hpp` | - | 70 | Configurable fake DRM device/event contract | `drm_api.hpp` | new test seam interface |
| `src/backends/drm/fake_kms_api.cpp` | - | 192 | Deterministic KMS state, operation log, and failure injection; `FakeKmsApi` | `fake_kms_api.hpp` | new test seam implementation |
| `src/backends/drm/fake_kms_api.hpp` | - | 87 | Configurable fake KMS contract and operation model | `kms_api.hpp` | new test seam interface |
| `src/backends/drm/kms_api.hpp` | - | 158 | Injectable master, dumb-buffer, property, state, atomic, legacy, and readback boundary; `KmsApi`, `KmsMode` | `drm_api.hpp`, `dumb_buffer_api.hpp`, `property_cache.hpp` | new narrow platform interface |
| `src/backends/drm/kms_dumb_buffer_api.cpp` | - | 35 | Borrowed-FD adapter from `KmsApi` to `DumbBufferApi`; `KmsDumbBufferApi` | `kms_api.hpp` | new final adapter |
| `src/backends/drm/kms_state.cpp` | - | 199 | Exact KMS capture, atomic request construction, restore, and readback; `ModeBlob`, `capture_saved_state`, `restore_saved_state` | `kms_state.hpp` | new final state module |
| `src/backends/drm/kms_state.hpp` | - | 62 | Saved route/state and atomic-request declarations | `kms_api.hpp` | new narrow interface |
| `src/backends/drm/mode_selector.cpp` | - | 60 | Exact-dimension and bounded-refresh mode ranking; `select_mode` | `mode_selector.hpp` | new final selector |
| `src/backends/drm/mode_selector.hpp` | - | 36 | Mode selection request/status contract | `resources.hpp` | new narrow interface |
| `src/backends/drm/pipeline_selector.cpp` | - | 83 | Compatible CRTC and XRGB8888 primary-plane selection; `select_crtc`, `select_primary_plane` | `pipeline_selector.hpp` | new final selector |
| `src/backends/drm/pipeline_selector.hpp` | - | 36 | CRTC/plane selection result contract | `resources.hpp` | new narrow interface |
| `src/backends/drm/presenter.cpp` | - | 544 | Initial modeset, asynchronous flip state, hashes/evidence, VT transitions, and ordered shutdown; `DrmPresenter` | `presenter.hpp`, connector/mode/pipeline selectors | new final presentation owner |
| `src/backends/drm/presenter.hpp` | - | 143 | DRM presentation and display-session control contract | device, report, dumb-buffer, KMS, headless-dump, output-presentation, and VT interfaces | new narrow coordinator interface |
| `src/backends/drm/presenter_pipeline.cpp` | - | 149 | Pipeline choice, atomic TEST_ONLY negotiation, legacy fallback, and initial reports; `select_pipeline`, `configure_api`, `try_atomic` | `presenter.hpp`, connector/mode/pipeline selectors | new final pipeline module |
| `src/backends/drm/property_cache.cpp` | - | 89 | Exact required atomic-property binding and validation; `build_atomic_property_cache` | `property_cache.hpp` | new final property model |
| `src/backends/drm/property_cache.hpp` | - | 72 | Typed connector/CRTC/primary-plane property cache | - | new narrow interface |
| `src/backends/drm/real_drm_api.cpp` | - | 500 | libdrm primary-node open/adopt, canonical discovery, resource enumeration, and `drmHandleEvent`; `RealDrmApi` | `drm_api.hpp` | new final platform boundary |
| `src/backends/drm/real_kms_api.cpp` | - | 336 | libdrm/ioctl master, dumb-buffer, atomic, legacy, property, and readback calls; `RealKmsApi` | `kms_api.hpp` | new final platform boundary |
| `src/backends/drm/resources.hpp` | - | 106 | Component-neutral discovered connector, mode, CRTC, plane, and XRGB8888 models | - | new narrow model |
| `src/backends/headless/frame_dump.cpp` | 132 | 213 | Staged PPM/manifest evidence publication with abort/finalize lifecycle; `FrameDumper`, `StagedFrameDump` | `frame_dump.hpp` | materially extended for async parity |
| `src/backends/headless/frame_dump.hpp` | 40 | 79 | Staged frame-dump declarations | - | materially extended interface |
| `src/backends/headless/output.cpp` | 51 | 1 | Compatibility translation unit for the moved software frame | `output.hpp` | reduced compatibility shell |
| `src/backends/headless/output.hpp` | 39 | 9 | Compatibility aliases to component-neutral `SoftwareFrame` | `software_frame.hpp` | reduced compatibility interface |
| `src/backends/headless/presenter.cpp` | - | 64 | Immediate headless presentation, staged dump finalization, suspend/resume, and shutdown; `headless::Presenter` | `presenter.hpp` | new final presenter |
| `src/backends/headless/presenter.hpp` | - | 34 | Headless `PresentationBackend` declaration | `frame_dump.hpp`, `presentation_backend.hpp` | new narrow interface |
| `src/backends/output/presentation_backend.hpp` | - | 59 | Internal synchronous/asynchronous presentation, service, suspend/resume, and shutdown contract; `PresentationBackend` | `software_frame.hpp` | new component-neutral interface |
| `src/backends/output/software_frame.cpp` | - | 75 | Bounded canonical XRGB8888 allocation, clear, and visible hashing; `SoftwareFrame` | `software_frame.hpp` | new final canonical frame |
| `src/backends/output/software_frame.hpp` | - | 65 | Canonical frame/view/spec model and unchanged limits | `compositor/rectangle.hpp` | new narrow interface |
| `src/backends/session/external_session.cpp` | - | 55 | Close-on-exec inherited-FD duplication and sole-owned cleanup; `ExternalDeviceSession` | `external_session.hpp` | new future broker seam |
| `src/backends/session/external_session.hpp` | - | 45 | Injectable inherited-FD ownership contract | - | new narrow interface |
| `src/backends/session/vt_api.cpp` | - | 181 | Linux VT/KD ioctl and exact tty identity implementation; `LinuxVirtualTerminalApi` | `vt_api.hpp` | new final platform boundary |
| `src/backends/session/vt_api.hpp` | - | 84 | Injectable VT/KD operation and saved-state contract | - | new narrow interface |
| `src/backends/session/vt_session.cpp` | - | 295 | Direct acquire, release, reacquire, unwind, and ordered restoration state machine; `DirectVirtualTerminalSession` | `vt_session.hpp` | new final session owner |
| `src/backends/session/vt_session.hpp` | - | 77 | Direct-session and display-control contracts | `vt_api.hpp` | new narrow interface |
| `src/gwcomp/compositor.cpp` | 176 | 295 | Scene mutation validation plus injected presentation delegation and lifecycle | `compositor.hpp`, `presentation_transaction.hpp`, headless presenter | extended coordinator, below cap |
| `src/gwcomp/compositor.hpp` | 92 | 137 | Compositor ownership, presentation timing/state, and process-facing completion API | output presentation/frame, scene/buffer, manifest, configuration | extended narrow coordinator interface |
| `src/gwcomp/contract_dispatch.cpp` | 204 | 246 | GWIPC decoding plus delayed acknowledgement/release finalization | `contract_dispatch.hpp` | extended contract boundary |
| `src/gwcomp/contract_dispatch.hpp` | 23 | 32 | Dispatch result with pending-frame/reply correlation | `compositor.hpp` | extended narrow interface |
| `src/gwcomp/drm_runtime.cpp` | - | 217 | CLI-to-device/presenter construction, deterministic automatic device/default-mode choice, and resource lifetime; `create_drm_presenter` | DRM model/device/presenter, headless dump, VT API, options | new final DRM factory |
| `src/gwcomp/drm_runtime.hpp` | - | 43 | Opaque runtime owners for real DRM/KMS/report/mirror/VT objects | `options.hpp` | new narrow interface |
| `src/gwcomp/options.cpp` | 91 | 270 | Headless/DRM CLI parsing and direct/external option validation; `parse_options` | `options.hpp`, `config.hpp` | extended CLI module |
| `src/gwcomp/options.hpp` | 23 | 42 | Backend, KMS API, mode, device, session, mirror, and report options | - | extended narrow model |
| `src/gwcomp/presentation_transaction.cpp` | 314 | 484 | Candidate render/present state, pending deadline, completion validation, transactional scene-manifest publication, promotion, acknowledgement inputs, and rollback; `PresentationTransaction` | `presentation_transaction.hpp`, `compositor.hpp`, software renderer | extended transaction owner |
| `src/gwcomp/presentation_transaction.hpp` | 24 | 65 | Owned pending scene/attachment/release/frame/manifest transaction | frame, buffer, scene, compositor contracts | extended narrow interface |
| `src/gwcomp/scene_manifest.cpp` | 251 | 284 | Deterministic scene serialization plus prepared, exactly-once publication for metadata and buffered ProtocolServer frames; `SceneManifest` | `scene_manifest.hpp`, compositor scene | extended evidence module |
| `src/gwcomp/scene_manifest.hpp` | 33 | 47 | Prepared scene-manifest record and serialization/publication declarations | compositor scene | extended narrow evidence interface |
| `src/gwcomp/runtime.cpp` | 236 | 357 | Listener/producer/signal/presentation poll reactor, VT coordination, and ordered shutdown; `run` | dispatch/runtime/signal, output presentation, optional headless/DRM factories | extended top-level coordinator |
| `src/gwcomp/signal_runtime.cpp` | - | 143 | Tagged self-pipe handlers for stop and VT release/acquire; `SignalRuntime` | `signal_runtime.hpp` | new final signal boundary |
| `src/gwcomp/signal_runtime.hpp` | - | 37 | Tagged compositor signal event contract | - | new narrow interface |
| `src/tools/drm_probe.cpp` | - | 549 | Read-only deterministic device/route discovery, KMS snapshot, active, and restored validation; `run_drm_probe` | probe interface plus DRM selectors/device | new final diagnostic tool |
| `src/tools/drm_probe.hpp` | - | 37 | Probe options and injected-run entry point | `drm_api.hpp` | new narrow interface |
| `src/tools/drm_probe_main.cpp` | - | 13 | Real-DRM probe process entry | `drm_probe.hpp` | new top-level main |

## Decomposition result

| Required boundary | Before | After | Status |
|---|---|---|---|
| Request decoding and dispatch | `request_dispatcher.cpp` mixed all handlers | `request_dispatcher.cpp` routes only; `request_handlers/*.cpp` owns semantic families; protocol decoders remain under `protocol/x11`. | complete |
| Resource ownership and storage | `resource_table.cpp` mixed every resource type and cleanup | `{window,drawable,font,property}_store.cpp`, `resource_cleanup.cpp`, and `resource_invariants.cpp`; table shell owns construction/lookups. | complete |
| Listener and socket ownership | `server.cpp` | `x11_listener.cpp`; GWIPC endpoint mechanics remain in `ipc/endpoint.cpp`. | complete |
| Client registry and cleanup | `server.cpp`, `resource_table.cpp` | `client_registry.cpp`, `resource_cleanup.cpp`, and `server_shutdown.cpp`. | complete |
| Poll/reactor construction | server and process mains | `server_reactor.cpp`, `gwcomp/runtime.cpp`, and `gwm/runtime.cpp`. | complete |
| Integrated peer coordination | `server.cpp` | `integrated_runtime.cpp` using `RuntimeBridge`, `PolicyPeer`, and `CompositorPeer`. | complete |
| Input coordination | `server.cpp` | `input_runtime.cpp` using `EventRouter`, `InputRouter`, `InputState`, and `SyntheticInputPeer`. | complete |
| Lifecycle completion | `server.cpp` | `lifecycle_runtime.cpp`. | complete |
| Compositor contract dispatch | `gwcomp/main.cpp` | `gwcomp/contract_dispatch.cpp`. | complete |
| Scene/presentation transaction | `gwcomp/compositor.cpp` | `Compositor` validates mutations; `presentation_transaction.cpp` owns candidate render/submit state and promotes/releases only after synchronous or verified asynchronous completion. | complete |
| WM contract dispatch | `gwm/main.cpp` | `gwm/contract_dispatch.cpp`. | complete |
| Process CLI and signals | process mains | 13–17-line mains, existing option parsers, and process-specific signal runtime modules. | complete |

## Budget result

| Budget target | After | Result |
|---|---:|---|
| `request_dispatcher.cpp <= 450` | 128 | pass |
| `server.cpp <= 500` | 65 | pass |
| `server_reactor.cpp <= 600` | 137 | pass |
| other new server runtime files `<= 500` | maximum 379 | pass |
| `resource_table.cpp <= 500` | 103 | pass |
| `gwcomp/main.cpp <= 250` | 13 | pass |
| `gwcomp/runtime.cpp <= 500` | 357 | pass |
| `gwcomp/compositor.cpp <= 600` | 295 | pass |
| `presentation_transaction.cpp <= 600` | 484 | pass |
| `gwm/main.cpp <= 250` | 13 | pass |
| new DRM presenter/runtime/probe files `<= 600` | maximum 549 | pass |
| all new/materially rewritten M10 files `<= 600` | maximum 549 | pass |
| all `src` C/C++ files `<= 1,000` or reviewed | only `ipc/connection.cpp` at 1,361 | pass via reviewed allowlist |

## Reviewed function spans

The 100-line ordinary-function value is a target and spans over 150 lines
require review. The shell guard's conservative lexical scanner reports the
following eight review items. These are function-review dispositions, not
hard-file allowlist entries.

| Function | Lexical span | Review disposition | Revisit |
|---|---:|---|---|
| `CompositorPeer::submit` in `compositor_peer.cpp:115` | 152 | Keep the serialized snapshot/content submission and acknowledgement state transition together; splitting would duplicate peer transaction invariants. | M11 peer-state review |
| `ServerRuntime::service_input` in `input_runtime.cpp:12` | 163 | Keep record ordering, state transition, event delivery, focus deferral, and acknowledgement in one single-threaded input turn. | M11 input-routing review |
| `PresentationTransaction::commit` in `presentation_transaction.cpp:126` | 286 | Keep candidate validation/rendering, manifest preparation, backend submit, synchronous completion, and owned asynchronous rollback points in one atomic transaction; event service/finalization is already separate. | M11 transaction-state review |
| `run` in `gwcomp/runtime.cpp:85` | 271 | Keep one explicit single-threaded listener/producer/signal/presentation reactor so pending-frame and VT gating remain visible; CLI construction, contract semantics, signal plumbing, and DRM initialization are separate modules. | M11 reactor review |
| `validate_application` in `ipc/connection.cpp:249` | 183 | Keep the exhaustive wire-type/capability/FD validation switch centralized at the transport trust boundary. | M11 IPC-internals review |
| `receive_one` in `ipc/connection.cpp:647` | 365 | Keep one recvmsg transaction responsible for credentials, ancillary FDs, envelope validation, and queue admission cleanup. | M11 IPC-internals review |
| `gwipc_connection_enqueue_with_sequence` in `ipc/connection.cpp:1189` | 151 | Keep public C-ABI validation and atomic sequence/queue admission together. | M11 IPC-internals review |
| `evaluate` in `wm/policy_engine.cpp:193` | 202 | Keep the deterministic policy evaluation, stacking, focus, and canonical operation ordering in one auditable pure pass. | M11 WM-policy review |

## Per-file inventory

“Dependencies” lists direct project-local quoted includes; a dash means the file
has none. “Major symbols” is a concise symbol index, not a complete API listing.
The target status describes the Phase 1 result. Only
`src/ipc/connection.cpp` has a hard file-size exception, recorded exactly in
`source_size_allowlist.txt`.

| Path | Before | After | Primary responsibility | Major classes/functions | Direct internal dependencies | Phase 1 status | Destination / exception |
|---|---:|---:|---|---|---|---|---|
| `src/backends/headless/frame_dump.cpp` | 132 | 132 | Implements frame dump for the headless output backend | dump,fnv1a64,frame_name,hex64,write_all | `backends/headless/frame_dump.hpp` | not targeted | unchanged |
| `src/backends/headless/frame_dump.hpp` | 40 | 40 | Declares frame dump for the headless output backend | FrameDumpMetadata,FrameDumpResult,FrameDumper | - | not targeted | unchanged |
| `src/backends/headless/output.cpp` | 51 | 51 | Implements output for the headless output backend | configure,disable | `backends/headless/output.hpp` | not targeted | unchanged |
| `src/backends/headless/output.hpp` | 39 | 39 | Declares output for the headless output backend | Output,enabled,height,id,pixels,width | - | not targeted | unchanged |
| `src/compositor/buffer.cpp` | 96 | 96 | Implements buffer for the component-neutral compositor model | BufferMapping::BufferMapping,final,import,is_srgb,metadata_valid,~BufferMapping | `compositor/buffer.hpp` | not targeted | unchanged |
| `src/compositor/buffer.hpp` | 49 | 49 | Declares buffer for the component-neutral compositor model | - | - | not targeted | unchanged |
| `src/compositor/damage_region.cpp` | 69 | 69 | Implements damage region for the component-neutral compositor model | DamageRegion::DamageRegion,add,add_full_output,empty,is_full_output,normalize | `compositor/damage_region.hpp` | not targeted | unchanged |
| `src/compositor/damage_region.hpp` | 30 | 30 | Declares damage region for the component-neutral compositor model | DamageRegion | `compositor/rectangle.hpp` | not targeted | unchanged |
| `src/compositor/rectangle.cpp` | 108 | 108 | Implements rectangle for the component-neutral compositor model | bottom,bounding_union,coordinate_fits,has_valid_extents,intersection,overlaps_or_is_compatibly_adjacent | `compositor/rectangle.hpp` | not targeted | unchanged |
| `src/compositor/rectangle.hpp` | 31 | 31 | Declares rectangle for the component-neutral compositor model | Rectangle,empty | - | not targeted | unchanged |
| `src/compositor/scene.cpp` | 303 | 303 | Implements scene for the component-neutral compositor model | abort_complete_snapshot,apply,begin_complete_snapshot,checked_extent,commit,disconnect | `compositor/scene.hpp` | not targeted | unchanged |
| `src/compositor/scene.hpp` | 72 | 72 | Declares scene for the component-neutral compositor model | CommitResult,PendingDamage,Scene,SceneModel,accepted,committed | `compositor/damage_region.hpp` | not targeted | unchanged |
| `src/core/checked_math.hpp` | 38 | 38 | Declares checked math for the core utility | checked_add,checked_align_up,checked_multiply | - | not targeted | unchanged |
| `src/core/geometry/rectangle.cpp` | 24 | 24 | Implements rectangle for the core geometry | intersect | `core/geometry/rectangle.hpp` | not targeted | unchanged |
| `src/core/geometry/rectangle.hpp` | 21 | 21 | Declares rectangle for the core geometry | Rectangle,empty | - | not targeted | unchanged |
| `src/core/geometry/region.cpp` | 43 | 43 | Implements region for the core geometry | add,bounds,touches | `core/geometry/region.hpp` | not targeted | unchanged |
| `src/core/geometry/region.hpp` | 22 | 22 | Declares region for the core geometry | Region,empty,rectangles | `core/geometry/rectangle.hpp` | not targeted | unchanged |
| `src/glasswyrmd/atom_table.cpp` | 73 | 73 | Implements atom table for the X11 server | AtomTable::AtomTable,find,intern,name,valid | `glasswyrmd/atom_table.hpp` | not targeted | unchanged |
| `src/glasswyrmd/atom_table.hpp` | 46 | 46 | Declares atom table for the X11 server | AtomTable,Exhausted,InternAtomResult,InternAtomStatus,Success,size | `protocol/x11/atoms.hpp` | not targeted | unchanged |
| `src/glasswyrmd/bitmap_storage.cpp` | 52 | 52 | Implements bitmap storage for the X11 server | create,put_xybitmap_lsb32 | `glasswyrmd/bitmap_storage.hpp` | not targeted | unchanged |
| `src/glasswyrmd/bitmap_storage.hpp` | 51 | 51 | Declares bitmap storage for the X11 server | BitmapStorage,at,bits,byte_size,height,set | - | not targeted | unchanged |
| `src/glasswyrmd/client_connection.cpp` | 370 | 370 | Implements client connection for the X11 server | ClientConnection::ClientConnection,clear_dispatch_blocked,close_after_output,close_with_log,enqueue,enqueue_server_packet | `glasswyrmd/client_connection.hpp`, `protocol/x11/reply.hpp` | not targeted | unchanged |
| `src/glasswyrmd/client_connection.hpp` | 147 | 147 | Declares client connection for the X11 server | ClientConnection,OutputPacket,RequestWorkBudget,State,available,byte_order | `glasswyrmd/compatibility_trace.hpp`, `glasswyrmd/request_dispatcher.hpp`, `glasswyrmd/server_state.hpp`, `protocol/x11/core.hpp`, `protocol/x11/request.hpp`, `protocol/x11/setup.hpp` | not targeted | unchanged |
| `src/glasswyrmd/client_registry.cpp` | - | 88 | Client accept, resource-base allocation, registry insertion, and closed-client removal | accept_clients,allocate_resource_base,remove_closed_clients | `glasswyrmd/server.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/compatibility_trace.cpp` | 253 | 253 | Implements compatibility trace for the X11 server | append,connection,create,error_name,event_window_offset,extension_name | `glasswyrmd/compatibility_trace.hpp` | not targeted | unchanged |
| `src/glasswyrmd/compatibility_trace.hpp` | 51 | 51 | Declares compatibility trace for the X11 server | CompatibilityTrace,bytes_written,enabled | `protocol/x11/byte_order.hpp` | not targeted | unchanged |
| `src/glasswyrmd/compositor_peer.cpp` | 430 | 430 | Implements compositor peer for the X11 server | CompositorPeer::CompositorPeer,ContractDelete,ControlDelete,DecodedDelete,connect,disconnect | `glasswyrmd/compositor_peer.hpp` | not targeted | unchanged |
| `src/glasswyrmd/compositor_peer.hpp` | 76 | 76 | Declares compositor peer for the X11 server | Buffer,CompositorBufferRelease,CompositorContentSubmission,CompositorPeer,CompositorSnapshotSubmission,Damage | `glasswyrmd/peer_transport.hpp`, `protocol/x11/screen_model.hpp` | not targeted | unchanged |
| `src/glasswyrmd/content_presenter.cpp` | 271 | 271 | Implements content presenter for the X11 server | accept_content,accept_lifecycle,damage,ensure_buffer,has_pending_damage,make_damage | `glasswyrmd/content_presenter.hpp`, `core/geometry/region.hpp`, `glasswyrmd/subtree_compositor.hpp` | not targeted | unchanged |
| `src/glasswyrmd/content_presenter.hpp` | 74 | 74 | Declares content presenter for the X11 server | ContentPresenter,WindowContent,buffers,forget_peer_attachments,frame_in_flight | `glasswyrmd/compositor_peer.hpp`, `glasswyrmd/lifecycle_types.hpp`, `glasswyrmd/published_buffer.hpp`, `glasswyrmd/resource_table.hpp` | not targeted | unchanged |
| `src/glasswyrmd/drawable_store.cpp` | - | 122 | Pixmap and graphics-context ownership | create_gc,create_pixmap,free_gc,free_pixmap | `glasswyrmd/resource_table.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/event_router.cpp` | 511 | 511 | Implements event router for the X11 server | capture,capture_input_transition,find_client,route,route_configure,route_crossing | `glasswyrmd/event_router.hpp`, `protocol/x11/event.hpp`, `protocol/x11/exposure_event.hpp`, `input/input_router.hpp`, `protocol/x11/crossing_event.hpp`, `protocol/x11/focus_event.hpp`, `protocol/x11/input_event.hpp`, `protocol/x11/event_mask.hpp` | not targeted | unchanged |
| `src/glasswyrmd/event_router.hpp` | 91 | 91 | Declares event router for the X11 server | DirectInputEventState,EventRouter,InputTransitionState | `glasswyrmd/client_connection.hpp`, `glasswyrmd/resource_table.hpp`, `core/geometry/rectangle.hpp`, `input/input_state.hpp`, `protocol/x11/input_event.hpp` | not targeted | unchanged |
| `src/glasswyrmd/font.cpp` | 145 | 145 | Implements font for the X11 server | apply_pixel,fixed_glyph,matches_fixed_font,raster_text8,rows | `glasswyrmd/font.hpp` | not targeted | unchanged |
| `src/glasswyrmd/font.hpp` | 38 | 38 | Declares font for the X11 server | FontResource,TextRasterResult | `core/geometry/rectangle.hpp`, `glasswyrmd/pixel_storage.hpp` | not targeted | unchanged |
| `src/glasswyrmd/font_store.cpp` | - | 47 | Font-resource ownership | close_font,open_font | `glasswyrmd/resource_table.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/graphics_context.hpp` | 22 | 22 | Declares graphics context for the X11 server | GraphicsContextResource | - | not targeted | unchanged |
| `src/glasswyrmd/input_runtime.cpp` | - | 178 | Synthetic-input peer processing, routing, focus transactions, and acknowledgements | service_input | `glasswyrmd/server_runtime.hpp`, `input/input_router.hpp`, `protocol/x11/event_mask.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/integrated_runtime.cpp` | - | 232 | Policy/compositor peer startup, replay, and integrated event coordination | initialize_integrated,service_integrated | `glasswyrmd/server_runtime.hpp`, `glasswyrmd/lifecycle_projection.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/lifecycle_coordinator.cpp` | 243 | 243 | Implements lifecycle coordinator for the X11 server | LifecycleCoordinator::LifecycleCoordinator,cancel_client,compositor_accepted,compositor_rejected,enqueue,enqueue_paused | `glasswyrmd/lifecycle_coordinator.hpp` | not targeted | unchanged |
| `src/glasswyrmd/lifecycle_coordinator.hpp` | 89 | 89 | Declares lifecycle coordinator for the X11 server | AwaitingCompositor,AwaitingPolicy,Configure,CoordinatorPhase,Create,Destroy | `glasswyrmd/lifecycle_types.hpp` | not targeted | unchanged |
| `src/glasswyrmd/lifecycle_projection.cpp` | 87 | 87 | Implements lifecycle projection for the X11 server | applied_policy,apply_policy_result,project_compositor,project_policy | `glasswyrmd/lifecycle_projection.hpp` | not targeted | unchanged |
| `src/glasswyrmd/lifecycle_projection.hpp` | 20 | 20 | Declares lifecycle projection for the X11 server | - | `glasswyrmd/compositor_peer.hpp`, `glasswyrmd/lifecycle_types.hpp`, `glasswyrmd/policy_peer.hpp`, `protocol/x11/screen_model.hpp` | not targeted | unchanged |
| `src/glasswyrmd/lifecycle_runtime.cpp` | - | 379 | Deferred lifecycle submission, peer completion, commit, and cancellation | cancel_client_lifecycle,commit_lifecycle,complete_lifecycle,defer_lifecycle,initialize_lifecycle,send_compositor | `glasswyrmd/server_runtime.hpp`, `glasswyrmd/lifecycle_projection.hpp`, `input/input_router.hpp`, `protocol/x11/reply.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/lifecycle_snapshot.cpp` | 83 | 83 | Implements lifecycle snapshot for the X11 server | apply_policy_state,build_lifecycle_snapshot | `glasswyrmd/lifecycle_snapshot.hpp` | not targeted | unchanged |
| `src/glasswyrmd/lifecycle_snapshot.hpp` | 16 | 16 | Declares lifecycle snapshot for the X11 server | - | `glasswyrmd/lifecycle_types.hpp`, `glasswyrmd/resource_table.hpp` | not targeted | unchanged |
| `src/glasswyrmd/lifecycle_types.hpp` | 45 | 45 | Declares lifecycle types for the X11 server | AppliedPolicyWindow,LifecycleSnapshot,LifecycleWindow | `glasswyrmd/window.hpp` | not targeted | unchanged |
| `src/glasswyrmd/m9_raster_ops.cpp` | 128 | 128 | Implements m9 raster ops for the X11 server | draw_line,draw_segments,fill_convex_polygon,fill_ellipse,plot | `glasswyrmd/m9_raster_ops.hpp` | not targeted | unchanged |
| `src/glasswyrmd/m9_raster_ops.hpp` | 42 | 42 | Declares m9 raster ops for the X11 server | RasterEllipse,RasterPoint,RasterSegment | `glasswyrmd/pixel_storage.hpp` | not targeted | unchanged |
| `src/glasswyrmd/main.cpp` | 17 | 17 | Implements main for the X11 server | main | `glasswyrmd/options.hpp`, `glasswyrmd/server.hpp` | not targeted | unchanged |
| `src/glasswyrmd/options.cpp` | 132 | 132 | Implements options for the X11 server | parse_display,parse_options,print_usage | `glasswyrmd/options.hpp`, `config.hpp` | not targeted | unchanged |
| `src/glasswyrmd/options.hpp` | 34 | 34 | Declares options for the X11 server | Options,ParseOptionsResult,integrated | - | not targeted | unchanged |
| `src/glasswyrmd/peer_transport.cpp` | 72 | 72 | Implements peer transport for the X11 server | PeerTransport::PeerTransport,connect,connection,disconnect,established,fd | `glasswyrmd/peer_transport.hpp` | not targeted | unchanged |
| `src/glasswyrmd/peer_transport.hpp` | 44 | 44 | Declares peer transport for the X11 server | Disconnected,Fatal,PeerBootstrapState,PeerProcessOutcome,PeerTransport,Progress | - | not targeted | unchanged |
| `src/glasswyrmd/pixel_storage.cpp` | 50 | 50 | Implements pixel storage for the X11 server | create,fill,resize_preserving_overlap | `glasswyrmd/pixel_storage.hpp` | not targeted | unchanged |
| `src/glasswyrmd/pixel_storage.hpp` | 46 | 46 | Declares pixel storage for the X11 server | PixelStorage,at,byte_size,height,pixels,stride | `core/geometry/rectangle.hpp` | not targeted | unchanged |
| `src/glasswyrmd/pixmap.hpp` | 36 | 36 | Declares pixmap for the X11 server | PixmapResource,bitmap,byte_size,pixels | `glasswyrmd/bitmap_storage.hpp`, `glasswyrmd/pixel_storage.hpp` | not targeted | unchanged |
| `src/glasswyrmd/policy_peer.cpp` | 359 | 359 | Implements policy peer for the X11 server | ContractDelete,ControlDelete,DecodedContractDelete,DecodedControlDelete,PolicyPeer::PolicyPeer,canonical_policy_hash | `glasswyrmd/policy_peer.hpp` | not targeted | unchanged |
| `src/glasswyrmd/policy_peer.hpp` | 61 | 61 | Declares policy peer for the X11 server | PolicyPeer,PolicySnapshotResult,PolicySnapshotSubmission,fd,policy_hash,replay_input | `glasswyrmd/peer_transport.hpp`, `protocol/x11/screen_model.hpp` | not targeted | unchanged |
| `src/glasswyrmd/property.cpp` | 80 | 80 | Implements property for the X11 server | byte_size,concatenate_property_data,format,item_count,slice_property_data,slice_values | `glasswyrmd/property.hpp` | not targeted | unchanged |
| `src/glasswyrmd/property.hpp` | 40 | 40 | Declares property for the X11 server | Append,Prepend,Property,PropertyMode,PropertySlice,Replace | - | not targeted | unchanged |
| `src/glasswyrmd/property_store.cpp` | - | 133 | Window property storage and byte-accounting operations | change_property,delete_property,get_property,list_properties | `glasswyrmd/resource_table.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/published_buffer.cpp` | 177 | 177 | Implements published buffer for the X11 server | PublishedWindowBuffer::PublishedWindowBuffer,copy_all_from,copy_from,create,create_memfd,current | `glasswyrmd/published_buffer.hpp` | not targeted | unchanged |
| `src/glasswyrmd/published_buffer.hpp` | 100 | 100 | Declares published buffer for the X11 server | PublishedBufferRetirement,PublishedBufferStore,PublishedWindowBuffer,Retired,accounted_bytes,announced | `core/geometry/rectangle.hpp`, `glasswyrmd/pixel_storage.hpp` | not targeted | unchanged |
| `src/glasswyrmd/raster_ops.cpp` | 78 | 78 | Implements raster ops for the X11 server | apply,copy_area,decode_pixel,put_zpixmap | `glasswyrmd/raster_ops.hpp` | not targeted | unchanged |
| `src/glasswyrmd/raster_ops.hpp` | 27 | 27 | Declares raster ops for the X11 server | RasterResult | `core/geometry/rectangle.hpp`, `glasswyrmd/pixel_storage.hpp` | not targeted | unchanged |
| `src/glasswyrmd/request_dispatcher.cpp` | 2091 | 128 | Decode-independent X11 opcode routing and shared dispatch-result translation | dispatch_request | `glasswyrmd/request_dispatcher.hpp`, `glasswyrmd/request_handlers/common.hpp`, `protocol/x11/core.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/request_dispatcher.hpp` | 113 | 113 | Declares request dispatcher for the X11 server | CloseClient,Configure,DeferredLifecycle,Destroy,DispatchContext,DispatchKind | `glasswyrmd/server_state.hpp`, `protocol/x11/byte_order.hpp`, `protocol/x11/request.hpp`, `protocol/x11/lifecycle_request.hpp`, `core/geometry/rectangle.hpp` | not targeted | unchanged |
| `src/glasswyrmd/request_handlers/atom.cpp` | - | 67 | Implements atom for the X11 request-handler family | get_atom_name,intern_atom | `glasswyrmd/request_handlers/common.hpp`, `protocol/x11/byte_cursor.hpp`, `protocol/x11/reply.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/request_handlers/color.cpp` | - | 142 | Implements color for the X11 request-handler family | Color,alloc_color,blue,color_pixel,free_colors,green | `glasswyrmd/request_handlers/common.hpp`, `protocol/x11/byte_cursor.hpp`, `protocol/x11/reply.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/request_handlers/common.cpp` | - | 23 | Implements common for the X11 request-handler family | error,exact_size,padded_size | `glasswyrmd/request_handlers/common.hpp`, `protocol/x11/reply.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/request_handlers/common.hpp` | - | 128 | Declares common for the X11 request-handler family | - | `glasswyrmd/request_dispatcher.hpp`, `protocol/x11/core.hpp` | complete | final narrow internal interface/destination |
| `src/glasswyrmd/request_handlers/drawable_access.cpp` | - | 141 | Implements drawable access for the X11 request-handler family | ClipByChildrenGuard::ClipByChildrenGuard,add_window_damage,known_drawable,mutable_storage,rectangle_difference,restore | `glasswyrmd/request_handlers/drawable_access.hpp`, `core/geometry/region.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/request_handlers/drawable_access.hpp` | - | 43 | Declares drawable access for the X11 request-handler family | ClipByChildrenGuard,Saved | `glasswyrmd/request_handlers/common.hpp`, `glasswyrmd/graphics_context.hpp`, `glasswyrmd/pixel_storage.hpp` | complete | final narrow internal interface/destination |
| `src/glasswyrmd/request_handlers/drawable_resource.cpp` | - | 138 | Implements drawable resource for the X11 request-handler family | GcDecodeResult,bad,change_gc,create_gc,create_pixmap,decode_gc_values | `glasswyrmd/request_handlers/common.hpp`, `protocol/x11/byte_cursor.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/request_handlers/drawing.cpp` | - | 336 | Implements drawing for the X11 request-handler family | Fill,clear_area,clipped_bounds,copy_area_request,fill_poly,poly_fill_arc | `glasswyrmd/request_handlers/common.hpp`, `glasswyrmd/request_handlers/drawable_access.hpp`, `glasswyrmd/m9_raster_ops.hpp`, `glasswyrmd/raster_ops.hpp`, `core/geometry/region.hpp`, `protocol/x11/byte_cursor.hpp`, `protocol/x11/exposure_event.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/request_handlers/font.cpp` | - | 131 | Implements font for the X11 request-handler family | close_font,list_fonts,open_font,query_font,query_text_extents,valid_fontable | `glasswyrmd/request_handlers/common.hpp`, `glasswyrmd/font.hpp`, `protocol/x11/byte_cursor.hpp`, `protocol/x11/reply.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/request_handlers/lifecycle.cpp` | - | 390 | Implements lifecycle for the X11 request-handler family | add_child_outer_damage,add_local_lifecycle_effects,add_parent_reveal,capture_structural_state,child_outer_rectangle,configure_window | `glasswyrmd/request_handlers/common.hpp`, `glasswyrmd/request_handlers/drawable_access.hpp`, `glasswyrmd/request_handlers/window_attributes.hpp`, `core/geometry/region.hpp`, `protocol/x11/byte_cursor.hpp`, `protocol/x11/event_mask.hpp`, `protocol/x11/lifecycle_request.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/request_handlers/property.cpp` | - | 236 | Implements property for the X11 request-handler family | change_property,decode_property_data,delete_property,get_property,list_properties,property_bad_atom | `glasswyrmd/request_handlers/common.hpp`, `protocol/x11/byte_cursor.hpp`, `protocol/x11/reply.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/request_handlers/query.cpp` | - | 199 | Implements query for the X11 request-handler family | fits_i16,get_input_focus,get_keyboard_mapping,get_modifier_mapping,get_pointer_mapping,immediate_child_at | `glasswyrmd/request_handlers/common.hpp`, `protocol/x11/byte_cursor.hpp`, `protocol/x11/reply.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/request_handlers/text.cpp` | - | 96 | Implements text for the X11 request-handler family | TextItem,delta,image_text8,poly_text8,text | `glasswyrmd/request_handlers/common.hpp`, `glasswyrmd/request_handlers/drawable_access.hpp`, `glasswyrmd/font.hpp`, `glasswyrmd/m9_raster_ops.hpp`, `core/geometry/region.hpp`, `protocol/x11/byte_cursor.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/request_handlers/window.cpp` | - | 227 | Implements window for the X11 request-handler family | change_window_attributes,decode_window_attributes,get_geometry,get_window_attributes | `glasswyrmd/request_handlers/window_attributes.hpp`, `protocol/x11/byte_cursor.hpp`, `protocol/x11/event_mask.hpp`, `protocol/x11/reply.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/request_handlers/window_attributes.hpp` | - | 23 | Declares window attributes for the X11 request-handler family | DecodedWindowAttributes | `glasswyrmd/request_handlers/common.hpp`, `protocol/x11/byte_cursor.hpp` | complete | final narrow internal interface/destination |
| `src/glasswyrmd/resource_cleanup.cpp` | - | 225 | Destroy plans and client/resource cleanup | capture_destroy_plan,cleanup_client,cleanup_pending,commit_client_cleanup,commit_destroy_plan,destroy_leaf | `glasswyrmd/resource_table.hpp`, `protocol/x11/event_mask.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/resource_id.hpp` | 27 | 27 | Declares resource id for the X11 server | is_server_owned_id,resource_id_matches_client | `protocol/x11/screen_model.hpp` | not targeted | unchanged |
| `src/glasswyrmd/resource_invariants.cpp` | - | 83 | Cross-resource ownership and accounting invariants | invariants_hold,window_property_bytes | `glasswyrmd/resource_table.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/resource_table.cpp` | 885 | 103 | Resource-table construction and type-agnostic lookup | ResourceTable::ResourceTable,find,find_font,find_gc,find_pixmap,find_window | `glasswyrmd/resource_table.hpp`, `glasswyrmd/resource_id.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/resource_table.hpp` | 205 | 205 | Declares resource table for the X11 server | BadAlloc,BadDrawable,BadFont,BadGContext,BadIdChoice,BadMatch | `glasswyrmd/resource_id.hpp`, `glasswyrmd/window.hpp`, `glasswyrmd/pixmap.hpp`, `glasswyrmd/graphics_context.hpp`, `glasswyrmd/font.hpp` | not targeted | unchanged |
| `src/glasswyrmd/runtime_bridge.cpp` | 287 | 287 | Implements runtime bridge for the X11 server | RuntimeBridge::RuntimeBridge,clear_transaction_result,compositor_rejected_ready,compositor_result_ready,content_rejected_ready,content_result_ready | `glasswyrmd/runtime_bridge.hpp` | not targeted | unchanged |
| `src/glasswyrmd/runtime_bridge.hpp` | 88 | 88 | Declares runtime bridge for the X11 server | Compositor,Failed,None,Policy,PolicyReady,PolicyRejected | `glasswyrmd/compositor_peer.hpp`, `glasswyrmd/policy_peer.hpp` | not targeted | unchanged |
| `src/glasswyrmd/server.cpp` | 1242 | 65 | Server authority construction and destruction | Server::Server,~Server | `glasswyrmd/server.hpp`, `glasswyrmd/event_router.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/server.hpp` | 56 | 60 | Public Server ownership and process entry interface | Server | `glasswyrmd/client_connection.hpp`, `glasswyrmd/options.hpp`, `glasswyrmd/server_state.hpp` | complete | final narrow internal interface/destination |
| `src/glasswyrmd/server_reactor.cpp` | - | 137 | Tagged poll-set construction, dispatch, trace initialization, and top-level run loop | event_loop,initialize_trace,run | `glasswyrmd/server_runtime.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/server_runtime.hpp` | - | 108 | Private single-reactor state shared by semantic server runtime units | PendingFocusInput,PendingMutation,ServerRuntime,SignalRuntime,read_descriptor | `glasswyrmd/server.hpp`, `glasswyrmd/content_presenter.hpp`, `glasswyrmd/event_router.hpp`, `glasswyrmd/lifecycle_coordinator.hpp`, `glasswyrmd/runtime_bridge.hpp`, `glasswyrmd/synthetic_input_peer.hpp`, `input/input_state.hpp` | complete | final narrow internal interface/destination |
| `src/glasswyrmd/server_shutdown.cpp` | - | 17 | Ordered server runtime shutdown | shutdown | `glasswyrmd/server_runtime.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/server_signals.cpp` | - | 70 | Signal self-pipe installation, observation, and restoration | close,request_stop,request_stop_signal,start,stop_requested,~SignalRuntime | `glasswyrmd/server_runtime.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/server_state.cpp` | 1 | 1 | Implements server state for the X11 server | - | `glasswyrmd/server_state.hpp` | not targeted | unchanged |
| `src/glasswyrmd/server_state.hpp` | 158 | 158 | Declares server state for the X11 server | LifecycleSerialSource,ServerState,apply_policy,atoms,cleanup_client,commit_create_lifecycle | `glasswyrmd/atom_table.hpp`, `glasswyrmd/resource_table.hpp`, `glasswyrmd/lifecycle_snapshot.hpp` | not targeted | unchanged |
| `src/glasswyrmd/subtree_compositor.cpp` | 93 | 93 | Implements subtree compositor for the X11 server | PendingWindow,background,compose_top_level_subtree,paint_window | `glasswyrmd/subtree_compositor.hpp`, `core/geometry/rectangle.hpp` | not targeted | unchanged |
| `src/glasswyrmd/subtree_compositor.hpp` | 15 | 15 | Declares subtree compositor for the X11 server | - | `glasswyrmd/pixel_storage.hpp`, `glasswyrmd/resource_table.hpp` | not targeted | unchanged |
| `src/glasswyrmd/synthetic_input_peer.cpp` | 205 | 205 | Implements synthetic input peer for the X11 server | SyntheticInputPeer::SyntheticInputPeer,accept_provider,acknowledge,connected,connection_events,connection_fd | `glasswyrmd/synthetic_input_peer.hpp` | not targeted | unchanged |
| `src/glasswyrmd/synthetic_input_peer.hpp` | 64 | 64 | Declares synthetic input peer for the X11 server | Barrier,Button,ConnectionDeleter,Key,Kind,ListenerDeleter | - | not targeted | unchanged |
| `src/glasswyrmd/window.hpp` | 95 | 95 | Declares window for the X11 server | Above,BackgroundSource,Below,LifecycleStackMode,MapState,None | `glasswyrmd/property.hpp`, `glasswyrmd/pixel_storage.hpp` | not targeted | unchanged |
| `src/glasswyrmd/window_store.cpp` | - | 248 | Window ownership, hierarchy, mapping, configuration, and event selections | all_event_selections,configure_local,create_window,event_selection,is_policy_candidate,recompute_map_states | `glasswyrmd/resource_table.hpp` | complete | final semantic implementation/destination |
| `src/glasswyrmd/x11_listener.cpp` | - | 182 | X11 Unix listener path validation, socket ownership, bind, listen, and cleanup | close_listener,make_address,open_listener,prepare_socket_path,remove_stale_socket,unlink_owned_socket | `glasswyrmd/server.hpp` | complete | final semantic implementation/destination |
| `src/gwcomp/compositor.cpp` | 443 | 176 | Scene mutation validation, attachment state, and presentation delegation | Compositor::Compositor,abort_snapshot,apply,attach,begin_snapshot,commit | `gwcomp/compositor.hpp`, `gwcomp/presentation_transaction.hpp` | complete | final semantic implementation/destination |
| `src/gwcomp/compositor.hpp` | 92 | 92 | Declares compositor for the compositor process | PeerProfile,PresentedFrame | `backends/headless/frame_dump.hpp`, `backends/headless/output.hpp`, `compositor/buffer.hpp`, `compositor/scene.hpp`, `gwcomp/scene_manifest.hpp` | not targeted | unchanged |
| `src/gwcomp/contract_dispatch.cpp` | - | 204 | GWIPC snapshot decoding, typed compositor contract dispatch, acknowledgements, and releases | ContractDeleter,PayloadDeleter,dispatch_contract_message,enqueue_ack,enqueue_release,enqueue_releases | `gwcomp/contract_dispatch.hpp` | complete | final semantic implementation/destination |
| `src/gwcomp/contract_dispatch.hpp` | - | 23 | Internal typed compositor dispatch contract | ContractDispatchResult | `gwcomp/compositor.hpp` | complete | final narrow internal interface/destination |
| `src/gwcomp/main.cpp` | 391 | 13 | Compositor CLI entry point only | main | `gwcomp/options.hpp`, `gwcomp/runtime.hpp` | complete | final semantic implementation/destination |
| `src/gwcomp/options.cpp` | 91 | 91 | Implements options for the compositor process | parse_options,parse_positive,print_usage,take_path | `gwcomp/options.hpp`, `config.hpp` | not targeted | unchanged |
| `src/gwcomp/options.hpp` | 23 | 23 | Declares options for the compositor process | ExitFailure,ExitSuccess,Options,ParseOptionsResult,Run | - | not targeted | unchanged |
| `src/gwcomp/presentation_transaction.cpp` | - | 314 | Atomic staged-scene validation, software rendering, frame dump, promotion, and buffer retirement | commit,pixel_bytes,release_retired_buffers,surface_bounds | `gwcomp/presentation_transaction.hpp`, `gwcomp/compositor.hpp`, `render/software/renderer.hpp` | complete | final semantic implementation/destination |
| `src/gwcomp/presentation_transaction.hpp` | - | 24 | Internal synchronous presentation-transaction boundary | - | - | complete | final narrow internal interface/destination |
| `src/gwcomp/runtime.cpp` | - | 236 | Compositor listener, producer registry, tagged poll loop, signal loop, and dispatch scheduling | ConnectionDeleter,ListenerDeleter,MessageDeleter,operator(),prepare_dump_directory,run | `gwcomp/contract_dispatch.hpp`, `gwcomp/runtime.hpp` | complete | final semantic implementation/destination |
| `src/gwcomp/runtime.hpp` | - | 9 | Declares runtime for the compositor process | - | `gwcomp/options.hpp` | not targeted | unchanged |
| `src/gwcomp/scene_manifest.cpp` | 251 | 251 | Implements scene manifest for the compositor process | PayloadDeleter,append,applied,boolean,describe,hash_bytes | `gwcomp/scene_manifest.hpp` | not targeted | unchanged |
| `src/gwcomp/scene_manifest.hpp` | 33 | 33 | Declares scene manifest for the compositor process | SceneManifestResult,final | `compositor/scene.hpp` | not targeted | unchanged |
| `src/gwm/contract_dispatch.cpp` | - | 369 | GWIPC snapshot/control decoding, typed policy dispatch, policy commit, and acknowledgements | ContractDeleter,ContractPayloadDeleter,ControlDeleter,ControlPayloadDeleter,context_from,disconnect | `gwm/contract_dispatch.hpp` | complete | final semantic implementation/destination |
| `src/gwm/contract_dispatch.hpp` | - | 28 | Internal WM producer/session dispatch state | PeerState | `wm/transaction.hpp` | complete | final narrow internal interface/destination |
| `src/gwm/main.cpp` | 556 | 13 | Window-manager CLI entry point only | main | `gwm/options.hpp`, `gwm/runtime.hpp` | complete | final semantic implementation/destination |
| `src/gwm/options.cpp` | 71 | 71 | Implements options for the window-manager process | parse_options,parse_positive,print_usage | `gwm/options.hpp`, `config.hpp` | not targeted | unchanged |
| `src/gwm/options.hpp` | 21 | 21 | Declares options for the window-manager process | ExitFailure,ExitSuccess,Options,ParseOptionsResult,Run | - | not targeted | unchanged |
| `src/gwm/runtime.cpp` | - | 170 | Window-manager listener, producer session, tagged poll loop, and dispatch scheduling | ConnectionDeleter,ListenerDeleter,MessageDeleter,operator(),run | `gwm/runtime.hpp`, `gwm/contract_dispatch.hpp`, `gwm/signal_runtime.hpp` | complete | final semantic implementation/destination |
| `src/gwm/runtime.hpp` | - | 9 | Declares runtime for the window-manager process | - | `gwm/options.hpp` | complete | final narrow internal interface/destination |
| `src/gwm/signal_runtime.cpp` | - | 52 | WM signal self-pipe installation, drain, and restoration | close_signal_runtime,drain_signal_runtime,install_signal_runtime,wake_for_signal | `gwm/signal_runtime.hpp` | complete | final semantic implementation/destination |
| `src/gwm/signal_runtime.hpp` | - | 20 | Internal WM signal-pipe state | SignalRuntime | - | complete | final narrow internal interface/destination |
| `src/input/input_router.cpp` | 108 | 108 | Implements input router for the input model | clamp_pointer,crossing_details,crossing_focus,event_coordinates,hit_test_top_level,motion_delivery_mask | `input/input_router.hpp`, `protocol/x11/event_mask.hpp` | not targeted | unchanged |
| `src/input/input_router.hpp` | 56 | 56 | Declares input router for the input model | DeliveryTarget,EventCoordinates,RecipientSelection,RouteWindow | `glasswyrmd/resource_table.hpp`, `input/input_state.hpp`, `protocol/x11/crossing_event.hpp` | not targeted | unchanged |
| `src/input/input_state.cpp` | 68 | 68 | Implements input state for the input model | accept_time,advance_time,any_button_down,button_down,mask,reset_provider_state | `input/input_state.hpp`, `protocol/x11/event_mask.hpp` | not targeted | unchanged |
| `src/input/input_state.hpp` | 37 | 37 | Declares input state for the input model | Accepted,InputState,InvalidTransition,InvalidValue,TransitionStatus,key_down | - | not targeted | unchanged |
| `src/ipc/connection.cpp` | 1361 | 1361 | Implements connection for the GWIPC transport/API | duplicate_fds,flush,gwipc_connection_enqueue,gwipc_connection_enqueue_with_sequence,gwipc_connection_process_poll_events,gwipc_connection_receive | `ipc/connection_internal.hpp`, `ipc/endpoint.hpp`, `ipc/wire/compositor_contract.hpp`, `ipc/wire/control.hpp`, `ipc/wire/envelope.hpp`, `ipc/wire/lifecycle_contract.hpp`, `ipc/wire/input_contract.hpp`, `ipc/wire/policy_contract.hpp` | reviewed exception | 1,361-line hard-default exception; stable C ABI connection state machine; revisit M11 IPC-internals review |
| `src/ipc/connection_internal.hpp` | 17 | 17 | Declares connection internal for the GWIPC transport/API | - | `ipc/internal.hpp` | not targeted | unchanged |
| `src/ipc/contract_api.cpp` | 201 | 201 | Implements contract api for the GWIPC transport/API | DECODE_CASE,SIMPLE_ACCESS,SIMPLE_ENCODE,bytes,codec_status,color | `ipc/wire/compositor_contract.hpp`, `ipc/wire/lifecycle_contract.hpp`, `ipc/wire/input_contract.hpp`, `ipc/wire/policy_contract.hpp` | not targeted | unchanged |
| `src/ipc/control_api.cpp` | 201 | 201 | Implements control api for the GWIPC transport/API | gwipc_control_decode_message,gwipc_control_encode_snapshot_abort,gwipc_control_encode_snapshot_begin,gwipc_control_encode_snapshot_end,gwipc_control_payload,gwipc_control_payload_data | `ipc/wire/control.hpp` | not targeted | unchanged |
| `src/ipc/endpoint.cpp` | 183 | 183 | Implements endpoint for the GWIPC transport/API | bind_endpoint,cleanup_endpoint,connect_endpoint,is_live_socket,make_address,parent_path | `ipc/endpoint.hpp` | not targeted | unchanged |
| `src/ipc/endpoint.hpp` | 32 | 32 | Declares endpoint for the GWIPC transport/API | EndpointIdentity | `ipc/internal.hpp` | not targeted | unchanged |
| `src/ipc/internal.hpp` | 121 | 121 | Declares internal for the GWIPC transport/API | Config,QueuedRecord,SnapshotState,gwipc_connection,gwipc_listener,gwipc_message | - | not targeted | unchanged |
| `src/ipc/message.cpp` | 93 | 93 | Implements message for the GWIPC transport/API | QueuedRecord::QueuedRecord,close_fd,gwipc_message_destroy,gwipc_message_fd_count,gwipc_message_flags,gwipc_message_payload | `ipc/internal.hpp` | not targeted | unchanged |
| `src/ipc/public_api.cpp` | 311 | 311 | Implements public api for the GWIPC transport/API | all_zero,client_config,defaulted,fill_instance_id,gwipc_connection_connect,gwipc_connection_destroy | `ipc/endpoint.hpp`, `ipc/internal.hpp` | not targeted | unchanged |
| `src/ipc/wire/byte_reader.hpp` | 94 | 94 | Declares byte reader for the GWIPC wire codec | ByteReader,bytes,done,i32,remaining,string | - | not targeted | unchanged |
| `src/ipc/wire/byte_writer.hpp` | 51 | 51 | Declares byte writer for the GWIPC wire codec | ByteWriter,bytes,i32,string,u16,u32 | - | not targeted | unchanged |
| `src/ipc/wire/compositor_contract.cpp` | 529 | 529 | Implements compositor contract for the GWIPC wire codec | decode,decode_id,encode,final_status,in_range,read_color | `ipc/wire/compositor_contract.hpp`, `ipc/wire/byte_reader.hpp`, `ipc/wire/byte_writer.hpp` | not targeted | unchanged |
| `src/ipc/wire/compositor_contract.hpp` | 173 | 173 | Declares compositor contract for the GWIPC wire codec | AlphaSemantics,Argb8888,BufferAttach,BufferDetach,BufferRelease,BufferReleaseReason | `ipc/wire/types.hpp` | not targeted | unchanged |
| `src/ipc/wire/control.cpp` | 434 | 434 | Implements control for the GWIPC wire codec | decode,encode,finished,in_range,nonzero,valid_utf8 | `ipc/wire/control.hpp`, `ipc/wire/byte_reader.hpp`, `ipc/wire/byte_writer.hpp` | not targeted | unchanged |
| `src/ipc/wire/control.hpp` | 102 | 102 | Declares control for the GWIPC wire codec | GWIPC_DECLARE_CODEC,Hello,Ping,Pong,ProtocolError,Reject | `ipc/wire/envelope.hpp` | not targeted | unchanged |
| `src/ipc/wire/envelope.cpp` | 77 | 77 | Implements envelope for the GWIPC wire codec | decode_envelope,encode_envelope | `ipc/wire/envelope.hpp`, `ipc/wire/byte_reader.hpp`, `ipc/wire/byte_writer.hpp` | not targeted | unchanged |
| `src/ipc/wire/envelope.hpp` | 34 | 34 | Declares envelope for the GWIPC wire codec | Envelope | `ipc/wire/types.hpp` | not targeted | unchanged |
| `src/ipc/wire/input_contract.cpp` | 15 | 15 | Implements input contract for the GWIPC wire codec | decode,encode | `ipc/wire/input_contract.hpp`, `ipc/wire/byte_reader.hpp`, `ipc/wire/byte_writer.hpp` | not targeted | unchanged |
| `src/ipc/wire/input_contract.hpp` | 16 | 16 | Declares input contract for the GWIPC wire codec | Accepted,Clamped,FocusRejected,FocusUnchanged,GWIPC_INPUT_CODEC,InvalidTransition | `ipc/wire/types.hpp` | not targeted | unchanged |
| `src/ipc/wire/lifecycle_contract.cpp` | 119 | 119 | Implements lifecycle contract for the GWIPC wire codec | decode,encode,tri_state | `ipc/wire/lifecycle_contract.hpp`, `ipc/wire/byte_reader.hpp`, `ipc/wire/byte_writer.hpp` | not targeted | unchanged |
| `src/ipc/wire/lifecycle_contract.hpp` | 45 | 45 | Declares lifecycle contract for the GWIPC wire codec | Above,Below,None,PolicyLifecycleWindowUpsert,PolicyStackMode,SurfacePolicyUpsert | `ipc/wire/policy_contract.hpp` | not targeted | unchanged |
| `src/ipc/wire/policy_contract.cpp` | 234 | 234 | Implements policy contract for the GWIPC wire codec | decode,encode,extent,tri | `ipc/wire/policy_contract.hpp`, `ipc/wire/byte_reader.hpp`, `ipc/wire/byte_writer.hpp` | not targeted | unchanged |
| `src/ipc/wire/policy_contract.hpp` | 85 | 85 | Declares policy contract for the GWIPC wire codec | GWIPC_POLICY_CODEC,PolicyAcknowledged,PolicyAppliedState,PolicyCommit,PolicyContextUpsert,PolicyMapIntent | `ipc/wire/types.hpp` | not targeted | unchanged |
| `src/ipc/wire/types.hpp` | 126 | 126 | Declares types for the GWIPC wire codec | Capability,CodecStatus,MessageFlag,MessageType,ProtocolErrorCode,RejectReason | - | not targeted | unchanged |
| `src/protocol/x11/atoms.hpp` | 90 | 90 | Declares atoms for the X11 protocol codec/model | PredefinedAtom | - | not targeted | unchanged |
| `src/protocol/x11/byte_cursor.hpp` | 138 | 138 | Declares byte cursor for the X11 protocol codec/model | ByteReader,ByteWriter,offset,read_bytes,read_u16,read_u32 | `protocol/x11/byte_order.hpp` | not targeted | unchanged |
| `src/protocol/x11/byte_order.hpp` | 24 | 24 | Declares byte order for the X11 protocol codec/model | ByteOrder,byte_order_from_marker | - | not targeted | unchanged |
| `src/protocol/x11/core.hpp` | 120 | 120 | Declares core for the X11 protocol codec/model | CoreErrorCode,CoreEventType,CoreMapState,CoreOpcode,CoreStackMode,wire_sequence | - | not targeted | unchanged |
| `src/protocol/x11/crossing_event.cpp` | 21 | 21 | Implements crossing event for the X11 protocol codec/model | encode_crossing_event | `protocol/x11/crossing_event.hpp`, `protocol/x11/byte_cursor.hpp` | not targeted | unchanged |
| `src/protocol/x11/crossing_event.hpp` | 27 | 27 | Declares crossing event for the X11 protocol codec/model | CrossingEvent,Grab,Normal,NotifyDetail,NotifyMode,Ungrab | `protocol/x11/input_event.hpp` | not targeted | unchanged |
| `src/protocol/x11/event.cpp` | 76 | 76 | Implements event for the X11 protocol codec/model | encode_configure_notify,encode_destroy_notify,encode_map_notify,encode_unmap_notify,event_header,finish_event | `protocol/x11/event.hpp`, `protocol/x11/byte_cursor.hpp`, `protocol/x11/core.hpp` | not targeted | unchanged |
| `src/protocol/x11/event.hpp` | 48 | 48 | Declares event for the X11 protocol codec/model | ConfigureNotifyEvent,DestroyNotifyEvent,MapNotifyEvent,UnmapNotifyEvent | `protocol/x11/byte_order.hpp` | not targeted | unchanged |
| `src/protocol/x11/event_mask.hpp` | 52 | 52 | Declares event mask for the X11 protocol codec/model | - | - | not targeted | unchanged |
| `src/protocol/x11/exposure_event.cpp` | 39 | 39 | Implements exposure event for the X11 protocol codec/model | encode_expose,encode_graphics_expose,encode_no_expose,finish,header | `protocol/x11/exposure_event.hpp`, `protocol/x11/byte_cursor.hpp`, `protocol/x11/core.hpp` | not targeted | unchanged |
| `src/protocol/x11/exposure_event.hpp` | 15 | 15 | Declares exposure event for the X11 protocol codec/model | ExposeEvent,GraphicsExposeEvent,NoExposeEvent,count,drawable,height | `protocol/x11/byte_order.hpp` | not targeted | unchanged |
| `src/protocol/x11/focus_event.cpp` | 16 | 16 | Implements focus event for the X11 protocol codec/model | encode_focus_event | `protocol/x11/focus_event.hpp`, `protocol/x11/byte_cursor.hpp` | not targeted | unchanged |
| `src/protocol/x11/focus_event.hpp` | 14 | 14 | Declares focus event for the X11 protocol codec/model | FocusEvent | `protocol/x11/crossing_event.hpp` | not targeted | unchanged |
| `src/protocol/x11/input_event.cpp` | 28 | 28 | Implements input event for the X11 protocol codec/model | encode_input_event | `protocol/x11/input_event.hpp`, `protocol/x11/byte_cursor.hpp` | not targeted | unchanged |
| `src/protocol/x11/input_event.hpp` | 29 | 29 | Declares input event for the X11 protocol codec/model | InputEvent | `protocol/x11/byte_order.hpp`, `protocol/x11/core.hpp` | not targeted | unchanged |
| `src/protocol/x11/lifecycle_request.cpp` | 96 | 96 | Implements lifecycle request for the X11 protocol codec/model | decode_configure_window,decode_map_subwindows,decode_map_window,decode_unmap_subwindows,decode_unmap_window,decode_window | `protocol/x11/lifecycle_request.hpp`, `protocol/x11/byte_cursor.hpp` | not targeted | unchanged |
| `src/protocol/x11/lifecycle_request.hpp` | 52 | 52 | Declares lifecycle request for the X11 protocol codec/model | BadLength,BadMatch,BadValue,Complete,ConfigureValueMask,ConfigureWindowRequest | `protocol/x11/byte_order.hpp`, `protocol/x11/core.hpp` | not targeted | unchanged |
| `src/protocol/x11/reply.cpp` | 91 | 91 | Implements reply for the X11 protocol codec/model | ReplyBuilder::ReplyBuilder,encode_core_error,ensure_fixed_capacity,padding_for,write_padding,write_payload | `protocol/x11/reply.hpp` | not targeted | unchanged |
| `src/protocol/x11/reply.hpp` | 52 | 52 | Declares reply for the X11 protocol codec/model | CoreError,ReplyBuilder | `protocol/x11/byte_cursor.hpp`, `protocol/x11/core.hpp` | not targeted | unchanged |
| `src/protocol/x11/request.cpp` | 86 | 86 | Implements request for the X11 protocol codec/model | RequestFramer::RequestFramer,consume,eof,inspect_header,reset | `protocol/x11/request.hpp`, `core/checked_math.hpp`, `protocol/x11/byte_cursor.hpp` | not targeted | unchanged |
| `src/protocol/x11/request.hpp` | 65 | 65 | Declares request for the X11 protocol codec/model | FramedRequest,RequestFrameResult,RequestFrameStatus,RequestFramer,body,expected_size | `protocol/x11/byte_order.hpp` | not targeted | unchanged |
| `src/protocol/x11/screen_model.hpp` | 25 | 25 | Declares screen model for the X11 protocol codec/model | ScreenModel | - | not targeted | unchanged |
| `src/protocol/x11/setup.cpp` | 235 | 235 | Implements setup for the X11 protocol codec/model | SetupParser::SetupParser,consume,encode_setup_failure,encode_setup_success,eof,evaluate_setup_request | `protocol/x11/setup.hpp`, `core/checked_math.hpp`, `protocol/x11/byte_cursor.hpp` | not targeted | unchanged |
| `src/protocol/x11/setup.hpp` | 88 | 88 | Declares setup for the X11 protocol codec/model | ParseResult,ParseStatus,SetupDecision,SetupParser,SetupReplyConfig,SetupRequest | `protocol/x11/byte_order.hpp`, `protocol/x11/screen_model.hpp` | not targeted | unchanged |
| `src/render/software/blend.cpp` | 45 | 45 | Implements blend for the software renderer | apply_opacity,blend,over_channel,scale,source_over | `render/software/blend.hpp` | not targeted | unchanged |
| `src/render/software/blend.hpp` | 17 | 17 | Declares blend for the software renderer | - | `render/software/pixel.hpp` | not targeted | unchanged |
| `src/render/software/pixel.hpp` | 53 | 53 | Declares pixel for the software renderer | Argb8888Premultiplied,Pixel,PixelFormat,Xrgb8888,is_premultiplied,load_u32 | - | not targeted | unchanged |
| `src/render/software/renderer.cpp` | 101 | 101 | Implements renderer for the software renderer | clear,composite,valid_framebuffer,valid_image | `render/software/renderer.hpp`, `render/software/blend.hpp` | not targeted | unchanged |
| `src/render/software/renderer.hpp` | 36 | 36 | Declares renderer for the software renderer | FramebufferView,ImageView,InvalidPremultipliedPixel,InvalidView,RenderResult,Success | `compositor/rectangle.hpp`, `render/software/pixel.hpp` | not targeted | unchanged |
| `src/scaffold/component.cpp` | 42 | 42 | Implements component for the component scaffold | component_info,run_placeholder | `config.hpp` | not targeted | unchanged |
| `src/wm/policy_engine.cpp` | 396 | 396 | Implements policy engine for the window-policy model | applied_state,clamp_coordinate,decoration,encode_policy_window_state,evaluate,extent_fits | `wm/policy_engine.hpp` | not targeted | unchanged |
| `src/wm/policy_engine.hpp` | 17 | 17 | Declares policy engine for the window-policy model | - | `wm/types.hpp` | not targeted | unchanged |
| `src/wm/transaction.cpp` | 80 | 80 | Implements transaction for the window-policy model | abort_snapshot,begin_snapshot,commit,disconnect,end_snapshot,remove | `wm/transaction.hpp` | not targeted | unchanged |
| `src/wm/transaction.hpp` | 42 | 42 | Declares transaction for the window-policy model | Transaction,committed_policy,committed_raw,pending,snapshot_active | `wm/policy_engine.hpp` | not targeted | unchanged |
| `src/wm/types.hpp` | 119 | 119 | Declares types for the window-policy model | Above,AppliedState,Below,Context,DecorationPreference,Dialog | - | not targeted | unchanged |

## Guard interpretation

The guard machine-enforces exact allowlist counts, the 1,000-line hard default,
the 600-line new/materially-rewritten limit, coordinator and main limits, and
the milestone-specific final caps. It emits advisory notes over the 100-line
ordinary-function target and explicit review findings over 150 lines. It also
rejects stale, missing, malformed, duplicate, or newly introduced
DRM/VT/presentation/runtime allowlist entries.

At final source commit `fe2ddde` the guard passes with eight reviewed function
spans. The sole hard-default exception is `src/ipc/connection.cpp`; no new M10
file is allowlisted.
