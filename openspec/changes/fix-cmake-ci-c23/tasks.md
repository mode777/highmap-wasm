# Tasks

## 1. Workflow fix

- [x] 1.1 Edit the `Configure CMake` step in `.github/workflows/cmake.yml` to add `-DCMAKE_C_FLAGS=-std=gnu17` to the `cmake -B ...` invocation (place it with the other `-D` flags, matching the style of `.github/workflows/erosion-parity.yml`), changing no other line of the workflow. Verify the file still parses as YAML (e.g. a one-off Node YAML parse or actionlint if available) and that `git diff` shows exactly the one added line.

## 2. CI verification

- [ ] 2.1 After the apply commit is pushed to `main` per the workflow follow-up, watch the `CMake on multiple platforms` run for that commit (`gh run watch <id> --repo mode777/highmap-wasm`) and verify it concludes successfully; record the run URL. Confirm the `build-wasm`, `erosion-parity`, and `pages` runs on the same commit are also green.

## Workflow follow-up

- Before committing, verify the repository resolves to the `mode777` identity: `git config user.email` must return `6848069+mode777@users.noreply.github.com`; set `git config user.name mode777` and that email repo-locally first if it differs.
- Commit the workflow fix and this change, push to `main`, then complete task 2.1.
- Archive the change after the green run is recorded, syncing the delta spec into `openspec/specs/desktop-ci/spec.md` through the archive workflow.
