# Spec Delta

## ADDED Requirements

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

## MODIFIED Requirements

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
