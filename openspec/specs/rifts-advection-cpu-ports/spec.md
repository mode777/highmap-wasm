# rifts-advection-cpu-ports Specification

## Purpose

Defines the CPU implementations of the three erosion/transform functions that
upstream ships only as OpenCL kernels — `rifts`, `advection_particle` and
`advection_warp` — including their call surface, kernel-faithful semantics,
determinism, input ownership, and the conditions under which numerical parity
with the GPU kernels may be claimed.

## Requirements

### Requirement: Complete CPU rifts and advection family

The library SHALL provide CPU implementations of `rifts`, `advection_particle`
in all six overloads (single-field and multi-field, with and without
`iterations`, gradient-driven and explicit `dx`/`dy`) and `advection_warp` in
both overloads (gradient-driven and explicit gradient grids) in the non-GPU
namespace, and SHALL keep the GPU family available alongside them.

#### Scenario: Every overload runs without an OpenCL device

- **WHEN** each CPU function and overload is called on a small grid with valid
  parameters and no OpenCL platform is present
- **THEN** every call returns normally, with `rifts` modifying its terrain grid
  in place and each advection call returning a result grid

#### Scenario: GPU and CPU families coexist in one binary

- **WHEN** a program links both families and calls the GPU and the CPU form of
  the same function on identical inputs
- **THEN** both symbols resolve independently and neither call replaces or
  shadows the other

### Requirement: GPU-equivalent call surface

Each CPU function SHALL accept the same argument combinations and defaults as
its public GPU declaration: optional noise-offset and mask grids for `rifts`;
optional advection-mask and result-mask grids, multi-field collections and
explicit `dx`/`dy` velocity grids for `advection_particle`; and an optional
mask plus explicit gradient grids for `advection_warp`.

#### Scenario: Omitted optional inputs equal null pointers

- **WHEN** a function is called once with optional grids supplied and once with
  those arguments omitted
- **THEN** the results are byte-identical, because omitting an optional grid is
  equivalent to passing a null pointer

#### Scenario: Explicit velocity form matches the gradient-driven form

- **WHEN** `advection_particle` or `advection_warp` is called with explicit
  velocity grids equal to the ones its gradient-driven overload derives
- **THEN** both calls return identical output grids

#### Scenario: Multi-field form matches per-field calls

- **WHEN** `advection_particle` advects a collection of fields in one call and
  each field is also advected in its own single-field call with the same
  parameters
- **THEN** each output field is byte-identical to the corresponding
  single-field result

#### Scenario: Iterations form chains single passes

- **WHEN** `advection_particle` runs with `iterations` greater than one
- **THEN** its output equals the result of chaining that many single-pass calls
  with the same seed and parameters

### Requirement: Kernel-faithful arithmetic

Each port SHALL reproduce its kernel's constants, traversal order, float32
accumulation and boundary sampling, so CPU and GPU differ only through
floating-point rounding and effects intrinsic to the algorithms: the sin-based
rifts cell hash, warp path direction flips and non-atomic particle
accumulation.

#### Scenario: Rift cell lookup matches the kernel formula

- **WHEN** `rifts` runs on a grid with fixed `kw`, angle, amplitude and seed
- **THEN** each cell's displacement is the amplitude-scaled, smoothed and
  remapped F1/F2 noise value the kernel computes for the same coordinates

#### Scenario: Particle walk matches the kernel formula

- **WHEN** one particle walks a grid whose velocity values are exactly
  representable in float32
- **THEN** each step applies the kernel's direction normalization, inertia
  blend, value persistence and accumulation formula

#### Scenario: Warp uses the kernel's two-pass walk

- **WHEN** `advection_warp` runs
- **THEN** the upward pass records visited positions and stops on the kernel's
  out-of-domain or descending-elevation conditions, and the downward pass
  blends the recorded field values in reverse order

#### Scenario: Warp sampling is mirrored, not clamped or wrapped

- **WHEN** an advection walk samples a position outside the unit square
- **THEN** the sampled value comes from mirrored-repeat linear sampling,
  matching the kernel sampler

### Requirement: Documented kernel quirks are preserved

Kernel quirks that affect observable output SHALL be preserved rather than
corrected: the rifts coordinate rotation that subtracts the center's
components as scalars, the all-zero warp output for a zero advection length,
the warp walk cap of 1024 steps, and the particle `count + 1` normalization
with its 64-field cap.

