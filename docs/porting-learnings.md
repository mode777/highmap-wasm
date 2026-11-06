# Porting GPU-only algorithms to CPU

Lessons from the thermal erosion family (AD-017) for the remaining GPU-only
algorithms. The thermal ports are the reference implementation of this
workflow; use them as the worked example rather than starting from scratch.

Relevant code:

- GPU kernels: `HighMap/src/gpu_opencl/kernels/`
- CPU ports: `HighMap/src/erosion/thermal_cpu.cpp`
- CPU declarations: `HighMap/include/highmap/erosion_thermal_cpu.hpp`
- GPU-vs-CPU harness: `tests/src/test_thermal_cpu.cpp`
- Parity record and measured bounds: `docs/thermal-parity.md`
- Decisions: AD-017 (thermal CPU ports), AD-018 (parity oracle), AD-019
  (deferred masks/deposition), AD-021 (rib read-back bug)

## 1. Port the kernel, not the wrapper

The OpenCL launch wrapper and the kernel are two different specifications. Read
both, then decide which one owns each behavior:

- The **kernel** owns the neighbour tables, distance weights, coefficients,
  branch structure, and float32 accumulation order.
- The **wrapper** owns iteration count, buffer swapping, border extrapolation
  after the loop, and any final clamp.

A CPU port that copies the wrapper's high-level intent but re-derives the
kernel arithmetic will drift. Transcribe the kernel loops literally, including
the order in which neighbours are visited and the order in which terms are
added. Keep the same helper constants (`DI`, `DJ`, `CD`, `CD_CONSERVE`, `REV`)
instead of re-deriving them from geometry.

If the kernel has a quirk, preserve it unless it is clearly a bug. For example,
`thermal_olsen` visits higher neighbours in a position-hashed order; that order
only changes rounding, but it is still part of the port.

## 2. Border rules are the main source of divergence

Every thermal variant has a different border contract, and the final pass is not
the same as the in-loop rule:

| Variant | In-loop border | Final pass |
|---|---|---|
| `thermal`, `thermal_auto_bedrock`, `thermal_inflate`, `thermal_ridge`, `thermal_scree` | one-cell frozen | one-cell extrapolation |
| `thermal_flatten` | two-cell band clamped to the interior | one-cell extrapolation |
| `thermal_olsen` | three-cell band clamped to the interior | one-cell extrapolation |
| `thermal_rib` | one-cell frozen | three-cell extrapolation |
| `thermal_conserve` | closed boundary, strictly inactive | none |

Two tests catch most border mistakes cheaply:

- Run zero iterations and assert the interior is untouched while the final
  border pass still runs.
- Run `thermal_conserve` with known border values and assert the border is
  bit-identical after any number of iterations.

Do not assume `extrapolate_borders()` is always called, and do not call it for
conserve. The kernel launch code is the authority on the final pass.

## 3. Preserve float32 accumulation order and traversal

Parity bounds are calibrated against the GPU's float32 result. Reordering
additions, using a double accumulator, or parallelising a reduction changes the
result and invalidates the bounds.

- Keep scalar `float` arithmetic unless the kernel explicitly uses doubles.
- Keep the same nested loop order and the same neighbour order.
- Keep the same branch structure; an `if` that skips a zero term is part of the
  arithmetic.
- Keep the port deterministic and single-threaded for the parity phase. Threads
  can be added later behind a separate build/flag, with a new parity campaign.

The thermal CPU ports use `ComputeMode::VA_SEQUENTIAL` and `-fno-openmp` in the
wasm build. That is deliberate, not a temporary limitation.

## 4. Match mask, deposition, and bedrock semantics exactly

The JS surface currently binds only the simple grid-in/grid-out forms, but the
C++ ports still need to match the full kernel contract:

- **Mask**: the result is a per-cell blend between the untouched input and the
  fully eroded result. The mask does not scale the erosion amount inside the
  kernel; it is applied by `apply_with_mask` around it.
- **Deposition**: the output grid holds the per-cell positive elevation gain
  for most variants and the absolute change for `thermal_ridge` and
  `thermal_schott`. It must not modify the caller's input grid.
- **Bedrock**: `thermal_auto_bedrock` tracks the highest level reached per cell
  and clamps **every iteration** (the kernel seeds its bedrock map with
  `-FLT_MAX` and updates it from `max(z_init, new_z)`). `thermal_conserve`
  applies its optional bedrock clamp once, after the loop. Do not assume all
  variants share one bedrock timing.
- **Talus/zmax**: scalar and per-cell forms must agree exactly when the grid is
  constant. Test that equivalence directly.

Deferred overloads belong in `HighMap/wasm/manifest.md`, not in silent gaps.

## 5. Test invariants before parity

GPU-vs-CPU parity is expensive and environment-sensitive. Start with CPU-only
invariants that do not need OpenCL:

- border preservation / extrapolation,
- determinism across repeated calls,
- mask blend equals the per-cell blend of the two endpoints,
- deposition is non-negative (or absolute, for ridge/Schott) and leaves the
  input untouched,
- mass conservation for `thermal_conserve`,
- auto-bedrock never erodes below the starting surface,
- zero-iteration and zero-rate no-op behavior where applicable.

Only after those pass should you add parity cases. This catches transcription
mistakes before a GPU runner is involved and keeps the parity suite focused on
arithmetic agreement.

## 6. Use the GPU oracle, but know its gaps

