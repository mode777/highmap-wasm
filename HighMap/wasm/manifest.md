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

### Thermal family (CPU ports)

The thermal variants were GPU-only upstream; `HighMap/src/erosion/thermal_cpu.cpp`
transcribes their OpenCL kernels into CPU code (see `docs/decisions.md`
AD-017) and `thermal_schott` reuses the upstream CPU implementation.

`talus` (and `zmax` for `thermalScree`) accepts a **number** (uniform limit) or
a **grid** (per-cell limit); the TS layer materializes a constant wasm grid for
numbers. Defaults are the C++ header defaults where declared; `thermalRib`,
`thermalFlatten` and `thermalOlsen` declare no `iterations` default in C++, so
the TS layer uses 10 for consistency.

| JS name (binding) | C++ call | Binding signature | JS options (defaults) |
|---|---|---|---|
| `thermal` | `hmap::thermal` | `(z&, talus&, iterations)` | `iterations: 10` |
| `thermalAutoBedrock` | `hmap::thermal_auto_bedrock` | `(z&, talus&, iterations)` | `iterations: 10` |
| `thermalConserve` | `hmap::thermal_conserve` | `(z&, talus&, iterations, rate)` | `iterations: 10, rate: 0.5` |
| `thermalFlatten` | `hmap::thermal_flatten` | `(z&, talus&, iterations, sigmaInf, sigmaSup)` | `iterations: 10, sigmaInf: 0.5, sigmaSup: 0` |
| `thermalOlsen` | `hmap::thermal_olsen` | `(z&, talus&, iterations)` | `iterations: 10` |
| `thermalRib` | `hmap::thermal_rib` | `(z&, iterations)` | `iterations: 10` |
| `thermalInflate` | `hmap::thermal_inflate` | `(z&, talus&, iterations)` | `iterations: 10` |
| `thermalRidge` | `hmap::thermal_ridge` | `(z&, talus&, iterations)` | `iterations: 10` |
| `thermalScree` | `hmap::thermal_scree` | `(z&, talus&, zmax&, iterations)` | `iterations: 10` |
| `thermalSchott` | `hmap::thermal_schott` | `(z&, talus&, iterations, intensity)` | `iterations: 10, intensity: 0.2` |

`thermalSchott` is not a port: it calls the upstream CPU implementation
(`src/erosion/thermal_schott.cpp`), which extrapolates the borders after
**every** iteration - unlike the GPU wrapper, which extrapolates once after the
loop. It is bound as-is and excluded from the parity bounds; see
`docs/thermal-parity.md`.

Deferred: the mask and deposition-map overloads of the thermal family (the
`const Array *p_mask` and `Array *p_deposition_map` parameters) are implemented
in C++ but not bound — the JS surface has no mask or output-grid concept yet.
Add them as optional arguments when that concept lands.

### Hydraulic family (CPU ports)

The hydraulic family was GPU-only upstream; `HighMap/src/erosion/hydraulic_cpu.cpp`
transcribes the OpenCL kernels into CPU code (AD-022). Output maps are optional,
caller-allocated `HArray`s in the options object; omitting one is equivalent to
passing a null output pointer. The C++ ports also implement the mask, moisture
map and rain map input overloads, but the JS surface binds only the default-map
forms for now.

