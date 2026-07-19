#include "m13_scale_client_hold.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace gw::test::m13 {
namespace {

void fail(const std::string_view message) {
  throw std::runtime_error(std::string(message));
}

void write_all(const int descriptor, const std::string_view contents) {
  std::size_t offset = 0;
  while (offset < contents.size()) {
    const auto count =
        ::write(descriptor, contents.data() + offset, contents.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) fail("could not write evidence ready file");
    offset += static_cast<std::size_t>(count);
  }
}

void publish_ready_file(const std::filesystem::path& target,
                        const std::uint32_t window,
                        const EvidenceStage stage) {
  if (target.empty() || target.filename().empty() || target.filename() == "." ||
      target.filename() == "..")
    fail("ready file must name a new file");
  auto parent = target.parent_path();
  if (parent.empty()) parent = ".";
  struct stat parent_status {};
  if (::lstat(parent.c_str(), &parent_status) != 0 ||
      !S_ISDIR(parent_status.st_mode) || S_ISLNK(parent_status.st_mode))
    fail("ready file parent must be a real directory");

  const auto temporary =
      parent / ("." + target.filename().string() + ".tmp." +
                std::to_string(static_cast<long long>(::getpid())));
  const int descriptor =
      ::open(temporary.c_str(),
             O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (descriptor < 0) fail("could not create private evidence ready file");
  bool published = false;
  try {
    const auto state = stage == EvidenceStage::Initial
                           ? "scaled-pixmap-initial"
                           : "scaled-pixmap-moved";
    const auto contents = "{\"state\":\"" + std::string(state) +
        "\",\"window\":" +
        std::to_string(window) + "}\n";
    write_all(descriptor, contents);
    if (::fsync(descriptor) != 0 || ::close(descriptor) != 0)
      fail("could not finalize evidence ready file");
    if (::link(temporary.c_str(), target.c_str()) != 0)
      fail("could not atomically publish evidence ready file");
    published = true;
    (void)::unlink(temporary.c_str());
  } catch (...) {
    if (!published) (void)::close(descriptor);
    (void)::unlink(temporary.c_str());
    throw;
  }
}

}  // namespace

void hold_for_evidence(const EvidenceHold& hold, const std::uint32_t window,
                       const EvidenceStage stage) {
  if (!hold.valid()) fail("evidence hold must be between 1 and 60000 ms");
  publish_ready_file(hold.ready_file, window, stage);
  std::this_thread::sleep_for(std::chrono::milliseconds(hold.milliseconds));
}

void self_test_evidence_hold() {
  if (EvidenceHold{}.valid() || EvidenceHold{0, "/tmp/ready"}.valid() ||
      EvidenceHold{60'001, "/tmp/ready"}.valid() ||
      EvidenceHold{1, {}}.valid() || !EvidenceHold{60'000, "/tmp/ready"}.valid())
    fail("evidence hold bounds self-test failed");
  std::string pattern = "/tmp/glasswyrm-scale-hold-test-XXXXXX";
  if (::mkdtemp(pattern.data()) == nullptr)
    fail("could not create evidence hold self-test directory");
  const auto root = std::filesystem::path(pattern);
  const auto ready = root / "ready.json";
  try {
    hold_for_evidence({1, ready.string()}, UINT32_C(0x11223344),
                      EvidenceStage::Initial);
    struct stat status {};
    if (::lstat(ready.c_str(), &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_nlink != 1 || (status.st_mode & 0777U) != 0600U)
      fail("evidence ready file is not a private regular file");
    const int descriptor = ::open(ready.c_str(), O_RDONLY | O_CLOEXEC);
    std::array<char, 128> bytes{};
    const auto count = descriptor < 0 ? -1 : ::read(descriptor, bytes.data(),
                                                    bytes.size());
    if (descriptor >= 0) (void)::close(descriptor);
    const std::string_view contents(
        bytes.data(), count < 0 ? 0U : static_cast<std::size_t>(count));
    if (contents !=
        "{\"state\":\"scaled-pixmap-initial\",\"window\":287454020}\n")
      fail("evidence ready file contents are not deterministic");
    bool rejected_existing = false;
    try {
      hold_for_evidence({1, ready.string()}, 1, EvidenceStage::Moved);
    } catch (const std::runtime_error&) {
      rejected_existing = true;
    }
    if (!rejected_existing) fail("existing evidence ready file was replaced");
  } catch (...) {
    std::filesystem::remove_all(root);
    throw;
  }
  std::filesystem::remove_all(root);
}

}  // namespace gw::test::m13
