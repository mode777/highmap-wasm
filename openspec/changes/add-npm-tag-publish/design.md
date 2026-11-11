# Design

## Context

- `build-wasm.yml` already builds the artifact from a clean checkout (emscripten 6.0.13, cached emsdk, `npm ci`, `scripts/build-wasm.sh`, `node scripts/build-js.mjs`) and runs `npm test` (vitest, bundler consumer test, quickstart test). `pages.yml` reuses that tested artifact via `workflow_run`.
- `package.json` is ESM (`"type": "module"`), `"private": true`, version `0.0.0`, with a `files` allowlist of `dist`, `README.md`, `LICENSE`, `THIRD_PARTY_LICENSES.md` (AD-004). No runtime dependencies.
- `tests/scaffold.test.ts` currently asserts `private === true`, so it must flip with the release contract.
- The `highmap-wasm` name is unclaimed on npmjs.com (registry returns 404 as of 2026-11-11).
- GitHub's `ubuntu-latest` Node 22 image ships npm 10.x; npm trusted publishing requires npm >= 11.5.1.
- LGPL-2.1: the tarball ships license notices; the public tagged commit plus GitHub Release source archives provide source availability.

See `proposal.md` for motivation and `specs/npm-publishing/spec.md` for the behavior contract.

## Goals / Non-Goals

**Goals:**

- One workflow file, tag-triggered, that validates, rebuilds, tests, publishes, and records the release.
- Credential-free, provenance-attested publishing through OIDC trusted publishing.
- A repeatable maintainer procedure with a clear first-release path.

**Non-Goals:**

- Publishing on every merge or on a schedule (tags are the only trigger).
- Automated changelog generation beyond GitHub's generated release notes.
- A token-based publish fallback inside CI (emergency manual publishing stays a documented out-of-band option).
- Publishing the showcase site (it remains Pages-only).
- Multi-package/workspace publishing.

## Decisions

### D1 — Separate `publish.yml`, triggered by `v*` tags, with validation in a script

Use one new workflow with `on: push: tags: ['v*']`; validate SemVer and the package-version match in a shell step rather than encoding it in the trigger glob. Rationale: a narrow glob (e.g. `v[0-9]+.[0-9]+.[0-9]+*`) silently skips malformed tags, while `v*` plus explicit validation produces a red run that tells the maintainer what is wrong (the "malformed tag fails" scenario).

Alternatives considered: reusing `build-wasm.yml` with an `if:` on tags (couples PR builds to release permissions and makes the permission surface wider); `workflow_dispatch`-only releases (does not satisfy "whenever a new tag is created").

### D2 — Rebuild and test inside the release run instead of reusing the CI artifact

The publish job checks out the tag with `fetch-depth: 0` and `submodules: recursive`, builds with the pinned emsdk, and runs the full `npm test`. Rationale: the spec requires the tarball to contain an artifact built from the tagged commit in that run, and npm provenance attests the workflow that produced it. Reusing an artifact from a branch build would break the tag-to-bytes chain and the `workflow_run` artifact scoping adds failure modes.

Alternative considered: `workflow_run` on `build-wasm` filtered to tags (artifact download, no rebuild). Rejected: the artifact is not guaranteed to match the tag commit, and provenance would point at the wrong workflow.

### D3 — Trusted publishing (OIDC) with explicit `--provenance`, no `NPM_TOKEN`

Permissions: `id-token: write` for the OIDC exchange and `contents: write` for the GitHub Release. The workflow installs `npm@11` (pinned major) before publishing because the Node 22 runner's npm 10.x does not support trusted publishing. `package.json` must declare a `repository.url` that exactly matches `https://github.com/mode777/highmap-wasm`, because npm validates that field against the GitHub OIDC claims. Rationale: no long-lived credential to rotate or leak, and npm records a provenance attestation linking the version to this repository, workflow, and commit.

