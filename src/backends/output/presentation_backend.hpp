#pragma once

#include "backends/output/software_frame.hpp"

#include <cstdint>
#include <string>

namespace glasswyrm::output {

enum class PresentDisposition { Complete, Pending, Rejected, Fatal };

struct PresentResult {
  PresentDisposition disposition{PresentDisposition::Rejected};
  std::uint64_t token{};
  std::uint64_t visible_hash{};
  std::string error;
};

enum class BackendEventKind { None, Complete, Fatal };

struct BackendEvent {
  BackendEventKind kind{BackendEventKind::None};
  std::uint64_t token{};
  std::uint64_t visible_hash{};
  std::string error;
};

enum class BackendStateResult { Complete, Fatal };

class PresentationBackend {
 public:
  virtual ~PresentationBackend() = default;

  [[nodiscard]] virtual PresentResult present(const SoftwareFrameView& frame) = 0;
  [[nodiscard]] virtual int poll_fd() const noexcept = 0;
  [[nodiscard]] virtual short poll_events() const noexcept = 0;
  [[nodiscard]] virtual BackendEvent service(short revents) = 0;
  [[nodiscard]] virtual BackendStateResult suspend(std::string& error) = 0;
  [[nodiscard]] virtual PresentResult resume(
      const SoftwareFrameView& committed) = 0;
  virtual void shutdown() noexcept = 0;
};

}  // namespace glasswyrm::output
