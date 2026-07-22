#pragma once

#include "compositor/scene.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>

namespace gw::compositor {

struct SceneManifestResult {
  std::uint64_t hash{};
  std::uint32_t surface_count{};
  std::uint32_t cursor_count{};
};

struct PreparedSceneManifest {
  SceneManifestResult result;
  std::string json;
  bool active{};
  bool publication_uncertain{};
  std::uint64_t uncertain_original_size{};
  std::uint64_t uncertain_expected_size{};
};

class SceneManifestIo {
public:
  virtual ~SceneManifestIo() = default;

  [[nodiscard]] virtual int open(const char *path, int flags,
                                 mode_t mode) const = 0;
  [[nodiscard]] virtual int stat(int fd, struct stat *status) const = 0;
  [[nodiscard]] virtual int lock(int fd, int operation) const = 0;
  [[nodiscard]] virtual ssize_t write(int fd, const void *data,
                                      std::size_t size) const = 0;
  [[nodiscard]] virtual ssize_t read_at(int fd, void *data, std::size_t size,
                                        off_t offset) const = 0;
  [[nodiscard]] virtual int synchronize(int fd) const = 0;
  [[nodiscard]] virtual int truncate(int fd, off_t size) const = 0;
  [[nodiscard]] virtual int close(int fd) const = 0;
};

class SceneManifest final {
public:
  explicit SceneManifest(std::filesystem::path path,
                         std::shared_ptr<const SceneManifestIo> io = {});

  [[nodiscard]] bool append(std::uint64_t commit_id, std::uint64_t generation,
                            const Scene &scene, SceneManifestResult &result,
                            std::string &error) const;
  [[nodiscard]] static bool prepare(std::uint64_t commit_id,
                                    std::uint64_t generation,
                                    const Scene &scene,
                                    PreparedSceneManifest &prepared,
                                    std::string &error);
  [[nodiscard]] static bool prepare_output_model(
      std::uint64_t commit_id, std::uint64_t generation, const Scene &scene,
      PreparedSceneManifest &prepared, std::string &error);
  [[nodiscard]] bool publish(PreparedSceneManifest &prepared,
                             std::string &error) const;
  static void abort(PreparedSceneManifest &prepared) noexcept;
  [[nodiscard]] static bool describe(std::uint64_t commit_id,
                                     std::uint64_t generation,
                                     const Scene &scene,
                                     SceneManifestResult &result,
                                     std::string &json, std::string &error);
  [[nodiscard]] static bool describe_output_model(
      std::uint64_t commit_id, std::uint64_t generation, const Scene &scene,
      SceneManifestResult &result, std::string &json, std::string &error);

private:
  std::filesystem::path path_;
  std::shared_ptr<const SceneManifestIo> io_;
};

} // namespace gw::compositor
