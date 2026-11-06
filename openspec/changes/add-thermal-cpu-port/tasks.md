# Tasks

## 1. Foundation: declarations, fidelity helpers, build wiring

- [x] 1.1 Create `HighMap/include/highmap/erosion_thermal_cpu.hpp` declaring
  `thermal`, `thermal_auto_bedrock`, `thermal_conserve`, `thermal_flatten`,
  `thermal_inflate`, `thermal_olsen`, `thermal_rib`, `thermal_ridge` and
  `thermal_scree` in `namespace hmap` with the argument lists and defaults of
  their `hmap::gpu::*` counterparts, plus the four `thermal_schott` overloads
  exactly as `src/erosion/thermal_schott.cpp` defines them; add the provenance
  comment (kernel transcription, rebase rule: delete header + port file when
  upstream declares CPU implementations). Verify with a throwaway TU that
  includes only this header and compiles:
  `emcc -std=c++20 -fsyntax-only -I HighMap/include -I HighMap/wasm/stubs -x c++ -`
- [x] 1.2 Create `HighMap/src/erosion/thermal_cpu.cpp` with the file-local
  helpers transcribed from the kernels — `helper_thermal_exchange`,
  `smoothstep3`, `almost_unit_identity`, `hash21u`, `freeze_border_1` (kernel
  check order, corners included), `clamp_border_band(b)`, and the 8-neighbour
  tables with each kernel's own literals — and the `thermal` port (double
  buffering, 1-cell freeze, `extrapolate_borders` after the loop, deposition
  `max(z - z0, 0)`). Verify no GPU dependency
  (`grep -nE "clwrapper|gpu::|OpenCL" HighMap/src/erosion/thermal_cpu.cpp`
  matches comments only) and that the file compiles in the wasm build (1.3).
- [x] 1.3 Add the new header to the port's includes and
  `erosion/thermal_cpu.cpp` to `HighMap/wasm/sources.cmake`. Verify:
  `EMSDK=C:/WS/tools/emsdk bash scripts/build-wasm.sh --no-tests` succeeds and
  `dist/highmap.wasm` is regenerated.
- [x] 1.4 Record the placement decision as a new entry in `docs/decisions.md`
  (ports live in `HighMap/src`/`HighMap/include` as new files only, why the
  same-source property matters for the parity oracle, the duplicate-symbol
  rebase rule, and the amended `wasm-build` rule). Verify: the entry is numbered
  after the last AD and is referenced from the port file's header comment.

## 2. Parity harness and CI gate, proven on `thermal`

- [ ] 2.1 Create `tests/src/test_thermal_cpu.cpp` with the two suites
  (`ThermalCpuInvariants` — no OpenCL needed; `ThermalParity` — skips with
  `GTEST_SKIP()` when no device is present), deterministic scene builders
  (seeded fBm 128², ramp + plateau, single spike), metric helpers (max abs, RMS,
  input range), a CSV line per case, and `RecordProperty` output. Verify: the
  file is picked up by the `src/*.cpp` glob and the desktop build compiles it in
  the workflow added by 2.2.
- [ ] 2.2 Create `.github/workflows/thermal-parity.yml`: ubuntu-latest, the apt
  set from `cmake.yml` including `pocl-opencl-icd`, configure with examples and
  benchmarks off, build `highmap_tests`, run
  `--gtest_filter='ThermalCpuInvariants.*:ThermalParity.*' --gtest_output=xml`,
  fail if the XML reports a skipped case, upload the XML and CSV log, and write
  the metrics into the job summary. Verify: push and confirm the job runs and
  uploads the artifact; then insert a deliberately failing assertion, confirm
  the job fails, and revert.
- [ ] 2.3 Add the `thermal` parity case (parameter matrix: 1 and 10 iterations,
  talus `0.1/ni` and `2/ni`) and the invariants it can prove without a device
  (zero iterations leaves the interior untouched and extrapolates one border
  cell; repeated calls are byte-identical; deposition is non-negative and does
  not alias the input). Verify: CI job green, and the case's max/RMS values are
  inside the provisional bound from design D5.

