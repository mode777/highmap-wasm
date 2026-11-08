# Design

## Context

See `proposal.md` for motivation. The three rifts/advection functions are already
bound and exported by the wasm artifact; the showcase currently has 24 entries and
none of them exercises the family. This change adds catalog entries, previews, test
coverage, and documentation only.

The demo contract is fixed. Each module under `site/src/demos/` exports a
`DemoDefinition` with metadata and a pure `build(hmap)` that owns every `HArray` it
creates and frees them in `finally`, returning `copyOut(...)`. Metadata drives the
gallery, detail page, code tab, and preview manifest. The root catalog test builds
every recipe twice in the determinism test and once more in the diorama test, with a
60 s timeout per test. Previews are captured from the real detail page (640 x 400
CSS pixels at device scale factor 2) and the manifest hashes each module's source;
`previewVersion` is 3.

One contract difference from all existing demos: `advectionParticle` and
`advectionWarp` return a NEW grid and leave both inputs untouched (the first
new-grid functions in the erosion/transform surface, AD-027), while `rifts` mutates
in place like the erosion family. The advection demos must therefore free the fresh
result wrapper after `copyOut` in addition to the inputs.

The reference recipes are `examples/ex_rifts/ex_rifts.cpp` (base `z1` and post-rifts
`z2` panels), `examples/ex_advection_particle/ex_advection_particle.cpp` (`z`,
single-pass `za`, and 10-iteration advected field `n` panels), and
`examples/ex_advection_warp/ex_advection_warp.cpp` (`z`, warped terrain `za`, and
warped noise field `zb` panels). The reference renders are `docs/images/ex_rifts.png`,
`docs/images/ex_advection_particle.png`, and `docs/images/ex_advection_warp.png`.

The only measured rifts/advection wasm timings are in
`docs/rifts-advection-parity.md` section "Cost" (single-threaded Node 26 on an Intel
Core Ultra 5 335, medians of 20 calls at 64²): `rifts` 1.05 ms, `advectionWarp`
1.65 ms, and `advectionParticle` 0.21 ms with 256 particles and 1 iteration. None
of the three examples needs a budget reduction at its source 256² size; only the
particle entry has a nontrivial projection (decision 2).

Constraints: no C++ or wasm API edits, no output-map rendering changes, one panel
per entry, and the catalog count moves from 24 to 27 while keeping the existing
test timeout.

## Goals / Non-Goals

**Goals:**

- Three example-faithful entries, one per newly exposed function, with the single
  API gap (`zeroed_edges`) recorded in `adaptationNote`.
- A catalog that proves the rifts/advection family is covered, keeps the existing
  deterministic, finite, diorama, and preview guarantees, and renders each specimen
  in well under a second in a normal browser.
- Previews, README coverage, and tests consistent with the thermal and hydraulic
  precedent.

**Non-Goals:**

- Changing the worker, diorama geometry, gallery layout, palette definitions, or
  preview capture contract.
- Reproducing the examples' base-noise-only or single-pass panels.
- Adding interactive controls, side-by-side panels, or seed rerolls.
- Changing the npm package surface or the Pages workflow.
- Re-tuning the rifts/advection kernels, changing their parameter semantics, or
  binding the deferred overloads (multi-field advection, explicit `dx`/`dy`, rifts
  mask/noise-offset inputs).

## Decisions

### 1. Three entries, one per function

Each entry is a straight translation of one example panel. `rifts` is an `erosion`
entry (upstream places it in the erosion family); the two advection entries are
`shaping` entries (upstream transforms, like the existing `warp` demo). The table is
the contract; palette and relief are initial art direction. The `advectionWarp` call
passes the example's non-default `advectionLength` and `valuePersistence`
explicitly; the `advectionParticle` call passes the example's particle count and
`iterations` explicitly. All other parameters rely on the C++ defaults.

