#include "tools/drm_probe.hpp"

#include "backends/drm/connector_name.hpp"
#include "backends/drm/connector_selector.hpp"
#include "backends/drm/device.hpp"
#include "backends/drm/mode_selector.hpp"
#include "backends/drm/pipeline_selector.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <set>
#include <string_view>
#include <tuple>
#include <vector>

namespace glasswyrm::tools {
namespace {

void usage(std::ostream& output) {
  output << "Usage: gw_drm_probe --device PATH|auto [--connector NAME]\n"
            "                    [--require-mode WIDTHxHEIGHT]\n"
            "                    [--list] [--snapshot-state]\n"
            "                    [--expect-active]\n"
            "                    [--expect-restored BASELINE.json]\n"
            "                    --output PATH\n";
}

bool take_value(int argc, char** argv, int& index, std::string& destination,
                const std::string_view option, std::ostream& error) {
  if (++index >= argc || argv[index][0] == '\0' ||
      std::string_view(argv[index]).starts_with("--")) {
    error << "gw_drm_probe: " << option << " requires a value\n";
    return false;
  }
  destination = argv[index];
  return true;
}

bool parse_dimension(const std::string_view text, std::uint32_t& value) {
  if (text.empty()) return false;
  const auto [end, parse_error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return parse_error == std::errc{} && end == text.data() + text.size() &&
         value > 0;
}

bool parse_mode(const std::string_view text, std::uint32_t& width,
                std::uint32_t& height) {
  const auto separator = text.find('x');
  return separator != std::string_view::npos && separator != 0 &&
         parse_dimension(text.substr(0, separator), width) &&
         parse_dimension(text.substr(separator + 1), height);
}

void json_string(std::ostream& output, const std::string_view value) {
  output << '"';
  for (const unsigned char byte : value) {
    switch (byte) {
      case '"': output << "\\\""; break;
      case '\\': output << "\\\\"; break;
      case '\b': output << "\\b"; break;
      case '\f': output << "\\f"; break;
      case '\n': output << "\\n"; break;
      case '\r': output << "\\r"; break;
      case '\t': output << "\\t"; break;
      default:
        if (byte < 0x20U) {
          output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<unsigned>(byte) << std::dec;
        } else {
          output << static_cast<char>(byte);
        }
    }
  }
  output << '"';
}

std::vector<std::string> candidate_paths(const DrmProbeOptions& options) {
  const auto& requested = options.device;
  if (requested != "auto") return {requested};
  std::vector<std::string> paths = options.auto_device_paths;
  if (!paths.empty()) {
    std::ranges::sort(paths);
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
  }
  std::error_code error;
  for (std::filesystem::directory_iterator entries("/dev/dri", error), end;
       !error && entries != end; entries.increment(error)) {
    const auto name = entries->path().filename().string();
    if (!name.starts_with("card") || name.size() == 4) continue;
    if (!std::all_of(name.begin() + 4, name.end(),
                     [](const unsigned char value) {
                       return value >= '0' && value <= '9';
                     }))
      continue;
    paths.push_back(entries->path().string());
  }
  std::ranges::sort(paths);
  return paths;
}

drm::DeviceSnapshot normalized_snapshot(drm::DeviceSnapshot snapshot) {
  for (auto& connector : snapshot.connectors) {
    std::ranges::sort(connector.modes, [](const drm::Mode& left,
                                         const drm::Mode& right) {
      return std::tie(left.width, left.height, left.refresh_millihz, left.name,
                      left.clock_khz, left.preferred) <
             std::tie(right.width, right.height, right.refresh_millihz,
                      right.name, right.clock_khz, right.preferred);
    });
  }
  std::ranges::sort(snapshot.connectors,
                    [](const drm::Connector& left,
                       const drm::Connector& right) {
    const auto left_name = drm::connector_name(left.type, left.type_id);
    const auto right_name = drm::connector_name(right.type, right.type_id);
    return std::tie(left_name, left.id) < std::tie(right_name, right.id);
  });
  for (auto& crtc : snapshot.crtcs)
    std::ranges::sort(crtc.connector_ids);
  std::ranges::sort(snapshot.crtcs, {}, &drm::Crtc::id);
  for (auto& plane : snapshot.planes)
    std::ranges::sort(plane.formats);
  std::ranges::sort(snapshot.planes, {}, &drm::Plane::id);
  return snapshot;
}

struct SelectedCandidate {
  std::size_t connector{};
  std::size_t mode{};
  std::optional<std::size_t> crtc;
  std::optional<std::size_t> plane;
};

std::optional<SelectedCandidate> select_candidate(
    const drm::DeviceSnapshot& snapshot, const DrmProbeOptions& options,
    std::string& error) {
  if (!options.required_width || !options.required_height) return std::nullopt;
  const auto connector = drm::select_connector(
      snapshot.connectors, snapshot.crtcs, *options.required_width,
      *options.required_height,
      options.connector
          ? std::optional<std::string_view>(*options.connector)
          : std::nullopt);
  if (connector.status != drm::ConnectorSelectionStatus::Success) {
    error = "no eligible connector exposes the required mode";
    return std::nullopt;
  }
  const auto& selected_connector = snapshot.connectors[connector.connector_index];
  const auto mode = drm::select_mode(
      selected_connector.modes,
      {*options.required_width, *options.required_height, 0, std::nullopt});
  if (mode.status != drm::ModeSelectionStatus::Success) {
    error = "required DRM mode could not be selected";
    return std::nullopt;
  }
  SelectedCandidate selected{connector.connector_index, mode.mode_index,
                             std::nullopt, std::nullopt};
  const auto crtc = drm::select_crtc(selected_connector, snapshot.crtcs);
  if (crtc.status != drm::CrtcSelectionStatus::Success) {
    error = "no compatible CRTC is available";
    return std::nullopt;
  }
  selected.crtc = crtc.crtc_index;
  if (!snapshot.planes.empty()) {
    const auto plane =
        drm::select_primary_plane(snapshot.crtcs[crtc.crtc_index],
                                  snapshot.planes);
    if (plane.status == drm::PlaneSelectionStatus::Success)
      selected.plane = plane.plane_index;
  }
  return selected;
}

void write_modes(std::ostream& output, const drm::Connector& connector) {
  output << '[';
  for (std::size_t index = 0; index < connector.modes.size(); ++index) {
    if (index != 0) output << ',';
    const auto& mode = connector.modes[index];
    output << "{\"name\":";
    json_string(output, mode.name);
    output << ",\"width\":" << mode.width << ",\"height\":"
           << mode.height << ",\"refresh_millihz\":"
           << mode.refresh_millihz << ",\"clock_khz\":" << mode.clock_khz
           << ",\"preferred\":" << (mode.preferred ? "true" : "false")
           << '}';
  }
  output << ']';
}

void write_current_mode(std::ostream& output, const drm::Crtc& crtc) {
  if (!crtc.active) {
    output << "null";
    return;
  }
  output << "{\"name\":";
  json_string(output, crtc.mode.name);
  output << ",\"width\":" << crtc.mode.width << ",\"height\":"
         << crtc.mode.height << ",\"refresh_millihz\":"
         << crtc.mode.refresh_millihz << ",\"clock_khz\":"
         << crtc.mode.clock_khz << '}';
}

std::string snapshot_json(const drm::DeviceSnapshot& snapshot,
                          const std::optional<SelectedCandidate>& selected) {
  std::ostringstream output;
  output << "{\n  \"device\":{\"path\":";
  json_string(output, snapshot.canonical_path);
  output << ",\"major\":" << snapshot.device_major << ",\"minor\":"
         << snapshot.device_minor << "},\n  \"driver\":{\"name\":";
  json_string(output, snapshot.driver.name);
  output << ",\"version\":";
  json_string(output, std::to_string(snapshot.driver.major) + "." +
                          std::to_string(snapshot.driver.minor) + "." +
                          std::to_string(snapshot.driver.patchlevel));
  output << ",\"date\":";
  json_string(output, snapshot.driver.date);
  output << ",\"description\":";
  json_string(output, snapshot.driver.description);
  output << ",\"bus_info\":";
  json_string(output, snapshot.driver.bus_info);
  output << "},\n  \"capabilities\":{\"dumb_buffer\":"
         << (snapshot.dumb_buffer ? "true" : "false")
         << ",\"universal_planes\":"
         << (snapshot.universal_planes ? "true" : "false")
         << ",\"atomic\":" << (snapshot.atomic ? "true" : "false")
         << "},\n  \"connectors\":[";
  for (std::size_t index = 0; index < snapshot.connectors.size(); ++index) {
    if (index != 0) output << ',';
    const auto& connector = snapshot.connectors[index];
    output << "{\"id\":" << connector.id << ",\"name\":";
    json_string(output,
                drm::connector_name(connector.type, connector.type_id));
    output << ",\"status\":";
    json_string(output, drm::connection_status_name(connector.status));
    output << ",\"non_desktop\":"
           << (connector.non_desktop ? "true" : "false")
           << ",\"possible_crtc_mask\":" << connector.possible_crtc_mask
           << ",\"current_crtc_id\":" << connector.current_crtc_id
           << ",\"modes\":";
    write_modes(output, connector);
    output << '}';
  }
  output << "],\n  \"crtcs\":[";
  for (std::size_t index = 0; index < snapshot.crtcs.size(); ++index) {
    if (index != 0) output << ',';
    const auto& crtc = snapshot.crtcs[index];
    output << "{\"id\":" << crtc.id << ",\"index\":" << crtc.index
           << ",\"current_mode\":";
    write_current_mode(output, crtc);
    output << ",\"current_framebuffer_id\":" << crtc.framebuffer_id
           << ",\"x\":" << crtc.x << ",\"y\":" << crtc.y
           << ",\"active\":" << (crtc.active ? "true" : "false")
           << ",\"connector_ids\":[";
    for (std::size_t route = 0; route < crtc.connector_ids.size(); ++route) {
      if (route != 0) output << ',';
      output << crtc.connector_ids[route];
    }
    output << "]}";
  }
  output << "],\n  \"planes\":[";
  for (std::size_t index = 0; index < snapshot.planes.size(); ++index) {
    if (index != 0) output << ',';
    const auto& plane = snapshot.planes[index];
    output << "{\"id\":" << plane.id << ",\"type\":";
    switch (plane.type) {
      case drm::PlaneType::Primary: json_string(output, "primary"); break;
      case drm::PlaneType::Cursor: json_string(output, "cursor"); break;
      case drm::PlaneType::Overlay: json_string(output, "overlay"); break;
      case drm::PlaneType::Unknown: json_string(output, "unknown"); break;
    }
    output << ",\"possible_crtc_mask\":" << plane.possible_crtc_mask
           << ",\"current_crtc_id\":" << plane.current_crtc_id
           << ",\"framebuffer_id\":" << plane.framebuffer_id
           << ",\"crtc_x\":" << plane.crtc_x
           << ",\"crtc_y\":" << plane.crtc_y
           << ",\"crtc_width\":" << plane.crtc_width
           << ",\"crtc_height\":" << plane.crtc_height
           << ",\"source_x\":" << plane.source_x
           << ",\"source_y\":" << plane.source_y
           << ",\"source_width\":" << plane.source_width
           << ",\"source_height\":" << plane.source_height
           << ",\"formats\":[";
    for (std::size_t format = 0; format < plane.formats.size(); ++format) {
      if (format != 0) output << ',';
      output << plane.formats[format];
    }
    output << "]}";
  }
  output << "],\n  \"selected_candidate\":";
  if (!selected) {
    output << "null";
  } else {
    const auto& connector = snapshot.connectors[selected->connector];
    const auto& mode = connector.modes[selected->mode];
    output << "{\"connector\":";
    json_string(output, drm::connector_name(connector.type, connector.type_id));
    output << ",\"connector_id\":" << connector.id << ",\"mode\":";
    json_string(output, mode.name);
    output << ",\"width\":" << mode.width << ",\"height\":" << mode.height
           << ",\"refresh_millihz\":" << mode.refresh_millihz;
    output << ",\"crtc_id\":";
    if (selected->crtc) output << snapshot.crtcs[*selected->crtc].id;
    else output << "null";
    output << ",\"plane_id\":";
    if (selected->plane) output << snapshot.planes[*selected->plane].id;
    else output << "null";
    output << '}';
  }
  output << "\n}\n";
  return output.str();
}

bool read_file(const std::string& path, std::string& contents) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return false;
  contents.assign(std::istreambuf_iterator<char>(input),
                  std::istreambuf_iterator<char>());
  return input.good() || input.eof();
}

bool write_file(const std::string& path, const std::string_view contents) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  return output.good();
}

bool device_qualifies(const drm::DeviceSnapshot& snapshot,
                      const DrmProbeOptions& options) {
  if (options.required_width && options.required_height) {
    std::string ignored;
    return select_candidate(snapshot, options, ignored).has_value();
  }
  return std::ranges::any_of(snapshot.connectors,
                             [&](const drm::Connector& connector) {
    if (options.connector &&
        drm::connector_name(connector.type, connector.type_id) !=
            *options.connector)
      return false;
    if (connector.status != drm::ConnectionStatus::Connected ||
        connector.modes.empty() || connector.non_desktop ||
        connector.type ==
            static_cast<std::uint32_t>(drm::ConnectorType::Writeback))
      return false;
    return std::ranges::any_of(snapshot.crtcs, [&](const drm::Crtc& crtc) {
      return crtc.index < 32 &&
             (connector.possible_crtc_mask & (1U << crtc.index)) != 0;
    });
  });
}

bool connector_route_is_active(const drm::DeviceSnapshot& snapshot,
                               const drm::Connector& connector) {
  if (connector.current_crtc_id == 0) return false;
  return std::ranges::any_of(snapshot.crtcs, [&](const drm::Crtc& crtc) {
    return crtc.id == connector.current_crtc_id &&
           std::ranges::find(crtc.connector_ids, connector.id) !=
               crtc.connector_ids.end();
  });
}

}  // namespace

