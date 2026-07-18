#include "glasswyrmd/extension_registry.hpp"

#include <algorithm>

namespace glasswyrm::server {

const ExtensionDescriptor* find_extension(const std::string_view name) noexcept {
  const auto found = std::ranges::find(kExtensionRegistry, name,
                                       &ExtensionDescriptor::name);
  return found == kExtensionRegistry.end() ? nullptr : &*found;
}

const ExtensionDescriptor*
find_extension(const std::uint8_t major_opcode) noexcept {
  const auto found = std::ranges::find(kExtensionRegistry, major_opcode,
                                       &ExtensionDescriptor::major_opcode);
  return found == kExtensionRegistry.end() ? nullptr : &*found;
}

const ExtensionDescriptor* find_extension(const ExtensionKind kind) noexcept {
  const auto found =
      std::ranges::find(kExtensionRegistry, kind, &ExtensionDescriptor::kind);
  return found == kExtensionRegistry.end() ? nullptr : &*found;
}

bool known_extension_name(const std::string_view name) noexcept {
  return find_extension(name) != nullptr;
}

ExtensionRegistry::ExtensionRegistry(
    const bool enabled, const std::span<const std::string> disabled)
    : ExtensionRegistry(enabled ? ExtensionCapability::GameCompat
                                : ExtensionCapability::None,
                        disabled) {}

ExtensionRegistry::ExtensionRegistry(
    const ExtensionCapability capabilities,
    const std::span<const std::string> disabled)
    : capabilities_(capabilities) {
  for (const auto& name : disabled) {
    const auto* extension = find_extension(std::string_view{name});
    if (extension)
      disabled_[static_cast<std::size_t>(extension - kExtensionRegistry.data())] =
          true;
  }
}

bool ExtensionRegistry::enabled(const std::string_view name) const noexcept {
  return query(name) != nullptr;
}

bool ExtensionRegistry::enabled(const std::uint8_t major_opcode) const noexcept {
  return query(major_opcode) != nullptr;
}

const ExtensionDescriptor*
ExtensionRegistry::query(const std::string_view name) const noexcept {
  const auto* extension = find_extension(name);
  if (!extension ||
      !has_extension_capability(capabilities_, extension->capability))
    return nullptr;
  const auto index =
      static_cast<std::size_t>(extension - kExtensionRegistry.data());
  return disabled_[index] ? nullptr : extension;
}

const ExtensionDescriptor*
ExtensionRegistry::query(const std::uint8_t major_opcode) const noexcept {
  const auto* extension = find_extension(major_opcode);
  if (!extension ||
      !has_extension_capability(capabilities_, extension->capability))
    return nullptr;
  const auto index =
      static_cast<std::size_t>(extension - kExtensionRegistry.data());
  return disabled_[index] ? nullptr : extension;
}

std::vector<std::string_view> ExtensionRegistry::enabled_names() const {
  std::vector<std::string_view> result;
  result.reserve(kExtensionRegistry.size());
  for (std::size_t index = 0; index < kExtensionRegistry.size(); ++index)
    if (!disabled_[index] && has_extension_capability(
                                 capabilities_,
                                 kExtensionRegistry[index].capability))
      result.push_back(kExtensionRegistry[index].name);
  return result;
}

}  // namespace glasswyrm::server
