# Tasks

## 1. Foundation: declarations, source list, decision log

- [x] 1.1 Create `HighMap/include/highmap/erosion_hydraulic_cpu.hpp` declaring
  `hydraulic_mcdonald`, `hydraulic_mcdonald_multiscale`, both
  `hydraulic_schott` overloads, `hydraulic_schott_erosion` and
  `hydraulic_vpipes` in `namespace hmap` with the public GPU declarations'
  argument lists and defaults; add the provenance comment (kernel
  transcription, output-map contract, rebase rule: delete header + port file
  when upstream declares CPU implementations). Verify with a throwaway TU that
  includes only this header and compiles:
  `emcc -std=c++20 -fsyntax-only -I HighMap/include -I HighMap/wasm/stubs -x c++ -`
- [x] 1.2 Create `HighMap/src/erosion/hydraulic_cpu.cpp` with the file header,
  includes and the shared anonymous-namespace helpers transcribed from the
  kernels: `hash21u`, `mcd_uniform`, `clamp_index` (clamp-to-edge sampling),
  `bilinear_sample_clamp`, `mix` (`a + rate * (b - a)`) and the Schott
  image-sampling helpers. Verify no GPU dependency:
  `grep -nE "clwrapper|gpu::|OpenCL" HighMap/src/erosion/hydraulic_cpu.cpp`
  matches comments only, and the file compiles in the wasm build (1.3).
- [x] 1.3 Add `erosion/hydraulic_cpu.cpp` to `HighMap/wasm/sources.cmake`.
  Verify: `EMSDK=C:/WS/tools/emsdk bash scripts/build-wasm.sh --no-tests`
  succeeds and `dist/highmap.wasm` is regenerated with the skeleton linked.
- [x] 1.4 Record the port placement and provenance as a new entry in
  `docs/decisions.md` (new files under `HighMap/src`/`HighMap/include` only,
  same-source property for the parity oracle, `_cpu` duplicate-symbol rebase
  rule, and the three upstream wrapper gaps that motivate the direct-kernel
  oracle). Verify: the entry is numbered after AD-021 and is referenced from
  the port file's header comment.

## 2. McDonald: port, bindings, JS surface and tests

- [x] 2.1 Port `hydraulic_mcdonald` and `hydraulic_mcdonald_multiscale` in
  `hydraulic_cpu.cpp`: `mcd_grad5`, the solve/filter/debris step loop with
  fixed sample order, the multiscale halving ladder and `resample_to_shape`,
  the final `bed + sed` terrain, and conditional sediment/discharge outputs.
  Verify: the desktop build compiles, and new
  `HydraulicCpuInvariants.McDonald*` cases in
  `tests/src/test_hydraulic_cpu.cpp` pass (determinism, output-map shapes,
  multiscale determinism, zero-step no-op).
- [x] 2.2 Bind and wrap McDonald: add manifest rows for `hydraulicMcdonald` and
  `hydraulicMcdonaldMultiscale`, the `bindings.cpp` wrappers with nullable
  `hmap::Array*` outputs (`allow_raw_pointers()`) and the
  `emscripten::val` → `std::vector<int>` helper for `stepsPerLevel`, the
  `src/loader/module.ts` signatures, the `src/api/highmap.ts` wrappers and
  option types, the `inplaceWithOutputs` helper (HArray liveness, shape and
  aliasing validation, nullable raw handles), the `src/index.ts` exports and
  the `scripts/index.d.ts` declarations. Verify:
  `npx vitest run tests/surface.test.ts` passes and the new names are asserted.
- [x] 2.3 Create `tests/hydraulic.test.ts` with McDonald cases: in-place
  `HArray`, plain-input isolation, required `steps`/`seed` rejection, omitted
  options equal explicit defaults, scalar-vs-grid talus is not applicable,
  `sediment`/`discharge` output maps filled with the grid shape, mismatched and
  aliased outputs rejected, omitted outputs equal null outputs, determinism and
  finiteness. Verify: `npx vitest run tests/hydraulic.test.ts` is green.

## 3. Schott: port, bindings, JS surface and tests

- [x] 3.1 Port `hydraulic_schott` (non-mask and mask overloads) and
  `hydraulic_schott_erosion` in `hydraulic_cpu.cpp`: clamp-to-edge slope,
  steepest-neighbour and flow-weight helpers, the `it % 10` phase cycle,
  erosion/thermal/deposition branches, the erosion-only moisture/flow form, the
  mask blend and the flow output. Verify: new `HydraulicCpuInvariants.Schott*`
  cases pass (determinism, zero-iteration no-op, mask blend equals the per-cell
  blend of the endpoints, flow output shape, erosion-only form).
- [x] 3.2 Bind and wrap Schott: manifest rows for `hydraulicSchott` and
  `hydraulicSchottErosion` (with the `flow` output and the mask/moisture
  deferral note), bindings, loader signatures, API wrappers and option types,
  `scripts/index.d.ts`, `src/index.ts` exports and the surface test. Verify:
  `npx vitest run tests/surface.test.ts` passes and the new names are asserted.
- [x] 3.3 Extend `tests/hydraulic.test.ts` with Schott cases: required `talus`,
  scalar-vs-constant-grid talus, `iterations` default 10, `flow` output filled,
  zero iterations leaves the terrain unchanged, determinism and finiteness.
  Verify: `npx vitest run tests/hydraulic.test.ts` is green.

## 4. Vpipes: port, bindings, JS surface and tests