The thermal oracle is the desktop OpenCL build (`hmap::gpu::thermal*`) from the
same tree. The harness is `tests/src/test_thermal_cpu.cpp`; the measured bounds
and mutation results are in `docs/thermal-parity.md`.

Rules that matter for the next port:

- A **skipped** parity case is a failure, not a pass. The workflow rejects a
  device-less runner up front.
- Calibrate bounds from measured worst cases, then mutate one coefficient per
  family and require the bound to fail. If a mutation does not fail, the bound
  is too loose or the test scene is too weak.
- Selection-based variants (`thermal_flatten`, `thermal_olsen`, `thermal_rib`)
  need wider bounds than continuous variants because an argmax flip moves
  material to a different neighbour.
- Record oracle gaps instead of editing upstream sources. Known examples:
  `thermal_conserve` needs `CL_R`/`CL_FLOAT` image support; `thermal_rib` has an
  upstream read-back bug for even iteration counts; `thermal_schott` extrapolates
  borders every iteration while the GPU wrapper does it once.

If a gap can only be closed by changing upstream, record it as a candidate
upstream issue and cover the variant with invariants in the meantime.

## 7. Performance is variant-specific; measure in the target runtime

Do not infer cost from one variant. The thermal family has two very different
cost profiles:

- `thermal`, `thermal_auto_bedrock`, `thermal_inflate`, `thermal_ridge`,
  `thermal_scree`, `thermal_schott`, `thermal_rib`: roughly O(8) neighbour work
  per cell per iteration.
- `thermal_conserve`, `thermal_flatten`, `thermal_olsen`: each cell recomputes
  its neighbours' neighbour tables, so the work is roughly O(64) per cell per
  iteration.

The cost table in `docs/thermal-parity.md` covers `thermal` only; it is not a
family-wide estimate. The O(64) variants are several times slower in Node and
can be an order of magnitude slower in a headless browser.

Measure in the runtime that matters:

- Node timings are useful for the CPU port and the test suite.
- A real desktop browser is the UX measurement. In the deployed showcase, all
  ten thermal entries generate in under 5 s per specimen at 256² with 100
  iterations (the three O(64) variants use 100 instead of the examples' 500).
- Headless Chrome with software WebGL/SwiftShader is **not** a UX measurement.
  It can be an order of magnitude or more slower than a normal browser because
  the renderer and the wasm worker compete for the same CPU. Use headful capture
  or a longer timeout for preview generation, but do not record headless timings
  as user-facing performance.

If a port needs production-scale speed, optimize the kernel rather than the
showcase parameters: precompute each cell's excess/outflow once and reuse it for
inflow, or cache neighbour tables per cell. That changes the arithmetic, so it
needs its own parity campaign. Do not silently change iteration counts or grid
resolution to hide a slow kernel.

## 8. Wasm integration checklist

Adding a CPU port is more than adding the `.cpp` file:

1. Add the source to `HighMap/wasm/sources.cmake`.
2. Declare it in a non-GPU header and include it from the binding file.
3. Add the embind function in `HighMap/wasm/bindings.cpp` and the manifest entry
   in `HighMap/wasm/manifest.md`.
4. Add the raw signature to `src/loader/module.ts`.
5. Add the high-level wrapper and option types to `src/api/highmap.ts`, and
   export the types from `src/index.ts`.
6. Update the hand-written public typings in `scripts/index.d.ts` and rebuild
   `dist/index.js`/`dist/index.d.ts` with `node scripts/build-js.mjs`.
7. Add CPU invariants and parity cases, then run the desktop parity workflow.
8. Add a showcase demo only if the algorithm is exposed and the recipe is a
   straight port of an example.

The hand-written typings are easy to miss: the thermal port added the JS API
first, but `scripts/index.d.ts` stayed on the old surface until the showcase
typecheck caught it. Treat the typings as part of the binding, not as an
afterthought.

## 9. Showcase and demo integration checklist

If the port should appear in the gallery:

- Add a pure demo module under `site/src/demos/` that imports only the package
  API, frees every `HArray` in `finally`, and returns `copyOut(grid)`.
- Record `source.example`, `source.panel`, and `referenceImage`; keep the
  example's grid, seed, and parameters unless a web-sized deviation is recorded
  in `adaptationNote` with both source and shipped values.
- Register the module in `demos/index.ts` and `demos/sources.ts`.
- Update the catalog count/coverage tests; a new family needs an explicit
  coverage assertion so a removed entry fails loudly.
- Capture previews and commit the WebPs plus `manifest.json`. The manifest
  source hash covers the demo module and metadata, so recapture when either
  changes.
- Verify in a real browser, not only in Node and not only through headless
  capture.

## 10. Porting checklist for the next algorithm

1. Read the kernel and the launch wrapper; list border, iteration, mask,
   deposition, and final-pass behavior.
2. Port the kernel literally into the non-GPU namespace; keep constants,
   traversal order, and float32 accumulation.
3. Add CPU-only invariants (borders, determinism, masks, deposition, special
   cases) and make them pass without OpenCL.
4. Add GPU parity scenes, calibrate bounds, and run a mutation check.
5. Record oracle gaps and upstream quirks; do not patch upstream without an
   explicit, flagged decision.
6. Add the wasm source, binding, manifest, loader signature, API wrapper,
   exported types, and `scripts/index.d.ts`.
7. Rebuild `dist/`, run the root tests, and run the desktop parity workflow.
8. Measure Node and real-browser cost; record the numbers and the measurement
   environment.
9. Add a showcase demo if the algorithm is user-visible and example-faithful.
10. Update this document with anything the next port should know.
