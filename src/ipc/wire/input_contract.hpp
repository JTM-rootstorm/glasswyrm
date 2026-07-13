#pragma once
#include "ipc/wire/types.hpp"
#include <cstdint>
#include <span>
#include <vector>
namespace gw::ipc::wire {
enum class SyntheticInputResult : std::uint16_t { Accepted=1, Clamped, InvalidTransition, FocusUnchanged, FocusRejected, LimitExceeded };
struct SyntheticMotion { std::uint64_t input_id{}; std::uint32_t time_ms{}; std::int32_t root_x{}, root_y{}; std::uint32_t flags{}; };
struct SyntheticButton { std::uint64_t input_id{}; std::uint32_t time_ms{}; std::uint8_t button{}, pressed{}; std::uint16_t reserved16{}; std::uint32_t flags{}; };
struct SyntheticKey { std::uint64_t input_id{}; std::uint32_t time_ms{}; std::uint8_t keycode{}, pressed{}; std::uint16_t reserved16{}; std::uint32_t flags{}; };
struct SyntheticBarrier { std::uint64_t input_id{}; std::uint32_t flags{}; };
struct SyntheticInputAcknowledged { std::uint64_t input_id{}; std::uint32_t time_ms{}; SyntheticInputResult result{SyntheticInputResult::Accepted}; std::int32_t root_x{},root_y{}; std::uint32_t pointer_window{},focus_window{}; std::uint16_t state{},reserved16{}; std::uint32_t delivered_event_count{},flags{}; };
#define GWIPC_INPUT_CODEC(T) std::vector<std::uint8_t> encode(const T&); CodecStatus decode(std::span<const std::uint8_t>, T&)
GWIPC_INPUT_CODEC(SyntheticMotion); GWIPC_INPUT_CODEC(SyntheticButton); GWIPC_INPUT_CODEC(SyntheticKey); GWIPC_INPUT_CODEC(SyntheticBarrier); GWIPC_INPUT_CODEC(SyntheticInputAcknowledged);
#undef GWIPC_INPUT_CODEC
}
