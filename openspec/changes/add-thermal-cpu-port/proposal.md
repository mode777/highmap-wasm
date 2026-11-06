# Proposal

## Why

Phase 2 of the wasm port is the CPU rewrite of the algorithms that only exist as
OpenCL kernels (`docs/wasm-port-research.md` §2 and §7). The thermal family is
the largest block of that work: `thermal_schott` is the only variant with an
upstream CPU implementation, while `thermal`, `thermal_auto_bedrock`,
`thermal_conserve`, `thermal_flatten`, `thermal_inflate`, `thermal_olsen`,
`thermal_rib`, `thermal_ridge` and `thermal_scree` exist solely as CL kernels
plus `clwrapper` launch code in `src/erosion/thermal_gpu.cpp`. None of them can
therefore run in a browser, so the shipped package currently has no thermal
weathering at all — its erosion surface stops at the two hydraulic twins.

This is the change that closes that gap: nine CPU ports, the existing
`thermal_schott` bound as-is, and the GPU-vs-CPU parity gate AGENTS.md requires
before parity may be claimed.

## What Changes

- **Nine CPU thermal implementations** in a new
  `HighMap/src/erosion/thermal_cpu.cpp` (namespace `hmap`), published through a
  new additive header `HighMap/include/highmap/erosion_thermal_cpu.hpp`. Each
  variant mirrors the signature set of its `hmap::gpu::` counterpart
  (array-talus and scalar-talus forms, mask overloads, bedrock and deposition
  outputs) so the CPU and GPU families stay drop-in interchangeable.
- **Kernel-faithful semantics**, not a re-invention: the arithmetic, neighbour
  tables, float literals, per-variant border treatment (1-cell frozen border,
  2-cell band for flatten, 3-cell band for olsen, closed boundary for conserve),
  the final `extrapolate_borders` pass (including `thermal_rib`'s
  `nbuffer = 3`), the `-FLT_MAX` auto-bedrock seed and `thermal_olsen`'s
  position hash are all reproduced from the kernels.
- **Ten new bindings** (the nine ports plus the upstream CPU `thermal_schott`)
  added to `HighMap/wasm/manifest.md` and `bindings.cpp`, with camelCase JS
  wrappers in `src/api/` and a `talus: number | GridLike` argument that
  materializes a constant grid internally.
- **Desktop parity harness**: a new `tests/src/test_thermal_cpu.cpp` gtest that
  compares `hmap::gpu::*` against the new `hmap::*` implementations on fixed
  scenes, reports per-variant error metrics, and fails when a calibrated
  tolerance is exceeded (or skips when no OpenCL device is present).
- **Parity CI job**: an additive workflow that builds the desktop library with
  the PoCL CPU OpenCL ICD and runs the parity filter, failing on skipped cases
  so a device-less runner cannot report a hollow pass.
- **Upstream-tree additions, no upstream edits**: the port source and its header
  are new files under `HighMap/src` and `HighMap/include`; no existing upstream
  file is modified. The `wasm-build` capability is amended to state that rule
  explicitly (additions allowed for ported algorithms, edits still forbidden).
- **Docs**: ADR entries for the placement, oracle and deferred-overload
  decisions, README API list, a parity report with the measured metrics, and
  the research-doc phase-2 status.

Not in this change: mask and deposition-map arguments on the JS surface (the
C++ ports implement them, the binding manifest records them as deferred), and
the remaining GPU-only families (`hydraulic_{mcdonald,schott,vpipes}`, `rifts`,
`advection_*`).

## Capabilities

### New Capabilities

- `thermal-cpu-ports`: the CPU implementations of the nine GPU-only thermal
  variants — algorithmic and boundary fidelity to the OpenCL kernels,
  deterministic single-threaded execution, the parity metrics that gate the
  parity claim, and the rules for keeping the ports replaceable if upstream
  ever ships its own CPU versions.

### Modified Capabilities

- `terrain-api`: the high-level JS surface grows by the ten thermal functions,
  including their required `talus` argument (`number | GridLike`), options
  defaults, and in-place return semantics.
- `wasm-build`: the curated source list gains the port translation units, and
  the "upstream sources remain unmodified" rule is clarified to permit new
  upstream-tree files for ported algorithms while still forbidding edits to
  existing ones; the parity harness becomes a build-and-CI invariant.

## Impact

- **New code**: `HighMap/src/erosion/thermal_cpu.cpp`,
  `HighMap/include/highmap/erosion_thermal_cpu.hpp`,
  `tests/src/test_thermal_cpu.cpp`, `tests/thermal.test.ts`,
  `.github/workflows/thermal-parity.yml`, `docs/thermal-parity.md`.
- **Edited code**: `HighMap/wasm/{manifest.md,bindings.cpp,sources.cmake}`,
  `src/{api/highmap.ts,loader/module.ts,index.ts}`,
  `tests/{surface.test.ts,golden.test.ts,golden/scenes.json}`,
  `scripts/golden-capture.mjs`, `README.md`, `docs/decisions.md`,
  `docs/wasm-port-research.md`.
- **Untouched**: every existing upstream file (`HighMap/src` and
  `HighMap/include` gain files but lose no edits); the desktop build keeps
  configuring and building as before and now also contains working CPU thermal
  symbols.
- **Cost**: wasm module size grows by nine kernels' worth of scalar code
  (estimated well under 100 KB uncompressed); the algorithms are
  single-threaded, so a 1024² grid over hundreds of iterations is seconds, not
  milliseconds — documented, not optimized in this change.
- **Verification**: CPU-side behaviour is covered by the local vitest suite;
  GPU parity is enforced by the new CI job, which is the only environment with
  an OpenCL device available to this repository.
