#include "tests/helpers/uinput_m11_protocol.hpp"

#include <linux/input.h>
#include <linux/uinput.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace protocol = gw::test::uinput_m11;

namespace {

constexpr std::uint16_t kBus = BUS_VIRTUAL;
constexpr std::uint16_t kVendor = 0x4757;
constexpr std::uint16_t kKeyboardProduct = 0x1101;
constexpr std::uint16_t kPointerProduct = 0x1102;
constexpr std::uint16_t kVersion = 1;

class UniqueFd {
 public:
  UniqueFd() = default;
  explicit UniqueFd(int value) : value_(value) {}
  ~UniqueFd() { reset(); }
  UniqueFd(const UniqueFd &) = delete;
  UniqueFd &operator=(const UniqueFd &) = delete;
  UniqueFd(UniqueFd &&other) noexcept : value_(std::exchange(other.value_, -1)) {}
  UniqueFd &operator=(UniqueFd &&other) noexcept {
    if (this != &other) reset(std::exchange(other.value_, -1));
    return *this;
  }
  int get() const { return value_; }
  explicit operator bool() const { return value_ >= 0; }
  void reset(int value = -1) {
    if (value_ >= 0) (void)::close(value_);
    value_ = value;
  }

 private:
  int value_{-1};
};

class UinputDevice {
 public:
  UinputDevice() = default;
  ~UinputDevice() {
    if (created_) (void)::ioctl(fd_.get(), UI_DEV_DESTROY);
  }
  UinputDevice(const UinputDevice &) = delete;
  UinputDevice &operator=(const UinputDevice &) = delete;

  bool create_keyboard(std::string &error) {
    if (!open_device(error) || !set_bit(UI_SET_EVBIT, EV_KEY, error))
      return false;
    for (const auto code : protocol::keyboard_key_codes())
      if (!set_bit(UI_SET_KEYBIT, code, error)) return false;
    return finish(protocol::kKeyboardName, kKeyboardProduct, false, error);
  }

  bool create_pointer(std::string &error) {
    if (!open_device(error) || !set_bit(UI_SET_EVBIT, EV_REL, error) ||
        !set_bit(UI_SET_EVBIT, EV_KEY, error) ||
        !set_bit(UI_SET_RELBIT, REL_X, error) ||
        !set_bit(UI_SET_RELBIT, REL_Y, error) ||
        !set_bit(UI_SET_RELBIT, REL_WHEEL, error) ||
        !set_bit(UI_SET_KEYBIT, BTN_LEFT, error) ||
        !set_bit(UI_SET_KEYBIT, BTN_MIDDLE, error) ||
        !set_bit(UI_SET_KEYBIT, BTN_RIGHT, error))
      return false;
#ifdef REL_HWHEEL
    horizontal_wheel_ = ::ioctl(fd_.get(), UI_SET_RELBIT, REL_HWHEEL) == 0;
    if (!horizontal_wheel_ && errno != EINVAL) {
      error = system_error("enable REL_HWHEEL");
      return false;
    }
#endif
    return finish(protocol::kPointerName, kPointerProduct, horizontal_wheel_,
                  error);
  }

  bool emit(const protocol::Event &event, std::string &error) const {
    input_event input{};
    input.type = event.type;
    input.code = event.code;
    input.value = event.value;
    if (!write_all(fd_.get(), &input, sizeof(input))) {
      error = system_error("write input event");
      return false;
    }
    input = {};
    input.type = EV_SYN;
    input.code = SYN_REPORT;
    if (!write_all(fd_.get(), &input, sizeof(input))) {
      error = system_error("write SYN_REPORT");
      return false;
    }
    return true;
  }

  protocol::DeviceIdentity identity() const {
    return {name_, sysname_, event_path_, kBus, kVendor, product_, kVersion,
            horizontal_wheel_};
  }
  bool horizontal_wheel() const { return horizontal_wheel_; }

 private:
  static std::string system_error(std::string_view action) {
    return std::string(action) + ": " + std::strerror(errno);
  }

  static bool write_all(int fd, const void *data, std::size_t size) {
    const auto *bytes = static_cast<const std::byte *>(data);
    std::size_t written = 0;
    while (written < size) {
      const auto result = ::write(fd, bytes + written, size - written);
      if (result > 0) {
        written += static_cast<std::size_t>(result);
      } else if (result < 0 && errno == EINTR) {
        continue;
      } else {
        return false;
      }
    }
    return true;
  }

