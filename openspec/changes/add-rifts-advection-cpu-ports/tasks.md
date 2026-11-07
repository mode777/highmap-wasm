# Tasks

## 1. Foundation: headers, source list, decision record

- [x] 1.1 Create `HighMap/include/highmap/erosion_rifts_cpu.hpp` with the
  upstream-style GPL header and a provenance comment (transcribes
  `src/gpu_opencl/kernels/rifts.cl`; rebase rule: delete this header and the
  port file if upstream ships a CPU implementation). Declare the single
  `hmap::rifts` entry point with the exact `hmap::gpu::rifts` signature from
  `HighMap/include/highmap/erosion.hpp:1448-1467`: `Array&`, `const glm::vec2&
  kw`, `float angle`, `float amplitude`, `std::uint32_t seed`, then every
  default (`elevation_noise_shift = 0`, `k_smooth_bottom = 0.05`,
  `k_smooth_top = 0.05`, `radial_spread_amp = 0.2`, `elevation_noise_amp = 0.1`,
  `clamp_vmin = 0`, `remap_vmin = 0`, `apply_mask = true`,
  `reverse_mask = false`, `mask_gamma = 1`), the three `const Array*` inputs
  (`p_noise_x`, `p_noise_y`, `p_mask`, all `nullptr`), `center = {0.5, 0.5}` and
  `bbox = {0, 1, 0, 1}`. Verify: a throwaway TU that includes only this header
  and takes `auto* f = static_cast<void (*)(hmap::Array&, const glm::vec2&,
  float, float, std::uint32_t, float, float, float, float, float, float, float,
  bool, bool, float, const hmap::Array*, const hmap::Array*, const hmap::Array*,
  const glm::vec2&, const glm::vec4&)>(&hmap::rifts);` compiles with
  `emcc -std=c++20 -fsyntax-only -I HighMap/include -I HighMap/wasm/stubs
  -x c++ -` (with `$EMSDK/upstream/emscripten` on PATH).

- [x] 1.2 Create `HighMap/include/highmap/transform_advection_cpu.hpp` with the
  same header conventions and declare all eight ports with the exact
  `hmap::gpu` parameter lists and defaults from
  `HighMap/include/highmap/transform.hpp:551-675`: `advection_particle`
  single-field and `std::vector<Array>` forms with and without `iterations`,
  plus the two explicit `(dx, dy, ...)` forms (`reverse = false`,
  `post_filter = true`, `post_filter_sigma = 0.125`, `advection_length = 0.1`,
  `value_persistence = 0.99`, `inertia = 0`, `const Array* p_advection_mask`
  and `p_mask` = `nullptr`; the `iterations` forms keep no default), and both
  `advection_warp` forms (z-driven and explicit `dx`/`dy`, `advection_length =
  0.1`, `value_persistence = 0.9`, `p_mask = nullptr`). Verify: a throwaway TU
  resolves each overload through an explicitly typed function pointer (eight
  pointers, one per overload) and compiles with the same `emcc -std=c++20
  -fsyntax-only` command.

- [x] 1.3 Create `HighMap/src/erosion/rifts_cpu.cpp` and
  `HighMap/src/transform/advection_cpu.cpp` with the GPL/provenance file
  headers, an `#include` of their own header, and only upstream CPU headers
  (`array`, `gradient`, `filters`, `internal/validation`) plus the standard
  library; leave the function bodies for groups 2-3. Add both files under a
  `# CPU ports of the GPU-only rifts/advection family
  (add-rifts-advection-cpu-ports)` comment in `HighMap/wasm/sources.cmake`.
  Verify: `EMSDK=C:/WS/tools/emsdk bash scripts/build-wasm.sh --no-tests`
  succeeds with both TUs compiled, `dist/highmap.wasm` is regenerated, and
  `grep -nE "clwrapper|OpenCL|gpu::|#pragma omp"` matches comments only in both
  files.

