#include <glasswyrm/ipc/contracts.h>
#include <glasswyrm/ipc/lifecycle.h>
#include <glasswyrm/ipc/input.h>
#include <glasswyrm/ipc/policy.h>
#include <glasswyrm/ipc/session.h>

#include "ipc/wire/compositor_contract.hpp"
#include "ipc/wire/lifecycle_contract.hpp"
#include "ipc/wire/input_contract.hpp"
#include "ipc/wire/policy_contract.hpp"
#include "ipc/wire/session_contract.hpp"

#include <algorithm>
#include <new>
#include <variant>
#include <vector>

namespace w = gw::ipc::wire;

struct gwipc_contract_payload { std::vector<std::uint8_t> bytes; };
using ContractValue = std::variant<w::OutputUpsert, w::OutputRemove,
    w::SurfaceUpsert, w::SurfaceRemove, w::BufferAttach, w::BufferDetach,
    w::BufferRelease, w::SurfaceDamage, w::FrameCommit, w::FrameAcknowledged,
    w::PolicyContextUpsert, w::PolicyWindowUpsert, w::PolicyWindowRemove,
    w::PolicyCommit, w::PolicyWindowState, w::PolicyAcknowledged,
    w::PolicyLifecycleWindowUpsert, w::SurfacePolicyUpsert,
    w::SyntheticMotion, w::SyntheticButton, w::SyntheticKey,
    w::SyntheticBarrier, w::SyntheticInputAcknowledged,
    w::PolicyBindingsUpsert, w::SessionStateChange,
    w::SessionStateAcknowledged>;
struct gwipc_decoded_contract {
  std::uint16_t type{};
  ContractValue value{w::OutputRemove{}};
  std::vector<gwipc_damage_rectangle> rectangles;
  gwipc_output_upsert output_upsert{}; gwipc_output_remove output_remove{};
  gwipc_surface_upsert surface_upsert{}; gwipc_surface_remove surface_remove{};
  gwipc_buffer_attach buffer_attach{}; gwipc_buffer_detach buffer_detach{};
  gwipc_buffer_release buffer_release{}; gwipc_surface_damage surface_damage{};
  gwipc_frame_commit frame_commit{}; gwipc_frame_acknowledged frame_acknowledged{};
  gwipc_policy_context_upsert policy_context_upsert{};
  gwipc_policy_window_upsert policy_window_upsert{};
  gwipc_policy_window_remove policy_window_remove{}; gwipc_policy_commit policy_commit{};
  gwipc_policy_window_state policy_window_state{}; gwipc_policy_acknowledged policy_acknowledged{};
  gwipc_policy_lifecycle_window_upsert policy_lifecycle_window_upsert{};
  gwipc_surface_policy_upsert surface_policy_upsert{};
  gwipc_synthetic_motion synthetic_motion{};
  gwipc_synthetic_button synthetic_button{};
  gwipc_synthetic_key synthetic_key{};
  gwipc_synthetic_barrier synthetic_barrier{};
  gwipc_synthetic_input_acknowledged synthetic_input_acknowledged{};
  gwipc_policy_bindings_upsert policy_bindings_upsert{};
  gwipc_session_state_change session_state_change{};
  gwipc_session_state_acknowledged session_state_acknowledged{};
};

