# Performance benchmarks

This document is the committed phase-3 baseline for the standalone wasm
artifact: what `npm run bench` measures, how it measures it, the numbers a
single-threaded build produced on the reference machine, and the protocol a
future pthreads build must follow to be compared against them.

Pointer chain:

- **Surface / coverage**: `HighMap/wasm/manifest.md` is the source of truth for
  the bound function set (AD-008). The harness parses it with the same rules as
  `tests/surface.test.ts`, so a new manifest entry automatically grows the
  timing suite — there is no hand-maintained function list to rot.
- **Harness**: `scripts/bench.mjs`, exposed as `npm run bench` (task 1.1).
  Filters: `npm run bench -- --only <substr> --sizes 256,512`.
- **Results**: every run writes
  `bench/results/bench-<ISO-date>-<hostname>.json` (gitignored; only
  `bench/results/.gitkeep` is tracked). The top level is
  `{"context": ..., "benchmarks": [...]}`, Google-Benchmark compatible so
  upstream's `scripts/benchmarks_analyze_results.py` can read it. The committed
  and reviewed baseline is this document.

## Methodology

- **Inputs are built once per cell** from fixed seeds and per-family recipes
  (fBm/Perlin noise for the noise family, showcase-scale erosion parameters for
  the erosion families). `advectionParticle`, `advectionWarp` and `rifts` get
  their recipe-defined companion grids; those companions are built outside the
  timed region, exactly like every other input.
- **One untimed warmup** run per (function, grid) cell, so the first-call cost
  — lazy init, JIT warm-up, allocator growth — never enters a median.
- **3–10 timed repetitions per cell**, bounded by a ~2 s early-stop budget:
  the harness stops adding reps once the cell's accumulated timed work passes
  the budget, but never below **3 reps** and never above **10**. Slow cells
  therefore report exactly 3 reps; cheap cells report 10. Rep counts are in the
  results JSON (`reps`) and are visible in the console table as `(n)`.
- **Reported statistics** are the median (headline), minimum and maximum of the
  timed reps. The median absorbs GC pauses and scheduler noise; no pass/fail
  anywhere depends on a duration (AD-029).
- **Pristine input per rep for in-place mutators**: every function that
  mutates its grid in place is timed on a fresh copy of the recipe input, so
  later reps never measure already-eroded terrain. The copy itself is outside
  the timed region.
- **Timing source**: `performance.now()` (sub-millisecond on this Node build),
  which the design chose over `Date.now()` for quantization reasons.
- **Grid sizes**: 256² (showcase scale), 512², 1024² (practical ceiling for the
  0.0.x surface). Erosion cost scales roughly with cell count, which the table
  makes visible.

## Reference run

| Field | Value |
|---|---|
| Date | 2026-11-08 (run started 12:36:14 UTC) |
| Host | `SHS5CG6392R1MC` |
| OS / platform / arch | Windows, `win32` / `x64` |
| Node | `v26.13.0` |
| Git commit | `6466300e5` (proposal commit; harness work uncommitted at run time) |
| Wasm artifact | `dist/highmap.wasm`, **592,141 bytes** (SHA-256 `1733387e94fc86b8b9e412e7a74df84e086acfe46046d3dbe9bd2a9bc521e04f`) |
| Emscripten | 6.0.13 (pinned) |
| Cells | 36 functions × 256/512/1024 = **108 cells**, all with `reps >= 3` |
| **Total wall time** | **37.4 minutes** (2240.8 s, `npm run bench` only; `npm run build` was 35.4 s on an incremental tree) |

### The 10-minute target, honestly

The design (`design.md` decision 3) set a full-suite wall-clock target under
**~10 minutes**. The measured run took **37.4 minutes**, about 3.7× over. The
cause is structural, not a harness defect: the slowest cells are genuinely
expensive — `thermalFlatten` at 1024² alone is ~65 s per rep, and the 3-rep
floor alone costs ~3.2 minutes for that one cell. The per-cell 2 s budget
already binds on every slow cell (they all report exactly 3 reps), so there is
no rep headroom left to cut without violating the spec's "at least three timed
repetitions" requirement. We therefore record the real number instead of
weakening the methodology; a future change may add a `--sizes`-narrowed
CI/report profile, but the full local sweep is expected to take this long.
Narrow reruns (`--only`, `--sizes`) are the intended way to iterate.

The build step must be run first (`npm run build`); the harness fails fast with
a clear message when `dist/` is missing. On this incremental tree the wasm
build found "no work to do" and the whole build step took 35.4 s.

## Baseline table

Median wall time in milliseconds per (function, grid) cell, from
`bench/results/bench-2026-11-08T12-36-14-119Z-SHS5CG6392R1MC.json`. Values are
the JSON medians printed to three decimals; the untruncated values are in that
file. All 36 manifest functions are timed at all three grid sizes.