DrmProbeParseResult parse_drm_probe_options(
    const int argc, char** argv, DrmProbeOptions& options,
    std::ostream& output, std::ostream& error) {
  options = {};
  std::set<std::string> seen;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--help") {
      usage(output);
      return DrmProbeParseResult::ExitSuccess;
    }
    if (!seen.insert(std::string(argument)).second) {
      error << "gw_drm_probe: duplicate option: " << argument << '\n';
      return DrmProbeParseResult::ExitFailure;
    }
    if (argument == "--device") {
      if (!take_value(argc, argv, index, options.device, argument, error))
        return DrmProbeParseResult::ExitFailure;
    } else if (argument == "--connector") {
      std::string value;
      if (!take_value(argc, argv, index, value, argument, error))
        return DrmProbeParseResult::ExitFailure;
      options.connector = std::move(value);
    } else if (argument == "--require-mode") {
      if (++index >= argc ||
          !parse_mode(argv[index], options.required_width.emplace(),
                      options.required_height.emplace())) {
        error << "gw_drm_probe: --require-mode requires WIDTHxHEIGHT\n";
        return DrmProbeParseResult::ExitFailure;
      }
    } else if (argument == "--output") {
      if (!take_value(argc, argv, index, options.output_path, argument, error))
        return DrmProbeParseResult::ExitFailure;
    } else if (argument == "--list") {
      options.list = true;
    } else if (argument == "--snapshot-state") {
      options.snapshot_state = true;
    } else if (argument == "--expect-active") {
      options.expect_active = true;
    } else if (argument == "--expect-restored") {
      std::string path;
      if (!take_value(argc, argv, index, path, argument, error))
        return DrmProbeParseResult::ExitFailure;
      options.expected_restored_path = std::move(path);
    } else {
      error << "gw_drm_probe: unknown option: " << argument << '\n';
      usage(error);
      return DrmProbeParseResult::ExitFailure;
    }
  }
  if (options.output_path.empty()) {
    error << "gw_drm_probe: --output is required\n";
    return DrmProbeParseResult::ExitFailure;
  }
  if (options.required_width.has_value() != options.required_height.has_value()) {
    error << "gw_drm_probe: incomplete required mode\n";
    return DrmProbeParseResult::ExitFailure;
  }
  const bool state_validation = options.snapshot_state || options.expect_active ||
                                options.expected_restored_path.has_value();
  if (state_validation &&
      (options.device == "auto" || !options.connector ||
       !options.required_width)) {
    error << "gw_drm_probe: state validation requires explicit --device, "
             "--connector, and --require-mode\n";
    return DrmProbeParseResult::ExitFailure;
  }
  return DrmProbeParseResult::Run;
}

