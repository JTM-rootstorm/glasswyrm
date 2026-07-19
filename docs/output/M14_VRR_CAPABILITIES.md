# Milestone 14 VRR capabilities

Milestone 14 separates display capability from userspace control and observed
behavior. No single boolean is called "VRR support."

## Capability states

`hardware_capable` is true only when the selected DRM connector exposes the
standard `vrr_capable` property with value one. Property absence is
unsupported; a non-boolean value is malformed and cannot be promoted.

`kms_controllable` additionally requires atomic KMS, the selected CRTC's
standard `VRR_ENABLED` property, and successful TEST_ONLY atomic commits for
both zero and one. Legacy KMS may report the connector capability but always
reports controllability false and never probes or mutates the optional CRTC
property.

`desired_enabled` is the compositor decision before submission.
`effective_enabled` is the successfully completed and read-back CRTC value.
`hardware_behavior_confirmed` is acceptance evidence derived from kernel
page-flip intervals; it is not inferred from property acceptance.

## Inventory

An M14 output inventory contains one VRR capability and policy record per
output, followed by committed state and latest timing when available. The
records include:

- connector-property presence and value;
- KMS controllability and simulation status;
- optional minimum and maximum refresh in millihertz;
- requested policy mode;
- desired and effective state;
- selected window and surface;
- deterministic reason flags;
- transition serial, kernel timestamp, and latest interval.

The server validates exact record counts, stable output identities, enum and
boolean domains, reason masks, generations, and snapshot ordering before
promoting inventory. A compositor reconnect must reproduce the committed
inventory and resume a retained in-flight transaction against the same staged
state.

## Headless simulation

`gwcomp --headless-vrr NAME=MIN-MAX` enables a bounded simulated range for an
existing named headless output. The range is expressed in millihertz. A
simulated output reports connector-property presence, controllability, and
range availability, but deliberately reports `hardware_capable=false` and the
`simulated-headless` reason. State transitions complete synchronously and
timing advances on a deterministic synthetic timeline; the backend does not
sleep or claim physical behavior.

## Historical and unsupported paths

VRR records are emitted only when the complete M14 capability profile is
negotiated. Historical sessions retain their exact M13 inventory and scene
shape. An incapable DRM output continues ordinary fixed-refresh presentation,
and non-Off policy changes are rejected with explicit capability reasons.
QXL is the required incapable hardware profile and is never treated as
positive VRR evidence.
