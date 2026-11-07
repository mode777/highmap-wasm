# hydraulic-cpu-ports Specification

## Purpose

Defines the CPU implementations of the five hydraulic erosion functions that
upstream ships only as OpenCL kernels: what each computes, how their particle,
stencil and pipe-network semantics behave, how their optional output maps are
written, and the conditions under which numerical parity with the GPU kernels
may be claimed.

## Requirements

### Requirement: Complete CPU hydraulic family

The library SHALL provide CPU implementations of `hydraulic_mcdonald`,
`hydraulic_mcdonald_multiscale`, `hydraulic_schott` (both overloads),
`hydraulic_schott_erosion` and `hydraulic_vpipes` in the non-GPU namespace, and
SHALL keep the GPU family available alongside them.

#### Scenario: Every function runs without an OpenCL device

- **WHEN** each of the five CPU functions is called on a small grid with valid
  parameters and no OpenCL platform is present
- **THEN** every call returns normally and leaves the terrain grid modified in
  place

#### Scenario: GPU and CPU families coexist in one binary

- **WHEN** a program links both families and calls the GPU and the CPU form of
  the same function on identical inputs
- **THEN** both symbols resolve independently and neither call replaces or
  shadows the other

### Requirement: GPU-equivalent call surface

Each CPU function SHALL accept the same argument combinations and defaults as
its public GPU declaration: optional sediment and discharge maps for McDonald,
a mask and flow map for Schott, a moisture map and flow map for Schott erosion,
and a rain map plus water-depth, sediment and velocity outputs for vpipes.

#### Scenario: Requested output maps are filled

- **WHEN** a function with output maps is called with non-null output arrays
- **THEN** each output array holds the corresponding result and the caller's
  terrain grid is unchanged except for the documented in-place result

#### Scenario: Omitted outputs equal null pointers

- **WHEN** the same call is made once with output maps and once without them
- **THEN** the terrain result is byte-identical, because omitting a map is
  equivalent to passing a null pointer

#### Scenario: Mask blends the untouched and fully eroded grids

- **WHEN** `hydraulic_schott` is called with a mask whose values are zero on
  part of the grid and one elsewhere
- **THEN** the result equals a per-cell blend between the input grid and the
  result of the same call without a mask

#### Scenario: Auxiliary input maps are honoured

- **WHEN** `hydraulic_schott_erosion` receives a non-constant moisture map or
  `hydraulic_vpipes` receives a non-constant rain map
- **THEN** the result differs from the same call with a constant map of the same
  mean, and the constant-map form matches the documented default

### Requirement: Kernel-faithful arithmetic

Each port SHALL reproduce its kernel's constants, traversal order, float32
accumulation and boundary sampling, so CPU and GPU differ only through
floating-point rounding and the atomic-order effects intrinsic to the McDonald
model.

#### Scenario: Single-particle McDonald step matches the kernel formula

- **WHEN** `hydraulic_mcdonald` runs one sample for one step on a small grid
  whose gradient and transfer amounts are exact in float32
- **THEN** the bed, sediment, discharge and momentum updates match the kernel's
  `mcd_grad5`, transfer, debris and filter formulas

#### Scenario: Schott phase cycle matches the kernel

- **WHEN** `hydraulic_schott` runs across `it % 10` boundaries
- **THEN** the erosion, thermal and deposition phases run in the same order and
  with the same iteration counts as the kernel

#### Scenario: Vpipes pass sequence matches the kernel

- **WHEN** `hydraulic_vpipes` runs one iteration
- **THEN** the flux, water, erosion and sediment-transport passes run in kernel
  order, the flux normalization uses the kernel's `sum > 1e-5` rule, and
  sediment is advected with clamp-to-edge bilinear sampling

#### Scenario: Border sampling is clamped, not wrapped

- **WHEN** a Schott or vpipes pass samples a neighbour outside the grid
- **THEN** the nearest edge value is used, matching the kernels' clamp-to-edge
  sampler

### Requirement: Deterministic single-threaded execution

A port SHALL be deterministic: identical grid, parameters and iteration count
SHALL produce bit-identical output on repeated calls within a build, with no
dependence on RNG state, wall-clock time or thread scheduling, and no shared
mutable state between calls.

#### Scenario: Repeated calls are byte-identical

- **WHEN** the same function runs twice on two copies of the same input
- **THEN** both outputs are byte-for-byte identical

#### Scenario: Calls do not leak state into one another

- **WHEN** two different hydraulic functions are called in both possible orders
  on fresh copies of the same input
- **THEN** each function's output is identical in both orders

#### Scenario: McDonald uses a fixed sample order

- **WHEN** `hydraulic_mcdonald` processes its samples
- **THEN** the samples are visited in a fixed index order, making the CPU result
  reproducible even though the GPU's atomic order is not

### Requirement: Output maps are written only when requested

The ports SHALL write each output map only when its pointer is non-null, SHALL
give every written map the terrain grid's shape, and SHALL NOT modify any
caller array other than the terrain grid and the requested outputs.

#### Scenario: Unrequested maps are left alone

- **WHEN** a function is called with one output map requested and the others
  null
- **THEN** only the requested map is written, and the caller's other arrays are
  untouched

#### Scenario: Written maps have the grid shape

- **WHEN** any output map is requested
- **THEN** its shape equals the terrain grid's shape after the call

#### Scenario: Output maps are finite and deterministic

- **WHEN** a function runs twice with sane parameters and the same output maps
- **THEN** each map is byte-identical between runs and contains no NaN

### Requirement: Parity claim requires a passing GPU comparison

Parity with the GPU family SHALL be claimed only for the variants and parameter
scenarios that a GPU-versus-CPU comparison has verified against calibrated
tolerance bounds, using fixed scenes and parameters, with per-variant error
metrics reported.

#### Scenario: Divergence fails the comparison

- **WHEN** a port's coefficient, traversal order or branch is deliberately
  altered
- **THEN** the comparison for that function exceeds its bound and fails

#### Scenario: A skipped comparison is not a pass

- **WHEN** the comparison cannot run because no OpenCL device is available
- **THEN** the automated parity job reports failure rather than success

#### Scenario: Broken wrappers use a direct-kernel oracle

- **WHEN** the pinned GPU wrapper cannot launch its kernel because an argument
  is missing or a kernel name is stale
- **THEN** the parity harness launches the kernel directly with the correct
  arguments and the parity report names the wrapper gap

#### Scenario: McDonald parity separates arithmetic from atomic order

- **WHEN** `hydraulic_mcdonald` runs with one sample, or with many samples
- **THEN** the single-sample case is compared per cell and the multi-sample case
  is compared by aggregate metrics, because GPU atomic ordering is not
  reproducible