- [x] 1.4 Add AD-026 to `docs/decisions.md` after AD-025, recording the
  two family translation units, the same-source property shared by the wasm build
  and the desktop parity build, the `_cpu` duplicate-symbol rebase rule and the
  two curated-list rows; reference AD-026 from both port file headers. Verify:
  `grep -n "AD-026" docs/decisions.md HighMap/src/erosion/rifts_cpu.cpp
  HighMap/src/transform/advection_cpu.cpp` matches all three, AD-025 is still
  present and the entries are in order.

## 2. Rifts CPU port and invariants

- [x] 2.1 Transcribe `rifts.cl` into `HighMap/src/erosion/rifts_cpu.cpp`.
  Anonymous-namespace helpers with their kernel names: `wang_hash` and `rand`
  (xorshift then `* (1.f / 4294967296.f)`), `fract`, `hash22f`
  (`dot(p, (127.1, 311.7))`, `dot(p, (269.5, 183.3))`,
  `fract(sin(p + fseed) * 43758.5453123f)`), `smin`/`smax` dispatching on
  `k > 0` to `min_smooth`/`max_smooth` with `pow_int(h, 3)`, `max_smooth`,
  `min_smooth`, `remap_from`, the kernel-form `lerp` (`(1 - t) * a + t * b`,
  not `std::lerp`), `g_to_xy` (`gx / nx` not `nx - 1`, bbox scaling
  `(bbox.y - bbox.x)` with the `(xmin, xmax, ymin, ymax)` order, per-cell noise
  offsets added after the bbox scaling) and `base_voronoi_f1df2` (3x3
  neighbourhood, `FLT_MAX` seeds, the kernel's two-line update order, returns
  `min1 / min2`). Then the row-major per-cell loop (`j` outer, `i` inner) with
  the center scalar-broadcast rotation preserved verbatim (`dr_x = (pos.x -
  center.x) * c + (pos.y - center.x) * s`, `dr_y = (pos.x - center.y) * (-s) +
  (pos.y - center.y) * c`), the literal sigmoid `-2 / (1 + exp(-20 * dr_x)) +
  1`, the `kw` scaling/rotation, elevation-noise coupling, `smax(min(noise_v,
  1), clamp_vmin, k_smooth_top)` followed by `remap_from(noise_v, 1,
  remap_vmin, clamp_vmin, 1)`, `t = apply_mask ? pow(val0, mask_gamma) : 1`
  with the `reverse_mask` flip applied even when `apply_mask` is false, and both
  kernel-form `lerp(val0, val, t)` blends. Drop only the unused `dir` local,
  with a comment. Verify: the wasm build from 1.3 still succeeds,
  `grep -nE "clwrapper|OpenCL|gpu::|#pragma omp"` matches comments only, and a
  `RiftsAdvectionCpuInvariants.RiftsSmoke` case added to a new
  `tests/src/test_rifts_advection_cpu.cpp` in this task (determinism across two
  calls, amplitude 0 no-op, all outputs finite on a 32x32 fBm) compiles and
  passes in the parity job's desktop build.

- [x] 2.2 Extend the `RiftsAdvectionCpuInvariants.Rifts*` cases in
  `tests/src/test_rifts_advection_cpu.cpp` (upstream test style, file header
  documenting the metric lines from D7): `ApplyMaskEndpoints` (a zero mask
  leaves cells unchanged and a one mask equals the mask-free call),
  `ReverseMaskQuirk` (`apply_mask = false, reverse_mask = true` leaves the grid
  unchanged, and on a grid holding zero in one region `apply_mask = true,
  reverse_mask = false` leaves that region unchanged while `reverse_mask = true`
  deforms it), `NoiseOffsets` (supplying `p_noise_x`/`p_noise_y` changes the
  output and leaves both inputs byte-identical), `CenterBroadcast` (a center
  with unequal components differs from a component-wise subtraction reference),
  `AsymmetricBbox` and `ShapeValidation` (a mismatched mask/noise shape returns
  without writing the terrain, matching the wrapper). Verify: the file compiles
  in the parity job's `highmap_tests` build and
  `./build/bin/highmap_tests --gtest_filter='RiftsAdvectionCpuInvariants.Rifts*'`
  passes in CI without an OpenCL device.

