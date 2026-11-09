# Design

## Context

See `proposal.md` for motivation. The current release profile comes from the
bootstrap design (archived change `2026-11-05-bootstrap-wasm-package`, D10),
which chose `-Os` without an A/B measurement. The wasm target is a standalone
CMake project (`HighMap/wasm/CMakeLists.txt`) built with the pinned
Emscripten 6.0.13 toolchain; the desktop build is a separate CMake project and
is unaffected by wasm-target flags. The benchmark harness already exists and
records the artifact size in every results file, and the desktop parity suite
already pins numerical behavior of the CPU ports.

The A/B experiment (same machine, same Node, baseline artifact SHA-256
verified) measured four profiles on a representative probe set, then confirmed
the winner at 1024². The results are summarized in the proposal; the raw
numbers are reproduced below for the decision record.

| profile | wasm bytes | 512² thermalFlatten | 512² hydraulicSchottErosion | 1024² thermalFlatten | 1024² hydraulicSchottErosion | 1024² advectionWarp |
|---|---:|---:|---:|---:|---:|---:|
| `-Os` (current) | 592,141 | 18,117 ms | 2,944 ms | 66,069 ms | 11,750 ms | 7,691 ms |
| `-O2` | 656,594 | 14,713 ms | 2,532 ms | — | — | — |
| `-O3` | 683,602 | 14,560 ms | 2,175 ms | 51,898 ms | 8,233 ms | 6,646 ms |
| `-O3 -msimd128` | 696,655 | 14,661 ms | 2,103 ms | — | — | — |

512² values are means of two runs; 1024² values are single runs of three timed
reps, matching the committed baseline's own conditions. Small cells were noisy
run-to-run, so the decision relies on the heavy, stable cells.

## Goals / Non-Goals

**Goals:**

- Ship a release artifact that is measurably faster on the cells that dominate
  wall-clock time, with no API, numerical, or deployment change.
- Make future codegen comparisons reproducible without editing tracked files.
- Leave the repository's benchmark baseline truthful: the table must describe
  the artifact that ships.

**Non-Goals:**

- Threading (pthreads/OpenMP), SIMD, LTO, `-ffast-math`, or any source-level
  optimization. These are separate experiments; see `docs/benchmarks.md` for
  the pthreads protocol.
- Changing the binding surface, JS wrapper, loader, or build commands.
- Optimizing the desktop build; its `-Ofast` reference profile stays as-is.

## Decisions

### D1 — Adopt `-O3` for compile and link; keep `-Os` available by override

`-O3` wins on every heavy cell: −20% `thermalFlatten`, −26%
`hydraulicSchottErosion`, −18% `advectionWarp` at 512², and −21%/−30%/−14% at
1024². It costs 91,461 bytes (+15.4%) of raw wasm, leaving the artifact at
683,602 bytes — acceptable for a terrain library and smaller than the original
2–5 MB bootstrap estimate.

Alternatives considered:

- **`-O2`**: 27 KB smaller than `-O3` but ~16% slower on
  `hydraulicSchottErosion` at 512², the second-hottest cell. The size saving
  does not compensate.
- **`-O3 -msimd128`**: only `advectionWarp` improves (~6%); small cells regress
  and the artifact grows another 13 KB. The loops are not vectorization-heavy
  enough to justify SIMD, and SIMD availability would become a consumer
  compatibility question.
- **`-Os` (status quo)**: no rationale beyond the bootstrap default; strictly
  dominated.
- **`-flto`**: deferred; potentially useful, but adds toolchain/link-time risk
  and has no measurement yet. Out of scope for a low-risk codegen change.

`-O3` is applied to both compile and link options. Emscripten's driver uses the
optimization level for the emitted JS glue as well, and keeping the two in sync
avoids surprising mismatches.

### D2 — Cache variables, not hardcoded flags

Add:

```cmake
set(WASM_OPT_FLAGS "-O3" CACHE STRING "Optimization flags for the wasm target")
set(WASM_EXTRA_FLAGS "" CACHE STRING "Extra compile/link flags for the wasm target")
```

and use both in `target_compile_options` and `target_link_options` for the
release path (the ASAN path keeps its own flags). Rationale: the next person
who wants to test `-Oz`, LTO, or SIMD should not have to create an uncommitted
diff and risk measuring a dirty tree — the experiment that produced this change
did exactly that. A cache variable also makes the chosen profile explicit in
`CMakeCache.txt`.

The variables are documented as measurement escape hatches, not a supported
build matrix: CI builds the default profile only, and `WASM_EXTRA_FLAGS` is
expected to hold ISA flags that need to reach both compile and link (for
example `-msimd128`). Fast-math flags remain forbidden by the spec.

### D3 — Re-baseline with a full sweep and keep the `-Os` table

The default artifact changes, so the baseline table must be re-measured from
the new artifact (new spec requirement). Use the existing harness unchanged:

```bash
npm run build
npm run bench          # full 36 × 3 sweep, ~30-40 min
```

Publish the new table as the current reference run, with the new artifact size
and SHA-256. Move the existing `-Os` table to a clearly dated historical
section rather than deleting it, so readers can see the measured improvement
and the pthreads protocol still has a stable reference if needed. Do not edit
the old rows in place.

If the full sweep is interrupted, rerun it rather than stitching together
narrow runs; partial runs cannot satisfy the full-baseline requirement.

### D4 — Record the experiment, not just the outcome

`docs/decisions.md` gets a new AD (next number is AD-030) that records:
the measured profile matrix, why `-O3` won, why `-O2` and SIMD were rejected,
the artifact-size trade-off, and the rule that fast-math stays off. This is
the durable evidence for a future rebase or a future "why not SIMD?" question.
`docs/wasm-port-research.md` §7 updates the phase-3 status: benchmarks and
codegen optimization done; pthreads and npm publish remain.

## Risks / Trade-offs

- **[Artifact grows 15%]** → Still under 0.7 MB raw and compressible; the
  package already ships a single wasm asset, so there is no extra request.
  If a size-constrained consumer appears, `WASM_OPT_FLAGS=-Oz` remains
  available without a code change.
- **[Benchmark noise on small cells]** → Do not claim small-cell wins; base
  acceptance on the heavy cells (erosion families and advection) that are
  stable across repeats. Record means of repeats and keep the harness
  methodology unchanged.
- **[Codegen changes numerical results]** → No fast-math or reassociation is
  enabled; the same C++ sources are compiled. Validation still runs the full
  JS test suite and the parity suite is unchanged. If a golden value shifts,
  stop and investigate rather than recapturing blindly.
- **[Cache-variable misuse]** → Only the default profile is supported in CI.
  Document the override variables next to the flags and keep the defaults
  reproducing the released artifact.
- **[Stale benchmark doc after future profile changes]** → The new baseline
  requirement makes re-baselining part of any future codegen change.
- **[Full sweep takes ~40 minutes]** → It is a one-off per profile; narrow
  `--only`/`--sizes` runs remain the iteration tool and are not a substitute
  for the published baseline.

## Migration Plan

1. Change the wasm CMake profile and rebuild `dist/`; the npm package has no
   versioned data or API migration.
2. Run the full test suite and the full benchmark sweep, then land the docs in
   the same change.
3. Rollback is a one-line revert of the CMake flags plus a rebuild; the
   historical `-Os` baseline remains in the document, so no measurement is
   lost.
