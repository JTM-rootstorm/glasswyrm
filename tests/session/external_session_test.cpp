#include "backends/session/external_session.hpp"
#include "tests/helpers/test_support.hpp"

#include <string>
#include <vector>

namespace {

class FakeFileDescriptorApi final
    : public glasswyrm::session::FileDescriptorApi {
public:
  int duplicate_close_on_exec(int fd) override {
    duplicated_from.push_back(fd);
    return fail_duplicate ? -1 : duplicate;
  }
  void close_fd(int fd) noexcept override { closed.push_back(fd); }
  std::string last_error() const override { return "fake duplicate failure"; }

  int duplicate{27};
  bool fail_duplicate{};
  std::vector<int> duplicated_from;
  std::vector<int> closed;
};

} // namespace

int main() {
  FakeFileDescriptorApi api;
  glasswyrm::session::ExternalDeviceSession session(api);
  std::string error;
  gw::test::require(session.adopt(5, error), "duplicate external descriptor");
  gw::test::require(session.device_fd() == 27 &&
                        api.duplicated_from == std::vector<int>{5} &&
                        !session.owns_virtual_terminal(),
                    "external session owns duplicate but not VT policy");
  gw::test::require(!session.adopt(6, error), "reject second adoption");
  session.reset();
  gw::test::require(api.closed == std::vector<int>{27},
                    "close only duplicated descriptor");

  FakeFileDescriptorApi failing_api;
  failing_api.fail_duplicate = true;
  glasswyrm::session::ExternalDeviceSession failing(failing_api);
  gw::test::require(!failing.adopt(-1, error), "reject invalid external fd");
  gw::test::require(!failing.adopt(8, error) && error.find("fake") != std::string::npos,
                    "report duplicate failure");
  gw::test::require(failing_api.closed.empty(),
                    "failed duplication does not close caller fd");
  return 0;
}
