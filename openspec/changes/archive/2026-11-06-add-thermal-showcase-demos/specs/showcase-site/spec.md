# Spec Delta

## ADDED Requirements

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

## MODIFIED Requirements

### Requirement: Example traceability

Every catalog entry SHALL identify the HighMap example it ports and the panel
or variant used. It MUST preserve that example's grid shape, seed, and
generation parameters, except for web-sized adaptations (grid resolution,
talus scaling, and iteration budget) that are explicitly recorded with both the
source and shipped values.

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
