# WASM port — research notes

Status: **living plan** (research complete; phase 1, the showcase and phase 2
have shipped — the thermal, hydraulic and rifts/advection families are
ported and parity-checked; only the primitives family remains GPU-only,
outside phase-2 scope; next up is phase 3).
Last updated 2026-11-08.
Research base: fork of [ottolink-dev/HighMap](https://github.com/ottolink-dev/HighMap)
at commit `4fb63ae6` (*Merge pull request #463 from ottolink-dev/perf/trench*), 2026-11-05.
An earlier pass was done on the copy pinned by the Hesiod submodule
(`0e6e3872`); stats below were re-verified on this tree and take precedence.

Goal: **compile HighMap to WebAssembly as a standalone, CPU-only terrain
generation library** (emscripten + embind), consumable from JavaScript.

---

## 1. Codebase inventory

Library only (`HighMap/src` + `HighMap/include`):

| Component | Files | Lines |
|---|---|---|
| `src/*.cpp` | 267 | 44,261 |
| `include/*.hpp` + `*.inl` | 86 | 29,031 |
| OpenCL kernels `src/gpu_opencl/kernels/**/*.cl` | 83 | 8,113 |
| **Total** | ~436 | **~81k** |

C++20. Module map (`src/` directories): erosion (32), filters (17),
hydrology (21), primitives (31), interpolate (15), geometry (14),
virtual_array (13, the tiled/out-of-core compute layer), export (16), and
~25 smaller groups.

Vendored dependencies (submodules under `external/`):

| Submodule | Size | Needed for WASM? |
|---|---|---|
| PointSampler | ~32k lines | cut (mesh-based scattering only) or defer |
| CLWrapper | ~2.1k lines | **cut** (OpenCL wrapper) |
| FastNoiseLite, Noise, delaunator, dkm, nanoflann, mixbox, libnpy, hmm, nn-c, terrain-descriptors | small / header-only | keep — all emscripten-friendly |

System libraries (CMake `find_package`/link): **OpenCL**, **OpenCV**,
**assimp**, **GSL**, OpenEXR, TIFF, zlib, zstd, nlohmann_json, glm,
OpenMP, lightusd (tinyusdz).

## 2. OpenCL coupling

Architecture:

- The 83 `.cl` kernels are embedded **as strings** via `#include` into
  `src/gpu_opencl/gpu_opencl.cpp` (`hmap::gpu::init_opencl()`) and JIT-built
  at runtime by CLWrapper with `-cl-fast-relaxed-math` etc.
- 37 of 267 `.cpp` files call `clwrapper::Run` directly; 25 of those are
  named `*_gpu.cpp`.
- There is **no runtime CPU fallback**. Consumers call the GPU path
  directly (the Hesiod app has 110 unconditional `hmap::gpu::` call sites).
- Build system: `find_package(OpenCL REQUIRED)` in the root `CMakeLists.txt`
  (line 115) is **unconditional**. The `HIGHMAP_ENABLE_OPENCL` option
  exists but gates nothing — removing OpenCL requires CMake surgery.
- CLWrapper's `DeviceManager` does accept `CL_DEVICE_TYPE_CPU`, so on
  desktop a CPU ICD (PoCL) works with zero code changes — irrelevant in a
  browser, but a useful dev crutch.

### CPU twins vs. GPU-only

17 of the 25 `*_gpu.cpp` files have plain CPU counterparts (filters,
morphology, gradient, curvature, selectors, warps, flow accumulation,
harmonic interpolation, `hydraulic_musgrave`, `hydraulic_stream`, …).
For those, dropping OpenCL is pure build surgery: exclude the `_gpu.cpp`
files + `gpu_opencl/` + CLWrapper and call the CPU functions.

**GPU-only (no CPU implementation exists — these are the porting work):**

| GPU-only file | Missing CPU implementation | ~Kernel LOC |
|---|---|---|
| `erosion/thermal_gpu.cpp` | the thermal family except `thermal_schott`: `thermal`, `thermal_auto_bedrock`, `thermal_conserve`, `thermal_flatten`, `thermal_rib`, `thermal_ridge`, `thermal_scree`, `thermal_olsen`, `thermal_inflate` | ~660 C++ + kernels |
| `erosion/hydraulic_mcdonald_gpu.cpp` | `hydraulic_mcdonald` | 360 |
| `erosion/hydraulic_schott_gpu.cpp` | `hydraulic_schott` | 218 |
| `erosion/hydraulic_vpipes_gpu.cpp` | `hydraulic_vpipes` | 225 |
| `erosion/rifts_gpu.cpp` | `rifts` | 68 |
| `transform/advection_particle_gpu.cpp` | `advection_particle` | 263 |
| `transform/advection_warp_gpu.cpp` | `advection_warp` | 66 |
| `primitives/primitives_gpu.cpp` | GPU variants of coherent noise (note: CPU `noise_fbm` etc. exist separately in `coherent_noise.cpp`) | — |

Total: ~1,850 lines of C++ launch code + ~2,400 lines of OpenCL C across
19 kernels that would need CPU rewrites. These are classic grid algorithms
(stencil iterations, particle loops, pipe networks) — no exotic OpenCL
features (plain float buffers, no images/samplers) — so they port to
plain C++ loops without conceptual difficulty, but the code has to be
written and validated.

**Correction (2026-11-05):** `thermal_schott` is *not* porting work.
Upstream ships a CPU implementation in `erosion/thermal_schott.cpp`; the
GPU file merely wraps it. Binding it is a curated-source-list addition
(`thermal_schott.cpp`) plus a manifest/binding entry. The other nine
thermal variants above remain GPU-only, so the remaining porting volume is
the §2 total minus the `thermal_schott` launch path and kernel.

**Update (2026-11-06):** the thermal family is done. The nine GPU-only
variants have CPU transcriptions in `src/erosion/thermal_cpu.cpp` (declared in
`include/highmap/erosion_thermal_cpu.hpp`), `thermal_schott` is bound from the
upstream CPU implementation, and GPU-vs-CPU parity is enforced by the
`thermal-parity` CI job (`docs/thermal-parity.md`). See AD-017 for why the
ports live in the upstream source tree rather than under `HighMap/wasm/`, and
`docs/porting-learnings.md` for the transferable checklist for the remaining
GPU-only algorithms.

**Update (2026-11-07):** `rifts`, `advection_particle` and `advection_warp`
are done. They live in `src/erosion/rifts_cpu.cpp` and
`src/transform/advection_cpu.cpp` with declarations in
`include/highmap/{erosion_rifts,transform_advection}_cpu.hpp`, are bound on the
JS surface, and are compared against the desktop OpenCL kernels by the
`erosion-parity` workflow (`docs/rifts-advection-parity.md`, AD-026/AD-028).
The GPU-only table above is now historical: the **primitives family**
(`primitives/primitives_gpu.cpp`, GPU variants of coherent noise) is the only
remaining GPU-only block, and it sits outside phase-2 scope — the CPU noise
functions already exist separately in `coherent_noise.cpp` and are bound.

## 3. Dependency audit for emscripten

| Dependency | Usage | WASM disposition |
|---|---|---|
| OpenCL + CLWrapper | 37 files + kernels | **remove** (see §2) |
| OpenCV | 9 files: FFT convolution (`cv::dft` in `convolve/convolve.cpp`), image I/O, resize, `scatter_field`, `texture` | **replace**: stb_image / pocketfft / hand-rolled resize |
| assimp | 1 file (mesh import) | cut |
| OpenEXR / TIFF / zstd / tinyusd / zlib | `export/` writers (16 files) | cut for compute-only lib |
| GSL | 2 files: SVD (`convolve/convolve2d_svd.cpp`), 1-D splines (`interpolate/interpolate1d.cpp`) | replace with Eigen or small local implementations |
| nlohmann_json, glm | widespread | keep — compile fine |
| OpenMP | `#pragma omp` in 13 files | compile with `-fno-openmp` (pragmas ignored); revisit if we enable pthreads |
| PointSampler (32k lines) | 12 use sites, mesh sampling/scattering | cut initially |
| spdlog-style logging (`HIGHMAP_ENABLE_LOGS`) | logging | keep or route to `console.log` |

## 4. Threading

- `ComputeMode` (`include/highmap/virtual_array/virtual_array.hpp`) already
  defines `VA_SEQUENTIAL` — a single-threaded execution mode. **Use it as
  the WASM default.**
- `distributed_tile_loop` uses `std::thread::hardware_concurrency()` +
  `std::thread`; guard or compile it out (Emscripten pthreads +
  SharedArrayBuffer / COOP-COEP is possible later but not worth it for v1).

## 5. Prior art

- **No existing WASM port** of HighMap (GitHub repo/code search, 2026-11:
  0 hits for "highmap wasm" / "hesiod wasm"; all 9 upstream forks are
  trivial). We are first.
- The only existing binding of any kind is
  [otto-link/pyHighMap](https://github.com/otto-link/pyHighMap) (Python,
  wraps a small subset, last pushed 2024-08 — stale). Useful as precedent
  that a headless subset builds and works outside the Qt app.

## 6. License findings

- The upstream `LICENSE` file was **changed from GPLv3 to LGPL-2.1 on
  2026-07-09** (commit `908a7ad`, verified via GitHub API). This fork
  carries LGPL-2.1.
- Inconsistencies to be aware of: the README still says "GPL v3.0" and
  per-file source headers still say "GNU General Public License". The
  LICENSE file + GitHub's detector say LGPL-2.1. If LGPL-2.1 holds it is
  favorable for embedding in a (closed or open) WASM product, with the
  usual LGPL relinking provision satisfied e.g. by publishing object
  files / the wasm build recipe. **Confirm intent with upstream before
  building anything commercial on it.**
- Keeping this fork public and preserving upstream notices satisfies the
  obvious obligations either way for development.

## 7. Feasibility & plan

**Verdict: feasible as a CPU library.** The math is float-grid based, the
public API is clean (`hmap::Array` = shape + `std::vector<float>`), and a
sequential execution mode already exists. OpenCL cannot run in browsers;
WebGPU/WGSL could be a long-term successor for the GPU-only algorithms but
is a much larger rewrite and is out of scope for v1.

Phases:

1. **Bootstrap — done (2026-11-05, 1–2 weeks estimated).** New
   `highmap_wasm` CMake target with a curated source list (no `export/`,
   `gpu_opencl/`, `*_gpu.cpp`, OpenCV/assimp/EXR/GSL-dependent files); GSL
   SVD replaced by an Eigen shim; embind surface per
   `HighMap/wasm/manifest.md`; npm package with Node smoke tests. See
   `openspec/changes/archive/2026-11-05-bootstrap-wasm-package/`. The
   shipped module is ~0.5 MB wasm, below the 2–5 MB estimate.
2. **CPU ports of GPU-only algorithms - complete.** Thermal family
   **done** (2026-11-06): nine variants ported in
   `HighMap/src/erosion/thermal_cpu.cpp` with declarations in
   `HighMap/include/highmap/erosion_thermal_cpu.hpp`, plus the existing CPU
   `thermal_schott`; validated against desktop OpenCL output (same
   seeds/parameters) by the `erosion-parity` CI job. Hydraulic family **done**
   (2026-11-06): `hydraulic_mcdonald`, `hydraulic_mcdonald_multiscale`,
   `hydraulic_schott`, `hydraulic_schott_erosion` and `hydraulic_vpipes` in
   `HighMap/src/erosion/hydraulic_cpu.cpp` with declarations in
   `HighMap/include/highmap/erosion_hydraulic_cpu.hpp`; Schott and vpipes are
   compared through direct kernel launches because the pinned wrappers are
   broken (`docs/hydraulic-parity.md`). Rifts/advection family **done**
   (2026-11-07): `rifts` in `HighMap/src/erosion/rifts_cpu.cpp` and
   `advection_particle` / `advection_warp` in
   `HighMap/src/transform/advection_cpu.cpp`, compared against the GPU by
   `docs/rifts-advection-parity.md`. Only the primitives family remains
   GPU-only, outside phase-2 scope.
3. **Harden & publish — partially done.** CI (GitHub Actions emscripten
   build) and the Pages showcase shipped with
   `openspec/changes/archive/2026-11-05-add-terrain-showcase/`. Remaining:
   benchmarks, the optional pthreads build, and publishing the npm
   package.

Risks:

- Upstream moves fast (~2,300 commits); keep this fork rebased and prefer
  *additive* wasm-side files over editing upstream sources so rebases stay
  cheap.
- The `#include "*.cl"` trick means `gpu_opencl.cpp` will not compile
  anywhere OpenCL headers are absent — exclusion must happen at CMake
  level, not `#ifdef`.

## 8. References

- Upstream: https://github.com/ottolink-dev/HighMap (docs:
  https://ottolink-dev.github.io/HighMap/)
- This fork: https://github.com/mode777/highmap-wasm
- Hesiod (GUI consumer): https://github.com/ottolink-dev/Hesiod
- pyHighMap: https://github.com/otto-link/pyHighMap
- License-change commit: `908a7ad4d2895f6a091d0b0fc87c4893eea4f580`
  ("Add LICENSE file", 2026-07-09)
