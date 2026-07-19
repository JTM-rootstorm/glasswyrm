#include "gwcomp/scene_manifest.hpp"

#include <glasswyrm/ipc.h>

#include <cstdint>
#include <memory>
#include <span>
#include <sstream>
#include <string_view>

namespace gw::compositor {
namespace {

constexpr std::uint64_t kFnvOffset = UINT64_C(14695981039346656037);
constexpr std::uint64_t kFnvPrime = UINT64_C(1099511628211);

struct PayloadDeleter {
  void operator()(gwipc_contract_payload *value) const {
    gwipc_contract_payload_destroy(value);
  }
};

void hash_bytes(std::uint64_t &hash, std::span<const std::uint8_t> bytes) {
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= kFnvPrime;
  }
}

void hash_u64(std::uint64_t &hash, const std::uint64_t value) {
  for (unsigned shift = 0; shift != 64; shift += 8) {
    const auto byte = static_cast<std::uint8_t>(value >> shift);
    hash_bytes(hash, {&byte, 1});
  }
}

template <class Value, class Encoder>
bool hash_contract(std::uint64_t &hash, const Value &value, Encoder encoder,
                   std::string &error) {
  gwipc_contract_payload *raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK) {
    error = "M13 scene contract encoding failed";
    return false;
  }
  const std::unique_ptr<gwipc_contract_payload, PayloadDeleter> payload(raw);
  std::size_t size = 0;
  const auto *bytes = gwipc_contract_payload_data(payload.get(), &size);
  hash_bytes(hash, {bytes, size});
  return true;
}

bool cursor_surface(const gwipc_surface_upsert &surface) {
  return surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_CURSOR;
}

bool metadata_surface(const gwipc_surface_upsert &surface) {
  return surface.presentation_flags ==
         GWIPC_SURFACE_PRESENTATION_METADATA_ONLY;
}

const char *boolean(const bool value) { return value ? "true" : "false"; }

bool hash_membership(std::uint64_t &hash, const std::uint64_t surface_id,
                     const SurfaceOutputMembership &membership,
                     std::string &error) {
  const gwipc_surface_output_state wire{
      sizeof(gwipc_surface_output_state),
      surface_id,
      membership.primary_output_id,
      membership.output_ids.data(),
      membership.output_ids.size(),
      membership.preferred_scale_numerator,
      membership.preferred_scale_denominator,
      membership.client_buffer_scale,
      membership.scale_mode,
      membership.layout_generation,
      membership.flags,
      {}};
  return hash_contract(hash, wire, gwipc_contract_encode_surface_output_state,
                       error);
}

void write_membership(std::ostringstream &json,
                      const SurfaceOutputMembership &membership) {
  json << "\"primary_output_id\":" << membership.primary_output_id
       << ",\"memberships\":[";
  for (std::size_t index = 0; index < membership.output_ids.size(); ++index) {
    if (index != 0)
      json << ',';
    json << membership.output_ids[index];
  }
  json << "],\"preferred_scale\":{\"numerator\":"
       << membership.preferred_scale_numerator << ",\"denominator\":"
       << membership.preferred_scale_denominator
       << "},\"client_buffer_scale\":" << membership.client_buffer_scale
       << ",\"scale_mode\":" << membership.scale_mode
       << ",\"layout_generation\":" << membership.layout_generation;
}

} // namespace

