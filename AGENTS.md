# AGENTS.md — highmap-wasm

## What this repo is

This is a fork of [ottolink-dev/HighMap](https://github.com/ottolink-dev/HighMap)
(the terrain-generation core behind the Hesiod GUI), created for one purpose:

> **Port HighMap to WebAssembly as a standalone, CPU-only terrain
> generation library** (emscripten + embind), usable from JavaScript.

It is **not** a general-purpose fork. Upstream development continues in
the original repo; we sync from it and try hard not to diverge (see
"Working rules" below).

## Read this first

- **Research & plan:** [`docs/wasm-port-research.md`](docs/wasm-port-research.md) —
  full codebase inventory, OpenCL dependency analysis, GPU-only algorithm
  list, emscripten dependency audit, license findings, phased plan with
  effort estimates. Everything an agent needs to know about *why* the
  build is set up the way it is (or will be).
- Remotes: `origin` = this fork, `upstream` = ottolink-dev/HighMap.
  Submodules must be initialized: `git submodule update --init --recursive`.

## Working rules

1. **Do not modify upstream sources unless there is no alternative.**
   Prefer additive files (new CMake targets, `wasm/` directory, replacement
   shims). When a source edit is unavoidable, keep it minimal and flag it
   in the PR — it costs us on every rebase.
2. **OpenCL is excluded from the WASM target.** Never add `clwrapper`,
   `gpu_opencl/`, `*_gpu.cpp`, or `.cl` kernels to the wasm build. The
   long-term fate of the GPU-only algorithms (thermal erosion family,
   `hydraulic_{mcdonald,schott,vpipes}`, `rifts`, `advection_*`) is CPU
   ports — see §2 of the research doc for the definitive list.
3. **CPU-first, sequential-first.** The wasm target compiles with
   `-fno-openmp` and uses `ComputeMode::VA_SEQUENTIAL`. Threading
   (Emscripten pthreads) is a later, optional phase.
4. **Desktop build stays intact.** The upstream CMake build (with OpenCL,
   OpenCV, etc.) must keep working after our changes — it is our reference
   implementation for validating CPU ports against GPU output.
5. **LGPL-2.1.** Preserve license headers and notices. If we ever ship a
   binary wasm artifact, we must satisfy LGPL relinking provisions (e.g.
   publish object files / the build recipe). See §6 of the research doc.

## Current status

- [x] Research complete (`docs/wasm-port-research.md`)
- [ ] Phase 1: `highmap_wasm` CMake target + embind surface + smoke test
- [ ] Phase 2: CPU ports of GPU-only algorithms
- [ ] Phase 3: CI, benchmarks, packaging

## Conventions

- Docs and commits in English; commit messages reference the phase or
  research-doc section when relevant (e.g. `wasm: port thermal erosion
  (research §2)`).
- New wasm-side code lives under `HighMap/wasm/` (binding, shims, target
  definition) unless a better location emerges.
- Validate CPU ports numerically against the desktop OpenCL build (same
  seed, same parameters) before claiming parity.
