# Proposal

## Why

Phase 3 opens with a measurement problem, not a feature problem. Its next
two decisions — whether the optional Emscripten pthreads build is worth its
complexity, and what performance the published npm package can promise —
both need numbers that do not exist in one place: per-function wall time at
practical grid sizes, single-threaded, on the shipped artifact. Scattered
timings exist (the showcase recorded three recipe builds; the parity docs
record desktop GPU/CPU comparisons), but there is no reproducible harness,
no recorded baseline, and no comparison protocol for a future pthreads
build.

## What Changes

- **A benchmark harness** `scripts/bench.mjs` (new, additive): loads the
  built artifact through the public loader, builds fixed-seed inputs per
  function family, and times every function in the binding manifest at
  256², 512² and 1024² grids — one warmup pass, then repeated timed runs,
  reporting median, min and max per (function, grid) cell, plus the wasm
  artifact size in the results context.
- **Machine-readable results**: JSON written under `bench/results/`
  (gitignored) using the top-level `{"context": ..., "benchmarks": [...]}`
  shape, mirroring Google Benchmark's top level so the existing upstream
  `scripts/benchmarks_analyze_results.py` can be pointed at it (per-entry
  fields are our own schema).
- **Recorded baseline** `docs/benchmarks.md`: methodology, reference
  machine and Node version, the measured table, artifact size, and the
  explicit protocol the future pthreads build will be compared against
  (same harness, same seeds, compare medians).
- **npm script** `npm run bench`, and a vitest smoke test asserting the
  harness runs on a tiny grid and emits structurally valid JSON — with NO
  timing assertions anywhere and no CI timing gates.
- **Docs**: an AD entry locking the harness contract (manifest-driven
  coverage, median-based reporting, no timing gates), a README development
  note, and a phase-3 status line in `docs/wasm-port-research.md`.

Not in this change: the pthreads build itself, CI performance jobs,
browser-side timing, plotting/analytics beyond JSON compatibility.

## Capabilities

### New Capabilities

- `benchmarks`: a reproducible, manifest-driven timing harness for the
  shipped wasm artifact in Node — coverage, methodology, results format,
  and the no-timing-gates rule.

### Modified Capabilities

- None. The harness only observes the existing API surface.

## Impact

- **New code**: `scripts/bench.mjs`, `tests/bench.test.ts`,
  `docs/benchmarks.md`, `bench/results/.gitkeep` (results JSON gitignored).
- **Edited code**: `package.json` (one script entry), `.gitignore`,
  `README.md`, `docs/decisions.md` (one AD entry),
  `docs/wasm-port-research.md` (phase-3 status line).
- **Untouched**: `HighMap/**`, the wasm build and binding surface, every
  existing test, the showcase, the parity workflows.
- **Cost**: one local run of the full suite is bounded (design sets a
  wall-clock target with per-cell rep caps); CI cost is unchanged because
  timings never gate.
- **Verification**: smoke test in vitest; the baseline doc records a real
  full run on the reference machine.
