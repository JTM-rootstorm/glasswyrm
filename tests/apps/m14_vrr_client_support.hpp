#pragma once

#include "m14_vrr_client_options.hpp"

#include "glasswyrm/ipc/output.h"

#include <cstdint>
#include <atomic>
#include <optional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gw::test::m14 {

inline constexpr std::uint16_t kPatternWidth = 256;
inline constexpr std::uint16_t kPatternHeight = 192;
inline constexpr std::uint16_t kDamageWidth = 64;
inline constexpr std::uint16_t kDamageHeight = 64;
inline constexpr std::uint64_t kFinalSpinNanoseconds = 200'000;
inline constexpr std::uint32_t kPresentationQueryFlags =
    GWIPC_OUTPUT_QUERY_DESCRIPTORS | GWIPC_OUTPUT_QUERY_LAYOUT |
    GWIPC_OUTPUT_QUERY_WINDOWS | GWIPC_OUTPUT_QUERY_VRR;

struct PresentationMarker {
  std::uint64_t commit_id{};
  std::uint64_t presented_generation{};

  [[nodiscard]] bool valid() const noexcept {
    return commit_id != 0 && presented_generation != 0;
  }
  friend bool operator==(const PresentationMarker &,
                         const PresentationMarker &) = default;
};

enum class PresentationObservationKind : std::uint8_t {
  Complete,
  RetryableNotReady,
  Fatal,
};

struct PresentationObservation {
  PresentationObservationKind kind{PresentationObservationKind::Fatal};
  PresentationMarker marker;
  std::uint64_t timestamp_nanoseconds{};
  std::string detail;
};

class PresentationObserver {
public:
  virtual ~PresentationObserver() = default;
  [[nodiscard]] virtual PresentationObservation observe() = 0;
};

enum class PresentationPacerAction : std::uint8_t {
  SubmitNow,
  Wait,
  FramePresented,
  MissedDeadline,
  Timeout,
  Complete,
  Fatal,
};

struct PresentationPacerEvent {
  PresentationPacerAction action{PresentationPacerAction::Fatal};
  std::uint32_t frame_ordinal{};
  std::uint64_t scheduled_deadline_nanoseconds{};
  std::string detail;
};

struct PresentationPacerStats {
  std::uint32_t scheduled_frame_count{};
  std::uint32_t submitted_frame_count{};
  std::uint32_t presented_frame_count{};
  PresentationMarker first_observed;
  PresentationMarker last_observed;
  std::uint32_t maximum_outstanding_updates{};
  std::uint32_t retryable_query_count{};
  std::uint32_t missed_deadline_count{};
  std::uint64_t maximum_completion_latency_nanoseconds{};
};

struct ClientState {
  ClientMode mode{ClientMode::Windowed};
  std::uint32_t window{};
  std::uint16_t width{};
  std::uint16_t height{};
  bool prefer{};
  bool fullscreen_requested{};
  bool borderless{};
  std::uint32_t frame_count{};
  std::uint32_t target_refresh_hz{};
  std::uint64_t target_interval_nanoseconds{};
  ClientPreference preference{ClientPreference::Default};
  bool events_selected{};
  std::uint32_t preference_reply_count{};
  std::uint32_t notify_event_count{};
  std::uint32_t notify_change_mask{};
  std::uint64_t reason_mask{};
  bool eventfd_synchronized{};
  std::string selected_output;
  PresentationPacerStats presentation;
  bool presentation_paced{};
};

class PresentationPacer {
public:
  PresentationPacer(std::uint32_t frame_count,
                    std::uint64_t interval_nanoseconds,
                    std::uint64_t completion_timeout_nanoseconds) noexcept;

  [[nodiscard]] bool begin(PresentationMarker initial,
                           std::uint64_t start_nanoseconds,
                           std::string &error) noexcept;
  [[nodiscard]] PresentationPacerEvent
  next(std::uint64_t now_nanoseconds) noexcept;
  [[nodiscard]] bool submitted(std::uint32_t frame_ordinal,
                               std::uint64_t now_nanoseconds,
                               std::string &error) noexcept;
  [[nodiscard]] PresentationPacerEvent
  observe(const PresentationObservation &observation) noexcept;
  [[nodiscard]] const PresentationPacerStats &stats() const noexcept {
    return stats_;
  }
  [[nodiscard]] bool frame_outstanding() const noexcept {
    return frame_outstanding_;
  }

private:
  [[nodiscard]] PresentationPacerEvent fatal(std::string detail) noexcept;
  std::uint64_t interval_nanoseconds_{};
  std::uint64_t completion_timeout_nanoseconds_{};
  std::uint64_t next_deadline_nanoseconds_{};
  std::uint64_t submission_nanoseconds_{};
  std::uint64_t completion_deadline_nanoseconds_{};
  PresentationPacerStats stats_;
  bool started_{};
  bool frame_outstanding_{};
  bool terminal_{};
};

[[nodiscard]] PresentationPacerEvent
observe_presentation(PresentationPacer &pacer, PresentationObserver &observer);

class EventfdDamageProducer {
public:
  EventfdDamageProducer();
  ~EventfdDamageProducer() noexcept;
  EventfdDamageProducer(const EventfdDamageProducer &) = delete;
  EventfdDamageProducer &operator=(const EventfdDamageProducer &) = delete;
  [[nodiscard]] std::vector<std::uint32_t> produce(std::uint32_t frame);

private:
  static void signal(int descriptor);
  static void wait(int descriptor);
  void run();
  int request_{-1};
  int ready_{-1};
  std::atomic<bool> stop_{};
  std::atomic<std::uint32_t> frame_{};
  std::mutex mutex_;
  std::vector<std::uint32_t> pixels_;
  std::thread worker_;
};

[[nodiscard]] std::uint64_t
target_interval_nanoseconds(std::uint32_t refresh_hz) noexcept;
[[nodiscard]] bool absolute_deadline(std::uint64_t start_nanoseconds,
                                     std::uint64_t interval_nanoseconds,
                                     std::uint32_t frame_index,
                                     std::uint64_t &deadline) noexcept;
[[nodiscard]] bool wait_until_monotonic(
    std::uint64_t deadline_nanoseconds,
    std::uint64_t final_spin_nanoseconds = kFinalSpinNanoseconds) noexcept;

[[nodiscard]] std::vector<std::uint32_t>
deterministic_pattern(std::uint16_t width, std::uint16_t height);
[[nodiscard]] std::vector<std::uint32_t>
deterministic_damage(std::uint32_t frame_index);
[[nodiscard]] std::string client_state_json(const ClientState &state);
void write_client_state(const std::string &path, const ClientState &state);

} // namespace gw::test::m14
