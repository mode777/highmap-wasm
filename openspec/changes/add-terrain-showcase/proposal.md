# Proposal

## Why

The library now has a coherent, working CPU-only seed surface, but there is no
public artifact that shows what it can actually do: the repository is public,
the only Pages deployment is the upstream Doxygen site, and the npm package is
unpublished. Before growing the algorithm surface, we need a showcase that makes
the current capability legible, inspectable, and honest about what is not yet
implemented. The libfive-wasm showcase is the precedent: a static gallery of
small, real programs with an orbitable 3D view and the exact source that
produced each result.

## What Changes

- Add a separate `site/` consumer (Vite + TypeScript + three.js, no framework)
  that is not part of the npm tarball and links the locally built package via
  `file:..`.
- Add a static gallery (no seed reroll or parameter controls) of nine terrains
  that are panel-level straight ports of existing HighMap examples:
  `ex_noise_fbm` (fbm, pingpong, ridged), `ex_noise` (Worley), `ex_warp`,
  `ex_falloff` (Euclidian), `ex_plateau`, `ex_smooth_fill`, and the first
  `hydraulic_stream` panel of `ex_hydraulic_stream`.
- Render each terrain as a **specimen diorama**: a square block with a
  heightmap top, vertical cut faces that follow the terrain boundary, a thin
  topsoil band, horizontal rock strata, and a flat bottom. The base is a
  first-class part of the presentation, not a thin plane.
- Add a detail page with an orbitable three.js viewport, mesh statistics,
  wireframe/flat view toggles, and a code tab showing the exact TypeScript
  recipe, the source C++ example, and the panel or variant it ports.
- Generate terrain off the main thread in a Web Worker; demo modules stay pure
  (no DOM, no three.js) so the catalog can be validated in Node against the real
  wasm artifact.
- Pre-render gallery thumbnails from the real detail page and commit WebP
  images plus a manifest with source hashes.
- Add a GitHub Actions wasm build and a Pages deployment that consumes that
  tested artifact. The showcase takes the Pages root; the legacy Doxygen
  `gh-pages` deployment is retired for now, and documentation links point at the
  existing GitHub Markdown docs. Doxygen can return under `/docs/` in a later
  change.
- Add catalog validation that every declared feature exists on the initialized
  API, every recipe builds deterministically to a finite, non-degenerate grid,
  and every thumbnail matches its recipe source hash.

Explicitly out of scope:

- New HighMap algorithms or CPU ports.
- Interactive seed reroll, parameter sliders, or a general terrain editor.
- A docs section inside the site.
- `hydraulicMusgrave` in the catalog: it is a straight code port but diverges
  to `Inf`/`NaN` at the example's 256 x 256 resolution, even with the existing
  zero-water guard. It stays out until a separate stability fix lands.
- `PARBERRY`, `WORLEY_DOUBLE`, and `VALUE_DELAUNAY`/`VALUE_LINEAR`: bound or
  adjacent enum values that are not usable through the current noise calls.
- Invented composite recipes (for example "badlands" or "archipelago"); the
  catalog stays tied to examples that port directly.

## Capabilities

### New Capabilities

- `showcase-site`: the public, static terrain showcase: gallery and detail
  behavior, the specimen diorama rendering contract, example-faithful catalog
  scope, worker-based generation, thumbnails, validation, and GitHub Pages
  publication.

### Modified Capabilities

None. The showcase consumes the existing `terrain-api`, `grid-marshalling`,
`wasm-build`, and `wasm-loader` behavior without changing their requirements.

## Impact

- New code under `site/` (site entry, router, gallery, demo pages, three.js
  viewport, diorama geometry, worker, demo catalog, styles, thumbnail capture
  script, site tests).
- New workflows under `.github/workflows/` for the wasm build and Pages
  deployment; the existing `deploy_docs.yml` is retired for now.
- Root test suite gains showcase catalog validation; the root package
  `files` list is unchanged, so the site is not shipped to npm.
- GitHub Pages source changes from the legacy `gh-pages` branch to GitHub
  Actions; the repository is already public.
- Site assets include the wasm binary, so the showcase carries the LGPL-2.1
  notice and a source/build link. No upstream C++ source is edited.
