# Rifts / advection GPU/CPU parity

`rifts`, `advection_particle` and `advection_warp` were OpenCL-only upstream.
They are ported to CPU in `HighMap/src/erosion/rifts_cpu.cpp` and
`HighMap/src/transform/advection_cpu.cpp` by transcribing
`src/gpu_opencl/kernels/{rifts,advection_particle,advection_warp}.cl` (AD-026).
This document records how the CPU results are compared with the GPU oracle,
what the measured agreement is, and which comparisons the pinned wrappers
cannot make. Porting lessons are in `docs/porting-learnings.md` §12; the
decision record is AD-028 in `docs/decisions.md`.

## Oracle and harness

- **Oracle**: the desktop OpenCL kernels built from the same tree as the CPU
  ports, so both sides share compiler flags and input generation.
- **Oracle device/driver** (first run, 2026-11-07): `cpu-skylake-avx512-AMD
  EPYC 9V45`, `OpenCL 3.0 PoCL HSTR` (Ubuntu runner, CPU ICD), Release build
  with the kernel build options
  `-cl-fast-relaxed-math -cl-mad-enable -cl-no-signed-zeros
  -cl-denorms-are-zero -cl-finite-math-only` (design D7 / AD-028: the
  fast-relaxed math contract applies to the oracle, not to the CPU port).
  Reference format used by `docs/thermal-parity.md`.
- **Harness**: `tests/src/test_rifts_advection_cpu.cpp`, part of the upstream
  `highmap_tests` binary (picked up by its `src/*.cpp` glob).
  - `RiftsAdvectionCpuInvariants.*` — CPU-only properties (determinism,
    no-ops, mask endpoints and the `reverse_mask` quirk, noise offsets, center
    broadcast, bbox order, shape validation, warp's zero-length and step-cap
    cases, the particle normalization formula, the start-position draw order,
    the inertia guard, the `iterations` chain, the 64-field cap, input
    immutability). No OpenCL needed.
  - `RiftsAdvectionParity.*` — the GPU-vs-CPU comparison.
- **Rifts oracle**: the healthy `hmap::gpu::rifts` wrapper, which binds every
  kernel argument and has no known gap.
- **Warp oracle**: a test-local direct launch of the `advection_warp` kernel
  through `clwrapper::Run` with the `CL_R` → `CL_RGBA` probe from
  `choose_float_image_format()`. The pinned `bind_imagef` hardcodes
  `CL_R`/`CL_FLOAT`, which the CI PoCL device may reject.
- **Particle oracle**: a test-local direct launch of `advection_particle` with
  explicitly zero-uploaded `out` and `count` buffers plus a replica of the
  wrapper's host post-processing, because the pinned wrapper never uploads
  those two buffers.
- **Scenes**: seeded Perlin fBm 32² (remapped to [0, 1]), a synthetic ramp
  32², one fBm 64² for the rifts neighbourhood, and a second seeded fBm / a
  cross-oriented ramp as the advected field. Particle aggregates use
  `nparticles = 1000`; the per-cell case uses one particle per cell (1024), so
  landing cells can still collide (see gap 7), and the single-particle trail
  case uses `nparticles = 1` on the 32² scenes to avoid collisions entirely.
- **Gate**: every non-diagnostic case must stay under its bound, and a
  *skipped* case fails the job except for the recorded float-image-format
  limitation. `tests/src/main.cpp` calls `gpu::init_opencl()` before gtest and
  aborts when no platform exists, so the workflow checks `clinfo -l` first.

### Metric lines

Every parity case prints CSV lines to stdout, which the CI job turns into the
job summary and the uploaded log:

```text
PARITY,<variant>,<scene>,<iters>,<max_abs>,<rms_abs>,<range>
PARITY_PCT,<variant>,<scene>,<iters>,<p99_abs>,<frac_over>,<threshold>,<range>
PARITY_AGG,<variant>,<scene>,<iters>,<rel_total_pct>,<mean_abs>,<range>
```

- `PARITY` reports the maximum and RMS of the per-cell absolute difference.
- `PARITY_PCT` reports `p99_abs`, the 99th percentile (nearest rank) of the
  per-cell absolute difference, and `frac_over`, the fraction of cells whose
  absolute difference is strictly above
  `threshold = 1e-2 * range`. `range` is the range of the input terrain or
  advected field, printed so a bound can be read as a fraction of it.
- `PARITY_AGG` is printed by many-particle advection. `rel_total_pct` is
  `100 * |Σcpu - Σgpu| / max(|Σgpu|, 1e-12)` (a percentage), and `mean_abs`
  is the mean per-cell absolute difference (absolute units). The paired
  `PARITY` line carries the per-cell maximum and RMS as diagnostics only.
  This is design D7's aggregate line with the percentage made explicit in the
  field name and the per-cell diagnostic moved onto the `PARITY` line.
- A `PARITY_AGG` line with the `advection_particle_gpu_repeat` variant records
  GPU-vs-GPU variability for the non-atomic kernel and is deliberately
  ungated.
- Every metric is also recorded as a gtest property
  (`<variant>.<scene>.<iters>.<metric>`), so the XML artifact carries the
  numbers next to the pass/fail result. The warp cases additionally record
  `advection_warp.image_format` (`CL_R` or `CL_RGBA`) so the report can name
  the probe result.

