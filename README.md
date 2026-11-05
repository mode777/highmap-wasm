# highmap-wasm

> WebAssembly port of the [HighMap](https://github.com/ottolink-dev/HighMap)
> terrain generation library — CPU-only, no native dependencies, one npm
> package for Node and the browser.

**Status: bootstrap (0.0.x, unpublished).** This is the first end-to-end
slice: a curated CPU-only build of the HighMap C++ library compiled with
Emscripten + embind, wrapped in a TypeScript API modeled on the
[pyHighMap](https://github.com/otto-link/pyHighMap) Python bindings. The seed
function surface (~30 functions: pyHighMap parity, array algebra, CPU
erosion twins) grows module-family by module-family toward the full curated
API — see `docs/decisions.md` (AD-009).

- One import everywhere: ESM-only, async `init()`, identical usage in
  Node >= 18, browsers, and Web Workers.
- Plain typed arrays in, zero-copy grids out — with explicit lifetimes only
  when you want them.
- Deterministic generation: same seed + parameters → same bytes.

## Quick start

Not yet published to npm; build from source (see Development) and import the
local package:

<!-- quickstart:start -->
```js
import { initHighmap } from "highmap-wasm";

const hmap = await initHighmap();

// pyHighMap-style pipeline in JS
const base = hmap.noise(hmap.NoiseType.PERLIN, [256, 128], [4, 2], 0);
const fbm = hmap.noiseFbm(hmap.NoiseType.SIMPLEX2S, [256, 128], [4, 2], 0);

const warped = hmap.warp(
  base,
  hmap.multiply(fbm, 0.1),
  hmap.multiply(fbm, 0.1),
);
hmap.falloff(warped);

// CPU erosion twin
hmap.remap(warped);
hmap.hydraulicStream(warped, 0.1, 0.01);

console.log(warped.shape, warped.data.length, warped.data[0]);

// zero-copy views are valid until free(); copyData() returns JS-owned data
const shaded = hmap.hillshade(warped, Math.PI / 4, Math.PI / 4);
const elevation = warped.copyData();
warped.free();
shaded.free();
console.log(elevation.length);
```
<!-- quickstart:end -->

## API model

- **Functions, not classes** (pyHighMap style): `noise`, `noiseFbm`,
  `noisePingpong`, `noiseRidged`, `warp`, `falloff`, `clamp`, `remap`,
  `plateau`, `smoothFill`, `hillshade`, plus the algebra layer
  (`add`, `subtract`, `multiply`, `divide`, `scale`) and erosion
  (`hydraulicMusgrave`, `hydraulicStream`).
- **Enums**: `hmap.NoiseType.*` and `hmap.DistanceFunction.*` (raw embind
  values; the string enums exported by the package work too).
- **Grids**: every grid argument accepts an `HArray` **or** a plain
  `{ data: Float32Array, shape: [ni, nj] }`. Plain inputs are copied in;
  `HArray` inputs are used **in place** (functions that mutate in C++ return
  the same wrapper — zero-copy pipelines). Functions that produce new arrays
  (`hillshade`, the algebra layer) always return fresh `HArray`s.
- **`HArray`**: `shape` (`[ni, nj]`), `data` (live `Float32Array` view,
  re-bound on access), `copyData()` (JS-owned), `free()` (idempotent; a
  `FinalizationRegistry` reclaims forgotten wrappers).

## Loading the wasm artifact

The package bundles the Emscripten glue and references `highmap.wasm` next to
the entry via `new URL("./highmap.wasm", import.meta.url)`:

- **Vite / Rollup / webpack 5**: works with no plugin — the artifact is
  emitted as an asset (covered by `scripts/test-bundler.mjs`).
- **esbuild**: the `new URL` reference is left as-is; copy
  `node_modules/highmap-wasm/dist/highmap.wasm` to your output directory.
- **Anything exotic / SSR**: pass bytes or a locator explicitly:

```js
import { readFileSync } from "node:fs";
import { initHighmap } from "highmap-wasm";

const hmap = await initHighmap({
  wasmBinary: readFileSync(new URL("highmap-wasm/dist/highmap.wasm", import.meta.url)),
});
```

`initHighmap({ locateWasm: (file) => "https://cdn.example.com/" + file })`
also works. CommonJS consumers can use dynamic `import()`.

## Development

Requirements: Emscripten **6.0.13** (pinned; install via emsdk and point
`$EMSDK` at it), CMake >= 3.22, Ninja, Node >= 18, and initialized
submodules (`git submodule update --init --recursive`).

```bash
npm install
scripts/build-wasm.sh          # dist/highmap.wasm + dist/highmap.js (Emscripten)
node scripts/build-js.mjs      # dist/index.js + dist/index.d.ts (esbuild bundle)
npm test                       # vitest suites + consumer bundler test
npm run check                  # TypeScript
node scripts/golden-capture.mjs  # recapture golden scenes (review the diff!)
```

The wasm build lives in `HighMap/wasm/` (standalone CMake project, curated
CPU-only source list, `cv::Mat`/GSL stubs, Eigen-backed `convolve2d_svd`
replacement). It never touches the upstream desktop build, with one flagged
exception: a guarded zero-water divisor in
`HighMap/src/erosion/hydraulic_musgrave.cpp` that fixes an upstream NaN bug
and keeps desktop and wasm code identical (AD-014).

Architecture and decision log: `docs/decisions.md`. Port research:
`docs/wasm-port-research.md`. This repository is spec-driven with OpenSpec
(`openspec/`).

## License

LGPL-2.1 (see `LICENSE`), matching the upstream library after its 2026-07-09
license change. Upstream notices are preserved. The npm tarball ships
`LICENSE` and `THIRD_PARTY_LICENSES.md`.