- [x] 4.1 Port `hydraulic_vpipes` in `hydraulic_cpu.cpp`: the host-side rain
  increment, flux pass (early exit, diffusion, `sum > 1e-5` normalization),
  water pass (divergence, evaporation, velocity), erosion pass (gradient norm,
  capacity, erode/depose) and sediment-transport pass (clamp-to-edge bilinear
  advection), with fresh-array swaps and conditional water-depth, sediment and
  velocity outputs. Verify: new `HydraulicCpuInvariants.Vpipes*` cases pass
  (determinism, non-negative water depth, zero-iteration no-op, flux-diffusion
  and maintain-volume option effects, output shapes).
- [x] 4.2 Bind and wrap vpipes: manifest row for `hydraulicVpipes` (with the
  `waterDepth`, `sediment`, `velU`, `velV` outputs and the rain-map deferral
  note), binding, loader signature, API wrapper and option types,
  `scripts/index.d.ts`, `src/index.ts` exports and the surface test. Verify:
  `npx vitest run tests/surface.test.ts` passes and the new name is asserted.
- [x] 4.3 Extend `tests/hydraulic.test.ts` with vpipes cases: C++ defaults,
  output maps filled with the grid shape, mismatched and aliased outputs
  rejected, zero iterations leaves the terrain unchanged, determinism and
  finiteness. Verify: `npx vitest run tests/hydraulic.test.ts` is green.

## 5. Locking the family: goldens, direct-kernel parity, calibration

- [x] 5.1 Add small deterministic scenes for `hydraulicMcdonald`,
  `hydraulicSchott` and `hydraulicVpipes` to `scripts/golden-capture.mjs` and
  `tests/golden.test.ts`, recapture `tests/golden/scenes.json` from the rebuilt
  artifact, and confirm the JSON diff adds only the new scenes. Verify:
  `npm run golden:capture && npm test` is green.
- [x] 5.2 Add the direct-kernel oracle helpers to
  `tests/src/test_hydraulic_cpu.cpp`: the `CL_R` → `CL_RGBA` float-image probe
  and binding helper, the Schott reference that passes `it`, and the vpipes
  reference that runs the four pass kernels in the fixed order. Verify: the
  file compiles in the desktop build and the helpers run a 1-iteration smoke
  case on CI without an unexpected skip.
- [x] 5.3 Add `HydraulicParity` cases: McDonald single-particle per-cell and
  multi-particle aggregate metrics; Schott phase-isolated cases
  (erosion-only, erosion + thermal, default mix) plus the erosion-only form;
  vpipes cases covering flux diffusion, maintain-volume, evaporation and
  iteration count. Emit `PARITY,...` CSV lines and gtest properties. Verify:
  the CI parity job runs every case and the provisional bounds hold or are
  calibrated from the measured values.
- [x] 5.4 Calibrate and freeze the tolerance table in
  `tests/src/test_hydraulic_cpu.cpp`; run one mutation per algorithm (McDonald
  transfer coefficient, Schott erosion/phase coefficient, vpipes capacity
  coefficient), confirm each breaks its bound, then revert. Verify: CI is green
  with the frozen table and the mutation runs are logged.
- [x] 5.5 Write `docs/hydraulic-parity.md`: the oracle device/driver, the exact
  harness invocation, the frozen bound table, the mutation-check results, the
  measured metrics of the first green run, the `CL_RGBA` fallback, the three
  wrapper gaps and the McDonald atomic-order split. Update
  `docs/porting-learnings.md` with the direct-kernel-oracle and output-map
  lessons. Verify: every command in the document runs as written and the
  recorded numbers match a fresh CI run.

## 6. CI and integration

- [x] 6.1 Rename `.github/workflows/thermal-parity.yml` to
  `.github/workflows/erosion-parity.yml`, add the hydraulic suite run step,
  update the job name, keep the `no_device` dispatch input, allow only the
  known "no float image support" skips, and include both metric sets in the job
  summary and uploaded artifacts. Verify: push and confirm the job runs both
  suites and publishes metrics; insert a deliberately failing assertion,
  confirm the job fails, and revert.
- [x] 6.2 Update `README.md` (API list gains the five hydraulic functions and
  the output-map contract, with the single-threaded cost caveat),
  `docs/wasm-port-research.md` (phase 2: hydraulic family complete, remaining
  `rifts` and `advection_*`), and `docs/decisions.md` (output-map contract,
  direct-kernel oracle, McDonald atomic-order parity). Verify:
  `node scripts/test-quickstart.mjs` still passes and the research doc no longer
  claims the hydraulic family is unported.
- [ ] 6.3 Integration check: clean rebuild
  (`EMSDK=C:/WS/tools/emsdk bash scripts/build-wasm.sh --no-tests &&
  node scripts/build-js.mjs && npm test`), `openspec validate
  add-hydraulic-cpu-ports`, and `git status` showing additions plus only the
  recorded edits under `HighMap/src`/`HighMap/include`. Verify: all commands
  exit 0, locally and in the pushed CI runs (`build-wasm`, `erosion-parity`).

## Workflow follow-up

- Commit and push with the mode777 identity after apply; confirm the
  `build-wasm` and `erosion-parity` jobs are green on the pushed commit and
  record the parity metrics from that run in `docs/hydraulic-parity.md`.
- Archive the change after review, syncing the `hydraulic-cpu-ports`,
  `terrain-api` and `wasm-build` deltas.
