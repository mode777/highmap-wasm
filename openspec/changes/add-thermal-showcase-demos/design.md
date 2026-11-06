# Design

## Context

See `proposal.md` for motivation. The showcase already has a stable demo
contract: a module exports metadata plus a pure `build(hmap)` that returns a
copied heightmap, the worker runs it, and the root test suite validates every
entry against the real wasm artifact. The ten thermal functions are already
bound and covered by the `thermal-cpu-ports` spec; this change only adds
catalog entries, previews, and the small metadata/test updates they require.

Measured on the current wasm artifact at 256² with 500 iterations: `thermal`
~0.9 s, `thermal_rib` ~0.04 s, `thermal_auto_bedrock` ~2.6 s,
`thermal_ridge` ~2.9 s, `thermal_inflate` ~3.4 s, `thermal_scree` ~3.3 s,
`thermal_schott` ~1.1 s, but `thermal_conserve` ~13–21 s,
`thermal_flatten` ~24–35 s, and `thermal_olsen` ~16–26 s. The last three are
O(64) per cell per iteration (each neighbour recomputes its own neighbour
table), so their cost is inherent to the ported kernel, not a build issue.

Browser measurements differ sharply from Node: at 100 iterations, Chrome takes
roughly 76 s for `thermal_conserve`, 98 s for `thermal_flatten`, and 19 s for
`thermal_olsen` (headful; headless is slower still). That cost is accepted for
this change. The detail page keeps its non-blocking loading state, and previews
are captured headful with an extended timeout.

## Goals / Non-Goals

**Goals:**

- Ten new entries that are straight ports of the thermal example panels, with
  one entry per API variant and every deviation recorded in metadata.
- A catalog that proves the thermal family is covered and keeps the existing
  deterministic, finite, diorama, and preview guarantees.
- Detail pages that stay interactive while generation runs, with the three slow
  variants accepted at roughly 19–98 s in-browser.

**Non-Goals:**

- Changing the worker, diorama geometry, gallery layout, existing entries'
  palettes, or preview capture contract.
- Adding a comparison view, parameter controls, or multiple panels per entry.
- Re-tuning the thermal kernels or changing their iteration semantics.

## Decisions

### 1. One entry per thermal variant, using a representative panel

Ten modules, all in the `erosion` family, with ids matching their file names:

| id | Source example | Panel | Recipe |
|---|---|---|---|
| `thermal` | `examples/ex_thermal/ex_thermal.cpp` | first result panel | fbm `PERLIN` 256² `[4,4]` seed 1; `thermal(z, 0.1/256, {iterations: 500})` |
| `thermal-conserve` | `examples/ex_thermal/ex_thermal.cpp` | second result panel | same base; `thermalConserve(z, 0.1/256, {iterations: 100})` |
| `thermal-auto-bedrock` | `examples/ex_thermal_auto_bedrock/ex_thermal_auto_bedrock.cpp` | result panel | fbm `PERLIN` 256² `[2,2]` seed 1; `thermalAutoBedrock(z, 0.5/256, {iterations: 500})` |
| `thermal-flatten` | `examples/ex_thermal_flatten/ex_thermal_flatten.cpp` | `sigma_sup = 0.05` panel | fbm `PERLIN` 256² `[2,2]` seed 1; `thermalFlatten(z, 1.5/256, {iterations: 100, sigmaInf: 0.5, sigmaSup: 0.05})` |
| `thermal-olsen` | `examples/ex_thermal_olsen/ex_thermal_olsen.cpp` | result panel | fbm `PERLIN` 256² `[4,4]` seed 0; `remap`; `thermalOlsen(z, 4/256, {iterations: 100})` |
| `thermal-rib` | `examples/ex_thermal_rib/ex_thermal_rib.cpp` | result panel | fbm `PERLIN` 256² `[2,2]` seed 1; `thermalRib(z, {iterations: 5})` |
| `thermal-ridge` | `examples/ex_thermal_ridge/ex_thermal_ridge.cpp` | `thermal_ridge` panel | fbm `PERLIN` 256² `[4,4]` seed 1; `remap`; `thermalRidge(z, 2/256, {iterations: 500})` |
| `thermal-inflate` | `examples/ex_thermal_ridge/ex_thermal_ridge.cpp` | `thermal_inflate` panel | same base; `thermalInflate(z, 2/256, {iterations: 500})` |
| `thermal-scree` | `examples/ex_thermal_ridge/ex_thermal_ridge.cpp` | `thermal_scree` panel | same base; `thermalScree(z, 2/256, 0.5, {iterations: 500})` |
| `thermal-schott` | `examples/ex_thermal_schott/ex_thermal_schott.cpp` | elevation-aligned talus panel | fbm `PERLIN` 256² `[2,2]` seed 1; `remap`; clone to talus map and `remap(talusMap, {vmin: 1/256, vmax: 2/256})`; `thermalSchott(z, talusMap, {iterations: 500})` |

