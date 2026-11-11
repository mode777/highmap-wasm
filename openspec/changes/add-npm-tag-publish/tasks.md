# Tasks

## 1. Make the package publishable

- [x] 1.1 Remove `"private": true` from `package.json`, set `"version": "0.1.0"`, add `"repository": { "type": "git", "url": "git+https://github.com/mode777/highmap-wasm.git" }` (npm trusted publishing validates this against the GitHub repository), and refresh `package-lock.json` (e.g. `npm install --package-lock-only`); verify with `npm pkg get version private repository` (version `0.1.0`, private absent, repository URL present) and confirm both lockfile version fields read `0.1.0`.
- [x] 1.2 Flip the scaffold contract in `tests/scaffold.test.ts` from `private === true` to `private` being absent while keeping the `files` allowlist and `engines` assertions; verify with `npx vitest run tests/scaffold.test.ts`.

## 2. Release guards as testable scripts

- [x] 2.1 Add `scripts/release-guard.mjs`: validate the tag argument against `v<semver>` (rejecting `v1.2`), compare it to `package.json`'s version, and emit `version` and `dist_tag` (`next` when the version has a prerelease suffix, else `latest`); verification: run it with `v0.1.0` (exit 0, `latest`), `v0.1.0-beta.1` (exit 0, `next`), `v9.9.9` (exit non-zero naming both versions), and `v1.2` (exit non-zero).
- [x] 2.2 Add vitest coverage in `tests/release-guard.test.ts` for the matching, mismatched, malformed, and prerelease cases by importing the script's exported checker; verify with `npx vitest run tests/release-guard.test.ts`.
- [x] 2.3 Add `scripts/release-pack-check.mjs` that runs `npm pack --dry-run --json`, asserts every packed path starts with `dist/` or equals `README.md`, `LICENSE`, or `THIRD_PARTY_LICENSES.md`, and fails listing unexpected paths; verify it passes against a locally built `dist/` and fails when a temporary stray file is added to the `files` list (then revert the probe).

## 3. Tag-triggered publish workflow

- [x] 3.1 Add `.github/workflows/publish.yml` triggered by `push: tags: ['v*']` with non-cancelling `npm-publish` concurrency, `environment: npm-publish`, job permissions `contents: write` + `id-token: write`, and a full-history/submodule checkout; verification: the file parses and `gh workflow view publish.yml` lists it after the change is pushed.
- [x] 3.2 Wire the guard steps in order — SemVer/version check via `scripts/release-guard.mjs`, ancestry check with `git merge-base --is-ancestor "$GITHUB_SHA" origin/main`, and duplicate-version check with `npm view "highmap-wasm@$VERSION" version` failing when the lookup succeeds; verification: the ancestry and duplicate commands are runnable locally against the current checkout and the script path exits correctly.
- [x] 3.3 Reuse the proven build steps from `build-wasm.yml` (cached emsdk 6.0.13, `npm ci`, `scripts/build-wasm.sh`, `node scripts/build-js.mjs`, `npm test`) inside the publish job; verification: the same command sequence passes locally after the metadata change.
- [x] 3.4 Add the publish stage: install `npm@11`, print `npm --version`, run `scripts/release-pack-check.mjs`, then `npm publish --provenance --access public --tag "$DIST_TAG"` with no token secret referenced; verification: a workflow-level grep shows `id-token: write`, `--provenance`, `--tag`, and no `NPM_TOKEN`/`secrets.NPM`.
- [x] 3.5 Create the GitHub Release after a successful publish with `gh release create "$TAG" --generate-notes` (edit notes when the release already exists) using `GITHUB_TOKEN`; verification: the release step is ordered after the publish step and only runs on success.

## 4. Documentation and decisions

- [x] 4.1 Update `README.md`: replace the "unpublished" status and "build from source" quickstart with `npm install highmap-wasm`, state the `0.1.x` stability expectation, and add a concise "Releasing" section pointing to the detailed procedure; verify every command in the section is copy-pasteable and matches the workflow's tag format.
- [x] 4.2 Add `docs/releasing.md` with the maintainer procedure (bump version, commit, tag `vX.Y.Z`, push tag) and the one-time setup (npm trusted publisher for repository/workflow/environment, GitHub `npm-publish` environment, prerelease validation via `v0.1.0-rc.1`); verify it names the exact workflow file and environment the workflow uses.
- [x] 4.3 Record AD-031 in `docs/decisions.md` (tag-triggered trusted publishing, no token, `latest`/`next` policy, first public version `0.1.0`) and update the phase-3 status in `docs/wasm-port-research.md`; verify the AD entry is linked from the release docs and the research doc no longer lists npm publish as remaining.

## 5. Integration verification

- [x] 5.1 Run the full local pipeline (`npm run build`, `npx vitest run`, `npm test`) and confirm the published file list via `node scripts/release-pack-check.mjs`; verify the tarball contains only `dist/`, `README.md`, `LICENSE`, and `THIRD_PARTY_LICENSES.md`.
- [x] 5.2 After pushing the change, confirm `gh workflow view publish.yml` resolves and a `workflow_dispatch`-less tag trigger is the only entry; verify no branch or pull-request trigger exists in the file.

## Workflow follow-up

- Configure the npm trusted publisher on npmjs.com (repository `mode777/highmap-wasm`, workflow `publish.yml`, environment `npm-publish`) and create the matching GitHub environment.
- Push `v0.1.0-rc.1` and verify: `next` dist-tag set, `latest` unchanged, provenance attestation present, GitHub Release created, and a clean-project install of `highmap-wasm@0.1.0-rc.1` initializes and generates terrain.
- Push `v0.1.0` and verify `npm install highmap-wasm` resolves to `0.1.0` from the clean project.
- Archive the change after the release is verified.