namespace {
template<class T> bool valid_input(const T* v) {
  return v && v->struct_size >= sizeof(T) &&
    std::all_of(std::begin(v->reserved), std::end(v->reserved), [](auto x){ return x == 0; });
}
w::SdrColorMetadata color(const gwipc_sdr_color_metadata& c) { return {
  static_cast<w::SdrColorSpace>(c.color_space), static_cast<w::TransferFunction>(c.transfer_function),
  static_cast<w::ColorPrimaries>(c.primaries), c.luminance_available != 0,
  c.minimum_luminance_millinit, c.maximum_luminance_millinit, c.max_frame_average_luminance_millinit}; }
gwipc_sdr_color_metadata color(const w::SdrColorMetadata& c) { return {
  static_cast<gwipc_sdr_color_space>(c.color_space), static_cast<gwipc_transfer_function>(c.transfer_function),
  static_cast<gwipc_color_primaries>(c.primaries), static_cast<uint8_t>(c.luminance_available),
  c.minimum_luminance_millinit, c.maximum_luminance_millinit, c.max_frame_average_luminance_millinit}; }
template<class T> gwipc_status make_payload(T value, gwipc_contract_payload** out) {
  if (!out) return GWIPC_STATUS_INVALID_ARGUMENT;
  *out = nullptr;
  try { auto p = new (std::nothrow) gwipc_contract_payload; if (!p) return GWIPC_STATUS_OUT_OF_MEMORY;
    p->bytes = w::encode(value);
    T checked;
    if (w::decode(p->bytes, checked) != w::CodecStatus::Ok) {
      delete p;
      return GWIPC_STATUS_INVALID_ARGUMENT;
    }
    *out = p; return GWIPC_STATUS_OK;
  } catch (const std::bad_alloc&) { return GWIPC_STATUS_OUT_OF_MEMORY; } catch (...) { return GWIPC_STATUS_SYSTEM_ERROR; }
}
gwipc_status codec_status(w::CodecStatus s) { return s == w::CodecStatus::Ok ? GWIPC_STATUS_OK : GWIPC_STATUS_PROTOCOL_ERROR; }
}

extern "C" {
#define SIMPLE_ENCODE(Pub, name, Wire, ...) \
gwipc_status gwipc_contract_encode_##name(const Pub* v, gwipc_contract_payload** out) { \
  if (!valid_input(v) || !out) return GWIPC_STATUS_INVALID_ARGUMENT; \
  return make_payload(Wire __VA_ARGS__, out); }

SIMPLE_ENCODE(gwipc_output_remove, output_remove, w::OutputRemove, {v->output_id})
SIMPLE_ENCODE(gwipc_surface_remove, surface_remove, w::SurfaceRemove, {v->surface_id})
SIMPLE_ENCODE(gwipc_buffer_detach, buffer_detach, w::BufferDetach, {v->surface_id, v->buffer_id})
SIMPLE_ENCODE(gwipc_buffer_release, buffer_release, w::BufferRelease, {v->buffer_id, static_cast<w::BufferReleaseReason>(v->reason)})
SIMPLE_ENCODE(gwipc_synthetic_motion, synthetic_motion, w::SyntheticMotion, {v->input_id,v->time_ms,v->root_x,v->root_y,v->flags})
SIMPLE_ENCODE(gwipc_synthetic_button, synthetic_button, w::SyntheticButton, {v->input_id,v->time_ms,v->button,v->pressed,v->reserved16,v->flags})
SIMPLE_ENCODE(gwipc_synthetic_key, synthetic_key, w::SyntheticKey, {v->input_id,v->time_ms,v->keycode,v->pressed,v->reserved16,v->flags})
SIMPLE_ENCODE(gwipc_synthetic_barrier, synthetic_barrier, w::SyntheticBarrier, {v->input_id,v->flags})
SIMPLE_ENCODE(gwipc_synthetic_input_acknowledged, synthetic_input_acknowledged, w::SyntheticInputAcknowledged, {v->input_id,v->time_ms,static_cast<w::SyntheticInputResult>(v->result),v->root_x,v->root_y,v->pointer_window,v->focus_window,v->state,v->reserved16,v->delivered_event_count,v->flags})
SIMPLE_ENCODE(gwipc_frame_commit, frame_commit, w::FrameCommit, {v->commit_id, v->output_id, v->producer_generation, v->flags})
SIMPLE_ENCODE(gwipc_frame_acknowledged, frame_acknowledged, w::FrameAcknowledged, {v->commit_id, v->output_id, v->presented_generation, static_cast<w::FrameResult>(v->result)})
SIMPLE_ENCODE(gwipc_policy_context_upsert, policy_context_upsert, w::PolicyContextUpsert, {v->root_window_id,v->workspace_id,v->output_id,v->work_x,v->work_y,v->work_width,v->work_height,v->flags})
SIMPLE_ENCODE(gwipc_policy_window_remove, policy_window_remove, w::PolicyWindowRemove, {v->window_id})
SIMPLE_ENCODE(gwipc_policy_commit, policy_commit, w::PolicyCommit, {v->commit_id,v->producer_generation,v->flags})
SIMPLE_ENCODE(gwipc_policy_acknowledged, policy_acknowledged, w::PolicyAcknowledged, {v->commit_id,v->producer_generation,v->applied_generation,v->policy_hash,v->window_count,static_cast<w::PolicyResult>(v->result)})
SIMPLE_ENCODE(gwipc_session_state_change, session_state_change, w::SessionStateChange, {v->generation,static_cast<w::SessionState>(v->state),v->flags})
SIMPLE_ENCODE(gwipc_session_state_acknowledged, session_state_acknowledged, w::SessionStateAcknowledged, {v->generation,static_cast<w::SessionState>(v->state),static_cast<w::SessionStateResult>(v->result),v->flags})
#undef SIMPLE_ENCODE

