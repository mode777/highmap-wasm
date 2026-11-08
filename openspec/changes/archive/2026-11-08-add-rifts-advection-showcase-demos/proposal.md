# Proposal

## Why

The three functions ported in `add-rifts-advection-cpu-ports` — `rifts`,
`advectionParticle`, and `advectionWarp` — are bound and shipped in the wasm API,
but the showcase still has no specimen for any of them. Visitors cannot see rift
carving or particle/warp field advection, compare the landforms with the original
HighMap renders, or trace them back to their C++ recipes. That change explicitly
deferred the showcase work ("Not in this change: showcase demos"); this change
completes it, following the thermal and hydraulic demo precedent.

## What Changes

- Add three example-faithful showcase entries, one per newly bound function:
  `rifts`, `advection-particle`, and `advection-warp`.
- Source each entry from the C++ sample panel that demonstrates it:
  `examples/ex_rifts/ex_rifts.cpp` (post-rifts `z2` panel — the example that
  exercises the ported `hmap::gpu::rifts`; `examples/ex_rift/ex_rift.cpp`
  demonstrates the different, unported single-rift primitive `hmap::rift` and is not
  the reference), `examples/ex_advection_particle/ex_advection_particle.cpp` (the
  10-iteration advected field `n` panel), and
  `examples/ex_advection_warp/ex_advection_warp.cpp` (the warped terrain `za`
  panel). Preserve each example's 256² grid, seeds, noise recipes, and parameters;
  the reference renders under `docs/images/` are the visual check.
- The samples' budgets are already web-sized (256² grids, 10000 particles, 10
  iterations, a single rifts pass), so no budget adaptation is expected; a timed
  smoke run confirms the specimens stay inside the CI and detail-page budgets, with
  a recorded fallback ladder if they do not.
- `ex_rifts` applies `zeroed_edges` before `remap`, and the wasm API does not expose
  it. The rifts entry omits that step and records the omission in its adaptation
  note instead of inventing an equivalent — the vpipes `bulkify` precedent.
- The advection entries are the first demos of new-grid API functions (`advectionParticle`
  and `advectionWarp` return a fresh `HArray` instead of mutating in place); their
  `build` functions free the result wrapper after `copyOut` in addition to the
  inputs.
- Register the three modules in `site/src/demos/index.ts` and
  `site/src/demos/sources.ts`, capture previews through the existing diorama
  pipeline, and regenerate the preview manifest.
- Extend catalog validation: raise the expected entry count from 24 to 27 and assert
  that `rifts`, `advectionParticle`, and `advectionWarp` are each represented,
  mirroring the existing thermal and hydraulic coverage assertions. Keep the
  existing source-existence, determinism, finite-range, diorama, and preview checks.
- Document the rifts/advection coverage and the recorded `zeroed_edges` omission in
  `site/README.md`.

Explicitly out of scope:

- New wasm bindings, CPU ports, numerical changes, or any C++/API edit. The three
  functions already exist; this is a showcase, test, and documentation change only.
- The deferred rifts/advection overloads (rifts' noise-offset and mask inputs,
  particle's multi-field and explicit `dx`/`dy` forms); the demos use only the bound
  surface.
- Interactive parameter controls, side-by-side panels, or seed rerolls; each entry
  remains a single static specimen.
- Replacing existing entries, changing the diorama rendering path, or touching the
  npm package surface and Pages workflow.

## Capabilities

### New Capabilities

None. The showcase already owns the catalog, traceability, validation, and preview
behavior; this change extends its coverage to the newly bound family.

### Modified Capabilities

- `showcase-site`: the catalog gains complete rifts/advection coverage, one
  example-faithful entry per newly exposed function, in addition to the existing
  thermal and hydraulic coverage.

## Impact

- New demo modules under `site/src/demos/`, registered in
  `site/src/demos/index.ts` and `site/src/demos/sources.ts`.
- Three new WebP previews and an updated `site/src/assets/previews/manifest.json`.
- `tests/showcase-catalog.test.ts`: entry count 24 → 27 plus a rifts/advection
  coverage assertion. Each recipe is built twice by CI and again on its detail page,
  so the shipped recipes must keep the existing 60 s test timeout and page load
  acceptable (projections are all well under 1 s per build).
- `site/README.md` gains a rifts/advection coverage section describing the entries
  and the recorded `zeroed_edges` omission.
- No changes to `HighMap/wasm/`, `src/api/`, the npm tarball, or the Pages workflow;
  the three functions already exist and the site only consumes them.