## 3. Advection CPU ports and invariants

- [x] 3.1 Implement the warp port in `HighMap/src/transform/advection_cpu.cpp`:
  the file-local mirrored-repeat normalized linear sampler (`s = u * N`,
  `m = fmod(s, 2N)`, `m += 2N` when negative, `m = 2N - m` when `m > N`,
  `q = m - 0.5`, `i0 = floor(q)`, `f = q - i0`, clamp `i0`/`i1` to `[0, N-1]`,
  bilinear via `(1 - f) * texel(i0) + f * texel(i1)` per axis; texel centers at
  `(i + 0.5) / N`, `u = 0` and `u = 1` on the edge texels), and both
  `advection_warp` overloads: `pos0 = ((gx + 0.5) / nx, (gy + 0.5) / ny)`,
  `max_steps = ceil(advection_length / min(drx, dry))`, `limit =
  min(max_steps, 1024)`, the upward pass (store position, read velocity, keep
  the previous direction when `hypot = 0`, advance one pixel, break outside
  `[0, 1]^2` or on `z_current - z_prev < -1e-5`), the backward blend
  (`ratio = mask * value_persistence`, `val = ratio * val + (1 - ratio) *
  new_val`, last position read twice), `dir` initialized to `(0, 0)` with the
  deviation documented in a comment, and a null mask materialized as constant
  one. Verify: new `RiftsAdvectionCpuInvariants.Warp*` cases are added and pass
  in the parity job: `ZeroLengthIsZeros` (all-zero output, not a copy),
  `ExplicitGradientEqualsGradientDriven` (constant `dx`/`dy` equal to
  `gradient_x/y` give identical output), `StepCap` (4096x1 grid, constant
  `dx = 1`, `dy = 0`, flat `z`: `advectionLength = 1.5` equals
  `advectionLength = 0.25`, both limited to 1024 walk steps), `MaskBlend`,
  `FlatStartFinite` and `Deterministic`.

- [x] 3.2 Implement the particle solver and the four single-pass overloads in
  `advection_cpu.cpp`: `pcg_hash(seed + id * 2)`, xorshift `rand`, initial
  position `((nx - 1) * rand, (ny - 1) * rand)`, up to 100 rejection draws
  against `p_advection_mask` (break when the mask is `> 0`, keep the last draw),
  `dist_max = advection_length * nx`, per-step velocity read, inertia blend
  guarded below `1e-9`, one-pixel move with `advection_sign = reverse ? -1 : 1`,
  position-skip when both components move less than `1e-9`, `(int)` truncation,
  mask/out-of-grid stop, `val = lerp(field[idx], val, value_persistence)`,
  `out[f][idx] += val`, `count[idx] += 1` in ascending particle and field order,
  and explicit zeroing of every accumulator and `count` at call start. Reproduce
  the wrapper host stage exactly: `out = (accum + original) / (count + 1)`
  clamped to `[0, 1]` (original where `count == 0`), optional
  `hmap::laplace(out, p_advection_mask, post_filter_sigma, 1)` and optional
  `lerp(out, original, *p_mask)`; keep the wrapper's `if (true)` branch as the
  only branch with a comment. Verify: new
  `RiftsAdvectionCpuInvariants.Particle*` cases pass:
  `ExplicitVelocityEqualsGradient` (explicit `-gradient_x/y` equals the z-driven
  form), `NormalizationFormula` (one particle per cell with `postFilter = false`
  matches the `(accum + original) / (count + 1)` formula on a hand-computed
  cell),
  `InertiaGuard` (`inertia = 1` from a zero direction does not move and stays
  finite), `PostFilterOff`, `AdvectionMask` and `Deterministic`.