| JS name (binding) | C++ call | Binding signature | JS options (defaults) |
|---|---|---|---|
| `hydraulicMcdonald` | `hmap::hydraulic_mcdonald` | `(z&, steps, seed, sediment&, discharge&, worldExtentKm, zScaleKm, samples, maxage, lrate, timeStep, rainfall, evapRate, gravity, viscosity, bedShear, critSlope, settleRate, thermalRate, depositionRate, suspensionRate, exitSlope)` | required `steps`, `seed`; `worldExtentKm: 40, zScaleKm: 4, samples: 8192, maxage: 512, lrate: 0.2, timeStep: 10, rainfall: 1, evapRate: 1e-4, gravity: 9.81, viscosity: 0.025, bedShear: 0.01, critSlope: 0.57, settleRate: 0.1, thermalRate: 2.5e-3, depositionRate: 5e-3, suspensionRate: 2.5e-4, exitSlope: 0.01`; optional outputs `sediment`, `discharge` |
| `hydraulicMcdonaldMultiscale` | `hmap::hydraulic_mcdonald_multiscale` | `(z&, seed, stepsPerLevel, sediment&, discharge&, worldExtentKm, zScaleKm, samples, maxage, lrate, timeStep, rainfall, evapRate, gravity, viscosity, bedShear, critSlope, settleRate, thermalRate, depositionRate, suspensionRate, exitSlope)` | required `seed`; `stepsPerLevel: [512, 256, 128]`; same physical defaults as `hydraulicMcdonald`; optional outputs `sediment`, `discharge` |
| `hydraulicSchott` | `hmap::hydraulic_schott` | `(z&, iterations, talus&, flow&, cErosion, cThermal, cDeposition, flowAccExponent, flowAccExponentDepo, flowRoutingExponent, thermalWeight, depositionWeight)` | required `talus`; `iterations: 10, cErosion: 1, cThermal: 0.1, cDeposition: 0.2, flowAccExponent: 0.8, flowAccExponentDepo: 0.8, flowRoutingExponent: 1.3, thermalWeight: 1.5, depositionWeight: 2.5`; optional output `flow` |
| `hydraulicSchottErosion` | `hmap::hydraulic_schott_erosion` | `(z&, iterations, flow&, cErosion, flowAccExponent, flowRoutingExponent)` | `iterations: 10, cErosion: 1, flowAccExponent: 0.8, flowRoutingExponent: 1.3`; optional output `flow` |
| `hydraulicVpipes` | `hmap::hydraulic_vpipes` | `(z&, waterHeight, maintainWaterVolume, evapRate, iterations, dt, kCapacity, kErode, kDepose, kDischargeExp, downcuttingMaxDepthRatio, fluxDiffusion, fluxDiffusionStrength, waterDepth&, sediment&, velU&, velV&)` | `waterHeight: 1e-2, maintainWaterVolume: true, evapRate: 0.1, iterations: 50, dt: 0.5, kCapacity: 0.5, kErode: 0.001, kDepose: 0.01, kDischargeExp: 1, downcuttingMaxDepthRatio: 10, fluxDiffusion: true, fluxDiffusionStrength: 0.01`; optional outputs `waterDepth`, `sediment`, `velU`, `velV` |

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

### Rifts / advection family (CPU ports)

The rifts and advection algorithms were GPU-only upstream;
`HighMap/src/erosion/rifts_cpu.cpp` and
`HighMap/src/transform/advection_cpu.cpp` transcribe their OpenCL kernels into
CPU code (AD-026). `rifts` mutates its grid in place like the erosion family;
the two advection functions return a new grid and leave both inputs untouched
(AD-027). `kw`, `center` and `bbox` are JS tuples that the TS layer converts
to the scalar binding arguments — the bbox order is `[xmin, xmax, ymin, ymax]`,
mirroring the C++ `glm::vec4`.

| JS name (binding) | C++ call | Binding signature | JS options (defaults) |
|---|---|---|---|
| `rifts` | `hmap::rifts` | `(z&, kwX, kwY, angle, amplitude, seed, elevationNoiseShift, kSmoothBottom, kSmoothTop, radialSpreadAmp, elevationNoiseAmp, clampVmin, remapVmin, applyMask, reverseMask, maskGamma, centerX, centerY, bboxXmin, bboxXmax, bboxYmin, bboxYmax)` | required `kw: [kx, ky]`, `angle`, `amplitude`, `seed`; `elevationNoiseShift: 0, kSmoothBottom: 0.05, kSmoothTop: 0.05, radialSpreadAmp: 0.2, elevationNoiseAmp: 0.1, clampVmin: 0, remapVmin: 0, applyMask: true, reverseMask: false, maskGamma: 1, center: [0.5, 0.5], bbox: [0, 1, 0, 1]` |
| `advectionParticle` | `hmap::advection_particle` | `(z&, field&, iterations, nparticles, seed, reverse, postFilter, postFilterSigma, advectionLength, valuePersistence, inertia) → Array` | required `nparticles`, `seed`; `iterations: 1, reverse: false, postFilter: true, postFilterSigma: 0.125, advectionLength: 0.1, valuePersistence: 0.99, inertia: 0` |
| `advectionWarp` | `hmap::advection_warp` | `(z&, field&, advectionLength, valuePersistence) → Array` | `advectionLength: 0.1, valuePersistence: 0.9` |

Deferred: the extra input forms of this family stay unbound — rifts'
`p_noise_x`/`p_noise_y`/`p_mask`, particle's `p_advection_mask`/`p_mask` plus
the multi-field and explicit `dx`/`dy` overloads, and warp's `p_mask` plus the
explicit `dx`/`dy` overload. The C++ ports implement all of them; the JS
surface has no mask or auxiliary-input-grid concept yet (AD-019 precedent,
AD-027), and an array of fields has no typed representation. Add them as
optional options when that concept lands.

## Notes

- Mutating binding functions take `Array&` and act **in place**; the TS layer
  returns the same `HArray` for chaining. Plain `{ data, shape }` inputs are
  copied into a fresh wasm array first, so callers' data is never touched.
- `hillshade` returns a new array (pyHighMap semantics).
- The algebra layer exists because JS has no operator overloading (AD-009).
