#include "backends/session/vt_session.hpp"

#include <linux/kd.h>
#include <utility>

namespace glasswyrm::session {

DirectVirtualTerminalSession::DirectVirtualTerminalSession(
    VirtualTerminalApi &api, DisplaySessionControl &display) noexcept
    : api_(api), display_(display) {}

DirectVirtualTerminalSession::~DirectVirtualTerminalSession() {
  try {
    std::string ignored;
    (void)restore(ignored);
  } catch (...) {
  }
}

void DirectVirtualTerminalSession::append_error(const std::string_view detail,
                                                std::string &error) {
  if (!error.empty())
    error += "; ";
  error.append(detail);
}

void DirectVirtualTerminalSession::append_api_error(
    const std::string_view operation, std::string &error) const {
  std::string detail(operation);
  const auto cause = api_.last_error();
  if (!cause.empty()) {
    detail += ": ";
    detail += cause;
  }
  append_error(detail, error);
}

bool DirectVirtualTerminalSession::acquire(const std::string_view path,
                                           const VirtualTerminalSignals signals,
                                           std::string &error) {
  error.clear();
  if (state_ != DirectSessionState::Empty) {
    error = "virtual-terminal session was already initialized";
    return false;
  }
  if (signals.release_signal <= 0 || signals.acquire_signal <= 0 ||
      signals.release_signal == signals.acquire_signal) {
    error = "VT release and acquire signals must be distinct positive values";
    return false;
  }
  if (!parse_virtual_terminal_path(path, terminal_number_)) {
    error = "tty path must be exactly /dev/ttyN for VT 1 through 63";
    return false;
  }

  terminal_fd_ = api_.open_terminal(path);
  if (terminal_fd_ < 0) {
    append_api_error("open virtual terminal", error);
    return false;
  }

  DeviceIdentity identity;
  if (!api_.identify(terminal_fd_, identity)) {
    append_api_error("identify virtual terminal", error);
    unwind_failed_acquire(error);
    return false;
  }
  if (!identity.character_device || identity.major != kLinuxTtyMajor ||
      identity.minor != terminal_number_) {
    error = "tty path is not the matching Linux virtual-terminal device";
    unwind_failed_acquire(error);
    return false;
  }
  if (!api_.get_state(terminal_fd_, saved_state_)) {
    append_api_error("query active virtual terminal", error);
    unwind_failed_acquire(error);
    return false;
  }
  have_state_ = true;
  if (!api_.get_mode(terminal_fd_, saved_mode_)) {
    append_api_error("query VT mode", error);
    unwind_failed_acquire(error);
    return false;
  }
  have_mode_ = true;
  if (!api_.get_kd_mode(terminal_fd_, saved_kd_mode_)) {
    append_api_error("query KD mode", error);
    unwind_failed_acquire(error);
    return false;
  }
  have_kd_mode_ = true;
  if (!api_.get_keyboard_mode(terminal_fd_, saved_keyboard_mode_)) {
    append_api_error("query keyboard mode", error);
    unwind_failed_acquire(error);
    return false;
  }
  have_keyboard_mode_ = true;
  if (!api_.activate(terminal_fd_, terminal_number_)) {
    append_api_error("activate virtual terminal", error);
    unwind_failed_acquire(error);
    return false;
  }
  activated_ = true;
  if (!api_.wait_until_active(terminal_fd_, terminal_number_)) {
    append_api_error("wait for active virtual terminal", error);
    unwind_failed_acquire(error);
    return false;
  }
  if (!api_.set_keyboard_mode(terminal_fd_, K_OFF)) {
    append_api_error("disable virtual-terminal keyboard processing", error);
    unwind_failed_acquire(error);
    return false;
  }
  keyboard_mode_set_ = true;
  if (!api_.set_process_mode(terminal_fd_, signals.release_signal,
                             signals.acquire_signal)) {
    append_api_error("set VT_PROCESS mode", error);
    unwind_failed_acquire(error);
    return false;
  }
  process_mode_set_ = true;
  if (!api_.set_graphics_mode(terminal_fd_)) {
    append_api_error("set KD_GRAPHICS mode", error);
    unwind_failed_acquire(error);
    return false;
  }
  graphics_mode_set_ = true;
  if (!display_.acquire_master(error)) {
    if (error.empty())
      error = "acquire DRM master";
    unwind_failed_acquire(error);
    return false;
  }
  master_owned_ = true;
  state_ = DirectSessionState::Active;
  return true;
}

bool DirectVirtualTerminalSession::release(std::string &error) {
  error.clear();
  if (state_ != DirectSessionState::Active) {
    error = "VT release requires an active direct session";
    return false;
  }
  if (!display_.quiesce_pending_flip(error)) {
    if (error.empty())
      error = "quiesce pending page flip";
    state_ = DirectSessionState::Failed;
    return false;
  }
  if (!display_.drop_master(error)) {
    if (error.empty())
      error = "drop DRM master for VT release";
    state_ = DirectSessionState::Failed;
    return false;
  }
  master_owned_ = false;
  if (!api_.acknowledge_release(terminal_fd_)) {
    append_api_error("acknowledge VT release", error);
    state_ = DirectSessionState::Failed;
    return false;
  }
  state_ = DirectSessionState::Suspended;
  return true;
}

bool DirectVirtualTerminalSession::reacquire(std::string &error) {
  error.clear();
  if (state_ != DirectSessionState::Suspended) {
    error = "VT acquire requires a suspended direct session";
    return false;
  }
  if (!api_.acknowledge_acquire(terminal_fd_)) {
    append_api_error("acknowledge VT acquire", error);
    state_ = DirectSessionState::Failed;
    return false;
  }
  if (!display_.acquire_master(error)) {
    if (error.empty())
      error = "reacquire DRM master";
    state_ = DirectSessionState::Failed;
    return false;
  }
  master_owned_ = true;
  if (!display_.present_committed_frame(error)) {
    if (error.empty())
      error = "full modeset of committed frame";
    state_ = DirectSessionState::Failed;
    return false;
  }
  state_ = DirectSessionState::Active;
  return true;
}

bool DirectVirtualTerminalSession::restore(std::string &error) {
  error.clear();
  if (state_ == DirectSessionState::Empty ||
      state_ == DirectSessionState::Restored)
    return true;

  bool success = true;
  std::string operation_error;
  if (!master_owned_ && graphics_mode_set_) {
    if (!api_.activate(terminal_fd_, terminal_number_)) {
      append_api_error(
          "reactivate Glasswyrm virtual terminal for display restore", error);
      success = false;
    } else if (!api_.wait_until_active(terminal_fd_, terminal_number_)) {
      append_api_error("wait for Glasswyrm virtual terminal during restore",
                       error);
      success = false;
    } else if (!display_.acquire_master(operation_error)) {
      append_error(operation_error.empty() ? "reacquire DRM master for restore"
                                           : operation_error,
                   error);
      success = false;
      operation_error.clear();
    } else {
      master_owned_ = true;
    }
  }
  if (master_owned_) {
    bool display_restored = true;
    if (!display_.restore_original_display(operation_error)) {
      append_error(operation_error.empty() ? "restore original display"
                                           : operation_error,
                   error);
      success = false;
      display_restored = false;
    }
    operation_error.clear();
    if (display_restored &&
        !display_.release_scanout_resources(operation_error)) {
      append_error(operation_error.empty() ? "release scanout resources"
                                           : operation_error,
                   error);
      success = false;
    }
    operation_error.clear();
    if (!display_.drop_master(operation_error)) {
      append_error(
          operation_error.empty() ? "drop DRM master" : operation_error, error);
      success = false;
    } else {
      master_owned_ = false;
    }
  }
  if (graphics_mode_set_ && have_kd_mode_) {
    if (!api_.set_kd_mode(terminal_fd_, saved_kd_mode_)) {
      append_api_error("restore original KD mode", error);
      success = false;
    } else {
      graphics_mode_set_ = false;
    }
  }
  if (process_mode_set_ && have_mode_) {
    if (!api_.set_mode(terminal_fd_, saved_mode_)) {
      append_api_error("restore original VT mode", error);
      success = false;
    } else {
      process_mode_set_ = false;
    }
  }
  if (keyboard_mode_set_ && have_keyboard_mode_) {
    if (!api_.set_keyboard_mode(terminal_fd_, saved_keyboard_mode_)) {
      append_api_error("restore original keyboard mode", error);
      success = false;
    } else {
      keyboard_mode_set_ = false;
    }
  }
  if (activated_ && have_state_ && saved_state_.active != terminal_number_) {
    if (!api_.activate(terminal_fd_, saved_state_.active)) {
      append_api_error("reactivate previous virtual terminal", error);
      success = false;
    } else if (!api_.wait_until_active(terminal_fd_, saved_state_.active)) {
      append_api_error("wait for previous virtual terminal", error);
      success = false;
    } else {
      activated_ = false;
    }
  }
  api_.close_terminal(std::exchange(terminal_fd_, -1));
  state_ = DirectSessionState::Restored;
  return success;
}

void DirectVirtualTerminalSession::unwind_failed_acquire(std::string &error) {
  if (master_owned_) {
    std::string cleanup_error;
    if (!display_.drop_master(cleanup_error))
      append_error(cleanup_error.empty() ? "cleanup DRM master" : cleanup_error,
                   error);
    master_owned_ = false;
  }
  if (graphics_mode_set_ && have_kd_mode_) {
    if (!api_.set_kd_mode(terminal_fd_, saved_kd_mode_))
      append_api_error("cleanup KD mode", error);
    graphics_mode_set_ = false;
  }
  if (process_mode_set_ && have_mode_) {
    if (!api_.set_mode(terminal_fd_, saved_mode_))
      append_api_error("cleanup VT mode", error);
    process_mode_set_ = false;
  }
  if (keyboard_mode_set_ && have_keyboard_mode_) {
    if (!api_.set_keyboard_mode(terminal_fd_, saved_keyboard_mode_))
      append_api_error("cleanup keyboard mode", error);
    keyboard_mode_set_ = false;
  }
  if (activated_ && have_state_ && saved_state_.active != terminal_number_) {
    if (!api_.activate(terminal_fd_, saved_state_.active) ||
        !api_.wait_until_active(terminal_fd_, saved_state_.active))
      append_api_error("cleanup previous virtual terminal", error);
    activated_ = false;
  }
  api_.close_terminal(std::exchange(terminal_fd_, -1));
  state_ = DirectSessionState::Failed;
}

} // namespace glasswyrm::session