- [x] 3.3 Implement the two `iterations` overloads, the remaining multi-field
  plumbing and the 64-field cap in `advection_cpu.cpp`: single-field forms
  delegate to the vector form, `iterations` loops the single-pass solver with
  the same seed, `num_f = min(num_fields, 64)` leaves later fields on the
  unaccumulated path, and all inputs are validated with `validate_non_empty` /
  `validate_same_shape` returning an empty `Array`/`std::vector<Array>` on
  mismatch. Verify: cases `ParticleIterationsChain` (`iterations = 3` equals
  three chained `iterations = 1` calls), `ParticleMultiFieldEqualsPerField`
  (one call with N fields equals N single-field calls byte for byte),
  `ParticleFieldCap` (65 identical fields with `post_filter = false`: fields
  0-63 are byte-identical to each other and field 64 differs at visited cells,
  following the unaccumulated path) and `ParticleShapeValidation` pass in the
  parity job.

- [x] 3.4 Complete the advection edge-case invariants in
  `tests/src/test_rifts_advection_cpu.cpp`: every input grid (terrain, field,
  velocity, masks) is byte-identical after both functions; omitting optional
  pointers equals passing explicit null pointers; zero particles with
  `post_filter = false` and `iterations = 0` return the original field (a fresh
  copy); `reverse` moves accumulation in the opposite direction; `inertia = 0`
  and `inertia = 0.2` are deterministic; and rifts followed by advection, in
  both call orders, produces the same per-function output. Verify:
  `./build/bin/highmap_tests --gtest_filter='RiftsAdvectionCpuInvariants.*'`
  passes in the parity job (the only environment that builds the gtest binary).

## 4. JS surface, typings and goldens

- [x] 4.1 Bind the three functions in `HighMap/wasm/bindings.cpp` and record
  them in `HighMap/wasm/manifest.md`. Rifts binding:
  `(z&, kwX, kwY, angle, amplitude, seed, elevationNoiseShift, kSmoothBottom,
  kSmoothTop, radialSpreadAmp, elevationNoiseAmp, clampVmin, remapVmin,
  applyMask, reverseMask, maskGamma, centerX, centerY, bboxXmin, bboxXmax,
  bboxYmin, bboxYmax)` (the deferred `p_noise_x`/`p_noise_y`/`p_mask` stay
  unbound); particle binding: the `iterations` overload
  `(z&, field&, iterations, nparticles, seed, reverse, postFilter,
  postFilterSigma, advectionLength, valuePersistence, inertia) -> Array`; warp
  binding: `(z&, field&, advectionLength, valuePersistence) -> Array`. Add the
  three manifest table rows with the full option defaults from design D6 and a
  prose deferral note next to the family rows covering rifts masks/noise
  offsets, particle `p_advection_mask`/`p_mask` and multi-field/explicit
  `dx`/`dy`, and warp `p_mask`/explicit gradients; add the raw signatures to
  `src/loader/module.ts`. Verify:
  `EMSDK=C:/WS/tools/emsdk bash scripts/build-wasm.sh --no-tests && node
  scripts/build-js.mjs` succeeds, and a Node probe
  (`node --input-type=module -e "import f from './dist/highmap.js'; const m =
  await f(); console.log(typeof m.rifts, typeof m.advectionParticle, typeof
  m.advectionWarp);"`) prints `function function function`.

- [x] 4.2 Implement the TS surface in `src/api/highmap.ts`: the `newGrid`
  helper next to `inplace`/`inplaceWithOutputs` (coerce every input, validate
  that each later input matches the first grid's shape, call the binding, wrap
  the returned raw handle, free every plain-input temporary in a `finally`;
  HArray inputs pass through and the result is always a fresh wrapper),
  `rifts(grid, kw, angle, amplitude, seed, options?)` through `inplace` with
  `kw`/`center`/`bbox` tuple validation and conversion to the scalar binding
  arguments (bbox order left as `[xmin, xmax, ymin, ymax]`),
  `advectionParticle(z, field, nparticles, seed, options?)` and
  `advectionWarp(z, field, options?)` through `newGrid` with the D6 defaults
  (`iterations: 1`, `reverse: false`, `postFilter: true`,
  `postFilterSigma: 0.125`, `advectionLength: 0.1`, `valuePersistence: 0.99`,
  `inertia: 0`; warp `advectionLength: 0.1`, `valuePersistence: 0.9`).
  Export `RiftsOptions`, `AdvectionParticleOptions`, `AdvectionWarpOptions` and
  the new method types from `src/api/highmap.ts` and `src/index.ts`; mirror the
  declarations in `scripts/index.d.ts`; extend the `tests/surface.test.ts`
  expected name list with `advectionParticle`, `advectionWarp` and `rifts`,
  keeping it alphabetical (33 -> 36); add AD-027 to `docs/decisions.md`
  (new-grid helper, in-place versus new-grid, scalarized tuples, deferred
  overloads) and extend `README.md`'s API list with the three names plus a note
  that the advection functions return fresh grids while `rifts` mutates in
  place. Verify: `npm run check` (`tsc --noEmit`) exits 0 and
  `npx vitest run tests/surface.test.ts` passes.

