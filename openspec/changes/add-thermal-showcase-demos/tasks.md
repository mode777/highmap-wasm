# Tasks

## 1. Metadata contract

- [x] 1.1 Rename `resolutionNote` to `adaptationNote` in `site/src/demos/types.ts`, update the only current user (`site/src/demos/stream-erosion.ts`), change the detail-page label in `site/src/demo-page.ts` from "Resolution" to "Adaptation", and update the catalog test reference; verify `npx tsc --noEmit` in `site/` and `npx vitest run tests/showcase-catalog.test.ts` pass.
- [x] 1.2 Update the demo-contract section of `site/README.md` so the field is documented as an adaptation note that records source and shipped values; verify the README example and rules match the renamed field.

## 2. Thermal demo modules

- [x] 2.1 Add `site/src/demos/thermal.ts` and `site/src/demos/thermal-conserve.ts` from the two `ex_thermal` result panels, using fbm `PERLIN` 256² `[4,4]` seed 1, talus `0.1/256`, and 500/100 iterations respectively; record the conserve iteration adaptation in `adaptationNote`, register both in `index.ts` and `sources.ts`, then verify both build through the wasm artifact with the declared shape and finite, non-degenerate output.
- [x] 2.2 Add `site/src/demos/thermal-auto-bedrock.ts` and `site/src/demos/thermal-rib.ts` from their single-panel examples, using fbm `PERLIN` 256² `[2,2]` seed 1, talus `0.5/256` for auto-bedrock, and 5 iterations for rib; register both and verify both build deterministically with the declared shape.
- [x] 2.3 Add `site/src/demos/thermal-flatten.ts` from the `sigma_sup = 0.05` panel and `site/src/demos/thermal-olsen.ts` from its single result panel, using 100 iterations and recording both the iteration adaptation and, for olsen, the `4/1024` → `4/256` talus scaling in `adaptationNote`; register both and verify both build in under 10 s each and produce finite, non-degenerate output.
- [x] 2.4 Add `site/src/demos/thermal-ridge.ts`, `site/src/demos/thermal-inflate.ts`, and `site/src/demos/thermal-scree.ts` from the three `ex_thermal_ridge` result panels, keeping 500 iterations and recording the `2/1024` → `2/256` talus scaling; register all three and verify each builds with the declared shape and finite, non-degenerate output.
- [x] 2.5 Add `site/src/demos/thermal-schott.ts` from the elevation-aligned talus panel: fbm `PERLIN` 256² `[2,2]` seed 1, `remap`, clone the grid with `scale(z, 1)`, remap the clone to `[1/256, 2/256]`, then `thermalSchott` with 500 iterations; register it and verify it builds with the declared shape and finite, non-degenerate output.

## 3. Catalog validation

- [x] 3.1 Update `tests/showcase-catalog.test.ts` to expect 19 entries instead of 9, keep the unique-id, kebab-case, family, source-path, reference-image, and palette checks, and rename the test description accordingly; verify the suite passes.
- [x] 3.2 Add a thermal-coverage test that collects every entry's declared `features` and asserts `thermal`, `thermalAutoBedrock`, `thermalConserve`, `thermalFlatten`, `thermalOlsen`, `thermalRib`, `thermalInflate`, `thermalRidge`, `thermalScree`, and `thermalSchott` are all present, naming any missing variant; verify the test passes and fails when one thermal entry is temporarily removed.
- [x] 3.3 Verify the existing feature-allowlist, deterministic double-build, finite-range, and diorama tests still pass for all 19 entries within the configured 60 s timeout; record the slowest recipe in the task notes if it exceeds 30 s.

## 4. Previews and site verification

- [x] 4.1 Add headful and extended-timeout options to `site/scripts/capture-thumbnails.mjs` for the slow thermal recipes, build the site, and run the capture headful; verify the script writes ten new WebPs and a manifest with 19 entries, then run `npx vitest run tests/showcase-previews.test.ts` to confirm every hash matches its module.
- [x] 4.2 Verify the gallery lists 19 cards, the `erosion` filter shows all ten thermal entries plus stream erosion, and each thermal detail page shows its source example, panel, reference image, adaptation note where recorded, code tab, and generation stats.
- [x] 4.3 Add a short "Thermal coverage" section to `site/README.md` listing the ten entries and their recorded adaptations; verify the documented preview and build commands still run as written.

## 5. Integration verification

- [x] 5.1 Run the full local gate from the tested artifact: root `npm test`, root `npm run check`, `npm run check` and `npm run build` in `site/`, and a `SITE_BASE=/highmap-wasm/` site build; verify all pass.
- [x] 5.2 Inspect the final diff for scope: no upstream C++ edits, no API or wasm-build changes, no site files in `npm pack --dry-run --json`, and no generated build output committed; verify with `git diff --stat`, `git status`, and the package-boundary test.

## Workflow follow-up

- Archive the change after review and the project's verification requirements are satisfied.
- Commit the archived change, synced main specs, and implementation together with the `mode777` git identity, then push to `main` per the project's apply and archive guidance.
