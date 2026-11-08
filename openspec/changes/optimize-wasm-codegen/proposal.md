# Proposal

## Why

The shipped wasm artifact is currently compiled with `-Os` (size-optimized)
purely as a bootstrap default; it was never chosen against a measured
alternative. A same-machine A/B of the committed baseline artifact
(SHA-256 verified) shows that `-O3` cuts 14–30% off the hottest cells with
no source changes and no change to the package's deployment contract:

| cell (1024²) | `-Os` | `-O3` | delta |
|---|---:|---:|---:|
| `thermalFlatten` | 66,069 ms | 51,898 ms | −21% |
| `hydraulicSchottErosion` | 11,750 ms | 8,233 ms | −30% |
| `advectionWarp` | 7,691 ms | 6,646 ms | −14% |

That is the cheapest remaining phase-3 win: it does not require pthreads,
SharedArrayBuffer, SIMD, or any new API. The measured alternatives were
rejected: `-O2` gives up ~12 points on `hydraulicSchottErosion` for only
~4.5% smaller output, and `-O3 + -msimd128` is a wash on most cells while
regressing several small ones.

## What Changes

- Change the wasm release codegen from `-Os` to `-O3` in
  `HighMap/wasm/CMakeLists.txt`, for both compile and link options.
- Expose the optimization level and optional codegen flags as CMake cache
  variables (`WASM_OPT_FLAGS`, `WASM_EXTRA_FLAGS`, defaults `-O3` and empty)
  so future A/B comparisons do not require editing tracked files.
- Re-run the committed benchmark baseline with the new artifact and update
  `docs/benchmarks.md`: new reference run (artifact size/hash), new full
  table, and the previous `-Os` table retained as a dated historical
  baseline with a measured comparison.
- Record the decision (including the rejected `-O2`/SIMD experiments) in
  `docs/decisions.md` and update the phase-3 status in
  `docs/wasm-port-research.md`.
- Not changing: the binding surface, JS wrapper, loader, algorithms, tests'
  timing policy, threading model, or deployment requirements. SIMD and
  pthreads remain out of scope.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `wasm-build`: adds a release-codegen requirement — the default build uses
  `-O3` and the optimization flags remain overridable without tracked-file
  edits, while the existing single-threaded/no-SharedArrayBuffer requirement
  continues to hold.
- `benchmarks`: adds a requirement that the recorded baseline is measured
  with the codegen profile actually shipped, and that a profile change
  produces a new full baseline while preserving the previous one as a dated
  comparison.

## Impact

- Code: `HighMap/wasm/CMakeLists.txt` only (build configuration; no
  `HighMap/src` or `HighMap/include` changes).
- Artifact: `dist/highmap.wasm` grows from 592,141 to 683,602 bytes
  (+15.4%; still under 0.7 MB) and gets a new SHA-256; JS glue is
  effectively unchanged.
- Docs: `docs/benchmarks.md`, `docs/decisions.md`,
  `docs/wasm-port-research.md`.
- Build/CI: same toolchain and commands; the existing `build-wasm` and
  Pages workflows need no changes. Compile time increases modestly; the
  first post-change build is a full rebuild.
- Runtime: no API or behavior change; numerical results remain governed by
  the existing parity suites (no fast-math, no FP reassociation).