- [x] 4.3 Create `tests/rifts-advection.test.ts` covering: rifts in-place
  identity (returned wrapper is the input wrapper) versus advection new-grid
  identity (returned wrapper is not either input); plain-input isolation for all
  three; required `kw`/`angle`/`amplitude`/`seed` and `nparticles`/`seed` errors
  naming the missing argument; omitted options equal every explicit default; an
  asymmetric bbox (`[0, 1, 0.25, 0.75]`) differs from the reordered
  `[0, 0.25, 1, 0.75]`; the `applyMask`/`reverseMask` quirk; `field` shape
  mismatch rejected naming `field`; freed HArray inputs rejected; deferred
  inputs absent (the manifest deferral prose names the unbound arguments, passing
  a mask/noise-offset/multi-field/explicit-velocity option leaves the result
  unchanged, and an array of fields throws); determinism and finiteness
  of both advection outputs; `advectionWarp` with `advectionLength: 0` returns
  all zeros; `advectionParticle` with `iterations: 0` returns the field; and
  `iterations: 3` equals three chained `iterations: 1` calls. Verify:
  `npx vitest run tests/rifts-advection.test.ts tests/surface.test.ts` is
  green.

- [x] 4.4 Pin the new surface in goldens: add `rifts-seed11` (64x64
  `SIMPLEX2S` fBm, remapped, `kw = (4, 1.2)`, angle 30, amplitude 0.1, seed 11,
  every binding argument spelled out with the 4.1 defaults) to
  `scripts/golden-capture.mjs` using the raw `rifts` binding, and
  `advection-particle-seed11` / `advection-warp-seed11` (64x64
  remapped fBm terrain and a second remapped fBm field; particle
  `nparticles = 256`, seed 11, `iterations = 1`;
  warp `advectionLength = 0.1`, `valuePersistence = 0.9`) using the raw
  bindings with every argument spelled out; add the three corresponding `it`
  blocks to `tests/golden.test.ts` through the high-level API and recapture
  `tests/golden/scenes.json` with `npm run golden:capture`. Verify: the capture
  prints three new scenes, `npx vitest run tests/golden.test.ts` is green, and
  the `scenes.json` diff adds exactly the three new keys.

## 5. GPU parity suite, calibration, CI and report

- [x] 5.1 Extend `tests/src/test_rifts_advection_cpu.cpp` (created in 2.1) with
  the file header documenting the `PARITY`, `PARITY_PCT` and `PARITY_AGG` line
  formats and the two suites from design D7 (`RiftsAdvectionCpuInvariants`,
  `RiftsAdvectionParity`). Copy the direct-kernel image helpers
  (`choose_float_image_format`, `make_image`, `read_image`, `set_image`) from
  `tests/src/test_hydraulic_cpu.cpp` into this file's anonymous namespace per
  design D7 (extraction stays deferred; do not reopen the Open Question). Add
  the rifts parity cases against the healthy `hmap::gpu::rifts` wrapper:
  clamped fBm 32x32 and synthetic ramp 32x32, one 64x64 fBm neighbourhood case,
  angle 0 and 90, amplitude 0.05 and 0.2, one supplied mask and one noise-offset
  case. Print
  `PARITY,<variant>,<scene>,<iters>,<max_abs>,<rms_abs>,<range>` and
  `PARITY_PCT,<variant>,<scene>,<iters>,<p99_abs>,<frac_over>,<threshold>,
  <range>` with `threshold = 1e-2 * range`, and record every metric with
  `RecordProperty`. Verify: the parity job compiles the file and
  `./build/bin/highmap_tests --gtest_filter='RiftsAdvectionParity.Rifts*'`
  produces both metric line types with no unexpected skip; the provisional
  gates (`rms <= 2e-3 * range`, `p99 <= 2e-2 * range`, `frac_over <= 2e-2`)
  pass or are calibrated in 5.4.