| id | Source example / panel | Grid, seed | Base noise | Shipped call | Reference image | Palette / relief | Projected single build |
|---|---|---|---|---|---|---|---|
| `rifts` | `examples/ex_rifts/ex_rifts.cpp`, post-rifts `z2` panel | 256², 0 | `noiseFbm(PERLIN, 256², [4,4], 0)` then `remap`; no `zeroed_edges` | `rifts(z, [8, 1.5], 30, 0.1, 1)` then `remap(z)` | `docs/images/ex_rifts.png` | volcanic / 0.20 | ~0.1 s |
| `advection-warp` | `examples/ex_advection_warp/ex_advection_warp.cpp`, warped terrain `za` panel | 256², 1 | `noiseFbm(PERLIN, 256², [2,2], 1)` then `remap` | `advectionWarp(z, z, { advectionLength: 0.05, valuePersistence: 0.96 })` | `docs/images/ex_advection_warp.png` | temperate / 0.20 | ~0.1 s |
| `advection-particle` | `examples/ex_advection_particle/ex_advection_particle.cpp`, advected field `n` panel | 256², 1 | elevation `noiseFbm(PERLIN, 256², [2,2], 1)` then `remap`; advected field `noiseFbm(PERLIN, 256², [32,32], 1)` | `advectionParticle(z, n0, 10000, 1, { iterations: 10 })` | `docs/images/ex_advection_particle.png` | arid / 0.20 | <= 0.6 s |

Titles follow the existing noun-phrase pattern: `Rifts`, `Advection warp`, and
`Particle advection`. Each entry's `features` list is `noiseFbm`, `remap`, and its
function (the particle entry remaps only the elevation input, matching the example).

Panel choice rationale: `advectionWarp` ports the example's headline `za` panel,
where the terrain is warped by its own gradient — the effect survives as an
elevation specimen. `advectionParticle` ports the 10-iteration `n` panel rather
than the single-pass `za` panel, because one pass at 10000 particles is visually
close to its input, while the advected `[32,32]` field shows the multi-iteration
transport the function is distinctive for, and exercises the `iterations` option.
`rifts` ports the `z2` panel; the `z1` panel is base noise, already represented by
the noise demos. Alternative: one entry per example panel was rejected because the
gallery contract is one specimen per entry and one per function is the coverage
requirement.

Adaptation notes, recorded verbatim in metadata:

- `rifts`: "The example computes 1 + fbm, then zeroed_edges, then remap; the wasm
  API does not expose zeroed_edges, so that pre-pass is omitted and no substitute is
  applied, and the constant offset is subsumed by the remap. Grid 256², base seed
  0, the rifts parameters (kw [8, 1.5], angle 30, amplitude 0.1, rifts seed 1), the
  default rifts options, and the post-rifts remap are unchanged."
- `advection-warp`: no note. Grid 256², seed 1, the [2,2] fbm recipe, and the
  example's advection length 0.05 and value persistence 0.96 are shipped verbatim.
- `advection-particle`: no note. Grid 256², seed 1, both fbm recipes, 10000
  particles, and the 10 iterations are shipped verbatim; the iterations chain and
  post-filter follow the C++ defaults.

The `meta.seed` field records each recipe's base-noise seed (0 for `rifts`, 1 for
both advection entries); the rifts entry's own RNG seed (1, from the example's
`++seed`) is part of the shipped call, and the particle entry passes the example's
seed 1 to the elevation, the advected field, and the walk.

### 2. Budgets: source values are already web-sized, with a timed smoke and a particle-only fallback

Only the particle entry has a nontrivial projection. Scaling the 64² probes: warp
and rifts are per-cell work (about 1.65 ms and 1.05 ms at 64², so roughly 26 ms and
17 ms at 256²), and the particle walk cost scales with particles × iterations ×
steps, where the step count per walk grows with 1 / grid spacing (4x from 64² to
256²): 256 particles for 1 iteration cost 0.21 ms at 64², so 10000 particles × 10
iterations at 256² projects to roughly 0.3–0.6 s including the per-iteration post
filter. All three sit well inside the under-5 s detail-page target used by the
previous families.

Runtime validation before capture:

