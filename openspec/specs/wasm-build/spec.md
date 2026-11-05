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

The wasm port SHALL be implemented through additive files; files under
`HighMap/src`, `HighMap/include`, `HighMap/CMakeLists.txt`, and the upstream
root `CMakeLists.txt` MUST NOT be edited, and the desktop OpenCL build MUST
keep configuring and building exactly as before.

#### Scenario: Desktop build unaffected

- **WHEN** the upstream desktop CMake build is configured and built after the
  wasm port files have been added
- **THEN** it configures and builds successfully with the same options as
  before the change

#### Scenario: No upstream file changes in version control

- **WHEN** the wasm port's files are inspected in version control
- **THEN** no file outside the additive wasm-side directories shows
  modifications relative to upstream, except changes explicitly recorded as
  flagged upstream bug fixes in the project decision log

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