- [x] 5.2 Add the warp parity cases using the direct `advection_warp` kernel
  with the `CL_R` -> `CL_RGBA` probe from `choose_float_image_format()`: 32x32
  clamped fBm and ramp scenes, `advectionLength` 0.01/0.05/0.1 crossed with
  `valuePersistence` 0.5/0.96/1.0, one supplied mask case and one explicit
  constant `dx`/`dy` case. Exclude flat starts from the bounds (the `dir =
  (0, 0)` deviation) and print `PARITY` plus `PARITY_PCT` lines. Verify: the
  parity job runs every warp case with `CL_R` or `CL_RGBA` and no skip, and the
  gates (`rms <= 1e-3 * range`, `p99 <= 1e-2 * range`, `frac_over(1e-2) <=
  1e-2`) pass or are calibrated in 5.4.

- [x] 5.3 Add the particle parity cases using a direct `advection_particle`
  kernel launch with explicitly zeroed `out` (one grid per field) and `count`
  buffers uploaded via `write_buffer` (add the local buffer helpers this
  reference needs; the hydraulic file has image helpers only), replicating the
  wrapper host stage `(accum + original) / (count + 1)` clamped to `[0, 1]`,
  the optional laplace and mask blend in the test. Scenes: 32x32 fBm and ramp;
  one particle per cell (`nparticles = 1024`) gated per cell at `max <= 1e-5 *
  range`, `rms <= 2e-6 * range`; an aggregate of at most 1000 particles with
  `iterations` 1 and 2 (run the single-pass kernel once or twice, chaining the
  output field), `reverse`, `postFilter = false` and `inertia` 0/0.2 gated only
  on `rel_total <= 5%` and `mean |dz| <= 1e-2 * range` via `PARITY_AGG`, with
  per-cell `max`/`rms` recorded as diagnostics; run the aggregate twice on the
  GPU side and record the GPU-vs-GPU variability. Verify: the parity job runs
  every particle case with no unexpected skip and the gated metrics hold or are
  calibrated in 5.4.

- [ ] 5.4 Calibrate and freeze the parity bounds from the first full oracle
  run, then run one deliberate mutation per function and confirm each breaks
  its gate before reverting: a rifts hash or smoothing constant (for example
  `43758.5453123f` -> `43758.5f`), the warp persistence factor or the
  downhill/uphill sign in its z-driven gradient, and the particle persistence
  or step sign. Record the measured values and mutation results for
  `docs/rifts-advection-parity.md`. Verify: CI is green with the frozen
  constants in `tests/src/test_rifts_advection_cpu.cpp`, each mutated run fails
  the corresponding gate, and `git diff` shows the mutation lines reverted.

- [ ] 5.5 Extend `.github/workflows/erosion-parity.yml`: raise
  `timeout-minutes` from 120 to 180, add a third run step
  `--gtest_filter='RiftsAdvectionCpuInvariants.*:RiftsAdvectionParity.*'` with
  `timeout-minutes: 20`, XML `build/rifts-advection-parity.xml` and
  `build/rifts-advection-parity.log`, a `^PARITY,` presence check, the third
  XML in the skip guard (still allowing only the image-format skip), the third
  XML/log in the uploaded artifact set, and `PARITY_PCT,` and `PARITY_AGG,`
  sections in the job summary. Keep the `no_device` dispatch input. Verify:
  push and confirm the job runs all three suites, publishes the new metric
  sections and artifact, and that a `no_device` dispatch fails through the new
  suite instead of skipping it.