1. Run the catalog test with the verbose reporter locally and in CI:
   `npx vitest run tests/showcase-catalog.test.ts --reporter=verbose`. It prints
   per-recipe durations and exercises the double build plus the diorama build.
2. Record the three rifts/advection durations in the change tasks. If CI logs are
   unavailable, run a one-off Node timing of the three `build(hmap)` calls and
   record the same table.
3. No wall-clock assertion is committed: the CI gate remains the existing 60 s
   per-test timeout, and the recorded durations are the review signal.

Fallback ladder, applied only if a measured single build exceeds its trigger:

| id | Trigger (single build) | Action | Resulting projection |
|---|---|---|---|
| `advection-particle` | > 2 s | iterations 10 -> 5; then nparticles 10000 -> 5000 | ~0.25 s; ~0.15 s |
| `advection-warp` | > 0.5 s | none expected; last resort grid 256² -> 128² with a recorded adaptation note | ~40 ms |
| `rifts` | > 0.5 s | none expected; last resort grid 256² -> 128² with a recorded adaptation note | ~45 ms |

Every fallback updates `adaptationNote` with the source and shipped values. Seeds
and noise recipes are never changed to buy time (see decision 6). Grid reduction is
a last resort for warp and rifts because their shape parameters (`kw`, angle,
advection length) are per-cell recipes and the source grid is already web-sized.

Alternative: reduce the particle budget preemptively. Rejected: the projection is
already well inside budget and the example's values are the traceability reference.

### 3. New-grid ownership in `build`

The advection demos are the first whose API calls return a fresh `HArray` instead
of mutating an input (AD-027). Their `build` functions follow the same ownership
rule with one more wrapper to free: create the inputs, call the function, `copyOut`
the result, and free the result and the inputs in `finally`. The inputs are never
mutated by the advection calls (pinned by the port's input-immutability
invariants), so `copyOut(result)` is the only data transfer. The rifts demo keeps
the in-place pattern: `rifts` mutates and returns its input wrapper, which is freed
after `copyOut`.

Alternative: reuse the result wrapper as the next call's input. Not needed — no
demo chains advection calls.

### 4. Rifts `zeroed_edges` omission

The `ex_rifts` example builds `z1 = 1 + fbm`, then `hmap::zeroed_edges(z1)`, then
`remap(z1)`, and carves `z2 = z1` with `gpu::rifts`. The wasm API exposes neither
`zeroed_edges` nor an equivalent border primitive, so the entry omits the pre-pass
and records the omission in its adaptation note; it does not hand-roll a border
clamp in the demo module, because that would put preprocessing outside the API
surface the showcase demonstrates. The `1.f +` constant offset is affine and
subsumed by the immediately following `remap`, so the omission is strictly the
border zeroing. Everything downstream — the `z2` copy, the rifts arguments
(`kw [8, 1.5]`, angle 30, intensity 0.1, seed 1), all default rifts options, and
the post-rifts `remap` — is shipped verbatim.

Consequence: the specimen's border cells keep their fbm values instead of being
pinned to zero, so the specimen's rim differs from the reference render's rim.
That is the recorded, visible cost of the API gap.

Alternative: approximate the border zeroing in JS by writing zeros into the copied
grid. Rejected for the same reason as the vpipes `bulkify` substitute: it invents
a preprocessing chain the API does not offer and misrepresents the shipped surface.

### 5. Gallery placement and registration

Add three modules named exactly after their ids under `site/src/demos/`. Register
them in `site/src/demos/index.ts` in this order: `advection-warp` and
`advection-particle` immediately after `warp` (the shaping block grows with its
transform-adjacent siblings), and `rifts` at the end of the list, after
`thermalSchott` (the erosion block keeps the hydraulic and thermal families
contiguous and gains rifts as its newest member). Register the same order in
`site/src/demos/sources.ts` with `?raw` imports.

Alternative: place `rifts` next to the hydraulic block, before `thermal`. Rejected:
it buries the new entry mid-list and splits the established example-family order.

