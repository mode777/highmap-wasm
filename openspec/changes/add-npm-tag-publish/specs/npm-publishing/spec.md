# Spec Delta

## Purpose

Defines how tagged commits become public npm releases: version gating, build-and-test provenance, credential-free publishing, dist-tag selection, and the GitHub Release that accompanies each published version.

## ADDED Requirements

### Requirement: Tag push triggers the release workflow

Pushing a Git tag that matches `v<major>.<minor>.<patch>` (optionally with a prerelease suffix) SHALL start the release workflow. Pushes to branches, pull requests, and tags that do not match the pattern MUST NOT publish anything.

#### Scenario: SemVer tag starts a release

- **WHEN** a maintainer pushes tag `v1.2.3` to the repository
- **THEN** the release workflow runs for that tag and reaches the publish stage

#### Scenario: Non-matching tag publishes nothing

- **WHEN** a tag such as `nightly-2026-11-11` is pushed
- **THEN** no release workflow publish job runs

#### Scenario: Branch pushes publish nothing

- **WHEN** commits are pushed to `main` without a tag
- **THEN** the release workflow does not run and no npm version is published

### Requirement: Tag and package version must agree

Before building or publishing, the release workflow SHALL verify that the tag is valid SemVer of the form `vX.Y.Z` or `vX.Y.Z-<prerelease>` and that `X.Y.Z` equals the `version` field in `package.json` at the tagged commit. A mismatch MUST fail the workflow before any artifact is published.

#### Scenario: Matching version proceeds

- **WHEN** tag `v0.2.0` is pushed and `package.json` at that commit declares `"version": "0.2.0"`
- **THEN** the workflow proceeds past validation

#### Scenario: Mismatched version fails

- **WHEN** tag `v0.2.0` is pushed but `package.json` declares `0.1.0`
- **THEN** the workflow fails with an error naming both versions and nothing is published

#### Scenario: Malformed tag fails

- **WHEN** a tag matching `v*` but not the SemVer pattern, such as `v1.2`, is pushed
- **THEN** the workflow fails with a validation error and nothing is published

### Requirement: Tagged commit must be on main

The release workflow SHALL verify that the tagged commit is reachable from the `main` branch before publishing, so that only code that passed the main-branch CI can be released.

#### Scenario: Tag on main publishes

- **WHEN** the tag points at a commit contained in `main`
- **THEN** the workflow proceeds

#### Scenario: Tag on an unmerged branch fails

- **WHEN** the tag points at a commit that is not reachable from `main`
- **THEN** the workflow fails before building and nothing is published

### Requirement: Published artifacts are rebuilt and tested from the tag

The release workflow SHALL build the wasm binary, JS entry, and TypeScript declarations from the tagged commit using the pinned Emscripten toolchain, and SHALL run the full `npm test` suite against those artifacts. Publishing MUST occur only after the build and tests succeed.

#### Scenario: Successful build and tests lead to publish

- **WHEN** the tagged commit builds and `npm test` passes
- **THEN** the workflow publishes the artifacts produced in that run

#### Scenario: Test failure blocks the release

- **WHEN** any test in `npm test` fails
- **THEN** the workflow exits non-zero and no npm version is published

#### Scenario: Artifact is not taken from an earlier run

- **WHEN** the workflow publishes
- **THEN** the tarball contains the wasm artifact built from the tagged commit in that same workflow run

### Requirement: Publishing uses trusted publishing with provenance

The release workflow SHALL authenticate to the npm registry using npm trusted publishing (OIDC) with the `id-token: write` permission, and SHALL publish with provenance attestation enabled. The workflow MUST NOT require or read a long-lived npm token secret.

#### Scenario: OIDC publish carries provenance

- **WHEN** the publish step runs on GitHub Actions
- **THEN** npm accepts the OIDC identity and the published version shows a provenance attestation linking it to this repository and workflow

#### Scenario: No long-lived token is used

- **WHEN** the workflow file is inspected
- **THEN** it contains no reference to an npm token secret and requests `id-token: write`

### Requirement: Dist-tag follows the version

A stable version `X.Y.Z` SHALL be published under the `latest` dist-tag, and a prerelease version `X.Y.Z-<prerelease>` SHALL be published under the `next` dist-tag, so a prerelease never becomes the default install.

#### Scenario: Stable release becomes latest

- **WHEN** version `0.2.0` is published
- **THEN** `npm install highmap-wasm` resolves to `0.2.0`

#### Scenario: Prerelease does not become latest

- **WHEN** version `0.2.0-beta.1` is published
- **THEN** the `next` dist-tag points at `0.2.0-beta.1` and `latest` is unchanged

### Requirement: Published tarball contains only shippable files

The published package SHALL contain the `dist/` artifacts, `README.md`, `LICENSE`, and `THIRD_PARTY_LICENSES.md`, and MUST NOT contain tests, C++ sources, benchmark data, development scripts, or build caches. The package manifest MUST NOT be marked private.

#### Scenario: Packed contents match the allowlist

- **WHEN** the release workflow packs the package before publishing
- **THEN** the file list contains only the allowlisted paths and the workflow fails if any unexpected path appears

#### Scenario: Package is installable

- **WHEN** a consumer installs the published version in a clean project
- **THEN** the package resolves, imports, initializes, and generates terrain without additional dependencies or configuration

### Requirement: Existing versions are never overwritten

If the version being released already exists on the npm registry, the workflow SHALL fail with a clear message naming the version and MUST NOT attempt to overwrite or republish it.

#### Scenario: Duplicate version fails cleanly

- **WHEN** tag `v0.2.0` is pushed after `0.2.0` was already published
- **THEN** the workflow fails before publishing and reports that the version already exists

### Requirement: A GitHub Release accompanies every published version

After a successful publish, the workflow SHALL create a GitHub Release for the tag with generated release notes and source archives. A GitHub Release MUST NOT be created when publishing failed.

#### Scenario: Successful publish creates the release

- **WHEN** the npm publish succeeds for tag `v0.2.0`
- **THEN** a GitHub Release named for `v0.2.0` exists with generated notes and links to the published package

#### Scenario: Failed publish leaves no release

- **WHEN** the npm publish fails
- **THEN** no GitHub Release is created for that tag

### Requirement: Release procedure is documented

The repository SHALL document the maintainer release procedure (version bump, commit, tag, push tag) and the one-time npm trusted-publisher and GitHub environment setup.

#### Scenario: A maintainer can follow the documented steps

- **WHEN** a maintainer reads the release documentation
- **THEN** it states the required tag format, version-bump step, prerequisite repository/workflow configuration, and how to verify the published result
