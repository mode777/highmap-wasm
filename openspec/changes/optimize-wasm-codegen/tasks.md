# Tasks

## 1. Build profile

- [x] 1.1 Edit `HighMap/wasm/CMakeLists.txt` to add `WASM_OPT_FLAGS`
  (default `-O3`) and `WASM_EXTRA_FLAGS` (default empty) as cache variables,
  and use both in the release `target_compile_options` and
  `target_link_options`; leave the `WASM_ASAN` branch unchanged. Verify: a
  fresh configure (`cmake -S HighMap/wasm -B build/wasm` with Emscripten on
  `PATH`) exits 0 and `build/wasm/CMakeCache.txt` contains
  `WASM_OPT_FLAGS:STRING=-O3` and an empty `WASM_EXTRA_FLAGS`; `git diff`
  shows no other semantic change.
- [x] 1.2 Rebuild `dist/`: run `scripts/build-wasm.sh --no-tests` and
  `node scripts/build-js.mjs`. If `bash` is unavailable on the apply machine,
  run the equivalent pinned commands directly (`cmake -S HighMap/wasm -B
  build/wasm -DCMAKE_TOOLCHAIN_FILE="$env:EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"`,
  `cmake --build build/wasm --parallel`, copy `highmap.js`/`highmap.wasm` to
  `dist/`, then `node scripts/build-js.mjs`) and note the substitution.
  Verify: a Node probe against `dist/index.js` initializes the module and
  returns a finite `thermal` grid; record the new `dist/highmap.wasm` byte
  size and SHA-256 (expected size ≈ 683,602 bytes, up from 592,141).
- [x] 1.3 Confirm the actual codegen and floating-point flags. Verify: the
  compile and link command files under `build/wasm/CMakeFiles/` (or
  `ninja -C build/wasm -t commands`) show `-O3` and no `-ffast-math`,
  `-ffinite-math-only`, or reassociation flag; `git diff --stat` shows no
  changes under `HighMap/src/` or `HighMap/include/`.
- [x] 1.4 Run the full JS gate against the new artifact: `npm test` and
  `npm run check`. Verify: every test file passes (currently 255 tests
  including the showcase catalog and preview suites) and TypeScript reports
  no errors.
- [x] 1.5 Verify the single-threaded deployment contract still holds on the
  new glue. Verify: searching `dist/highmap.js` for `SharedArrayBuffer` and
  `pthread` returns no matches, and the loader tests from 1.4 initialize the
  module without cross-origin isolation.

## 2. Baseline and documentation

- [x] 2.1 Re-run the committed baseline with the new artifact: confirm
  `dist/` is the artifact from 1.2, then run `npm run bench` (full sweep,
  expected ~30-40 minutes; do not build or run other heavy work in parallel).
  Verify: the run exits 0, every manifest function appears at 256², 512² and
  1024², every cell has at least 3 reps, and the JSON `context.wasmBytes`
  equals the size recorded in 1.2. Record the results file path and total wall
  time.
- [x] 2.2 Update `docs/benchmarks.md`: replace the reference run and baseline
  table with the 2.1 measurements (date, host, Node, commit, new artifact size
  and SHA-256, profile `-O3`), update the slowest-cells section, move the
  existing `-Os` table into a dated "Historical baseline" section instead of
  deleting it, add a comparison of the two baselines (at least the heavy
  erosion and advection cells) with the rejected `-O2`/SIMD results, and make
  the pthreads protocol point at the current baseline while noting the
  historical one. Verify: spot-check at least five cells against the 2.1 JSON
  (one thermal, one hydraulic, one noise, one advection, one filter), the old
  table is still present and labeled, and every row states a measured value.
- [x] 2.3 Add AD-030 to `docs/decisions.md` recording the `-O3` choice, the
  measured profile matrix, the artifact-size trade-off (+15.4%), why `-O2` and
  `-msimd128` were rejected, and the rule that fast-math stays disabled.
  Verify: the AD number follows AD-029, the entry references
  `docs/benchmarks.md`, and the decision log renders as a valid list.
- [x] 2.4 Update `docs/wasm-port-research.md` §7 item 3: record that codegen
  optimization is done and the baseline was re-measured at the new artifact
  size, and state that the remaining phase-3 scope is the optional pthreads
  build and npm publish. Verify: no stale `592,141`/`-Os` claim remains in the
  research doc (the historical table in `docs/benchmarks.md` intentionally
  keeps the old size), and its status line still matches the repository state.

## 3. Integration verification

- [x] 3.1 Run the full local gate from the final artifact: rebuild with
  `scripts/build-wasm.sh` (or the documented direct-CMake fallback), then run
  `npm test` and `npm run check` again. Verify: both exit 0 and no test relies
  on a measured duration.
- [x] 3.2 Confirm the showcase still builds and loads the new artifact: run
  `npm run check` and `npm run build` in `site/` (use `SITE_BASE` only for a
  Pages-path check if desired). Verify: both exit 0 and the built site resolves
  `highmap.wasm` from the new `dist/` artifact.
- [x] 3.3 Inspect the final diff for scope. Verify: `git diff --stat` and
  `git status --short` show only `HighMap/wasm/CMakeLists.txt`,
  `docs/benchmarks.md`, `docs/decisions.md`, `docs/wasm-port-research.md`, and
  this change's artifacts; no `HighMap/src/`, `HighMap/include/`, workflow,
  package-manifest, test, `dist/`, or `bench/results/` changes are staged.

## Workflow follow-up

- Before committing, verify the repository resolves to the `mode777` identity
  (`git config user.email` must return
  `6848069+mode777@users.noreply.github.com`); set it repo-locally first if it
  differs.
- Commit the build change, docs, benchmarks and this change, then push to
  `main` per the project's apply guidance.
- Archive only after review confirms the new baseline was produced by the
  shipped `-O3` artifact, syncing the `wasm-build` and `benchmarks` deltas
  through the archive workflow.