gwipc_status gwipc_contract_encode_output_upsert(const gwipc_output_upsert* v, gwipc_contract_payload** out) {
  if (!valid_input(v) || !out || v->enabled > 1) return GWIPC_STATUS_INVALID_ARGUMENT;
  return make_payload(w::OutputUpsert{v->output_id, v->enabled != 0, v->logical_x, v->logical_y,
    v->logical_width, v->logical_height, v->physical_pixel_width, v->physical_pixel_height,
    v->refresh_millihertz, v->scale_numerator, v->scale_denominator,
    static_cast<w::Transform>(v->transform), color(v->color)}, out); }
gwipc_status gwipc_contract_encode_surface_upsert(const gwipc_surface_upsert* v, gwipc_contract_payload** out) {
  if (!valid_input(v) || !out || v->visible > 1 || v->clipping > 1) return GWIPC_STATUS_INVALID_ARGUMENT;
  return make_payload(w::SurfaceUpsert{v->surface_id, v->x11_window_id, v->parent_surface_id, v->output_id,
    v->logical_x,v->logical_y,v->logical_width,v->logical_height,v->stacking,v->visible!=0,v->clipping!=0,
    v->clip_x,v->clip_y,v->clip_width,v->clip_height,static_cast<w::Transform>(v->transform),v->opacity,
    v->scale_numerator,v->scale_denominator,color(v->color),v->presentation_flags,
    static_cast<w::TriState>(v->fullscreen_eligible),static_cast<w::TriState>(v->direct_scanout_eligible)}, out); }
gwipc_status gwipc_contract_encode_buffer_attach(const gwipc_buffer_attach* v, gwipc_contract_payload** out) {
  if (!valid_input(v) || !out) return GWIPC_STATUS_INVALID_ARGUMENT;
  return make_payload(w::BufferAttach{v->buffer_id,v->surface_id,v->width,v->height,v->stride,v->byte_offset,
    v->storage_size,static_cast<w::PixelFormat>(v->pixel_format),v->modifier,
    static_cast<w::AlphaSemantics>(v->alpha_semantics),color(v->color),
    static_cast<w::SynchronizationMode>(v->synchronization),v->flags}, out); }
gwipc_status gwipc_contract_encode_surface_damage(const gwipc_surface_damage* v, gwipc_contract_payload** out) {
  if (!valid_input(v) || !out || (v->rectangle_count && !v->rectangles) || v->rectangle_count > GWIPC_MAXIMUM_DAMAGE_RECTANGLES) return GWIPC_STATUS_INVALID_ARGUMENT;
  w::SurfaceDamage d; d.surface_id=v->surface_id; d.rectangles.reserve(v->rectangle_count);
  try { for(size_t i=0;i<v->rectangle_count;++i) d.rectangles.push_back({v->rectangles[i].x,v->rectangles[i].y,v->rectangles[i].width,v->rectangles[i].height}); }
  catch(const std::bad_alloc&) { return GWIPC_STATUS_OUT_OF_MEMORY; }
  return make_payload(std::move(d),out); }
gwipc_status gwipc_contract_encode_policy_window_upsert(const gwipc_policy_window_upsert* v, gwipc_contract_payload** out) {
  if(!valid_input(v)||!out)return GWIPC_STATUS_INVALID_ARGUMENT;
  return make_payload(w::PolicyWindowUpsert{v->window_id,v->parent_window_id,v->transient_for,v->workspace_id,v->requested_x,v->requested_y,v->requested_width,v->requested_height,v->border_width,static_cast<w::PolicyWindowType>(v->window_type),static_cast<w::PolicyMapIntent>(v->map_intent),v->override_redirect!=0,static_cast<uint16_t>(v->decoration_preference),v->fullscreen_requested!=0,v->maximized_requested!=0,v->minimized_requested!=0,v->attention_requested!=0,v->creation_serial,v->map_serial,v->focus_serial,v->flags},out); }
