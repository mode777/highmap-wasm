# Spec Delta

## Purpose

Defines the desktop CMake CI matrix's compiler-standard contract: the
`CMake on multiple platforms` workflow configures with `-std=gnu17` so the
vendored `external/nn-c` K&R C sources compile on current runners, and the
workflow stays green on `main`.

## ADDED Requirements

### Requirement: Desktop CMake matrix configures with a compatible C standard

The desktop CMake matrix workflow SHALL configure with
`-DCMAKE_C_FLAGS=-std=gnu17` so the vendored `external/nn-c` (Triangle) sources,
which use K&R function definitions that newer gcc defaults (C23) reject, compile
on current `ubuntu-latest` runners. The workflow SHALL pass on pushes to `main`.

#### Scenario: Vendored C compiles under the pinned standard

- **WHEN** the desktop matrix workflow configures and builds on `ubuntu-latest`
- **THEN** the `nn-c` Triangle sources compile without the K&R-versus-prototype
  error and the build step succeeds

#### Scenario: Main stays green

- **WHEN** a push to `main` triggers the desktop matrix
- **THEN** the workflow run for that commit concludes successfully
