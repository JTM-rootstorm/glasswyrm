#include "gwcomp/scene_manifest.hpp"
#include "tests/helpers/test_support.hpp"

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

enum class Failure {
  None,
  Open,
  Stat,
  Lock,
  PartialWrite,
  FirstSync,
  Truncate,
  RollbackSync,
  Unlock,
  Close,
};

class FakeIo final : public gw::compositor::SceneManifestIo {
public:
  explicit FakeIo(const Failure initial) : failure(initial) {}

  int open(const char *path, const int flags,
           const mode_t mode) const override {
    if (failure == Failure::Open) {
      errno = EACCES;
      return -1;
    }
    return ::open(path, flags, mode);
  }

  int stat(const int fd, struct stat *status) const override {
    if (failure == Failure::Stat) {
      errno = EIO;
      return -1;
    }
    return ::fstat(fd, status);
  }

  int lock(const int fd, const int operation) const override {
    if (failure == Failure::Lock && operation == LOCK_EX) {
      errno = EBUSY;
      return -1;
    }
    if (failure == Failure::Unlock && operation == LOCK_UN &&
        unlock_calls++ == 0) {
      errno = EBUSY;
      return -1;
    }
    return ::flock(fd, operation);
  }

  ssize_t write(const int fd, const void *data,
                const std::size_t size) const override {
    if (partial_write_failure() && write_calls++ == 0) {
      return ::write(fd, data, std::min<std::size_t>(size, 7));
    }
    if (partial_write_failure()) {
      errno = ENOSPC;
      return -1;
    }
    return ::write(fd, data, size);
  }

  ssize_t read_at(const int fd, void *data, const std::size_t size,
                  const off_t offset) const override {
    return ::pread(fd, data, size, offset);
  }

  int synchronize(const int fd) const override {
    const auto call = sync_calls++;
    if ((failure == Failure::FirstSync && call == 0) ||
        (failure == Failure::RollbackSync && call == 0)) {
      errno = EIO;
      return -1;
    }
    return ::fdatasync(fd);
  }

  int truncate(const int fd, const off_t size) const override {
    if (failure == Failure::Truncate) {
      errno = EIO;
      return -1;
    }
    return ::ftruncate(fd, size);
  }

  int close(const int fd) const override {
    const int result = ::close(fd);
    if (failure == Failure::Close && close_calls++ == 0) {
      errno = EIO;
      return -1;
    }
    return result;
  }

  void clear_failure() {
    failure = Failure::None;
    write_calls = 0;
    sync_calls = 0;
    unlock_calls = 0;
    close_calls = 0;
  }

  Failure failure;

private:
  [[nodiscard]] bool partial_write_failure() const {
    return failure == Failure::PartialWrite || failure == Failure::Truncate ||
           failure == Failure::RollbackSync;
  }

  mutable unsigned write_calls{};
  mutable unsigned sync_calls{};
  mutable unsigned unlock_calls{};
  mutable unsigned close_calls{};
};

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}

void write_file(const std::filesystem::path &path, const std::string &bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << bytes;
  output.close();
  gw::test::require(output.good(), "write scene manifest test fixture");
}

gw::compositor::PreparedSceneManifest prepared_record() {
  gw::compositor::PreparedSceneManifest prepared;
  prepared.json =
      "{\"commit_id\":41,\"generation\":17,\"scene_hash\":\"fixture\"}\n";
  prepared.active = true;
  return prepared;
}

void require_contains(const std::string &value, const std::string_view expected,
                      const std::string_view context) {
  gw::test::require(value.find(expected) != std::string::npos, context);
}

void partial_write_rolls_back(const std::filesystem::path &root) {
  const auto path = root / "partial.jsonl";
  const std::string original = "original\n";
  write_file(path, original);
  auto io = std::make_shared<FakeIo>(Failure::PartialWrite);
  gw::compositor::SceneManifest manifest(path, io);
  auto prepared = prepared_record();
  const auto record = prepared.json;
  std::string error;
  gw::test::require(!manifest.publish(prepared, error),
                    "partial append fails publication");
  require_contains(error, "scene manifest append write failed",
                   "partial append preserves the primary write stage");
  gw::test::require(read_file(path) == original && prepared.active &&
                        !prepared.publication_uncertain,
                    "partial append restores exact original bytes");
  io->clear_failure();
  gw::test::require(manifest.publish(prepared, error) &&
                        read_file(path) == original + record &&
                        !prepared.active,
                    "rolled-back prepared record retries exactly once");
}

