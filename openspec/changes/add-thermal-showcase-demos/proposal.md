# Proposal

## Why

The thermal erosion family is now available as CPU-only wasm functions, but the
showcase still presents stream erosion as its only erosion example. Visitors
cannot see the newly ported thermal variants, compare their landforms, or trace
them back to the original HighMap examples. Adding the thermal panels turns the
completed port into visible, inspectable evidence.

## What Changes

- Add ten example-faithful showcase entries, one for each CPU thermal variant
  exposed by the API: `thermal`, `thermalConserve`, `thermalAutoBedrock`,
  `thermalFlatten`, `thermalOlsen`, `thermalRib`, `thermalRidge`,
  `thermalInflate`, `thermalScree`, and `thermalSchott`.
- Source each entry from its original C++ example and record the ported panel:
  `ex_thermal` (thermal, conserve), `ex_thermal_auto_bedrock`,
  `ex_thermal_flatten` (the `sigma_sup = 0.05` panel), `ex_thermal_olsen`,
  `ex_thermal_rib`, `ex_thermal_ridge` (ridge, inflate, scree), and
  `ex_thermal_schott` (the elevation-aligned talus panel).
- Keep every entry at 256² and preserve the example's noise type, seed,
  `kw`, and erosion parameters, with these recorded adaptations:
  - `thermal_olsen` and `thermal_ridge`/`thermal_inflate`/`thermal_scree`
    scale their talus from the example's 1024² grid to 256² (`4/256` and
    `2/256`).
  - `thermal_conserve`, `thermal_flatten`, and `thermal_olsen` use 100
    iterations instead of 500 because their CPU kernels are O(64) per cell;
    in a normal desktop browser all ten thermal entries generate in under 5 s
    per specimen, while the other seven variants keep their example iteration
    counts.
- Register the ten modules in the demo catalog and the raw-source map, and
  recapture all gallery previews plus the manifest.
- Update catalog validation: replace the fixed count of nine, assert that every
  thermal API variant is represented, and keep the existing source,
  determinism, finite-range, diorama, and preview checks.
- Rename the demo metadata field `resolutionNote` to `adaptationNote` (and the
  detail-page label to "Adaptation") so iteration-budget notes are not
  mislabeled as resolution changes.
- Document the new thermal coverage and its recorded adaptations in
  `site/README.md`.

Explicitly out of scope:

- New wasm bindings, CPU ports, or numerical changes to the thermal kernels.
- Interactive parameter controls, side-by-side before/after comparison, or
  seed reroll; each entry remains a single static specimen.
- Replacing the existing nine entries or changing the diorama rendering path.
- Changes to the npm package surface or the deployed Pages workflow.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `showcase-site`: the catalog gains complete thermal-family coverage, and the
  example-traceability requirement now permits recorded performance adaptations
  (iteration budget) in addition to resolution and talus scaling.

## Impact

- New demo modules under `site/src/demos/`, plus registration in
  `site/src/demos/index.ts` and `site/src/demos/sources.ts`.
- New WebP previews and an updated `site/src/assets/previews/manifest.json`.
- `tests/showcase-catalog.test.ts` loses the hardcoded nine-entry count and
  gains a thermal-coverage assertion; the existing 60 s test timeout covers the
  three 100-iteration recipes.
- `site/README.md` documents the thermal entries and their adaptations.
- No upstream C++ edits, no API or wasm-build changes, and no files added to the
  npm tarball.