gwipc_status gwipc_contract_encode_policy_window_state(const gwipc_policy_window_state* v, gwipc_contract_payload** out) {
  if(!valid_input(v)||!out)return GWIPC_STATUS_INVALID_ARGUMENT;
  return make_payload(w::PolicyWindowState{v->window_id,v->transient_for,v->workspace_id,v->output_id,v->final_x,v->final_y,v->final_width,v->final_height,v->stacking,static_cast<w::PolicyWindowType>(v->window_type),static_cast<w::PolicyAppliedState>(v->applied_state),v->visible!=0,v->focused!=0,v->managed!=0,v->decoration_eligible!=0,v->override_redirect!=0,v->attention_requested!=0,static_cast<uint16_t>(v->fullscreen_eligible),static_cast<uint16_t>(v->direct_scanout_eligible),v->flags},out); }
gwipc_status gwipc_contract_encode_policy_lifecycle_window_upsert(const gwipc_policy_lifecycle_window_upsert* v, gwipc_contract_payload** out) {
  if(!valid_input(v)||!valid_input(&v->window)||!out||v->window.override_redirect>1||v->window.fullscreen_requested>1||v->window.maximized_requested>1||v->window.minimized_requested>1||v->window.attention_requested>1)return GWIPC_STATUS_INVALID_ARGUMENT;
  const auto&pub=v->window;
  return make_payload(w::PolicyLifecycleWindowUpsert{{pub.window_id,pub.parent_window_id,pub.transient_for,pub.workspace_id,pub.requested_x,pub.requested_y,pub.requested_width,pub.requested_height,pub.border_width,static_cast<w::PolicyWindowType>(pub.window_type),static_cast<w::PolicyMapIntent>(pub.map_intent),pub.override_redirect!=0,static_cast<uint16_t>(pub.decoration_preference),pub.fullscreen_requested!=0,pub.maximized_requested!=0,pub.minimized_requested!=0,pub.attention_requested!=0,pub.creation_serial,pub.map_serial,pub.focus_serial,pub.flags},v->geometry_serial,v->stack_serial,v->stack_sibling,static_cast<w::PolicyStackMode>(v->stack_mode),v->flags},out); }
gwipc_status gwipc_contract_encode_surface_policy_upsert(const gwipc_surface_policy_upsert* v, gwipc_contract_payload** out) {
  if(!valid_input(v)||!out||v->focused>1||v->managed>1||v->decoration_eligible>1||v->override_redirect>1||v->attention_requested>1)return GWIPC_STATUS_INVALID_ARGUMENT;
  return make_payload(w::SurfacePolicyUpsert{v->surface_id,v->x11_window_id,v->workspace_id,static_cast<w::PolicyWindowType>(v->window_type),static_cast<w::PolicyAppliedState>(v->applied_state),v->focused!=0,v->managed!=0,v->decoration_eligible!=0,v->override_redirect!=0,v->attention_requested!=0,static_cast<uint16_t>(v->fullscreen_eligible),static_cast<uint16_t>(v->direct_scanout_eligible),v->flags},out); }
gwipc_status gwipc_contract_encode_policy_bindings_upsert(const gwipc_policy_bindings_upsert* v, gwipc_contract_payload** out) {
  if(!valid_input(v)||!out||v->reserved16||v->reserved_buttons||v->reserved_flags||v->raise_on_focus>1||v->consume_wm_bindings>1)return GWIPC_STATUS_INVALID_ARGUMENT;
  return make_payload(w::PolicyBindingsUpsert{v->move_modifiers,v->resize_modifiers,v->close_modifiers,v->move_button,v->resize_button,v->close_keysym,v->minimum_width,v->minimum_height,v->raise_on_focus!=0,v->consume_wm_bindings!=0},out); }

const uint8_t* gwipc_contract_payload_data(const gwipc_contract_payload* p,size_t* n) { if(n)*n=p?p->bytes.size():0; return !p||p->bytes.empty()?nullptr:p->bytes.data(); }
void gwipc_contract_payload_destroy(gwipc_contract_payload* p) { delete p; }

