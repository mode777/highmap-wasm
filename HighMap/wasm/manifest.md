# Binding manifest

The single source of truth for the embind surface (AD-008). Every row is one
function exposed on the wasm module and (wrapped) on the JS API object;
`bindings.cpp` implements one row per entry, the TS API layer mirrors it, and
`tests/surface.test.ts` asserts the exposed surface equals this manifest.

Growth policy: new functions append rows here plus their binding + wrapper +
tests. Existing rows never change signature within a major version.

## Enums

| C++ enum | JS enum | values |
|---|---|---|
| `hmap::NoiseType` | `NoiseType` | PARBERRY, PERLIN, PERLIN_BILLOW, PERLIN_HALF, SIMPLEX2, SIMPLEX2S, VALUE, VALUE_CUBIC, WORLEY, WORLEY_DOUBLE, WORLEY_VALUE |
| `hmap::DistanceFunction` | `DistanceFunction` | CHEBYSHEV, EUCLIDIAN, EUCLISHEV, MANHATTAN |

## Class

| C++ | JS (raw) | notes |
|---|---|---|
| `hmap::Array` | `RawArray` | constructed via `makeArray(ni, nj, value)`; accessors `ni()`, `nj()`, `dataPtr()` |

## Functions

Defaults are applied by the TS layer (pyHighMap defaults); the binding layer
takes explicit arguments.

| JS name (binding) | C++ call | Binding signature | JS options (defaults) |
|---|---|---|---|
| `makeArray` | `hmap::Array` ctor | `(ni, nj, value)` | — |
| `noise` | `hmap::noise` | `(noiseType, ni, nj, kx, ky, seed)` | — |
| `noiseFbm` | `hmap::noise_fbm` | `(noiseType, ni, nj, kx, ky, seed, octaves, weight, persistence, lacunarity)` | `octaves: 8, weight: 0.7, persistence: 0.5, lacunarity: 2` |
| `noisePingpong` | `hmap::noise_pingpong` | same as `noiseFbm` | same as `noiseFbm` |
| `noiseRidged` | `hmap::noise_ridged` | `noiseFbm` args + `kSmoothing` | `…same, kSmoothing: 0.1` |
| `clampValues` (JS: `clamp`) | `hmap::clamp` | `(array&, vmin, vmax)` | `vmin: 0, vmax: 1` |
| `remapValues` (JS: `remap`) | `hmap::remap` | `(array&, vmin, vmax)` | `vmin: 0, vmax: 1` |
| `falloff` | `hmap::falloff` | `(array&, strength, distFct)` | `strength: 1, distFct: EUCLIDIAN` |
| `plateau` | `hmap::plateau` | `(array&, ir, factor)` | — |
| `smoothFill` | `hmap::smooth_fill` | `(array&, ir, k)` | `k: 0.1` |
| `hillshade` | `hmap::hillshade` | `(array&) → Array`, `(azimuth, zenith, talusRef)` after | `talusRef: 1` |
| `warp` | `hmap::warp` | `(array&, dx&, dy&)` | — |
| `add` / `subtract` / `multiply` / `divide` | `Array` operators | `(a&, b&) → Array` | — |
| `addScalar` / `subtractScalar` / `multiplyScalar` / `divideScalar` | `Array` operators | `(a&, value) → Array` | — |
| `scale` | `operator*=` | `(array&, factor)` | — |
| `hydraulicMusgrave` | `hmap::hydraulic_musgrave` | `(z&, iterations, cCapacity, cErosion, cDeposition, waterLevel, evapRate)` | `iterations: 100, cCapacity: 1, cErosion: 0.1, cDeposition: 0.1, waterLevel: 0.01, evapRate: 0.01` |
| `hydraulicStream` | `hmap::hydraulic_stream` | `(z&, cErosion, talusRef, ir, clippingRatio)` | `ir: 1, clippingRatio: 10` |

Deferred: the moisture-aware `hydraulic_musgrave` overload (C++ takes an
extra moisture map). It NaNs on non-constant moisture maps in the pinned
upstream tree — the sediment transport step divides by the local water level,
which can hit exact zero, and one NaN then spreads through the Laplace
regularization (verified against the CPU code path; not a port artifact).
Revisit with an upstream fix; see AD-013 in docs/decisions.md.

The **high-level JS API** (`initHighmap()`) exposes these under pyHighMap
names — `clamp` and `remap` for the two `*Values` bindings, with options
objects carrying the defaults listed above; the algebra layer's `add` /
`subtract` / `multiply` / `divide` accept a grid **or** a number and dispatch
to the scalar variants internally.

## Notes

- Mutating binding functions take `Array&` and act **in place**; the TS layer
  returns the same `HArray` for chaining. Plain `{ data, shape }` inputs are
  copied into a fresh wasm array first, so callers' data is never touched.
- `hillshade` returns a new array (pyHighMap semantics).
- The algebra layer exists because JS has no operator overloading (AD-009).