### 6. Seeds and noise are never adapted

Only the particle budget may deviate (iterations and particle count), and grid
reduction is the recorded last resort for any entry. The noise type, `kw`, seeds,
octaves, weight, and `remap` calls are preserved exactly; the `zeroed_edges`
omission is an API gap, not a noise change. Palettes and relief are art direction
and do not alter the recipe.

This keeps the traceability claim meaningful: any visual difference from the
reference is either the recorded border omission or the known CPU-versus-GPU
difference, never a hidden reseed or re-noise.

Alternative: tweak seeds to find more striking specimens. Rejected: it would break
the comparison with `docs/images/`.

### 7. Catalog validation, README, and previews

**Catalog test.** Raise the hardcoded count from 24 to 27, rename the first test to
"has twenty-seven ...", and add a rifts/advection coverage test mirroring the
thermal and hydraulic ones: collect every entry's `features` and assert that
`rifts`, `advectionParticle`, and `advectionWarp` are each declared, naming the
missing function on failure. Keep the source-existence and reference-image checks,
the deterministic double build, the finite-range check, the feature-allowlist
check, and the diorama check unchanged. The feature-allowlist check already passes
for the three functions because they exist on the initialized API object.

**README.** Add a `## Rifts and advection coverage` section next to the thermal and
hydraulic ones. List the three entries, note that all three keep the example's 256²
grid and source budgets, and record the `zeroed_edges` omission on the rifts entry
and the new-grid semantics of the two advection demos.

**Previews.** Run `cd site && npm run build && npm run thumbnails:capture`. The
script discovers demo modules by filename and needs no change: it writes three new
`src/assets/previews/<id>.webp` files and refreshes `manifest.json` for all 27
entries. Commit the three WebPs and the manifest. `previewVersion` stays 3 because
the viewport, lighting, material, and capture contract do not change. If headless
capture of the particle recipe stalls, use the documented `CAPTURE_HEADFUL=1` and
`CAPTURE_TIMEOUT_MS=300000` run.

## Risks / Trade-offs

- **The particle projection is extrapolated from a 64², 256-particle probe.** Walk
  length scales with 1/grid spacing, so the 256² estimate carries the most
  uncertainty of the three. → The fallback ladder in decision 2 cuts iterations
  first, then particles; the CI gate stays the 60 s timeout.
- **The advected `[32,32]` field is low-frequency.** The `advection-particle`
  specimen is a stretched, smooth field rather than rugged terrain. → Prototype
  before preview capture and tune palette and relief; if it still reads flat,
  relief may be raised within art direction, but the recipe stays verbatim.
- **The CPU ports do not match the GPU renders pixel for pixel.** Rifts carries
  the documented sin-hash Voronoi noise (parity report gap 8), and the particle
  walk's aggregate is order-dependent on GPU. → Previews are illustrative
  specimens, not parity evidence; `docs/rifts-advection-parity.md` is the parity
  record, and the detail page links the source example and shows the adaptation
  note.
- **The rifts specimen's rim differs from the reference** because of the recorded
  `zeroed_edges` omission. → Stated in the adaptation note on the detail page.
- **CI runners are slower than the measured laptop.** → All projections are
  sub-second locally, leaving more than an order of magnitude of headroom under
  the 60 s per-test timeout; the recorded durations are the review signal.

## Migration Plan

1. Add the three modules following the `warp` / `stream-erosion` patterns (decision
   3 for the new-grid free pattern); register them in `index.ts` and `sources.ts`;
   run the catalog test against the local wasm artifact with the count already
   raised.
2. Run the timed smoke (decision 2). Apply any fallback and update the affected
   adaptation note before proceeding.
3. Build the site, capture previews, commit the three WebPs and the regenerated
   manifest, and verify the gallery order, detail pages, code tabs, and adaptation
   notes.
4. Update `site/README.md` and the catalog test; run the root suite.
5. Rollback is a revert of the three modules, registrations, previews, manifest,
   test, and README edits; no API, data, or package migration is involved.
