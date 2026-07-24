#pragma once

#include "backends/output/presentation_backend.hpp"
#include "compositor/scene.hpp"
#include "compositor/vrr_state.hpp"
#include "output/vrr/decision.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace gw::compositor {

struct PreparedVrrOutput {
  glasswyrm::output::VrrPresentationRequest request;
  glasswyrm::output::VrrPresentationCapability capability;
};

struct PreparedVrrFrame {
  using OutputMap = std::map<std::uint64_t, PreparedVrrOutput>;
  using RequestMap =
      std::map<std::uint64_t, glasswyrm::output::VrrPresentationRequest>;

  OutputMap outputs;

  [[nodiscard]] RequestMap presentation_requests() const;
};

struct CompletedVrrFrame {
  CommittedVrrState::OutputMap outputs;
};

class VrrRuntime final {
public:
  [[nodiscard]] static std::optional<PreparedVrrFrame> prepare(
      const Scene& scene,
      const glasswyrm::output::PresentationBackend& presenter,
      const CommittedVrrState& committed, std::string& error);

  [[nodiscard]] static std::optional<CompletedVrrFrame> complete(
      const PreparedVrrFrame& prepared,
      const glasswyrm::output::VrrPresentationFeedbackMap& feedback,
      std::uint64_t commit_id, std::uint64_t presented_generation,
      std::string& error);
};

} // namespace gw::compositor
