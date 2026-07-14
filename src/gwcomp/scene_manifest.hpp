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

struct PreparedSceneManifest {
  SceneManifestResult result;
  std::string json;
  bool active{};
};

class SceneManifest final {
public:
  explicit SceneManifest(std::filesystem::path path) : path_(std::move(path)) {}

  [[nodiscard]] bool append(std::uint64_t commit_id, std::uint64_t generation,
                            const Scene &scene, SceneManifestResult &result,
                            std::string &error) const;
  [[nodiscard]] static bool prepare(std::uint64_t commit_id,
                                    std::uint64_t generation,
                                    const Scene &scene,
                                    PreparedSceneManifest &prepared,
                                    std::string &error);
  [[nodiscard]] bool publish(PreparedSceneManifest &prepared,
                             std::string &error) const;
  static void abort(PreparedSceneManifest &prepared) noexcept;
  [[nodiscard]] static bool describe(std::uint64_t commit_id,
                                     std::uint64_t generation,
                                     const Scene &scene,
                                     SceneManifestResult &result,
                                     std::string &json, std::string &error);

private:
  std::filesystem::path path_;
};

} // namespace gw::compositor
