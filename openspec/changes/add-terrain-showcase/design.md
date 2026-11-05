# Design

## Context

The package builds a ~0.5 MB wasm artifact plus an ESM wrapper, exposes an
`HArray`-based terrain API, and already works in Web Workers. `dist/` is
gitignored; there is no wasm CI workflow. The repository is public, but the
Pages root currently serves the upstream Doxygen site from the `gh-pages`
branch, and `deploy_docs.yml` pushes there on every main/dev/doc push.

The showcase is a separate consumer, following the libfive-wasm precedent:
`site/` has its own Vite/TypeScript/three.js dependencies, depends on the root
package with `file:..`, and is excluded from the npm tarball. The libfive site
is structural inspiration only; its source is MPL-2.0 and this repository is
LGPL-2.1, so site code is written fresh rather than copied.

The catalog is constrained by the current API and by the HighMap examples. A
catalog survey found only a handful of examples whose generation pipeline maps
exactly to the exposed surface; the rest use GPU-only algorithms, missing
primitives, or extra overloads. The design therefore ports panels/variants of
examples rather than inventing composite recipes.

## Goals / Non-Goals

**Goals:**

- A static gallery and detail pages that present nine example-faithful terrain
  specimens with solid diorama bases, orbit controls, stats, and exact recipe
  source.
- A catalog that is mechanically validated against the initialized API and the
  source examples, so the showcase cannot drift into claiming unimplemented
  behavior.
- A single wasm build per CI run, used by the tests and deployed unchanged to
  GitHub Pages at `https://mode777.github.io/highmap-wasm/`.
- Fast gallery loads: no wasm on the gallery page, only pre-rendered previews.

**Non-Goals:**

- New algorithms, CPU ports, or numerical fixes (including `hydraulicMusgrave`).
- Seed reroll, parameter sliders, a terrain editor, or a docs section.
- Preserving the current Doxygen Pages deployment in this change.
- Server-side rendering, a React/Vue framework, or a multi-page build.
- Shipping the site in the npm package.

## Decisions

### 1. Site structure and stack

Use `site/` with Vite, vanilla TypeScript, and three.js; no UI framework. The
site is a hash-routed SPA: `#/`, `#/p/<id>`, and `#/p/<id>?preview=1`.

Why: the libfive precedent proves this stack is sufficient and keeps the
bundle small. The DOM surface is a gallery, a detail page, and tabs; a
framework would add build and dependency complexity without reducing code. A
hash router avoids GitHub Pages 404 rewrites entirely.

Site layout:

```text
site/
  index.html
  package.json              highmap-wasm: "file:.."
  vite.config.ts            base from SITE_BASE, default "/"
  tsconfig.json
  README.md                 run/build/demo/thumbnail/deploy contract
  src/
    main.ts, app.ts, router.ts, chrome.ts
    gallery.ts, demo-page.ts, code-view.ts, dom.ts
    viewport.ts             three.js scene, orbit, lighting, shadow
    terrain/                pure geometry, palette, normalization (no three)
    demos/                  types.ts, index.ts, sources.ts, <id>.ts
    worker/                 generate.ts, worker-client.ts
    assets/previews/        <id>.webp + manifest.json
  scripts/capture-thumbnails.mjs
```

`terrain/` stays free of three.js imports so the root Node test suite can
import it. `demos/` stays free of three.js and DOM imports for the same reason.
`viewport.ts` is the only module that owns WebGL and BufferGeometry.

### 2. Catalog: nine panel-level ports, fixed at 256²

