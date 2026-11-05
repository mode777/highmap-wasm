# Proposal

## Why

This fork exists to ship HighMap as a standalone, CPU-only WebAssembly terrain
generation library consumable from JavaScript (see AGENTS.md and
docs/wasm-port-research.md). Today there is no binding, no wasm build, and no
package — research is complete and the architecture is agreed, so the next
step is the first end-to-end slice: C++ compiled to wasm, wrapped in an npm
package that works in Node and the browser, with an API that mirrors the
existing pyHighMap Python bindings. Shipping this early proves the whole
pipeline (build → bind → load → compose) before the larger CPU-porting phases
land on top of it.

## What Changes

- **New standalone Emscripten build** under `HighMap/wasm/`: its own CMake
  project (not wired into the upstream root build), compiling a curated
  CPU-only subset of `HighMap/src` — no OpenCL, no `*_gpu.cpp`, no
  `gpu_opencl/`, no OpenCV/assimp/EXR/GSL-heavy files — plus a tiny
  `cv::Mat` forward-declaration stub so `array.hpp` parses unmodified.
- **embind binding layer** exposing `hmap::Array` and a seed surface of
  ~30 functions driven by an explicit, growable binding manifest
  (pyHighMap's 11 functions, the array-algebra layer JS needs because it has
  no numpy, CPU erosion twins, and a few filters).
- **New npm package** at the repo root: ESM-only, async `init()`, loading
  `highmap.wasm` via `new URL(..., import.meta.url)` with `wasmBinary` /
  `locateWasm` overrides; works in Node >= 18, browsers, and workers.
- **Hybrid grid marshalling contract**: functions accept either an `HArray`
  wrapper or a plain `{ data: Float32Array, shape: [ni, nj] }`, and return
  `HArray` (live zero-copy view, `copyData()`, idempotent `free()`,
  `FinalizationRegistry` safety net).
- **camelCase function API** modeled on pyHighMap: module-level functions,
  enum constants (`NoiseType`, `DistanceFunction`), options-object defaults.
- **Tests and docs baseline**: Node smoke test plus vitest suite;
  `docs/decisions.md` ADR log capturing the locked design decisions.
- **No upstream source edits**: everything wasm-side is additive; the
  desktop OpenCL build must keep working unchanged.

## Capabilities

### New Capabilities

- `wasm-build`: the standalone Emscripten build that turns the HighMap C++
  sources into `dist/highmap.wasm` + JS glue — curated CPU-only source list,
  stub headers, pinned toolchain, zero upstream edits, desktop build intact.
- `wasm-loader`: async `init()` and wasm artifact location across Node,
  browsers, and workers (ESM-only, override hooks, instance semantics).
- `grid-marshalling`: how 2D float grids cross the JS/wasm boundary —
  `HArray` lifecycle, zero-copy views, copies, coercion of plain typed
  arrays, error behavior on malformed input.
- `terrain-api`: the high-level camelCase function surface modeled after
  pyHighMap (seed set of ~30 functions plus enums), designed to grow
  module-family by module-family toward the full curated API.

### Modified Capabilities

(none — no specs exist yet)

## Impact

- **New code**: `HighMap/wasm/` (CMake project, `bindings.cpp`, stubs,
  binding manifest), `src/` (loader, api, ffi), `scripts/build-wasm.sh`,
  `tests/`, `package.json`, `docs/decisions.md`.
- **Untouched**: `HighMap/src`, `HighMap/include`, `HighMap/CMakeLists.txt`,
  root `CMakeLists.txt` — the desktop reference build must remain buildable
  as-is (it is the validation oracle for later CPU ports).
- **New toolchain**: Node + npm dev dependencies at repo root (typescript,
  vitest, esbuild); Emscripten SDK pinned in the build script.
- **License**: LGPL-2.1 notices preserved; npm tarball ships LICENSE and
  THIRD_PARTY_LICENSES.md.
