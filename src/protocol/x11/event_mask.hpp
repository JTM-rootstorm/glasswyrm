#pragma once

#include <cstdint>

namespace gw::protocol::x11::event_mask {

inline constexpr std::uint32_t KeyPress = 1U << 0U;
inline constexpr std::uint32_t KeyRelease = 1U << 1U;
inline constexpr std::uint32_t ButtonPress = 1U << 2U;
inline constexpr std::uint32_t ButtonRelease = 1U << 3U;
inline constexpr std::uint32_t EnterWindow = 1U << 4U;
inline constexpr std::uint32_t LeaveWindow = 1U << 5U;
inline constexpr std::uint32_t PointerMotion = 1U << 6U;
inline constexpr std::uint32_t PointerMotionHint = 1U << 7U;
inline constexpr std::uint32_t Button1Motion = 1U << 8U;
inline constexpr std::uint32_t Button2Motion = 1U << 9U;
inline constexpr std::uint32_t Button3Motion = 1U << 10U;
inline constexpr std::uint32_t Button4Motion = 1U << 11U;
inline constexpr std::uint32_t Button5Motion = 1U << 12U;
inline constexpr std::uint32_t ButtonMotion = 1U << 13U;
inline constexpr std::uint32_t KeymapState = 1U << 14U;
inline constexpr std::uint32_t Exposure = 1U << 15U;
inline constexpr std::uint32_t VisibilityChange = 1U << 16U;
inline constexpr std::uint32_t StructureNotify = 1U << 17U;
inline constexpr std::uint32_t ResizeRedirect = 1U << 18U;
inline constexpr std::uint32_t SubstructureNotify = 1U << 19U;
inline constexpr std::uint32_t SubstructureRedirect = 1U << 20U;
inline constexpr std::uint32_t FocusChange = 1U << 21U;
inline constexpr std::uint32_t PropertyChange = 1U << 22U;
inline constexpr std::uint32_t ColormapChange = 1U << 23U;
inline constexpr std::uint32_t OwnerGrabButton = 1U << 24U;
inline constexpr std::uint32_t All = 0x01ffffffU;
inline constexpr std::uint32_t DoNotPropagate = KeyPress | KeyRelease |
    ButtonPress | ButtonRelease | PointerMotion | ButtonMotion;

}  // namespace gw::protocol::x11::event_mask

namespace gw::protocol::x11::state_mask {
inline constexpr std::uint16_t Shift = 1U << 0U;
inline constexpr std::uint16_t Lock = 1U << 1U;
inline constexpr std::uint16_t Control = 1U << 2U;
inline constexpr std::uint16_t Mod1 = 1U << 3U;
inline constexpr std::uint16_t Mod2 = 1U << 4U;
inline constexpr std::uint16_t Mod3 = 1U << 5U;
inline constexpr std::uint16_t Mod4 = 1U << 6U;
inline constexpr std::uint16_t Mod5 = 1U << 7U;
inline constexpr std::uint16_t Button1 = 1U << 8U;
inline constexpr std::uint16_t Button2 = 1U << 9U;
inline constexpr std::uint16_t Button3 = 1U << 10U;
inline constexpr std::uint16_t Button4 = 1U << 11U;
inline constexpr std::uint16_t Button5 = 1U << 12U;
}  // namespace gw::protocol::x11::state_mask
