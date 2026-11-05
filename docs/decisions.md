# Decisions (ADR log)

Locked design decisions for the `highmap-wasm` port. Each entry records the
decision, its date, and the rationale. Superseding an entry adds a new AD and
marks the old one superseded — entries are never edited in place.

- **AD-001 — Hybrid grid marshalling.** (2026-11-05) API functions accept
  either an `HArray` wrapper or a plain `{ data: Float32Array, shape: [ni, nj] }`
  object (one shared coercion helper validates both), and always return
  `HArray`. Rationale: casual users write pyHighMap-style one-liners with plain
  typed arrays; pipeline users keep grids zero-copy across calls. Copy-per-call
  (pyHighMap literalism) wastes ~4 MB per hop on 1024² grids; wrapper-only
  (libfive-wasm literalism) forces lifetime concepts on every user.
- **AD-002 — camelCase API.** (2026-11-05) JS function names are camelCase
  (`noiseFbm`, `smoothFill`, `hydraulicMusgrave`). Rationale: JS-native
  conventions, matches the libfive-wasm precedent; C++/Python names stay
  discoverable via the binding manifest mapping.
- **AD-003 — ESM-only package.** (2026-11-05) Single ESM entry, async
  `init()`, no CJS build (CJS consumers use dynamic `import()`). Artifact
  located via `new URL('./highmap.wasm', import.meta.url)`; `wasmBinary` and
  `locateWasm` options override. Node >= 18, browsers, and workers share one
  entry. Rationale: proven unchanged from the libfive-wasm playbook.
- **AD-004 — Root package layout.** (2026-11-05) `package.json` at the repo
  root, JS sources in `src/`, build scripts in `scripts/`, tests in `tests/`;
  C++ binding side under `HighMap/wasm/`. The npm tarball ships only `dist/`
  plus README/LICENSE, so the fork and the package coexist cleanly.
- **AD-005 — embind as the FFI layer.** (2026-11-05) HighMap is C++ classes
  and free functions with no C API, so Emscripten's embind is the binding
  layer (libfive-wasm's hand-written C FFI applied to a C API that HighMap
  does not have). `glm` types are converted at the binding boundary and never
  exposed to JS.
- **AD-006 — Standalone Emscripten CMake project.** (2026-11-05)
  `HighMap/wasm/CMakeLists.txt` is its own project root, never included by
  the upstream build, with an explicit curated source list
  (`HighMap/wasm/sources.cmake`). Rationale: the upstream root CMake has an
  unconditional `find_package(OpenCL REQUIRED)` and GLOBs all sources; an
  explicit list fails loudly on upstream renames at rebase time (a GLOB
  would fail silently). Zero upstream files are edited (AGENTS.md rule 1).
- **AD-007 — Stub headers for OpenCV includes.** (2026-11-05)
  `HighMap/wasm/stubs/opencv2/*.hpp` forward-declare just enough (`cv::Mat`)
  for upstream headers to parse; OpenCV-dependent translation units
  (`io.cpp`, `opencv_wrapper.cpp`, `export/`, …) are excluded from the
  curated list, and io symbols referenced by kept units (e.g.
  `read_to_array`) are replaced by throw-shims in `HighMap/wasm/shims.cpp`.
- **AD-008 — Manifest-driven surface growth.** (2026-11-05) The exposed
  function set is enumerated in `HighMap/wasm/manifest.md`; `bindings.cpp`
  and the TS API layer derive from it, and a test asserts the surface equals
  the manifest. Growing the API is manifest + binding entries, never
  architectural work.
- **AD-009 — Seed surface scope, broad surface as the goal.** (2026-11-05)
  v1 binds ~30 functions: the 11 pyHighMap functions, the array-algebra
  layer (`add`, `subtract`, `multiply`, `divide`, `scale` — JS has no numpy
  operators), the CPU erosion twins (`hydraulicMusgrave`,
  `hydraulicStream`), and `NoiseType` / `DistanceFunction` enums. The
  explicit end goal is the broad curated API (~60+ functions) grown
  module-family by module-family in later changes; existing signatures stay
  backward compatible within a major version.
- **AD-010 — Sequential, CPU-only execution.** (2026-11-05) The artifact is
  built `-fno-openmp`, single-threaded (`ComputeMode::VA_SEQUENTIAL`
  default), no pthreads, no SharedArrayBuffer requirement. Threading is a
  later optional phase (research doc §4).
- **AD-011 — Golden tests captured from the artifact.** (2026-11-05) Seed
  golden-value snapshots are captured from the wasm artifact itself
  (self-consistent determinism). Cross-validation against the desktop OpenCL
  build is reserved for phase 2, where it validates the CPU *ports* of
  GPU-only algorithms (research doc §7).
- **AD-012 — Docs baseline with the bootstrap change.** (2026-11-05) This
  ADR log is born with the bootstrap change; every subsequent OpenSpec change
  appends entries for the decisions it locks.
- **AD-013 — Moisture-aware hydraulic erosion deferred.** (2026-11-05) The
  C++ moisture overload of `hydraulic_musgrave` was prototyped in the seed
  surface and removed before release: its sediment-transport step divides by
  the local water level, which can reach exact zero for non-constant moisture
  maps, and the resulting NaN spreads through the Laplace regularization
  (all cells NaN within iterations; verified against the CPU code path, i.e.
  upstream behavior on the pinned tree, not a port artifact). The overload
  returns when upstream fixes the division (candidate upstream issue); the
  no-moisture `hydraulicMusgrave` works and stays bound.
- **AD-014 — Flagged upstream fix: hydraulic_musgrave NaN.** (2026-11-05)
  During bootstrap verification, `hydraulic_musgrave` produced grid-wide NaN
  on typical terrain (remapped fbm, any parameter combination; NaN from the
  first iteration; ASan clean). Root cause: the sediment transport step
  computes `s * dw / w` after `w -= dw`, and since `dw <= w` is enforced by
  a `min()`, `w` reaches exact zero whenever the water term binds — `0/0`
  NaN then spreads (Laplace regularization turns local NaN grid-wide). Same
  `.cpp` compiles into the desktop build, so it NaNs identically there; the
  code is old upstream, not a port artifact. Fix: the fork's single flagged
  upstream edit — guard the transport with `if (w > 0)` (zero-flux limit,
  identical for desktop and wasm, preserving the phase-2 parity oracle).
  Spec `wasm-build` carries the narrow exception. Rebases: expect a conflict
  in `hydraulic_musgrave.cpp`; re-apply the guard.
- **AD-015 — Root README replaced with the package README.** (2026-11-05)
  The root `README.md` now documents `highmap-wasm` (quick start, API model,
  loader options, development) instead of the upstream C++ library. Docs-only
  divergence needed because npm always ships the root README as the package
  README. Upstream content remains available in git history and at
  ottolink-dev/HighMap; flag on rebases if upstream changes its README
  substantially.