The table is the contract; palette and relief are art direction chosen during
implementation. Initial values: `thermal` arid/0.18, `thermal-conserve`
temperate/0.18, `thermal-auto-bedrock` volcanic/0.20, `thermal-flatten`
arid/0.18, `thermal-olsen` arctic/0.20, `thermal-rib` temperate/0.16,
`thermal-ridge` arid/0.20, `thermal-inflate` volcanic/0.20, `thermal-scree`
arctic/0.20, `thermal-schott` temperate/0.18.

Why one per variant rather than one per example: the point of the change is to
make the completed CPU family visible, and three examples expose several
variants through separate panels. Why one panel per entry rather than every
panel: the gallery contract is one specimen per entry, and a single
representative panel keeps the gallery readable.

### 2. 256² with recorded talus and iteration adaptations

Every entry uses 256². `ex_thermal_olsen` and `ex_thermal_ridge` target 1024²,
so their talus is scaled from `4/1024` and `2/1024` to `4/256` and `2/256`.
`thermal_conserve`, `thermal_flatten`, and `thermal_olsen` use 100 iterations
instead of 500. In Node the cost drops from 13–35 s to 0.7–4.4 s; in Chrome the
same recipes take roughly 19–98 s, which is accepted for this change. The
result keeps the same landform structure while retaining slightly more relief
than the 500-iteration reference (`thermal_olsen` roughness changes by under 3%
after 25 iterations; conserve and flatten smooth more gradually).

The other seven variants keep their example iteration counts (`thermal_rib`
stays at 5). `thermal_flatten` uses the
example's second panel (`sigma_sup = 0.05`) because it exercises both
relaxation terms; `thermal_schott` uses the elevation-aligned talus panel
because it demonstrates the per-cell talus input.

Alternatives considered: 128² with 25 iterations would bring the three slow
recipes to roughly 1–6 s in-browser but would change both the grid shape and
the iteration budget; 500 iterations at 256² is faithful but multiplies the
already-accepted browser cost. The chosen compromise keeps the source grid
shape and only records a performance deviation, with the slow browser cost
explicitly accepted.

### 3. Metadata rename from `resolutionNote` to `adaptationNote`

The existing `DemoMeta.resolutionNote` is rendered as "Resolution" in the
detail sidebar. Iteration-budget notes are not resolution changes, so rename
the field to `adaptationNote` and render the label "Adaptation". Only
`stream-erosion` currently sets the field; update it and the catalog test.
This keeps the existing single-note presentation without adding a second
metadata field.

### 4. Registration and previews

Add the ten modules to `site/src/demos/index.ts` and their `?raw` imports to
`site/src/demos/sources.ts`. `scripts/capture-thumbnails.mjs` discovers demo
files from the directory, so it needs no change: run it once after the site
build to write ten new WebPs and refresh `manifest.json` for all nineteen
entries. `previewVersion` stays 3 because the viewport, lighting, material, and
capture contract do not change.

### 5. Validation

Update `tests/showcase-catalog.test.ts`:

- replace the hardcoded `toHaveLength(9)` with the new count (19) and keep the
  unique-id, kebab-case, family, metadata, source-file, and reference-image
  checks;
- add a thermal-coverage test that collects every entry's `features` and
  asserts all ten thermal API names are present, naming any missing variant;
- keep the feature-allowlist, deterministic double-build, finite-range, and
  diorama checks unchanged. The suite's existing 60 s timeout already covers
  the slowest recipe.

The preview test is id-agnostic and will cover the new entries once the
manifest is regenerated.

## Risks / Trade-offs

- **Thermal effects can read weakly in 3D.** `thermal_rib` (5 iterations) and
  `thermal_schott` are subtle, and erosion smooths rather than adds features.
  → Prototype all ten early, tune palette and relief per entry, and keep each
  entry's reference image link so the comparison is explicit.
- **Reduced iteration counts are a fidelity deviation.** → Record both source
  and shipped values in `adaptationNote`; the detail page shows the note, and
  the spec now permits recorded performance adaptations.
- **Catalog tests get slower.** The three 100-iteration recipes add roughly
  10 s per full build pass in Node. → The existing 60 s test timeout is
  sufficient; keep the double-build determinism check rather than weakening
  the spec.
- **Browser generation is slow and accepted.** The three O(64) recipes take
  roughly 19–98 s in Chrome, and headless capture is slower still. → Keep the
  worker and loading state, capture previews headful with an extended timeout,
  and leave kernel optimization or threading to a separate change.
- **Preview capture needs a browser.** → Use the existing script and committed
  WebPs; CI continues to validate hashes without launching a browser.
- **Ten more previews increase the gallery payload.** → Each WebP is small and
  the gallery only loads preview images; no wasm is requested on the gallery
  route.

## Migration Plan

1. Add the ten demo modules and register them; run the catalog test against the
   local wasm artifact.
2. Build the site, capture all previews headful with the extended timeout, and
   commit the new WebPs and manifest.
3. Verify the gallery filters, detail pages, code tabs, and adaptation notes.
4. Rollback is a revert of the demo modules, registrations, previews, metadata
   rename, test changes, and README note; no data or API migration is involved.
