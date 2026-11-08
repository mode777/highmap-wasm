# Proposal

## Why

The desktop-build matrix workflow (`.github/workflows/cmake.yml`, "CMake on
multiple platforms") is the only red CI on `main` — and it has never been green:
all 45 of its recorded runs, back to its first on 2026-11-05, failed with the
same compile error, including runs triggered by docs-only commits. The `ubuntu-latest`
runner's gcc defaults its C language to C23, and the vendored Triangle sources
under `external/nn-c/` use K&R function definitions that C23 rejects:

```text
external/nn-c/nn/triangle.c:15619:1: error: number of arguments doesn't match prototype
external/nn-c/nn/triangle.h:287:6: error: prototype declaration
```

The same problem is already documented and worked around in this repository:
`erosion-parity.yml` configures with `-DCMAKE_C_FLAGS=-std=gnu17` (see
`docs/rifts-advection-parity.md`, "Running it": "-std=gnu17 is required on
toolchains that default C to C23"), and that workflow is green on the same
runners. AGENTS.md requires the desktop build to keep working; this change
applies the known fix to the one workflow missing it.

## What Changes

- Add `-DCMAKE_C_FLAGS=-std=gnu17` to the `Configure CMake` step of
  `.github/workflows/cmake.yml`, matching the flag already used by
  `erosion-parity.yml`.
- Nothing else: no source edits, no CMake list changes, no workflow restructuring.

Explicitly out of scope:

- Patching the vendored `external/nn-c` sources (working rule 1: do not modify
  upstream; the K&R code is vendored).
- Setting a global C standard in a `CMakeLists.txt` (affects the wasm and local
  desktop builds; far broader than the CI-only problem).
- Pinning the runner's gcc version or image (fragile, hides the drift).
- Re-enabling the commented-out matrix legs (Windows, clang) or bumping action
  versions — separate concerns.

## Capabilities

### New Capabilities

- `desktop-ci`: the desktop CMake CI matrix configures with a C standard the
  vendored sources accept and stays green on `main`.

### Modified Capabilities

None.

## Impact

- One line added to the `Configure CMake` step's flag list in
  `.github/workflows/cmake.yml`.
- Verified only by CI: the fix is a workflow flag, so acceptance is the green
  `CMake on multiple platforms` run on the fixing commit. No runtime code, npm
  package, or site changes.
