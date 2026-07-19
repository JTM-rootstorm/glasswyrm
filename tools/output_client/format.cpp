#include "output_client/output_client.hpp"

#include <iomanip>
#include <sstream>

namespace glasswyrm::tools::output_client {
namespace {

std::string output_id(const std::uint64_t value) {
  std::ostringstream stream;
  stream << std::hex << std::nouppercase << std::setfill('0') << std::setw(16)
         << value;
  return stream.str();
}

std::string json_string(const std::string_view value) {
  std::ostringstream stream;
  stream << '"';
  for (const auto character : value) {
    switch (character) {
    case '"': stream << "\\\""; break;
    case '\\': stream << "\\\\"; break;
    case '\b': stream << "\\b"; break;
    case '\f': stream << "\\f"; break;
    case '\n': stream << "\\n"; break;
    case '\r': stream << "\\r"; break;
    case '\t': stream << "\\t"; break;
    default:
      if (static_cast<unsigned char>(character) < 0x20)
        stream << "\\u" << std::hex << std::setfill('0') << std::setw(4)
               << static_cast<unsigned>(static_cast<unsigned char>(character));
      else
        stream << character;
    }
  }
  stream << '"';
  return stream.str();
}

const char *kind_name(const gwipc_output_kind value) {
  return value == GWIPC_OUTPUT_DRM ? "drm" : "headless";
}

const char *boolean(const bool value) { return value ? "true" : "false"; }

const OutputDescriptor *descriptor(const Snapshot &snapshot,
                                   const std::uint64_t id) {
  const auto found = snapshot.descriptors.find(id);
  return found == snapshot.descriptors.end() ? nullptr : &found->second;
}

void print_modes_json(const Snapshot &snapshot, const std::uint64_t id,
                      std::ostream &output) {
  output << '[';
  bool first = true;
  for (const auto &mode : snapshot.modes) {
    if (mode.output_id != id)
      continue;
    if (!first) output << ',';
    first = false;
    output << "{\"id\":\"" << output_id(mode.id) << "\",\"width\":"
           << mode.width << ",\"height\":" << mode.height
           << ",\"refresh_millihertz\":" << mode.refresh_millihertz
           << ",\"preferred\":" << boolean(mode.preferred)
           << ",\"current\":" << boolean(mode.current) << '}';
  }
  output << ']';
}

void print_outputs_json_array(const Snapshot &snapshot, std::ostream &output) {
  output << '[';
  bool first = true;
  for (const auto &[id, state] : snapshot.outputs) {
    if (!first) output << ',';
    first = false;
    const auto *metadata = descriptor(snapshot, id);
    output << "{\"id\":\"" << output_id(id) << "\",\"name\":"
           << json_string(metadata ? metadata->name : "") << ",\"kind\":"
           << json_string(metadata ? kind_name(metadata->kind) : "unknown")
           << ",\"enabled\":" << boolean(state.enabled)
           << ",\"connected\":"
           << boolean(metadata && (metadata->capabilities &
                                   GWIPC_OUTPUT_CAP_CONNECTED) != 0)
           << ",\"primary\":" << boolean(id == snapshot.primary_output_id)
           << ",\"physical_width\":" << state.physical_width
           << ",\"physical_height\":" << state.physical_height
           << ",\"physical_width_mm\":"
           << (metadata ? metadata->physical_width_mm : 0)
           << ",\"physical_height_mm\":"
           << (metadata ? metadata->physical_height_mm : 0)
           << ",\"refresh_millihertz\":" << state.refresh_millihertz
           << ",\"logical_x\":" << state.logical_x
           << ",\"logical_y\":" << state.logical_y
           << ",\"logical_width\":" << state.logical_width
           << ",\"logical_height\":" << state.logical_height
           << ",\"scale_numerator\":" << state.scale_numerator
           << ",\"scale_denominator\":" << state.scale_denominator
           << ",\"transform\":" << json_string(transform_name(state.transform))
           << ",\"capabilities\":"
           << (metadata ? metadata->capabilities : 0) << ",\"modes\":";
    print_modes_json(snapshot, id, output);
    if (snapshot.vrr_queried)
      append_vrr_output_json(snapshot, id, output);
    output << '}';
  }
  output << ']';
}

void print_windows_json_array(const Snapshot &snapshot, std::ostream &output) {
  output << '[';
  bool first_window = true;
  for (const auto &[id, window] : snapshot.windows) {
    if (!first_window) output << ',';
    first_window = false;
    output << "{\"window_id\":" << id << ",\"logical_x\":" << window.x
           << ",\"logical_y\":" << window.y << ",\"logical_width\":"
           << window.width << ",\"logical_height\":" << window.height
           << ",\"primary_output_id\":\"" << output_id(window.primary_output_id)
           << "\",\"output_ids\":[";
    for (std::size_t index = 0; index < window.output_ids.size(); ++index) {
      if (index != 0) output << ',';
      output << '"' << output_id(window.output_ids[index]) << '"';
    }
    output << "],\"preferred_scale_numerator\":"
           << window.preferred_scale_numerator
           << ",\"preferred_scale_denominator\":"
           << window.preferred_scale_denominator
           << ",\"client_buffer_scale\":" << window.client_buffer_scale
           << ",\"scale_mode\":"
           << json_string(window.scale_mode == GWIPC_SURFACE_SCALE_SCALED_PIXMAP
                              ? "scaled-pixmap" : "legacy")
           << ",\"visible\":" << boolean(window.visible)
           << ",\"focused\":" << boolean(window.focused)
           << ",\"fullscreen\":" << boolean(window.fullscreen);
    if (snapshot.vrr_queried)
      append_vrr_window_json(snapshot, id, output);
    output << '}';
  }
  output << ']';
}

void print_header_text(const Snapshot &snapshot, std::ostream &output) {
  output << "layout generation=" << snapshot.generation << " root="
         << snapshot.root_width << 'x' << snapshot.root_height << " primary="
         << output_id(snapshot.primary_output_id) << '\n';
}

void print_outputs_text(const Snapshot &snapshot, std::ostream &output) {
  for (const auto &[id, state] : snapshot.outputs) {
    const auto *metadata = descriptor(snapshot, id);
    output << "output " << output_id(id) << " name="
           << (metadata ? metadata->name : "unknown") << " kind="
           << (metadata ? kind_name(metadata->kind) : "unknown")
           << " enabled=" << boolean(state.enabled) << " connected="
           << boolean(metadata && (metadata->capabilities &
                                   GWIPC_OUTPUT_CAP_CONNECTED) != 0)
           << " primary=" << boolean(id == snapshot.primary_output_id)
           << " physical=" << state.physical_width << 'x'
           << state.physical_height << '@' << state.refresh_millihertz
           << " logical=" << state.logical_x << ',' << state.logical_y << ' '
           << state.logical_width << 'x' << state.logical_height << " scale="
           << state.scale_numerator << '/' << state.scale_denominator
           << " transform=" << transform_name(state.transform)
           << " capabilities=" << (metadata ? metadata->capabilities : 0);
    if (snapshot.vrr_queried)
      append_vrr_output_text(snapshot, id, output);
    output << '\n';
    for (const auto &mode : snapshot.modes)
      if (mode.output_id == id)
        output << "  mode " << output_id(mode.id) << ' ' << mode.width << 'x'
               << mode.height << '@' << mode.refresh_millihertz
               << " preferred=" << boolean(mode.preferred)
               << " current=" << boolean(mode.current) << '\n';
  }
}

void print_windows_text(const Snapshot &snapshot, std::ostream &output) {
  for (const auto &[id, window] : snapshot.windows) {
    output << "window " << id << " logical=" << window.x << ',' << window.y
           << ' ' << window.width << 'x' << window.height << " primary="
           << output_id(window.primary_output_id) << " outputs=";
    for (std::size_t index = 0; index < window.output_ids.size(); ++index) {
      if (index != 0) output << ',';
      output << output_id(window.output_ids[index]);
    }
    output << " preferred-scale=" << window.preferred_scale_numerator << '/'
           << window.preferred_scale_denominator << " client-buffer-scale="
           << window.client_buffer_scale << " mode="
           << (window.scale_mode == GWIPC_SURFACE_SCALE_SCALED_PIXMAP
                   ? "scaled-pixmap" : "legacy")
           << " visible=" << boolean(window.visible)
           << " focused=" << boolean(window.focused)
           << " fullscreen=" << boolean(window.fullscreen);
    if (snapshot.vrr_queried)
      append_vrr_window_text(snapshot, id, output);
    output << '\n';
  }
}

} // namespace

void print_outputs(const Snapshot &snapshot, const bool json,
                   std::ostream &output) {
  if (json) {
    output << "{\"layout_generation\":" << snapshot.generation
           << ",\"root_width\":" << snapshot.root_width
           << ",\"root_height\":" << snapshot.root_height
           << ",\"primary_output_id\":\""
           << output_id(snapshot.primary_output_id) << "\",\"outputs\":";
    print_outputs_json_array(snapshot, output);
    output << "}\n";
  } else {
    print_header_text(snapshot, output);
    print_outputs_text(snapshot, output);
  }
}

void print_windows(const Snapshot &snapshot, const bool json,
                   std::ostream &output) {
  if (json) {
    output << "{\"layout_generation\":" << snapshot.generation
           << ",\"windows\":";
    print_windows_json_array(snapshot, output);
    output << "}\n";
  } else {
    print_header_text(snapshot, output);
    print_windows_text(snapshot, output);
  }
}

void print_all(const Snapshot &snapshot, const bool json,
               std::ostream &output) {
  if (json) {
    output << "{\"layout_generation\":" << snapshot.generation
           << ",\"root_width\":" << snapshot.root_width
           << ",\"root_height\":" << snapshot.root_height
           << ",\"primary_output_id\":\""
           << output_id(snapshot.primary_output_id) << "\",\"outputs\":";
    print_outputs_json_array(snapshot, output);
    output << ",\"windows\":";
    print_windows_json_array(snapshot, output);
    output << "}\n";
  } else {
    print_header_text(snapshot, output);
    print_outputs_text(snapshot, output);
    print_windows_text(snapshot, output);
  }
}

void print_acknowledgement(
    const gwipc_output_configuration_acknowledged &ack, const bool json,
    std::ostream &output) {
  if (json) {
    output << "{\"request_id\":" << ack.request_id
           << ",\"result\":" << static_cast<unsigned>(ack.result)
           << ",\"applied_generation\":" << ack.applied_generation
           << ",\"primary_output_id\":\""
           << output_id(ack.primary_output_id) << "\",\"root_width\":"
           << ack.root_logical_width << ",\"root_height\":"
           << ack.root_logical_height << ",\"enabled_output_count\":"
           << ack.enabled_output_count << "}\n";
  } else {
    output << "configuration result=" << static_cast<unsigned>(ack.result)
           << " generation=" << ack.applied_generation << " primary="
           << output_id(ack.primary_output_id) << " root="
           << ack.root_logical_width << 'x' << ack.root_logical_height
           << " enabled=" << ack.enabled_output_count << '\n';
  }
}

} // namespace glasswyrm::tools::output_client