gwipc_status gwipc_contract_decode_message(const gwipc_message* m, gwipc_decoded_contract** out) {
  if (!m || !out) return GWIPC_STATUS_INVALID_ARGUMENT;
  *out=nullptr;
  size_t n=0; const auto* p=gwipc_message_payload(m,&n); std::span<const uint8_t> bytes(p,n);
  try { auto d=new(std::nothrow) gwipc_decoded_contract; if(!d)return GWIPC_STATUS_OUT_OF_MEMORY; d->type=gwipc_message_type(m); w::CodecStatus s=w::CodecStatus::InvalidValue;
#define DECODE_CASE(msg, T) case msg: { T v; s=w::decode(bytes,v); d->value=std::move(v); break; }
    switch(d->type) { DECODE_CASE(GWIPC_MESSAGE_OUTPUT_UPSERT,w::OutputUpsert) DECODE_CASE(GWIPC_MESSAGE_OUTPUT_REMOVE,w::OutputRemove)
      DECODE_CASE(GWIPC_MESSAGE_SURFACE_UPSERT,w::SurfaceUpsert) DECODE_CASE(GWIPC_MESSAGE_SURFACE_REMOVE,w::SurfaceRemove)
      DECODE_CASE(GWIPC_MESSAGE_BUFFER_ATTACH,w::BufferAttach) DECODE_CASE(GWIPC_MESSAGE_BUFFER_DETACH,w::BufferDetach)
      DECODE_CASE(GWIPC_MESSAGE_BUFFER_RELEASE,w::BufferRelease) DECODE_CASE(GWIPC_MESSAGE_SURFACE_DAMAGE,w::SurfaceDamage)
      DECODE_CASE(GWIPC_MESSAGE_FRAME_COMMIT,w::FrameCommit) DECODE_CASE(GWIPC_MESSAGE_FRAME_ACKNOWLEDGED,w::FrameAcknowledged)
      DECODE_CASE(GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT,w::PolicyContextUpsert) DECODE_CASE(GWIPC_MESSAGE_POLICY_WINDOW_UPSERT,w::PolicyWindowUpsert)
      DECODE_CASE(GWIPC_MESSAGE_POLICY_WINDOW_REMOVE,w::PolicyWindowRemove) DECODE_CASE(GWIPC_MESSAGE_POLICY_COMMIT,w::PolicyCommit)
      DECODE_CASE(GWIPC_MESSAGE_POLICY_WINDOW_STATE,w::PolicyWindowState) DECODE_CASE(GWIPC_MESSAGE_POLICY_ACKNOWLEDGED,w::PolicyAcknowledged)
      DECODE_CASE(GWIPC_MESSAGE_POLICY_LIFECYCLE_WINDOW_UPSERT,w::PolicyLifecycleWindowUpsert) DECODE_CASE(GWIPC_MESSAGE_SURFACE_POLICY_UPSERT,w::SurfacePolicyUpsert)
      DECODE_CASE(GWIPC_MESSAGE_SYNTHETIC_MOTION,w::SyntheticMotion) DECODE_CASE(GWIPC_MESSAGE_SYNTHETIC_BUTTON,w::SyntheticButton)
      DECODE_CASE(GWIPC_MESSAGE_SYNTHETIC_KEY,w::SyntheticKey) DECODE_CASE(GWIPC_MESSAGE_SYNTHETIC_BARRIER,w::SyntheticBarrier)
      DECODE_CASE(GWIPC_MESSAGE_SYNTHETIC_INPUT_ACKNOWLEDGED,w::SyntheticInputAcknowledged)
      DECODE_CASE(GWIPC_MESSAGE_POLICY_BINDINGS_UPSERT,w::PolicyBindingsUpsert)
      DECODE_CASE(GWIPC_MESSAGE_SESSION_STATE_CHANGE,w::SessionStateChange)
      DECODE_CASE(GWIPC_MESSAGE_SESSION_STATE_ACKNOWLEDGED,w::SessionStateAcknowledged)
      default: delete d; return GWIPC_STATUS_INVALID_ARGUMENT; }
#undef DECODE_CASE
    if(s!=w::CodecStatus::Ok){delete d;return codec_status(s);}
    if (d->type == GWIPC_MESSAGE_SURFACE_DAMAGE) {
      const auto& damage = std::get<w::SurfaceDamage>(d->value);
      d->rectangles.reserve(damage.rectangles.size());
      for (const auto& r : damage.rectangles)
        d->rectangles.push_back({r.x, r.y, r.width, r.height});
    }
    *out=d; return GWIPC_STATUS_OK;
  } catch(const std::bad_alloc&){return GWIPC_STATUS_OUT_OF_MEMORY;} catch(...){return GWIPC_STATUS_SYSTEM_ERROR;} }