Alternatives considered: `NPM_TOKEN` secret (rejected per user decision and spec requirement); GitHub Packages registry (not the user's target; npmjs.com is the public install path).

### D4 — Gate the tag against `main` with `git merge-base --is-ancestor`

After a full-history checkout, run `git merge-base --is-ancestor "$GITHUB_SHA" origin/main` and fail otherwise. Rationale: tags are cheap to create from any branch; requiring ancestry keeps releases to code that passed main-branch CI without forbidding release-branch hotfixes that are merged first.

Alternative considered: branch-protection-only process (not machine-enforced).

### D5 — Dist-tag from the version string

Detect a `-` in the stripped version: prerelease → `--tag next`, stable → `--tag latest`. Rationale: prevents a beta from becoming the default install, and matches npm's own SemVer conventions. `latest` is never passed for prereleases even if npm's default would otherwise move it.

### D6 — Duplicate-version guard before publish

Run `npm view "highmap-wasm@$VERSION" version`; a successful lookup means the version exists → fail before publishing. Combine with `concurrency: { group: npm-publish, cancel-in-progress: false }` so two tag pushes cannot interleave. Rationale: npm rejects duplicate versions anyway, but the guard produces a clear, early message and prevents a partially completed run from leaving a GitHub Release without a publish.

### D7 — Tarball allowlist check with `npm pack --dry-run --json`

Pack, parse the file list, and assert every path starts with `dist/` or is one of the three allowlisted root files. Rationale: the `files` field is the source of truth, but a regression (e.g. adding a stray top-level artifact) should fail the release, not ship. This also gives the workflow a deterministic version of "package is installable" input.

### D8 — Publish first, then create the GitHub Release

Publish, then `gh release create "$TAG" --generate-notes` (idempotent check: if the release already exists, edit notes instead of failing). Rationale: the spec forbids a release when publishing fails; the reverse (publish succeeded, release creation failed) is recoverable and surfaced in the job summary. Use the built-in `GITHUB_TOKEN` with `contents: write` rather than a third-party action.

### D9 — Package metadata: drop `private`, start at `0.1.0`

Remove `"private": true` (absence means public), set `"version": "0.1.0"` for the first public release, and update `package-lock.json` accordingly. Update `tests/scaffold.test.ts` to assert `private` is absent rather than `true`. Rationale: the package has been built, tested, benchmarked, and parity-checked; `0.1.0` signals the first installable release while the API continues to grow (AD-009).

Alternative considered: stay on `0.0.x` (rejected: npm treats `0.0.x` as placeholder; `0.1.0` is the conventional first public version).

### D10 — Documentation: README release section plus one AD entry

Update the README status/quickstart (install from npm, not "build from source"), add a "Releasing" section or `docs/releasing.md` with the exact maintainer steps, and record the decisions as AD-031 in `docs/decisions.md`. Rationale: the spec requires the procedure and the one-time setup to be documented; the AD log keeps the choice of trusted publishing and the version policy discoverable.

## Risks / Trade-offs

- **Trusted-publisher misconfiguration (wrong workflow filename, environment mismatch)** → publish fails at the OIDC exchange, which is safe (no partial release); document the exact npmjs.com fields and validate with a prerelease tag (`v0.1.0-rc.1`) before the stable tag.
- **npm CLI on the runner is too old** → install `npm@11` explicitly in the publish job and print `npm --version` before publishing.
- **Tag pushed from an unmerged branch** → D4 ancestry gate fails before any build or publish.
- **Duplicate tag/version after a partial failure** → D6 guard plus non-cancelling concurrency; the maintainer re-tags only after deciding whether to bump the patch version (npm forbids republishing).
- **Release creation fails after a successful publish** → npm version stays published; the job summary names the tag, and `gh release create` is re-runnable idempotently.
- **First stable release inherits the whole current API surface** → the surface is manifest-driven and benchmarked (AD-008/AD-029); the README documents the `0.1.x` stability expectation.
- **LGPL relinking obligations for a wasm binary** → the repo is public and tagged, GitHub Release archives carry the sources, and the tarball includes `LICENSE`/`THIRD_PARTY_LICENSES.md`; note this in the release docs.

## Migration Plan

1. Land the workflow, package metadata (`private` removed, `0.1.0`), scaffold-test update, README/docs changes, and AD-031 in one change; all existing CI stays green (the publish workflow only runs on tags).
2. Maintainer one-time setup: on npmjs.com, configure the trusted publisher for `highmap-wasm` (repository `mode777/highmap-wasm`, workflow `publish.yml`, environment `npm-publish`); create the matching GitHub environment.
3. Validate with a prerelease tag `v0.1.0-rc.1` (publishes under `next`) and confirm install, provenance, and the GitHub Release.
4. Cut the stable `v0.1.0`; confirm `latest` and `npm install highmap-wasm` in a clean project.

Rollback: delete the tag and disable the workflow; if a bad version was published, `npm deprecate` (or `npm unpublish` within registry policy) and publish a fixed patch. A published version number is never reused.