#### Scenario: Rifts keeps the kernel's scalar center subtraction

- **WHEN** `rifts` runs with a center whose two components differ
- **THEN** the result matches the kernel's rotation convention rather than a
  component-wise center subtraction

#### Scenario: Zero advection length yields an all-zero warp

- **WHEN** `advection_warp` is called with an advection length of zero
- **THEN** the returned grid holds zero in every cell instead of a copy of the
  advected field

#### Scenario: Warp walk stops at 1024 steps

- **WHEN** the requested advection length implies more than 1024 walk steps
- **THEN** the output equals the output of the length that implies exactly 1024
  steps, because the walk is capped

#### Scenario: Particle normalization keeps the count-plus-one form

- **WHEN** a known number of particles contributes to one cell
- **THEN** the cell value is the accumulated particle values plus the field
  value, divided by the contribution count plus one and clamped to [0, 1]

#### Scenario: Particle field processing is capped at 64

- **WHEN** a particle-advection call supplies more than 64 fields
- **THEN** fields past the 64th follow the kernel's unaccumulated path instead
  of being advected like the first 64

### Requirement: Deterministic single-threaded execution

A port SHALL be deterministic: identical grid, parameters and seed SHALL
produce bit-identical output on repeated calls within a build, with no
dependence on RNG state, wall-clock time or thread scheduling, and no shared
mutable state between calls.

#### Scenario: Repeated calls are byte-identical

- **WHEN** the same function runs twice on two copies of the same input
- **THEN** both outputs are byte-for-byte identical

#### Scenario: Calls do not leak state into one another

- **WHEN** a rifts call and an advection call are made in both possible orders
  on fresh copies of the same input
- **THEN** each function's output is identical in both orders

#### Scenario: Particle accumulation follows particle index order

- **WHEN** several particles contribute to the same cell
- **THEN** their values are accumulated in ascending particle index order,
  making the CPU result reproducible where the GPU's non-atomic order is not

### Requirement: Advection inputs are not modified

The advection ports SHALL NOT modify any input grid, and `rifts` SHALL modify
only the terrain grid it is given, leaving supplied noise-offset and mask grids
untouched.

#### Scenario: Advection leaves every input untouched

- **WHEN** `advection_particle` or `advection_warp` is called with terrain,
  field, velocity and mask grids
- **THEN** every input grid is byte-identical after the call and the returned
  grid is independent of them

#### Scenario: Rifts writes only its terrain grid

- **WHEN** `rifts` is called with noise-offset and mask grids
- **THEN** the terrain grid changes and the noise-offset and mask grids are
  byte-identical after the call

### Requirement: Parity claim requires a passing GPU comparison

Parity with the GPU family SHALL be claimed only for the variants and
parameter scenarios verified against calibrated tolerance bounds on fixed
scenes, with per-variant metrics reported and the oracle choice plus any
wrapper gap recorded.

#### Scenario: Divergence fails the comparison

- **WHEN** a port's coefficient, traversal order or stopping condition is
  deliberately altered
- **THEN** the comparison for that function exceeds its bound and fails

#### Scenario: A skipped comparison is not a pass

- **WHEN** the comparison cannot run because no OpenCL device is available
- **THEN** the automated parity job reports failure rather than success

#### Scenario: Rifts is gated by distribution metrics

- **WHEN** `rifts` is compared with the GPU
- **THEN** its bounds apply to percentile error and differing-cell fraction,
  because a one-ULP hash difference can flip an entire Voronoi cell, while the
  per-cell maximum is diagnostic only

#### Scenario: Warp is gated by percentile metrics

- **WHEN** `advection_warp` is compared with the GPU
- **THEN** its bounds apply to percentile error, because a single direction
  flip redirects an entire walk

#### Scenario: Particle parity separates arithmetic from accumulation order

- **WHEN** `advection_particle` runs with one particle, or with many particles
- **THEN** the single-particle case is compared per cell and the multi-particle
  case is compared by aggregate diagnostics, because GPU accumulation is not
  atomic

#### Scenario: Broken wrappers use a direct-kernel oracle

- **WHEN** the pinned GPU wrapper cannot serve as the oracle because it never
  uploads its accumulation buffers or because the device lacks the required
  image format
- **THEN** the harness launches the kernel directly with explicitly zeroed
  buffers or falls back from `CL_R` to `CL_RGBA` images, and the parity report
  names the wrapper gap
