# showcase-site Specification

## Purpose

Publishes a static, example-faithful gallery of HighMap terrain specimens
rendered in the browser, so visitors can inspect the current CPU-only
capability in 3D and read the exact recipe behind each terrain.

## Requirements

### Requirement: Static gallery

The showcase SHALL present a static gallery at the site root that lists every
catalog terrain with its title, description, source example, and preview
image, and SHALL allow filtering by terrain family (noise, shaping, erosion).

#### Scenario: Root gallery lists the catalog

- **WHEN** a visitor opens the site root
- **THEN** one card is shown for every catalog entry, and each card opens that
  terrain's detail page

#### Scenario: Family filter narrows the gallery

- **WHEN** a visitor selects a terrain family filter
- **THEN** only entries in that family remain visible, and selecting "All"
  restores the full catalog

### Requirement: Implemented-algorithm catalog

The catalog SHALL include only terrains composed exclusively of functions
exposed by the current `highmap-wasm` API. It MUST NOT claim or demonstrate
unimplemented algorithms, GPU-only algorithms, or invented composite recipes.
`hydraulicMusgrave` MUST remain excluded until its numerical stability is
fixed.

#### Scenario: Unsupported feature fails validation

- **WHEN** a catalog entry declares a feature that is not present on the
  initialized API object
- **THEN** the catalog validation suite fails and names the entry and feature

#### Scenario: Musgrave is not shown

- **WHEN** the deployed catalog is inspected
- **THEN** it contains no terrain built with `hydraulicMusgrave`

### Requirement: Thermal family coverage

The catalog SHALL include at least one example-faithful entry for every thermal
erosion variant exposed by the API: `thermal`, `thermalAutoBedrock`,
`thermalConserve`, `thermalFlatten`, `thermalOlsen`, `thermalRib`,
`thermalInflate`, `thermalRidge`, `thermalScree`, and `thermalSchott`. Each
entry MUST trace to the source example panel that demonstrates that variant.

#### Scenario: Every thermal variant is shown

- **WHEN** the deployed catalog is inspected
- **THEN** each of the ten thermal variants appears in the declared features of
  at least one entry

#### Scenario: Missing thermal variant fails validation

- **WHEN** a catalog entry covering a thermal variant is removed
- **THEN** the catalog validation suite fails and names the missing variant

### Requirement: Hydraulic family coverage

The catalog SHALL include at least one example-faithful entry for every
hydraulic function exposed by the API other than the stability-excluded
`hydraulicMusgrave`: `hydraulicMcdonald`, `hydraulicMcdonaldMultiscale`,
`hydraulicSchott`, `hydraulicSchottErosion`, and `hydraulicVpipes`. Each entry
MUST declare its function in `features` and MUST trace to the source example
panel that demonstrates it.

#### Scenario: Every hydraulic function is shown

- **WHEN** the deployed catalog is inspected
- **THEN** each of the five hydraulic functions appears in the declared
  features of at least one entry

#### Scenario: Hydraulic specimens are catalogued

- **WHEN** a visitor opens a hydraulic specimen
- **THEN** the catalog contains entries with the ids `hydraulic-mcdonald`,
  `hydraulic-mcdonald-multiscale`, `hydraulic-schott`,
  `hydraulic-schott-erosion`, and `hydraulic-vpipes`, one per hydraulic
  function

#### Scenario: Missing hydraulic function fails validation

- **WHEN** a catalog entry covering a hydraulic function is removed
- **THEN** the catalog validation suite fails and names the missing function

#### Scenario: Musgrave remains excluded

- **WHEN** the deployed catalog is inspected
- **THEN** no entry declares `hydraulicMusgrave` in its features and no entry
  cites a Musgrave source example

### Requirement: Example traceability

Every catalog entry SHALL identify the HighMap example it ports and the panel
or variant used. It MUST preserve that example's grid shape, seed, and
generation parameters, except for recorded adaptations: web-sized changes to
grid resolution, talus scaling, or iteration budget, for which both source and
shipped values are stated; and a preprocessing step the wasm API cannot
express, which MUST be omitted and recorded rather than approximated.

#### Scenario: Detail page links the C++ source

- **WHEN** a visitor opens a terrain's detail page
- **THEN** the page names the source example and the panel or variant, and
  links to the corresponding C++ file in the repository

#### Scenario: Missing source fails validation

- **WHEN** a catalog entry references a source example path that does not
  exist
- **THEN** the catalog validation suite fails

#### Scenario: Recorded adaptation is visible

- **WHEN** a catalog entry deviates from its source example's resolution,
  talus, or iteration count
