# thermal-cpu-ports Specification

## Purpose

Defines the CPU implementations of the nine thermal erosion variants that
upstream ships only as OpenCL kernels: what each computes, how borders and
iteration counts behave, and the conditions under which numerical parity with
the GPU family may be claimed.

## Requirements

### Requirement: Complete CPU thermal family

The library SHALL provide CPU implementations of `thermal`,
`thermal_auto_bedrock`, `thermal_conserve`, `thermal_flatten`,
`thermal_inflate`, `thermal_olsen`, `thermal_rib`, `thermal_ridge` and
`thermal_scree` in the non-GPU namespace, and SHALL make the existing upstream
CPU `thermal_schott` reachable through the same declaration surface.

#### Scenario: Every variant runs without an OpenCL device

- **WHEN** the library is built on a machine with no OpenCL platform and each
  of the ten thermal functions is called on a small grid
- **THEN** every call returns normally and leaves the grid modified in place

#### Scenario: GPU and CPU families coexist in one binary

- **WHEN** a program links both families and calls the GPU and the CPU form of
  the same variant on identical inputs
- **THEN** both symbols resolve independently and neither call replaces or
  shadows the other

### Requirement: GPU-equivalent call surface

Each CPU variant SHALL accept the same argument combinations as its GPU
counterpart: a per-cell talus array or a constant talus, an optional intensity
mask, an optional bedrock lower limit, and the deposition output where the GPU
variant exposes one.

#### Scenario: Constant talus equals a constant talus grid

- **WHEN** a variant is called with a scalar talus and with a grid of that same
  constant value, all else equal
- **THEN** both calls produce identical grids

#### Scenario: Mask blends the untouched and fully eroded grids

- **WHEN** a variant is called with a mask whose values are zero on part of the
  grid and one elsewhere
- **THEN** the result equals a per-cell blend between the input grid and the
  result of the same call without a mask

#### Scenario: Deposition output reports where material was added

- **WHEN** a variant that exposes a deposition output is called on terrain that
  gains material
- **THEN** the deposition grid holds the per-cell positive elevation gain (or
  the absolute change for the ridge and Schott variants) and never modifies the
  caller's input grid

### Requirement: Kernel-faithful arithmetic

Each variant SHALL reproduce its OpenCL kernel's neighbour tables, distance
weights, coefficients, branch structure and float32 accumulation order, so
that CPU and GPU results differ only through floating-point rounding and never
through a different algorithm.

#### Scenario: Single-iteration exchange matches the hand-computed kernel result

- **WHEN** `thermal` runs one iteration on a small grid whose neighbour slopes
  and talus limits are chosen so the exchange amounts are exact in float32
- **THEN** every cell equals the kernel formula `z + sum over 8 neighbours of
  the talus-capped exchange divided by the neighbour distance`, using the
  kernel's 0.2 rate

#### Scenario: Mass-preserving variant conserves total elevation

- **WHEN** `thermal_conserve` runs several iterations on a closed grid
- **THEN** the sum of elevations stays unchanged within float32 tolerance

#### Scenario: Rib lowers by the shallowest neighbouring slope

- **WHEN** `thermal_rib` runs one iteration on a grid with distinct known
  slopes
- **THEN** each interior cell is lowered by the smallest absolute neighbour
  slope divided by its distance weight

#### Scenario: Auto-bedrock never erodes below the initial surface

- **WHEN** `thermal_auto_bedrock` runs any number of iterations
- **THEN** no cell ends below its starting elevation (the kernel tracks the
  highest level reached and clamps the cell to it, so the variant deposits
  material rather than eroding it away)

### Requirement: Per-variant border treatment

During iterations each variant SHALL apply its kernel's border rule — a
one-cell frozen border, a two-cell band for `thermal_flatten`, a three-cell band
for `thermal_olsen`, or a fully closed boundary for `thermal_conserve` — and
SHALL finish with the same border extrapolation as the GPU launch code (none for
`thermal_conserve`, one cell for most variants, three cells for `thermal_rib`).

#### Scenario: Conserve never touches its boundary

- **WHEN** `thermal_conserve` runs on a grid with known border values
- **THEN** every border cell keeps its input value exactly, with no
  extrapolation applied afterwards

#### Scenario: Zero iterations still applies the final border pass

- **WHEN** `thermal` is called with zero iterations on a non-flat grid
- **THEN** the interior is unchanged and the one-cell border holds the linear
  extrapolation of its inward neighbours

#### Scenario: Wide-border variants clamp their band to the interior

- **WHEN** `thermal_flatten` or `thermal_olsen` runs one iteration
- **THEN** the cells in the variant's border band equal the nearest interior
  cell of the same iteration, matching the kernel's clamp behaviour

### Requirement: Deterministic single-threaded execution

A variant SHALL be deterministic: identical grid, parameters and iteration count
SHALL produce bit-identical output on repeated calls within a build, with no
dependence on RNG state, wall-clock time, or thread scheduling, and no shared
mutable state between calls.

#### Scenario: Repeated calls are byte-identical

- **WHEN** the same variant runs twice on two copies of the same input
- **THEN** both outputs are byte-for-byte identical

#### Scenario: Calls do not leak state into one another

- **WHEN** two different variants are called in both possible orders on fresh
  copies of the same input
- **THEN** each variant's output is identical in both orders

### Requirement: Parity claim requires a passing GPU comparison

Parity with the GPU family SHALL be claimed only for the variants and parameter
scenarios that a GPU-versus-CPU comparison has verified against calibrated
tolerance bounds, using fixed scenes and parameters, with per-variant error
metrics reported.

#### Scenario: Divergence fails the comparison

- **WHEN** a port's coefficient, neighbour table or branch is deliberately
  altered
- **THEN** the comparison for that variant exceeds its bound and fails

#### Scenario: A skipped comparison is not a pass

- **WHEN** the comparison cannot run because no OpenCL device is available
- **THEN** the automated parity job reports failure rather than success
