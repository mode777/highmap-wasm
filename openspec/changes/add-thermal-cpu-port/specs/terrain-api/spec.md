# Spec Delta

## ADDED Requirements

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