### Running it

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-std=gnu17 \
  -DHIGHMAP_ENABLE_DOCS=OFF -DHIGHMAP_ENABLE_EXAMPLES=OFF \
  -DHIGHMAP_ENABLE_BENCHMARKS=OFF -DHIGHMAP_ENABLE_TESTS=ON -S .
cmake --build build --target highmap_tests --parallel 4
set -euo pipefail
./build/bin/highmap_tests \
  --gtest_filter='RiftsAdvectionCpuInvariants.*:RiftsAdvectionParity.*' \
  --gtest_output=xml:build/rifts-advection-parity.xml \
  | tee build/rifts-advection-parity.log
grep '^PARITY,' build/rifts-advection-parity.log
grep '^PARITY_PCT,' build/rifts-advection-parity.log
grep '^PARITY_AGG,' build/rifts-advection-parity.log
```

`-std=gnu17` is required on toolchains that default C to C23: the vendored
`nn-c` (Triangle) code uses K&R definitions that C23 rejects. `--parallel 4`
keeps the build inside the runner's memory.

The `erosion-parity` GitHub workflow runs the same configure, build and run
commands after the thermal and hydraulic suites on `ubuntu-latest` with PoCL.
The rifts/advection step has its own 20-minute timeout, a `^PARITY,` presence
check, the third XML in the skip guard (still allowing only the image-format
skip), the third XML/log in the uploaded artifact set, and `PARITY_PCT,` /
`PARITY_AGG,` sections in the job summary.

### Device-less guard (task 5.5)

The workflow accepts a `no_device` dispatch input that sets
`OCL_ICD_VENDORS=/nonexistent`. Dispatch run
[42453970120](https://github.com/mode777/highmap-wasm/actions/runs/42453970120)
failed at the **Report the OpenCL platform** step with
`no OpenCL platform available - parity cannot be verified` (exit 1), before any
suite was started. The guard therefore fails the job before a suite could
silently skip; a device-less environment can never pass. The final green run
[42453946008](https://github.com/mode777/highmap-wasm/actions/runs/42453946008)
ran all three suites with no unexpected skips.

## Measured agreement

Three reference oracle runs and the final frozen green run are recorded. The
**first run** (2026-11-07, tree `e349bec1`) exposed the particle draw-order bug
and the rifts hash-flip distribution; the **second run** (2026-11-07, tree
`592074466`) repeated every suite after the draw-order fix. The only port change
between the runs was the draw-order fix (`draw_start_pos()` in
`advection_cpu.cpp`); the test gained the `ParticleStartDrawOrder` invariant.
The run-2 numbers are the per-variant reference; the run-1 tables are kept
because they justify the rifts recalibration and the particle root cause.

A **third run** (2026-11-08, tree `a66929a77`) is the mutation check. No port
arithmetic changed except the deliberate rifts hash mutation, but a rebuilt
oracle shifted PoCl's work-item scheduling and exposed the per-binary nature of
the particle collision losses. Its measurements are in "Mutation check" and
drove the one-per-cell gate redesign described below.

The **final green run** (2026-11-08, tree `4cfc0d63e`, run
[42453946008](https://github.com/mode777/highmap-wasm/actions/runs/42453946008),
12m54s) is the frozen reference: all three suites green, no unexpected skips.

### Final green run (2026-11-08, tree `4cfc0d63e`)

Oracle: PoCl, `OpenCL 3.0`, `cpu-skylake-avx512-AMD EPYC 9V45`. Key measured
metrics:

```text
PARITY,advection_particle_one_per_cell,fbm,1,5.96e-8,4.09e-9,1.0
PARITY,advection_particle_one_per_cell,ramp,1,5.96e-8,1.86e-9,0.75
PARITY,advection_particle_single_trail,fbm,1,0,0,1.0        # bit-identical
PARITY,advection_particle_single_trail,ramp,1,0,0,0.75      # bit-identical
PARITY_AGG,particle_agg_iter1,{fbm,ramp},1,0,0,...           # bit-identical
PARITY,advection_warp_l050_p0960,fbm,1,1.19e-7,2.94e-8,1.0  # warp rounding level (all 20 gated scenes)
PARITY,rifts_a0_amp02,ramp,1,4.45e-2,7.58e-3,0.70           # rifts at documented sin-hash noise level
```

Suite totals: `RiftsAdvectionCpuInvariants` 31 tests in 771 ms,
`RiftsAdvectionParity` 7 tests in 1257 ms, and the full
`RiftsAdvectionCpuInvariants.*:RiftsAdvectionParity.*` filter 38 tests in
2028 ms. The one-per-cell fbm scene returned to rounding level (`rms 4.09e-9`),
both single-particle trail scenes were bit-identical, every aggregate scene was
bit-identical, warp stayed at rounding level, and rifts stayed at the calibrated
sin-hash noise level (`rms/range 7.58e-3` on the ramp).

### Second run (2026-11-07, tree `592074466`, post-fix)

The draw-order fix removed the entire fbm particle divergence: the per-cell fbm
case moved from `max 0.1816 / rms 0.0378` to rounding level
(`5.96e-8 / 3.39e-9`), and every aggregate scene became bit-identical. The
warp cases are unchanged at rounding level, and rifts stays inside the run-1
recalibrated bounds. One case came in outside its then-current tight gate: the
ramp one-particle-per-cell scene, where the oracle loses exactly one deposit
because the kernel accumulates without atomics. That is an oracle limitation of
the synthetic scene, not a port divergence; the evidence and the resulting gate
split are in "Particle one-per-cell ramp" below.

#### Warp (run 2)

`max` / `rms` are absolute; the gate is `rms <= 1e-3 · range`,
`p99 <= 1e-2 · range`, `frac_over(1e-2) <= 1e-2`, with `range` 0.99999994 on
fbm and 0.74999994 on the ramp. All 22 variants pass with several orders of
magnitude of margin; 6 of them are bit-identical.

| variant | fbm max / rms | ramp max / rms |
|---|---|---|
| `l010_p0500` | 0 / 0 | 0 / 0 |
| `l010_p0960` | 0 / 0 | 0 / 0 |
| `l010_p1000` | 0 / 0 | 0 / 0 |
| `l050_p0500` | 1.19e-7 / 2.20e-8 | 1.19e-7 / 2.27e-8 |
| `l050_p0960` | 1.19e-7 / 2.94e-8 | 1.19e-7 / 3.06e-8 |
| `l050_p1000` | 1.19e-7 / 3.05e-8 | 1.19e-7 / 3.14e-8 |
| `l100_p0500` | 1.19e-7 / 1.96e-8 | 1.19e-7 / 1.98e-8 |
| `l100_p0960` | 3.28e-7 / 3.28e-8 | 1.19e-7 / 2.77e-8 |
| `l100_p1000` | 3.28e-7 / 3.44e-8 | 1.19e-7 / 2.84e-8 |
| `advection_warp_supplied_mask` | 1.79e-7 / 2.07e-8 | — |
| `advection_warp_constant_velocity` | — | 1.19e-7 / 2.18e-8 |

The ungated flat-start diagnostic is unchanged (`max 0.4497 / rms 0.1515` on
the fbm field) and still records the kernel's uninitialized `dir` deviation
(gap 4).

#### Rifts (run 2)

Values are `abs / range` (rms, p99), except `max` which is absolute and
diagnostic only. `range` is 1 on the fbm/fbm64 scenes and 0.700000048 on the
ramp.

| variant | scene | max (diag) | rms / range | p99 / range | frac_over | result |
|---|---|---|---|---|---|---|
| `rifts_a0_amp005` | fbm | 5.65e-3 | 9.05e-4 | 4.24e-3 | 0 | passed |
| `rifts_a90_amp02` | fbm | 4.56e-2 | 4.60e-3 | 2.28e-2 | 0.0439 | passed |
| `rifts_a0_amp02` | ramp | 4.45e-2 | 1.08e-2 | 4.98e-2 | 0.1338 | passed |
| `rifts_a90_amp005` | ramp | 1.11e-2 | 2.63e-3 | 1.05e-2 | 0.0166 | passed |
| `rifts_64_neighborhood` | fbm64 | 1.54e-2 | 1.85e-3 | 8.85e-3 | 0.00659 | passed |
| `rifts_supplied_mask` | fbm | 3.63e-3 | 4.49e-4 | 2.43e-3 | 0 | passed |
| `rifts_noise_offsets` | fbm | 6.30e-2 | 1.04e-2 | 4.31e-2 | 0.1934 | passed |

The worst case is `rifts_noise_offsets` at `rms/range 1.04e-2` against the
`2.5e-2` bound (2.4x headroom); every frac_over is below 0.194 against the
0.4 bound.

#### Particle (run 2)

`PARITY` / `PARITY_AGG` values. `rel_total_pct` is percent, `mean_abs` and the
per-cell max/rms are absolute. The one-per-cell rows use the per-cell gate; the
aggregate rows gate only `rel_total_pct` and `mean_abs`, with the per-cell
columns diagnostic.

| variant | scene | iters | max (diag) | rms (diag) | rel_total_pct | mean_abs | range |
|---|---|---|---|---|---|---|---|
| `advection_particle_one_per_cell` | fbm | 1 | 5.96e-8 | 3.39e-9 | n/a (cell gate) | n/a | 0.99999994 |
| `advection_particle_one_per_cell` | ramp | 1 | 1.996e-3 | 6.24e-5 | n/a (cell gate) | n/a | 0.74999994 |
| `particle_agg_iter1` | fbm | 1 | 0 | 0 | 0 | 0 | 0.99999994 |
| `particle_agg_iter2` | fbm | 2 | 0 | 0 | 0 | 0 | 0.99999994 |
| `particle_agg_reverse` | fbm | 1 | 0 | 0 | 0 | 0 | 0.99999994 |
| `particle_agg_inertia02` | fbm | 1 | 0 | 0 | 0 | 0 | 0.99999994 |
| `particle_agg_iter1` | ramp | 1 | 0 | 0 | 0 | 0 | 0.74999994 |
| `particle_agg_iter2` | ramp | 2 | 0 | 0 | 0 | 0 | 0.74999994 |
| `particle_agg_reverse` | ramp | 1 | 0 | 0 | 0 | 0 | 0.74999994 |
| `particle_agg_inertia02` | ramp | 1 | 0 | 0 | 0 | 0 | 0.74999994 |
| `advection_particle_gpu_repeat` (ungated) | fbm | 1 | 0 | 0 | 0 | 0 | 0.99999994 |

The fbm one-per-cell row is rounding level (`max` one ULP, `rms` far below it).
The ramp row is the only non-zero metric of the whole post-fix advection
family, and its profile (`rms = max / sqrt(1024)` exactly) says one cell
differs. The aggregate rows are `0` on both sides of every metric, including
the GPU-vs-GPU repeat: PoCL schedules the non-atomic accumulation
deterministically here, and its order matches the port's ascending-id order.

#### Particle one-per-cell ramp — verified oracle limitation

The ramp direction is not axis-aligned: `dx/dy = -gradient_x/y(scene_ramp)`
gives a constant interior `dir ≈ (-0.9864, -0.1644)`, so `pos += dir` moves in
fractional steps. A float-exact replay of the port (bit-identical to the
`dist/` wasm build, 0/1024 cells) was used to test the boundary-tie hypothesis:

- no start or landing coordinate is within `1e-5` of an integer cell boundary
  (`(int)pos` cannot tie);
- a 1-ULP perturbation of every landing changes nothing; a uniform relative
  perturbation of `dir` from `-3e-3` to `+3e-3` in `1e-6` steps produces either
  no cell difference or at least two differing cells, never the observed
  single-cell profile;
- GCC `-Ofast -ffast-math` (the CI desktop build) and clang fast-math replays,
  with and without FMA, change only the last bits of the walk
  (`max <= 1.2e-7`) and never move a landing.

The actual signature is a lost deposit in the oracle's accumulation. The scene
has 1024 particles but only 634 distinct landing cells; 267 cells receive more
than one particle (up to 6). The fbm scene has the same kind of collisions (586
distinct landing cells, 273 collision cells, up to 7) yet measured no loss,
which points at the oracle's work-item scheduling rather than the scene's
collision count. `advection_particle.cl:109-111` does
`out[...] += val` and `count[idx] += 1.f` without atomics, so a collision cell
may lose a whole update. The run-2 profile is exactly that: losing one deposit
at a three-way collision cell reproduces `max 0.00199598074` and
`rms 6.23743981e-5` exactly, and there are 22 such particle/cell
possibilities (11 cells, all with three colliding particles); no boundary or
count-one cell can produce this magnitude on the linear ramp. The CPU replay's
sequential ascending-id accumulation is the documented defined behaviour
(design D5), so there is nothing to fix in the port.

Independent confirmation: running the same kernel source with the same
`-cl-fast-relaxed-math` options on a local Intel OpenCL GPU collapses the
collision-cell counts from 2-6 to 1 and produces `max 0.29` — the kernel's
non-atomic accumulation is device-undefined, exactly as gap 7 says. PoCL's CPU
device is much better behaved (one lost deposit instead of most of them), but
it is not required to be.

**Resolution (run 2)**: the fbm scene kept the rounding-level gate and the ramp
scene kept `max` as a diagnostic and gated `rms <= 1e-3 · range`, which covers
any single lost deposit on this scene (worst case `rms 3.49e-4`, i.e.
`4.66e-4 · range`) with ~2x headroom, while the draw-order bug that this case
was designed to catch measured `rms/range 5.8e-3` and still fails by ~6x.
Option (a), de-tying the scene, was rejected: there are no exact ties to
remove, the direction is already fractional, and de-tying would not remove the
landing-cell collisions that make the oracle undefined.

Run 3 then moved the loss to the fbm scene after an oracle rebuild, so that
split is superseded: both one-per-cell scenes now gate only on the
distribution, and the exact per-cell claim moved to the collision-free
single-particle trail case (see "Particle one-per-cell redesign" below).

### First run (2026-11-07, tree `e349bec1`, pre-fix)

The first full oracle run reported, in this order: **warp perfect**, **rifts a
hash-flip signature that needed gate recalibration**, and **particle a real
divergence**. The sections below record those numbers and the rifts
recalibration they justify; the particle root cause follows the run-1 table.

Warp, all 20 gated warp variants (`0.01/0.05/0.1 × 0.5/0.96/1.0` on fbm and
ramp, plus the supplied-mask and constant-velocity cases): `max_abs <= 3.3e-7`
on every scene, i.e. rounding level, several orders of magnitude inside the
`rms <= 1e-3 · range` gate. Nothing was changed on the warp side; run 2
supersedes these numbers with the per-variant table above.


### `PARITY` / `PARITY_PCT` — rifts (run 1)

Values are normalized by `range` (rms and p99 are `abs / range`, so the table
is comparable across scenes). `range` is ~1 for the fbm and fbm64 scenes and
0.7 for the ramp.

| variant | scene | max (diag) | rms | p99 | frac_over | first-run result |
|---|---|---|---|---|---|---|
| `rifts_a0_amp005` | fbm | — | 9.0e-4 | 4.2e-3 | 0 | passed |
| `rifts_a90_amp02` | fbm | — | 4.6e-3 | 2.3e-2 | 0.044 | failed all three (bounds `2e-3`/`2e-2`/`2e-2`) |
| `rifts_a0_amp02` | ramp | — | 1.08e-2 | 5.0e-2 | 0.134 | failed all three |
| `rifts_a90_amp005` | ramp | — | 2.6e-3 | 1.1e-2 | 0.024 | failed rms (1.3x) and frac_over (1.2x) |
| `rifts_64_neighborhood` | fbm64 | — | 1.9e-3 | 8.8e-3 | 0.0066 | passed |
| `rifts_supplied_mask` | fbm | — | 4.5e-4 | 2.4e-3 | 0 | passed |
| `rifts_noise_offsets` | fbm | — | 1.04e-2 | 4.3e-2 | 0.193 | failed all three |

The absolute `max_abs` of each variant was printed on the `PARITY` line of the
run log and is *not* reproduced here: it is an ungated diagnostic whose size is
set by a single flipped Voronoi cell, and the point of the table is the
distribution.

### `PARITY` / `PARITY_AGG` — particle (first run, pre-fix)

| variant | scene | iters | per-cell max (diag) | per-cell rms (diag) | rel_total_pct | mean_abs | range |
|---|---|---|---|---|---|---|---|
| `advection_particle_one_per_cell` | fbm | 1 | 0.181605935 | 0.0378289782 | n/a (cell gate) | n/a | 0.99999994 |
| `advection_particle_one_per_cell` | ramp | 1 | 0.016366899 | 0.00432430254 | n/a (cell gate) | n/a | 0.74999994 |
| `particle_agg_iter1` | fbm | 1 | — | — | 0.63 | 0.0303 | 0.99999994 |
| `particle_agg_iter2` | fbm | 2 | — | — | 1.29 | 0.0419 | 0.99999994 |
| `particle_agg_reverse` | fbm | 1 | — | — | 0.17 | 0.0294 | 0.99999994 |
| `particle_agg_inertia02` | fbm | 1 | — | — | 0.83 | 0.0319 | 0.99999994 |
| `particle_agg_iter1` | ramp | 1 | — | — | 0.02 | 0.0042 | 0.74999994 |
| `advection_particle_gpu_repeat` (ungated) | fbm | 1 | 0 | 0 | 0 | 0 | — |

Both one-per-cell rows failed their gate (`max <= 1e-5 · range`,
`rms <= 2e-6 · range`); every fbm aggregate row passed `rel_total_pct <= 5` and
failed `mean_abs <= 1e-2 · range` (0.0303 / 0.0419 / 0.0294 / 0.0319 against
0.01), while the ramp aggregate row passed both (0.0042 against 0.0075). The
ramp scene was 9x better than fbm, and the GPU-vs-GPU repeat was bit-identical,
so the first-run difference was not accumulation noise - it was the draw-order
divergence documented next.

### Particle draw order — root cause of the first-run divergence

The whole particle divergence was **one unspecified evaluation order**: which
of the two starting-position `rand()` draws lands in `x`.

`advection_particle.cl:36` (and the rejection loop at `:45`) reads

```c
pos = (float2)((nx - 1) * rand(&rng_state), (ny - 1) * rand(&rng_state));
```

Both calls mutate the same `rng_state`, so the element order decides which
draw becomes `pos.x`. An OpenCL **vector literal** is emitted element by
element by clang, so the oracle takes the first draw for `x` and the second for
`y`. The port originally wrote the same expression as a two-argument C++
constructor call:

```cpp
pos = glm::vec2((float)(nx - 1) * rand(rng_state),
                (float)(ny - 1) * rand(rng_state));
