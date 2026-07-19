#pragma once

#include "backends/headless/vrr_simulation.hpp"
#include "backends/output/presentation_backend.hpp"
#include "backends/output/software_frame_set.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace glasswyrm::headless {

class VrrReport final {
public:
  VrrReport(const VrrReport &) = delete;
  VrrReport &operator=(const VrrReport &) = delete;
  VrrReport(VrrReport &&other) noexcept;
  VrrReport &operator=(VrrReport &&other) noexcept;
  ~VrrReport();

  [[nodiscard]] static std::optional<VrrReport>
  create(const std::filesystem::path &path, std::string &error);

  [[nodiscard]] bool
  record_capability(const VrrSimulationCapability &capability,
                    std::string &error);
  [[nodiscard]] bool record_presentation(
      const output::SoftwareFrameSetView &frames,
      const output::VrrPresentationFeedbackMap &feedback, std::string &error);
  [[nodiscard]] bool finish(std::string &error) noexcept;

private:
  struct Summary {
    std::uint64_t sample_count{};
    std::uint64_t enabled_count{};
    std::uint64_t disabled_count{};
    std::uint64_t minimum_interval{};
    std::uint64_t maximum_interval{};
    std::uint64_t interval_sum{};
    std::uint64_t pass_count{};
    std::vector<std::uint64_t> intervals;
    std::vector<std::uint64_t> absolute_errors;
  };

  explicit VrrReport(int fd) noexcept : fd_(fd) {}
  [[nodiscard]] bool append(const std::string &record,
                            std::string &error) noexcept;

  int fd_{-1};
  bool finished_{};
  std::map<std::uint64_t, Summary> summaries_;
};

} // namespace glasswyrm::headless
