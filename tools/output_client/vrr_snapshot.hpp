#pragma once

#include "output_client/output_client.hpp"

namespace glasswyrm::tools::output_client {

enum class VrrRecordResult { NotVrr, Consumed, Invalid };

[[nodiscard]] VrrRecordResult consume_vrr_snapshot_record(
    std::uint16_t type, const gwipc_decoded_contract *decoded,
    Snapshot &snapshot, std::string &error);
[[nodiscard]] bool validate_vrr_snapshot(const Snapshot &snapshot,
                                         std::string &error);

}  // namespace glasswyrm::tools::output_client
