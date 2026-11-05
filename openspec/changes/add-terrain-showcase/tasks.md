# Tasks

## 1. Site scaffold

- [x] 1.1 Create `site/` as a separate Vite + TypeScript + three.js consumer with its own `package.json`, lockfile, `tsconfig.json`, `vite.config.ts` (`base` from `SITE_BASE`, default `/`), `index.html`, and `highmap-wasm: "file:.."`; verify `npm ci` in `site/` succeeds after `dist/` exists, and `npm run check` and `npm run build` both pass.
- [x] 1.2 Add the hash router and page shells for `#/`, `#/p/<id>`, `#/p/<id>?preview=1`, and not-found; verify each route renders the expected shell in `npm run dev`, and an unknown demo id shows the not-found page rather than a blank screen.
- [x] 1.3 Add the shared chrome, responsive layout, focus styles, reduced-motion handling, and canvas labeling; verify keyboard tab order, visible focus, and the reduced-motion layout with a manual accessibility checklist.
- [x] 1.4 Write `site/README.md` covering local artifact build, `npm run dev`, `npm run check`, `npm run build`, the demo contract, thumbnail capture, and Pages deployment; verify every documented command runs as written.

## 2. Example-faithful catalog and generation

- [x] 2.1 Define the demo metadata contract and add the nine catalog entries from `design.md` with id, title, description, family, source example, panel/variant, grid, seed, palette, relief, and resolution note; verify a metadata test rejects duplicate ids, unknown families, and missing source paths.
- [x] 2.2 Implement the nine pure recipes using only the exposed API (`noiseFbm`, `noisePingpong`, `noiseRidged`, `noise`, `remap`, `warp`, `falloff`, `plateau`, `smoothFill`, `hydraulicStream`), freeing every `HArray` in `finally`; verify each recipe builds through the wasm artifact in Node and returns the declared shape.
- [x] 2.3 Add the generation worker and client so terrain builds off the main thread, with request tokens for stale responses, serializable errors, and a retry action; verify a detail page renders generated data and a forced failure shows the error and retry without freezing the page.
- [x] 2.4 Add the Code tab with Vite `?raw` source, syntax highlighting, copy control, and the source-example/panel link; verify the displayed text matches the demo module on disk and the copy control copies the full source.
- [x] 2.5 Add root catalog tests for feature allowlisting against the initialized API, source-file existence, no `hydraulicMusgrave`, deterministic byte-identical builds, finite values, non-zero range, and declared shape; verify `npm test -- --run tests/showcase-catalog.test.ts` passes and a deliberately unknown feature fails the suite.

## 3. Specimen diorama and viewport

- [x] 3.1 Implement pure terrain helpers for display normalization, per-demo palettes, and the diorama geometry: indexed top grid, four boundary walls with hard edges, topsoil band, at least three rock strata, flat bottom cap, and a base below the minimum height; verify a geometry test checks wall coverage, closed shell, base depth, finite positions, and expected triangle counts.
- [x] 3.2 Implement the three.js viewport with scene, camera framing, orbit limits, resize handling, warm key light, environment/hemisphere fill, shadow catcher, and ACES tone mapping; verify every catalog entry frames fully on load and the camera cannot go below the block.
- [x] 3.3 Wire the detail page stats (grid shape, triangle count, generation time) and the wireframe/flat view toggles; verify each toggle changes the rendered material and the stats match the built geometry and measured generation time.
- [x] 3.4 Tune palettes and relief for all nine specimens so the topsoil, strata, and solid base read clearly at gallery and detail sizes; verify a manual visual review of all nine confirms a closed block, visible cut faces, and legible terrain relief.

## 4. Gallery, previews, and provenance

- [x] 4.1 Implement gallery cards, family filters, the preview image glob with a CSS fallback, and card links; verify the root gallery shows all nine entries, each filter shows only its family, and "All" restores the full list.
- [x] 4.2 Implement preview mode and `site/scripts/capture-thumbnails.mjs` (ephemeral `vite preview`, puppeteer-core via `CHROME_SHELL_PATH` or `CHROME_URL`, fixed 16:10 frame at 640x400 CSS / DPR 2, WebP quality 82); verify the script captures all nine previews and writes `manifest.json` with dimensions and source hashes.
- [x] 4.3 Add preview provenance validation for manifest coverage, non-empty WebP files, and recomputed source hashes over the demo module source plus serialized metadata; verify the suite passes and a deliberately stale hash or missing image fails with the demo id named.
- [x] 4.4 Commit the nine WebP previews and manifest and wire them into the gallery; verify the built gallery loads committed previews and does not request wasm before a detail page is opened.

## 5. Deployment, docs links, and package boundary

- [x] 5.1 Add `.github/workflows/build-wasm.yml` to install pinned Emscripten 6.0.13, check out recursive submodules, run `npm ci`, `scripts/build-wasm.sh`, `node scripts/build-js.mjs`, and `npm test`, then upload `dist/` as the `highmap-wasm` artifact; verify the workflow YAML is valid and the same commands pass locally.
- [x] 5.2 Add `.github/workflows/pages.yml` to download the tested `highmap-wasm` artifact, install `site/`, run `npm run check`, build with `SITE_BASE=/highmap-wasm/`, and deploy Pages with the required permissions and concurrency; verify the same base-path build succeeds locally.
- [x] 5.3 Retire `.github/workflows/deploy_docs.yml` so no workflow pushes to `gh-pages`; verify no `peaceiris/actions-gh-pages` or `gh-pages` deployment remains.
- [x] 5.4 Add footer links to the existing GitHub docs, the LGPL-2.1 notice, and the exact source/build instructions, injecting the build commit SHA when available; verify the built site contains all links and the license text.
- [x] 5.5 Document the Pages source switch to GitHub Actions, the first-deploy verification, and the rollback path in `site/README.md`; verify the instructions match the workflows and repository settings.
- [x] 5.6 Add a package-boundary test asserting `npm pack --dry-run --json` contains no `site/` path; verify the test passes and the tarball still contains `dist`, README, LICENSE, and `THIRD_PARTY_LICENSES.md`.
- [ ] 5.7 Switch the repository Pages source to GitHub Actions and trigger the first deployment; verify `https://mode777.github.io/highmap-wasm/` serves the gallery and a detail page loads the wasm and renders its specimen.

## 6. Integration verification

- [x] 6.1 Run the full local gate: root `npm test`, root `npm run check`, `site/npm run check`, `site` build with `SITE_BASE=/highmap-wasm/`, and the package-boundary test; verify all pass from the tested `dist/` artifact.
- [ ] 6.2 Verify the deployed site covers all nine detail routes, each preview is present and current, the code tab matches each recipe, and docs/license links resolve.
- [x] 6.3 Inspect the final diff for scope: no upstream C++ source edits, no new package exports, no site files in the npm tarball, and no secrets or generated build output committed; verify with `git diff --stat`, `git status`, and `npm pack --dry-run`.

## Workflow follow-up

- Archive the change after review and the project's verification requirements are satisfied.
- Commit the archived change, synced main specs, and implementation together with the `mode777` git identity, per the project's archive guidance.
