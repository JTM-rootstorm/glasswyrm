#include "backends/output/software_frame_set.hpp"

#include "output/model/scale.hpp"
#include "output/model/transform.hpp"

#include <glasswyrm/ipc/contracts.h>

#include <chrono>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>

namespace glasswyrm::output {
namespace {

constexpr std::uint64_t kFnvOffset = UINT64_C(14695981039346656037);
constexpr std::uint64_t kFnvPrime = UINT64_C(1099511628211);

void append_byte(std::uint64_t &hash, const std::uint8_t value) noexcept {
  hash ^= value;
  hash *= kFnvPrime;
}

void append_u32(std::uint64_t &hash, const std::uint32_t value) noexcept {
  for (unsigned shift = 0; shift < 32; shift += 8)
    append_byte(hash, static_cast<std::uint8_t>(value >> shift));
}

void append_u64(std::uint64_t &hash, const std::uint64_t value) noexcept {
  for (unsigned shift = 0; shift < 64; shift += 8)
    append_byte(hash, static_cast<std::uint8_t>(value >> shift));
}

bool valid_damage(const gw::compositor::Rectangle rectangle,
                  const OutputSpec output) noexcept {
  if (rectangle.x < 0 || rectangle.y < 0 || rectangle.width == 0 ||
      rectangle.height == 0)
    return false;
  const auto right = static_cast<std::uint64_t>(rectangle.x) + rectangle.width;
  const auto bottom =
      static_cast<std::uint64_t>(rectangle.y) + rectangle.height;
  return right <= output.width && bottom <= output.height;
}

} // namespace

SoftwareFrameSet::SoftwareFrameSet(const SoftwareFrameSet* const previous) {
  if (previous != nullptr && previous->finalized()) {
    try {
      hash_history_ = previous->hash_history_;
    } catch (const std::bad_alloc&) {
      // A cache miss remains correct and lets rendering proceed normally.
    }
  }
}

std::uint64_t calculate_frame_set_aggregate_hash(
    const std::map<std::uint64_t, OutputFrameResult> &outputs,
    const std::uint64_t layout_generation,
    const std::uint64_t primary_output_id) noexcept {
  std::uint64_t hash = kFnvOffset;
  constexpr std::string_view tag = "glasswyrm-output-frame-set-v1";
  for (const auto character : tag)
    append_byte(hash, static_cast<std::uint8_t>(character));
  append_u64(hash, layout_generation);
  append_u64(hash, primary_output_id);
  for (const auto &[output_id, output] : outputs) {
    append_u64(hash, output_id);
    append_u32(hash, output.output.width);
    append_u32(hash, output.output.height);
    append_u32(hash, output.scale.numerator);
    append_u32(hash, output.scale.denominator);
    append_u32(hash, static_cast<std::uint32_t>(output.transform));
    append_u64(hash, output.visible_hash);
  }
  return hash;
}

bool SoftwareFrameSet::append(OutputFrameResult output, std::string &error) {
  if (finalized()) {
    error = "software frame set is already finalized";
    return false;
  }
  if (outputs_.size() == kMaximumOutputs) {
    error = "software frame set exceeds the output limit";
    return false;
  }
  if (output.output.output_id == 0 || !output.frame.enabled() ||
      output.frame.id() != output.output.output_id ||
      output.frame.width() != output.output.width ||
      output.frame.height() != output.output.height ||
      output.output.width == 0 || output.output.height == 0 ||
      output.logical.x < 0 || output.logical.y < 0 ||
      output.logical.width == 0 || output.logical.height == 0 ||
      !valid_output_scale(output.scale) ||
      !valid_output_transform(output.transform)) {
    error = "software output frame metadata is inconsistent";
    return false;
  }
  if (output.damage.size() > GWIPC_MAXIMUM_DAMAGE_RECTANGLES) {
    error = "software output frame exceeds the damage limit";
    return false;
  }
  for (const auto rectangle : output.damage) {
    if (!valid_damage(rectangle, output.output)) {
      error = "software output frame damage is outside its physical extent";
      return false;
    }
  }
  const auto pixels = static_cast<std::uint64_t>(output.output.width) *
                      output.output.height;
  if (pixels > kMaximumTotalPixels - total_pixels_) {
    error = "software frame set exceeds the total pixel limit";
    return false;
  }
  const auto id = output.output.output_id;
  if (outputs_.contains(id)) {
    error = "software frame set contains a duplicate output ID";
    return false;
  }

  FrameHashMeasurement hash;
  const auto started = std::chrono::steady_clock::now();
  auto history = hash_history_.find(id);
  if (history != hash_history_.end()) {
    for (std::size_t index = 0; index < history->second.size(); ++index) {
      const auto& candidate = history->second[index];
      if (!candidate || !candidate->pixels ||
          candidate->pixels->size() != output.frame.pixels().size() ||
          std::memcmp(candidate->pixels->data(), output.frame.pixels().data(),
                      output.frame.pixels().size_bytes()) != 0)
        continue;
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now() - started);
      hash = {candidate->hash, pixels * 3U,
              elapsed.count() < 0
                  ? 0U
                  : static_cast<std::uint64_t>(elapsed.count())};
      output.frame_hash_reused = true;
      if (index != 0)
        std::swap(history->second[0], history->second[index]);
      break;
    }
  }
  if (!output.frame_hash_reused) {
    hash = hash_visible_xrgb8888_measured(output.frame.pixels());
    try {
      auto snapshot = std::make_shared<const std::vector<std::uint32_t>>(
          output.frame.pixels().begin(), output.frame.pixels().end());
      auto& entries = hash_history_[id];
      entries[1] = std::move(entries[0]);
      entries[0] = CanonicalHashEntry{std::move(snapshot), hash.hash};
    } catch (const std::bad_alloc&) {
      // Hash caching is best-effort. Preserve inherited entries so allocation
      // pressure cannot turn a correct canonical frame into a render failure.
    }
  }
  output.visible_hash = hash.hash;
  output.frame_hash_bytes = hash.bytes;
  output.frame_hash_nanoseconds = hash.nanoseconds;
  outputs_.emplace(id, std::move(output));
  total_pixels_ += pixels;
  error.clear();
  return true;
}

