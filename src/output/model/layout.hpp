#pragma once

#include "output/model/types.hpp"

#include <cstdint>

namespace glasswyrm::output {

enum class LayoutValidationError {
  None,
  TooManyOutputs,
  IncompleteInventory,
  NoEnabledOutput,
  InvalidPrimaryOutput,
  InvalidIdentity,
  InvalidName,
  InvalidDescriptor,
  InvalidPosition,
  OverlappingOutputs,
  InvalidRootExtent,
  InvalidScale,
  UnsupportedTransform,
  InvalidMode,
  InvalidLogicalExtent,
  InvalidDisabledState,
  InvalidGeneration,
  InvalidOutputOrder,
  PhysicalLimitExceeded,
};

struct LayoutValidationResult {
  LayoutValidationError error{LayoutValidationError::None};
  OutputId output_id{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == LayoutValidationError::None;
  }
};

[[nodiscard]] LayoutValidationResult
validate_layout(const OutputLayout &layout);

[[nodiscard]] const char *
layout_validation_error_name(LayoutValidationError error) noexcept;

} // namespace glasswyrm::output
