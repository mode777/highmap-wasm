# Spec Delta

## ADDED Requirements

### Requirement: Direct-kernel oracle for broken GPU wrappers

When a pinned GPU wrapper cannot launch its kernel because an argument is
missing or a kernel name is stale, the parity harness SHALL launch the kernel
directly with the correct arguments, and the parity report SHALL record the
wrapper gap; the port SHALL NOT require an edit to the upstream wrapper.

#### Scenario: Schott kernel is launched with its phase argument

- **WHEN** the parity harness compares `hydraulic_schott`
- **THEN** it binds the kernel's `it` argument directly and runs the same
  erosion, thermal and deposition phases the kernel defines

#### Scenario: Vpipes pass kernels are launched in order

- **WHEN** the parity harness compares `hydraulic_vpipes`
- **THEN** it launches the flux, water, erosion and sediment-transport kernels
  directly, because the pinned wrapper names kernels that no longer exist

#### Scenario: Upstream wrappers stay unmodified

- **WHEN** the change is applied
- **THEN** no existing upstream file is modified and the desktop build still
  configures and builds with the same options as before

#### Scenario: The wrapper gap is visible in the report

- **WHEN** a direct-kernel oracle is used for a variant
- **THEN** the parity report names the broken wrapper and records it as a
  candidate upstream issue