- [ ] 5.6 Write `docs/rifts-advection-parity.md` (oracle device/driver, exact
  harness invocation, frozen bound table, measured metrics, mutation-check
  table, wrapper gaps for particle `out`/`count` and warp `CL_R`, the
  `CL_RGBA` fallback, the warp `dir` and particle inertia deviations, the
  negative-elevation `pow` domain caveat, the `PARITY_PCT` definition and
  measured Node wasm cost), add AD-028 to `docs/decisions.md` (hash
  discontinuity, mirrored-repeat sampler, missing zero uploads, non-atomic
  accumulation, UB deviations), append section 12 to
  `docs/porting-learnings.md` (hash-based noise gates on percentiles, image
  sampler fidelity, non-atomic accumulation), and mark the three functions
  done in `docs/wasm-port-research.md`'s phase-2 status, noting the primitives
  family as the only remaining GPU-only block outside phase-2 scope. Verify:
  every command in the report runs as written, the recorded numbers match the
  green CI run, `grep -n "AD-028" docs/decisions.md` succeeds after AD-027,
  `grep -n "^## 12" docs/porting-learnings.md` matches, and the research doc no
  longer lists `rifts`/`advection_*` as unported.

## 6. Integration gate

- [x] 6.1 Run the full gate from a clean state:
  `EMSDK=C:/WS/tools/emsdk npm run build`, `npm test` (all vitest suites,
  bundler and quickstart checks), `npm run check`, and
  `openspec validate add-rifts-advection-cpu-ports --strict`. Verify: every
  command exits 0, `dist/index.js` and `dist/index.d.ts` exist, and the vitest
  output includes `tests/surface.test.ts`, `tests/rifts-advection.test.ts` and
  `tests/golden.test.ts`.

- [x] 6.2 Audit the change scope and spec coverage: `git status --short` lists
  only the recorded new files (`HighMap/include/highmap/erosion_rifts_cpu.hpp`,
  `HighMap/include/highmap/transform_advection_cpu.hpp`,
  `HighMap/src/erosion/rifts_cpu.cpp`,
  `HighMap/src/transform/advection_cpu.cpp`,
  `tests/src/test_rifts_advection_cpu.cpp`, `tests/rifts-advection.test.ts`,
  `docs/rifts-advection-parity.md`) and the recorded edits
  (`HighMap/wasm/{manifest.md,bindings.cpp,sources.cmake}`,
  `src/{api/highmap.ts,loader/module.ts,index.ts}`, `scripts/index.d.ts`,
  `scripts/golden-capture.mjs`, `tests/{surface.test.ts,golden.test.ts,golden/
  scenes.json}`, `.github/workflows/erosion-parity.yml`, `README.md`,
  `docs/{wasm-port-research.md,decisions.md,porting-learnings.md}`); confirm
  `git diff -- HighMap/src/erosion/rifts_gpu.cpp
  HighMap/src/transform/advection_particle_gpu.cpp
  HighMap/src/transform/advection_warp_gpu.cpp
  HighMap/include/highmap/erosion.hpp
  HighMap/include/highmap/transform.hpp` is empty. Cross-check the three spec
  deltas' scenarios against the named parity/invariant/JS cases. Verify: the
  status list matches the recorded set exactly, the upstream diff is empty, and
  every scenario has a named test or parity case.

## Workflow follow-up

- Before committing, confirm `git config user.email` returns
  `6848069+mode777@users.noreply.github.com` (set the repo-local identity back
  if it does not), then commit and push with the mode777 identity.
- Confirm the `build-wasm` and `erosion-parity` jobs are green on the pushed
  commit and record the metrics from that run in
  `docs/rifts-advection-parity.md`.
- Archive only after review confirms the parity metrics, the mutation checks and
  the full gate are green, then sync the `rifts-advection-cpu-ports`,
  `terrain-api` and `wasm-build` deltas into `openspec/specs/` as part of the
  archive.
