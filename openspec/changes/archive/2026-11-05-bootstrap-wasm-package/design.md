# Design

## Context

The fork carries the full HighMap C++ tree (research: `docs/wasm-port-research.md`).
Constraints that shape this design:

- The desktop build (`find_package(OpenCL REQUIRED)` unconditional in the root
  CMakeLists, GLOB'd sources in `HighMap/CMakeLists.txt`) must stay intact — it
  is the validation oracle for later CPU ports. So the wasm build cannot reuse
  the upstream CMake entry points.
- `include/highmap/array.hpp` — the central type — includes `<opencv2/core.hpp>`
  and declares two `cv::Mat` conversion functions (`to_cv_mat`,
  `cv_mat_to_array`). Nothing else in the seed surface needs OpenCV.
- The only existing binding precedent is pyHighMap: 11 module-level functions,
  2 enums, numpy-in/numpy-out, no exposed `Array` class, defaults via kwargs.
- The architectural reference is `mode777/libfive-wasm`: ESM-only package,
  async `init()`, `new URL('./x.wasm', import.meta.url)` loading with
  `wasmBinary`/`locateWasm` overrides, idempotent `free()` +
  `FinalizationRegistry`, view-rebinding getters, ADR log, OpenSpec-driven.

Key difference from libfive-wasm: libfive exposes a **C API**, so that port
hand-wrote a pointer FFI layer with generated flat shims. HighMap is **C++
classes and free functions** with no C API — Emscripten's embind is the FFI
layer, which also removes the need for hand-rolled pointer marshalling and can
emit TypeScript declarations for the raw surface.

## Goals / Non-Goals

**Goals:**

- One end-to-end slice: `HighMap/src` → curated wasm build → embind surface →
  ESM package → Node tests, with a composable seed API (~30 functions).
- Every choice structured so later changes only *extend* (manifest, module
  families) rather than re-architect.
- Zero upstream file edits; all new files in additive locations.

**Non-Goals:**

- Full curated API (~60+ functions) — arrives incrementally in later changes.
- CPU ports of GPU-only algorithms (thermal family, hydraulic
  mcdonald/schott/vpipes, rifts, advection) — phase 2.
- Numerical parity validation against the desktop OpenCL build — that oracle
  matters for phase 2 ports; the seed surface binds already-CPU functions.
- pthreads, SIMD, WebGPU — later optional phases.
- Browser showcase site, CI pipelines, npm publishing — hardening/publish
  change. (Browser/worker *support* is designed in and compiled for; it is
  just not yet exercised by an automated browser test.)
- `export/` writers, mesh/PointSampler features, OpenCV-dependent functions
  (FFT convolution, texture, scatter_field), GSL-dependent functions
  (`convolve2d_svd`, `interpolate1d`).

## Decisions

### D1: embind as the FFI layer

Bind `hmap::Array` and curated free functions with `emscripten/bind.h` in a
single additive `HighMap/wasm/bindings.cpp`. Alternatives: a libfive-wasm-style
C shim + hand FFI (would require writing and maintaining a flattened C API for
~30 C++ functions — pure overhead with no C consumer); WebIDL binder (bindings
by interface definition, weaker fit for free functions and `std::vector`).
embind keeps signatures close to the C++ headers and emits `.d.ts` for the raw
layer (`--embind-emit-tsd` in recent Emscripten).

### D2: Standalone CMake project rooted at `HighMap/wasm/`

`HighMap/wasm/CMakeLists.txt` is its own project (`emcmake cmake -S HighMap/wasm
-B build/wasm`), never `add_subdirectory`-ed into the upstream root. It
compiles an **explicit curated source list** (see D4) and adds only the small
vendored dependencies it needs (FastNoiseLite, Noise, delaunator, dkm,
nanoflann, hmm, libnpy, mixbox, nn-c, terrain-descriptors — header-only or
small; PointSampler and CLWrapper excluded). Alternatives: an option-gated
branch inside the root CMake (edits an upstream file, and the root still drags
`find_package(OpenCL REQUIRED)`); reusing `HighMap/CMakeLists.txt` with
filters (GLOB + exclude is fragile against upstream churn and links the full
dependency set).

### D3: Stub headers instead of patching `array.hpp`

`HighMap/wasm/stubs/opencv2/core.hpp` contains a forward declaration of
`cv::Mat` only, put on the include path ahead of any real OpenCV. The two
conversion functions parse as declarations; their definitions are simply not
in the curated source list. Alternative: `#ifdef` edits in upstream headers —
rejected (rebase cost, violates the additive rule).

> **Implementation note (AD-014):** one upstream edit ultimately proved
> unavoidable — a guarded zero-water divisor in
> `src/erosion/hydraulic_musgrave.cpp` (NaN bug breaking a spec-required
> function; identical code in both builds). It is minimal, flagged in-source,
> covered by a narrow spec exception, and recorded in `docs/decisions.md`.

### D4: Curated source list as a CMake variable in one place

A single `sources.cmake` in `HighMap/wasm/` lists every compiled `.cpp`
explicitly (seed estimate: ~120–150 of the 267 files — everything except
`gpu_opencl/`, `*_gpu.cpp`, `export/`, OpenCV/assimp/GSL/PointSampler
dependents). Explicit lists fail loudly on upstream renames at rebase time,
which is the desired behavior (a GLOB would fail silently by dropping files).
The list is the build-side twin of the binding manifest (D7).

### D5: Hybrid marshalling over an embind-exposed Array

`bindings.cpp` exposes `hmap::Array` (constructors from shape; methods needed
by the wrapper). The TS layer in `src/` wraps it as `HArray`:

- Inputs: one shared coercion helper accepts `HArray` or
  `{ data: Float32Array, shape: [ni, nj] }`, validates, and creates/uses the
  wasm-side Array (plain inputs are copied in — they are one-shot values;
  `HArray` inputs are passed zero-copy).
- Outputs: always a new `HArray` wrapping the returned `hmap::Array`.
- `HArray.data` is a getter that re-wraps the current wasm `Memory.buffer` on
  every access (survives heap growth detaching old buffers — libfive-wasm
  AD-006 pattern); `copyData()` returns a JS-owned copy; `free()` is
  idempotent and backed by a `FinalizationRegistry` safety net.
- Never hand out raw `emscripten::typed_memory_view` results from a function
  call — views outlive the call only inside a wrapper that re-binds.

`glm::ivec2`/`glm::vec2` are **not** exposed; binding signatures convert at
the boundary (JS arrays ↔ glm types via small binding helpers), keeping glm
out of the public contract.

### D6: Package shape cloned from libfive-wasm

Root `package.json`, `"type": "module"`, ESM-only, single entry, no runtime
dependencies, `files: ["dist", "README.md", "LICENSE", "THIRD_PARTY_LICENSES.md"]`,
`sideEffects: false`, `engines.node >= 18`, `private: true` until the publish
change. Loader in `src/loader/`: async `init(options?)` instantiating the
MODULARIZE + EXPORT_ES6 Emscripten factory; default artifact location via
`new URL('./highmap.wasm', import.meta.url)`; `wasmBinary` > `locateWasm` >
default precedence; `ENVIRONMENT=node,web,worker`; `ALLOW_MEMORY_GROWTH=1`.
CJS consumers documented to use dynamic `import()` — no CJS build.

### D7: Binding manifest as the single source of surface truth

`HighMap/wasm/manifest.md` (or a machine-readable list embedded beside the
bindings) enumerates every bound function with its C++ name, JS camelCase
name, options, and defaults. `bindings.cpp` and the TS api layer both derive
from it; tests assert the exposed surface equals the manifest. Later changes
grow the API by adding manifest entries + binding lines, never by touching
architecture.

### D8: Seed surface (~30 functions)

- pyHighMap parity (11): `noise`, `noiseFbm`, `noisePingpong`, `noiseRidged`,
  `warp`, `falloff`, `clamp`, `remap`, `plateau`, `smoothFill`, `hillshade`.
- Algebra (JS-has-no-numpy): `add`, `subtract`, `multiply`, `divide`
  (grid-grid and grid-scalar overloads), `scale`.
- Erosion CPU twins: `hydraulicMusgrave`, `hydraulicStream`.
- Enums: `NoiseType`, `DistanceFunction` (pyHighMap value sets).
- Optional trailing options object per function; defaults = pyHighMap defaults.
- Exact filter extras beyond `smoothFill`/`plateau` are picked during
  implementation from what the curated list already compiles (does not change
  the contract minimums in the specs).

### D9: Testing strategy for this change

- **Vitest in Node**: API unit tests (both input forms, validation errors,
  `free()` semantics, determinism same-seed-same-values), plus golden-value
  tests captured **from the artifact itself** (self-consistent snapshots, not
  desktop parity — parity is the phase-2 oracle for ported algorithms).
- **Consumer bundler test**: esbuild (or Vite) build of a scratch consumer
  asserting the wasm is emitted as an asset (spec: loader/bundler scenario).
- **Build invariants**: desktop CMake configure check (CPU-only machine not
  required for configure-only... where OpenCL is present it configures; the
  wasm build itself is the CPU-only proof), `git status` cleanliness of
  upstream paths.

### D10: Build flags baseline

`-Os`, `MODULARIZE=1`, `EXPORT_ES6=1`, `ENVIRONMENT=node,web,worker`,
`ALLOW_MEMORY_GROWTH=1`, `-fno-openmp`, `-fexceptions` (embind error paths
raise C++ exceptions; use wasm exceptions `-fwasm-exceptions` if the pinned
toolchain's size numbers favor it), `--embind-emit-tsd` for raw typings.
ComputeMode default `VA_SEQUENTIAL`. Pinned Emscripten version recorded in
`scripts/build-wasm.sh` when the first successful build settles on one.

### D11: Docs baseline

`docs/decisions.md` ADR log seeded with the exploration outcome: hybrid
marshalling, camelCase, ESM-only, layout, manifest-driven growth,
embind-not-C-shim, standalone CMake, seed-surface scope (option 2 now, broad
curated surface as the stated end goal).

## Risks / Trade-offs

- [embind + exceptions add code size to an already 2–5 MB artifact] →
  measure in the build task; accept for v1 (size work is a later change);
  keep `-Os` and no-debug runtime.
- [Upstream renames/deletes break the curated list at rebase] → explicit list
  centralizes the conflict into one file; CI-less local build fails loudly.
- [Heap growth detaches cached views] → `HArray.data` re-binds per access;
  documented "copy to own" rule for consumers who hold data long-term.
- [`highmap-wasm` name may be taken on npm] → registry check during bootstrap;
  fallback names reserved decision (`@mode777/highmap-wasm`) at publish time.
- [Dev environment is Windows; build scripts are bash] → scripts run under
  git-bash/WSL locally and Linux CI later; no PowerShell-only constructs.
- [Node-only automated tests may miss browser-specific loader bugs] →
  `ENVIRONMENT` tri-target is compiled in and the bundler asset test covers
  the loader path; real browser exercise lands with the showcase change.

## Migration Plan

Greenfield and purely additive: rollback is deleting the new directories and
files (`HighMap/wasm/`, `src/`, `scripts/`, `tests/`, `docs/decisions.md`,
`package.json`, lockfile). No existing behavior migrates.

## Open Questions

- Exact Emscripten version pin (settle at first successful build; recorded in
  the build script and ADR log).
- `--embind-emit-tsd` output quality on the pinned version — if unusable, raw
  typings are hand-written once for the seed surface (small) and regenerated
  per family later.
- Final filter shortlist beyond `smoothFill`/`plateau` (resolved during
  implementation from the compiling set; no contract impact).
