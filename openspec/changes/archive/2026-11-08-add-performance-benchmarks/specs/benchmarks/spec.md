# Spec Delta

## Purpose

Defines a reproducible, manifest-driven timing harness for the shipped wasm
artifact in Node — its coverage, methodology, results format, and the rule
that timings never gate a test run.

## ADDED Requirements

### Requirement: Manifest-driven timing harness

The repository SHALL provide `npm run bench` that, after `npm run build`,
loads the built artifact through the public JS loader and times every
function listed in `HighMap/wasm/manifest.md` at 256², 512² and 1024²
grids with fixed seeds.

#### Scenario: full run
- **WHEN** `npm run bench` runs after `npm run build`
- **THEN** every manifest function appears in the results at all three grid
  sizes
- **AND** a human-readable table is printed and a JSON file is written
  under `bench/results/`

#### Scenario: narrow run
- **WHEN** the harness is invoked with its filtering flags (by function
  and/or grid size)
- **THEN** only the requested cells are timed and reported

#### Scenario: artifact missing
- **WHEN** the harness runs without a built artifact present
- **THEN** it exits non-zero with a message telling the user to run
  `npm run build` first

### Requirement: Timing methodology

The harness SHALL perform one untimed warmup run per cell, then repeat
timed runs within a per-cell budget, and SHALL report at least the median,
minimum and maximum of the measured durations per cell. It SHALL NOT be
sensitive to machine speed in its pass/fail behavior.

#### Scenario: repeated invocations
- **WHEN** the harness runs twice on the same machine
- **THEN** both runs produce results for every cell with identical seeds
  and input construction, differing only in measured durations

#### Scenario: minimum repetitions
- **WHEN** any cell is timed in a full run
- **THEN** it reports at least three timed repetitions alongside its
  statistics

### Requirement: Results format

Each run SHALL write one JSON file containing a `context` object (Node
version, platform, git commit, wasm artifact size in bytes) and a
`benchmarks` array with one entry per (function, grid) cell carrying at
least name, grid, repetition count and the reported duration statistics.

#### Scenario: structural validation
- **WHEN** a results file is read
- **THEN** `context` and `benchmarks` are present and every entry contains
  the required fields with finite numeric durations

#### Scenario: analyzer compatibility
- **WHEN** a results file is consumed by a Google-Benchmark-style analyzer
  that reads a top-level `benchmarks` array
- **THEN** the file is readable without reshaping its top level

### Requirement: No timing gates

Neither `npm test` nor any CI workflow SHALL fail based on measured
durations. The bench smoke test SHALL only assert that the harness runs and
emits structurally valid results on a small grid.

#### Scenario: CI stability
- **WHEN** the harness becomes slower or faster on a different machine
- **THEN** `npm test` and CI outcomes do not change

### Requirement: Recorded baseline

`docs/benchmarks.md` SHALL record the methodology, the reference machine
and Node version, a full measured baseline table, the artifact size, and
the comparison protocol a future pthreads build must follow (same harness,
same seeds, compare medians against this baseline).

#### Scenario: pthreads comparison
- **WHEN** a pthreads build lands
- **THEN** `docs/benchmarks.md`'s protocol allows an apples-to-apples
  median comparison per (function, grid) cell without re-inventing the
  harness

#### Scenario: attributable results
- **WHEN** a reader consults a baseline row
- **THEN** the document states the machine, Node version, git commit and
  wasm artifact size the row was measured with
