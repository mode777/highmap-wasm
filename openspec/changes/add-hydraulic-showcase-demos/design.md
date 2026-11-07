# Design

## Context

See `proposal.md` for motivation. The five hydraulic functions are already
bound and exported by the wasm artifact; the showcase currently has 19 entries
and only `stream-erosion` represents the hydraulic family. This change adds
catalog entries, previews, test coverage, and documentation only.

The demo contract is fixed. Each module under `site/src/demos/` exports a
`DemoDefinition` with metadata and a pure `build(hmap)` that owns and frees
every `HArray` in `finally` and returns `copyOut(z)`. The worker returns exactly
one `Float32Array` heightmap; there is no secondary-map channel. Metadata drives
the gallery, detail page, code tab, and preview manifest. The root catalog test
builds every recipe twice in the determinism test and once more in the diorama
test, with a 60 s timeout per test. Previews are captured from the real detail
page (640 x 400 CSS pixels at device scale factor 2) and the manifest hashes each
module's source; `previewVersion` is 3.

The reference recipes are
`examples/ex_hydraulic_mcdonald/ex_hydraulic_mcdonald.cpp` (base and multiscale
panels), `examples/ex_hydraulic_schott/ex_hydraulic_schott.cpp` (full and
erosion-only panels), and
`examples/ex_hydraulic_vpipes_gpu/ex_hydraulic_vpipes_gpu.cpp` (single panel).
The reference renders are `docs/images/ex_hydraulic_mcdonald0..3.png`,
`docs/images/ex_hydraulic_schott.png`, and
`docs/images/ex_hydraulic_vpipes_gpu.png`.

The only measured hydraulic wasm timings are in `docs/hydraulic-parity.md`
section "Cost": single-threaded Node 26 on an Intel Core Ultra 5 335. The relevant
probes are McDonald at 256² with 4 steps / 1024 samples / maxage 64 = 68 ms,
McDonald at 512² with 2 steps / 1024 samples / maxage 64 = 44 ms, Schott at
256² with 10 iterations = 268 ms, and vpipes at 512² with 10 iterations =
204 ms. The McDonald probe shows that cost is dominated by
`steps x samples x maxage`, not by grid size.

Constraints: no C++ or wasm API edits, no output-map rendering changes, one
panel per entry, `hydraulicMusgrave` stays excluded, and the catalog count moves
from 19 to 24 while keeping the existing test timeout.

## Goals / Non-Goals

**Goals:**

- Five example-faithful entries, one per newly exposed hydraulic function, with
  every budget deviation recorded in `adaptationNote`.
- A catalog that proves the hydraulic family is covered, keeps the existing
  deterministic, finite, diorama, and preview guarantees, and renders each
  specimen in a few seconds in a normal browser.
- Previews, README coverage, and tests consistent with the thermal precedent.

**Non-Goals:**

- Changing the worker, diorama geometry, gallery layout, palette definitions, or
  preview capture contract.
- Reproducing the examples' discharge or sediment debug renders.
- Adding `hydraulicMusgrave`, interactive controls, side-by-side panels, or
  seed rerolls.
- Changing the npm package surface or the Pages workflow.
- Re-tuning the hydraulic kernels or changing their parameter semantics.

## Decisions

### 1. Five entries, one per hydraulic function

Each entry is a straight translation of one example panel, all in the
`erosion` family. The table is the contract; palette and relief are initial art
direction. Each entry's `features` list is `noiseFbm`, `remap`, and its hydraulic
function. All calls omit optional output maps and rely on the C++ defaults for
every parameter not shown.

