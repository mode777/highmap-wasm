# Design

## Context

See `proposal.md`. The matrix workflow fails in its Build step on
`external/nn-c/nn/triangle.c:15619` (K&R definition vs `triangle.h:287`
prototype) because the runner gcc's default C standard is C23. All 45 recorded
runs back to 2026-11-05 failed identically, so this is not a regression from any
repository change. `erosion-parity.yml` proves the fix on the same runner image:
it configures the same desktop CMake tree with
`-DCMAKE_C_FLAGS=-std=gnu17` (line 83) and is green.

## Goals / Non-Goals

**Goals:**

- A green `CMake on multiple platforms` run on `main` with a one-line,
  precedent-matching workflow change.

**Non-Goals:**

- Modernizing the vendored Triangle code or any upstream source.
- Changing compiler standards for the wasm build, the local desktop build, or
  the parity workflow (already correct).
- Expanding the matrix (Windows/clang legs) or updating action versions.

## Decisions

### 1. Workflow-level `-DCMAKE_C_FLAGS=-std=gnu17`

Add the flag to the `Configure CMake` step of `cmake.yml`, exactly as
`erosion-parity.yml` does. This scopes the standard pin to the one CI context
that needs it — a runner whose default drifted — without touching any source,
CMake list, or other build.

Alternatives rejected:

- Patch `external/nn-c/nn/triangle.c` to prototype-style definitions: modifies
  vendored upstream code (working rule 1) and costs a rebase on every sync.
- `set(CMAKE_C_STANDARD 17)` or similar in a `CMakeLists.txt`: a global change
  affecting the wasm build and every local desktop build, for a CI-only drift.
- Install/pin an older gcc on the runner: extra CI minutes and a pin that rots;
  the flag is the documented, already-proven approach.

### 2. Verification is the CI run itself

The flag only exists in CI, so local builds cannot exercise it. Acceptance is
the `CMake on multiple platforms` run on the fixing commit concluding success,
watched with `gh run watch`, with the run URL recorded in the tasks. The other
workflows on the same commit (`build-wasm`, `erosion-parity`, `pages`) are
unaffected by a desktop-workflow flag but are confirmed green as part of the
check.

## Risks / Trade-offs

- **gnu17 is a workaround, not a modernization.** If upstream ever cleans up the
  vendored C, the flag becomes redundant but harmless. → No action needed now;
  the flag mirrors the parity workflow, so the two stay consistent.
- **The workflow's other latent gaps are not addressed.** The matrix currently
  builds but does not run the desktop tests, and the Windows/clang legs are
  commented out. → Out of scope; this change only restores the build to green.

## Migration Plan

1. Edit the one line, push per the apply guidance, and watch the run.
2. Rollback is reverting the one line; no data or code migration involved.
