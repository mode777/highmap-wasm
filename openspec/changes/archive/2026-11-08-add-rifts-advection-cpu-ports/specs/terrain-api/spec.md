# Spec Delta

## ADDED Requirements

### Requirement: Rifts erosion function

The API SHALL expose `rifts` as a camelCase function that accepts a grid plus
the required `kw` frequency pair, `angle`, `amplitude` and `seed`, mutates the
grid in place and returns it, matching the in-place conventions of the erosion
family.

#### Scenario: HArray input is eroded in place

- **WHEN** `rifts` is called with an `HArray`
- **THEN** the returned grid is that same wrapper, holding the deformed
  values, and the shape is unchanged

#### Scenario: Plain grid input leaves the caller's data untouched

- **WHEN** `rifts` is called with a plain `{ data, shape }` object
- **THEN** a new `HArray` is returned and the caller's `Float32Array` still
  holds its original values

#### Scenario: Missing required argument is rejected

- **WHEN** `rifts` is called without `kw`, `angle`, `amplitude` or `seed`
- **THEN** a package error is thrown naming the missing argument before any
  wasm call

#### Scenario: Reverse mask inverts the elevation blend

- **WHEN** `rifts` is called with `applyMask` true first with `reverseMask`
  false and then with `reverseMask` true on a grid holding zero in one region
  and one elsewhere
- **THEN** the region left unchanged by the first call is deformed by the
  second and the other region is left unchanged

### Requirement: Rifts option defaults

The rifts options object SHALL default to `elevationNoiseShift` 0,
`kSmoothBottom` 0.05, `kSmoothTop` 0.05, `radialSpreadAmp` 0.2,
`elevationNoiseAmp` 0.1, `clampVmin` 0, `remapVmin` 0, `applyMask` true,
`reverseMask` false, `maskGamma` 1, `center` at (0.5, 0.5) and `bbox` spanning
(0, 1, 0, 1), matching the C++ declaration; omitting the object SHALL be
equivalent to passing every default explicitly.

#### Scenario: Omitted options equal explicit defaults

- **WHEN** `rifts` is called once without an options object and once with every
  default spelled out
- **THEN** both calls return identical grids

#### Scenario: Explicit center and bbox are honoured

- **WHEN** `rifts` is called with a center or bbox different from the defaults
- **THEN** the rift pattern moves or scales accordingly and the result differs
  from the defaulted call

### Requirement: Advection functions return new grids

The API SHALL expose `advectionParticle(z, field, nparticles, seed, options?)`
and `advectionWarp(z, field, options?)`, each returning a NEW grid and leaving
both input grids untouched; `field` SHALL match `z`'s shape, and a mismatch
SHALL be rejected naming `field`.

#### Scenario: New-grid semantics differ from the erosion family

- **WHEN** `advectionParticle` or `advectionWarp` is called with `HArray`
  inputs
- **THEN** it returns a new wrapper rather than mutating and returning its grid
  argument, making these the first new-grid functions in the erosion/transform
  surface

#### Scenario: Inputs are not modified

- **WHEN** an advection function is called with a terrain grid and a field grid
- **THEN** both input grids are byte-identical after the call and the returned
  grid is independent of them

#### Scenario: Mismatched field is rejected with context

- **WHEN** `field` has a shape different from `z`
- **THEN** a package error is thrown naming `field` before any wasm call

#### Scenario: Missing required particle argument is rejected

- **WHEN** `advectionParticle` is called without `nparticles` or `seed`
- **THEN** a package error is thrown naming the missing argument before any
  wasm call

### Requirement: Advection option defaults

Particle advection SHALL default `iterations` to 1 (a JS-level default, because
the C++ iterations overload declares none), `reverse` to false, `postFilter`
to true, `postFilterSigma` to 0.125, `advectionLength` to 0.1,
`valuePersistence` to 0.99 and `inertia` to 0; warp advection SHALL default
`advectionLength` to 0.1 and `valuePersistence` to 0.9. Omitting the options
SHALL equal passing every default.

#### Scenario: Omitted options equal explicit defaults

- **WHEN** either advection function is called once without an options object
  and once with every default spelled out
- **THEN** both calls return identical grids

#### Scenario: Iterations repeat the single pass

- **WHEN** `advectionParticle` is called with `iterations: 3`
- **THEN** its output equals three chained calls with `iterations: 1` and the
  same seed and options

### Requirement: Advection outputs obey the marshalling contract

Advection functions SHALL follow the same argument validation, shape reporting
and determinism rules as the rest of the surface; with sane parameters their
output SHALL be finite and deterministic, and `advectionWarp` with
`advectionLength` 0 SHALL return an all-zero grid.

#### Scenario: Repeated runs are byte-identical

- **WHEN** either advection function runs twice on two copies of the same input
  with the same options
- **THEN** both outputs are byte-for-byte identical and contain no NaN

#### Scenario: Output reports the terrain grid's shape

- **WHEN** either advection function returns
- **THEN** the returned `HArray` reports the same shape as `z`

#### Scenario: Zero advection length returns an all-zero grid

- **WHEN** `advectionWarp` is called with `advectionLength: 0`
- **THEN** every cell of the returned grid is zero, not a copy of the field

### Requirement: Deferred overloads stay unexposed

The JS surface SHALL expose only the single-field, gradient-driven advection
forms and the mask-free, noise-offset-free rifts form: rifts mask and
noise-offset grids, particle multi-field and explicit `dx`/`dy` forms, and warp
explicit-gradient forms SHALL remain unbound until a later change adds them.

#### Scenario: Deferred inputs are absent from the surface

- **WHEN** the exported API functions and their documented options are
  enumerated
- **THEN** no rifts mask or noise-offset input, no particle multi-field
  collection, and no explicit velocity or gradient input is present
