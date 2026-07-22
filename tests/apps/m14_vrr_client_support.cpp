#include "m14_vrr_client_support.hpp"

#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace gw::test::m14 {
namespace {

bool monotonic_nanoseconds(std::uint64_t &nanoseconds) noexcept {
  timespec value{};
  if (::clock_gettime(CLOCK_MONOTONIC, &value) != 0)
    return false;
  nanoseconds =
      static_cast<std::uint64_t>(value.tv_sec) * UINT64_C(1'000'000'000) +
      static_cast<std::uint64_t>(value.tv_nsec);
  return true;
}

timespec as_timespec(const std::uint64_t nanoseconds) noexcept {
  return {static_cast<time_t>(nanoseconds / UINT64_C(1'000'000'000)),
          static_cast<long>(nanoseconds % UINT64_C(1'000'000'000))};
}

void write_all(const int descriptor, const std::string_view contents) {
  std::size_t offset = 0;
  while (offset < contents.size()) {
    const auto count =
        ::write(descriptor, contents.data() + offset, contents.size() - offset);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      throw std::runtime_error("client-state write failed");
    offset += static_cast<std::size_t>(count);
  }
}

} // namespace

EventfdDamageProducer::EventfdDamageProducer()
    : request_(::eventfd(0, EFD_CLOEXEC)), ready_(::eventfd(0, EFD_CLOEXEC)) {
  if (request_ < 0 || ready_ < 0) {
    if (request_ >= 0)
      ::close(request_);
    if (ready_ >= 0)
      ::close(ready_);
    throw std::runtime_error("could not create cadence eventfds");
  }
  worker_ = std::thread([this] { run(); });
}

EventfdDamageProducer::~EventfdDamageProducer() noexcept {
  stop_.store(true);
  const std::uint64_t one = 1;
  const auto written = ::write(request_, &one, sizeof(one));
  static_cast<void>(written);
  if (worker_.joinable())
    worker_.join();
  (void)::close(request_);
  (void)::close(ready_);
}

void EventfdDamageProducer::signal(const int descriptor) {
  const std::uint64_t one = 1;
  if (::write(descriptor, &one, sizeof(one)) != sizeof(one))
    throw std::runtime_error("cadence eventfd signal failed");
}

void EventfdDamageProducer::wait(const int descriptor) {
  std::uint64_t value{};
  if (::read(descriptor, &value, sizeof(value)) != sizeof(value))
    throw std::runtime_error("cadence eventfd wait failed");
}

void EventfdDamageProducer::run() {
  for (;;) {
    wait(request_);
    if (stop_.load())
      return;
    auto value = deterministic_damage(frame_.load());
    {
      std::lock_guard lock(mutex_);
      pixels_ = std::move(value);
    }
    signal(ready_);
  }
}

std::vector<std::uint32_t>
EventfdDamageProducer::produce(const std::uint32_t frame) {
  frame_.store(frame);
  signal(request_);
  wait(ready_);
  std::lock_guard lock(mutex_);
  return pixels_;
}

std::uint64_t
target_interval_nanoseconds(const std::uint32_t refresh_hz) noexcept {
  return refresh_hz == 0 ? 0 : UINT64_C(1'000'000'000) / refresh_hz;
}

bool absolute_deadline(const std::uint64_t start_nanoseconds,
                       const std::uint64_t interval_nanoseconds,
                       const std::uint32_t frame_index,
                       std::uint64_t &deadline) noexcept {
  if (interval_nanoseconds == 0)
    return false;
  const auto multiplier = static_cast<std::uint64_t>(frame_index) + 1U;
  if (multiplier >
      (std::numeric_limits<std::uint64_t>::max() - start_nanoseconds) /
          interval_nanoseconds)
    return false;
  deadline = start_nanoseconds + multiplier * interval_nanoseconds;
  return true;
}

bool wait_until_monotonic(const std::uint64_t deadline_nanoseconds,
                          const std::uint64_t final_spin_nanoseconds) noexcept {
  if (deadline_nanoseconds == 0)
    return false;
  const auto spin = final_spin_nanoseconds > deadline_nanoseconds
                        ? deadline_nanoseconds
                        : final_spin_nanoseconds;
  const auto sleep_deadline = deadline_nanoseconds - spin;
  auto sleep_time = as_timespec(sleep_deadline);
  int status{};
  do {
    status =
        ::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &sleep_time, nullptr);
  } while (status == EINTR);
  if (status != 0)
    return false;
  for (;;) {
    std::uint64_t now{};
    if (!monotonic_nanoseconds(now))
      return false;
    if (now >= deadline_nanoseconds)
      break;
  }
  return true;
}

std::vector<std::uint32_t> deterministic_pattern(const std::uint16_t width,
                                                 const std::uint16_t height) {
  std::vector<std::uint32_t> pixels;
  pixels.reserve(static_cast<std::size_t>(width) * height);
  for (std::uint16_t y = 0; y < height; ++y) {
    for (std::uint16_t x = 0; x < width; ++x) {
      const bool checker = ((x / 8U) ^ (y / 8U)) & 1U;
      const bool diagonal = x == y || x + y + 1U == width;
      pixels.push_back(diagonal  ? UINT32_C(0x00ff4040)
                       : checker ? UINT32_C(0x001020e0)
                                 : UINT32_C(0x00e0f020));
    }
  }
  return pixels;
}

std::vector<std::uint32_t>
deterministic_damage(const std::uint32_t frame_index) {
  const auto first =
      frame_index % 2U == 0 ? UINT32_C(0x0010d0f0) : UINT32_C(0x00f05020);
  const auto second =
      frame_index % 2U == 0 ? UINT32_C(0x00f05020) : UINT32_C(0x0010d0f0);
  std::vector<std::uint32_t> pixels;
  pixels.reserve(static_cast<std::size_t>(kDamageWidth) * kDamageHeight);
  for (std::uint16_t y = 0; y < kDamageHeight; ++y)
    for (std::uint16_t x = 0; x < kDamageWidth; ++x)
      pixels.push_back(((x / 4U) ^ (y / 4U)) & 1U ? first : second);
  return pixels;
}

std::string client_state_json(const ClientState &state) {
  return "{\n"
         "  \"schema\": \"glasswyrm.m14-vrr-client.v2\",\n"
         "  \"mode\": \"" +
         std::string(client_mode_name(state.mode)) +
         "\",\n"
         "  \"window\": " +
         std::to_string(state.window) +
         ",\n"
         "  \"width\": " +
         std::to_string(state.width) +
         ",\n"
         "  \"height\": " +
         std::to_string(state.height) +
         ",\n"
         "  \"preference\": \"" +
         std::string(client_preference_name(state.preference ==
                                                    ClientPreference::Default &&
                                                state.prefer
                                            ? ClientPreference::Prefer
                                            : state.preference)) +
         "\",\n"
         "  \"fullscreen_requested\": " +
         (state.fullscreen_requested ? "true" : "false") +
         ",\n"
         "  \"borderless\": " +
         (state.borderless ? "true" : "false") +
         ",\n"
         "  \"frame_count\": " +
         std::to_string(state.frame_count) +
         ",\n"
         "  \"target_refresh_hz\": " +
         std::to_string(state.target_refresh_hz) +
         ",\n"
         "  \"target_interval_nanoseconds\": " +
         std::to_string(state.target_interval_nanoseconds) +
         ",\n"
         "  \"events_selected\": " +
         (state.events_selected ? "true" : "false") +
         ",\n"
         "  \"preference_reply_count\": " +
         std::to_string(state.preference_reply_count) +
         ",\n"
         "  \"notify_event_count\": " +
         std::to_string(state.notify_event_count) +
         ",\n"
         "  \"notify_change_mask\": " +
         std::to_string(state.notify_change_mask) +
         ",\n"
         "  \"reason_mask\": " +
         std::to_string(state.reason_mask) +
         ",\n"
         "  \"eventfd_synchronized\": " +
         (state.eventfd_synchronized ? "true" : "false") +
         ",\n"
         "  \"preference_sequence\": " +
         (state.mode == ClientMode::Preference
              ? "[\"Default\",\"Allow\",\"Prefer\",\"Disable\"]"
              : "[]") +
         ",\n"
         "  \"cadence_absolute_monotonic\": " +
         (state.mode == ClientMode::Cadence ? "true" : "false") +
         ",\n"
         "  \"bounded_damage_width\": " +
         std::to_string(kDamageWidth) +
         ",\n"
         "  \"bounded_damage_height\": " +
         std::to_string(kDamageHeight) + "\n}\n";
}

void write_client_state(const std::string &path, const ClientState &state) {
  if (path.empty())
    throw std::runtime_error("client-state path is empty");
  const int descriptor = ::open(
      path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (descriptor < 0)
    throw std::runtime_error("client-state path must name a new file");
  try {
    const auto contents = client_state_json(state);
    write_all(descriptor, contents);
    if (::fsync(descriptor) != 0)
      throw std::runtime_error("client-state sync failed");
  } catch (...) {
    (void)::close(descriptor);
    (void)::unlink(path.c_str());
    throw;
  }
  if (::close(descriptor) != 0) {
    (void)::unlink(path.c_str());
    throw std::runtime_error("client-state close failed");
  }
}

} // namespace gw::test::m14