void synchronization_rolls_back(const std::filesystem::path &root) {
  const auto path = root / "sync.jsonl";
  const std::string original = "stable\n";
  write_file(path, original);
  auto io = std::make_shared<FakeIo>(Failure::FirstSync);
  gw::compositor::SceneManifest manifest(path, io);
  auto prepared = prepared_record();
  const auto record = prepared.json;
  std::string error;
  gw::test::require(!manifest.publish(prepared, error),
                    "first synchronization failure rejects publication");
  require_contains(error, "scene manifest append synchronization failed",
                   "synchronization remains the primary failure");
  gw::test::require(read_file(path) == original && prepared.active &&
                        !prepared.publication_uncertain,
                    "synchronization failure rolls back exact bytes");
  io->clear_failure();
  gw::test::require(manifest.publish(prepared, error) &&
                        read_file(path) == original + record,
                    "synchronization rollback remains retryable");
}

void rollback_diagnostics(const std::filesystem::path &root) {
  {
    const auto path = root / "truncate.jsonl";
    write_file(path, "before\n");
    auto io = std::make_shared<FakeIo>(Failure::Truncate);
    gw::compositor::SceneManifest manifest(path, io);
    auto prepared = prepared_record();
    std::string error;
    gw::test::require(!manifest.publish(prepared, error),
                      "truncate failure rejects publication");
    require_contains(error, "scene manifest append write failed",
                     "truncate failure preserves the primary write error");
    require_contains(error, "scene manifest rollback truncate failed",
                     "truncate failure is appended as rollback context");
    gw::test::require(prepared.active && prepared.publication_uncertain,
                      "failed truncate retains uncertain prepared state");
  }
  {
    const auto path = root / "rollback-sync.jsonl";
    const std::string original = "before\n";
    write_file(path, original);
    auto io = std::make_shared<FakeIo>(Failure::RollbackSync);
    gw::compositor::SceneManifest manifest(path, io);
    auto prepared = prepared_record();
    const auto record = prepared.json;
    std::string error;
    gw::test::require(!manifest.publish(prepared, error),
                      "rollback synchronization failure rejects publication");
    require_contains(error, "scene manifest append write failed",
                     "rollback sync failure preserves the primary write error");
    require_contains(error, "scene manifest rollback synchronization failed",
                     "rollback sync failure is appended as context");
    gw::test::require(
        read_file(path) == original && prepared.active &&
            prepared.publication_uncertain,
        "failed rollback sync retains exact visible bytes and uncertainty");
    io->clear_failure();
    gw::test::require(
        manifest.publish(prepared, error) &&
            read_file(path) == original + record,
        "exact-size uncertain rollback retries without duplication");
  }
}

void cleanup_failures_are_retryable(const std::filesystem::path &root) {
  {
    const auto path = root / "unlock.jsonl";
    const std::string original = "locked\n";
    write_file(path, original);
    auto io = std::make_shared<FakeIo>(Failure::Unlock);
    gw::compositor::SceneManifest manifest(path, io);
    auto prepared = prepared_record();
    const auto record = prepared.json;
    std::string error;
    gw::test::require(!manifest.publish(prepared, error),
                      "unlock failure rejects publication");
    require_contains(error, "scene manifest unlock failed",
                     "unlock failure remains the primary diagnostic");
    gw::test::require(read_file(path) == original && prepared.active,
                      "unlock failure rolls back while ownership is retained");
    io->clear_failure();
    gw::test::require(manifest.publish(prepared, error) &&
                          read_file(path) == original + record,
                      "unlock failure retry appends exactly once");
  }
  {
    const auto path = root / "close.jsonl";
    const std::string original = "closed\n";
    write_file(path, original);
    auto io = std::make_shared<FakeIo>(Failure::Close);
    gw::compositor::SceneManifest manifest(path, io);
    auto prepared = prepared_record();
    const auto record = prepared.json;
    std::string error;
    gw::test::require(!manifest.publish(prepared, error),
                      "close failure is reported");
    require_contains(error, "scene manifest close failed",
                     "close failure retains its exact stage");
    gw::test::require(read_file(path) == original + record && prepared.active &&
                          prepared.publication_uncertain,
                      "close failure records the uncertain durable append");
    io->clear_failure();
    gw::test::require(
        manifest.publish(prepared, error) &&
            read_file(path) == original + record && !prepared.active,
        "close retry reconciles the record without duplicating it");
  }
}

