#pragma once

#include "output/model/layout.hpp"

#include <glasswyrm/ipc.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace glasswyrm::server {

enum class CompositorInventoryState {
  Idle,
  AwaitingSnapshot,
  ReadingDescriptors,
  ReadingModes,
  ReadingLayout,
  ReadingVrrCapabilities,
  ReadingVrrPolicies,
  ReadingVrrStates,
  ReadingPresentationTiming,
  AwaitingAcknowledgement,
  Complete,
  Failed,
};

enum class CompositorInventoryFailure {
  None,
  InvalidState,
  InvalidQuery,
  EncodeFailed,
  EnqueueFailed,
  UnexpectedMessage,
  InvalidEnvelope,
  InvalidSnapshot,
  InvalidOrder,
  InvalidRecord,
  CountMismatch,
  CorrelationMismatch,
  Rejected,
  InvalidLayout,
};

class CompositorInventoryQuerySink {
public:
  virtual ~CompositorInventoryQuerySink() = default;
  [[nodiscard]] virtual gwipc_status
  enqueue(const gwipc_outgoing_message &message,
          std::uint64_t &sequence) = 0;
};

class CompositorOutputInventory final {
public:
  explicit CompositorOutputInventory(bool vrr_profile = false)
      : vrr_profile_(vrr_profile) {}
  [[nodiscard]] bool start(gwipc_connection *connection,
                           std::uint64_t query_id, std::string &error);
  [[nodiscard]] bool start(CompositorInventoryQuerySink &sink,
                           std::uint64_t query_id, std::string &error);
  [[nodiscard]] bool consume(const gwipc_message *message,
                             std::string &error);

  [[nodiscard]] CompositorInventoryState state() const noexcept {
    return state_;
  }
  [[nodiscard]] CompositorInventoryFailure failure() const noexcept {
    return failure_;
  }
  [[nodiscard]] std::uint64_t query_id() const noexcept { return query_id_; }
  [[nodiscard]] std::uint64_t query_sequence() const noexcept {
    return query_sequence_;
  }
  [[nodiscard]] const output::OutputLayout *layout() const noexcept {
    return completed_layout_ ? &*completed_layout_ : nullptr;
  }
  [[nodiscard]] const std::vector<gwipc_output_vrr_capability_upsert>&
  vrr_capabilities() const noexcept {
    return vrr_capabilities_;
  }
  [[nodiscard]] const std::vector<gwipc_output_vrr_policy_upsert>&
  vrr_policies() const noexcept {
    return vrr_policies_;
  }
  [[nodiscard]] const std::vector<gwipc_output_vrr_state_upsert>&
  vrr_states() const noexcept {
    return vrr_states_;
  }
  [[nodiscard]] const std::vector<gwipc_presentation_timing>& timings()
      const noexcept {
    return timings_;
  }

private:
  [[nodiscard]] bool consume_begin(const gwipc_message *message,
                                   std::string &error);
  [[nodiscard]] bool consume_descriptor(const gwipc_message *message,
                                        std::string &error);
  [[nodiscard]] bool consume_mode(const gwipc_message *message,
                                  std::string &error);
  [[nodiscard]] bool consume_state(const gwipc_message *message,
                                   std::string &error);
  [[nodiscard]] bool consume_end(const gwipc_message *message,
                                 std::string &error);
  [[nodiscard]] bool consume_vrr_capability(const gwipc_message* message,
                                            std::string& error);
  [[nodiscard]] bool consume_vrr_policy(const gwipc_message* message,
                                        std::string& error);
  [[nodiscard]] bool consume_vrr_state(const gwipc_message* message,
                                       std::string& error);
  [[nodiscard]] bool consume_timing(const gwipc_message* message,
                                    std::string& error);
  [[nodiscard]] bool consume_acknowledgement(const gwipc_message *message,
                                             std::string &error);
  [[nodiscard]] bool fail(CompositorInventoryFailure failure,
                          const char *message, std::string &error);
  [[nodiscard]] bool consume_item_envelope(const gwipc_message *message,
                                           std::string &error);

  CompositorInventoryState state_{CompositorInventoryState::Idle};
  CompositorInventoryFailure failure_{CompositorInventoryFailure::None};
  std::string failure_message_;
  std::uint64_t query_id_{};
  std::uint64_t query_sequence_{};
  std::uint64_t snapshot_id_{};
  std::uint64_t generation_{};
  std::uint32_t expected_item_count_{};
  std::uint32_t item_count_{};
  std::vector<output::OutputId> descriptor_order_;
  std::size_t mode_output_index_{};
  output::OutputModeId last_mode_id_{};
  std::size_t state_index_{};
  output::OutputLayout pending_layout_;
  std::optional<output::OutputLayout> completed_layout_;
  bool vrr_profile_{};
  std::vector<gwipc_output_vrr_capability_upsert> vrr_capabilities_;
  std::vector<gwipc_output_vrr_policy_upsert> vrr_policies_;
  std::vector<gwipc_output_vrr_state_upsert> vrr_states_;
  std::vector<gwipc_presentation_timing> timings_;
};

[[nodiscard]] const char *compositor_inventory_failure_name(
    CompositorInventoryFailure failure) noexcept;

} // namespace glasswyrm::server
