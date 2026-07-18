#pragma once

#include "ipc/wire/compositor_contract.hpp"
#include "ipc/wire/output_contract.hpp"
#include "output/model/layout.hpp"

#include <cstdint>
#include <map>
#include <optional>

namespace glasswyrm::server {

enum class OutputConfigurationSnapshotStatus {
  Accepted,
  Busy,
  InvalidState,
  InvalidRecord,
  CountMismatch,
};

enum class OutputConfigurationStage {
  Idle,
  Collecting,
  SnapshotReady,
  PolicyPending,
  CompositorPending,
  CommitReady,
  RollbackPending,
};

struct OutputConfigurationTransaction {
  std::uint64_t configuration_id{};
  output::OutputLayout old_layout;
  output::OutputLayout proposed_layout;
};

class OutputConfigurationCoordinator final {
public:
  explicit OutputConfigurationCoordinator(output::OutputLayout inventory);

  [[nodiscard]] bool valid() const noexcept { return valid_; }
  [[nodiscard]] OutputConfigurationStage stage() const noexcept {
    return stage_;
  }
  [[nodiscard]] OutputConfigurationSnapshotStatus
  begin_snapshot(std::uint64_t snapshot_id, std::uint32_t expected_item_count);
  [[nodiscard]] OutputConfigurationSnapshotStatus
  stage_output(const gw::ipc::wire::OutputUpsert &output);
  [[nodiscard]] OutputConfigurationSnapshotStatus
  end_snapshot(std::uint64_t snapshot_id, std::uint32_t actual_item_count);
  void abort_snapshot() noexcept;

  [[nodiscard]] std::optional<
      gw::ipc::wire::OutputConfigurationAcknowledged>
  submit(const gw::ipc::wire::OutputConfigurationCommit &commit);
  [[nodiscard]] bool accept_policy() noexcept;
  [[nodiscard]] std::optional<
      gw::ipc::wire::OutputConfigurationAcknowledged>
  reject_policy() noexcept;
  [[nodiscard]] bool accept_compositor() noexcept;
  [[nodiscard]] bool can_accept_compositor() const noexcept {
    return stage_ == OutputConfigurationStage::CompositorPending &&
           transaction_.has_value();
  }
  [[nodiscard]] bool begin_rollback(
      gw::ipc::wire::OutputConfigurationResult rejection) noexcept;
  [[nodiscard]] std::optional<
      gw::ipc::wire::OutputConfigurationAcknowledged>
  finish_rollback(bool succeeded) noexcept;
  [[nodiscard]] std::optional<
      gw::ipc::wire::OutputConfigurationAcknowledged>
  commit() noexcept;
  [[nodiscard]] std::optional<
      gw::ipc::wire::OutputConfigurationAcknowledged>
  fail_internal() noexcept;

  [[nodiscard]] const OutputConfigurationTransaction *transaction()
      const noexcept {
    return transaction_ ? &*transaction_ : nullptr;
  }
  [[nodiscard]] const output::OutputLayout &committed_layout() const noexcept {
    return committed_layout_;
  }
  [[nodiscard]] const output::OutputLayout &inventory() const noexcept {
    return inventory_;
  }

private:
  using Result = gw::ipc::wire::OutputConfigurationResult;
  using Acknowledged = gw::ipc::wire::OutputConfigurationAcknowledged;

  [[nodiscard]] Acknowledged acknowledgement(std::uint64_t request_id,
                                             Result result) const noexcept;
  [[nodiscard]] Result build_candidate(
      const gw::ipc::wire::OutputConfigurationCommit &commit,
      output::OutputLayout &candidate) const;
  void clear_staging() noexcept;
  void clear_transaction() noexcept;

  bool valid_{};
  OutputConfigurationStage stage_{OutputConfigurationStage::Idle};
  std::uint64_t snapshot_id_{};
  std::uint32_t expected_item_count_{};
  std::uint32_t item_count_{};
  OutputConfigurationSnapshotStatus snapshot_failure_{
      OutputConfigurationSnapshotStatus::Accepted};
  Result staged_result_{Result::InvalidLayout};
  std::map<output::OutputId, gw::ipc::wire::OutputUpsert> staged_outputs_;
  output::OutputLayout inventory_;
  output::OutputLayout committed_layout_;
  std::optional<OutputConfigurationTransaction> transaction_;
  Result rollback_result_{Result::CompositorRejected};
};

} // namespace glasswyrm::server
