# Spec Delta

## ADDED Requirements

### Requirement: Baseline tracks the shipped codegen profile

The committed baseline in `docs/benchmarks.md` SHALL be measured from an
artifact built with the repository's default codegen profile, and the
reference run SHALL state the artifact size and profile the rows were measured
with. When the default profile changes, the change SHALL produce a new full
baseline for the new artifact and SHALL retain the previous baseline as a
dated historical comparison.

#### Scenario: Baseline matches the default profile

- **WHEN** a change alters the default wasm codegen profile
- **THEN** the completed change updates `docs/benchmarks.md` with a full
  baseline measured from the new default artifact and records that artifact's
  size

#### Scenario: Previous baseline is preserved

- **WHEN** a new baseline replaces the reference table
- **THEN** the prior table remains in the document, dated and labeled as
  historical, instead of being silently overwritten

#### Scenario: Attributable profile

- **WHEN** a reader consults a baseline row
- **THEN** the document states the codegen profile, machine, Node version, git
  commit and wasm artifact size used for that measurement
