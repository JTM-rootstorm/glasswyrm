#ifndef GLASSWYRM_WM_TYPES_HPP
#define GLASSWYRM_WM_TYPES_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace glasswyrm::wm {

inline constexpr std::size_t maximum_windows = 4096;
inline constexpr std::uint32_t maximum_work_extent = 16384;
inline constexpr std::uint32_t maximum_window_extent = 16384;
inline constexpr std::uint32_t maximum_border_width = 65535;

enum class WindowType : std::uint8_t { Unknown, Normal, Dialog, Utility };
enum class DecorationPreference : std::uint8_t { Unknown, False, True };
enum class AppliedState : std::uint8_t { Normal = 1, Maximized, Fullscreen, Minimized };
enum class TriState : std::uint8_t { Unknown, False, True };
enum class StackMode : std::uint8_t { None, Above, Below };
enum class EvaluationError : std::uint8_t {
  None,
  IncompleteSnapshot,
  InvalidContext,
  InvalidWindow,
  UnknownReference,
  Limit,
  UnsupportedMetadata,
  OutputFailure,
};

struct Context {
  std::uint32_t root_window_id{};
  std::uint32_t workspace_id{};
  std::uint64_t output_id{};
  std::int32_t work_x{};
  std::int32_t work_y{};
  std::uint32_t work_width{};
  std::uint32_t work_height{};
  std::uint32_t flags{};
};

struct RawWindow {
  std::uint32_t window_id{};
  std::uint32_t parent_window_id{};
  std::uint32_t transient_for{};
  std::uint32_t workspace_id{};
  std::int32_t requested_x{};
  std::int32_t requested_y{};
  std::uint32_t requested_width{};
  std::uint32_t requested_height{};
  std::uint32_t border_width{};
  WindowType window_type{WindowType::Unknown};
  bool wants_map{};
  bool override_redirect{};
  DecorationPreference decoration_preference{DecorationPreference::Unknown};
  bool fullscreen_requested{};
  bool maximized_requested{};
  bool minimized_requested{};
  bool attention_requested{};
  std::uint64_t creation_serial{};
  std::uint64_t map_serial{};
  std::uint64_t focus_serial{};
  std::uint32_t flags{};
  std::uint64_t geometry_serial{};
  std::uint64_t stack_serial{};
  std::uint32_t stack_sibling{};
  StackMode stack_mode{StackMode::None};
};

struct RawState {
  bool complete{};
  std::uint64_t producer_generation{};
  Context context{};
  bool has_context{};
  std::map<std::uint32_t, RawWindow> windows;
};

struct WindowState {
  std::uint32_t window_id{};
  std::uint32_t transient_for{};
  std::uint32_t workspace_id{};
  std::uint64_t output_id{};
  std::int32_t final_x{};
  std::int32_t final_y{};
  std::uint32_t final_width{};
  std::uint32_t final_height{};
  std::int32_t stacking{-1};
  WindowType window_type{WindowType::Unknown};
  AppliedState applied_state{AppliedState::Normal};
  bool visible{};
  bool focused{};
  bool managed{};
  bool decoration_eligible{};
  bool override_redirect{};
  bool attention_requested{};
  TriState fullscreen_eligible{TriState::False};
  TriState direct_scanout_eligible{TriState::Unknown};
};

struct PolicyState {
  std::uint64_t generation{};
  std::uint64_t hash{};
  Context context{};
  std::map<std::uint32_t, WindowState> windows;
  std::vector<std::uint32_t> output_order;
};

struct Evaluation {
  EvaluationError error{EvaluationError::None};
  PolicyState policy;
  [[nodiscard]] explicit operator bool() const noexcept {
    return error == EvaluationError::None;
  }
};

}  // namespace glasswyrm::wm

#endif