uint16_t gwipc_decoded_contract_type(const gwipc_decoded_contract* d){return d?d->type:0;}
void gwipc_decoded_contract_destroy(gwipc_decoded_contract* d){delete d;}

#define SIMPLE_ACCESS(name,msg,Wire,...) const gwipc_##name* gwipc_decoded_##name(const gwipc_decoded_contract* d){ if(!d||d->type!=msg)return nullptr; const auto& v=std::get<Wire>(d->value); auto& o=const_cast<gwipc_decoded_contract*>(d)->name; o=gwipc_##name __VA_ARGS__; return &o; }
SIMPLE_ACCESS(output_remove,GWIPC_MESSAGE_OUTPUT_REMOVE,w::OutputRemove,{sizeof(o),v.output_id,{}})
SIMPLE_ACCESS(surface_remove,GWIPC_MESSAGE_SURFACE_REMOVE,w::SurfaceRemove,{sizeof(o),v.surface_id,{}})
SIMPLE_ACCESS(buffer_detach,GWIPC_MESSAGE_BUFFER_DETACH,w::BufferDetach,{sizeof(o),v.surface_id,v.buffer_id,{}})
SIMPLE_ACCESS(buffer_release,GWIPC_MESSAGE_BUFFER_RELEASE,w::BufferRelease,{sizeof(o),v.buffer_id,static_cast<gwipc_buffer_release_reason>(v.reason),{}})
SIMPLE_ACCESS(frame_commit,GWIPC_MESSAGE_FRAME_COMMIT,w::FrameCommit,{sizeof(o),v.commit_id,v.output_id,v.producer_generation,v.flags,{}})
SIMPLE_ACCESS(frame_acknowledged,GWIPC_MESSAGE_FRAME_ACKNOWLEDGED,w::FrameAcknowledged,{sizeof(o),v.commit_id,v.output_id,v.presented_generation,static_cast<gwipc_frame_result>(v.result),{}})
SIMPLE_ACCESS(policy_context_upsert,GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT,w::PolicyContextUpsert,{sizeof(o),v.root_window_id,v.workspace_id,v.output_id,v.work_x,v.work_y,v.work_width,v.work_height,v.flags,{}})
SIMPLE_ACCESS(policy_window_remove,GWIPC_MESSAGE_POLICY_WINDOW_REMOVE,w::PolicyWindowRemove,{sizeof(o),v.window_id,{}})
SIMPLE_ACCESS(policy_commit,GWIPC_MESSAGE_POLICY_COMMIT,w::PolicyCommit,{sizeof(o),v.commit_id,v.producer_generation,v.flags,{}})
SIMPLE_ACCESS(policy_acknowledged,GWIPC_MESSAGE_POLICY_ACKNOWLEDGED,w::PolicyAcknowledged,{sizeof(o),v.commit_id,v.producer_generation,v.applied_generation,v.policy_hash,v.window_count,static_cast<gwipc_policy_result>(v.result),{}})
SIMPLE_ACCESS(synthetic_motion,GWIPC_MESSAGE_SYNTHETIC_MOTION,w::SyntheticMotion,{sizeof(o),v.input_id,v.time_ms,v.root_x,v.root_y,v.flags,{}})
SIMPLE_ACCESS(synthetic_button,GWIPC_MESSAGE_SYNTHETIC_BUTTON,w::SyntheticButton,{sizeof(o),v.input_id,v.time_ms,v.button,v.pressed,v.reserved16,v.flags,{}})
SIMPLE_ACCESS(synthetic_key,GWIPC_MESSAGE_SYNTHETIC_KEY,w::SyntheticKey,{sizeof(o),v.input_id,v.time_ms,v.keycode,v.pressed,v.reserved16,v.flags,{}})
SIMPLE_ACCESS(synthetic_barrier,GWIPC_MESSAGE_SYNTHETIC_BARRIER,w::SyntheticBarrier,{sizeof(o),v.input_id,v.flags,{}})
SIMPLE_ACCESS(synthetic_input_acknowledged,GWIPC_MESSAGE_SYNTHETIC_INPUT_ACKNOWLEDGED,w::SyntheticInputAcknowledged,{sizeof(o),v.input_id,v.time_ms,static_cast<gwipc_synthetic_input_result>(v.result),v.root_x,v.root_y,v.pointer_window,v.focus_window,v.state,v.reserved16,v.delivered_event_count,v.flags,{}})
SIMPLE_ACCESS(policy_bindings_upsert,GWIPC_MESSAGE_POLICY_BINDINGS_UPSERT,w::PolicyBindingsUpsert,{sizeof(o),v.move_modifiers,v.resize_modifiers,v.close_modifiers,0,v.move_button,v.resize_button,0,v.close_keysym,v.minimum_width,v.minimum_height,(uint8_t)v.raise_on_focus,(uint8_t)v.consume_wm_bindings,0,{}})
SIMPLE_ACCESS(session_state_change,GWIPC_MESSAGE_SESSION_STATE_CHANGE,w::SessionStateChange,{sizeof(o),v.generation,(gwipc_session_state)v.state,v.flags,{}})
SIMPLE_ACCESS(session_state_acknowledged,GWIPC_MESSAGE_SESSION_STATE_ACKNOWLEDGED,w::SessionStateAcknowledged,{sizeof(o),v.generation,(gwipc_session_state)v.state,(gwipc_session_state_result)v.result,v.flags,{}})
#undef SIMPLE_ACCESS

