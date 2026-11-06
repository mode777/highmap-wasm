# Hydraulic GPU/CPU parity

The hydraulic erosion family was OpenCL-only upstream. Five functions were
ported to CPU in `HighMap/src/erosion/hydraulic_cpu.cpp` by transcribing
`src/gpu_opencl/kernels/hydraulic_{mcdonald,schott,vpipes}.cl` (AD-022). This
document records how the CPU results are compared with the GPU oracle, what
the measured agreement is, and which comparisons the pinned wrappers cannot
make. Porting lessons are in `docs/porting-learnings.md`.

## Oracle and harness

- **Oracle**: the desktop OpenCL kernels built from the same tree as the CPU
  ports, so both sides share compiler flags and input generation.
- **Harness**: `tests/src/test_hydraulic_cpu.cpp`, part of the upstream
  `highmap_tests` binary (picked up by its `src/*.cpp` glob).
  - `HydraulicCpuInvariants.*` — CPU-only properties (determinism, output-map
    shapes, zero-iteration no-ops, Schott mask blend, vpipes option effects).
    No OpenCL needed.
  - `HydraulicParity.*` — the GPU-vs-CPU comparison.
- **McDonald**: the working `hmap::gpu::hydraulic_mcdonald` wrapper is used for
  a single-sample per-cell case (arithmetic fidelity) and a many-sample
  aggregate case (atomic-order behaviour).
- **Schott**: a test-local direct-kernel reference binds the kernel's `it`
  argument, which the pinned wrapper never binds. Cases isolate erosion-only,
  erosion + thermal, and the default mix (which reaches deposition).
- **Vpipes**: a test-local direct-kernel reference runs the four pass kernels
  in the fixed dev-branch order, because the pinned wrapper launches kernel
  names that no longer exist.
- **Scenes**: seeded Perlin fBm 32² and a synthetic ramp 32².
- **Metrics**: per case, `max |cpu - gpu|` and `rms |cpu - gpu|`, normalised by
  the input range; printed as `PARITY,...` CSV lines and recorded as gtest
  properties. The McDonald aggregate case additionally prints
  `PARITY_AGG,...` with total-sediment, total-discharge and mean-`|Δz|` metrics.
- **Gate**: every parity case must stay under its bound, and a *skipped* case
  fails the job except for the recorded float-image-format limitation. The job
  checks `clinfo -l` first: upstream's `tests/src/main.cpp` calls
  `gpu::init_opencl()` before gtest and aborts when no platform exists, so a
  device-less runner is rejected up front with a clear message.

### Running it

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-std=gnu17 \
  -DHIGHMAP_ENABLE_EXAMPLES=OFF -DHIGHMAP_ENABLE_BENCHMARKS=OFF \
  -DHIGHMAP_ENABLE_TESTS=ON
cmake --build build --target highmap_tests --parallel 4
./build/bin/highmap_tests \
  --gtest_filter='HydraulicCpuInvariants.*:HydraulicParity.*' \
  --gtest_output=xml:build/hydraulic-parity.xml
