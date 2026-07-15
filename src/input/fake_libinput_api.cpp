#include "input/fake_libinput_api.hpp"

#include "input/device_path_allowlist.hpp"

namespace glasswyrm::input {

bool FakeLibinputApi::create(DevicePathAllowlist &,
                             const std::span<const std::string> paths,
                             std::string &error) {
  if (!create_error_.empty()) {
    error = create_error_;
    return false;
  }
  if (created_) {
    error = "fake libinput context already exists";
    return false;
  }
  created_paths_.assign(paths.begin(), paths.end());
  created_ = true;
  error.clear();
  return true;
}

bool FakeLibinputApi::dispatch(std::string &error) {
  ++dispatch_count_;
  if (!dispatch_error_.empty()) {
    error = dispatch_error_;
    return false;
  }
  error.clear();
  return true;
}

bool FakeLibinputApi::next_event(std::uint32_t, std::uint32_t,
                                 LibinputEvent &event) {
  if (events_.empty()) return false;
  event = std::move(events_.front());
  events_.pop_front();
  ++consumed_event_count_;
  return true;
}

bool FakeLibinputApi::suspend(std::string &error) {
  ++suspend_count_;
  if (!suspend_error_.empty()) {
    error = suspend_error_;
    return false;
  }
  suspended_ = true;
  error.clear();
  return true;
}

bool FakeLibinputApi::resume(std::string &error) {
  ++resume_count_;
  if (!resume_error_.empty()) {
    error = resume_error_;
    return false;
  }
  suspended_ = false;
  error.clear();
  return true;
}

}  // namespace glasswyrm::input
