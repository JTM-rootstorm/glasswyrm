#include "ipc/wire/policy_contract.hpp"
#include "ipc/wire/byte_reader.hpp"
#include "ipc/wire/byte_writer.hpp"
#include <limits>
namespace gw::ipc::wire {
namespace {
bool extent(std::int32_t p, std::uint32_t n) {
  return n && static_cast<std::int64_t>(p) + n - 1 <=
                  std::numeric_limits<std::int32_t>::max();
}
bool tri(std::uint16_t v) { return v <= 2; }
} // namespace
std::vector<std::uint8_t> encode(const PolicyContextUpsert &v) {
  ByteWriter w;
  w.u32(v.root_window_id);
  w.u32(v.workspace_id);
  w.u64(v.output_id);
  w.i32(v.work_x);
  w.i32(v.work_y);
  w.u32(v.work_width);
  w.u32(v.work_height);
  w.u32(v.flags);
  w.u32(0);
  return std::move(w).take();
}
CodecStatus decode(std::span<const std::uint8_t> b, PolicyContextUpsert &v) {
  ByteReader r(b);
  PolicyContextUpsert x;
  std::uint32_t z;
  if (!r.u32(x.root_window_id) || !r.u32(x.workspace_id) ||
      !r.u64(x.output_id) || !r.i32(x.work_x) || !r.i32(x.work_y) ||
      !r.u32(x.work_width) || !r.u32(x.work_height) || !r.u32(x.flags) ||
      !r.u32(z))
    return CodecStatus::Truncated;
  if (!r.done())
    return CodecStatus::TrailingData;
  if (!x.root_window_id || !x.workspace_id || !x.output_id ||
      !extent(x.work_x, x.work_width) || !extent(x.work_y, x.work_height) ||
      x.flags || z)
    return CodecStatus::InvalidValue;
  v = x;
  return CodecStatus::Ok;
}
std::vector<std::uint8_t> encode(const PolicyWindowUpsert &v) {
  ByteWriter w;
  w.u32(v.window_id);
  w.u32(v.parent_window_id);
  w.u32(v.transient_for);
  w.u32(v.workspace_id);
  w.i32(v.requested_x);
  w.i32(v.requested_y);
  w.u32(v.requested_width);
  w.u32(v.requested_height);
  w.u32(v.border_width);
  w.u16((std::uint16_t)v.window_type);
  w.u16((std::uint16_t)v.map_intent);
  w.u8(v.override_redirect);
  w.u8((std::uint8_t)v.decoration_preference);
  w.u8(v.fullscreen_requested);
  w.u8(v.maximized_requested);
  w.u8(v.minimized_requested);
  w.u8(v.attention_requested);
  w.u16(0);
  w.u64(v.creation_serial);
  w.u64(v.map_serial);
  w.u64(v.focus_serial);
  w.u32(v.flags);
  w.u32(0);
  return std::move(w).take();
}
CodecStatus decode(std::span<const std::uint8_t> b, PolicyWindowUpsert &v) {
  ByteReader r(b);
  PolicyWindowUpsert x;
  std::uint16_t wt, mi, z16;
  std::uint8_t o, d, f, m, n, a;
  std::uint32_t z;
  if (!r.u32(x.window_id) || !r.u32(x.parent_window_id) ||
      !r.u32(x.transient_for) || !r.u32(x.workspace_id) ||
      !r.i32(x.requested_x) || !r.i32(x.requested_y) ||
      !r.u32(x.requested_width) || !r.u32(x.requested_height) ||
      !r.u32(x.border_width) || !r.u16(wt) || !r.u16(mi) || !r.u8(o) ||
      !r.u8(d) || !r.u8(f) || !r.u8(m) || !r.u8(n) || !r.u8(a) || !r.u16(z16) ||
      !r.u64(x.creation_serial) || !r.u64(x.map_serial) ||
      !r.u64(x.focus_serial) || !r.u32(x.flags) || !r.u32(z))
    return CodecStatus::Truncated;
  if (!r.done())
    return CodecStatus::TrailingData;
  x.window_type = (PolicyWindowType)wt;
  x.map_intent = (PolicyMapIntent)mi;
  x.override_redirect = o;
  x.decoration_preference = d;
  x.fullscreen_requested = f;
  x.maximized_requested = m;
  x.minimized_requested = n;
  x.attention_requested = a;
  if (!x.window_id || !x.requested_width || !x.requested_height || wt > 3 ||
      mi > 1 || o > 1 || !tri(d) || f > 1 || m > 1 || n > 1 || a > 1 ||
      !x.creation_serial || (mi != 0 && x.map_serial == 0) ||
      x.flags || z16 || z)
    return CodecStatus::InvalidValue;
  v = x;
  return CodecStatus::Ok;
}
std::vector<std::uint8_t> encode(const PolicyWindowRemove &v) {
  ByteWriter w;
  w.u32(v.window_id);
  w.u32(0);
  return std::move(w).take();
}
CodecStatus decode(std::span<const std::uint8_t> b, PolicyWindowRemove &v) {
  ByteReader r(b);
  PolicyWindowRemove x;
  std::uint32_t z;
  if (!r.u32(x.window_id) || !r.u32(z))
    return CodecStatus::Truncated;
  if (!r.done())
    return CodecStatus::TrailingData;
  if (!x.window_id || z)
    return CodecStatus::InvalidValue;
  v = x;
  return CodecStatus::Ok;
}
std::vector<std::uint8_t> encode(const PolicyCommit &v) {
  ByteWriter w;
  w.u64(v.commit_id);
  w.u64(v.producer_generation);
  w.u32(v.flags);
  w.u32(0);
  return std::move(w).take();
}
CodecStatus decode(std::span<const std::uint8_t> b, PolicyCommit &v) {
  ByteReader r(b);
  PolicyCommit x;
  std::uint32_t z;
  if (!r.u64(x.commit_id) || !r.u64(x.producer_generation) || !r.u32(x.flags) ||
      !r.u32(z))
    return CodecStatus::Truncated;
  if (!r.done())
    return CodecStatus::TrailingData;
  if (!x.commit_id || !x.producer_generation || x.flags || z)
    return CodecStatus::InvalidValue;
  v = x;
  return CodecStatus::Ok;
}
std::vector<std::uint8_t> encode(const PolicyWindowState &v) {
  ByteWriter w;
  w.u32(v.window_id);
  w.u32(v.transient_for);
  w.u32(v.workspace_id);
  w.u32(0);
  w.u64(v.output_id);
  w.i32(v.final_x);
  w.i32(v.final_y);
  w.u32(v.final_width);
  w.u32(v.final_height);
  w.i32(v.stacking);
  w.u16((std::uint16_t)v.window_type);
  w.u16((std::uint16_t)v.applied_state);
  w.u8(v.visible);
  w.u8(v.focused);
  w.u8(v.managed);
  w.u8(v.decoration_eligible);
  w.u8(v.override_redirect);
  w.u8(v.attention_requested);
  w.u8((std::uint8_t)v.fullscreen_eligible);
  w.u8((std::uint8_t)v.direct_scanout_eligible);
  w.u32(v.flags);
  w.u32(0);
  return std::move(w).take();
}
CodecStatus decode(std::span<const std::uint8_t> b, PolicyWindowState &v) {
  ByteReader r(b);
  PolicyWindowState x;
  std::uint32_t z1, z2;
  std::uint16_t wt, as;
  std::uint8_t q[8];
  if (!r.u32(x.window_id) || !r.u32(x.transient_for) ||
      !r.u32(x.workspace_id) || !r.u32(z1) || !r.u64(x.output_id) ||
      !r.i32(x.final_x) || !r.i32(x.final_y) || !r.u32(x.final_width) ||
      !r.u32(x.final_height) || !r.i32(x.stacking) || !r.u16(wt) || !r.u16(as))
    return CodecStatus::Truncated;
  for (auto &i : q)
    if (!r.u8(i))
      return CodecStatus::Truncated;
  if (!r.u32(x.flags) || !r.u32(z2))
    return CodecStatus::Truncated;
  if (!r.done())
    return CodecStatus::TrailingData;
  x.window_type = (PolicyWindowType)wt;
  x.applied_state = (PolicyAppliedState)as;
  x.visible = q[0];
  x.focused = q[1];
  x.managed = q[2];
  x.decoration_eligible = q[3];
  x.override_redirect = q[4];
  x.attention_requested = q[5];
  x.fullscreen_eligible = q[6];
  x.direct_scanout_eligible = q[7];
  if (!x.window_id || !x.workspace_id || !x.output_id || !x.final_width ||
      !x.final_height || wt > 3 || as < 1 || as > 4 || q[0] > 1 || q[1] > 1 ||
      q[2] > 1 || q[3] > 1 || q[4] > 1 || q[5] > 1 || !tri(q[6]) ||
      !tri(q[7]) || x.flags || z1 || z2)
    return CodecStatus::InvalidValue;
  v = x;
  return CodecStatus::Ok;
}
std::vector<std::uint8_t> encode(const PolicyAcknowledged &v) {
  ByteWriter w;
  w.u64(v.commit_id);
  w.u64(v.producer_generation);
  w.u64(v.applied_generation);
  w.u64(v.policy_hash);
  w.u32(v.window_count);
  w.u16((std::uint16_t)v.result);
  w.u16(0);
  return std::move(w).take();
}
CodecStatus decode(std::span<const std::uint8_t> b, PolicyAcknowledged &v) {
  ByteReader r(b);
  PolicyAcknowledged x;
  std::uint16_t rs, z;
  if (!r.u64(x.commit_id) || !r.u64(x.producer_generation) ||
      !r.u64(x.applied_generation) || !r.u64(x.policy_hash) ||
      !r.u32(x.window_count) || !r.u16(rs) || !r.u16(z))
    return CodecStatus::Truncated;
  if (!r.done())
    return CodecStatus::TrailingData;
  x.result = (PolicyResult)rs;
  if (!x.commit_id || !x.producer_generation || rs < 1 || rs > 7 || z)
    return CodecStatus::InvalidValue;
  v = x;
  return CodecStatus::Ok;
}
} // namespace gw::ipc::wire
