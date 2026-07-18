#include "glasswyrmd/options.hpp"

#include "config.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace {

using glasswyrm::server::InputDevicePath;
using glasswyrm::server::InputDevicePathResolver;
using glasswyrm::server::Options;
using glasswyrm::server::ParseOptionsResult;

bool fake_device(std::string_view path, InputDevicePath &resolved,
                 std::string &error) {
  if (path == "/bad") {
    error = "fixture rejected path";
    return false;
  }
  if (path == "/alias/keyboard" || path == "/dev/input/event0")
    resolved = {"/dev/input/event0", 1, 10, 100};
  else if (path == "/alias/pointer" || path == "/dev/input/event1")
    resolved = {"/dev/input/event1", 1, 11, 101};
  else
    resolved = {std::string(path), 1, 12, 102};
  error.clear();
  return true;
}

ParseOptionsResult parse(std::vector<std::string> values, Options &options,
                         std::string &output, std::string &error,
                         InputDevicePathResolver resolver =
                             glasswyrm::server::resolve_input_device_path) {
  std::vector<char *> arguments;
  arguments.reserve(values.size());
  for (auto &value : values)
    arguments.push_back(value.data());
  std::ostringstream output_stream;
  std::ostringstream error_stream;
  const auto result = glasswyrm::server::parse_options(
      static_cast<int>(arguments.size()), arguments.data(), options,
      output_stream, error_stream, resolver);
  output = output_stream.str();
  error = error_stream.str();
  return result;
}

} // namespace