const gwipc_output_upsert* gwipc_decoded_output_upsert(const gwipc_decoded_contract* d){if(!d||d->type!=GWIPC_MESSAGE_OUTPUT_UPSERT)return nullptr;const auto&v=std::get<w::OutputUpsert>(d->value);auto&o=const_cast<gwipc_decoded_contract*>(d)->output_upsert;o={sizeof(o),v.output_id,(uint8_t)v.enabled,v.logical_x,v.logical_y,v.logical_width,v.logical_height,v.physical_pixel_width,v.physical_pixel_height,v.refresh_millihertz,v.scale_numerator,v.scale_denominator,(gwipc_transform)v.transform,color(v.color),{}};return&o;}
const gwipc_surface_upsert* gwipc_decoded_surface_upsert(const gwipc_decoded_contract* d){if(!d||d->type!=GWIPC_MESSAGE_SURFACE_UPSERT)return nullptr;const auto&v=std::get<w::SurfaceUpsert>(d->value);auto&o=const_cast<gwipc_decoded_contract*>(d)->surface_upsert;o={sizeof(o),v.surface_id,v.x11_window_id,v.parent_surface_id,v.output_id,v.logical_x,v.logical_y,v.logical_width,v.logical_height,v.stacking,(uint8_t)v.visible,(uint8_t)v.clipping,v.clip_x,v.clip_y,v.clip_width,v.clip_height,(gwipc_transform)v.transform,v.opacity,v.scale_numerator,v.scale_denominator,color(v.color),v.presentation_flags,(gwipc_tri_state)v.fullscreen_eligible,(gwipc_tri_state)v.direct_scanout_eligible,{}};return&o;}
const gwipc_buffer_attach* gwipc_decoded_buffer_attach(const gwipc_decoded_contract* d){if(!d||d->type!=GWIPC_MESSAGE_BUFFER_ATTACH)return nullptr;const auto&v=std::get<w::BufferAttach>(d->value);auto&o=const_cast<gwipc_decoded_contract*>(d)->buffer_attach;o={sizeof(o),v.buffer_id,v.surface_id,v.width,v.height,v.stride,v.byte_offset,v.storage_size,(gwipc_pixel_format)v.pixel_format,v.modifier,(gwipc_alpha_semantics)v.alpha_semantics,color(v.color),(gwipc_synchronization_mode)v.synchronization,v.flags,{}};return&o;}
const gwipc_surface_damage* gwipc_decoded_surface_damage(const gwipc_decoded_contract* d){if(!d||d->type!=GWIPC_MESSAGE_SURFACE_DAMAGE)return nullptr;const auto&v=std::get<w::SurfaceDamage>(d->value);auto*x=const_cast<gwipc_decoded_contract*>(d);x->surface_damage={sizeof(x->surface_damage),v.surface_id,x->rectangles.data(),x->rectangles.size(),{}};return&x->surface_damage;}
const gwipc_policy_window_upsert* gwipc_decoded_policy_window_upsert(const gwipc_decoded_contract*d){if(!d||d->type!=GWIPC_MESSAGE_POLICY_WINDOW_UPSERT)return nullptr;const auto&v=std::get<w::PolicyWindowUpsert>(d->value);auto&o=const_cast<gwipc_decoded_contract*>(d)->policy_window_upsert;o={sizeof(o),v.window_id,v.parent_window_id,v.transient_for,v.workspace_id,v.requested_x,v.requested_y,v.requested_width,v.requested_height,v.border_width,(gwipc_policy_window_type)v.window_type,(gwipc_policy_map_intent)v.map_intent,(uint8_t)v.override_redirect,(gwipc_tri_state)v.decoration_preference,(uint8_t)v.fullscreen_requested,(uint8_t)v.maximized_requested,(uint8_t)v.minimized_requested,(uint8_t)v.attention_requested,v.creation_serial,v.map_serial,v.focus_serial,v.flags,{}};return&o;}
const gwipc_policy_window_state* gwipc_decoded_policy_window_state(const gwipc_decoded_contract*d){if(!d||d->type!=GWIPC_MESSAGE_POLICY_WINDOW_STATE)return nullptr;const auto&v=std::get<w::PolicyWindowState>(d->value);auto&o=const_cast<gwipc_decoded_contract*>(d)->policy_window_state;o={sizeof(o),v.window_id,v.transient_for,v.workspace_id,v.output_id,v.final_x,v.final_y,v.final_width,v.final_height,v.stacking,(gwipc_policy_window_type)v.window_type,(gwipc_policy_applied_state)v.applied_state,(uint8_t)v.visible,(uint8_t)v.focused,(uint8_t)v.managed,(uint8_t)v.decoration_eligible,(uint8_t)v.override_redirect,(uint8_t)v.attention_requested,(gwipc_tri_state)v.fullscreen_eligible,(gwipc_tri_state)v.direct_scanout_eligible,v.flags,{}};return&o;}
const gwipc_policy_lifecycle_window_upsert* gwipc_decoded_policy_lifecycle_window_upsert(const gwipc_decoded_contract*d){if(!d||d->type!=GWIPC_MESSAGE_POLICY_LIFECYCLE_WINDOW_UPSERT)return nullptr;const auto&v=std::get<w::PolicyLifecycleWindowUpsert>(d->value);auto&o=const_cast<gwipc_decoded_contract*>(d)->policy_lifecycle_window_upsert;const auto&x=v.window;o={sizeof(o),{sizeof(o.window),x.window_id,x.parent_window_id,x.transient_for,x.workspace_id,x.requested_x,x.requested_y,x.requested_width,x.requested_height,x.border_width,(gwipc_policy_window_type)x.window_type,(gwipc_policy_map_intent)x.map_intent,(uint8_t)x.override_redirect,(gwipc_tri_state)x.decoration_preference,(uint8_t)x.fullscreen_requested,(uint8_t)x.maximized_requested,(uint8_t)x.minimized_requested,(uint8_t)x.attention_requested,x.creation_serial,x.map_serial,x.focus_serial,x.flags,{}},v.geometry_serial,v.stack_serial,v.stack_sibling,(gwipc_policy_stack_mode)v.stack_mode,v.flags,{}};return&o;}
const gwipc_surface_policy_upsert* gwipc_decoded_surface_policy_upsert(const gwipc_decoded_contract*d){if(!d||d->type!=GWIPC_MESSAGE_SURFACE_POLICY_UPSERT)return nullptr;const auto&v=std::get<w::SurfacePolicyUpsert>(d->value);auto&o=const_cast<gwipc_decoded_contract*>(d)->surface_policy_upsert;o={sizeof(o),v.surface_id,v.x11_window_id,v.workspace_id,(gwipc_policy_window_type)v.window_type,(gwipc_policy_applied_state)v.applied_state,(uint8_t)v.focused,(uint8_t)v.managed,(uint8_t)v.decoration_eligible,(uint8_t)v.override_redirect,(uint8_t)v.attention_requested,(gwipc_tri_state)v.fullscreen_eligible,(gwipc_tri_state)v.direct_scanout_eligible,v.flags,{}};return&o;}
}
