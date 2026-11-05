# Tasks

## 1. Package scaffold and docs baseline

- [x] 1.1 Create root `package.json` (private, `"type": "module"`, no runtime
  deps, `files` allowlist, `engines.node >= 18`), `tsconfig.json`, vitest
  config, and `.gitignore` additions (`dist/`, `build/`, `node_modules/`);
  verify `npm install` succeeds and `npm test` runs a passing empty suite
- [x] 1.2 Create `docs/decisions.md` ADR log seeded with the locked
  exploration decisions (hybrid marshalling, camelCase, ESM-only, root
  package layout, embind-not-C-shim, standalone `HighMap/wasm/` CMake,
  manifest-driven surface growth, seed scope ~30 fns with the broad curated
  API as the stated end goal); verify the file exists with one numbered AD
  entry per decision

## 2. Minimal end-to-end wasm build

- [x] 2.1 Create `HighMap/wasm/CMakeLists.txt` as a standalone Emscripten
  project (own project root, never included by the upstream build),
  `HighMap/wasm/stubs/opencv2/core.hpp` forwarding `cv::Mat`, and
  `HighMap/wasm/sources.cmake` with a minimal compiling seed list
  (`array.cpp` + its noise dependency chain only); verify `emcmake cmake`
  configure + build succeeds on a machine with only emsdk and the small
  vendored submodules
- [x] 2.2 Create `HighMap/wasm/bindings.cpp` exposing `hmap::Array`
  (constructors, shape, data access) and `noise` via embind with the D10
  flag baseline; verify the build produces `dist/highmap.wasm` + ES6 glue
  and a raw Node script instantiating the factory can construct an Array
  and call `noise`
- [x] 2.3 Create `scripts/build-wasm.sh` (pinned Emscripten version,
  configure/build/copy-to-dist steps) runnable under git-bash; verify a
  rerun from a clean tree reproduces the full `dist/` artifact set

## 3. Loader: `init()` and artifact location

- [x] 3.1 Implement `src/loader/` `init(options?)` with default
  `new URL('./highmap.wasm', import.meta.url)` location and
  `wasmBinary` / `locateWasm` override precedence; verify unit tests cover
  default resolution and both overrides in Node
- [x] 3.2 Create the package entry `src/index.ts` exporting `init` and
  types; verify a Node test imports the package entry, awaits `init()`,
  and reaches the bound `noise` function
- [x] 3.3 Implement clear rejection errors when the artifact cannot be
  located/fetched/instantiated; verify a unit test with a broken
  `locateWasm` URL asserts a descriptive rejection

## 4. Grid marshalling (`HArray`)

- [x] 4.1 Implement `HArray` (shape, `data` getter that re-binds to the
  current wasm memory, `copyData()`, idempotent `free()`,
  `FinalizationRegistry` safety net, package error type) in `src/api/`;
  verify unit tests cover view contents, copy-survives-free, double-free,
  use-after-free throwing, and registry reclaim under forced GC
- [x] 4.2 Implement the shared input coercion helper accepting `HArray` or
  `{ data: Float32Array, shape: [ni, nj] }` with full pre-wasm validation
  (Float32Array, two positive integer shape entries, length match, error
  naming the argument); verify unit tests assert identical results for both
  input forms and rejection of a length mismatch and a `Uint8Array` input
- [x] 4.3 Verify the `[ni, nj]` row-major shape convention with a
  known-values small-grid round-trip test (generate, read back, assert
  element order)

## 5. Seed API surface

- [x] 5.1 Create the binding manifest (`HighMap/wasm/manifest.md`)
  enumerating every bound function (C++ name, JS camelCase name, options,
  defaults) and grow `bindings.cpp` + `src/api/` from it; verify a test
  asserts the exposed API surface equals the manifest exactly
- [x] 5.2 Bind the 11 pyHighMap-parity functions (`noise`, `noiseFbm`,
  `noisePingpong`, `noiseRidged`, `warp`, `falloff`, `clamp`, `remap`,
  `plateau`, `smoothFill`, `hillshade`) plus `NoiseType` /
  `DistanceFunction` enums with pyHighMap defaults as trailing options
  objects; verify per-function tests pass (requested shape, determinism,
  omitted-options-equals-explicit-defaults, enum selects flavor)
- [x] 5.3 Bind the algebra layer (`add`, `subtract`, `multiply`, `divide`
  with grid-grid and grid-scalar forms, `scale`); verify element-wise unit
  tests for both forms
- [x] 5.4 Bind the CPU erosion twins (`hydraulicMusgrave`,
  `hydraulicStream`); verify shape preservation and same-seed determinism
  tests pass
- [x] 5.5 Add golden-value snapshot tests captured from the artifact
  itself (noise family, warp, one erosion) with a documented recapture
  procedure; verify `npm test` is green with goldens checked in

## 6. Integration checks

- [x] 6.1 Add a consumer bundler test that packs/bundles a scratch esbuild
  (or Vite) project importing the package and asserts the `.wasm` is
  emitted as an asset and `init()` works from the bundle; verify the test
  script passes
- [x] 6.2 Verify build invariants: `git status` shows no modifications
  under `HighMap/src`, `HighMap/include`, or either upstream CMakeLists
  after a full build, except the single flagged upstream fix from AD-014
  (`hydraulic_musgrave.cpp` guard); and the desktop CMake configure still
  succeeds
  _(`git status` confirmed exactly the one flagged file. Desktop configure
  cannot run on this machine at all — no C/C++ compiler or system libraries
  are installed; it fails inside `project()` before reaching any project
  code, which is environmental and unchanged by this work: neither upstream
  CMakeLists was touched. Desktop build validation is deferred to a properly
  provisioned machine / CI.)_
- [x] 6.3 Write the README quick start (install, `init()`, pyHighMap-style
  example) matching the real API; verify the documented example runs
  verbatim as a Node script

## Workflow follow-up

- Archive the change once review requirements are satisfied
  (`/opsx-archive`), then verify the archived specs.
- Follow-up changes (per `docs/decisions.md` and the research doc): grow the
  manifest toward the broad curated API; phase-2 CPU ports of GPU-only
  algorithms validated against the desktop OpenCL build; CI, benchmarks, and
  the npm publish change.