```

`-std=gnu17` is required on toolchains that default C to C23: the vendored
`nn-c` (Triangle) code uses K&R definitions that C23 rejects. `--parallel 4`
keeps the build inside the runner's memory.

The `erosion-parity` GitHub workflow runs this suite together with the thermal
suite on `ubuntu-latest` with PoCL. It also accepts a `no_device` dispatch input
that points `OCL_ICD_VENDORS` at a missing path, which proves that a device-less
run fails instead of passing silently.

## Measured agreement

CI run of 2026-11-06 (`erosion-parity`), oracle device
`cpu-haswell-AMD EPYC 9V74 80-Core Processor`, `OpenCL 3.0 PoCL HSTR`,
Release build, 2 scenes (fBm 32², ramp 32²):

| variant | worst max | worst rms | bound max | bound rms |
|---|---|---|---|---|
| `hydraulic_mcdonald_single` | 0 | 0 | 1e-5 | 2e-6 |
| `hydraulic_mcdonald_sediment` | 0 | 0 | 1e-5 | 2e-6 |
| `hydraulic_mcdonald_discharge` | 2.5e-7 | 1.9e-8 | 1e-5 | 2e-6 |
| `hydraulic_mcdonald_aggregate` | 0.27% rel. sed. | 0.11% rel. dis. | 5% | 5% |
| `schott_erosion_only` | 6.0e-8 | 1.5e-8 | 2e-2 | 2e-4 |
| `schott_erosion_thermal` | 6.0e-8 | 2.1e-8 | 2e-2 | 2e-4 |
| `schott_default_mix` | 6.0e-8 | 1.5e-8 | 2e-2 | 2e-4 |
| `hydraulic_schott_erosion` | 6.0e-8 | 6.8e-9 | 2e-2 | 2e-4 |
| `vpipes_default_1` | 3.0e-8 | 1.4e-9 | 1e-5 | 2e-6 |
| `vpipes_default_2` | 6.0e-8 | 2.8e-9 | 1e-5 | 2e-6 |
| `vpipes_no_flux_diffusion` | 3.0e-8 | 1.4e-9 | 1e-5 | 2e-6 |
| `vpipes_no_maintain` | 3.0e-8 | 1.5e-9 | 1e-5 | 2e-6 |
| `vpipes_no_evap` | 3.0e-8 | 3.0e-9 | 1e-5 | 2e-6 |

All values are fractions of the input range unless marked otherwise. Each
vpipes case also compares `waterDepth`, `sediment`, `velU` and `velV`; the
worst sediment error was `2.2e-7` absolute (`9.2e-5` of its range) and the
worst velocity error was `3.1e-4` absolute (`~3e-6` of its range). The sediment
bound is therefore `5e-4`/`5e-5`, as noted below.

The McDonald aggregate row reports total-sediment and total-discharge relative
differences; its gating `mean |Δz|` was `4.3e-5` (bound `1e-2`). Per-cell
`max`/`rms` for that case are `5.4e-3`/`2.8e-4` of range and are diagnostic
only, because GPU atomic order is not reproducible. The aggregate varies
slightly run to run: the first oracle run measured `0.42%`/`0.13%` and
`mean |Δz| = 5.6e-5`, still far inside the bounds.

## Bounds and calibration

Frozen bounds (2026-11-06, from the first full oracle run):

- Continuous passes (McDonald single-particle, vpipes terrain/depth/velocity):
  `max <= 1e-5 · range`, `rms <= 2e-6 · range`.
- Vpipes sediment (a small residual field with cancellation):
  `max <= 5e-4 · range`, `rms <= 5e-5 · range` — about 5x / 16x the measured
  worst case.
- Selection-based Schott passes (steepest-neighbour argmax and discrete
  up/down counts can flip): `max <= 2e-2 · range`, `rms <= 2e-4 · range`.
- McDonald multi-particle aggregates: `rel_sediment <= 5%`,
  `rel_discharge <= 5%`, `mean |Δz| <= 1e-2 · range`.

Calibration rule: one deliberate mutation per algorithm must break its bound —
a McDonald transfer coefficient, a Schott erosion/phase coefficient, and a
vpipes capacity coefficient. The mutation is applied once, recorded, and
reverted. Do not loosen a bound without the measured numbers and a mutation
check justifying it.

## Known oracle gaps

1. **`hydraulic_schott` wrapper does not bind `it`.** The pinned
   `src/erosion/hydraulic_schott_gpu.cpp` binds every kernel argument except the
   final `it`, so the phase cycle cannot run through the wrapper. The harness
   launches the kernel directly and binds `it`.
2. **`hydraulic_vpipes` wrapper launches stale kernel names.** The pinned
   `src/erosion/hydraulic_vpipes_gpu.cpp` launches
   `hydraulic_vpipes_flow_simulation`, `_velocity`, `_erosion_deposition` and
   `_advection`, which the refactored `hydraulic_vpipes.cl` no longer defines.
   The harness launches the four pass kernels directly in the fixed dev-branch
   order.
3. **`hydraulic_schott` mask overload is declared with a different parameter
   order than its definition.** The CPU port follows the public header order;
   the JS surface defers masks; the mask blend is covered as a CPU invariant.
4. **Float image format.** The pinned CLWrapper binds `CL_R`/`CL_FLOAT` images.
   The CI PoCL device rejects that format for `thermal_conserve`, so the
   hydraulic harness probes `CL_R` first and falls back to `CL_RGBA`/`CL_FLOAT`
   through raw `cl::Image2D` bindings. The kernels read `.x`, so the fallback is
   behaviorally equivalent. If neither format works, Schott/vpipes cases skip
   with the explicit "no float image support" message, which the CI guard
   allows and this report records.
5. **McDonald atomic order.** The GPU kernel uses atomic adds and is
   reproducible only up to atomic scheduling order. The CPU port processes
   samples in a fixed index order, so it is deterministic but cannot match an
   arbitrary GPU order per cell. The single-sample case is per-cell; the
   many-sample case is aggregate.

The three wrapper gaps are candidate upstream issues. The fork does not edit
the pinned wrappers (AD-022/AD-024).

## Cost

Single-threaded wasm, Windows 11 / Intel Core Ultra 5 335 / Node 26.13,
measured from the built artifact:

| function | grid | parameters | time |
|---|---|---|---|
| `hydraulicMcdonald` | 256² | 4 steps, 1024 samples, maxage 64 | 68 ms |
| `hydraulicMcdonald` | 512² | 2 steps, 1024 samples, maxage 64 | 44 ms |
| `hydraulicSchott` | 256² | 10 iterations | 268 ms |
| `hydraulicSchott` | 512² | 10 iterations | 884 ms |
| `hydraulicVpipes` | 256² | 10 iterations | 83 ms |
| `hydraulicVpipes` | 512² | 10 iterations | 204 ms |

McDonald's cost is dominated by `steps × samples × maxage`, not by grid size;
the 512² row is cheaper because it runs half as many steps. Schott is a
per-cell stencil with several neighbour-table recomputations; vpipes keeps
roughly ten full-grid state arrays. These are cost notes, not optimization
targets: the ports are deliberately sequential and synchronous.
