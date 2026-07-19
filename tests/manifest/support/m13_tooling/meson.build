m13_fixture_validator = find_program(
  tests_source_dir / 'compat/m13/validate_fixtures.py',
)
m13_fixture_root = tests_source_dir / 'fixtures/m13'
test(
  'm13-fixture-bootstrap',
  m13_fixture_validator,
  args: [m13_fixture_root],
  timeout: 10,
)
test(
  'm13-fixture-complete',
  m13_fixture_validator,
  args: [m13_fixture_root, '--require-complete'],
  timeout: 10,
)

m13_fixture_validator_test = find_program(
  tests_source_dir / 'compat/m13/validate_fixtures_test.py',
)
test(
  'm13-fixture-validator',
  m13_fixture_validator_test,
  args: [
    m13_fixture_validator.full_path(),
    tests_source_dir / 'fixtures/m13',
  ],
  timeout: 10,
)

m13_fixture_promoter = find_program(
  tests_source_dir / 'compat/m13/promote_fixtures.py',
)
m13_fixture_promoter_test = find_program(
  tests_source_dir / 'compat/m13/promote_fixtures_test.py',
)
test(
  'm13-fixture-promotion',
  m13_fixture_promoter_test,
  args: [
    m13_fixture_promoter.full_path(),
    tests_source_dir / 'fixtures/m13',
    m13_fixture_validator.full_path(),
  ],
  timeout: 10,
)

m13_evidence_validator = find_program(
  tests_source_dir / 'compat/m13/validate_evidence.py',
)
m13_evidence_validator_test = find_program(
  tests_source_dir / 'compat/m13/validate_evidence_test.py',
)
test(
  'm13-evidence-validator',
  m13_evidence_validator_test,
  args: [m13_evidence_validator.full_path()],
  timeout: 10,
)

m13_vm_cli_contract_test = find_program(
  tests_source_dir / 'tools/m13_vm_cli_contract_test.sh',
)
test(
  'm13-vm-cli-contract',
  m13_vm_cli_contract_test,
  args: [meson.project_source_root()],
  timeout: 15,
)

m13_frame_comparator = find_program(
  tests_source_dir / 'compat/m13/compare_output_frames.py',
)
m13_frame_comparator_test = find_program(
  tests_source_dir / 'compat/m13/compare_output_frames_test.py',
)

m13_frame_set_validator = find_program(
  tests_source_dir / 'compat/m13/validate_frame_sets.py',
)
m13_frame_set_validator_test = find_program(
  tests_source_dir / 'compat/m13/validate_frame_sets_test.py',
)
test(
  'm13-frame-set-validator',
  m13_frame_set_validator_test,
  args: [m13_frame_set_validator.full_path()],
  timeout: 10,
)
test(
  'm13-frame-comparator',
  m13_frame_comparator_test,
  args: [m13_frame_comparator.full_path()],
  timeout: 10,
)

m13_raw_output_probe = find_program(
  tests_source_dir / 'compat/m13/m13_raw_output_probe.py',
)
test(
  'm13-raw-output-probe-help',
  m13_raw_output_probe,
  args: ['--help'],
  timeout: 10,
)

m13_legacy_client = find_program(
  tests_source_dir / 'compat/m13/m13_legacy_client.py',
)
test(
  'm13-legacy-client-help',
  m13_legacy_client,
  args: ['--help'],
  timeout: 10,
)
