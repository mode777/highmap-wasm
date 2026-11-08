# Tasks

## 1. Harness

- [x] 1.1 Add `scripts/bench.mjs`: manifest parsing (same parsing rules as
  `tests/surface.test.ts`), per-family fixed-seed input recipes (including
  companion grids for `advectionParticle`, `advectionWarp`, `rifts`),
  256/512/1024 grid sweep, warmup + budgeted timed reps (≥3, ≤10, ~2 s
  cap), median/min/max reporting, `--only <fn>` and `--sizes <list>`
  flags, console table, and JSON output to
  `bench/results/bench-<ISO-date>-<hostname>.json` with the `context`
  block (Node version, platform, git commit, wasm bytes). Fail fast with a
  clear message when `dist/` is missing. Verify: `node scripts/bench.mjs
  --only noiseFbm --sizes 256` exits 0 and prints one row plus writes one
  JSON file whose `benchmarks` array has exactly one entry with finite
  numeric `median_ms`/`min_ms`/`max_ms`.
- [x] 1.2 Add `"bench": "node scripts/bench.mjs"` to `package.json` scripts
  and `bench/results/*.json` to `.gitignore` (keep a `.gitkeep`). Verify:
  `npm run bench -- --only thermal --sizes 256,512` exits 0 and `git
  status --short` does not list the produced JSON.

## 2. Full run and baseline

- [x] 2.1 Run `npm run build` then a full `npm run bench`; record total
  wall time (design target < ~10 min; if exceeded, lower per-cell rep
  budgets and record the change). Verify: every manifest function appears
  at all three grid sizes in the JSON; no cell has fewer than 3 reps.
- [x] 2.2 Write `docs/benchmarks.md`: methodology (warmup, reps, medians),
  reference machine + Node version + git commit + wasm artifact size, the
  full median table, the slowest cells called out, and the pthreads
  comparison protocol (same harness, same seeds, compare per-cell medians
  against this table). Verify: the table matches the JSON from 2.1
  cell-for-cell (spot-check ≥5 cells including one erosion, one noise, one
  advection).

## 3. Guardrails and docs

- [x] 3.1 Add `tests/bench.test.ts` smoke: run the harness as a child
  process on one function at 64² (or the smallest supported grid) and
  assert exit 0 and structurally valid JSON (`context` + `benchmarks`
  array with required finite numeric fields). No duration assertions.
  Verify: `npx vitest run tests/bench.test.ts` passes in < 60 s.
- [x] 3.2 Append one AD entry to `docs/decisions.md` (AD-029) locking the
  harness contract (manifest-driven coverage, warmup + median methodology,
  Google-Benchmark-compatible top-level JSON, no timing gates) and add a
  phase-3 benchmarks status line to `docs/wasm-port-research.md` §7 item
  3. Add a short "Benchmarks" note to README's development section
  (`npm run bench`, results location, baseline doc pointer). Verify: AD
  numbering continues the existing sequence and all three docs render
  their lists correctly.

## 4. Integration verification

- [x] 4.1 Run `npm test` and `npm run check` at the repo root; both must
  pass with the new smoke test included and no timing sensitivity (rerun
  `npx vitest run tests/bench.test.ts` once to confirm stability).
- [x] 4.2 Inspect `git diff --stat`: only `scripts/bench.mjs`,
  `tests/bench.test.ts`, `docs/benchmarks.md`, `bench/results/.gitkeep`,
  `package.json`, `.gitignore`, `README.md`, `docs/decisions.md`,
  `docs/wasm-port-research.md` change; nothing under `HighMap/`, the
  parity workflows, or existing tests.

## Workflow follow-up

- Before committing, verify the repository resolves to the mode777
  identity (`git config user.email` must return
  6848069+mode777@users.noreply.github.com); set it repo-locally first if
  it differs.
- Commit the implementation and this change per the project's apply
  guidance, then archive only after review confirms the recorded baseline
  matches a fresh run.
