# Spec Delta

## ADDED Requirements

### Requirement: Release codegen is speed-optimized and overridable

The release wasm build SHALL compile and link with `-O3` by default. The
optimization level and any additional codegen flags SHALL be exposed as CMake
cache variables so alternative profiles can be built and benchmarked without
editing tracked files. The release profile MUST NOT enable fast-math,
finite-math-only, or any other flag that changes floating-point semantics.

#### Scenario: Default profile uses speed optimization

- **WHEN** the wasm project is configured without overrides
- **THEN** the compile and link commands for the release artifact use `-O3`
  rather than `-Os`

#### Scenario: Optimization level can be overridden

- **WHEN** the wasm project is configured with a different optimization level
  through the documented cache variable
- **THEN** the artifact is built with that level and no tracked file is
  modified

#### Scenario: Floating-point semantics stay intact

- **WHEN** the release artifact is built with the default profile
- **THEN** no fast-math or reassociation flag is applied, and the existing
  parity requirements remain the numerical correctness criterion

#### Scenario: Single-threaded contract still holds

- **WHEN** the multi-optimized release artifact is loaded in an environment
  without cross-origin isolation
- **THEN** it initializes and generates terrain without SharedArrayBuffer or
  threading support, as required by the existing single-threaded execution
  requirement
