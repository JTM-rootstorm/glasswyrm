#pragma once

#include "input/device_path_allowlist.hpp"
#include "input/libinput_api.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace glasswyrm::input {

enum class RealInputKind { MotionRelative, MotionAbsolute, Button, Key };

struct RealInputRecord {
  RealInputKind kind{RealInputKind::MotionRelative};
  std::uint64_t device_id{};
  std::uint32_t time_ms{};
  std::int32_t root_x{};
  std::int32_t root_y{};
  std::uint32_t code{};
  bool pressed{};
};

struct InputReadiness {
  bool keyboard{};
  bool pointer{};
  [[nodiscard]] bool ready() const noexcept { return keyboard && pointer; }
};

struct InputDrainBudget {
  std::size_t maximum_events{256};
  std::size_t maximum_work_units{256};
};

enum class InputServiceStatus { Complete, BudgetExhausted, Inactive, Fatal };

struct InputServiceResult {
  InputServiceStatus status{InputServiceStatus::Complete};
  std::vector<RealInputRecord> records;
  std::size_t consumed_events{};
  std::size_t ignored_events{};
  std::string error;
  bool provider_state_reset{};
};

class LibinputTimestampConverter {
 public:
  [[nodiscard]] std::uint32_t convert(std::uint64_t time_usec) noexcept;
  void reset() noexcept {
    last_usec_ = 0;
    last_ms_ = 1;
  }

 private:
  std::uint64_t last_usec_{};
  std::uint32_t last_ms_{1};
};

class LibinputBackend {
 public:
  explicit LibinputBackend(LibinputApi &api) : api_(api) {}
  LibinputBackend(const LibinputBackend &) = delete;
  LibinputBackend &operator=(const LibinputBackend &) = delete;

  [[nodiscard]] bool initialize(std::span<const std::string> device_paths,
                                std::uint32_t root_width,
                                std::uint32_t root_height,
                                std::string &error);
  [[nodiscard]] InputServiceResult service(
      InputDrainBudget budget = InputDrainBudget{});
  [[nodiscard]] bool suspend(std::string &error);
  [[nodiscard]] bool resume(std::string &error);

  [[nodiscard]] bool initialized() const noexcept { return initialized_; }
  [[nodiscard]] bool active() const noexcept { return active_; }
  [[nodiscard]] int poll_fd() const noexcept {
    return initialized_ ? api_.poll_fd() : -1;
  }
  [[nodiscard]] InputReadiness readiness() const noexcept;
  [[nodiscard]] std::int32_t pointer_x() const noexcept { return pointer_x_; }
  [[nodiscard]] std::int32_t pointer_y() const noexcept { return pointer_y_; }
  [[nodiscard]] std::size_t held_key_count() const noexcept;
  [[nodiscard]] std::size_t held_button_count() const noexcept;
  [[nodiscard]] std::size_t bounded_diagnostic_count() const noexcept {
    return bounded_diagnostics_;
  }

 private:
  struct DeviceState {
    std::uint8_t capabilities{};
    std::set<std::uint32_t> held_keys;
    std::set<std::uint32_t> held_buttons;
    double vertical_v120{};
    double horizontal_v120{};
  };

  [[nodiscard]] InputServiceResult drain(InputDrainBudget budget,
                                         bool publish_records);
  void convert(const LibinputEvent &event, InputServiceResult &result,
               bool publish_records);
  void add_button_record(const LibinputEvent &event, std::uint32_t button,
                         bool pressed, InputServiceResult &result,
                         bool publish_records);
  void clear_provider_state() noexcept;
  void note_ignored(InputServiceResult &result) noexcept;

  static constexpr std::size_t kMaximumDiagnostics = 32;
  LibinputApi &api_;
  std::optional<DevicePathAllowlist> access_;
  std::map<std::uint64_t, DeviceState> devices_;
  LibinputTimestampConverter timestamp_;
  std::uint32_t root_width_{};
  std::uint32_t root_height_{};
  std::int32_t pointer_x_{};
  std::int32_t pointer_y_{};
  double relative_x_{};
  double relative_y_{};
  bool initialized_{};
  bool active_{};
  std::size_t bounded_diagnostics_{};
};

}  // namespace glasswyrm::input
