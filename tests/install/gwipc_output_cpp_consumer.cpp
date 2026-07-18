#include <glasswyrm/ipc.h>

#include <array>
#include <cstdint>

int main() {
  static_assert(GWIPC_OUTPUT_CONFIGURATION_ACCEPTED == 1);
  static_assert(GWIPC_OUTPUT_CONFIGURATION_INTERNAL_ERROR == 12);
  static_assert(GWIPC_OUTPUT_TRANSFORM_FLIPPED_270 ==
                (UINT32_C(1) << GWIPC_TRANSFORM_FLIPPED_270));
  static_assert(GWIPC_MAXIMUM_OUTPUT_PHYSICAL_PIXELS == UINT64_C(16777216));
  static_assert(GWIPC_MAXIMUM_TOTAL_OUTPUT_PIXELS == UINT64_C(67108864));

  const auto api = gwipc_get_api_version();
  if (api.major != 0 || api.minor < 8 || api.patch != 0)
    return 1;

  constexpr std::array<std::uint64_t, 2> membership{1, 2};
  gwipc_surface_output_state surface{};
  surface.struct_size = sizeof(surface);
  surface.surface_id = 9;
  surface.primary_output_id = membership[0];
  surface.output_ids = membership.data();
  surface.output_count = membership.size();
  surface.preferred_scale_numerator = 5;
  surface.preferred_scale_denominator = 4;
  surface.client_buffer_scale = 2;
  surface.scale_mode = GWIPC_SURFACE_SCALE_SCALED_PIXMAP;
  surface.layout_generation = 7;

  gwipc_output_configuration_commit commit{};
  commit.struct_size = sizeof(commit);
  commit.configuration_id = 42;
  commit.base_generation = 7;
  commit.primary_output_id = 1;
  gwipc_contract_payload *payload = nullptr;
  if (gwipc_contract_encode_output_configuration_commit(&commit, &payload) !=
      GWIPC_STATUS_OK)
    return 1;
  gwipc_contract_payload_destroy(payload);
  return surface.output_count == 2 ? 0 : 1;
}