void operation_stages_are_stable(const std::filesystem::path &root) {
  for (const auto [failure, expected, name] : {
           std::tuple{Failure::Open, "scene manifest open failed", "open"},
           std::tuple{Failure::Stat, "scene manifest stat failed", "stat"},
           std::tuple{Failure::Lock, "scene manifest lock failed", "lock"},
       }) {
    const auto path = root / (std::string(name) + ".jsonl");
    auto io = std::make_shared<FakeIo>(failure);
    gw::compositor::SceneManifest manifest(path, io);
    auto prepared = prepared_record();
    std::string error;
    gw::test::require(!manifest.publish(prepared, error) && prepared.active,
                      "injected operation failure preserves prepared record");
    require_contains(error, expected,
                     "injected operation identifies its stage");
  }
}

void target_validation(const std::filesystem::path &root) {
  {
    const auto path = root / "fifo-target";
    gw::test::require(::mkfifo(path.c_str(), 0600) == 0,
                      "create non-regular manifest target");
    gw::compositor::SceneManifest manifest(path);
    auto prepared = prepared_record();
    std::string error;
    gw::test::require(!manifest.publish(prepared, error) && prepared.active,
                      "non-regular manifest target is rejected");
    require_contains(error, "target is not a regular file",
                     "non-regular target rejection is explicit");
  }
  {
    const auto real = root / "real.jsonl";
    const auto link = root / "target-link.jsonl";
    write_file(real, "real\n");
    std::filesystem::create_symlink(real, link);
    gw::compositor::SceneManifest manifest(link);
    auto prepared = prepared_record();
    std::string error;
    gw::test::require(!manifest.publish(prepared, error) && prepared.active,
                      "symlink manifest target is rejected");
    require_contains(error, "scene manifest open failed",
                     "symlink target rejection reports the open boundary");
  }
  {
    const auto real_parent = root / "real-parent";
    const auto link_parent = root / "link-parent";
    std::filesystem::create_directory(real_parent);
    std::filesystem::create_directory_symlink(real_parent, link_parent);
    gw::compositor::SceneManifest manifest(link_parent / "scene.jsonl");
    auto prepared = prepared_record();
    std::string error;
    gw::test::require(!manifest.publish(prepared, error) && prepared.active,
                      "symlink manifest parent is rejected");
    require_contains(error, "parent must be a real directory",
                     "symlink parent rejection is explicit");
  }
  {
    const auto path = root / "permissions.jsonl";
    gw::compositor::SceneManifest manifest(path);
    auto prepared = prepared_record();
    std::string error;
    gw::test::require(manifest.publish(prepared, error),
                      "regular manifest target publishes");
    struct stat status{};
    gw::test::require(::stat(path.c_str(), &status) == 0 &&
                          (status.st_mode & 0777U) == 0600U,
                      "new manifest retains private permissions");
  }
}

} // namespace

int main() {
  char temporary[] = "/tmp/glasswyrm-scene-manifest-rollback-XXXXXX";
  gw::test::require(::mkdtemp(temporary) != nullptr,
                    "create scene manifest rollback directory");
  const std::filesystem::path root = temporary;
  partial_write_rolls_back(root);
  synchronization_rolls_back(root);
  rollback_diagnostics(root);
  cleanup_failures_are_retryable(root);
  operation_stages_are_stable(root);
  target_validation(root);
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return 0;
}
