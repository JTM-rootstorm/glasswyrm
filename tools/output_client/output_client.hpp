#pragma once

#include <glasswyrm/ipc.h>

#include <cstdint>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace glasswyrm::tools::output_client {

struct OutputDescriptor {
  std::uint64_t id{};
  gwipc_output_kind kind{GWIPC_OUTPUT_HEADLESS};
  std::uint32_t capabilities{};
  std::string name;
  std::uint32_t physical_width_mm{};
  std::uint32_t physical_height_mm{};
  std::uint32_t transforms{};
  std::uint32_t minimum_scale_numerator{};
  std::uint32_t minimum_scale_denominator{};
  std::uint32_t maximum_scale_numerator{};
  std::uint32_t maximum_scale_denominator{};
  std::uint32_t maximum_scale_denominator_value{};
  std::uint32_t maximum_physical_width{};
  std::uint32_t maximum_physical_height{};
};

struct OutputMode {
  std::uint64_t id{};
  std::uint64_t output_id{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t refresh_millihertz{};
  bool preferred{};
  bool current{};
};

struct OutputState {
  std::uint64_t id{};
  bool enabled{};
  std::int32_t logical_x{};
  std::int32_t logical_y{};
  std::uint32_t logical_width{};
  std::uint32_t logical_height{};
  std::uint32_t physical_width{};
  std::uint32_t physical_height{};
  std::uint32_t refresh_millihertz{};
  std::uint32_t scale_numerator{};
  std::uint32_t scale_denominator{};
  gwipc_transform transform{GWIPC_TRANSFORM_NORMAL};
};

struct WindowState {
  std::uint64_t surface_id{};
  std::uint32_t window_id{};
  std::int32_t x{};
  std::int32_t y{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint64_t primary_output_id{};
  std::vector<std::uint64_t> output_ids;
  std::uint32_t preferred_scale_numerator{1};
  std::uint32_t preferred_scale_denominator{1};
  std::uint32_t client_buffer_scale{1};
  gwipc_surface_scale_mode scale_mode{GWIPC_SURFACE_SCALE_LEGACY};
  bool visible{};
  bool focused{};
  bool fullscreen{};
};

struct VrrCapability {
  std::uint64_t output_id{};
  bool connector_property_present{};
  bool hardware_capable{};
  bool kms_controllable{};
  bool simulated{};
  bool range_available{};
  bool atomic_required{};
  std::uint32_t minimum_refresh_millihertz{};
  std::uint32_t maximum_refresh_millihertz{};
  std::uint64_t reason_flags{};
};

struct VrrOutputState {
  std::uint64_t output_id{};
  gwipc_vrr_policy_mode requested_mode{GWIPC_VRR_POLICY_OFF};
  gwipc_vrr_decision decision{GWIPC_VRR_DECISION_DISABLED};
  bool desired_enabled{};
  bool effective_enabled{};
  bool property_readback_valid{};
  bool session_active{};
  std::uint32_t candidate_window_id{};
  std::uint64_t candidate_surface_id{};
  std::uint64_t reason_flags{};
  std::uint64_t state_generation{};
  std::uint64_t transition_serial{};
  std::uint64_t last_commit_id{};
  std::uint64_t last_presented_generation{};
  std::uint32_t last_flip_sequence{};
  std::uint64_t last_flip_timestamp_nanoseconds{};
  std::uint64_t last_interval_nanoseconds{};
};

struct VrrWindowState {
  std::uint64_t surface_id{};
  std::uint32_t window_id{};
  std::uint64_t output_id{};
  gwipc_vrr_window_preference preference{GWIPC_VRR_PREFERENCE_DEFAULT};
  bool policy_selected{};
  bool policy_eligible{};
  bool focused{};
  bool fullscreen{};
  bool borderless_fullscreen{};
  bool exclusive_output_membership{};
  std::uint64_t reason_flags{};
  std::uint64_t policy_generation{};
};

struct VrrTiming {
  std::uint64_t output_id{};
  std::uint64_t commit_id{};
  std::uint64_t presented_generation{};
  std::uint32_t flip_sequence{};
  std::uint32_t flags{};
  std::uint64_t kernel_timestamp_nanoseconds{};
  std::uint64_t interval_nanoseconds{};
  bool effective_enabled{};
  bool timestamp_available{};
};

struct Snapshot {
  std::uint64_t generation{};
  std::uint64_t primary_output_id{};
  std::uint32_t root_width{};
  std::uint32_t root_height{};
  std::uint32_t enabled_output_count{};
  std::map<std::uint64_t, OutputDescriptor> descriptors;
  std::vector<OutputMode> modes;
  std::map<std::uint64_t, OutputState> outputs;
  std::map<std::uint32_t, WindowState> windows;
  std::map<std::uint64_t, VrrCapability> vrr_capabilities;
  std::map<std::uint64_t, gwipc_vrr_policy_mode> vrr_policies;
  std::map<std::uint64_t, VrrOutputState> vrr_outputs;
  std::map<std::uint32_t, VrrWindowState> vrr_windows;
  std::map<std::uint64_t, VrrTiming> vrr_timings;
  bool vrr_queried{};
};

struct Edit {
  std::optional<bool> enabled;
  std::optional<std::pair<std::uint32_t, std::uint32_t>> mode;
  std::optional<std::uint32_t> refresh_millihertz;
  std::optional<std::pair<std::int32_t, std::int32_t>> position;
  std::optional<std::pair<std::uint32_t, std::uint32_t>> scale;
  std::optional<gwipc_transform> transform;
  std::optional<gwipc_vrr_policy_mode> vrr_policy;
  bool primary{};
};

class Client final {
public:
  explicit Client(std::string socket_path)
      : socket_path_(std::move(socket_path)) {}
  ~Client();
  Client(const Client &) = delete;
  Client &operator=(const Client &) = delete;

