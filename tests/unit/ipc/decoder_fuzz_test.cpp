#include "ipc/wire/compositor_contract.hpp"
#include "ipc/wire/control.hpp"
#include "ipc/wire/envelope.hpp"
#include "ipc/wire/lifecycle_contract.hpp"
#include "ipc/wire/policy_contract.hpp"
#include "tests/helpers/test_support.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using gw::test::require;

std::uint32_t next(std::uint32_t &state) {
  state = state * 1664525U + 1013904223U;
  return state;
}

} // namespace

int main() {
  using namespace gw::ipc::wire;

  Hello valid_hello;
  valid_hello.sender_role = Role::TestProducer;
  valid_hello.maximum_payload = 4096;
  valid_hello.maximum_fd_count = 1;
  valid_hello.sender_instance_id[0] = 1;
  const auto hello_bytes = encode(valid_hello);
  Hello hello_result;
  for (std::size_t size = 0; size < hello_bytes.size(); ++size) {
    require(decode(std::span(hello_bytes).first(size), hello_result) !=
                CodecStatus::Ok,
            "truncated control records are never accepted");
  }
  auto trailed_hello = hello_bytes;
  trailed_hello.push_back(0);
  require(decode(trailed_hello, hello_result) == CodecStatus::TrailingData,
          "trailed control records are rejected deterministically");

  OutputUpsert valid_output;
  valid_output.output_id = 1;
  const auto output_bytes = encode(valid_output);
  OutputUpsert output_result;
  for (std::size_t size = 0; size < output_bytes.size(); ++size) {
    require(decode(std::span(output_bytes).first(size), output_result) !=
                CodecStatus::Ok,
            "truncated contract records are never accepted");
  }
  auto mutated_output = output_bytes;
  mutated_output[8] = 2;
  require(decode(mutated_output, output_result) == CodecStatus::InvalidValue,
          "mutated contract booleans are rejected deterministically");
  auto trailed_output = output_bytes;
  trailed_output.push_back(0);
  require(decode(trailed_output, output_result) == CodecStatus::TrailingData,
          "trailed contract records are rejected deterministically");

  Envelope valid_envelope;
  valid_envelope.sequence = 1;
  valid_envelope.payload_size = 4;
  const auto header = encode_envelope(valid_envelope);
  std::vector<std::uint8_t> record(header.begin(), header.end());
  record.insert(record.end(), 4, 0);
  Envelope envelope_result;
  require(decode_envelope(record, 0, 4, envelope_result) == CodecStatus::Ok,
          "known-good fuzz seed envelope decodes");
  for (std::size_t size = 0; size < record.size(); ++size) {
    require(decode_envelope(std::span(record).first(size), 0, 4,
                            envelope_result) != CodecStatus::Ok,
            "truncated envelope records are never accepted");
  }
  auto mutated_envelope = record;
  mutated_envelope[22] = 1;
  require(decode_envelope(mutated_envelope, 0, 4, envelope_result) ==
              CodecStatus::InvalidValue,
          "mutated reserved envelope fields are rejected deterministically");

  std::uint32_t state = 0x47574950U;
  for (std::size_t iteration = 0; iteration < 4096; ++iteration) {
    std::vector<std::uint8_t> bytes(next(state) % 257U);
    for (auto &byte : bytes) {
      byte = static_cast<std::uint8_t>(next(state));
    }
    const std::span<const std::uint8_t> input(bytes);
    Envelope envelope;
    Hello hello;
    Welcome welcome;
    Reject reject;
    ProtocolError error;
    SnapshotBegin begin;
    SnapshotEnd end;
    SnapshotAbort abort;
    OutputUpsert output;
    SurfaceUpsert surface;
    BufferAttach buffer;
    SurfaceDamage damage;
    FrameAcknowledged acknowledged;
    PolicyWindowUpsert policy_window;
    PolicyWindowState policy_state;
    PolicyLifecycleWindowUpsert lifecycle_window;
    SurfacePolicyUpsert surface_policy;
    (void)decode_envelope(input, next(state) % 18U, next(state) % 1048577U,
                          envelope);
    (void)decode(input, hello);
    (void)decode(input, welcome);
    (void)decode(input, reject);
    (void)decode(input, error);
    (void)decode(input, begin);
    (void)decode(input, end);
    (void)decode(input, abort);
    (void)decode(input, output);
    (void)decode(input, surface);
    (void)decode(input, buffer);
    (void)decode(input, damage);
    (void)decode(input, acknowledged);
    (void)decode(input, policy_window);
    (void)decode(input, policy_state);
    (void)decode(input, lifecycle_window);
    (void)decode(input, surface_policy);
    require(damage.rectangles.size() <= kMaximumDamageRectangles,
            "random damage decoding stays within its allocation bound");
    require(hello.name.size() <= kMaximumInstanceLabel &&
                reject.detail.size() <= kMaximumDiagnosticString &&
                error.detail.size() <= kMaximumDiagnosticString &&
                abort.detail.size() <= kMaximumDiagnosticString,
            "random control decoding keeps every variable field bounded");
  }
  return 0;
}