bool SoftwareFrameSet::finalize(const std::uint64_t layout_generation,
                                const std::uint64_t primary_output_id,
                                const std::uint64_t commit_id,
                                const std::uint64_t generation,
                                const std::uint64_t ordinal,
                                std::string &error) {
  if (finalized()) {
    error = "software frame set is already finalized";
    return false;
  }
  if (outputs_.empty() || layout_generation == 0 || primary_output_id == 0 ||
      commit_id == 0 || generation == 0 || ordinal == 0 ||
      !outputs_.contains(primary_output_id)) {
    error = "software frame set commit metadata is incomplete";
    return false;
  }
  layout_generation_ = layout_generation;
  primary_output_id_ = primary_output_id;
  commit_id_ = commit_id;
  generation_ = generation;
  ordinal_ = ordinal;
  aggregate_hash_ = calculate_frame_set_aggregate_hash(
      outputs_, layout_generation_, primary_output_id_);
  for (auto iterator = hash_history_.begin();
       iterator != hash_history_.end();) {
    if (outputs_.contains(iterator->first)) {
      ++iterator;
    } else {
      iterator = hash_history_.erase(iterator);
    }
  }
  finalized_ = true;
  error.clear();
  return true;
}

bool SoftwareFrameSet::set_vrr_requests(
    const std::map<std::uint64_t, VrrPresentationRequest>& requests,
    std::string& error) {
  if (!finalized() || requests.size() != outputs_.size()) {
    error = "VRR presentation metadata does not cover the finalized frame set";
    return false;
  }
  for (const auto& [output_id, request] : requests) {
    const auto output = outputs_.find(output_id);
    if (output == outputs_.end() || !request.valid ||
        !vrr::valid_policy_mode(request.requested_mode) ||
        (request.reason_flags & ~vrr::kKnownReasonMask) != 0 ||
        request.state_generation == 0 || request.transition_serial == 0 ||
        (request.desired_enabled !=
         (request.decision == vrr::Decision::Enabled))) {
      error = "VRR presentation metadata is invalid";
      return false;
    }
  }
  for (const auto& [output_id, request] : requests)
    outputs_.at(output_id).vrr = request;
  error.clear();
  return true;
}

SoftwareFrameSetView SoftwareFrameSet::view() const noexcept {
  return {&outputs_,         aggregate_hash_, layout_generation_,
          primary_output_id_, commit_id_,      generation_,
          ordinal_};
}

} // namespace glasswyrm::output