- **THEN** its metadata records the source and shipped values, and the detail
  page shows the adaptation note

#### Scenario: Inexpressible preprocessing step is recorded

- **WHEN** a source example applies a preprocessing step that the wasm API
  cannot express
- **THEN** the entry omits that step without substituting an invented
  equivalent, and its adaptation note names the omitted step and states that
  it is not applied

### Requirement: Solid specimen diorama

Each terrain SHALL render as a closed specimen block: a heightmap top surface,
vertical cut faces that follow the terrain boundary, a single solid cut-face
color, and a flat bottom. The base MUST sit below the lowest terrain point and
the block MUST have visible thickness rather than appearing as a thin plane.

#### Scenario: Diorama reads as a cut block

- **WHEN** any catalog terrain is displayed
- **THEN** its sides are visible below the terrain surface in one uniform
  cut-face color, and the underside is closed

#### Scenario: Base stays below the terrain

- **WHEN** the diorama geometry is validated
- **THEN** the bottom face lies below the minimum heightmap value and every
  boundary top vertex connects to a side wall

### Requirement: Detail view

Each catalog entry SHALL have a detail page with an orbitable 3D viewport, the
grid shape, triangle count, generation time, and the exact recipe source that
produced the terrain. The viewport MUST frame the whole specimen on load and
MUST NOT allow the camera below the block.

#### Scenario: Detail page shows the specimen and stats

- **WHEN** a visitor opens a terrain's detail page
- **THEN** the specimen is framed in the viewport and its grid shape,
  triangle count, and generation time are displayed

#### Scenario: Code view shows the executed recipe

- **WHEN** the visitor opens the code view
- **THEN** it displays the exact recipe source for that terrain and a control
  to copy it

### Requirement: Non-blocking generation

Terrain generation SHALL run without blocking the page's main thread. A failed
generation MUST surface an error with a retry action instead of leaving the
viewport blank.

#### Scenario: Page remains interactive during generation

- **WHEN** a detail page is generating its terrain
- **THEN** the page continues to respond to input while generation is in
  progress

#### Scenario: Generation failure is recoverable

- **WHEN** generation fails
- **THEN** the page shows an error message and a retry action that attempts
  the generation again

### Requirement: Deterministic catalog validation

Every catalog recipe SHALL build deterministically to a finite, non-degenerate
grid with the declared shape and seed. Continuous integration SHALL fail if
any recipe produces non-finite values, an empty value range, or different
output across repeated runs.

#### Scenario: Repeated builds are identical

- **WHEN** each catalog recipe is built twice with the same seed and
  parameters
- **THEN** both runs produce byte-identical heightmap data

#### Scenario: Invalid terrain fails validation

- **WHEN** a recipe produces a NaN, an infinity, or a grid whose minimum
  equals its maximum
- **THEN** the catalog validation suite fails

### Requirement: Previews match demos

Gallery previews SHALL be pre-rendered from the same diorama rendering path as
the detail page. Continuous integration SHALL fail when a recipe changes
without its preview and manifest hash being updated, or when a catalog entry
has no preview.

#### Scenario: Stale preview fails validation

- **WHEN** a recipe's source changes but its recorded preview hash does not
- **THEN** the preview validation suite fails

#### Scenario: Missing preview fails validation

- **WHEN** a catalog entry has no preview image
- **THEN** the preview validation suite fails

### Requirement: GitHub Pages publication

The showcase SHALL be published to GitHub Pages at the repository's project
path by a workflow that deploys the wasm artifact built and tested in the same
pipeline. The site MUST load its wasm artifact and render a terrain at that
base path.

#### Scenario: Push to main deploys the tested artifact

- **WHEN** a successful wasm build completes on the main branch
- **THEN** the showcase is built from that run's artifact and deployed to the
  project Pages URL

#### Scenario: Deployed site loads its wasm

- **WHEN** a visitor opens a detail page on the deployed project URL
- **THEN** the wasm artifact loads from the site base path and the terrain is
  rendered

### Requirement: Documentation and license links

The showcase SHALL link to the existing repository documentation and SHALL
display the LGPL-2.1 notice together with a link to the deployed artifact's
source and build instructions.

#### Scenario: Footer exposes docs and license

- **WHEN** a visitor views any showcase page
- **THEN** the footer links to the repository documentation, the project
  license, and the wasm source and build instructions

### Requirement: Separate package consumer

The showcase SHALL consume the locally built package and MUST NOT be included
in the npm tarball.

#### Scenario: npm package excludes the site

- **WHEN** the package tarball contents are listed
- **THEN** no file under `site/` is included
