# Proposal

## Why

The wasm package is buildable and tested, but it has no release path: `package.json` is marked `"private": true`, the README states the package is unpublished, and the only CI workflows build and deploy the showcase. Phase 3 of the port plan (`docs/wasm-port-research.md`, §7) lists "publishing the npm package" as remaining work.

Publishing must be reproducible and traceable: the artifact users install has to be built from the exact tagged commit, pass the same test suite CI already trusts, and carry provenance tying it to this repository and workflow. A tag-triggered GitHub Actions release gives that without a manual publish step or a long-lived npm credential.

## What Changes

- Add a tag-triggered release workflow (`.github/workflows/publish.yml`) that runs when a `v*` tag is pushed:
  - validates the tag is `v<semver>` and equals the `version` in `package.json`;
  - verifies the tagged commit is reachable from `main`;
  - rebuilds the wasm and JS artifacts from the tagged commit with the pinned Emscripten toolchain, then runs the full `npm test` suite;
  - publishes via npm **trusted publishing** (OIDC) with provenance enabled — no long-lived `NPM_TOKEN` secret;
  - publishes prerelease versions under the `next` dist-tag and stable versions under `latest`;
  - creates a GitHub Release for the tag with generated notes.
- Make the package publishable: remove `"private": true`, set the release version, and keep the published tarball limited to `dist/` plus the license and README files (AD-004).
- Update the package scaffold test so it enforces the publishable contract instead of `private: true`.
- Replace the README's "not yet published" status with real install/usage instructions and document the maintainer release procedure (bump version, commit, tag, push tag) and the one-time npm trusted-publisher setup.
- Record the release decisions in `docs/decisions.md`.

## Capabilities

### New Capabilities

- `npm-publishing`: tag-driven release behavior — tag/version validation, build-and-test provenance, trusted publishing, dist-tag selection, and GitHub Release creation.

### Modified Capabilities

<!-- No existing spec's requirements change: wasm-build still produces the same
     artifact set, and wasm-loader behavior is untouched. -->

## Impact

- **CI**: new `.github/workflows/publish.yml`; existing `build-wasm`, `pages` and `erosion-parity` workflows unchanged.
- **Package metadata**: `package.json` (`private`, `version`), `package-lock.json` version fields.
- **Tests**: `tests/scaffold.test.ts` (publishable-package assertions).
- **Docs**: `README.md` (status, install, release procedure), `docs/decisions.md` (new AD entries), research doc status note.
- **External setup (one-time, maintainer)**: configure this repository and workflow as a trusted publisher for the `highmap-wasm` package on npmjs.com; create the `npm-publish` GitHub environment.
- **License**: the published tarball still carries `LICENSE` and `THIRD_PARTY_LICENSES.md`; the GitHub Release source archives and the public tagged commit provide the LGPL-2.1 source-availability evidence.