| id | Source example | Pipeline |
|---|---|---|
| `fractal-noise` | `examples/ex_noise_fbm` — fbm panel, PERLIN | `noiseFbm(PERLIN, [256,256], [4,4], 1)` + `remap` |
| `pingpong-noise` | `examples/ex_noise_fbm` — pingpong panel, PERLIN_BILLOW | `noisePingpong(PERLIN_BILLOW, [256,256], [4,4], 1)` + `remap` |
| `ridged-noise` | `examples/ex_noise_fbm` — ridged panel, SIMPLEX2S | `noiseRidged(SIMPLEX2S, [256,256], [4,4], 1)` + `remap` |
| `worley-noise` | `examples/ex_noise` — WORLEY panel | `noise(WORLEY, [256,256], [4,4], 1)` + `remap` |
| `warp` | `examples/ex_warp` | fbm base `[2,2]`, dx/dy fbm `[4,4]` seeds 2/3, remap to ±0.25, `warp` |
| `falloff` | `examples/ex_falloff` — EUCLIDIAN panel | fbm `[256,256]` `[4,4]` seed 1, `remap`, `falloff(1, EUCLIDIAN)` |
| `plateau` | `examples/ex_plateau` | fbm `[256,256]` `[4,4]` seed 1, `plateau(64, 4)` |
| `smooth-fill` | `examples/ex_smooth_fill` | fbm `[256,256]` `[2,2]` seed 1, `smoothFill(32, {k: 0.1})` |
| `stream-erosion` | `examples/ex_hydraulic_stream` — first panel | fbm `[256,256]` `[2,2]` seed 1, `remap`, `hydraulicStream(0.1, 5/256, {ir: 1, clippingRatio: 10})` |

`ex_noise_fbm` and `ex_noise` are banner examples; each catalog entry ports one
declared panel/column, not the whole banner. Every entry records
`sourceExample`, `sourcePanel`, and a resolution note where applicable.

The only resolution deviation is `stream-erosion`: the source example uses
1024² and `talusRef = 5/1024`; the site uses 256² and `talusRef = 5/256` to keep
the mesh web-sized, with `cErosion`, `ir`, and `clippingRatio` unchanged. This
is recorded in the demo metadata and shown in the detail sidebar.

`hydraulicMusgrave` is excluded: it is a straight code port but diverges to
`Inf`/`NaN` at 128² and above with default parameters. That is a separate
stability change, not a showcase task. `PARBERRY`, `WORLEY_DOUBLE`, and the
`VALUE_DELAUNAY`/`VALUE_LINEAR` enum values are also excluded because they are
not usable through the current noise calls.

Families: `noise` (fractal, pingpong, ridged, Worley), `shaping` (warp,
falloff, plateau, smooth fill), `erosion` (stream erosion).

### 3. Demo contract and worker boundary

A demo module exports metadata plus a pure builder:

```ts
build(hmap: Highmap): { data: Float32Array; shape: [number, number] }
```

The builder owns any `HArray` it creates, copies out with `copyData()`, and
frees every wrapper in a `finally` block. It does not normalize for display;
the renderer does that, so the recipe code stays exactly the example's
algorithm.

The worker initializes `highmap-wasm` once, receives a demo id, runs the
builder, and transfers the `Float32Array` buffer back. The main thread builds
geometry from the heightmap. Generation is currently fast at 256² (fbm ~20 ms,
stream erosion ~10 ms on a desktop Node run), but keeping it off the main
thread avoids jank on slower machines and matches the future-proof boundary.

Why not build geometry in the worker? It would force three.js into the worker
and into the demo test graph. The heightmap transfer is small (256 KB), and the
geometry build is a few milliseconds on the main thread.

Why not run wasm on the main thread? Erosion plus fbm is still tens of
milliseconds and can grow with later algorithms; the worker also gives each
detail page an isolated wasm heap.

### 4. Diorama geometry

Coordinate convention: heightmap `[ni, nj]`, row-major `j * ni + i`; map `i` to
X, `j` to Z, height to Y. Normalize the heightmap to `[0, 1]` for display and
scale by a per-demo relief value (default 0.20 of the 1×1 tile). This preserves
relative shape while letting falloff's negative values render cleanly.

The block:

- **Top**: indexed `ni × nj` grid, smooth vertex normals, vertex colors from
  normalized elevation and local slope.
- **Shell**: four boundary walls, each a quad strip from the top boundary down
  to a flat base plane. Vertices are duplicated per wall so the cut faces stay
  hard-edged. The walls are vertically subdivided for smooth shading.
- **Bottom**: a two-triangle cap at the base plane, dark.
- **Base depth**: 0.22 of the tile width below the lowest top vertex, so the
  block has visible mass.
