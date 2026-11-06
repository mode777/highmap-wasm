# Thermal GPU/CPU parity

The thermal erosion family was OpenCL-only upstream. Nine variants were ported
to CPU in `HighMap/src/erosion/thermal_cpu.cpp` by transcribing
`src/gpu_opencl/kernels/thermal*.cl` (AD-017); the tenth, `thermal_schott`, uses
the upstream CPU implementation. This document records how the CPU results are
compared with the GPU oracle, what the measured agreement is, and which
comparisons the oracle cannot make.

## Oracle and harness

- **Oracle**: the desktop OpenCL family (`hmap::gpu::thermal*`) built from the
  same tree as the CPU family, so both sides share compiler flags and input
  generation.
- **Harness**: `tests/src/test_thermal_cpu.cpp`, part of the upstream
  `highmap_tests` binary (picked up by its `src/*.cpp` glob).
  - `ThermalCpuInvariants.*` — CPU-only properties (borders, determinism,
    masks, deposition, mass conservation). No OpenCL needed.
  - `ThermalParity.*` — the GPU-vs-CPU comparison.
- **Scenes**: seeded Perlin fBm 128², a synthetic ramp with a plateau, and a
  single spike. **Parameters**: talus `0.1/ni` and `2/ni`, 1 and 10 iterations
  (`thermal_rib`: 1 and 9, see below), `rate` 0.5, `sigma_inf` 0.5.
- **Metrics**: per case, `max |cpu - gpu|` and `rms |cpu - gpu|`, normalised by
  the input elevation range; printed as `PARITY,...` CSV lines and recorded as
  gtest properties.
- **Gate**: every parity case must stay under its bound, and a *skipped* case
  fails the job. The only allowed skip is the image-support limitation below.

### Running it

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-std=gnu17 \
  -DHIGHMAP_ENABLE_EXAMPLES=OFF -DHIGHMAP_ENABLE_BENCHMARKS=OFF \
  -DHIGHMAP_ENABLE_TESTS=ON
cmake --build build --target highmap_tests --parallel 4
./build/bin/highmap_tests --gtest_filter='ThermalCpuInvariants.*:ThermalParity.*' \
  --gtest_output=xml:build/thermal-parity.xml
