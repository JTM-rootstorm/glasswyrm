#include "backends/output/software_frame_set.hpp"

#include "output/model/scale.hpp"
#include "output/model/transform.hpp"

#include <glasswyrm/ipc/contracts.h>

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

std::uint64_t compute_aggregate_hash(
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

} // namespace

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
  output.visible_hash = output.frame.visible_hash();
  const auto id = output.output.output_id;
  if (!outputs_.emplace(id, std::move(output)).second) {
    error = "software frame set contains a duplicate output ID";
    return false;
  }
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
  aggregate_hash_ =
      compute_aggregate_hash(outputs_, layout_generation_, primary_output_id_);
  finalized_ = true;
  error.clear();
  return true;
}

SoftwareFrameSetView SoftwareFrameSet::view() const noexcept {
  return {&outputs_,         aggregate_hash_, layout_generation_,
          primary_output_id_, commit_id_,      generation_,
          ordinal_};
}

} // namespace glasswyrm::output