- **Side color**: one solid cut-face color per palette; no topsoil or rock
  bands.

Top and shell are separate meshes/materials: the top uses smooth normals and a
terrain palette; the shell uses flat shading and a single cut-face color derived
from the same palette. The viewport groups them, frames the union's bounding
sphere with a margin, and limits the orbit polar angle so the camera cannot go
below the block. A shadow catcher plane sits at the bottom, with the directional
key light casting a soft shadow.

Why a solid block instead of a displaced plane: the finite heightmap tile needs
an intentional edge; a cut block reads as a specimen and matches the libfive
gallery language. Why not marching cubes or an adaptive surface: the source is
already a regular grid, and a grid top preserves the exact heightmap data and
the crisp cut edge.

Palettes are code constants, no image assets: `temperate`, `arid`, `volcanic`,
`arctic`. Each has elevation stops, slope-rock blending, and a matching solid
cut-face color. Palette/relief tuning is art direction and does not change the
spec.

### 5. Detail page and code view

The detail page has a stage (3D/Code tabs), a meta bar with grid shape,
triangle count, generation time, and an aside with the title, description,
family, source example link, panel/variant, resolution note, and view toggles.

The Code tab reads the demo module with Vite's `?raw` import, syntax-highlights
it with highlight.js, and adds a copy button. The same raw source is hashed for
the preview manifest, so the displayed code, executed code, and thumbnail
provenance all point at one file. The C++ example link points at the exact
repository path; a small reference link to the example's 2D PNG is optional.

No interactive terrain controls: the site is a gallery, not a lab. Wireframe
and flat toggles are view modes only.

### 6. Thumbnails

`scripts/capture-thumbnails.mjs` starts `vite preview` on an OS-assigned port,
drives Chrome (puppeteer-core via `CHROME_SHELL_PATH` or `CHROME_URL`), opens
`#/p/<id>?preview=1`, waits for a `data-demo-ready` marker, and captures the
fixed 16:10 stage at 640×400 CSS pixels with device scale factor 2, WebP quality
82. Preview mode hides the header, aside, tabs, and footer and fixes the stage
frame.

The manifest records `previewVersion`, capture viewport, and for each demo the
file, source hash, width, and height. The source hash is SHA-256 over the demo
module source plus its serialized metadata (id, seed, grid, parameters,
palette, relief, source example/panel). The validation suite recomputes it.
Thumbnails are committed; CI validates them but does not launch a browser
(headless WebGL is too flaky for required CI). Missing images fall back to a
CSS specimen placeholder at runtime, but CI fails a missing entry.

Recapture when a recipe, metadata, palette, camera, or preview CSS changes; bump
`PREVIEW_VERSION` for viewport/material changes.

### 7. Validation in the root test suite

Root vitest imports the site catalog and pure terrain modules directly. Node
self-reference resolves `highmap-wasm` to the root package, so the same demo
modules the site bundles are validated against the real artifact.

Tests:

- unique kebab-case ids; non-empty metadata; family in the closed set; source
  example path exists; panel/variant present.
- every declared feature is a key on the initialized API object; the catalog
  contains no `hydraulicMusgrave`.
- each recipe builds twice with byte-identical data, with the declared shape,
  finite values, and a non-zero range.
- diorama geometry: base below minimum top height, boundary walls cover every
  top edge, bottom cap present, no non-finite positions.
- preview manifest covers every demo, files are non-empty, source hashes match.
- `npm pack --dry-run` does not include `site/`.

Site typecheck (`tsc --noEmit`) and production build run in CI.

### 8. CI and Pages

Two workflows, mirroring the libfive pattern:

- `build-wasm.yml` on push/PR: checkout with recursive submodules, install
  Emscripten 6.0.13 (direct emsdk install with a version-keyed cache), `npm ci`,
  `scripts/build-wasm.sh`, `node scripts/build-js.mjs`, `npm test`, and upload
  `dist/` as the `highmap-wasm` artifact.
