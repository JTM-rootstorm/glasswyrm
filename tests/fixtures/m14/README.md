# Milestone 14 deterministic fixtures

These files are host-side acceptance evidence for variable refresh rate policy,
the experimental `GW_VRR` protocol, and the `gwinfo`/`gwout` control surface.
They are deterministic fixtures only; none claims physical display behavior.

The little- and big-endian protocol fixtures are regenerated logically by
`m14_gw_vrr_fixture_test`. The policy fixtures are regenerated logically by
`m14_policy_fixture_test`. The tool fixtures must match output from the bounded
fake output-control server. `SHA256SUMS` protects every fixture in this folder.
