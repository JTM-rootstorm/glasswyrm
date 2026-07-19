#include "output_client/output_client.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <numeric>

namespace glasswyrm::tools::output_client {
namespace {

template <typename Integer>
bool parse_integer(const std::string_view text, Integer &value,
                   const int base = 10) {
  if (text.empty())
    return false;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value, base);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool scale_at_least(const std::pair<std::uint32_t, std::uint32_t> value,
                    const std::uint32_t numerator,
                    const std::uint32_t denominator) {
  return static_cast<std::uint64_t>(value.first) * denominator >=
         static_cast<std::uint64_t>(numerator) * value.second;
}

bool scale_at_most(const std::pair<std::uint32_t, std::uint32_t> value,
                   const std::uint32_t numerator,
                   const std::uint32_t denominator) {
  return static_cast<std::uint64_t>(value.first) * denominator <=
         static_cast<std::uint64_t>(numerator) * value.second;
}

bool swaps_axes(const gwipc_transform transform) {
  return transform == GWIPC_TRANSFORM_ROTATE_90 ||
         transform == GWIPC_TRANSFORM_ROTATE_270 ||
         transform == GWIPC_TRANSFORM_FLIPPED_90 ||
         transform == GWIPC_TRANSFORM_FLIPPED_270;
}

std::uint32_t logical_dimension(const std::uint32_t physical,
                                const std::uint32_t numerator,
                                const std::uint32_t denominator) {
  const auto product = static_cast<std::uint64_t>(physical) * denominator;
  return static_cast<std::uint32_t>(product / numerator +
                                    (product % numerator != 0));
}

bool overlaps(const OutputState &left, const OutputState &right) {
  return static_cast<std::uint64_t>(left.logical_x) + left.logical_width >
             static_cast<std::uint64_t>(right.logical_x) &&
         static_cast<std::uint64_t>(right.logical_x) + right.logical_width >
             static_cast<std::uint64_t>(left.logical_x) &&
         static_cast<std::uint64_t>(left.logical_y) + left.logical_height >
             static_cast<std::uint64_t>(right.logical_y) &&
         static_cast<std::uint64_t>(right.logical_y) + right.logical_height >
             static_cast<std::uint64_t>(left.logical_y);
}

std::optional<std::uint64_t> parse_output_id(const std::string_view value) {
  std::uint64_t id = 0;
  if (value.starts_with("0x")) {
    if (parse_integer(value.substr(2), id, 16))
      return id;
  } else if (value.size() == 16) {
    if (parse_integer(value, id, 16))
      return id;
  } else if (parse_integer(value, id)) {
    return id;
  }
  return std::nullopt;
}

bool validate_layout(Snapshot &snapshot, std::string &error) {
  std::vector<const OutputState *> enabled;
  for (const auto &[id, output] : snapshot.outputs) {
    if (!snapshot.descriptors.contains(id)) {
      error = "output inventory is incomplete";
      return false;
    }
    if (output.enabled)
      enabled.push_back(&output);
  }
  if (enabled.empty()) {
    error = "refusing to disable the last enabled output";
    return false;
  }
  const auto primary = snapshot.outputs.find(snapshot.primary_output_id);
  if (primary == snapshot.outputs.end() || !primary->second.enabled) {
    error = "the primary output must remain enabled";
    return false;
  }
  for (std::size_t left = 0; left < enabled.size(); ++left)
    for (std::size_t right = left + 1; right < enabled.size(); ++right)
      if (overlaps(*enabled[left], *enabled[right])) {
        error = "enabled output rectangles overlap";
        return false;
      }
  std::uint64_t root_width = 0;
  std::uint64_t root_height = 0;
  for (const auto *output : enabled) {
    if (output->logical_x < 0 || output->logical_y < 0) {
      error = "enabled output positions must be nonnegative";
      return false;
    }
    root_width = std::max(root_width,
                          static_cast<std::uint64_t>(output->logical_x) +
                              output->logical_width);
    root_height = std::max(root_height,
                           static_cast<std::uint64_t>(output->logical_y) +
                               output->logical_height);
  }
  if (root_width > GWIPC_MAXIMUM_ROOT_LOGICAL_WIDTH ||
      root_height > GWIPC_MAXIMUM_ROOT_LOGICAL_HEIGHT) {
    error = "edited output layout exceeds the X11 root bounds";
    return false;
  }
  snapshot.root_width = root_width;
  snapshot.root_height = root_height;
  snapshot.enabled_output_count = enabled.size();
  return true;
}

} // namespace

std::optional<gwipc_transform>
parse_transform(const std::string_view value) noexcept {
  if (value == "normal") return GWIPC_TRANSFORM_NORMAL;
  if (value == "rotate-90") return GWIPC_TRANSFORM_ROTATE_90;
  if (value == "rotate-180") return GWIPC_TRANSFORM_ROTATE_180;
  if (value == "rotate-270") return GWIPC_TRANSFORM_ROTATE_270;
  if (value == "flipped") return GWIPC_TRANSFORM_FLIPPED;
  if (value == "flipped-90") return GWIPC_TRANSFORM_FLIPPED_90;
  if (value == "flipped-180") return GWIPC_TRANSFORM_FLIPPED_180;
  if (value == "flipped-270") return GWIPC_TRANSFORM_FLIPPED_270;
  return std::nullopt;
}

const char *transform_name(const gwipc_transform value) noexcept {
  switch (value) {
  case GWIPC_TRANSFORM_NORMAL: return "normal";
  case GWIPC_TRANSFORM_ROTATE_90: return "rotate-90";
  case GWIPC_TRANSFORM_ROTATE_180: return "rotate-180";
  case GWIPC_TRANSFORM_ROTATE_270: return "rotate-270";
  case GWIPC_TRANSFORM_FLIPPED: return "flipped";
  case GWIPC_TRANSFORM_FLIPPED_90: return "flipped-90";
  case GWIPC_TRANSFORM_FLIPPED_180: return "flipped-180";
  case GWIPC_TRANSFORM_FLIPPED_270: return "flipped-270";
  }
  return "unknown";
}

bool parse_scale(const std::string_view value,
                 std::pair<std::uint32_t, std::uint32_t> &scale) {
  const auto separator = value.find('/');
  return separator != std::string_view::npos &&
         parse_integer(value.substr(0, separator), scale.first) &&
         parse_integer(value.substr(separator + 1), scale.second) &&
         scale.first != 0 && scale.second != 0 &&
         std::gcd(scale.first, scale.second) == 1;
}

bool parse_position(
    const std::string_view value,
    std::pair<std::int32_t, std::int32_t> &position) {
  const auto separator = value.find(',');
  return separator != std::string_view::npos &&
         parse_integer(value.substr(0, separator), position.first) &&
         parse_integer(value.substr(separator + 1), position.second);
}

bool parse_mode(const std::string_view value,
                std::pair<std::uint32_t, std::uint32_t> &extent,
                std::optional<std::uint32_t> &refresh) {
  const auto at = value.find('@');
  const auto dimensions = value.substr(0, at);
  const auto separator = dimensions.find('x');
  if (separator == std::string_view::npos ||
      !parse_integer(dimensions.substr(0, separator), extent.first) ||
      !parse_integer(dimensions.substr(separator + 1), extent.second) ||
      extent.first == 0 || extent.second == 0)
    return false;
  if (at == std::string_view::npos)
    return true;
  std::uint32_t parsed = 0;
  if (!parse_integer(value.substr(at + 1), parsed) || parsed == 0)
    return false;
  refresh = parsed;
  return true;
}

bool apply_edit(Snapshot &snapshot, const std::string_view selector,
                const Edit &edit, std::string &error) {
  auto selected = snapshot.outputs.end();
  for (auto iterator = snapshot.outputs.begin();
       iterator != snapshot.outputs.end(); ++iterator) {
    const auto descriptor = snapshot.descriptors.find(iterator->first);
    if (descriptor != snapshot.descriptors.end() &&
        descriptor->second.name == selector) {
      selected = iterator;
      break;
    }
  }
  if (selected == snapshot.outputs.end()) {
    const auto id = parse_output_id(selector);
    if (id)
      selected = snapshot.outputs.find(*id);
  }
  if (selected == snapshot.outputs.end()) {
    error = "unknown output '" + std::string(selector) + "'";
    return false;
  }
  auto &output = selected->second;
  const auto &descriptor = snapshot.descriptors.at(selected->first);
  const bool was_enabled = output.enabled;
  if (edit.enabled)
    output.enabled = *edit.enabled;
  if (!output.enabled && snapshot.primary_output_id == output.id) {
    error = "refusing to disable the primary output without selecting another";
    return false;
  }
  if (edit.primary) {
    if (!output.enabled ||
        (descriptor.capabilities & GWIPC_OUTPUT_CAP_PRIMARY_ELIGIBLE) == 0) {
      error = "selected output cannot become primary";
      return false;
    }
    snapshot.primary_output_id = output.id;
  }
  if (!was_enabled && output.enabled && !edit.mode) {
    auto mode = std::find_if(snapshot.modes.begin(), snapshot.modes.end(),
                             [&](const auto &candidate) {
                               return candidate.output_id == output.id &&
                                      candidate.current;
                             });
    if (mode == snapshot.modes.end())
      mode = std::find_if(snapshot.modes.begin(), snapshot.modes.end(),
                          [&](const auto &candidate) {
                            return candidate.output_id == output.id &&
                                   candidate.preferred;
                          });
    if (mode == snapshot.modes.end()) {
      error = "re-enabled output requires an available mode";
      return false;
    }
    output.physical_width = mode->width;
    output.physical_height = mode->height;
    output.refresh_millihertz = mode->refresh_millihertz;
  }
  if (edit.mode) {
    if (descriptor.kind == GWIPC_OUTPUT_DRM) {
      error = "DRM mode changes are not supported in Milestone 13";
      return false;
    }
    const auto arbitrary =
        (descriptor.capabilities & GWIPC_OUTPUT_CAP_ARBITRARY_HEADLESS_MODE) != 0;
    const auto mode = std::find_if(
        snapshot.modes.begin(), snapshot.modes.end(), [&](const auto &candidate) {
          return candidate.output_id == output.id &&
                 candidate.width == edit.mode->first &&
                 candidate.height == edit.mode->second &&
                 (!edit.refresh_millihertz ||
                  candidate.refresh_millihertz == *edit.refresh_millihertz);
        });
    if (!arbitrary && mode == snapshot.modes.end()) {
      error = "requested mode is not in the output inventory";
      return false;
    }
    output.physical_width = edit.mode->first;
    output.physical_height = edit.mode->second;
    if (edit.refresh_millihertz)
      output.refresh_millihertz = *edit.refresh_millihertz;
    else if (mode != snapshot.modes.end())
      output.refresh_millihertz = mode->refresh_millihertz;
  }
  if (edit.position) {
    output.logical_x = edit.position->first;
    output.logical_y = edit.position->second;
  }
  if (edit.scale) {
    if ((descriptor.capabilities & GWIPC_OUTPUT_CAP_SCALE_CONFIGURABLE) == 0 ||
        edit.scale->second > descriptor.maximum_scale_denominator_value ||
        !scale_at_least(*edit.scale, descriptor.minimum_scale_numerator,
                        descriptor.minimum_scale_denominator) ||
        !scale_at_most(*edit.scale, descriptor.maximum_scale_numerator,
                       descriptor.maximum_scale_denominator)) {
      error = "requested exact scale is unsupported";
      return false;
    }
    output.scale_numerator = edit.scale->first;
    output.scale_denominator = edit.scale->second;
  }
  if (edit.transform) {
    const auto bit = UINT32_C(1) << static_cast<unsigned>(*edit.transform);
    if ((descriptor.capabilities & GWIPC_OUTPUT_CAP_TRANSFORM_CONFIGURABLE) == 0 ||
        (descriptor.transforms & bit) == 0) {
      error = "requested transform is unsupported";
      return false;
    }
    output.transform = *edit.transform;
  }
  if (!output.enabled) {
    output.logical_x = output.logical_y = 0;
    output.logical_width = output.logical_height = 0;
    output.physical_width = output.physical_height = 0;
    output.refresh_millihertz = 0;
  } else {
    auto width = output.physical_width;
    auto height = output.physical_height;
    if (swaps_axes(output.transform))
      std::swap(width, height);
    output.logical_width = logical_dimension(
        width, output.scale_numerator, output.scale_denominator);
    output.logical_height = logical_dimension(
        height, output.scale_numerator, output.scale_denominator);
  }
  return validate_layout(snapshot, error);
}

} // namespace glasswyrm::tools::output_client
