#include <glasswyrm/ipc.h>

#include <cstdint>

int main() {
  static_assert(GWIPC_VRR_POLICY_ALWAYS_ELIGIBLE == 5);
  static_assert(GWIPC_VRR_PREFERENCE_PREFER == 3);
  static_assert(GWIPC_VRR_DECISION_REJECTED == 4);
  static_assert(GWIPC_VRR_REASON_MANUAL_ALWAYS_ELIGIBLE ==
                (UINT64_C(1) << 32));
  static_assert(GWIPC_VRR_KNOWN_REASON_MASK ==
                ((UINT64_C(1) << 33) - UINT64_C(1)));

  const auto api = gwipc_get_api_version();
  if (api.major != 0 || api.minor < 9 || api.patch != 0)
    return 1;

  gwipc_presentation_timing timing{};
  timing.struct_size = sizeof(timing);
  timing.output_id = 1;
  timing.commit_id = 2;
  timing.presented_generation = 3;
  timing.flip_sequence = 4;
  timing.flags = GWIPC_PRESENTATION_TIMING_SIMULATED;
  timing.kernel_timestamp_nanoseconds = UINT64_C(50000000);
  timing.interval_nanoseconds = UINT64_C(16666667);
  timing.effective_vrr_enabled = 1;
  timing.timestamp_available = 1;

  gwipc_contract_payload *payload = nullptr;
  if (gwipc_contract_encode_presentation_timing(&timing, &payload) !=
      GWIPC_STATUS_OK)
    return 1;
  gwipc_contract_payload_destroy(payload);
  return 0;
}
