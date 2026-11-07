# terrain-api Specification

## Purpose

Defines the high-level JavaScript function surface for terrain generation —
module-level camelCase functions and enum constants modeled after the
pyHighMap Python bindings — including its seed scope, parameter conventions,
determinism, and growth contract.

## Requirements

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

### Requirement: Thermal erosion function family

The API SHALL expose `thermal`, `thermalAutoBedrock`, `thermalConserve`,
`thermalFlatten`, `thermalInflate`, `thermalOlsen`, `thermalRib`,
`thermalRidge`, `thermalScree` and `thermalSchott` as camelCase functions that
accept a grid, mutate it in place and return it, matching the in-place
conventions of the existing erosion functions.

#### Scenario: HArray input is eroded in place

- **WHEN** any thermal function is called with an `HArray`
- **THEN** the returned grid is that same wrapper, holding the eroded values,
  and the shape is unchanged

#### Scenario: Plain grid input leaves the caller's data untouched

- **WHEN** a thermal function is called with a plain `{ data, shape }` object
- **THEN** a new `HArray` is returned and the caller's `Float32Array` still
  holds its original values

#### Scenario: Every function reaches the wasm implementation

- **WHEN** the ten functions are enumerated on the API object
- **THEN** each is a function, and each succeeds on a small grid

### Requirement: Talus accepted as a number or a grid

The thermal functions that take a talus limit SHALL accept either a number,
meaning a uniform limit across the grid, or a grid of per-cell limits. A number
SHALL be exactly equivalent to a grid filled with that value.

#### Scenario: Scalar and constant-grid talus agree

- **WHEN** the same function is called once with `talus: 0.02` and once with a
  grid uniformly holding `0.02`
- **THEN** both calls return identical values

#### Scenario: Per-cell talus is honoured

- **WHEN** a grid with two different talus regions is used
- **THEN** the region with the larger talus limit erodes less than the region
  with the smaller one, all else equal

### Requirement: Thermal option defaults

The thermal functions SHALL take their optional parameters as a trailing
options object: `iterations` defaulting to 10 for every function (including the
variants whose C++ overload declares no default), `rate` 0.5 for conserve,
`sigmaInf` 0.5 and `sigmaSup` 0 for flatten, and `intensity` 0.2 for Schott.

#### Scenario: Omitted options equal explicit defaults

- **WHEN** a thermal function is called once without an options object and once
  with every default spelled out
- **THEN** both calls return identical grids

#### Scenario: Zero iterations only applies the border pass

- **WHEN** `thermal` is called with `iterations: 0`
- **THEN** the interior keeps its input values and only the one-cell border is
  extrapolated, mirroring the C++ entry point

### Requirement: Thermal outputs obey the marshalling contract

Thermal functions SHALL follow the same argument validation, shape reporting and
determinism rules as the rest of the surface; with sane parameters (positive
talus, positive scree ceiling, non-negative iterations) their output SHALL be
finite.

#### Scenario: Mismatched auxiliary grid is rejected with context

- **WHEN** a thermal function receives a talus grid whose shape differs from the
  erode grid
- **THEN** a package error is thrown naming the offending argument before any
  wasm call

#### Scenario: Repeated runs are byte-identical

- **WHEN** any thermal function runs twice on two copies of the same input with
  the same options
- **THEN** both outputs are byte-for-byte identical and contain no NaN

### Requirement: Hydraulic erosion function family

The API SHALL expose `hydraulicMcdonald`, `hydraulicMcdonaldMultiscale`,
`hydraulicSchott`, `hydraulicSchottErosion` and `hydraulicVpipes` as camelCase
functions that accept a grid, mutate it in place and return it, matching the
in-place conventions of the existing erosion functions.

#### Scenario: HArray input is eroded in place

- **WHEN** any hydraulic function is called with an `HArray`
- **THEN** the returned grid is that same wrapper, holding the eroded values,
  and the shape is unchanged

#### Scenario: Plain grid input leaves the caller's data untouched

- **WHEN** a hydraulic function is called with a plain `{ data, shape }` object
- **THEN** a new `HArray` is returned and the caller's `Float32Array` still
  holds its original values

#### Scenario: Every function reaches the wasm implementation

- **WHEN** the five functions are enumerated on the API object
- **THEN** each is a function, and each succeeds on a small grid with valid
  arguments

### Requirement: Hydraulic output maps

The hydraulic functions SHALL accept optional output `HArray`s in their options
object — `sediment` and `discharge` for McDonald, `flow` for Schott,
`waterDepth`, `sediment`, `velU` and `velV` for vpipes — and SHALL validate
shape, liveness and aliasing before the wasm call.

#### Scenario: Requested maps are filled

- **WHEN** a hydraulic function is called with one or more output maps
- **THEN** each provided map holds the corresponding result and the returned
  terrain grid is the same wrapper as the in-place input

#### Scenario: Mismatched output map is rejected with context

- **WHEN** an output map's shape differs from the terrain grid
- **THEN** a package error is thrown naming the offending option before any
  wasm call

#### Scenario: Aliased output map is rejected

- **WHEN** an output map is the same `HArray` as the terrain grid or as another
  output map
- **THEN** a package error is thrown before any wasm call, so the terrain input
  cannot be overwritten by an output

#### Scenario: Omitted maps are not allocated

- **WHEN** a hydraulic function is called without output maps
- **THEN** no output `HArray` is created and the terrain result is identical to
  the same call with maps that are subsequently ignored

### Requirement: Hydraulic required arguments and defaults

The API SHALL require `steps` and `seed` for `hydraulicMcdonald`, `seed` for
`hydraulicMcdonaldMultiscale` and `talus` for the Schott functions; optional
parameters SHALL be a trailing options object whose defaults mirror the C++
declarations, with `iterations` defaulting to 10 where the C++ declaration has
no default.

#### Scenario: Omitted options equal explicit defaults

- **WHEN** a hydraulic function is called once without an options object and
  once with every default spelled out
- **THEN** both calls return identical grids

#### Scenario: Missing required argument is rejected

- **WHEN** `hydraulicMcdonald` is called without `steps` or `seed`, or a Schott
  function is called without `talus`
- **THEN** a package error is thrown naming the missing argument before any
  wasm call

#### Scenario: Scalar and constant-grid talus agree

- **WHEN** a Schott function is called once with `talus: 0.02` and once with a
  grid uniformly holding `0.02`
- **THEN** both calls return identical values

### Requirement: Hydraulic outputs obey the marshalling contract

Hydraulic functions SHALL follow the same argument validation, shape reporting
and determinism rules as the rest of the surface; with sane parameters their
terrain result and requested output maps SHALL be finite.

#### Scenario: Repeated runs are byte-identical

- **WHEN** any hydraulic function runs twice on two copies of the same input
  with the same options
- **THEN** both terrain results and both sets of output maps are byte-for-byte
  identical

#### Scenario: Mismatched auxiliary grid is rejected with context

- **WHEN** a Schott function receives a talus grid whose shape differs from the
  erode grid
- **THEN** a package error is thrown naming the offending argument before any
  wasm call

#### Scenario: Output maps are HArrays with the grid shape

- **WHEN** any output map is provided
- **THEN** it is an `HArray`, remains alive after the call, and reports the same
  shape as the terrain grid
