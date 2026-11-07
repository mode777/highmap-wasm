# Spec Delta

## ADDED Requirements

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
