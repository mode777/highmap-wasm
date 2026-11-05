# Spec Delta

## Purpose

Defines the high-level JavaScript function surface for terrain generation —
module-level camelCase functions and enum constants modeled after the
pyHighMap Python bindings — including its seed scope, parameter conventions,
determinism, and growth contract.

## ADDED Requirements

### Requirement: pyHighMap-parity seed functions

The API SHALL expose camelCase equivalents of every function in the existing
pyHighMap binding: `noise`, `noiseFbm`, `noisePingpong`, `noiseRidged`,
`warp`, `falloff`, `clamp`, `remap`, `plateau`, `smoothFill`, and
`hillshade`, with semantics matching the underlying C++ functions.

#### Scenario: pyHighMap example translates directly

- **WHEN** the pyHighMap README noise example is translated to JS
  (`noise` for the base, `noiseFbm` for fractal layering, `warp` with two
  displacement grids)
- **THEN** the calls succeed and return grids of the requested shape

### Requirement: Array-algebra function layer

The API SHALL expose element-wise algebra functions sufficient to compose
grids without operator overloading: at minimum `add`, `subtract`,
`multiply`, and `divide` (grid-grid and grid-scalar) and `scale`; results
SHALL follow the standard marshalling contract.

#### Scenario: Scalar scaling of a generated grid

- **WHEN** `scale` is applied to a noise grid with factor `0.1`
- **THEN** the returned grid has the same shape and each value is the
  original value multiplied by `0.1`

#### Scenario: Grid-grid arithmetic

- **WHEN** `add` is applied to two grids of equal shape
- **THEN** each output value is the sum of the corresponding input values

### Requirement: CPU erosion functions in the seed surface

The API SHALL expose at least the hydraulic erosion functions that already
have CPU implementations in the C++ library (notably the Musgrave and
stream-erosion variants), each accepting a grid plus parameters and
returning an eroded grid.

#### Scenario: Erosion modifies the grid deterministically

- **WHEN** a CPU erosion function runs on a generated grid with fixed
  parameters and seed
- **THEN** it returns a grid of identical shape whose values are
  deterministically derived from the input

### Requirement: Enum constants

The API object SHALL expose `NoiseType` and `DistanceFunction` enum
constants covering the values exposed by pyHighMap, usable directly as
function arguments.

#### Scenario: Enum argument selects noise flavor

- **WHEN** `noise` is called with `NoiseType.PERLIN` versus
  `NoiseType.SIMPLEX2S` and otherwise identical arguments
- **THEN** both calls succeed and produce different grids

### Requirement: Optional parameters via options object

Functions with optional parameters SHALL take them as a trailing options
object whose defaults match the pyHighMap binding defaults; omitting the
object SHALL be equivalent to passing it with all defaults.

#### Scenario: Omitted options equal explicit defaults

- **WHEN** `noiseFbm` is called twice — once without an options object and
  once with the documented default values spelled out
- **THEN** both calls return identical grids

### Requirement: Deterministic generation

Generation functions SHALL be deterministic: the same function, arguments,
and seed SHALL produce identical output values across calls within an
instance and across environments.

#### Scenario: Same seed, same bytes

- **WHEN** a generation function is invoked twice with identical arguments
- **THEN** the returned grids contain identical values

### Requirement: Backward-compatible surface growth

The seed surface (~30 functions) is the first increment of a broader curated
API; as the surface grows by function family, existing function signatures
MUST remain backward compatible within the same major version.

#### Scenario: Growth does not break existing consumers

- **WHEN** new function families are added in later releases
- **THEN** code written against the seed surface continues to work unchanged