| id | Source example / panel | Grid, seed | Base noise | Shipped call | Reference image | Palette / relief | Projected single build |
|---|---|---|---|---|---|---|---|
| `hydraulic-mcdonald` | `examples/ex_hydraulic_mcdonald/ex_hydraulic_mcdonald.cpp`, post-erosion `z` panel | 256², 1 | `noiseFbm(PERLIN, 256², [2,2], 1)` then `remap` | `hydraulicMcdonald(z, 128, 1, { samples: 1024, maxage: 64 })` | `docs/images/ex_hydraulic_mcdonald1.png` | temperate / 0.20 | 2.2 s |
| `hydraulic-mcdonald-multiscale` | same example, `zm` multiscale panel | 512², 1 | `noiseFbm(PERLIN, 512², [2,2], 1)` then `remap` | `hydraulicMcdonaldMultiscale(z, 1, { stepsPerLevel: [256, 128, 64], samples: 512, maxage: 64 })` | `docs/images/ex_hydraulic_mcdonald3.png` | arid / 0.20 | 3.8 s |
| `hydraulic-schott` | `examples/ex_hydraulic_schott/ex_hydraulic_schott.cpp`, `z1` full-Schott panel | 256², 2 | `noiseFbm(PERLIN, 256², [2,2], 2)` then `remap` | `hydraulicSchott(z, 2 / 256, { iterations: 100 })` | `docs/images/ex_hydraulic_schott.png` | temperate / 0.18 | 2.7 s |
| `hydraulic-schott-erosion` | same example, `z2` erosion-only panel | 256², 2 | `noiseFbm(PERLIN, 256², [2,2], 2)` then `remap` | `hydraulicSchottErosion(z, { iterations: 30 })` | `docs/images/ex_hydraulic_schott.png` | arid / 0.18 | <= 0.8 s |
| `hydraulic-vpipes` | `examples/ex_hydraulic_vpipes_gpu/ex_hydraulic_vpipes_gpu.cpp`, post-erosion `z` panel | 512², 2 | `noiseFbm(PERLIN, 512², [4,4], 2, { octaves: 8, weight: 0.7 })` then `remap`; no `bulkify` | `hydraulicVpipes(z, { iterations: 50 })` | `docs/images/ex_hydraulic_vpipes_gpu.png` | volcanic / 0.20 | 1.0 s |

The `hydraulicVpipes` call passes `iterations: 50` explicitly even though it is
the C++ and JS default, so the recipe reads the same way as the thermal ones.
The multiscale call overrides the wrapper's `[512, 256, 128]` default with the
example's `[256, 128, 64]` ladder. The `hydraulicSchott` talus is the scalar
`2 / 256`; the wrapper materializes the constant grid the example builds with
`Array(shape, 2.f / shape.x)`.

Titles follow the thermal pattern: `Hydraulic erosion (McDonald)`,
`Hydraulic erosion (McDonald, multiscale)`, `Hydraulic erosion (Schott)`,
`Hydraulic erosion (Schott, erosion only)`, and `Hydraulic erosion (vpipes)`.

The McDonald pair shares seed 1 and the `[2,2]` fbm recipe, and the Schott pair
shares seed 2 and its base noise, so adjacent entries are directly comparable,
as the corresponding example panels are.

Adaptation notes, recorded verbatim in metadata:

- `hydraulic-mcdonald`: "Web-sized budget: 128 steps, 1024 samples, and maxage
  64; the example uses 256 steps, 8192 samples, and maxage 512. Grid 256², seed
  1, and all other McDonald parameters are unchanged. The reference is the
  post-erosion terrain panel, not the log-scaled discharge render."
- `hydraulic-mcdonald-multiscale`: "Web-sized budget: 512 samples and maxage 64;
  the example uses the defaults 8192 samples and maxage 512. Grid 512², seed 1,
  the [256, 128, 64] step ladder, and all other McDonald parameters are
  unchanged."
- `hydraulic-schott`: "Web-sized budget: 100 iterations; the example uses 400.
  Grid 256², seed 2, talus 2/256, and all other Schott parameters are unchanged."
- `hydraulic-schott-erosion`: no note. Its grid, seed, and 30 iterations match
  the example; the panel field distinguishes it from `hydraulic-schott`.