```

`-std=gnu17` is required on toolchains that default C to C23: the vendored
`nn-c` (Triangle) code uses K&R definitions that C23 rejects. `--parallel 4`
keeps the build inside the runner's memory; an unbounded `--parallel` spawns
`gmake -j` (unlimited) and gets killed.

The `thermal-parity` GitHub workflow does the same on `ubuntu-latest` with
PoCL. It also accepts a `no_device` dispatch input that points
`OCL_ICD_VENDORS` at a missing path, which exists to prove that a device-less
run fails instead of passing silently.

## Measured agreement

CI run of 2026-11-06 (`thermal-parity`), oracle device
`cpu-haswell-AMD EPYC 9V74 80-Core Processor`, `OpenCL 3.0 PoCL HSTR`,
Release build, 15 parity cases per variant (6 for `thermal_rib`):

| variant | cases | worst max | worst rms | bound max | bound rms |
|---|---|---|---|---|---|
| `thermal` | 12 | 3.6e-7 | 4.7e-8 | 1e-5 | 2e-6 |
| `thermal_auto_bedrock` | 12 | 2.4e-7 | 1.1e-7 | 1e-5 | 2e-6 |
| `thermal_flatten` | 12 | 4.0e-3 | 3.9e-5 | 2e-2 | 2e-4 |
| `thermal_inflate` | 12 | 2.1e-7 | 1.1e-8 | 1e-5 | 2e-6 |
| `thermal_olsen` | 12 | 0 | 0 | 2e-2 | 2e-4 |
| `thermal_rib` | 6 | 9.2e-7 | 9.6e-9 | 2e-2 | 2e-4 |
| `thermal_ridge` | 12 | 6.0e-8 | 5.0e-9 | 1e-5 | 2e-6 |
| `thermal_scree` | 12 | 2.1e-7 | 9.1e-9 | 1e-5 | 2e-6 |
| `thermal_conserve` | 0 | — | — | 1e-5 | 2e-6 |

All values are fractions of the input range. `thermal_olsen` is bit-exact on
this oracle; `thermal_flatten` is the only variant where an argmax flip moves
material to a different neighbour, which is why it needs the wider
single-cell bound.

`thermal_conserve` could not be compared on this oracle (see below). Its CPU
implementation is covered by the invariants: closed boundary preserved exactly,
total elevation conserved to float32 precision, `rate = 0` is a no-op.

## Bounds and calibration

- Continuous variants (`thermal`, `thermal_auto_bedrock`, `thermal_conserve`,
  `thermal_inflate`, `thermal_ridge`, `thermal_scree`): `max <= 1e-5 · range`,
  `rms <= 2e-6 · range` — about 25x and 20x the measured worst case.
- Selection-based variants (`thermal_flatten`, `thermal_olsen`, `thermal_rib`):
  `max <= 2e-2 · range`, `rms <= 2e-4 · range` — about 5x the measured worst
  case.

Calibration rule and result (2026-11-06): mutating one coefficient per family
must break its bound while everything else stays green.

| mutation | worst max | bound | result |
|---|---|---|---|
| `thermal` exchange rate 0.2 → 0.25 | 0.76 | 1e-5 | failed as required |
| `thermal_olsen` redistribution 0.25 → 0.30 | 0.049 | 2e-2 | failed as required |

`thermal_auto_bedrock` failed as well (it shares the exchange helper); the other
15 cases stayed green, confirming the bounds are not over-tight. Mutating
`thermal_olsen`'s position-hash rotation instead would only reorder float
additions (rounding-level, ~1e-7) and is not detectable by any meaningful
bound — a coefficient mutation is the right probe.

## Known oracle gaps

1. **`thermal_conserve` needs float image support.** The GPU path is the only
   image2d-based variant (`CL_R`/`CL_FLOAT`). The CI PoCL device advertises image
   support but rejects that format (`CL_INVALID_OPERATION`), so the harness
   probes by creating the same 1x1 image and skips the case with a "no float
   image support" message. The CI guard accepts *only* that skip; any other skip
   (notably a missing device) fails the job. Re-run the suite on a device with
   `CL_R`/`CL_FLOAT` support to close the gap; no code change is required.
2. **`thermal_rib` is compared at odd iteration counts only.** The upstream
   launch reads the wrong buffer back for even counts
   (`src/erosion/thermal_gpu.cpp` uses `run_ba.read_buffer("z_in")`, which
   downloads the stale *input* buffer), so `gpu::thermal_rib(z, 10)` returns the
   padded input rather than the result. Odd counts exercise the same kernel and
   agree to float rounding, which is what the port must be compared against.
   This is an upstream launch bug in the GPU wrapper; the fork does not edit
   upstream sources (see AD-017), so it is recorded here as a candidate
   upstream issue.
3. **`thermal_schott` is not compared.** It is the upstream CPU implementation,
   bound as-is. It extrapolates the borders after **every** iteration, while the
   GPU wrapper extrapolates once after the loop, so the two disagree in a
   border band; that is an upstream inconsistency, not a port artifact.

## Cost

Single-threaded wasm, `thermal`, Windows 11 / Intel Core Ultra 5 335 / Node
26.13, measured from the built artifact:

| grid | iterations | time |
|---|---|---|
| 256² | 10 | 17 ms |
| 512² | 10 | 46 ms |
| 1024² | 10 | 141 ms |
| 512² | 500 | 1.55 s |
| 1024² | 500 | 15.8 s |

Cost is linear in cells and iterations; the other variants are within the same
order (each cell visits eight neighbours per iteration).
