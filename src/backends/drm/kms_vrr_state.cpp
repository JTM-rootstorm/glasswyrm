#include "backends/drm/kms_vrr_state.hpp"

#include <optional>

namespace glasswyrm::drm {
namespace {

bool test_value(KmsApi &api, const int fd, const KmsVrrState &state,
                const std::span<const AtomicPropertyValue> selected_state,
                const bool enabled, std::string &error) {
  const auto request = make_vrr_atomic_request(selected_state, state, enabled);
  return api.atomic_commit(fd, request, AtomicTestOnly | AtomicAllowModeset,
                           nullptr, error);
}

} // namespace

KmsVrrState probe_kms_vrr_state(
    KmsApi &api, const int fd, const Connector &connector,
    const PipelineIds pipeline, const SavedKmsState &saved,
    const std::span<const AtomicPropertyValue> selected_state) {
  KmsVrrState result;
  result.connector_property_present = connector.vrr_property_present;
  result.hardware_capable = connector.vrr_capable;
  result.atomic_available = saved.atomic;
  result.crtc_property_present = saved.properties.crtc.vrr_enabled.has_value();
  result.crtc_id = pipeline.crtc;
  if (saved.properties.crtc.vrr_enabled) {
    result.crtc_property_id = saved.properties.crtc.vrr_enabled->id;
    result.original_enabled = saved.properties.crtc.vrr_enabled->value != 0;
  }

  if (!result.connector_property_present) {
    result.status = KmsVrrStatus::ConnectorPropertyAbsent;
    result.diagnostic = "DRM connector vrr_capable property is absent";
    return result;
  }
  if (!result.hardware_capable) {
    result.status = KmsVrrStatus::ConnectorNotCapable;
    result.diagnostic = "DRM connector is not VRR capable";
    return result;
  }
  if (!result.atomic_available) {
    result.status = KmsVrrStatus::AtomicUnavailable;
    result.diagnostic = "atomic KMS is unavailable";
    return result;
  }
  if (!result.crtc_property_present) {
    result.status =
        saved.properties.crtc.vrr_status == OptionalVrrPropertyStatus::Absent
            ? KmsVrrStatus::CrtcPropertyAbsent
            : KmsVrrStatus::CrtcPropertyInvalid;
    result.diagnostic =
        result.status == KmsVrrStatus::CrtcPropertyAbsent
            ? "CRTC VRR_ENABLED property is absent"
            : "CRTC VRR_ENABLED property metadata is invalid";
    return result;
  }

  std::string operation_error;
  if (!test_value(api, fd, result, selected_state, false,
                  operation_error)) {
    result.status = KmsVrrStatus::TestOffRejected;
    result.diagnostic = "VRR_ENABLED=0 TEST_ONLY failed: " + operation_error;
    return result;
  }
  result.test_off_passed = true;
  if (!test_value(api, fd, result, selected_state, true,
                  operation_error)) {
    result.status = KmsVrrStatus::TestOnRejected;
    result.diagnostic = "VRR_ENABLED=1 TEST_ONLY failed: " + operation_error;
    return result;
  }
  result.test_on_passed = true;
  result.controllable = true;
  result.status = KmsVrrStatus::Controllable;
  return result;
}

std::vector<AtomicPropertyValue> make_vrr_atomic_request(
    const std::span<const AtomicPropertyValue> selected_state,
    const KmsVrrState &state, const bool enabled) {
  std::vector<AtomicPropertyValue> result(selected_state.begin(),
                                          selected_state.end());
  if (!state.crtc_property_present || state.crtc_property_id == 0)
    return result;

  AtomicPropertyValue *found = nullptr;
  for (auto &property : result) {
    if (property.object_id != state.crtc_id ||
        property.property_id != state.crtc_property_id)
      continue;
    if (found == nullptr)
      found = &property;
  }
  if (found != nullptr) {
    found->value = enabled ? 1U : 0U;
  } else {
    result.push_back(
        {state.crtc_id, state.crtc_property_id, enabled ? 1U : 0U});
  }
  return result;
}

bool read_kms_vrr_enabled(KmsApi &api, const int fd,
                          const PipelineIds pipeline,
                          const KmsVrrState &state, bool &enabled,
                          std::string &error) {
  if (!state.crtc_property_present || state.crtc_property_id == 0) {
    error = "CRTC VRR_ENABLED property is unavailable";
    return false;
  }
  std::vector<ObjectProperty> properties;
  if (!api.object_properties(fd, KmsObjectType::Crtc, pipeline.crtc,
                             properties, error))
    return false;
  const ObjectProperty *found = nullptr;
  for (const auto &property : properties) {
    if (property.name != "VRR_ENABLED")
      continue;
    if (found != nullptr) {
      error = "CRTC VRR_ENABLED property is duplicated during readback";
      return false;
    }
    found = &property;
  }
  if (found == nullptr || found->id != state.crtc_property_id ||
      found->value > 1) {
    error = "CRTC VRR_ENABLED property changed during readback";
    return false;
  }
  enabled = found->value != 0;
  error.clear();
  return true;
}

bool verify_kms_vrr_enabled(KmsApi &api, const int fd,
                            const PipelineIds pipeline,
                            const KmsVrrState &state, const bool expected,
                            std::string &error) {
  bool enabled{};
  if (!read_kms_vrr_enabled(api, fd, pipeline, state, enabled, error))
    return false;
  if (enabled != expected) {
    error = std::string("CRTC VRR_ENABLED readback mismatch: expected ") +
            (expected ? "1" : "0") + ", observed " +
            (enabled ? "1" : "0");
    return false;
  }
  return true;
}

const char *kms_vrr_status_name(const KmsVrrStatus status) noexcept {
  switch (status) {
  case KmsVrrStatus::ConnectorPropertyAbsent:
    return "connector-property-absent";
  case KmsVrrStatus::ConnectorNotCapable:
    return "connector-not-capable";
  case KmsVrrStatus::AtomicUnavailable:
    return "atomic-unavailable";
  case KmsVrrStatus::CrtcPropertyAbsent:
    return "crtc-property-absent";
  case KmsVrrStatus::CrtcPropertyInvalid:
    return "crtc-property-invalid";
  case KmsVrrStatus::TestOffRejected:
    return "test-off-rejected";
  case KmsVrrStatus::TestOnRejected:
    return "test-on-rejected";
  case KmsVrrStatus::Controllable:
    return "controllable";
  }
  return "unknown";
}

} // namespace glasswyrm::drm
