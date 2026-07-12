#include <glasswyrm/ipc/control.h>

#include "ipc/wire/control.hpp"

#include <algorithm>
#include <new>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace wire = gw::ipc::wire;

struct gwipc_control_payload {
  std::vector<std::uint8_t> bytes;
};

using ControlValue =
    std::variant<wire::SnapshotBegin, wire::SnapshotEnd, wire::SnapshotAbort>;

struct gwipc_decoded_control {
  std::uint16_t type{};
  ControlValue value{wire::SnapshotBegin{}};
  std::string detail;
  gwipc_snapshot_begin begin{};
  gwipc_snapshot_end end{};
  gwipc_snapshot_abort abort{};
};

namespace {

template <typename T>
bool valid_input(const T *value) noexcept {
  return value && value->struct_size >= sizeof(T) &&
         std::all_of(std::begin(value->reserved), std::end(value->reserved),
                     [](const auto item) { return item == 0; });
}

template <typename T>
gwipc_status make_payload(T value, gwipc_control_payload **out_payload) {
  if (!out_payload) return GWIPC_STATUS_INVALID_ARGUMENT;
  *out_payload = nullptr;
  try {
    auto payload = new (std::nothrow) gwipc_control_payload;
    if (!payload) return GWIPC_STATUS_OUT_OF_MEMORY;
    payload->bytes = wire::encode(value);
    T decoded;
    if (wire::decode(payload->bytes, decoded) != wire::CodecStatus::Ok) {
      delete payload;
      return GWIPC_STATUS_INVALID_ARGUMENT;
    }
    *out_payload = payload;
    return GWIPC_STATUS_OK;
  } catch (const std::bad_alloc &) {
    return GWIPC_STATUS_OUT_OF_MEMORY;
  } catch (...) {
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
}

}  // namespace

extern "C" {

gwipc_status gwipc_control_encode_snapshot_begin(
    const gwipc_snapshot_begin *value, gwipc_control_payload **out_payload) {
  if (!valid_input(value)) return GWIPC_STATUS_INVALID_ARGUMENT;
  return make_payload(
      wire::SnapshotBegin{value->snapshot_id,
                          static_cast<wire::SnapshotDomain>(value->domain),
                          value->flags, value->generation,
                          value->expected_item_count},
      out_payload);
}

gwipc_status gwipc_control_encode_snapshot_end(
    const gwipc_snapshot_end *value, gwipc_control_payload **out_payload) {
  if (!valid_input(value)) return GWIPC_STATUS_INVALID_ARGUMENT;
  return make_payload(wire::SnapshotEnd{value->snapshot_id, value->generation,
                                        value->actual_item_count},
                      out_payload);
}

gwipc_status gwipc_control_encode_snapshot_abort(
    const gwipc_snapshot_abort *value, gwipc_control_payload **out_payload) {
  if (!valid_input(value) ||
      (value->detail_length != 0 && value->detail == nullptr))
    return GWIPC_STATUS_INVALID_ARGUMENT;
  try {
    const std::string detail(value->detail ? value->detail : "",
                             value->detail_length);
    return make_payload(
        wire::SnapshotAbort{value->snapshot_id, value->reason, detail},
        out_payload);
  } catch (const std::bad_alloc &) {
    return GWIPC_STATUS_OUT_OF_MEMORY;
  } catch (...) {
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
}

const uint8_t *gwipc_control_payload_data(const gwipc_control_payload *payload,
                                          size_t *out_size) {
  if (out_size) *out_size = payload ? payload->bytes.size() : 0;
  return !payload || payload->bytes.empty() ? nullptr : payload->bytes.data();
}

void gwipc_control_payload_destroy(gwipc_control_payload *payload) {
  delete payload;
}

gwipc_status gwipc_control_decode_message(
    const gwipc_message *message, gwipc_decoded_control **out_control) {
  if (!message || !out_control) return GWIPC_STATUS_INVALID_ARGUMENT;
  *out_control = nullptr;
  size_t size = 0;
  const auto *payload = gwipc_message_payload(message, &size);
  const std::span<const std::uint8_t> bytes(payload, size);
  try {
    auto control = new (std::nothrow) gwipc_decoded_control;
    if (!control) return GWIPC_STATUS_OUT_OF_MEMORY;
    control->type = gwipc_message_type(message);
    wire::CodecStatus status = wire::CodecStatus::InvalidValue;
    switch (control->type) {
      case GWIPC_MESSAGE_SNAPSHOT_BEGIN: {
        wire::SnapshotBegin value;
        status = wire::decode(bytes, value);
        control->value = value;
        break;
      }
      case GWIPC_MESSAGE_SNAPSHOT_END: {
        wire::SnapshotEnd value;
        status = wire::decode(bytes, value);
        control->value = value;
        break;
      }
      case GWIPC_MESSAGE_SNAPSHOT_ABORT: {
        wire::SnapshotAbort value;
        status = wire::decode(bytes, value);
        control->value = std::move(value);
        break;
      }
      default:
        delete control;
        return GWIPC_STATUS_INVALID_ARGUMENT;
    }
    if (status != wire::CodecStatus::Ok) {
      delete control;
      return GWIPC_STATUS_PROTOCOL_ERROR;
    }
    *out_control = control;
    return GWIPC_STATUS_OK;
  } catch (const std::bad_alloc &) {
    return GWIPC_STATUS_OUT_OF_MEMORY;
  } catch (...) {
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
}

uint16_t gwipc_decoded_control_type(const gwipc_decoded_control *control) {
  return control ? control->type : 0;
}

const gwipc_snapshot_begin *gwipc_decoded_snapshot_begin(
    const gwipc_decoded_control *control) {
  if (!control || control->type != GWIPC_MESSAGE_SNAPSHOT_BEGIN) return nullptr;
  const auto &value = std::get<wire::SnapshotBegin>(control->value);
  auto &result = const_cast<gwipc_decoded_control *>(control)->begin;
  result = {sizeof(result), value.snapshot_id,
            static_cast<gwipc_snapshot_domain>(value.domain), value.flags,
            value.generation, value.expected_item_count, {}};
  return &result;
}

const gwipc_snapshot_end *gwipc_decoded_snapshot_end(
    const gwipc_decoded_control *control) {
  if (!control || control->type != GWIPC_MESSAGE_SNAPSHOT_END) return nullptr;
  const auto &value = std::get<wire::SnapshotEnd>(control->value);
  auto &result = const_cast<gwipc_decoded_control *>(control)->end;
  result = {sizeof(result), value.snapshot_id, value.generation,
            value.actual_item_count, {}};
  return &result;
}

const gwipc_snapshot_abort *gwipc_decoded_snapshot_abort(
    const gwipc_decoded_control *control) {
  if (!control || control->type != GWIPC_MESSAGE_SNAPSHOT_ABORT) return nullptr;
  const auto &value = std::get<wire::SnapshotAbort>(control->value);
  auto *mutable_control = const_cast<gwipc_decoded_control *>(control);
  mutable_control->detail = value.detail;
  auto &result = mutable_control->abort;
  result = {sizeof(result), value.snapshot_id, value.reason,
            mutable_control->detail.c_str(), mutable_control->detail.size(), {}};
  return &result;
}

void gwipc_decoded_control_destroy(gwipc_decoded_control *control) {
  delete control;
}

}  // extern "C"