| Function | 256² median (ms) | 512² median (ms) | 1024² median (ms) |
|---|---:|---:|---:|
| add | 0.091 | 0.169 | 0.756 |
| advectionParticle | 78.301 | 198.635 | 1661.835 |
| advectionWarp | 108.006 | 905.901 | 7649.897 |
| clamp | 0.263 | 0.923 | 3.764 |
| divide | 0.297 | 0.594 | 2.914 |
| falloff | 0.580 | 1.701 | 7.072 |
| hillshade | 4.875 | 27.761 | 115.443 |
| hydraulicMcdonald | 2515.985 | 3010.593 | 4290.133 |
| hydraulicMcdonaldMultiscale | 5132.439 | 7107.482 | 9414.481 |
| hydraulicMusgrave | 431.795 | 1898.922 | 7238.633 |
| hydraulicSchott | 3320.232 | 12806.162 | 50361.779 |
| hydraulicSchottErosion | 725.367 | 2901.897 | 11684.616 |
| hydraulicStream | 11.222 | 38.991 | 171.711 |
| hydraulicVpipes | 354.841 | 1483.476 | 5599.644 |
| multiply | 0.068 | 0.393 | 2.240 |
| noise | 1.995 | 8.451 | 34.752 |
| noiseFbm | 18.670 | 77.751 | 309.186 |
| noisePingpong | 37.283 | 137.149 | 592.969 |
| noiseRidged | 35.283 | 135.191 | 527.728 |
| plateau | 215.285 | 893.881 | 8756.976 |
| remap | 0.242 | 1.498 | 5.338 |
| rifts | 40.929 | 167.086 | 650.596 |
| scale | 0.125 | 0.957 | 2.911 |
| smoothFill | 54.442 | 244.082 | 1282.948 |
| subtract | 0.071 | 0.424 | 2.452 |
| thermal | 1123.937 | 4554.619 | 17331.358 |
| thermalAutoBedrock | 1629.574 | 5638.424 | 18987.053 |
| thermalConserve | 2627.965 | 9882.838 | 38239.962 |
| thermalFlatten | 4087.267 | 17802.802 | 64759.539 |
| thermalInflate | 2218.010 | 9736.205 | 32000.418 |
| thermalOlsen | 3022.468 | 9245.715 | 33920.794 |
| thermalRib | 17.752 | 66.907 | 224.937 |
| thermalRidge | 1984.706 | 8864.054 | 30181.807 |
| thermalSchott | 1166.538 | 5102.742 | 20020.120 |
| thermalScree | 2445.238 | 10380.076 | 33327.944 |
| warp | 2.554 | 9.006 | 39.959 |

## Slowest cells

The five most expensive cells in the baseline are all 1024² cells from the
thermal and hydraulic erosion families:

1. `thermalFlatten` @1024² — **64,759.5 ms** (~64.8 s; 3 reps)
2. `hydraulicSchott` @1024² — **50,361.8 ms** (~50.4 s; 3 reps)
3. `thermalConserve` @1024² — **38,240.0 ms** (~38.2 s; 3 reps)
4. `thermalOlsen` @1024² — **33,920.8 ms** (~33.9 s; 3 reps)
5. `thermalScree` @1024² — **33,327.9 ms** (~33.3 s; 3 reps)

`thermalFlatten` @512² (17.8 s) and `thermalConserve`, `thermalOlsen`,
`thermalScree`, `thermalInflate`, `thermalRidge` and `thermalAutoBedrock` at
1024² (19–34 s) account for most of the sweep's wall time. The cheap tier is the
array-algebra layer (`add`, `subtract`, `multiply`, `divide`, `scale`, `remap`,
`clamp` — sub-millisecond at 256²) and the noise family except the fBm variants.

## Pthreads comparison protocol

When the optional Emscripten pthreads build lands (AGENTS.md rule 3, phase 3),
it is compared against this table as follows:

1. Build the pthreads artifact with the identical pinned toolchain
   (Emscripten 6.0.13) and the same source revision plus only the threading
   changes; record the new `dist/highmap.wasm` byte size.
2. On the **same machine** (or an explicitly stated equivalent one), with the
   same Node version, run the **identical harness and recipes**:
   `npm run bench` (no changes to seeds, parameters, grids or input
   construction). Different seeds or recipe tweaks make the comparison void.
3. Compare **per-cell medians** (not means, not single reps) against the table
   above, cell for cell, and record the speed-up ratio column next to the
   baseline numbers. Absolute milliseconds are machine-specific; the ratio and
   the direction of each cell are the deliverables.
4. Report cells where the pthreads build is slower or flat as well as the wins;
   note that the 2 s per-cell budget and the 3–10 rep window apply unchanged, so
   a faster build may legitimately report more reps (up to 10) than the
   baseline's 3 on slow cells.
5. Write the comparison into a new results JSON plus a dated section here; do
   not edit the baseline rows in place.