- `hydraulic-vpipes`: "The example applies bulkify(PRIM_CUBIC_PULSE, 1) before
  remap; the wasm API does not expose bulkify, so the pre-pass is omitted and no
  substitute is applied. Grid 512², seed 2, the fbm recipe (octaves 8, weight
  0.7), remap, and the default 50 iterations are unchanged."

Why one entry per function: the delta spec requires every hydraulic function
outside Musgrave to appear, and two examples expose two meaningful panels each.
Why one panel per entry: the gallery contract is one specimen per entry.
Alternative: one entry per example (three total) was rejected because it would
leave functions uncovered and mix panels in a single render.

### 2. Budgets derived from the parity probe, with a timed smoke and fallback

The particle budget dominates the McDonald entries; the iteration count
dominates Schott and vpipes. All projections are single-build Node times on the
reference laptop from the parity report. Using the 68 ms McDonald probe
(`4 x 1024 x 64` sample-steps at 256²) as the unit:

- `hydraulic-mcdonald`: source `256 x 8192 x 512` is 4096x the probe, about
  279 s. Shipped `128 x 1024 x 64` is 32x the probe, about 2.2 s (1/128 of the
  source work).
- `hydraulic-mcdonald-multiscale`: source `(256 + 128 + 64) x 8192 x 512` is
  7168x the probe, about 487 s. Shipped `448 x 512 x 64` is 56x the probe, about
  3.8 s (1/128 of the source work). The ladder runs at 128², 256², and 512² on
  the 512² grid; the estimate holds the example grid because the probe shows
  grid size is secondary (512² with 2 steps is cheaper than 256² with 4 steps).
- `hydraulic-schott`: 10 iterations measured 268 ms, so 400 iterations would be
  about 10.7 s and 100 iterations about 2.7 s (1/4).
- `hydraulic-schott-erosion`: no direct probe. Bounding it by the full Schott
  rate gives 30 iterations <= 0.8 s; the erosion-only kernel does less work.
- `hydraulic-vpipes`: 10 iterations measured 204 ms at 512², so the default 50
  iterations are about 1.0 s.

The five specimens total about 10.5 s per catalog pass. The slowest double
build is multiscale at about 7.6 s locally, about 23 s at a 3x CI slowdown,
well inside the 60 s per-test timeout. The slowest detail page is multiscale at
about 3.8 s locally, consistent with the thermal family's under-5 s target.

The traceability requirement's "iteration budget" is applied to the solver's
generation budget: for McDonald that is the `steps` x `samples` x `maxage`
product, and each adaptation note states all three values.

Runtime validation before capture:

1. Run the catalog test with the verbose reporter locally and in CI:
   `npx vitest run tests/showcase-catalog.test.ts --reporter=verbose`. It prints
   per-recipe durations and exercises the double build plus the diorama build.
2. Record the five hydraulic durations from that run in the change tasks. If CI
   logs are unavailable, run a one-off Node timing of the five `build(hmap)`
   calls on the reference laptop and record the same table.
3. No wall-clock assertion is committed: timings are machine-dependent. The CI
   gate remains the existing 60 s per-test timeout, and the recorded durations
   are the review signal.

Fallback rule, applied in this order:

| id | Trigger (single build) | Action | Resulting projection |
|---|---|---|---|
| `hydraulic-mcdonald` | > 3.3 s | steps 128 -> 64 | 1.1 s |
| `hydraulic-mcdonald-multiscale` | > 5.7 s | maxage 64 -> 32; if capture or diorama cost at 512² still hurts, drop grid to 256² (ladder 64/128/256) | 1.9 s; the 256² variant is also about 1.9 s but halves memory and geometry |
| `hydraulic-schott` | > 4.0 s | iterations 100 -> 60 | 1.6 s |
| `hydraulic-schott-erosion` | > 1.2 s | iterations 30 -> 20 | 0.5 s |
| `hydraulic-vpipes` | > 1.5 s | iterations 50 -> 30 | 0.6 s |

