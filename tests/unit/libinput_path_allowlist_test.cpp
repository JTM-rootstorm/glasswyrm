#include "input/device_path_allowlist.hpp"
#include "tests/helpers/test_support.hpp"

#include <cerrno>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

int main() {
  using glasswyrm::input::DevicePathAllowlist;
  using gw::test::require;

  char allowed_template[] = "/tmp/glasswyrm-input-allowed-XXXXXX";
  char denied_template[] = "/tmp/glasswyrm-input-denied-XXXXXX";
  const int allowed_seed = ::mkstemp(allowed_template);
  const int denied_seed = ::mkstemp(denied_template);
  require(allowed_seed >= 0 && denied_seed >= 0, "temporary devices created");
  (void)::close(allowed_seed);
  (void)::close(denied_seed);

  const std::string alias = std::string(allowed_template) + "-alias";
  require(::symlink(allowed_template, alias.c_str()) == 0,
          "allowlist alias created");
  std::string error;
  const std::vector<std::string> configured{alias, allowed_template};
  auto access = DevicePathAllowlist::create(configured, error);
  require(access.has_value() && access->paths().size() == 1 && error.empty(),
          "configured paths canonicalize and deduplicate");

  const int fd = access->open_restricted(alias, O_RDWR);
  require(fd >= 0, "canonical allowlisted alias opens");
  const int status_flags = ::fcntl(fd, F_GETFL);
  const int descriptor_flags = ::fcntl(fd, F_GETFD);
  require((status_flags & O_ACCMODE) == O_RDONLY &&
              (status_flags & O_NONBLOCK) != 0 &&
              (descriptor_flags & FD_CLOEXEC) != 0,
          "open callback enforces read-only nonblocking close-on-exec");
  access->close_restricted(fd);
  require(access->open_restricted(denied_template, O_RDONLY) == -EACCES,
          "canonical path outside exact allowlist is rejected");
  require(access->open_restricted("/definitely/missing/input", O_RDONLY) < 0,
          "unresolvable library path is rejected");

  const std::vector<std::string> empty;
  require(!DevicePathAllowlist::create(empty, error).has_value(),
          "empty startup allowlist is rejected");
  (void)::unlink(alias.c_str());
  (void)::unlink(allowed_template);
  (void)::unlink(denied_template);
}