  bool open_device(std::string &error) {
    fd_.reset(::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC));
    if (fd_) return true;
    error = system_error("open /dev/uinput");
    return false;
  }

  bool set_bit(unsigned long request, int bit, std::string &error) {
    if (::ioctl(fd_.get(), request, bit) == 0) return true;
    error = system_error("configure uinput capability");
    return false;
  }

  bool finish(std::string_view name, std::uint16_t product,
              bool horizontal_wheel, std::string &error) {
    uinput_setup setup{};
    setup.id = {kBus, kVendor, product, kVersion};
    std::snprintf(setup.name, sizeof(setup.name), "%.*s",
                  static_cast<int>(name.size()), name.data());
    if (::ioctl(fd_.get(), UI_DEV_SETUP, &setup) < 0 ||
        ::ioctl(fd_.get(), UI_DEV_CREATE) < 0) {
      error = system_error("create uinput device");
      return false;
    }
    created_ = true;
    name_ = name;
    product_ = product;
    horizontal_wheel_ = horizontal_wheel;
    std::array<char, 64> sysname{};
    if (::ioctl(fd_.get(), UI_GET_SYSNAME(sysname.size()), sysname.data()) < 0) {
      error = system_error("query uinput sysname");
      return false;
    }
    sysname_ = sysname.data();
    return discover_event_path(error);
  }

  bool discover_event_path(std::string &error) {
    const auto directory = std::filesystem::path("/sys/class/input") / sysname_;
    for (int attempt = 0; attempt != 100; ++attempt) {
      std::error_code status_error;
      if (std::filesystem::exists(directory, status_error)) {
        for (std::filesystem::directory_iterator iterator(directory, status_error),
             end;
             !status_error && iterator != end; iterator.increment(status_error)) {
          const auto entry = iterator->path().filename().string();
          if (entry.starts_with("event") && entry.size() > 5 &&
              std::all_of(entry.begin() + 5, entry.end(), [](unsigned char c) {
                return c >= '0' && c <= '9';
              })) {
            const auto candidate = std::filesystem::path("/dev/input") / entry;
            if (std::filesystem::exists(candidate, status_error) &&
                !status_error) {
              event_path_ = candidate.string();
              return true;
            }
          }
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    error = "discover event path for " + sysname_ + ": timed out";
    return false;
  }

  UniqueFd fd_;
  bool created_{};
  bool horizontal_wheel_{};
  std::uint16_t product_{};
  std::string name_;
  std::string sysname_;
  std::string event_path_;
};

class SocketPath {
 public:
  explicit SocketPath(std::string path) : path_(std::move(path)) {}
  ~SocketPath() {
    if (bound_) (void)::unlink(path_.c_str());
  }
  void mark_bound() { bound_ = true; }

 private:
  std::string path_;
  bool bound_{};
};

void usage(FILE *output) {
  std::fprintf(output,
      "Usage:\n"
      "  gw_uinput_m11 serve --control-socket PATH --devices-json PATH\n"
      "  gw_uinput_m11 run --control-socket PATH --scenario NAME "
      "--result-json PATH\n"
      "Scenarios: basic-typing, repeat, scroll, primary-selection, "
      "clipboard-probe, move, resize, close, pointer-anchor, post-vt, "
      "post-restart\n");
}

bool make_address(const std::string &path, sockaddr_un &address,
                  socklen_t &length, std::string &error) {
  if (path.empty() || path.size() >= sizeof(address.sun_path)) {
    error = "control socket path is empty or too long";
    return false;
  }
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) +
                                  path.size() + 1);
  return true;
}

bool write_file_0600(const std::string &path, std::string_view contents,
                     std::string &error) {
  UniqueFd output(::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC |
                                         O_CLOEXEC | O_NOFOLLOW,
                         S_IRUSR | S_IWUSR));
  if (!output) {
    error = "open output " + path + ": " + std::strerror(errno);
    return false;
  }
  if (::fchmod(output.get(), S_IRUSR | S_IWUSR) < 0) {
    error = "set output mode: " + std::string(std::strerror(errno));
    return false;
  }
  std::size_t offset = 0;
  while (offset < contents.size()) {
    const auto result =
        ::write(output.get(), contents.data() + offset, contents.size() - offset);
    if (result > 0) offset += static_cast<std::size_t>(result);
    else if (result < 0 && errno == EINTR) continue;
    else {
      error = "write output: " + std::string(std::strerror(errno));
      return false;
    }
  }
  return true;
}

