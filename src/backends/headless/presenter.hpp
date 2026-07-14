#pragma once

#include "backends/headless/frame_dump.hpp"
#include "backends/output/presentation_backend.hpp"

#include <filesystem>
#include <utility>

namespace glasswyrm::headless {

class Presenter final : public output::PresentationBackend {
 public:
  explicit Presenter(std::filesystem::path dump_directory)
      : dumper_(std::move(dump_directory)) {}

  [[nodiscard]] output::PresentResult present(
      const output::SoftwareFrameView& frame) override;
  [[nodiscard]] int poll_fd() const noexcept override { return -1; }
  [[nodiscard]] short poll_events() const noexcept override { return 0; }
  [[nodiscard]] output::BackendEvent service(short revents) override;
  [[nodiscard]] output::BackendStateResult suspend(std::string& error) override;
  [[nodiscard]] output::PresentResult resume(
      const output::SoftwareFrameView& committed) override;
  [[nodiscard]] output::BackendStateResult shutdown(
      std::string& error) noexcept override {
    error.clear();
    return output::BackendStateResult::Complete;
  }

 private:
  FrameDumper dumper_;
};

}  // namespace glasswyrm::headless
