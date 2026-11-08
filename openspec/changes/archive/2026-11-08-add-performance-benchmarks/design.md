# Design

## Context

See `proposal.md` — Why. Phase 3 needs a single-threaded baseline before the
optional pthreads build (AGENTS.md rule 3: CPU-first, sequential-first). The
harness must be reproducible, cheap to run, and never able to make CI
flaky. The surface it measures is already fixed by AD-008
(`HighMap/wasm/manifest.md` is the source of truth for the exposed function
set, and `tests/surface.test.ts` asserts it), so coverage cannot drift by
accident if the harness reads the manifest.

## Goals / Non-Goals

**Goals:**

- Manifest-complete coverage of the timing suite.
- Median-based reporting with warmup, so results survive GC and scheduler
  noise.
- Machine-readable JSON results with an attributable context block.
- A committed human baseline plus a defined pthreads comparison protocol.

**Non-Goals:**

- pthreads itself, CI performance jobs, browser-side timing, statistical
  significance testing, plotting or dashboards.

## Decisions

1. **Node-only harness at `scripts/bench.mjs`** — Node is the reproducible
   baseline environment; browser cost stays observable through the
   showcase. The harness requires `dist/` to exist and fails fast with a
   "run npm run build first" message otherwise. Alternatives considered:
   browser-based timing via Playwright (rejected: scheduling noise and
   browser matrix make the baseline incomparable); reusing vitest
   `bench` mode (rejected: it couples timing to the test runner and its
   output shape is not Google-Benchmark compatible).

2. **Manifest as the coverage source of truth (AD-008 pattern)** — parse
   `HighMap/wasm/manifest.md` the same way `tests/surface.test.ts` does;
   every listed function is timed, so new surface growth automatically
   grows the suite. Per-function fixed-seed input recipes (noise seeds,
   erosion parameters at showcase scale) live in one table in the harness;
   functions needing extra grids (`advectionParticle`, `advectionWarp`,
   `rifts`) get recipe-defined companions. Alternatives considered: a
   hand-maintained function list (rejected: silently rots as the surface
   grows); timing through the C++ benchmark harness (rejected: it cannot
   see the shipped JS artifact or its marshalling cost).

3. **Warmup + median over a per-cell budget** — one untimed warmup, then
   timed repetitions capped per cell (default: at most 10 reps or ~2 s per
   cell, whichever first; at least 3 reps) with the full-suite wall-clock
   target under ~10 minutes. Report median, min, max, rep count. Medians
   absorb GC and scheduler noise; no pass/fail depends on timing.
   Alternatives considered: mean over a fixed rep count (rejected: one
   GC pause moves the mean, and a fixed count makes slow cells unbounded);
   single-shot timing (rejected: sub-millisecond quantization on Windows).

4. **Google-Benchmark-compatible top level** — results files use
   `{"context": {...}, "benchmarks": [...]}` so upstream's
   `scripts/benchmarks_analyze_results.py` can read them; per-entry fields
   are our own (`name`, `grid`, `reps`, `median_ms`, `min_ms`, `max_ms`).
   Filenames: `bench-<ISO-date>-<hostname>.json` under `bench/results/`
   (gitignored; the committed baseline lives in `docs/benchmarks.md`).
   Alternatives considered: emitting Google-Benchmark-shaped entries
   (`real_time`, `iterations`) so the analyzer needs no changes
   (rejected: it constrains our fields for no benefit — the analyzer only
   depends on the top-level array).

5. **Context captures the machine** — Node version, platform/arch, git
   commit, and `dist/*.wasm` byte size, so a baseline row is always
   attributable.

6. **No CI timing gates** — shared runners are noisy; timings gate nothing.
   CI only runs the smoke test. A future change may add an
   artifact-upload workflow if comparing across commits proves necessary.

7. **Grid sizes 256/512/1024** — 256² matches the showcase scale, 1024² is
   the practical ceiling for the 0.0.x surface; erosion-family cost scales
   roughly with cells, which the baseline table should make visible.
   Alternatives considered: a single 512² size (rejected: hides both the
   showcase-scale story and the scaling curve).

## Risks / Trade-offs

- Machine variance makes absolute numbers non-portable → the baseline doc
  pins hardware/Node and the protocol compares runs on one machine.
- Windows timer granularity → use `performance.now()`; medians over several
  reps smooth sub-millisecond quantization (rare at these grid sizes).
- Very slow cells (1024² particle advection) → per-cell rep caps bound the
  suite; `--only`/`--sizes` flags allow narrow reruns.
- Manifest parsing duplicated between harness and `tests/surface.test.ts` →
  the two intentionally share parsing rules; if they ever diverge, the
  surface test is authoritative and the harness is a task-level fix.
- Baseline staleness as the surface grows → every new manifest function
  forces a recorded baseline refresh, because a missing cell is visible in
  the table.

## Migration Plan

Purely additive: one new script, one smoke test, docs, a package.json
script entry and a .gitignore line. Nothing existing changes behavior, so
rollback is deleting the added files and the two one-line edits.
