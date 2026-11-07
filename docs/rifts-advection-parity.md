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
- **Oracle device/driver**: `TODO(CI): fill from first run` (expected format:
  `cpu-haswell-AMD EPYC 9V74 80-Core Processor`, `OpenCL 3.0 PoCL HSTR`, as
  recorded in `docs/thermal-parity.md` and `docs/hydraulic-parity.md`).
- **Harness**: `tests/src/test_rifts_advection_cpu.cpp`, part of the upstream
  `highmap_tests` binary (picked up by its `src/*.cpp` glob).
  - `RiftsAdvectionCpuInvariants.*` — CPU-only properties (determinism,
    no-ops, mask endpoints and the `reverse_mask` quirk, noise offsets, center
    broadcast, bbox order, shape validation, warp's zero-length and step-cap
    cases, the particle normalization formula, the inertia guard, the
    `iterations` chain, the 64-field cap, input immutability). No OpenCL
    needed.
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
  `nparticles = 1000`; the per-cell case uses one particle per cell (1024).
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
  -DHIGHMAP_ENABLE_EXAMPLES=OFF -DHIGHMAP_ENABLE_BENCHMARKS=OFF \
  -DHIGHMAP_ENABLE_TESTS=ON
cmake --build build --target highmap_tests --parallel 4
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

The `erosion-parity` GitHub workflow runs this suite after the thermal and
hydraulic suites on `ubuntu-latest` with PoCL, with its own 20-minute step
timeout. It also accepts a `no_device` dispatch input that points
`OCL_ICD_VENDORS` at a missing path, which proves that a device-less run fails
through this suite instead of skipping it.

## Measured agreement

`TODO(CI): fill from first run` (same format as
`docs/hydraulic-parity.md`: worst measured value per variant, then the bound).

### `PARITY` / `PARITY_PCT` — rifts

| variant | scene | worst max | worst rms | worst p99 | worst frac_over | range |
|---|---|---|---|---|---|---|
| `rifts_a0_amp005` | fbm | TODO(CI) | TODO(CI) | TODO(CI) | TODO(CI) | ~1 |
| `rifts_a90_amp02` | fbm | TODO(CI) | TODO(CI) | TODO(CI) | TODO(CI) | ~1 |
| `rifts_a0_amp02` | ramp | TODO(CI) | TODO(CI) | TODO(CI) | TODO(CI) | ~0.7 |
| `rifts_a90_amp005` | ramp | TODO(CI) | TODO(CI) | TODO(CI) | TODO(CI) | ~0.7 |
| `rifts_64_neighborhood` | fbm64 | TODO(CI) | TODO(CI) | TODO(CI) | TODO(CI) | ~1 |
| `rifts_supplied_mask` | fbm | TODO(CI) | TODO(CI) | TODO(CI) | TODO(CI) | ~1 |
| `rifts_noise_offsets` | fbm | TODO(CI) | TODO(CI) | TODO(CI) | TODO(CI) | ~1 |

### `PARITY` / `PARITY_PCT` — warp (direct kernel)

| variant | scene | worst max | worst rms | worst p99 | worst frac_over |
|---|---|---|---|---|---|
| `advection_warp_l010_p0500` … `l100_p1000` | fbm | TODO(CI) | TODO(CI) | TODO(CI) | TODO(CI) |
| `advection_warp_l010_p0500` … `l100_p1000` | ramp | TODO(CI) | TODO(CI) | TODO(CI) | TODO(CI) |
| `advection_warp_supplied_mask` | fbm | TODO(CI) | TODO(CI) | TODO(CI) | TODO(CI) |
| `advection_warp_constant_velocity` | ramp | TODO(CI) | TODO(CI) | TODO(CI) | TODO(CI) |
| `advection_warp_flat_start_diag` (ungated) | flat | TODO(CI) | TODO(CI) | TODO(CI) | TODO(CI) |

The nine (length, persistence) combinations per scene are
`0.01/0.05/0.1 × 0.5/0.96/1.0`; the variant name encodes them as
`l<length×1000>_p<persistence×1000>`.

### `PARITY` / `PARITY_AGG` — particle

