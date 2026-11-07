# Proposal

## Why

Phase 2 of the wasm port is the CPU rewrite of the algorithms that upstream
ships only as OpenCL kernels (`docs/wasm-port-research.md` §2 and §7). The
thermal family is complete; the remaining hydraulic block is
`hydraulic_mcdonald`, `hydraulic_schott` and `hydraulic_vpipes`, which exist
only as kernels plus launch code in `src/erosion/hydraulic_*_gpu.cpp`. None of
them can run in a browser, so the shipped package currently has no
flow-coupled, Schott or pipe-network erosion at all.

This change ports that block: five public functions (the three base algorithms
plus the McDonald multiscale driver and the erosion-only Schott variant), their
optional output maps on the JS surface, and a GPU-vs-CPU parity harness that
uses the kernels directly where the pinned GPU wrappers are broken.

## What Changes

- **Five CPU implementations** in a new
  `HighMap/src/erosion/hydraulic_cpu.cpp` (namespace `hmap`), published through
  a new additive header `HighMap/include/highmap/erosion_hydraulic_cpu.hpp`:
  `hydraulic_mcdonald`, `hydraulic_mcdonald_multiscale`, `hydraulic_schott`
  (both overloads), `hydraulic_schott_erosion` and `hydraulic_vpipes`. Their
  signatures and defaults mirror the `hmap::gpu::` declarations so the families
  stay drop-in interchangeable.
- **Kernel-faithful semantics**: McDonald's seeded particle hash, 5-point
  gradient, transfer/debris rules and filter; Schott's clamp-to-edge image
  sampling, phase cycle and erosion-only form; vpipes' flux, water, erosion,
  sediment-transport pass sequence and bilinear advection. No border
  extrapolation is invented where the kernels use clamp-to-edge or per-particle
  bounds.
- **Output maps on the JS surface**: optional, caller-allocated `HArray`s in
  the options object — `sediment` and `discharge` for McDonald, `flow` for
  Schott, `waterDepth`, `sediment`, `velU` and `velV` for vpipes. The binding
  takes nullable `hmap::Array*` outputs; the TS layer validates shape, liveness
  and aliasing before the wasm call. Mask, moisture-map and rain-map input
  overloads are implemented in C++ but deferred on the JS surface.
- **Direct-kernel parity oracle**: a new `tests/src/test_hydraulic_cpu.cpp`
  with CPU-only invariants and GPU comparisons. Schott and vpipes launch the
  OpenCL kernels directly because their pinned wrappers are broken (Schott
  never binds the kernel's `it`; vpipes launches kernel names that no longer
  exist). McDonald compares single-particle arithmetic per cell and
  multi-particle behaviour by aggregate metrics, because GPU atomic ordering is
  not reproducible.
- **Parity CI**: the existing parity workflow is extended to run the hydraulic
  suite alongside the thermal suite, with the same fail-on-skip guard and
  published metrics. No upstream file is edited.
- **Docs**: AD entries for the port placement, output-map contract and
  direct-kernel oracle; a `docs/hydraulic-parity.md` report with the measured
  metrics and wrapper gaps; README API list and research-doc phase-2 status.

Not in this change: showcase demos (a separate change), and the JS surface for
the mask/moisture/rain input overloads.

## Capabilities

### New Capabilities

- `hydraulic-cpu-ports`: the CPU implementations of the five GPU-only
  hydraulic functions — algorithmic fidelity to the OpenCL kernels,
  deterministic single-threaded execution, output-map semantics, and the
  conditions under which numerical parity may be claimed.

### Modified Capabilities

- `terrain-api`: the high-level JS surface grows by the five hydraulic
  functions, including required arguments, option defaults and the
  output-map contract.
- `wasm-build`: the curated source list gains the hydraulic port translation
  unit, and the parity CI requirement is clarified to permit a direct-kernel
  oracle when a pinned GPU wrapper cannot launch its kernel.

## Impact

- **New code**: `HighMap/src/erosion/hydraulic_cpu.cpp`,
  `HighMap/include/highmap/erosion_hydraulic_cpu.hpp`,
  `tests/src/test_hydraulic_cpu.cpp`, `tests/hydraulic.test.ts`,
  `docs/hydraulic-parity.md`.
- **Edited code**: `HighMap/wasm/{manifest.md,bindings.cpp,sources.cmake}`,
  `src/{api/highmap.ts,loader/module.ts,index.ts}`, `scripts/index.d.ts`,
  `tests/{surface.test.ts,golden.test.ts,golden/scenes.json}`,
  `scripts/golden-capture.mjs`, `.github/workflows/thermal-parity.yml`
  (renamed/extended), `README.md`, `docs/decisions.md`,
  `docs/wasm-port-research.md`.
- **Untouched**: every existing upstream file; the desktop build keeps
  configuring and building as before and now also contains working CPU
  hydraulic symbols. The broken GPU wrappers stay broken upstream and are
  recorded, not patched.
- **Cost**: the port adds scalar implementations of the three models. vpipes
  keeps several full-grid state arrays; McDonald is dominated by
  `steps × samples × maxage`; Schott is a per-cell stencil. All are
  single-threaded and synchronous, documented with measured Node/browser cost.
- **Verification**: CPU behaviour is covered by vitest; GPU parity is enforced
  by the extended CI job, which is the only environment with an OpenCL device
  available to this repository.
