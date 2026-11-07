# wasm-build Specification

## Purpose

Defines how the HighMap C++ sources are compiled into the WebAssembly artifact
and JS glue shipped by the npm package, and the invariants that keep the port
additive to the upstream fork while the desktop reference build stays intact.

## Requirements

### Requirement: Build produces the shippable artifact set

The wasm build SHALL produce, in one invocation from a clean checkout, the
complete artifact set the npm package needs: the `highmap.wasm` binary, a JS
glue module loadable by the package entry point, and TypeScript declarations
for the exposed surface.

#### Scenario: Clean build yields all artifacts

- **WHEN** the wasm build command runs on a clean checkout with the pinned
  Emscripten toolchain and initialized submodules
- **THEN** the build output directory contains `highmap.wasm`, a loadable JS
  glue module, and TypeScript declarations, and the build exits successfully

### Requirement: CPU-only artifact with no heavy system dependencies

The wasm build SHALL compile a curated CPU-only subset of the HighMap sources
and MUST NOT require OpenCL, OpenCV, assimp, GSL, OpenEXR, TIFF, or
PointSampler to be present on the build machine. GPU-only sources (the OpenCL
wrapper, `gpu_opencl/`, `*_gpu.cpp`) and file-export writers MUST be excluded
from the compilation.

#### Scenario: Build machine needs only Emscripten

- **WHEN** the wasm build is configured and built on a machine that has the
  Emscripten SDK but none of the excluded system libraries installed
- **THEN** configuration and compilation succeed without errors

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

### Requirement: Flagged upstream bug fixes keep both builds identical

When an upstream numerical bug breaks a spec-required function, the port MAY
fix it in place with a minimal, clearly flagged edit, provided the same code
compiles into both the desktop and wasm builds — source-level identity
preserves the CPU-port parity oracle. Every such fix MUST be recorded in the
project decision log with rationale and rebase guidance.

#### Scenario: Fix is identical for desktop and wasm

- **WHEN** a flagged upstream fix is applied
- **THEN** the desktop build compiles the same changed source file without
  conditional compilation, and the decision log contains an AD entry naming
  the file, the defect, and the rebase note

### Requirement: Single-threaded execution requirements

The produced artifact MUST NOT require SharedArrayBuffer, cross-origin
isolation headers, or any threading support from the host environment; all
computation SHALL run synchronously on the calling thread.

#### Scenario: Loads without cross-origin isolation

- **WHEN** the package is initialized in a browser page served without
  COOP/COEP headers
- **THEN** initialization and terrain generation succeed

### Requirement: Bound surface is manifest-driven

The set of C++ functions exposed through the binding layer SHALL be governed
by a single explicit manifest within the wasm build, so that extending the
API surface is a manifest and binding change that requires no architectural
rework.

#### Scenario: Surface matches the manifest

- **WHEN** the artifact is built and its exposed function set is enumerated
- **THEN** it corresponds exactly to the functions listed in the manifest

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