## 3. Slice A: core thermal variants on the JS surface

- [x] 3.1 Bind `thermal`: manifest row (JS name, signature, `iterations: 10`
  default), `bindings.cpp` wrapper taking `const hmap::Array &talus`, a shared
  `talusGrid` helper in `src/api/highmap.ts` that materializes a constant wasm
  grid for numeric talus and deletes it in `finally`, the module declaration in
  `src/loader/module.ts`, the API implementation and `ThermalOptions` export in
  `src/index.ts`, and the updated expected-name list in `tests/surface.test.ts`.
  Verify: `npx vitest run tests/surface.test.ts` passes and the new name is
  asserted.
- [x] 3.2 Create `tests/thermal.test.ts` covering in-place return for `HArray`
  input, plain-input isolation, scalar talus equals a constant talus grid,
  omitted options equal explicit defaults, `iterations: 0` interior no-op,
  mismatched talus shape rejection before the wasm call, and byte-identical
  repeat runs. Verify: `npx vitest run tests/thermal.test.ts` is green.
- [ ] 3.3 Port, bind and test `thermal_auto_bedrock`: bedrock seeded to
  `-FLT_MAX` before the first iteration, per-iteration
  `max(z_init, new_z)` tracking, deposition output, 1-cell freeze plus
  post-loop extrapolation; harness case in `tests/src/test_thermal_cpu.cpp`
  including the "no cell below its starting elevation" invariant (the kernel
  clamps to the highest level reached, so this variant deposits rather than
  erodes); manifest row,
  binding, API and vitest cases. Verify: vitest green and the CI parity case
  inside its bound.
- [ ] 3.4 Port, bind and test `thermal_conserve`: closed boundary (border cells
  copied, no post-loop extrapolation), `rate` default 0.5, bedrock applied only
  after the loop, pull-pattern outflow/inflow exactly as the kernel; harness
  cases plus a sum-conservation invariant; manifest row, binding, API and
  vitest cases (border untouched, total elevation preserved, `rate` honoured).
  Verify: vitest green and the CI parity case inside its bound.

## 4. Slice B: directed-transfer variants

- [ ] 4.1 Port, bind and test `thermal_flatten`: 2-cell clamp band,
  `(gx + gy) & 7` rotation for both the centre and the donating neighbour,
  steepest-receiver search with the centre's rotation, `sigma_inf`/`sigma_sup`
  defaults 0.5/0; harness cases (band clamp, `sigma_sup` effect) and vitest
  cases. Verify: vitest green and the CI parity case inside its bound.
- [ ] 4.2 Port, bind and test `thermal_olsen`: 3-cell clamp band, weighted loss
  pass, gain pass rotated by `hash21u(gx, gy) & 7` with the kernel's
  accumulation order; harness cases (band clamp, byte-identical repeats) and
  vitest cases. Verify: vitest green and the CI parity case inside its bound.
- [ ] 4.3 Port, bind and test `thermal_rib`: `z - min_k |z - z_k| / c_k`, 1-cell
  freeze, `extrapolate_borders(z, 3)`; harness cases (gradient monotonicity,
  3-cell border) and vitest cases (every interior cell decreases or stays equal,
  determinism). Verify: vitest green and the CI parity case inside its bound.

## 5. Slice C: local-slope variants and Schott

- [ ] 5.1 Port, bind and test `thermal_inflate`: negative-slope sum,
  `amp = smoothstep3(fmin(fmax(1 - talus/slope_max, 0), 1))`, 1-cell freeze;
  harness case including the flat-grid case (`slope_max = 0` must yield
  `amp = 0`, not NaN) and vitest cases. Verify: vitest green and the CI parity
  case inside its bound.