Every fallback updates `adaptationNote` with the new source and shipped values.
Seeds and noise recipes are never changed to buy time (see decision 7).

Alternative: ship the source budgets. That would take roughly 4.6 minutes for
`hydraulic-mcdonald`, 8.1 minutes for the multiscale entry, and 10.7 s for
Schott, which is unusable on the detail page and in CI. Alternative: 128² grids
with source budgets was rejected because the particle work dominates, so the
smaller grid barely helps while losing visible detail.

### 3. Heightmap-only output maps

None of the five modules allocates or passes `sediment`, `discharge`, `flow`,
`waterDepth`, `velU`, or `velV`. The examples' debug panels (notably the
log-scaled discharge render `ex_hydraulic_mcdonald2.png`) are deliberately not
reproduced; each `referenceImage` points at the terrain panel instead. The
`hydraulic-mcdonald` adaptation note states that its reference is the terrain
panel, not the discharge render.

The renderer consumes one heightmap, so output maps would add allocations and
runtime with no visible effect, and showing them would require a worker and
diorama change outside this proposal. The recipes can pass outputs later without
changing their terrain calls if map rendering is ever added.

Alternatives considered: allocate the maps and discard them (wasteful and
pointless), or fold discharge into the palette (not a heightmap, needs a new
render path, and would misrepresent the example's terrain panel).

### 4. Vpipes `bulkify` omission

The vpipes example runs
`bulkify(z, PRIM_CUBIC_PULSE, 1)` before `remap`, and the wasm API does not
expose `bulkify`. The entry omits the pre-pass and records that omission in its
adaptation note; it does not approximate it with `plateau`, `clamp`, `remap`, or
a quantization step. The rest of the recipe (grid, seed, fbm octaves and weight,
remap, and vpipes defaults) stays intact.

Alternative: substitute `plateau` or a step/quantize pass to recover some of the
blocky character. Rejected because it invents a different preprocessing chain,
would not match the example, and the modified traceability requirement explicitly
asks for omission plus a record rather than an approximation.

### 5. Gallery placement and registration

Add five modules named exactly after their ids under `site/src/demos/`. Register
them in `site/src/demos/index.ts` between `streamErosion` and `thermal`, in this
order:

`hydraulic-mcdonald`, `hydraulic-mcdonald-multiscale`, `hydraulic-schott`,
`hydraulic-schott-erosion`, `hydraulic-vpipes`.

Register the same order in `site/src/demos/sources.ts` with `?raw` imports.
Rationale: all hydraulic entries stay together at the head of the erosion block,
and the order follows the examples (McDonald base then multiscale, Schott full
then erosion-only, vpipes), so comparable specimens are adjacent.

Alternative: append the five after the thermal block in landing order. Rejected
because it splits the erosion family and buries the new hydraulic coverage after
ten thermal entries.

### 6. Catalog validation, README, and previews

**Catalog test.** Raise the hardcoded count from 19 to 24, rename the first test
to "has twenty-four ...", and add a hydraulic-coverage test mirroring the
thermal one: collect every entry's `features` and assert that
`hydraulicMcdonald`, `hydraulicMcdonaldMultiscale`, `hydraulicSchott`,
`hydraulicSchottErosion`, and `hydraulicVpipes` are each declared, naming the
missing function on failure. Keep the `hydraulicMusgrave` exclusion test, the
source-existence and reference-image checks, the deterministic double build, the
finite-range check, and the diorama check unchanged.

Alternative: rely on the entry count alone. Rejected because a count can stay at
24 while a function disappears; the coverage assertion is what enforces the
delta spec.

**README.** Add a `## Hydraulic coverage` section next to the thermal one. List
the five entries, note that multiscale and vpipes keep the example's 512² grid,
summarize the reduced budgets (McDonald steps/samples/maxage, Schott iterations),
and record the vpipes `bulkify` omission and the Musgrave exclusion.

**Previews.** Run `cd site && npm run build && npm run thumbnails:capture`. The
script discovers demo modules by filename and needs no change: it writes five
new `src/assets/previews/<id>.webp` files and refreshes `manifest.json` for all
24 entries. Commit the five WebPs and the manifest. `previewVersion` stays 3
because the viewport, lighting, material, and capture contract do not change. If
headless capture of the 512² or Schott recipes stalls, use the documented
`CAPTURE_HEADFUL=1` and `CAPTURE_TIMEOUT_MS=300000` run.

Alternative: bump `previewVersion` and recapture everything. Rejected because no
capture-contract field changed; a bump would only invalidate the manifest for no
benefit.

### 7. Seeds and noise are never adapted

Only grids and solver budgets may deviate. The noise type, `kw`, seed, octaves,
weight, persistence, lacunarity, and `remap` call are preserved exactly; the
vpipes `bulkify` omission is an API gap, not a noise change. Palettes and relief
are art direction and do not alter the recipe.

This keeps the traceability claim meaningful: any visual difference from the
reference is either a recorded budget reduction or the known CPU-versus-GPU
difference, never a hidden reseed or re-noise.

Alternative: tweak seeds to find more striking specimens. Rejected because the
modified traceability requirement requires seed preservation and any reseed
would break the comparison with `docs/images/`.

## Risks / Trade-offs

- **CI runners are slower than the measured laptop.** The worst double build is
  about 7.6 s locally and about 23 s at 3x, under the 60 s per-test timeout. →
  Keep the headroom rule in decision 2: if a CI double build approaches 30 s,
  apply that specimen's fallback and update its note.
- **Multiscale cost at 512² is the least certain estimate.** It combines three
  grid sizes and may undercount per-step grid passes. → The fallback triggers at
  5.7 s, about 50% above the 3.8 s projection; it reduces maxage first and the
  grid second.
- **Reduced budgets can read weakly.** McDonald loses half its steps, Schott
  keeps 10 of 40 full erosion/thermal/deposition cycles, and vpipes keeps its
  full 50 iterations. → Prototype all five before preview capture, compare with
  the reference panels, and tune palette and relief; if a specimen is under
  budget but too smooth, raise its budget within the ceiling rather than touching
  the noise recipe.
- **The CPU ports do not match the GPU renders pixel for pixel** (notably
  McDonald's fixed-order aggregates versus GPU atomic order, and the missing
  vpipes bulkify). → Previews are illustrative specimens, not parity evidence;
  the detail page links the source example and shows the adaptation note.
- **512² state and geometry.** vpipes keeps roughly ten full-grid float arrays
  (about 10 MB at 512²) and the multiscale McDonald run keeps about nine (about
  9 MB). Each 512² diorama has about 522k top triangles versus 130k for 256². →
  Both are well within wasm memory and render one specimen at a time; the 512²
  entries are the exception, not the new default.
- **Headless capture can stall on heavy recipes.** → Keep the worker and
  non-blocking detail page, and use the capture script's headful mode and
  extended timeout.
- **Musgrave could slip in through a broad feature list.** → The existing
  exclusion test stays and no module declares or cites it.

## Migration Plan

1. Add the five modules following the `stream-erosion` pattern; register them in
   `index.ts` and `sources.ts`; run the catalog test against the local wasm
   artifact.
2. Run the timed smoke (decision 2). Apply any fallback and update the affected
   adaptation note before proceeding.
3. Build the site, capture previews, commit the five WebPs and the regenerated
   manifest, and verify the gallery order, detail pages, code tabs, and
   adaptation notes.
4. Update `site/README.md` and the catalog test; run the root suite.
5. Rollback is a revert of the five modules, registrations, previews, manifest,
   test, and README edits; no API, data, or package migration is involved.
