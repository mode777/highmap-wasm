# Terrain showcase

The public gallery for `highmap-wasm`. It is a **separate consumer** of the
package: not part of the npm tarball, and it always links the locally built
artifact with `file:..`.

The site is a static Vite + TypeScript + three.js app. Every terrain is a
panel-level port of an existing HighMap C++ example; the gallery shows
pre-rendered specimen dioramas and only the detail page initializes wasm.

## Run

Build the package artifact first, because the site never fetches
`highmap-wasm` from the registry:

```sh
scripts/build-wasm.sh
node scripts/build-js.mjs
cd site
npm ci
npm run dev        # http://localhost:5173/
```

Production build and typecheck:

```sh
npm run check
npm run build
```

For the project Pages URL, set the base path:

```sh
# bash
SITE_BASE=/highmap-wasm/ npm run build
```

```powershell
# PowerShell
$env:SITE_BASE = "/highmap-wasm/"; npm run build
```

`SITE_BASE` defaults to `/`, so local builds open at the server root.

## Demo contract

Each demo is a TypeScript module under `src/demos/` with metadata and a pure
builder:

```ts
import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "my-demo",
    title: "My demo",
    description: "One sentence about the terrain.",
    family: "noise",
    features: ["noiseFbm", "remap"],
    grid: GRID,
    seed: 1,
    palette: "temperate",
    relief: 0.2,
    source: {
      example: "examples/ex_noise_fbm/ex_noise_fbm.cpp",
      panel: "fbm banner, PERLIN column",
    },
    // Optional: record any web-sized deviation from the source values.
    // adaptationNote:
    //   "Ported at 256² with talusRef 5/256; the example uses 1024² and 5/1024.",
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [4, 4], 1);
    try {
      hmap.remap(z);
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
```

Rules:

- `id` is stable and kebab-case; detail routes are `#/p/<id>`.
- `family` is one of `noise`, `shaping`, `erosion`.
- `features` must be names exposed by the initialized `highmap-wasm` API. The
  root catalog test fails otherwise. Do not declare GPU-only or unimplemented
  algorithms.
- `source.example` is a real repository-relative C++ path, and `source.panel`
  names the panel or variant that was ported. Keep the example's grid, seed,
  and parameters unless `adaptationNote` records a web-sized deviation.
- `adaptationNote` is optional and states both the source and shipped values
  when the grid resolution, talus, or iteration budget differs from the
  example; the detail page shows it as "Adaptation".
- The builder owns every `HArray` it creates and frees it in `finally`. It must
  not import three.js, the DOM, or browser-only APIs; the root Node test imports
  it directly. Return `copyOut(grid)` so the worker receives JS-owned data.
- `relief` is the vertical scale in tile units; `palette` is one of
  `temperate`, `arid`, `volcanic`, `arctic`.
- Do not add a composite recipe that is not a straight port of an example.
  `hydraulicMusgrave` stays out while its numerical stability is unresolved.

## Thermal coverage

The catalog includes one entry for each CPU thermal variant: `thermal`,
`thermal-conserve`, `thermal-auto-bedrock`, `thermal-flatten`, `thermal-olsen`,
`thermal-rib`, `thermal-ridge`, `thermal-inflate`, `thermal-scree`, and
`thermal-schott`.

All ten use a 256² grid. `thermal-conserve`, `thermal-flatten`, and
`thermal-olsen` use 100 iterations instead of the examples' 500; olsen also
scales its talus from `4/1024` to `4/256`. The ridge family (`thermal-ridge`,
`thermal-inflate`, `thermal-scree`) scales its talus from `2/1024` to `2/256`.
Those deviations are recorded in each demo's `adaptationNote`.

The three O(64) variants are slow in a browser (roughly 19–98 s), so capture
their previews headful with the extended timeout described below.

## Add a demo

1. Add `src/demos/<id>.ts` using the contract above.
2. Import it in `src/demos/index.ts` and append it to `demos`.
3. Add its raw source import to `src/demos/sources.ts` under the same id.
4. Run the catalog test from the repository root:
   ```sh
   npx vitest run tests/showcase-catalog.test.ts
   ```
5. Build and capture its preview (see below), then verify the detail page and
   the gallery card.

## Thumbnails

Gallery cards use committed WebP previews captured from the real detail page.
After `npm run build`:

```sh
npm run thumbnails:capture
```

The script starts `vite preview` on an OS-assigned port, drives Chrome through
`puppeteer-core`, opens `#/p/<id>?preview=1`, waits for the rendered specimen,
and writes `src/assets/previews/<id>.webp` plus `manifest.json`.

The three O(64) thermal recipes take minutes to generate in a browser, and
headless Chrome is slower than headful. For a full capture, run headful with an
extended generation timeout:

```sh
CAPTURE_HEADFUL=1 CAPTURE_TIMEOUT_MS=300000 npm run thumbnails:capture
```

```powershell
# PowerShell
$env:CAPTURE_HEADFUL = "1"; $env:CAPTURE_TIMEOUT_MS = "300000"; npm run thumbnails:capture
```

Chrome discovery:

- `CHROME_SHELL_PATH=/path/to/chrome` — launch that browser.
- `CHROME_URL=http://127.0.0.1:9222` — connect to an existing DevTools browser.
- With neither, the script looks for Chrome or Edge in the usual platform
  locations.

Capture contract: 640 x 400 CSS pixels at device scale factor 2 (1280 x 800),
WebP quality 82, fixed 16:10 preview stage. The manifest records a SHA-256 of
each demo module (metadata included). The root preview test fails when a
recipe changes without a recapture, when an image is missing, or when the
manifest has an orphan entry.

Recapture when a demo module, palette, camera, or preview CSS changes. Bump
`PREVIEW_VERSION` in `scripts/capture-thumbnails.mjs` when the viewport,
lighting, or material contract changes, and update the assertion in
`tests/showcase-previews.test.ts`.

## Deployment

`.github/workflows/build-wasm.yml` builds and tests the wasm artifact on pushes
and pull requests. `.github/workflows/pages.yml` runs after a successful
`build-wasm` on `main`, downloads that run's `dist/` artifact, builds the site
with `SITE_BASE=/highmap-wasm/`, and deploys `site/dist` to GitHub Pages.

The repository Pages source is **GitHub Actions** (**Settings → Pages → Build
and deployment → Source**), not the legacy `gh-pages` branch. The old
`deploy_docs.yml` workflow was retired with this change.

First deploy verification:

1. Push the change to `main` and wait for `build-wasm` to succeed.
2. Confirm the `pages` workflow deployed.
3. Open `https://mode777.github.io/highmap-wasm/` and check the gallery, one
   detail page, the code tab, and the footer links.
4. Confirm the deployed detail page loads `assets/highmap-*.wasm` and renders a
   specimen.

Rollback: set Pages back to the legacy `gh-pages` source and restore the old
docs workflow from git history. The site is additive; it does not change the
package artifact or the public API.

## Docs and license

The footer links the existing repository docs (`README.md`,
`docs/decisions.md`, `docs/wasm-port-research.md`), the C++ examples, the build
recipe, and the license. There is no docs section inside the site yet.

HighMap and this showcase are LGPL-2.1. Serving the wasm artifact is
distribution; the footer carries the notice and the source/build links.