int run_drm_probe(drm::DrmApi& api, const DrmProbeOptions& options,
                  std::ostream& error) {
  auto paths = candidate_paths(options);
  if (paths.empty()) {
    error << "gw_drm_probe: no DRM primary-node candidates found\n";
    return 1;
  }
  drm::DeviceDiscovery discovery;
  std::string selected_path;
  if (options.device == "auto") {
    std::vector<std::string> eligible;
    for (const auto& path : paths) {
      auto candidate = drm::Device::open(api, path, {}, discovery);
      if (!candidate) continue;
      const auto snapshot = normalized_snapshot(candidate->snapshot());
      if (device_qualifies(snapshot, options)) eligible.push_back(path);
    }
    if (eligible.empty()) {
      error << "gw_drm_probe: no DRM device has an eligible connector"
            << (discovery.error.empty() ? "" : ": " + discovery.error) << '\n';
      return 1;
    }
    if (eligible.size() != 1) {
      error << "gw_drm_probe: automatic DRM device selection is ambiguous\n";
      return 1;
    }
    selected_path = eligible.front();
  } else {
    selected_path = paths.front();
  }
  auto device = drm::Device::open(api, selected_path, {}, discovery);
  if (!device) {
    error << "gw_drm_probe: unable to open a usable DRM device: "
          << discovery.error << '\n';
    return 1;
  }

  const auto snapshot = normalized_snapshot(device->snapshot());

  std::string selection_error;
  const auto selected = select_candidate(snapshot, options, selection_error);
  if (options.required_width && !selected) {
    error << "gw_drm_probe: " << selection_error << '\n';
    return 1;
  }
  if (options.connector && !options.required_width) {
    const bool found = std::ranges::any_of(
        snapshot.connectors, [&](const drm::Connector& connector) {
          return drm::connector_name(connector.type, connector.type_id) ==
                 *options.connector;
        });
    if (!found) {
      error << "gw_drm_probe: requested connector was not found\n";
      return 1;
    }
  }
  if (options.expect_active) {
    const bool active = selected
                            ? connector_route_is_active(
                                  snapshot,
                                  snapshot.connectors[selected->connector])
                            : std::ranges::any_of(
                                  snapshot.connectors,
                                  [&](const drm::Connector& connector) {
                                    return connector_route_is_active(snapshot,
                                                                     connector);
                                  });
    if (!active) {
      error << "gw_drm_probe: expected an active connector route\n";
      return 1;
    }
  }

  const auto json = snapshot_json(snapshot, selected);
  std::string baseline;
  if (options.expected_restored_path) {
    if (!read_file(*options.expected_restored_path, baseline)) {
      error << "gw_drm_probe: cannot read restoration baseline\n";
      return 1;
    }
  }
  if (!write_file(options.output_path, json)) {
    error << "gw_drm_probe: cannot write output snapshot\n";
    return 1;
  }
  if (options.expected_restored_path && baseline != json) {
    error << "gw_drm_probe: current KMS snapshot differs from baseline\n";
    return 1;
  }
  return 0;
}

}  // namespace glasswyrm::tools
