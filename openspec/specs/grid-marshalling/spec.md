# grid-marshalling Specification

## Purpose

Defines how 2D float grids cross the JavaScript/WebAssembly boundary: the
`HArray` result type, its lifetime and zero-copy semantics, the coercion of
plain typed-array inputs, and the validation and error behavior of grid
arguments.

## Requirements

### Requirement: Dual input forms for grid arguments

Every API function that accepts a grid SHALL accept either an `HArray` (the
wrapper type returned by API functions) or a plain object of the form
`{ data: Float32Array, shape: [ni, nj] }`; both forms SHALL produce identical
results.

#### Scenario: Plain input matches HArray input

- **WHEN** a function is called twice — once with a plain `{ data, shape }`
  object and once with the `HArray` produced by an earlier call holding the
  same values
- **THEN** both calls return grids with identical shape and values

### Requirement: HArray result type with live view and copy

Functions returning a grid SHALL return an `HArray` exposing: `shape` as
`[ni, nj]`, a `data` getter returning a live `Float32Array` view over wasm
memory, and a `copyData()` method returning a JS-owned copy that stays valid
after the `HArray` is freed.

#### Scenario: View reflects computed values

- **WHEN** an `HArray` is returned from a generation function and its `data`
  is read
- **THEN** the view contains the function's output values with length
  `ni * nj` matching `shape`

#### Scenario: Copy survives freeing the wrapper

- **WHEN** `copyData()` is called on an `HArray` and the wrapper is then
  freed
- **THEN** the copied `Float32Array` still holds the same values, while
  further `data` access on the freed wrapper throws

### Requirement: Explicit idempotent free with GC safety net

`HArray` SHALL expose an idempotent `free()` that releases the underlying
wasm memory immediately; using a freed wrapper (other than calling `free()`
again) SHALL throw a package-defined error. A wrapper that is garbage
collected without `free()` SHALL still have its wasm memory reclaimed via a
finalizer registry.

#### Scenario: Double free is safe

- **WHEN** `free()` is called twice on the same `HArray`
- **THEN** the second call returns normally without error

#### Scenario: Use after free throws package error

- **WHEN** `data` or any function is used on a freed `HArray`
- **THEN** a package-defined error is thrown, not a low-level wasm crash

#### Scenario: Garbage-collected wrappers are reclaimed

- **WHEN** an `HArray` becomes unreachable without `free()` and garbage
  collection runs
- **THEN** the finalizer registry releases the underlying wasm memory

### Requirement: Input validation before entering wasm

Grid arguments SHALL be validated in JS before any wasm call: `data` MUST be
a `Float32Array`, `shape` MUST be two positive integers, and
`data.length` MUST equal `shape[0] * shape[1]`; violations SHALL throw a
package-defined error naming the offending argument.

#### Scenario: Length mismatch throws with context

- **WHEN** a function is called with a grid argument whose `data` holds 10
  elements and whose `shape` is `[4, 4]`
- **THEN** a package-defined error is thrown naming the argument and the
  mismatch, and no wasm call is made

#### Scenario: Non-float32 data is rejected

- **WHEN** a grid argument's `data` is a `Uint8Array`
- **THEN** a package-defined error is thrown before any wasm call

### Requirement: Documented shape convention

The package SHALL document and consistently apply the shape convention
`[ni, nj]` with `(i, j)` indexing matching the underlying C++ array layout,
so that a known small grid maps predictably between JS indices and values.

#### Scenario: Indexing matches the documented convention

- **WHEN** a grid with known distinct values is passed through any round-trip
  (for example generated, then read back via `data`)
- **THEN** element order matches the documented `[ni, nj]` row-major
  convention exactly
