# Proposal

## Why

The five hydraulic functions ported in the previous change — `hydraulicMcdonald`,
`hydraulicMcdonaldMultiscale`, `hydraulicSchott`, `hydraulicSchottErosion`, and
`hydraulicVpipes` — are now part of the shipped wasm API, but the showcase still
presents stream erosion as its only hydraulic example. Visitors cannot see the
new landforms, compare them with the original HighMap renders, or trace them
back to their C++ recipes, so the port's most distinctive additions remain
invisible. This change completes the showcase work that the hydraulic port
explicitly deferred.

## What Changes

- Add five example-faithful showcase entries, one per newly bound function:
  `hydraulic-mcdonald`, `hydraulic-mcdonald-multiscale`, `hydraulic-schott`,
  `hydraulic-schott-erosion`, and `hydraulic-vpipes`.
- Source each entry from the original C++ sample panel that demonstrates it:
  `examples/ex_hydraulic_mcdonald` (base and multiscale panels),
  `examples/ex_hydraulic_schott` (base and erosion-only panels), and
  `examples/ex_hydraulic_vpipes_gpu` (single panel). Preserve each example's
  noise type, seed, and panel structure; the existing reference renders under
  `docs/images/` are the visual check.
- Keep the entries within a sensible browser budget. Where a sample's grid,
  sample counts, or iteration counts would make the specimen too slow for CI
  and the detail page, ship web-sized values and record both the source and
  shipped values in `adaptationNote`, following the established convention.
- The vpipes sample's `bulkify` pre-pass is not exposed by the wasm API. The
  vpipes entry omits that step and records the omission in its adaptation note
  instead of inventing an equivalent or leaving it undocumented.
- Register the five modules in `site/src/demos/index.ts` and
  `site/src/demos/sources.ts`, capture previews through the existing diorama
  pipeline, and regenerate the preview manifest.
- Extend catalog validation: raise the expected entry count from 19 to 24 and
  assert that every newly exposed hydraulic function is represented, mirroring
  the existing thermal-coverage assertion. Keep the existing source-existence,
  determinism, finite-range, diorama, and preview checks.
- Document the hydraulic coverage and its recorded adaptations in
  `site/README.md`.

Explicitly out of scope:

- New wasm bindings, CPU ports, numerical changes, or any C++/API edit. This is
  a showcase, test, and documentation change only.
- A `hydraulicMusgrave` entry. It MUST remain excluded until its numerical
  stability is fixed, as the current catalog requirement states.
- Interactive parameter controls, side-by-side before/after comparison, or seed
  reroll; each entry remains a single static specimen.
- Replacing existing entries, changing the diorama rendering path, or touching
  the npm package surface and Pages workflow.

## Capabilities

### New Capabilities

None. The showcase already owns the catalog, traceability, validation, and
preview behavior; this change extends its coverage rather than introducing new
system behavior.

### Modified Capabilities

- `showcase-site`: the catalog gains complete hydraulic-family coverage, one
  example-faithful entry per newly exposed hydraulic function, in addition to
  the existing thermal coverage. The example-traceability requirement is
  broadened so a recorded adaptation may also cover a preprocessing step the
  wasm API cannot express (the vpipes `bulkify` pass), not only grid, talus, and
  iteration changes.

## Impact

- New demo modules under `site/src/demos/`, registered in
  `site/src/demos/index.ts` and `site/src/demos/sources.ts`.
- Five new WebP previews and an updated
  `site/src/assets/previews/manifest.json`.
- `tests/showcase-catalog.test.ts`: entry count 19 → 24 plus a hydraulic-coverage
  assertion. Each recipe is built twice by CI and again on its detail page, so
  the shipped budgets must keep the existing 60 s test timeout and page load
  acceptable.
- `site/README.md` gains a hydraulic coverage section describing the entries
  and their recorded adaptations.
- No changes to `HighMap/wasm/`, `src/api/`, the npm tarball, or the Pages
  workflow; the five functions already exist and the site only consumes them.
