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
- **AD-016 — Planning docs are a living plan.** (2026-11-05) The research doc
  now reflects post-bootstrap state: phase 1 (build, embind surface, npm
  package) and the Pages showcase are complete, and phase 2 is next.
  Correction to its §2 GPU-only list: `thermal_schott` has a CPU
  implementation upstream (`src/erosion/thermal_schott.cpp`) and is bindable
  without a port; the other nine thermal variants remain GPU-only. Phase 3's
  remaining scope is benchmarks, the optional pthreads build, and npm publish
  (CI and Pages shipped with the showcase). Rationale: the original research
  snapshot predates the bootstrap and showcase work, and stale planning claims
  would otherwise inflate the next change's scope.
- **AD-017 - Thermal CPU ports live in the upstream source tree.** (2026-11-05)
  The nine CPU thermal implementations ship as a new
  `HighMap/src/erosion/thermal_cpu.cpp` plus
  `HighMap/include/highmap/erosion_thermal_cpu.hpp`, not as files under
  `HighMap/wasm/`. Rationale: `HighMap/CMakeLists.txt` globs `src/*.cpp`, so
  the desktop build (the GPU parity oracle) and the wasm build compile the same
  translation unit from the same source - the same-source property the
  `wasm-build` spec already relies on - and the parity harness needs no
  separate target, flag set, or `.cpp`-inclusion tricks. The `wasm-build`
  requirement now permits *new* files under `HighMap/src`/`HighMap/include` for
  ported algorithms while still forbidding edits to existing files. Only
  declarations are added for `thermal_schott`: upstream defines it in
  `thermal_schott.cpp` but never declares it, so the new header is its first
  public declaration. Rebase: if upstream ships CPU implementations, expect a
  duplicate-symbol link error or a duplicate-default-argument compile error and
  delete the port file plus the header; bindings and tests speak only the
  `hmap::` names, so no call site changes.
- **AD-018 - Thermal GPU/CPU parity oracle lives in CI.** (2026-11-06) The
  `thermal-parity` workflow builds the desktop library with PoCL and compares
  `hmap::gpu::thermal*` against the CPU ports on fixed scenes
  (`tests/src/test_thermal_cpu.cpp`); calibrated bounds, measured agreement and
  the mutation check are recorded in `docs/thermal-parity.md`. A skipped case
  fails the job, with one exception: `thermal_conserve` is image2d-based
  (`CL_R`/`CL_FLOAT`), which the CI PoCL device rejects, so that case skips
  with an explicit "no float image support" message and needs a device with
  image support to be closed. Rationale: AGENTS.md requires numerical
  validation against the desktop OpenCL build before claiming parity, and the
  development machine has no OpenCL ICD.
- **AD-019 - Thermal JS surface defers masks and deposition maps.**
  (2026-11-06) The CPU ports implement the full `hmap::gpu::*` overload set
  (mask, bedrock, deposition), but the bindings expose only the plain in-place
  form: `thermal`, `thermalAutoBedrock`, `thermalConserve`, `thermalFlatten`,
  `thermalInflate`, `thermalOlsen`, `thermalRib`, `thermalRidge`,
  `thermalScree`, `thermalSchott`. Rationale: the JS surface has no mask or
  output-grid concept yet and a single-family precedent would predate the
  whole-pipeline design. The deferral is recorded in
  `HighMap/wasm/manifest.md`; adding the arguments later is backward
  compatible.
- **AD-020 - Thermal option defaults.** (2026-11-06) JS defaults are
  `iterations` 10 for every thermal function (including `thermalRib`,
  `thermalFlatten` and `thermalOlsen`, whose C++ overloads declare no default),
  `rate` 0.5, `sigmaInf` 0.5, `sigmaSup` 0 and `intensity` 0.2. `talus` and
  `zmax` are required positional arguments - no scale-dependent default is
  invented - and a numeric value becomes a constant wasm grid that the API
  layer frees after the call.
- **AD-021 - `thermal_rib` GPU read-back bug recorded, not fixed.**
  (2026-11-06) `gpu::thermal_rib` reads the wrong buffer back for even
  iteration counts (`run_ba.read_buffer("z_in")` downloads the stale input
  buffer in `src/erosion/thermal_gpu.cpp`), so `gpu::thermal_rib(z, 10)`
  returns the padded input instead of the eroded grid. The fork does not edit
  that file (AD-017); the CPU port is correct, and the parity harness compares
  at odd iteration counts, which exercise the same kernel. Recorded as a
  candidate upstream issue in `docs/thermal-parity.md`.