- `pages.yml` on `workflow_run` after a successful `build-wasm` on `main`:
  checkout the tested SHA, download the `highmap-wasm` artifact into `dist/`,
  install `site/`, run `npm run check`, build with
  `SITE_BASE=/highmap-wasm/`, upload the Pages artifact, and deploy.

The wasm is built once and the deployed site consumes that exact artifact.
Building wasm again in the Pages workflow would either duplicate work or risk a
different artifact, so it is not done.

`deploy_docs.yml` is retired (deleted or disabled) because Pages switches from
the legacy `gh-pages` branch to GitHub Actions. The repository setting is
manual; `site/README.md` documents it, and the first deployment is verified at
the project URL.

Vite's `base` comes from `SITE_BASE` (default `/`) so local dev works at `/` and
Pages works at `/highmap-wasm/`. The wasm URL is emitted by Vite as an asset
through the package's `new URL("./highmap.wasm", import.meta.url)` reference,
already covered by the root bundler test.

Documentation links point at the existing GitHub README, `docs/decisions.md`,
`docs/wasm-port-research.md`, and the examples directory. The footer also shows
the LGPL-2.1 notice and a link to the exact commit plus `scripts/build-wasm.sh`
for the deployed artifact. Doxygen can return under `/docs/` in a later change.

### 9. Package boundary

The root `files` list already ships only `dist`, README, LICENSE, and
`THIRD_PARTY_LICENSES.md`; `site/` is therefore excluded. The site depends on
`file:..` so it always consumes the locally built artifact, never the registry.
A test asserts the pack contents stay that way.

## Risks / Trade-offs

- **Some examples may read weakly in 3D.** `smoothFill` and `plateau` changes
  can be subtle from a distance. → Prototype all nine early; use palette and
  relief to make the effect legible, and keep the C++ reference link so the
  comparison is explicit.
- **The stream demo is not resolution-faithful.** The source uses 1024²; the
  site uses 256² and a scaled `talusRef`. → Record the deviation in metadata
  and the detail sidebar; keep the rest of the parameters identical.
- **Musgrave is implemented but absent.** Visitors may ask why. → Keep it out
  of the catalog; document the stability issue in the change and address it in
  a separate fix.
- **Pages source switch is a manual repository setting.** The workflow will not
  deploy until Pages is set to GitHub Actions. → Document it in `site/README.md`
  and verify the first deployment; rollback is re-enabling the old source and
  `deploy_docs.yml`.
- **Emscripten 6.0.13 in CI.** Toolchain install can be slow or unavailable in
  an action cache. → Install from emsdk directly, cache by version, and pin the
  same version as `scripts/build-wasm.sh`.
- **Headless WebGL thumbnails.** Capture is environment-sensitive. → Capture
  locally, commit WebP + manifest, and keep browser launch out of required CI.
- **Mobile GPU cost.** 256² top plus shell is ~130k triangles, manageable, but
  not free. → Cap device pixel ratio at 2, pause rendering when the tab is
  hidden or the code tab is active, and keep the gallery static.
- **LGPL distribution.** Serving the wasm on Pages is distribution. → The
  footer carries the notice and source/build link; strict object-file relinking
  artifacts can be added later if legal review requires them.
- **Root tests importing site code can accidentally pull three.js.** → Keep
  `demos/` and `terrain/` free of three.js imports; only `viewport.ts` imports
  it.

## Migration Plan

1. Build `dist/` locally, add `site/`, and verify the gallery, detail pages, and
   worker with `SITE_BASE=/`.
2. Add the wasm build workflow and Pages workflow; retire `deploy_docs.yml`;
   document the Pages source setting and local build steps.
3. Switch Pages to GitHub Actions, push to `main`, and verify the project URL,
   wasm loading, previews, docs links, and license footer.
4. Rollback: re-enable the legacy Pages source and `deploy_docs.yml`; the site
   is additive and does not change the package artifact or API.

## Open Questions

- Doxygen information architecture under `/docs/` is deferred to a later docs
  change; it does not affect this site or its deployment.
- Whether strict LGPL relinking requires published object files in addition to
  the pinned source/build recipe is deferred to a legal review; the spec's
  source/build link ships now.
