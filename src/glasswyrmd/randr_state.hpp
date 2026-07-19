#pragma once

#include "glasswyrmd/resource_table.hpp"
#include "output/model/layout.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace glasswyrm::server {

inline constexpr std::uint32_t kRandROutputId = 0x00000100U;
inline constexpr std::uint32_t kRandRCrtcId = 0x00000101U;
inline constexpr std::uint32_t kRandRModeId = 0x00000102U;
inline constexpr std::uint32_t kRandRConfigurationTimestamp = 1U;
inline constexpr std::uint16_t kRandRRotate0 = 1U;
inline constexpr std::uint16_t kRandRSupportedNotifyMask = 0x000fU;

struct RandRSelection {
  ClientId client{};
  std::uint32_t window{};
  std::uint16_t mask{};
};

struct RandRModeObject {
  std::uint64_t internal_id{};
  std::uint32_t xid{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t refresh_millihertz{};
  std::uint32_t flags{};
  std::string name;
  bool preferred{};
  bool current{};
};

struct RandROutputObject {
  std::uint64_t internal_id{};
  std::uint32_t xid{};
  std::uint32_t crtc_xid{};
  std::string name;
  bool connected{};
  bool enabled{};
  bool primary{};
  std::uint32_t physical_width_mm{};
  std::uint32_t physical_height_mm{};
  std::int32_t logical_x{};
  std::int32_t logical_y{};
  std::uint32_t logical_width{};
  std::uint32_t logical_height{};
  std::uint32_t physical_width{};
  std::uint32_t physical_height{};
  output::RationalScale scale{};
  output::OutputTransform transform{output::OutputTransform::Normal};
  std::vector<RandRModeObject> modes;
};

class RandRState {
 public:
  [[nodiscard]] bool configure_output_layout(
      const output::OutputLayout& layout);
  [[nodiscard]] bool output_model_enabled() const noexcept {
    return layout_.has_value();
  }
  [[nodiscard]] const output::OutputLayout* output_layout() const noexcept {
    return layout_ ? &*layout_ : nullptr;
  }
  [[nodiscard]] const std::vector<RandROutputObject>& outputs() const noexcept {
    return outputs_;
  }
  [[nodiscard]] const RandROutputObject* find_output(
      std::uint32_t xid) const noexcept;
  [[nodiscard]] const RandROutputObject* find_crtc(
      std::uint32_t xid) const noexcept;
  [[nodiscard]] const RandRModeObject* find_mode(
      std::uint32_t xid) const noexcept;
  [[nodiscard]] std::uint32_t primary_output_xid() const noexcept;
  [[nodiscard]] std::uint32_t configuration_timestamp() const noexcept;

  [[nodiscard]] bool select(ClientId client, std::uint32_t window,
                            std::uint16_t mask);
  [[nodiscard]] std::uint16_t selection(ClientId client,
                                        std::uint32_t window) const noexcept;
  [[nodiscard]] const std::vector<RandRSelection>& selections() const noexcept {
    return selections_;
  }
  [[nodiscard]] std::size_t clear_client(ClientId client) noexcept;
  [[nodiscard]] std::size_t clear_window(std::uint32_t window) noexcept;
  [[nodiscard]] std::size_t prune_windows(
      const ResourceTable& resources) noexcept;

 private:
  struct ObjectIds {
    std::uint32_t output{};
    std::uint32_t crtc{};
  };

  std::optional<output::OutputLayout> layout_;
  std::vector<RandROutputObject> outputs_;
  std::map<std::uint64_t, ObjectIds> output_xids_;
  std::map<std::uint64_t, std::uint32_t> mode_xids_;
  std::uint32_t next_xid_{kRandROutputId};
  std::vector<RandRSelection> selections_;
};

}  // namespace glasswyrm::server
