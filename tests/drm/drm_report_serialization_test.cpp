#include "backends/drm/drm_report.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <iostream>
#include <string>

namespace {

using glasswyrm::drm::DiscoveryReport;
using glasswyrm::drm::DamageCopyReport;
using glasswyrm::drm::DrmReportRecord;
using glasswyrm::drm::FatalReport;
using glasswyrm::drm::FlipReport;
using glasswyrm::drm::ModesetReport;
using glasswyrm::drm::ReportApiPath;
using glasswyrm::drm::RestoreReport;
using glasswyrm::drm::SelectionReport;
using glasswyrm::drm::VtReport;
using glasswyrm::drm::VtTransition;

void require_record(const DrmReportRecord& record, const std::string& expected,
                    const std::string& label) {
  const auto actual = glasswyrm::drm::serialize_report_record(record);
  if (actual != expected + "\n")
    std::cerr << label << "\nexpected: " << expected << "\nactual: " << actual;
  gw::test::require(actual == expected + "\n", label);
}

} // namespace

int main() {
  require_record(
      DiscoveryReport{"/dev/dri/card0", "virtio_gpu", true, true, false},
      "{\"record\":\"discovery\",\"device\":\"/dev/dri/card0\","
      "\"driver\":\"virtio_gpu\",\"primary_node\":true,"
      "\"dumb_buffer\":true,\"atomic\":false}",
      "stable discovery JSON");

  SelectionReport selection;
  selection.connector_name = "Virtual-1";
  selection.connector_id = 31;
  selection.crtc_id = 42;
  selection.primary_plane_id = 7;
  selection.mode_name = "1024x768";
  selection.width = 1024;
  selection.height = 768;
  selection.refresh_millihz = 60000;
  selection.api = ReportApiPath::Legacy;
  selection.framebuffer_format = "XRGB8888";
  selection.pitches = {4096, 4096};
  selection.sizes = {3145728, 3145728};
  selection.vt_path = "/dev/tty2";
  selection.vt_owned = true;
  require_record(
      selection,
      "{\"record\":\"selection\",\"connector\":\"Virtual-1\","
      "\"connector_id\":31,\"crtc_id\":42,\"primary_plane_id\":7,"
      "\"mode\":\"1024x768\",\"width\":1024,\"height\":768,"
      "\"refresh_millihz\":60000,\"api\":\"legacy\","
      "\"dumb_buffer\":true,\"atomic\":false,"
      "\"framebuffer_format\":\"XRGB8888\",\"buffer_count\":2,"
      "\"pitches\":[4096,4096],\"sizes\":[3145728,3145728],"
      "\"vt_path\":\"/dev/tty2\",\"vt_owned\":true}",
      "stable selection JSON");

  require_record(
      ModesetReport{3, 9, 11, 0, 55, 0x12ab, 0x12ab,
                    ReportApiPath::Atomic},
      "{\"record\":\"modeset\",\"ordinal\":3,\"commit_id\":9,"
      "\"generation\":11,"
      "\"front_buffer\":0,\"framebuffer_id\":55,"
      "\"canonical_hash\":\"00000000000012ab\","
      "\"scanout_hash\":\"00000000000012ab\",\"api\":\"atomic\"}",
      "stable modeset JSON");
  require_record(
      FlipReport{3, 10, 12, 1, 56, 0x12ab, 0x12ab, 99,
                 ReportApiPath::Legacy},
      "{\"record\":\"flip\",\"ordinal\":3,\"commit_id\":10,"
      "\"generation\":12,\"front_buffer\":1,\"framebuffer_id\":56,"
      "\"canonical_hash\":\"00000000000012ab\","
      "\"scanout_hash\":\"00000000000012ab\","
      "\"page_flip_sequence\":99,\"api\":\"legacy\"}",
      "stable flip JSON");
  require_record(VtReport{VtTransition::Release, false, false, 0x12ab},
                 "{\"record\":\"vt\",\"transition\":\"release\","
                 "\"master_owned\":false,\"full_modeset\":false,"
                 "\"committed_hash\":\"00000000000012ab\"}",
                 "stable VT release JSON");
  require_record(VtReport{VtTransition::Acquire, true, true, 0x12ab},
                 "{\"record\":\"vt\",\"transition\":\"acquire\","
                 "\"master_owned\":true,\"full_modeset\":true,"
                 "\"committed_hash\":\"00000000000012ab\"}",
                 "stable VT acquire JSON");
  DamageCopyReport copy;
  copy.generation = 12;
  copy.buffer_index = 1;
  copy.framebuffer_id = 56;
  copy.full_frame_bytes = 128;
  copy.copied_bytes = 24;
  copy.history_span = 2;
  copy.cumulative_full_frame_bytes = 384;
  copy.cumulative_copied_bytes = 280;
  copy.rectangles = {{1, 2, 3, 2}};
  require_record(
      copy,
      "{\"record\":\"damage-copy\",\"generation\":12,\"buffer\":1,"
      "\"framebuffer_id\":56,\"full_frame_bytes\":128,"
      "\"copied_bytes\":24,\"copy_rectangles\":[{\"x\":1,\"y\":2,"
      "\"width\":3,\"height\":2}],\"history_span\":2,"
      "\"full_copy_reason\":\"none\","
      "\"cumulative_full_frame_bytes\":384,"
      "\"cumulative_copied_bytes\":280,"
      "\"cumulative_copy_ratio_ppm\":729166}",
      "stable damage-copy JSON");
  require_record(RestoreReport{true, false, true, true},
                 "{\"record\":\"restore\",\"kms\":true,\"vt\":false,"
                 "\"master_drop\":true,\"framebuffer_cleanup\":true}",
                 "stable restore JSON");
  require_record(
      FatalReport{"page_flip", "timeout\nunknown", "Virtual-1", 42, 56, 10,
                  12},
      "{\"record\":\"fatal\",\"stage\":\"page_flip\","
      "\"reason\":\"timeout\\nunknown\",\"connector\":\"Virtual-1\","
      "\"crtc_id\":42,\"framebuffer_id\":56,\"commit_id\":10,"
      "\"generation\":12}",
      "stable escaped fatal JSON");
  return 0;
}