| variant | scene | iters | per-cell max (diag) | per-cell rms (diag) | rel_total_pct | mean_abs |
|---|---|---|---|---|---|---|
| `advection_particle_one_per_cell` | fbm | 1 | TODO(CI) | TODO(CI) | n/a (cell gate) | n/a |
| `advection_particle_one_per_cell` | ramp | 1 | TODO(CI) | TODO(CI) | n/a (cell gate) | n/a |
| `particle_agg_iter1` | fbm | 1 | TODO(CI) | TODO(CI) | TODO(CI) | TODO(CI) |
| `particle_agg_iter2` | fbm | 2 | TODO(CI) | TODO(CI) | TODO(CI) | TODO(CI) |
| `particle_agg_reverse` | fbm | 1 | TODO(CI) | TODO(CI) | TODO(CI) | TODO(CI) |
| `particle_agg_inertia02` | fbm | 1 | TODO(CI) | TODO(CI) | TODO(CI) | TODO(CI) |
| `particle_agg_iter1` | ramp | 1 | TODO(CI) | TODO(CI) | TODO(CI) | TODO(CI) |
| `particle_agg_iter2` | ramp | 2 | TODO(CI) | TODO(CI) | TODO(CI) | TODO(CI) |
| `advection_particle_gpu_repeat` (ungated) | fbm | 1 | TODO(CI) | TODO(CI) | TODO(CI) | TODO(CI) |

`rel_total_pct` is a percentage; `mean_abs` is gated against
`1e-2 * range`. The per-cell maximum and RMS of the aggregate rows are printed
on the paired `PARITY` line and recorded as properties but never gated.

## Bounds and calibration

Provisional bounds in the harness (frozen by the first full oracle run in task
5.4; the values below are the ones the tree currently carries):

| case | gate | state |
|---|---|---|
| rifts | `rms <= 2e-3 · range`, `p99 <= 2e-2 · range`, `frac_over(1e-2) <= 2e-2`; max diagnostic | provisional, `TODO(CI)` |
| warp (direct kernel) | `rms <= 1e-3 · range`, `p99 <= 1e-2 · range`, `frac_over(1e-2) <= 1e-2`; max diagnostic | provisional, `TODO(CI)` |
| particle, one particle per cell | `max <= 1e-5 · range`, `rms <= 2e-6 · range` | provisional, `TODO(CI)` |
| particle, many particles | `rel_total_pct <= 5`, `mean_abs <= 1e-2 · range`; per-cell metrics diagnostic | provisional, `TODO(CI)` |

Calibration rule: one deliberate mutation per algorithm must break its gate —
a rifts hash or smoothing constant (for example
`43758.5453123f → 43758.5f`), the warp persistence factor or the
downhill/uphill sign in its z-driven gradient, and the particle persistence or
step sign. Each mutation is applied once, measured, recorded below, and
reverted. Do not loosen a bound without the measured numbers and a mutation
check justifying it.

`TODO(CI): replace "provisional" with the frozen values measured by the first
full oracle run and record which cases had to be recalibrated (task 5.4).`

### Mutation check

`TODO(CI): fill after the mutation run (task 5.4).`

| mutation | measured failure | bound | result |
|---|---|---|---|
| rifts hash `43758.5453123f → 43758.5f` | TODO(CI) | `2e-3` / `2e-2` / `2e-2` | TODO(CI) |
| warp persistence `value_persistence` factor | TODO(CI) | `1e-3` / `1e-2` / `1e-2` | TODO(CI) |
| warp z-driven gradient sign (uphill → downhill) | TODO(CI) | `1e-3` / `1e-2` / `1e-2` | TODO(CI) |
| particle persistence `value_persistence` | TODO(CI) | `5%` / `1e-2 · range` | TODO(CI) |
| particle step sign (`advection_sign`) | TODO(CI) | `5%` / `1e-2 · range` | TODO(CI) |

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
   work-items that reach the same cell lose updates, so a many-particle GPU
   result is not reproducible and cannot be compared cell by cell. The
   aggregate cases therefore gate only on total mass and mean `|Δz|`, and
   `advection_particle_gpu_repeat` records the GPU-vs-GPU spread.
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

`TODO(CI): record the Node wasm cost from the built artifact` (same format as
`docs/hydraulic-parity.md`; single-threaded wasm, measured from `dist/`):

| function | grid | parameters | time |
|---|---|---|---|
| `rifts` | 256² | `kw = (4, 1.2)`, angle 30, amplitude 0.1, seed 11 | TODO(CI) |
| `rifts` | 512² | same | TODO(CI) |
| `advectionWarp` | 256² | length 0.1, persistence 0.9 | TODO(CI) |
| `advectionParticle` | 256² | 1024 particles, iterations 1 | TODO(CI) |

These are cost notes, not optimization targets: the ports are deliberately
sequential and synchronous, matching the wasm build's `-fno-openmp`
`VA_SEQUENTIAL` execution model (AD-010).