bool SceneManifest::describe_output_model(
    const std::uint64_t commit_id, const std::uint64_t generation,
    const Scene &scene, SceneManifestResult &result, std::string &json,
    std::string &error) {
  error.clear();
  if (scene.configuration_generation == 0 || scene.primary_output_id == 0 ||
      scene.outputs.empty() || !scene.outputs.contains(scene.primary_output_id)) {
    error = "M13 scene manifest requires a complete output layout";
    return false;
  }

  std::uint64_t hash = kFnvOffset;
  constexpr std::string_view tag = "glasswyrm-scene-v2";
  hash_bytes(hash,
             {reinterpret_cast<const std::uint8_t *>(tag.data()), tag.size()});
  hash_u64(hash, scene.configuration_generation);
  hash_u64(hash, scene.primary_output_id);
  for (const auto &[unused, output] : scene.outputs) {
    (void)unused;
    if (!hash_contract(hash, output, gwipc_contract_encode_output_upsert,
                       error))
      return false;
  }
  for (const auto &[surface_id, surface] : scene.surfaces) {
    const auto membership = scene.surface_outputs.find(surface_id);
    const bool metadata_only = metadata_surface(surface);
    if (metadata_only && membership != scene.surface_outputs.end()) {
      error = "M13 metadata-only scene manifest surface has output membership";
      return false;
    }
    if (!metadata_only && membership == scene.surface_outputs.end()) {
      error = "M13 scene manifest surface lacks output membership";
      return false;
    }
    if (!hash_contract(hash, surface, gwipc_contract_encode_surface_upsert,
                       error))
      return false;
    if (membership != scene.surface_outputs.end() &&
        !hash_membership(hash, surface_id, membership->second, error))
      return false;
    if (const auto policy = scene.surface_policies.find(surface_id);
        policy != scene.surface_policies.end() &&
        !hash_contract(hash, policy->second,
                       gwipc_contract_encode_surface_policy_upsert, error))
      return false;
  }

  std::uint32_t surface_count = 0;
  std::uint32_t cursor_count = 0;
  std::ostringstream output;
  output << "{\"schema\":\"glasswyrm-scene-v2\",\"commit_id\":"
         << commit_id << ",\"generation\":" << generation
         << ",\"layout_generation\":" << scene.configuration_generation
         << ",\"primary_output_id\":" << scene.primary_output_id
         << ",\"outputs\":[";
  std::size_t output_index = 0;
  for (const auto &[output_id, state] : scene.outputs) {
    if (output_index++ != 0)
      output << ',';
    output << "{\"output_id\":" << output_id
           << ",\"enabled\":" << boolean(state.enabled != 0)
           << ",\"primary\":"
           << boolean(output_id == scene.primary_output_id)
           << ",\"logical\":{\"x\":" << state.logical_x
           << ",\"y\":" << state.logical_y << ",\"width\":"
           << state.logical_width << ",\"height\":" << state.logical_height
           << "},\"physical\":{\"width\":"
           << state.physical_pixel_width << ",\"height\":"
           << state.physical_pixel_height << "},\"scale\":{\"numerator\":"
           << state.scale_numerator << ",\"denominator\":"
           << state.scale_denominator << "},\"transform\":"
           << state.transform << '}';
  }
  output << "],\"surfaces\":[";
  bool first_surface = true;
  for (const auto &[surface_id, surface] : scene.surfaces) {
    if (cursor_surface(surface)) {
      ++cursor_count;
      continue;
    }
    ++surface_count;
    if (!first_surface)
      output << ',';
    first_surface = false;
    output << "{\"surface_id\":" << surface_id
           << ",\"x11_window_id\":" << surface.x11_window_id
           << ",\"x\":" << surface.logical_x << ",\"y\":"
           << surface.logical_y << ",\"width\":" << surface.logical_width
           << ",\"height\":" << surface.logical_height
           << ",\"metadata_only\":" << boolean(metadata_surface(surface));
    if (const auto membership = scene.surface_outputs.find(surface_id);
        membership != scene.surface_outputs.end()) {
      output << ',';
      write_membership(output, membership->second);
    }
    output << '}';
  }
  output << "],\"cursors\":[";
  bool first_cursor = true;
  for (const auto &[surface_id, surface] : scene.surfaces) {
    if (!cursor_surface(surface))
      continue;
    if (!first_cursor)
      output << ',';
    first_cursor = false;
    output << "{\"surface_id\":" << surface_id << ",\"x\":"
           << surface.logical_x << ",\"y\":" << surface.logical_y << ',';
    write_membership(output, scene.surface_outputs.at(surface_id));
    output << '}';
  }
  output << "],\"scene_hash\":\"";
  output.setf(std::ios::hex, std::ios::basefield);
  output.width(16);
  output.fill('0');
  output << hash;
  output.setf(std::ios::dec, std::ios::basefield);
  output << "\",\"surface_count\":" << surface_count
         << ",\"cursor_count\":" << cursor_count << "}\n";
  if (!output.good()) {
    error = "M13 scene manifest serialization failed";
    return false;
  }
  result = {hash, surface_count, cursor_count};
  json = output.str();
  return true;
}

bool SceneManifest::prepare_output_model(
    const std::uint64_t commit_id, const std::uint64_t generation,
    const Scene &scene, PreparedSceneManifest &prepared, std::string &error) {
  PreparedSceneManifest replacement;
  if (!describe_output_model(commit_id, generation, scene, replacement.result,
                             replacement.json, error))
    return false;
  replacement.active = true;
  prepared = std::move(replacement);
  return true;
}

} // namespace gw::compositor
