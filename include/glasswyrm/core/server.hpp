#pragma once

#include <string>

#include <glasswyrm/core/build_config.hpp>

namespace glasswyrm::core {

enum class ServerStatus {
  ScaffoldOnly,
  Ready,
  Error,
};

class Server {
 public:
  explicit Server(BuildConfig config = build_config());

  [[nodiscard]] const BuildConfig& config() const noexcept;
  [[nodiscard]] ServerStatus status() const noexcept;
  [[nodiscard]] std::string describe() const;

 private:
  BuildConfig config_;
  ServerStatus status_ = ServerStatus::ScaffoldOnly;
};

std::string status_name(ServerStatus status);

}  // namespace glasswyrm::core
