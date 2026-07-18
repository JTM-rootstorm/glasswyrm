#ifndef GLASSWYRM_WM_MULTI_OUTPUT_POLICY_HPP
#define GLASSWYRM_WM_MULTI_OUTPUT_POLICY_HPP

#include "wm/types.hpp"

#include <cstddef>
#include <cstdint>
#include <map>

namespace glasswyrm::wm {

struct OutputSelection {
  Rectangle geometry;
  std::uint64_t inherited_output_id{};
  std::uint64_t previous_output_id{};
  std::uint64_t preferred_output_id{};
  bool retain_previous{};
};

[[nodiscard]] std::uint64_t select_output(
    const std::map<std::uint64_t, OutputContext>& outputs,
    std::uint64_t primary_output_id,
    const OutputSelection& selection) noexcept;

[[nodiscard]] Rectangle initial_placement(const OutputContext& output,
                                          std::uint32_t width,
                                          std::uint32_t height,
                                          std::size_t cascade_slot) noexcept;
[[nodiscard]] Rectangle fullscreen_geometry(
    const OutputContext& output) noexcept;
[[nodiscard]] Rectangle maximize_geometry(
    const OutputContext& output) noexcept;
[[nodiscard]] Rectangle retain_visible_pixel(
    const std::map<std::uint64_t, OutputContext>& outputs,
    std::uint64_t assigned_output_id, Rectangle geometry) noexcept;

}  // namespace glasswyrm::wm

#endif