UniqueFd create_listener(const std::string &path, SocketPath &cleanup,
                         std::string &error) {
  sockaddr_un address{};
  socklen_t length{};
  if (!make_address(path, address, length, error)) return {};
  struct stat existing {};
  if (::lstat(path.c_str(), &existing) == 0) {
    error = "control socket path already exists";
    return {};
  }
  if (errno != ENOENT) {
    error = "inspect control socket path: " + std::string(std::strerror(errno));
    return {};
  }
  UniqueFd listener(::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0));
  if (!listener) {
    error = "create control socket: " + std::string(std::strerror(errno));
    return {};
  }
  const mode_t previous = ::umask(0077);
  const int bind_result = ::bind(listener.get(),
                                 reinterpret_cast<sockaddr *>(&address), length);
  ::umask(previous);
  if (bind_result < 0) {
    error = "bind control socket: " + std::string(std::strerror(errno));
    return {};
  }
  cleanup.mark_bound();
  if (::chmod(path.c_str(), S_IRUSR | S_IWUSR) < 0 ||
      ::listen(listener.get(), 4) < 0) {
    error = "secure/listen control socket: " + std::string(std::strerror(errno));
    return {};
  }
  return listener;
}

UniqueFd create_signal_fd(std::string &error) {
  sigset_t mask;
  ::sigemptyset(&mask);
  ::sigaddset(&mask, SIGINT);
  ::sigaddset(&mask, SIGTERM);
  if (::sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
    error = "block termination signals: " + std::string(std::strerror(errno));
    return {};
  }
  UniqueFd fd(::signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK));
  if (!fd) error = "create signal fd: " + std::string(std::strerror(errno));
  return fd;
}

bool run_scenario(std::string_view name, const UinputDevice &keyboard,
                  const UinputDevice &pointer, std::size_t &emitted,
                  std::string &error) {
  emitted = 0;
  for (const auto &event : protocol::scenario_events(name)) {
#ifdef REL_HWHEEL
    if (event.code == REL_HWHEEL && event.type == EV_REL &&
        !pointer.horizontal_wheel())
      continue;
#endif
    const auto &device =
        event.device == protocol::Device::keyboard ? keyboard : pointer;
    if (!device.emit(event, error)) return false;
    ++emitted;
    if (event.delay_ms != 0)
      std::this_thread::sleep_for(std::chrono::milliseconds(event.delay_ms));
  }
  return true;
}