  [[nodiscard]] bool query(std::uint32_t flags, Snapshot &snapshot,
                           std::string &error,
                           bool complete_configuration = false);
  [[nodiscard]] bool commit(const Snapshot &snapshot,
                            gwipc_output_configuration_acknowledged &ack,
                            std::string &error);

private:
  [[nodiscard]] bool connect(std::string &error);
  [[nodiscard]] bool wait_established(std::string &error);
  std::string socket_path_;
  gwipc_connection *connection_{};
  std::uint64_t next_request_id_{1};
};

[[nodiscard]] bool apply_edit(Snapshot &snapshot, std::string_view selector,
                              const Edit &edit, std::string &error);
[[nodiscard]] std::optional<gwipc_transform>
parse_transform(std::string_view value) noexcept;
[[nodiscard]] const char *transform_name(gwipc_transform value) noexcept;
[[nodiscard]] bool parse_scale(std::string_view value,
                               std::pair<std::uint32_t, std::uint32_t> &scale);
[[nodiscard]] bool
parse_position(std::string_view value,
               std::pair<std::int32_t, std::int32_t> &position);
[[nodiscard]] bool parse_mode(std::string_view value,
                              std::pair<std::uint32_t, std::uint32_t> &extent,
                              std::optional<std::uint32_t> &refresh);

void print_outputs(const Snapshot &snapshot, bool json, std::ostream &output);
void print_windows(const Snapshot &snapshot, bool json, std::ostream &output);
void print_all(const Snapshot &snapshot, bool json, std::ostream &output);
void print_acknowledgement(const gwipc_output_configuration_acknowledged &ack,
                           bool json, std::ostream &output);
[[nodiscard]] std::optional<gwipc_vrr_policy_mode>
parse_vrr_policy(std::string_view value) noexcept;
[[nodiscard]] const char *vrr_policy_name(gwipc_vrr_policy_mode value) noexcept;
[[nodiscard]] const char *
vrr_preference_name(gwipc_vrr_window_preference value) noexcept;
[[nodiscard]] const char *vrr_decision_name(gwipc_vrr_decision value) noexcept;
[[nodiscard]] std::vector<std::string_view>
vrr_reason_names(std::uint64_t reasons);
[[nodiscard]] bool apply_vrr_edit(Snapshot &snapshot, std::string_view selector,
                                  gwipc_vrr_policy_mode mode,
                                  std::string &error);
[[nodiscard]] bool vrr_output_matches(const Snapshot &snapshot,
                                      std::uint64_t output_id,
                                      std::string_view selector) noexcept;
void print_vrr(const Snapshot &snapshot,
               std::optional<std::string_view> selector, bool json,
               std::ostream &output);
void append_vrr_output_json(const Snapshot &snapshot, std::uint64_t output_id,
                            std::ostream &output);
void append_vrr_output_text(const Snapshot &snapshot, std::uint64_t output_id,
                            std::ostream &output);
void append_vrr_window_json(const Snapshot &snapshot, std::uint32_t window_id,
                            std::ostream &output);
void append_vrr_window_text(const Snapshot &snapshot, std::uint32_t window_id,
                            std::ostream &output);

} // namespace glasswyrm::tools::output_client