int main() {
  std::string output;
  std::string error;
  Options options;
  if (parse({"glasswyrmd", "--display", "99", "--socket-dir", "/tmp/x",
             "--wm-socket", "/tmp/gwm.sock", "--compositor-socket",
             "/tmp/gwcomp.sock", "--software-content",
             "--synthetic-input-socket", "/tmp/input.sock"},
            options, output, error) != ParseOptionsResult::Run ||
      options.display != 99 || options.socket_dir != "/tmp/x" ||
      !options.integrated() || !options.software_content ||
      options.synthetic_input_socket != "/tmp/input.sock" ||
      *options.wm_socket != "/tmp/gwm.sock" ||
      *options.compositor_socket != "/tmp/gwcomp.sock") {
    return 1;
  }
  for (const auto &values : {
           std::vector<std::string>{"glasswyrmd", "--wm-socket", "/tmp/wm"},
           std::vector<std::string>{"glasswyrmd", "--compositor-socket",
                                    "/tmp/comp"},
           std::vector<std::string>{"glasswyrmd", "--wm-socket", "/tmp/a",
                                    "--wm-socket", "/tmp/b",
                                    "--compositor-socket", "/tmp/c"},
           std::vector<std::string>{"glasswyrmd", "--software-content"},
           std::vector<std::string>{"glasswyrmd", "--synthetic-input-socket",
                                    "/tmp/input.sock"},
           std::vector<std::string>{"glasswyrmd", "--wm-socket", "/tmp/a",
                                    "--compositor-socket", "/tmp/b",
                                    "--synthetic-input-socket", "/tmp/a",
                                    "--synthetic-input-socket", "/tmp/b"},
           std::vector<std::string>{"glasswyrmd", "--wm-socket", "/tmp/a",
                                    "--compositor-socket", "/tmp/b",
                                    "--software-content", "--software-content"},
       }) {
    options = {};
    if (parse(values, options, output, error) !=
        ParseOptionsResult::ExitFailure) {
      return 2;
    }
  }
  options = {};
  const auto output_model_result =
      parse({"glasswyrmd", "--wm-socket", "/tmp/wm", "--compositor-socket",
             "/tmp/comp", "--output-model"},
            options, output, error);
#if GW_HAS_LIBGWIPC
  if (output_model_result != ParseOptionsResult::Run ||
      !options.output_model || !options.integrated()) {
    return 16;
  }
#else
  if (output_model_result != ParseOptionsResult::ExitFailure ||
      error.find("does not include libgwipc") == std::string::npos) {
    return 16;
  }
#endif

  for (const auto &values : {
           std::vector<std::string>{"glasswyrmd", "--output-model"},
           std::vector<std::string>{"glasswyrmd", "--wm-socket", "/tmp/wm",
                                    "--compositor-socket", "/tmp/comp",
                                    "--output-model", "--output-model"},
       }) {
    options = {};
    if (parse(values, options, output, error) !=
        ParseOptionsResult::ExitFailure) {
      return 17;
    }
  }

  options = {};
  if (parse({"glasswyrmd"}, options, output, error) !=
          ParseOptionsResult::Run ||
      options.integrated() || options.real_input_enabled() ||
      options.output_model || options.scale_protocol ||
      options.xkb_rules != "evdev" || options.xkb_model != "pc105" ||
      options.xkb_layout != "us" || !options.xkb_variant.empty() ||
      !options.xkb_options.empty() || options.repeat_delay_ms != 500 ||
      options.repeat_rate_hz != 25) {
    return 3;
  }
  options = {};
  if (parse({"glasswyrmd", "--help"}, options, output, error) !=
          ParseOptionsResult::ExitSuccess ||
      output.find("--wm-socket PATH --compositor-socket PATH") ==
          std::string::npos ||
      output.find("--software-content") == std::string::npos) {
    return 4;
  }
  if (output.find("--output-model") == std::string::npos) {
    return 18;
  }
  if (output.find("--scale-protocol") == std::string::npos) {
    return 19;
  }
  if (output.find("--synthetic-input-socket PATH") == std::string::npos) {
    return 5;
  }
  if (output.find("--game-compat") == std::string::npos ||
      output.find("--disable-extension NAME") == std::string::npos) {
    return 13;
  }
  if (output.find("--libinput-device PATH") == std::string::npos ||
      output.find("--xkb-rules NAME") == std::string::npos ||
      output.find("--repeat-delay-ms N") == std::string::npos) {
    return 8;
  }
  options = {};
  if (parse({"glasswyrmd", "--x11-trace", "/tmp/trace.jsonl"}, options, output,
            error) != ParseOptionsResult::Run ||
      options.x11_trace != "/tmp/trace.jsonl") {
    return 6;
  }
  options = {};
  if (parse({"glasswyrmd", "--x11-trace", "/tmp/a", "--x11-trace", "/tmp/b"},
            options, output, error) != ParseOptionsResult::ExitFailure) {
    return 7;
  }

#if GW_HAS_EXPERIMENTAL
#if GW_HAS_LIBGWIPC
  options = {};
  if (parse({"glasswyrmd", "--wm-socket", "/tmp/wm",
             "--compositor-socket", "/tmp/comp", "--output-model",
             "--scale-protocol"},
            options, output, error) != ParseOptionsResult::Run ||
      !options.scale_protocol) {
    return 20;
  }
#endif
  options = {};
  if (parse({"glasswyrmd", "--wm-socket", "/tmp/wm", "--compositor-socket",
             "/tmp/comp", "--software-content", "--game-compat",
             "--disable-extension", "MIT-SHM"},
            options, output, error) != ParseOptionsResult::Run ||
      !options.game_compat || options.disabled_extensions.size() != 1 ||
      options.disabled_extensions.front() != "MIT-SHM") {
    return 14;
  }
#endif
  for (const auto &values : {
           std::vector<std::string>{"glasswyrmd", "--game-compat"},
           std::vector<std::string>{"glasswyrmd", "--scale-protocol"},
           std::vector<std::string>{"glasswyrmd", "--wm-socket", "/tmp/wm",
                                    "--compositor-socket", "/tmp/comp",
                                    "--output-model", "--scale-protocol",
                                    "--scale-protocol"},
           std::vector<std::string>{"glasswyrmd", "--disable-extension",
                                    "MIT-SHM"},
           std::vector<std::string>{"glasswyrmd", "--disable-extension",
                                    "not-an-extension"},
           std::vector<std::string>{"glasswyrmd", "--game-compat",
                                    "--game-compat"},
       }) {
    options = {};
    if (parse(values, options, output, error) !=
        ParseOptionsResult::ExitFailure) {
      return 15;
    }
  }

#if GW_HAS_LIBINPUT_BACKEND
  options = {};
  if (parse({"glasswyrmd",
             "--wm-socket",
             "/tmp/wm",
             "--compositor-socket",
             "/tmp/comp",
             "--libinput-device",
             "/alias/keyboard",
             "--libinput-device",
             "/alias/pointer",
             "--xkb-rules",
             "custom",
             "--xkb-model",
             "model",
             "--xkb-layout",
             "gb",
             "--xkb-variant",
             "intl",
             "--xkb-options",
             "compose:ralt",
             "--repeat-delay-ms",
             "250",
             "--repeat-rate-hz",
             "40"},
            options, output, error, fake_device) != ParseOptionsResult::Run ||
      !options.real_input_enabled() || options.libinput_devices.size() != 2 ||
      options.libinput_devices[0].canonical_path != "/dev/input/event0" ||
      options.libinput_devices[1].canonical_path != "/dev/input/event1" ||
      options.xkb_rules != "custom" || options.xkb_model != "model" ||
      options.xkb_layout != "gb" || options.xkb_variant != "intl" ||
      options.xkb_options != "compose:ralt" || options.repeat_delay_ms != 250 ||
      options.repeat_rate_hz != 40) {
    return 9;
  }
#else
  options = {};
  if (parse({"glasswyrmd", "--libinput-device", "/alias/keyboard"}, options,
            output, error, fake_device) != ParseOptionsResult::ExitFailure ||
      error.find("does not include the libinput backend") ==
          std::string::npos) {
    return 9;
  }
#endif

  for (const auto &values : {
#if GW_HAS_LIBINPUT_BACKEND
           std::vector<std::string>{"glasswyrmd", "--libinput-device",
                                    "/alias/keyboard"},
           std::vector<std::string>{"glasswyrmd", "--wm-socket", "/tmp/wm",
                                    "--compositor-socket", "/tmp/comp",
                                    "--synthetic-input-socket", "/tmp/input",
                                    "--libinput-device", "/alias/keyboard"},
           std::vector<std::string>{"glasswyrmd", "--wm-socket", "/tmp/wm",
                                    "--compositor-socket", "/tmp/comp",
                                    "--libinput-device", "/alias/keyboard",
                                    "--libinput-device", "/dev/input/event0"},
           std::vector<std::string>{"glasswyrmd", "--wm-socket", "/tmp/wm",
                                    "--compositor-socket", "/tmp/comp",
                                    "--libinput-device", "/bad"},
#endif
           std::vector<std::string>{"glasswyrmd", "--repeat-delay-ms", "0"},
           std::vector<std::string>{"glasswyrmd", "--repeat-delay-ms", "-1"},
           std::vector<std::string>{"glasswyrmd", "--repeat-rate-hz", "0"},
           std::vector<std::string>{"glasswyrmd", "--repeat-rate-hz",
                                    "1000000001"},
           std::vector<std::string>{"glasswyrmd", "--repeat-rate-hz", "x"},
           std::vector<std::string>{"glasswyrmd", "--xkb-rules", ""},
           std::vector<std::string>{"glasswyrmd", "--xkb-layout", "us",
                                    "--xkb-layout", "gb"},
       }) {
    options = {};
    if (parse(values, options, output, error, fake_device) !=
        ParseOptionsResult::ExitFailure) {
      return 10;
    }
  }

  InputDevicePath resolved;
  std::string detail;
  if (glasswyrm::server::resolve_input_device_path("/dev/null", resolved,
                                                   detail) ||
      detail.find("beneath /dev/input") == std::string::npos) {
    return 11;
  }
  Options replaced;
  replaced.libinput_devices.push_back({"/dev/input/event999999", 1, 2, 3});
  if (glasswyrm::server::validate_input_device_paths(replaced, detail) ||
      detail.find("identity changed") == std::string::npos) {
    return 12;
  }
  return 0;
}