- **AD-022 - Hydraulic CPU ports live in the upstream source tree.**
  (2026-11-06) The five CPU hydraulic implementations ship as a new
  `HighMap/src/erosion/hydraulic_cpu.cpp` plus
  `HighMap/include/highmap/erosion_hydraulic_cpu.hpp`, following AD-017. The
  `_cpu` suffix is deliberate: if upstream ever adds its own CPU hydraulic
  implementations, the duplicate symbols fail loudly at link time, and deleting
  the port file plus the header is the whole rebase fix because bindings and
  tests speak only the `hmap::` names. Three pinned GPU wrappers are defective
  and are recorded, not patched: `hydraulic_schott_gpu.cpp` never binds the
  kernel's `it` argument; `hydraulic_vpipes_gpu.cpp` launches pre-refactor
  kernel names that no longer exist; and the `hydraulic_schott` mask overload is
  declared with a different parameter order than its definition. The parity
  harness therefore launches the Schott and vpipes kernels directly (see
  `docs/hydraulic-parity.md`) and does not depend on those wrappers.
- **AD-023 - Hydraulic output maps are caller-allocated HArrays.**
  (2026-11-06) The hydraulic functions accept optional output maps
  (`sediment`, `discharge`, `flow`, `waterDepth`, `velU`, `velV`) as `HArray`s
  in the options object. The caller allocates them with `makeGrid`; the TS layer
  validates liveness, shape and aliasing before the wasm call; omitted maps
  become null `hmap::Array*` outputs. Rationale: it preserves the existing
  in-place `HArray` return convention, maps directly onto the C++ output
  pointers, and gives the caller lifetime control. A result object or
  auto-allocated maps would break the convention or hide memory. The C++ ports
  also implement the mask/moisture/rain input overloads, but those remain
  deferred on the JS surface.
- **AD-024 - Direct-kernel oracle for broken hydraulic wrappers.**
  (2026-11-06) `tests/src/test_hydraulic_cpu.cpp` launches the Schott and vpipes
  OpenCL kernels directly instead of going through the pinned wrappers. The
  Schott reference binds the kernel's `it` argument; the vpipes reference runs
  the four pass kernels in the fixed dev-branch order. This gives a real
  GPU-vs-CPU comparison without editing upstream sources. The parity report
  records the three wrapper gaps as candidate upstream issues. If upstream fixes
  a wrapper, the direct-kernel helper can be retired or kept as a second oracle.
- **AD-025 - McDonald parity separates arithmetic from atomic order.**
  (2026-11-06) The CPU McDonald port processes samples in a fixed index order,
  so its result is deterministic; the GPU kernel uses atomic adds and is
  reproducible only up to atomic scheduling order. The parity harness therefore
  compares a single-sample run per cell (kernel arithmetic fidelity) and a
  many-sample run by aggregate metrics (total sediment, total discharge, mean
  absolute elevation change). A per-cell multi-particle bound would be
  meaningless. The CPU invariants pin determinism and output-map behavior.
- **AD-026 - Rifts/advection CPU ports live in two family translation units.**
  (2026-11-07) `hmap::rifts` ships in `HighMap/src/erosion/rifts_cpu.cpp` with
  `HighMap/include/highmap/erosion_rifts_cpu.hpp`, and
  `hmap::advection_particle` (six overloads) plus `hmap::advection_warp` (two
  overloads) ship in `HighMap/src/transform/advection_cpu.cpp` with
  `HighMap/include/highmap/transform_advection_cpu.hpp`, following AD-017 and
  AD-022. Two units rather than one keep family ownership with the upstream
  erosion/transform directories and avoid forcing transform dependencies on the
  erosion port. The desktop `file(GLOB_RECURSE ... src/*.cpp)` picks both up
  automatically and the two new curated rows in `HighMap/wasm/sources.cmake`
  add them to the wasm target, so the shipped artifact and the desktop parity
  harness execute the same source. The `_cpu` suffix is deliberate: if upstream
  ever adds CPU implementations with these names, duplicate symbols fail loudly
  at link time (duplicate default arguments at compile time), and deleting the
  port file plus the header is the whole rebase fix because bindings and tests
  speak only the `hmap::` names.
