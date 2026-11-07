# Proposal

## Why

Phase 2 of the wasm port is the CPU rewrite of the algorithms that upstream
ships only as OpenCL kernels (`docs/wasm-port-research.md` §2 and §7). The
thermal and hydraulic families are complete; the remaining erosion/transform
block is `rifts`, `advection_particle` and `advection_warp`, which exist only
as kernels plus launch code in `src/erosion/rifts_gpu.cpp` and
`src/transform/advection_*_gpu.cpp`. None of them can run in a browser, so the
shipped package currently has no rift carving and no particle- or
warp-based field advection at all.

This change closes the phase-2 porting list: three functions (one rifts entry
point, six `advection_particle` overloads, two `advection_warp` overloads),
their JS surface, and the GPU-vs-CPU parity gate AGENTS.md requires. It does
not make the port complete overall: `primitives/primitives_gpu.cpp` still has
GPU-only noise generators (the voronoi family and friends) that were never part
of the phase-2 erosion/transform scope and remain unported.

## What Changes

- **Three CPU implementations** in two new additive translation units
  (namespace `hmap`, upstream-style license headers):
  `HighMap/src/erosion/rifts_cpu.cpp` with
  `HighMap/include/highmap/erosion_rifts_cpu.hpp`, and
  `HighMap/src/transform/advection_cpu.cpp` with
  `HighMap/include/highmap/transform_advection_cpu.hpp`. Signatures and
  defaults mirror the `hmap::gpu::` declarations one-for-one, so the CPU and
  GPU families stay drop-in interchangeable. No existing upstream file is
  edited.
- **Kernel-faithful rifts semantics**: literal transcription of
  `base_voronoi_f1df2`, `hash22f`, `smin`/`smax` and `wang_hash` (no CPU
  versions exist), including the `fract(sin(x) * 43758.5453)` cell hash, the
  `kw` scaling/rotation, the sigmoid radial spread, elevation-noise coupling
  and mask blending.
- **Kernel-faithful advection semantics**: `advection_particle`'s per-particle
  seeded walk with inertia, value persistence, advection mask and post filter
  across all six overloads; `advection_warp`'s gradient-driven uphill walk and
  mirrored-repeat linear image sampling, reproduced without an OpenCL image.
- **JS surface with mixed semantics**: `rifts` mutates in place and returns the
  input wrapper, matching the erosion family; `advectionParticle` and
  `advectionWarp` return NEW grids — the first new-grid functions in the
  erosion/transform surface. Required arguments and option defaults mirror the
  C++ declarations. Rifts masks/noise-offset input grids and the advection
  multi-field and explicit `dx`/`dy` overloads are implemented in C++ but
  deferred on the JS surface, recorded in the binding manifest.
- **Direct-kernel parity oracle**: a new `tests/src/test_rifts_advection_cpu.cpp`
  with CPU invariants and GPU comparisons. Rifts is gated by percentile and
  differing-cell fractions rather than per-cell maxima, because its sin-based
  hash can flip a whole Voronoi cell on a 1-ULP difference. Warp uses the same
  percentile approach and falls back from `CL_R` to `CL_RGBA` images where the
  device lacks the format. Particle advection compares single-particle
  arithmetic per cell and multi-particle behaviour by aggregate diagnostics,
  using a direct kernel launch with explicitly zeroed `out`/`count` buffers
  because the pinned wrapper never uploads them and the kernel accumulates
  non-atomically.
- **Parity CI**: the existing parity workflow is extended to run the
  rifts/advection suite alongside the thermal and hydraulic suites, with its
  timeout raised, the same fail-on-skip guard and published metrics. No
  upstream file is edited.
- **Docs**: AD entries for the port placement, new-grid API semantics and the
  direct-kernel oracle; a `docs/rifts-advection-parity.md` report with measured
  metrics and wrapper gaps; README API list, research-doc phase-2 status and a
  porting-learnings update.

Not in this change: showcase demos (a separate change), the JS surface for the
deferred overloads, and the GPU-only primitive noise generators, which are
outside the phase-2 erosion/transform scope.

## Capabilities

### New Capabilities

- `rifts-advection-cpu-ports`: the CPU implementations of the three remaining
  GPU-only erosion/transform functions — algorithmic fidelity to the OpenCL
  kernels, deterministic single-threaded execution, and the conditions under
  which numerical parity may be claimed.

### Modified Capabilities

- `terrain-api`: the high-level JS surface grows by `rifts`,
  `advectionParticle` and `advectionWarp`, including required arguments,
  option defaults, in-place versus new-grid return semantics, and the deferred
  overloads.
- `wasm-build`: the curated source list gains the two new port translation
  units, and the parity CI requirement is extended to cover the rifts/advection
  suite with direct-kernel oracles where the pinned GPU wrappers are broken.

## Impact

- **New code**: `HighMap/src/erosion/rifts_cpu.cpp`,
  `HighMap/include/highmap/erosion_rifts_cpu.hpp`,
  `HighMap/src/transform/advection_cpu.cpp`,
  `HighMap/include/highmap/transform_advection_cpu.hpp`,
  `tests/src/test_rifts_advection_cpu.cpp`, `tests/rifts-advection.test.ts`,
  `docs/rifts-advection-parity.md`.
- **Edited code**: `HighMap/wasm/{manifest.md,bindings.cpp,sources.cmake}`,
  `src/{api/highmap.ts,loader/module.ts,index.ts}`, `scripts/index.d.ts`,
  `tests/{surface.test.ts,golden.test.ts,golden/scenes.json}`,
  `scripts/golden-capture.mjs`, `.github/workflows/erosion-parity.yml`
  (extended, timeout raised), `README.md`,
  `docs/{wasm-port-research.md,decisions.md,porting-learnings.md}`.
- **Untouched**: every existing upstream file under `HighMap/src`,
  `HighMap/include` and the CMake lists; the desktop build keeps configuring
  and building as before and now also contains working CPU rifts/advection
  symbols. The broken GPU wrappers stay broken upstream and are recorded, not
  patched.
- **Cost**: rifts is a per-cell F1/F2 lookup over a fixed Voronoi
  neighbourhood; warp walks up to `advection_length / min(drx, dry)` steps per
  cell in two passes; particle advection is dominated by
  `iterations × nparticles` and keeps one accumulator grid per field. All are
  single-threaded and synchronous, documented with measured Node/browser cost.
- **Verification**: JS behaviour is covered by vitest and SHA-256 golden scenes
  captured from the wasm artifact; GPU parity is enforced by the extended
  parity job, which is the only environment with an OpenCL device available to
  this repository.