- [ ] 5.2 Port, bind and test `thermal_ridge`: positive-slope sum with the same
  amplitude shape plus `abs(z - z0)` deposition; harness case and vitest cases.
  Verify: vitest green and the CI parity case inside its bound.
- [ ] 5.3 Port, bind and test `thermal_scree`: negative-slope sum scaled by
  `almost_unit_identity(fmin(fmax(1 - z/zmax, 0), 1))`; harness case and vitest
  cases (`zmax` limits the effect; a non-positive ceiling is documented as
  caller error, not guarded, matching C++). Verify: vitest green and the CI
  parity case inside its bound.
- [ ] 5.4 Bind the existing upstream `thermal_schott`: add
  `erosion/thermal_schott.cpp` to the curated source list, manifest row with
  `intensity: 0.2`, binding and API, harness cases for determinism and the
  shape of the result, plus vitest cases; document in the manifest that this
  variant extrapolates borders every iteration and is therefore excluded from
  the parity bounds. Verify: vitest green, `tests/surface.test.ts` sees all ten
  names, and the manifest's deferred note lists masks and deposition.

## 6. Locking the family: goldens, calibration, parity report

- [x] 6.1 Add two thermal scenes (`thermal-seed11`, `thermal-conserve-seed11`)
  to `scripts/golden-capture.mjs` and `tests/golden.test.ts`, recapture
  `tests/golden/scenes.json` from the rebuilt artifact, and confirm the JSON
  diff adds only the new scenes. Verify: `npm run golden:capture && npm test`
  is green.
- [ ] 6.2 Calibrate and freeze the tolerance table in
  `tests/src/test_thermal_cpu.cpp` from the first full parity run (record
  max/RMS per variant), tighten bounds that are orders of magnitude loose, and
  run the two mutation checks (a `thermal` coefficient and the `thermal_olsen`
  rotation): each must fail its bound, then revert. Verify: CI green with the
  frozen table and both mutation runs logged.
- [ ] 6.3 Write `docs/thermal-parity.md`: the oracle device/driver, the exact
  harness invocation, the frozen bound table, the mutation-check results, the
  measured metrics of the first green run, the `thermal_schott` deviation note,
  and a runtime-cost section measured locally on this machine (512²×10 and
  1024²×500 for `thermal`, with the command used). Verify: every command in the
  document runs as written and the recorded numbers match a fresh run (CI log
  for the metrics, local Node for timings).

## 7. Documentation and integration

- [x] 7.1 Update `README.md` (API model list gains the ten thermal functions,
  talus as number or grid, the deferred mask/deposition note, and the
  single-threaded cost caveat) and `docs/wasm-port-research.md` (phase 2 status:
  thermal family complete, remaining families listed). Verify:
  `node scripts/test-quickstart.mjs` still passes and the research doc no longer
  claims the thermal family is unported.
- [ ] 7.2 Add AD entries for the CI parity oracle (PoCL on ubuntu, skipped case
  equals failure), the deferred JS overloads, and the thermal-surface defaults
  (including `iterations: 10` for the three variants with no C++ default).
  Verify: entries follow the existing numbering and the README/manifest point to
  them.
- [ ] 7.3 Integration check: clean rebuild
  (`EMSDK=C:/WS/tools/emsdk bash scripts/build-wasm.sh --no-tests &&
  node scripts/build-js.mjs && npm test`), `openspec validate
  add-thermal-cpu-port`, and `git status` showing additions plus only the
  recorded edits under `HighMap/src`/`HighMap/include`. Verify: all commands
  exit 0, locally and in the pushed CI runs (`build-wasm`, `thermal-parity`).

## Workflow follow-up

- Commit and push with the mode777 identity after apply; confirm the
  `build-wasm` and `thermal-parity` jobs are green on the pushed commit and
  record the parity metrics from that run in `docs/thermal-parity.md`.
- Archive the change after review, syncing the `thermal-cpu-ports`,
  `terrain-api` and `wasm-build` deltas.