- **AD-027 - New-grid JS functions and tuple binding for fixed-size math
  types.** (2026-11-07) `rifts` follows the erosion family: it mutates an
  `HArray` in place and returns that same wrapper, copying plain `{ data,
  shape }` inputs first. `advectionParticle` and `advectionWarp` are the first
  erosion/transform functions that produce a new grid, so the API layer gains a
  `newGrid` helper next to `inplace` / `inplaceWithOutputs`: it coerces every
  input (plain objects become temporaries freed in `finally`), rejects a later
  input whose shape differs from the first with an error naming that argument,
  calls the binding and wraps the returned handle in a fresh `HArray`. The
  fixed-size C++ math types are scalarized at the binding boundary instead of
  registering `glm::vec2`/`glm::vec4` or using `emscripten::val`, following the
  noise family's `kx, ky` convention: `kw` → `kwX, kwY`, `center` → `centerX,
  centerY`, `bbox` → `bboxXmin, bboxXmax, bboxYmin, bboxYmax` (the C++ order,
  not `[xmin, ymin, xmax, ymax]`). The TS layer validates tuple length and
  finiteness. The JS surface binds only the mask-free, noise-offset-free rifts
  form, the single-field gradient-driven advection forms and the z-driven warp
  form; rifts' `p_noise_x`/`p_noise_y`/`p_mask`, particle's
  `p_advection_mask`/`p_mask` plus the multi-field and explicit `dx`/`dy`
  overloads, and warp's `p_mask` plus the explicit `dx`/`dy` overload stay
  unbound and are recorded as prose in `HighMap/wasm/manifest.md` (AD-019
  precedent). `advectionParticle` binds the `iterations` overload with a JS
  default of 1, so `iterations: 0` is a documented no-op.
- **AD-028 - Parity-oracle gaps for rifts, warp and particle advection.**
  (2026-11-07) Four properties of this family make a pointwise parity bound
  meaningless, so the harness gates on distributions and aggregates and keeps
  the per-cell maximum as a diagnostic:
  - **Rifts' hash is discontinuous.** The Voronoi hash is
    `fract(sin(dot(p, k) + fseed) * 43758.5453123)`; a one-ULP `sin` difference
    between the GPU and CPU builds can flip a whole cell. Rifts is gated on
    RMS, the 99th percentile of per-cell absolute error and the fraction of
    cells above `1e-2 * range`, never on a pointwise maximum.
  - **The warp sampler is implementation-defined at the sub-ULP level.**
    `advection_warp.cl` samples with mirrored-repeat linear filtering, for
    which OpenCL guarantees only ≥9-bit subpixel precision. The CPU port pins
    the documented mapping (texel centres at `(i + 0.5) / N`, mirrored fold,
    clamped edge texels) and the harness uses the same percentile gates.
  - **The particle wrapper never uploads `out`/`count`, and the kernel
    accumulates non-atomically.** `advection_particle_gpu.cpp` allocates both
    buffers without writing them, and `advection_particle.cl` uses a plain
    `+=` for the accumulator and the count, so a many-particle GPU result is
    neither reproducible nor comparable cell by cell. The direct-kernel
    reference uploads explicit zeros; one particle per cell is compared
    per cell; many-particle cases are claimed only on total mass and mean
    `|Δz|`; and a repeated GPU run records the GPU-vs-GPU variability. The
    wrapper's swapped host-stage loop bounds (`j < shape.x`, `i < shape.y`) are
    out of bounds on non-square grids; the test replica sweeps the whole grid.
  - **Two kernel constructs are undefined behaviour.** Warp's `dir` is used
    uninitialized on a flat start and the particle solver calls `normalize()`
    on a zero vector when `inertia = 1` with no previous direction. The CPU
    ports define both as `(0, 0)`, the deviations are documented in the port
    and pinned by CPU invariants, flat starts and `inertia = 1` are excluded
    from the parity bounds, and a deliberately ungated flat-start line records
    the GPU side. `rifts`' `pow(val0, mask_gamma)` also stays domain-limited:
    negative elevations with the mask enabled produce the kernel's NaN, and
    the parity scenes are remapped to [0, 1]. See
    `docs/rifts-advection-parity.md`.
