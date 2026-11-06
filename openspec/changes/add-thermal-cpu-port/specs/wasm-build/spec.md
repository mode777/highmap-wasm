# Spec Delta

## MODIFIED Requirements

### Requirement: Upstream sources remain unmodified

The wasm port SHALL be implemented additively: no existing file under
`HighMap/src`, `HighMap/include`, `HighMap/CMakeLists.txt`, or the upstream
root `CMakeLists.txt` MUST be edited, and the desktop OpenCL build MUST keep
configuring and building exactly as before. New files MAY be added under
`HighMap/src` and `HighMap/include` to house CPU ports of GPU-only algorithms,
provided every addition is a new file, is listed in the curated wasm source
list, and is recorded in the project decision log.

#### Scenario: Desktop build unaffected

- **WHEN** the upstream desktop CMake build is configured and built after the
  wasm port files have been added
- **THEN** it configures and builds successfully with the same options as
  before the change

#### Scenario: No upstream file changes in version control

- **WHEN** the wasm port's files are inspected in version control
- **THEN** no existing upstream file shows modifications relative to upstream,
  except changes explicitly recorded as flagged upstream bug fixes in the
  project decision log, and the only added upstream-tree files are the recorded
  CPU ports with their headers

## ADDED Requirements

### Requirement: Ported algorithms compile into both builds

CPU ports of GPU-only algorithms SHALL be listed in the curated wasm source list
and SHALL be compiled by the desktop build from the same translation units, so
the shipped artifact and the parity oracle run identical source. The ports MUST
NOT require OpenCL, Emscripten-only APIs, or conditional compilation.

#### Scenario: Wasm build stays free of OpenCL for ported algorithms

- **WHEN** the wasm build is configured with the port sources added
- **THEN** the build compiles the port translation units without `gpu_opencl/`,
  `*_gpu.cpp`, or any OpenCL header being present on the include path

#### Scenario: Desktop build compiles the same sources

- **WHEN** the desktop build is configured after the port files are added
- **THEN** its source glob picks up the port translation units and the resulting
  library contains both the GPU and the CPU forms of each ported variant

### Requirement: GPU parity is enforced by a CI job

An automated job SHALL build the desktop library with a CPU OpenCL
implementation, run the parity suite for the ported algorithms, and fail when a
per-variant tolerance is exceeded. The job SHALL publish the measured error
metrics as an artifact.

#### Scenario: Divergent port fails the build

- **WHEN** a ported variant produces results outside its tolerance bound
- **THEN** the parity job exits non-zero and the failing variant is named in the
  log

#### Scenario: Metrics are retained

- **WHEN** the parity job runs successfully
- **THEN** the per-variant error metrics are available as a downloadable
  artifact alongside the job log