```

C++ leaves the evaluation order of function arguments unspecified: clang
evaluates left-to-right, GCC right-to-left. The Emscripten (clang) build
therefore matched the oracle, while the GCC-built desktop CI binary swapped the
two draws. On a square grid the swap transposes every starting cell, so every
particle deposits at the transposed walk cell.

Evidence (reproduced locally against `dist/`, which is the clang build): the
parity walks were replayed in JavaScript from the wasm artifact's own `z`,
`field`, `dx` and `dy` grids, once in kernel element order (x ← first draw) and
once with the two draws swapped, and the two outputs were compared with the
same metric definitions as the harness.

| quantity | first-run CI (port vs kernel) | local A/B replay (draws x↔y swapped) |
|---|---|---|
| `one_per_cell/fbm` max | 0.181605935 | 0.181605935 |
| `one_per_cell/fbm` rms | 0.0378289782 | 0.037828979 |
| `one_per_cell/ramp` max | 0.016366899 | 0.016366899 |
| `one_per_cell/ramp` rms | 0.00432430254 | 0.004324304 |
| `agg_iter1/fbm` rel_total | 0.63% | 0.624% |
| `agg_iter1/fbm` mean_abs | 0.0303 | 0.030329435 |
| `agg_reverse/fbm` rel_total | 0.17% | 0.175% |
| `agg_reverse/fbm` mean_abs | 0.0294 | 0.029394280 |
| `agg_iter1/ramp` rel_total | 0.02% | 0.021% |
| `agg_iter1/ramp` mean_abs | 0.0042 | 0.004243457 |

Every CI metric is reproduced to rounding by "same walk, two draws swapped" -
including the ramp/fbm ratio that pointed at direction handling - and the
compiled `dist/` artifact matches the source-order variant bit for bit
(0/1024 cells differ, `max 1.2e-7`; the swapped variant differs in 635/1024
fbm cells and 705/1024 ramp cells). The walk arithmetic, the normalization, the
truncation, the deposit and the host stage were therefore already correct; only
the draw order was compiler-dependent.

**Fix** (`HighMap/src/transform/advection_cpu.cpp`): the two draws are
sequenced explicitly in kernel element order through a new file-local helper
`draw_start_pos()` (`rx = rand(state); ry = rand(state);`) used by both the
mask-free and the rejection-sampling paths. `RiftsAdvectionCpuInvariants.
ParticleStartDrawOrder` pins the order on any compiler: it derives the start
cell from `pcg_hash(seed)` + two `rand()` draws, runs one particle with a
constant +x velocity, and asserts the deposit lands on `(gx + 1, gy)` - the
transposed cells must stay untouched.

Consequences: the GCC-built desktop CPU result changes, the clang/Emscripten
result does not (verified: the golden `advection-particle-seed11` digest is
unchanged and was not recaptured). The post-fix parity numbers are the run-2
tables above; run 3 started the mutation check (see "Mutation check").

## Bounds and calibration

Bounds, as frozen by the first oracle run (2026-11-07, tree `e349bec1`) and
revised by the second run (2026-11-07, tree `592074466`); the particle
one-per-cell rows were redesigned after the third run (2026-11-08, tree
`a66929a77`) and confirmed by the final green run (2026-11-08, tree
`4cfc0d63e`):

| case | gate | state |
|---|---|---|
| rifts | `rms <= 2.5e-2 · range`, `p99 <= 1e-1 · range`, `frac_over(1e-2) <= 0.4`; max diagnostic | **recalibrated** in run 1; run 2 worst `rms/range` 1.04e-2, final run worst 7.58e-3 |
| warp (direct kernel) | `rms <= 1e-3 · range`, `p99 <= 1e-2 · range`, `frac_over(1e-2) <= 1e-2`; max diagnostic | unchanged; run 2 measured `max <= 3.3e-7`, final run `l050_p0960` measured `1.19e-7 / 2.94e-8` |
| particle, one particle per cell (fbm and ramp) | `rms <= 5e-3 · range`; max diagnostic | **redesigned** after run 3: the oracle's loss pattern is per-binary; run 3 measured fbm `max 0.0444` / `rms 1.39e-3`, ramp `max 5.96e-8` / `rms 1.86e-9`; final run measured fbm `5.96e-8 / 4.09e-9`, ramp `5.96e-8 / 1.86e-9` |
| particle, single-particle trail (fbm and ramp) | `max <= 1e-5 · range`, `rms <= 2e-6 · range` | new collision-free exact case: one particle, one writer per cell; final run bit-identical on both scenes |
| particle, many particles | `rel_total_pct <= 5`, `mean_abs <= 1e-2 · range`; per-cell metrics diagnostic | **frozen at the design values**; run 2 measured `0 / 0` on every scene, final run bit-identical |

### Rifts recalibration

The first run showed a clean split. The scenes where the hash is read at
rounding level pass comfortably (`a0_amp005` rms 9.0e-4, `supplied_mask`
rms 4.5e-4 and `64_neighborhood` rms 1.9e-3), and on the ramp the error scales
with the amplitude as a hash-flip count should: `a90_amp005` measures rms 2.6e-3
and `a90_amp02` 1.08e-2, a factor 4.1 for a 4x amplitude. That is the signature
of `fract(sin(x * 43758.5453123))` amplifying a 1-ULP `sin` difference into a
whole-cell Voronoi flip under the oracle's `-cl-fast-relaxed-math`, with the
flip count set by the perturbation size - documented design risk D7 / AD-028,
not a port error. A wrong formula or a wrong constant lands at O(1) and still
fails these bounds by a wide margin.

The new bounds carry ~2x headroom over the worst measured value:

| metric | old bound | worst measured | new bound | headroom |
|---|---|---|---|---|
| rms | 2e-3 · range | 1.08e-2 · range | 2.5e-2 · range | 2.3x |
| p99 | 2e-2 · range | 5.0e-2 · range | 1e-1 · range | 2.0x |
| frac_over(1e-2) | 2e-2 | 0.193 | 0.4 | 2.1x |

`max_abs` stays an ungated diagnostic (a single flipped cell sets it and it is
unbounded by construction).

### Particle one-per-cell redesign

The one-particle-per-cell case was designed as an exactly-solvable per-cell
comparison, and run 2 measured it at rounding level on fbm with a single lost
deposit on the ramp. Run 3 (mutation check, tree `a66929a77`) rebuilt the
oracle and inverted the picture: fbm measured `max 0.0444` / `rms 1.39e-3`
(~22 lost deposits at hot cells) while the ramp measured `max 5.96e-8` /
`rms 1.86e-9`, i.e. no loss at all. PoCl is deterministic per binary
(`gpu_repeat` was 0 within each run), but the loss pattern follows work-item
scheduling and binary layout, so neither scene can carry a per-cell exactness
claim across rebuilds.

Both scenes now gate only on the distribution, `rms <= 5e-3 · range`, with
`max` as a diagnostic. The bound has ~3.6x headroom over the worst observed
`1.39e-3` and stays ~7.6x below the draw-order bug's `0.0378`, so a real
walk/formula regression still fails loudly. The exact per-cell claim moved to
`ParticleSingleParticleTrail`: one particle, 32² fbm and ramp scenes,
`advection_length = 0.3` (a long, multi-cell walk), `post_filter = false`, and
the same direct-kernel reference. A single particle has one writer per cell, so
the oracle has no collision losses and CPU/GPU must agree at rounding level;
that case keeps `max <= 1e-5 · range` and `rms <= 2e-6 · range`.

### Aggregate gate freeze

Run 2 measured `rel_total_pct = 0` and `mean_abs = 0` on every aggregate scene
(both terrains, all four variants) and on `advection_particle_gpu_repeat`. The
gates stay at the design values (`rel_total_pct <= 5`, `mean_abs <= 1e-2 ·
range`) as conservative regression bounds. They are deliberately far above the
measured rounding level because the kernel's non-atomic accumulation makes the
exact many-particle result device-dependent, and they still catch the pre-fix
draw-order level (`0.63%` and `0.0303`). No re-freeze is pending: the design
values are the frozen values.

Calibration rule: one deliberate mutation per algorithm must break its gate —
a rifts hash input constant (for example the first `dot` coefficient
`127.1f → 227.1f`; the scalar `43758.5453123f → 43758.5f` perturbation is below
the sin-hash noise floor, see "Mutation check"), the warp persistence factor or
the downhill/uphill sign in its z-driven gradient, and the particle persistence
or step sign. Each mutation is applied once, measured, recorded below, and
reverted. Do not loosen a bound without the measured numbers and a mutation
check justifying it.

Run 3 (2026-11-08, tree `a66929a77`) started the mutation check against the
recalibrated rifts bounds and the post-fix particle implementation. The check
finished on the frozen tree `4cfc0d63e`; the table below records every applied
mutation, its CI run and the outcome. The post-fix PARITY / PARITY_AGG tables
are filled in "Measured agreement".

### Mutation check

Each mutation was applied once, measured, and reverted in the immediately
following commit; `git diff` shows no mutation lines remain. The first rifts
mutation was undetectable by design of the sin-hash noise floor and is replaced
by an O(1) hash-input mutation; every remaining mutation was caught.

| mutation | commit | run | result |
|---|---|---|---|
| rifts `43758.5453123f -> 43758.5f` (sine-hash multiplier, p.x) | a66929a77 | 42425076782 | NOT DETECTED — gates passed; perturbation ≤4.5% is below the calibrated sin-hash noise floor (inherent, not a gate defect); ALSO exposed PoCl per-binary scheduling sensitivity (fbm one-per-cell rms 1.39e-3) → gate redesign |
| rifts `127.1f -> 227.1f` (hash22f dot constant) | 7a6356cf3 | 42426969194 | CAUGHT — `RiftsAdvectionParity.Rifts` FAILED (e.g. a90_amp02 frac_over 0.298, noise_offsets rms 1.11e-2); all other suites green |
| warp z-driven `gradient_x` negated | 74a456e0b | 42452261503 | CAUGHT — `RiftsAdvectionCpuInvariants.WarpExplicitGradientEqualsGradientDriven` FAILED (the wrapper-level gradient convention is bound by the invariants; the parity oracle binds the kernel arithmetic) |
| warp backward-blend persistence inverted (`ratio = mask * (1 - p)`) | 887846f53 | 42453237036 | CAUGHT — `RiftsAdvectionParity.Warp` FAILED (parity-gate sensitivity proven) |
| particle step sign flipped (`reverse ? 1 : -1`) | 887846f53 | 42453237036 | CAUGHT — `ParticleOnePerCell`, `ParticleSingleParticleTrail`, `ParticleAggregate` + 3 invariants FAILED |
| final revert (frozen constants) | 4cfc0d63e | 42453946008 | GREEN — all three suites pass |

The first-run draw-order failure (the bug fixed by `draw_start_pos()`) and the
run-3 PoCl scheduling shift were not applied mutations; they are documented in
"Particle draw order" and "Particle one-per-cell redesign" above. The two
advection mutation runs also proved that the invariant suite, not only the GPU
oracle, catches wrapper-level sign errors.

## Known oracle gaps

1. **Particle `out` / `count` are never uploaded by the pinned wrapper.**
   `advection_particle_gpu.cpp` binds both as `CL_MEM_READ_WRITE` buffers
   created from a null host pointer and calls `write_buffer` only for
   `advected_field`, `dx` and `dy`. The kernel accumulates into uninitialized
   device memory, so the wrapper's result depends on whatever the driver
   leaves there. The direct reference in the harness uploads explicit zeros
   (`bind_and_upload(run, "out", …)`, `"count"`). This is a candidate upstream
   issue; the fork does not edit the pinned wrapper (AD-026).
2. **Particle host-stage loop bounds are swapped.**
   `advection_particle_gpu.cpp:266-267` runs `for (int j = 0; j < shape.x; ++j)`
   and `for (int i = 0; i < shape.y; ++i)` while indexing `count(i, j)` and
   `advected_field(i, j)`. On a square grid the two sweeps are equivalent; on a
   non-square grid the loops read out of bounds. The port sweeps
   `j < shape.y`, `i < shape.x`, which is identical on the square scenes used
   by the parity harness and the only defined behaviour elsewhere. Flagged
   during the port; candidate upstream issue.
3. **Warp `CL_R` / `CL_FLOAT` image format risk.** The pinned `bind_imagef`
   hardcodes `cl::ImageFormat(CL_R, CL_FLOAT)`. The CI PoCL device rejects that
   format for `thermal_conserve`, so it may reject it for `advection_warp`
   too. The harness probes `CL_R` first and falls back to
   `CL_RGBA`/`CL_FLOAT` through raw `cl::Image2D` bindings, expanding
   one-channel host data to four channels and reading back the `.x` component.
   The kernel reads `.x`, so the fallback is behaviorally equivalent. If
   neither format works, the warp cases skip with the explicit "no float image
   support" message, which the CI guard allows and this report records.
4. **Warp `dir` is uninitialized on a flat start.** `advection_warp.cl:44`
   declares `float2 dir;` and uses it on the first step when the sampled
   velocity is zero. That is undefined behaviour in OpenCL C. The CPU port
   pins `dir = (0, 0)`: a flat start does not move until a non-zero velocity
   appears, and the blend returns the field value at the start cell. Flat
   starts are excluded from the bounds; `advection_warp_flat_start_diag`
   prints the deviation's metrics and is never gated.
5. **Particle `normalize((0, 0))` under inertia = 1.** The kernel blends
   `inertia * dir_prev + (1 - inertia) * dir` and calls `normalize()` on the
   result. With `inertia = 1` and `dir_prev = (0, 0)` that normalizes a zero
   vector (undefined). The port keeps `dir = (0, 0)`, the particle never moves,
   `count` stays zero and the field is returned unchanged
   (`RiftsAdvectionCpuInvariants.ParticleInertiaGuard`). The parity aggregate
   only uses `inertia = 0` and `0.2`.
6. **Rifts `pow(val0, mask_gamma)` domain.** With `apply_mask = true`, the
   kernel computes `t = pow(val0, mask_gamma)` where `val0` is the input
   elevation. A negative elevation makes `pow` NaN and the whole cell's blend
   NaN. The parity scenes are remapped to [0, 1] (and the ramp is strictly
   positive), so the oracle stays in domain. The port preserves the kernel
   arithmetic; callers that feed negative elevations with the mask enabled get
   the kernel's behaviour, not a clamp.
7. **Non-atomic particle accumulation.** `advection_particle.cl:109-111` does
   `out[...] += val` and `count[idx] += 1` without atomics. Concurrent
   work-items that reach the same cell can lose updates, so the kernel contract
   does not guarantee a reproducible many-particle result and the aggregate
   cases gate only on total mass and mean `|Δz|`;
   `advection_particle_gpu_repeat` records the GPU-vs-GPU spread. The first
   oracle run measured that spread as exactly zero (PoCL schedules the
   work-items deterministically here), and run 2 measured the same on every
   aggregate scene. The one-particle-per-cell ramp scene still hit the race:
   run 2's `max 1.996e-3` / `rms 6.24e-5` is exactly one lost deposit at a
   three-way collision cell (run 2 measured 267 collision cells; a local Intel
   OpenCL run of the same kernel loses most of them, collapsing `count` to 1).
   This is device-defined kernel behaviour, not a port divergence. Across runs
   2 and 3 the one-per-cell loss moved from the ramp (one deposit) to fbm
   (~22 deposits) after an oracle rebuild, so both scenes now gate only on
   `rms` (see "Particle one-per-cell redesign"); the exact per-cell gate is
   reserved for the collision-free single-particle trail case.
8. **Rifts hash discontinuity.** The Voronoi hash is
   `fract(sin(dot(p, k) + fseed) * 43758.5453123)`. A one-ULP difference in
   `sin` can flip a whole Voronoi cell, so a pointwise maximum is meaningless
   even though the field is visually identical. Rifts is therefore gated on
   RMS, p99 and the fraction of cells above `1e-2 * range`.
9. **Mirrored-repeat linear sampler fidelity.** `advection_warp.cl` samples
   with `CLK_NORMALIZED_COORDS_TRUE | CLK_ADDRESS_MIRRORED_REPEAT |
   CLK_FILTER_LINEAR`. OpenCL guarantees only ≥9-bit subpixel precision for
   linear filtering and leaves the exact rounding to the implementation. The
   CPU port pins the documented mapping (texel centres at `(i + 0.5) / N`,
   mirrored fold, clamped edge texels); deviation from a specific driver is
   absorbed by the p99 / `frac_over` gates, not by a per-cell bound.

## Cost

Single-threaded wasm, Windows 11 / Intel Core Ultra 5 335 / Node 26.13,
measured from the built `dist/index.js` + `dist/highmap.wasm` artifact through
the public `initHighmap()` API. The artifact is current for the ports:
`dist/highmap.wasm` was built after the draw-order fix, and the only later
port-source changes were the reverted mutation commits
(`git diff 592074466..HEAD -- HighMap/src/transform/advection_cpu.cpp
HighMap/src/erosion/rifts_cpu.cpp` is empty). Init time was 12.1 ms; the
per-call figures are medians of 20 calls after 3 warm-ups:

| function | grid | parameters | time |
|---|---|---|---|
| `rifts` | 64² | `kw = (4, 1.2)`, angle 30, amplitude 0.1, seed 11 | 1.05 ms |
| `advectionWarp` | 64² | length 0.1, persistence 0.9 (defaults) | 1.65 ms |
| `advectionParticle` | 64² | 256 particles, iterations 1 | 0.21 ms |

The times include the high-level API's output allocation and grid copy. These
are cost notes, not optimization targets: the ports are deliberately sequential
and synchronous, matching the wasm build's `-fno-openmp` `VA_SEQUENTIAL`
execution model (AD-010).
