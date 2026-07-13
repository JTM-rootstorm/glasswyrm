#include "backends/headless/output.hpp"
#include "backends/output/software_frame.hpp"

#include "tests/helpers/test_support.hpp"

#include <cstdint>
#include <string>
#include <type_traits>

int main() {
  using glasswyrm::output::SoftwareFrame;
  static_assert(std::is_same_v<glasswyrm::headless::Output, SoftwareFrame>);

  SoftwareFrame frame;
  std::string error;
  gw::test::require(!frame.configure(0, 1, 1, error),
                    "zero output ID is rejected");
  gw::test::require(error == "headless output ID must be nonzero",
                    "legacy configuration error remains stable");
  gw::test::require(
      !frame.configure(1, SoftwareFrame::kMaximumWidth + 1U, 1, error),
      "oversized output is rejected");
  gw::test::require(frame.configure(26, 2, 2, error),
                    "software frame configures");
  gw::test::require(frame.enabled(), "software frame is enabled");
  gw::test::require(frame.pixels().size() == 4, "software frame pixel count");
  for (const auto pixel : frame.pixels()) {
    gw::test::require(pixel == SoftwareFrame::kClearPixel,
                      "software frame uses the canonical clear pixel");
  }

  const auto spec = frame.spec(60'000);
  gw::test::require(spec.output_id == 26 && spec.width == 2 &&
                        spec.height == 2 && spec.refresh_millihz == 60'000,
                    "software frame exports its output specification");

  frame.pixels()[0] = 0xff112233U;
  gw::test::require(!frame.configure(26, 0, 2, error),
                    "invalid reconfiguration is rejected");
  gw::test::require(frame.enabled() && frame.width() == 2 &&
                        frame.pixels()[0] == 0xff112233U,
                    "invalid reconfiguration preserves the committed frame");

  frame.disable();
  gw::test::require(!frame.enabled() && frame.id() == 0 &&
                        frame.pixels().empty(),
                    "software frame disables cleanly");
  return 0;
}