int serve(const std::string &socket_path, const std::string &devices_path) {
  std::string error;
  UinputDevice keyboard;
  UinputDevice pointer;
  if (!keyboard.create_keyboard(error) || !pointer.create_pointer(error)) {
    std::fprintf(stderr, "gw_uinput_m11: %s\n", error.c_str());
    return 1;
  }
  SocketPath cleanup(socket_path);
  auto listener = create_listener(socket_path, cleanup, error);
  auto signal_fd = create_signal_fd(error);
  if (!listener || !signal_fd ||
      !write_file_0600(devices_path,
                       protocol::devices_json(keyboard.identity(),
                                              pointer.identity()),
                       error)) {
    std::fprintf(stderr, "gw_uinput_m11: %s\n", error.c_str());
    return 1;
  }
  for (;;) {
    pollfd descriptors[2]{{listener.get(), POLLIN, 0},
                          {signal_fd.get(), POLLIN, 0}};
    const int ready = ::poll(descriptors, 2, -1);
    if (ready < 0 && errno == EINTR) continue;
    if (ready < 0) {
      std::fprintf(stderr, "gw_uinput_m11: poll: %s\n", std::strerror(errno));
      return 1;
    }
    if ((descriptors[1].revents & POLLIN) != 0) return 0;
    if ((descriptors[0].revents & POLLIN) == 0) continue;
    UniqueFd peer(::accept4(listener.get(), nullptr, nullptr, SOCK_CLOEXEC));
    if (!peer) continue;
    ucred credentials{};
    socklen_t credentials_size = sizeof(credentials);
    if (::getsockopt(peer.get(), SOL_SOCKET, SO_PEERCRED, &credentials,
                     &credentials_size) < 0 ||
        credentials_size != sizeof(credentials) ||
        credentials.uid != ::geteuid())
      continue;
    std::array<char, 256> packet{};
    const auto received = ::recv(peer.get(), packet.data(), packet.size(),
                                 MSG_TRUNC);
    std::string_view scenario;
    if (received <= 0 || static_cast<std::size_t>(received) > packet.size() ||
        !protocol::parse_request(
            std::string_view(packet.data(), static_cast<std::size_t>(received)),
            scenario)) {
      const auto response = protocol::result_json("", "rejected", 0);
      (void)::send(peer.get(), response.data(), response.size(), MSG_NOSIGNAL);
      continue;
    }
    std::size_t emitted = 0;
    if (!run_scenario(scenario, keyboard, pointer, emitted, error)) {
      const auto response = protocol::result_json(scenario, "failed", 0);
      (void)::send(peer.get(), response.data(), response.size(), MSG_NOSIGNAL);
      std::fprintf(stderr, "gw_uinput_m11: %s\n", error.c_str());
      return 1;
    }
    const auto response =
        protocol::result_json(scenario, "completed", emitted);
    (void)::send(peer.get(), response.data(), response.size(), MSG_NOSIGNAL);
  }
}

int run(const std::string &socket_path, std::string_view scenario,
        const std::string &result_path) {
  std::string error;
  sockaddr_un address{};
  socklen_t length{};
  if (!protocol::known_scenario(scenario) ||
      !make_address(socket_path, address, length, error)) {
    usage(stderr);
    return 2;
  }
  UniqueFd peer(::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0));
  if (!peer || ::connect(peer.get(), reinterpret_cast<sockaddr *>(&address),
                         length) < 0) {
    std::fprintf(stderr, "gw_uinput_m11: connect: %s\n", std::strerror(errno));
    return 1;
  }
  const auto request = protocol::encode_request(scenario);
  if (::send(peer.get(), request.data(), request.size(), MSG_NOSIGNAL) !=
      static_cast<ssize_t>(request.size())) {
    std::fprintf(stderr, "gw_uinput_m11: send: %s\n", std::strerror(errno));
    return 1;
  }
  pollfd descriptor{peer.get(), POLLIN, 0};
  if (::poll(&descriptor, 1, 15000) <= 0) {
    std::fprintf(stderr, "gw_uinput_m11: scenario response timed out\n");
    return 1;
  }
  std::array<char, 1024> response{};
  const auto received = ::recv(peer.get(), response.data(), response.size(),
                               MSG_TRUNC);
  if (received <= 0 || static_cast<std::size_t>(received) > response.size() ||
      !write_file_0600(result_path,
                       std::string_view(response.data(),
                                        static_cast<std::size_t>(received)),
                       error)) {
    std::fprintf(stderr, "gw_uinput_m11: %s\n", error.c_str());
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--help") {
    usage(stdout);
    return 0;
  }
  if (argc < 2) {
    usage(stderr);
    return 2;
  }
  const std::string_view mode(argv[1]);
  std::string socket_path;
  std::string devices_path;
  std::string result_path;
  std::string scenario;
  for (int index = 2; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (index + 1 >= argc) {
      usage(stderr);
      return 2;
    }
    const std::string value(argv[++index]);
    if (argument == "--control-socket") socket_path = value;
    else if (argument == "--devices-json") devices_path = value;
    else if (argument == "--result-json") result_path = value;
    else if (argument == "--scenario") scenario = value;
    else {
      usage(stderr);
      return 2;
    }
  }
  if (mode == "serve" && !socket_path.empty() && !devices_path.empty() &&
      result_path.empty() && scenario.empty())
    return serve(socket_path, devices_path);
  if (mode == "run" && !socket_path.empty() && devices_path.empty() &&
      !result_path.empty() && protocol::known_scenario(scenario))
    return run(socket_path, scenario, result_path);
  usage(stderr);
  return 2;
}
