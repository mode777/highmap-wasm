# Spec Delta

## ADDED Requirements

### Requirement: Rifts and advection ports join the curated source list

The curated wasm source list SHALL include the new rifts and advection CPU port
translation units, and the desktop build SHALL compile the same files without
OpenCL, conditional compilation or edits to existing upstream CMake lists.

#### Scenario: Wasm build compiles the new ports without OpenCL

- **WHEN** the wasm build is configured with the rifts and advection port
  sources added
- **THEN** both translation units compile with no `gpu_opencl/`, `*_gpu.cpp`
  or OpenCL header on the include path

#### Scenario: Desktop build compiles the same sources

- **WHEN** the desktop build is configured after the port files are added
- **THEN** its source glob picks up both port translation units and the
  resulting library contains the CPU and GPU forms of all three functions

### Requirement: Parity CI runs the rifts and advection suite

The parity job SHALL run the rifts/advection suite alongside the thermal and
hydraulic suites in the same invocation, with a timeout that covers it, SHALL
fail when the suite produces no parity metric lines or when a case is skipped
for any reason other than the recorded image-format limitation, and SHALL
publish its metrics with the existing suites'.

#### Scenario: All three suites run in one job

- **WHEN** the parity job runs on a runner with an OpenCL device
- **THEN** the thermal, hydraulic and rifts/advection suites all execute and
  each produces parity metric lines

#### Scenario: Missing metrics fail the job

- **WHEN** the rifts/advection suite completes without producing parity metric
  lines
- **THEN** the job exits non-zero instead of reporting success

#### Scenario: Unexpected skip fails the job

- **WHEN** a rifts/advection case is skipped for a reason other than a device
  that supports neither `CL_R` nor `CL_RGBA` float images
- **THEN** the job exits non-zero and names the skipped case

#### Scenario: Rifts and advection metrics are published

- **WHEN** the parity job finishes
- **THEN** the rifts/advection percentile and differing-cell-fraction metrics
  are available in the downloadable artifact and the job summary

### Requirement: Direct-kernel oracles for particle and warp advection

Particle-advection parity SHALL compare against a direct kernel launch whose
accumulation and count buffers are explicitly zeroed before execution, and
warp-advection parity SHALL use a `CL_R` image first and fall back to `CL_RGBA`
when the device lacks that format, rather than skipping the case.

#### Scenario: Particle accumulation starts from zeroed buffers

- **WHEN** the parity harness launches the particle-advection kernel directly
- **THEN** the accumulation and count buffers are zeroed before the launch,
  because the pinned wrapper never uploads them

#### Scenario: Warp falls back to a supported image format

- **WHEN** the device cannot create `CL_R` float images for warp advection
- **THEN** the harness re-runs the comparison with `CL_RGBA` images before any
  skip is recorded

#### Scenario: The oracle and image format are recorded

- **WHEN** a direct-kernel oracle or an image-format fallback is used
- **THEN** the parity report names the wrapper gap and the image format that
  produced the measured metrics
