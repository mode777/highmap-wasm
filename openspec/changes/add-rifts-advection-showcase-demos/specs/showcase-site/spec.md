# Spec Delta

## ADDED Requirements

### Requirement: Rifts and advection family coverage

The catalog SHALL include at least one example-faithful entry for every rifts and
advection function exposed by the API: `rifts`, `advectionParticle`, and
`advectionWarp`. Each entry MUST declare its function in `features` and MUST trace
to the source example panel that demonstrates it.

#### Scenario: Every rifts and advection function is shown

- **WHEN** the deployed catalog is inspected
- **THEN** each of the three functions appears in the declared features of at least
  one entry

#### Scenario: Rifts and advection specimens are catalogued

- **WHEN** a visitor opens a rifts or advection specimen
- **THEN** the catalog contains entries with the ids `rifts`, `advection-particle`,
  and `advection-warp`, one per function

#### Scenario: Missing rifts or advection function fails validation

- **WHEN** a catalog entry covering one of the three functions is removed
- **THEN** the catalog validation suite fails and names the missing function

#### Scenario: Rifts preprocessing omission is recorded

- **WHEN** the `rifts` entry's metadata is inspected
- **THEN** its adaptation note names the omitted `zeroed_edges` step from
  `ex_rifts.cpp` and states that it is not applied
