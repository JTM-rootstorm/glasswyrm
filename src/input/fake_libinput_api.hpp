#pragma once

#include "input/libinput_api.hpp"

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace glasswyrm::input {

class FakeLibinputApi final : public LibinputApi {
 public:
  [[nodiscard]] bool create(DevicePathAllowlist &access,
                            std::span<const std::string> paths,
                            std::string &error) override;
  [[nodiscard]] int poll_fd() const noexcept override { return poll_fd_; }
  [[nodiscard]] bool dispatch(std::string &error) override;
  [[nodiscard]] bool next_event(std::uint32_t root_width,
                                std::uint32_t root_height,
                                LibinputEvent &event) override;
  [[nodiscard]] bool suspend(std::string &error) override;
  [[nodiscard]] bool resume(std::string &error) override;

  void queue(LibinputEvent event) { events_.push_back(std::move(event)); }
  void fail_create(std::string error) { create_error_ = std::move(error); }
  void fail_dispatch(std::string error) { dispatch_error_ = std::move(error); }
  void fail_suspend(std::string error) { suspend_error_ = std::move(error); }
  void fail_resume(std::string error) { resume_error_ = std::move(error); }
  void set_poll_fd(int fd) noexcept { poll_fd_ = fd; }

  [[nodiscard]] const std::vector<std::string> &created_paths() const noexcept {
    return created_paths_;
  }
  [[nodiscard]] std::size_t dispatch_count() const noexcept {
    return dispatch_count_;
  }
  [[nodiscard]] std::size_t consumed_event_count() const noexcept {
    return consumed_event_count_;
  }
  [[nodiscard]] std::size_t suspend_count() const noexcept {
    return suspend_count_;
  }
  [[nodiscard]] std::size_t resume_count() const noexcept {
    return resume_count_;
  }

 private:
  std::deque<LibinputEvent> events_;
  std::vector<std::string> created_paths_;
  std::string create_error_;
  std::string dispatch_error_;
  std::string suspend_error_;
  std::string resume_error_;
  int poll_fd_{73};
  std::size_t dispatch_count_{};
  std::size_t consumed_event_count_{};
  std::size_t suspend_count_{};
  std::size_t resume_count_{};
  bool created_{};
  bool suspended_{};
};

}  // namespace glasswyrm::input
