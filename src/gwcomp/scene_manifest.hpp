#pragma once

#include "compositor/scene.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace gw::compositor {

struct SceneManifestResult {
  std::uint64_t hash{};
  std::uint32_t surface_count{};
};

class SceneManifest final {
public:
  explicit SceneManifest(std::filesystem::path path) : path_(std::move(path)) {}

  [[nodiscard]] bool append(std::uint64_t commit_id, std::uint64_t generation,
                            const Scene &scene, SceneManifestResult &result,
                            std::string &error) const;
  [[nodiscard]] static bool describe(std::uint64_t commit_id,
                                     std::uint64_t generation,
                                     const Scene &scene,
                                     SceneManifestResult &result,
                                     std::string &json, std::string &error);

private:
  std::filesystem::path path_;
};

} // namespace gw::compositor
