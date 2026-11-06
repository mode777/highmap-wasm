# Design

## Context

See `proposal.md` — Why. The constraints that shape this design:

- **Upstream declares only the GPU family.** `HighMap/include/highmap/erosion.hpp`
  closes `namespace hmap` at line 714 and declares every thermal variant inside
  `namespace hmap::gpu` (lines ~1900–2290). `hmap::thermal_schott` *is* defined
  in `src/erosion/thermal_schott.cpp`, but no header declares it. Any CPU
  thermal API therefore needs a declaration surface of its own.
- **The kernels are the spec.** The algorithms live in
  `src/gpu_opencl/kernels/thermal*.cl` (embedded as strings in
  `gpu_opencl.cpp`); `src/erosion/thermal_gpu.cpp` only orchestrates
  double-buffered launches. Border handling is split between the kernels
  (`apply_boundaries_io`, `apply_boundaries_buffer_io(b)`) and the launch code
  (`extrapolate_borders` after the loop).
- **The desktop build globs sources.** `HighMap/CMakeLists.txt` compiles
  `file(GLOB_RECURSE ... src/*.cpp)`, so a new file under `HighMap/src` is
  picked up by both the desktop library and, once listed, the curated wasm
  build. Desktop release flags are `-Ofast -ffast-math` (GCC/Clang); the wasm
  target uses `-Os` with no fast-math.
- **No OpenCL device on the development machine** (the Khronos ICD registry key
  is absent); the upstream CI (`cmake.yml`) installs `pocl-opencl-icd`, so CI is
  the only place the GPU oracle can run.
- **The binding layer is manifest-driven** (`HighMap/wasm/manifest.md`, AD-008)
  and the JS API applies defaults (AD-009); masks are not part of any existing
  JS function.

## Goals / Non-Goals

**Goals:**

- One CPU implementation per missing variant, in one translation unit, sharing
  the exact arithmetic of its kernel.
- Signature-for-signature compatibility with `hmap::gpu::*` so call sites move
  between the two families by dropping a namespace prefix, and so an upstream
  CPU implementation can replace the port without touching bindings or tests.
- A parity harness that runs in the same binary and with the same compiler flags
  as the GPU oracle, plus a CI job that makes a failed or skipped comparison
  visible.

**Non-Goals:**

- Masks and deposition maps on the JS surface (C++ ports implement them; the
  manifest records the deferral).
- Performance work: no OpenMP, no pthreads, no tiling. Cost is measured and
  documented, not optimized.
- Any change to the GPU code path, to `thermal_schott`'s existing CPU behaviour,
  or to the desktop build's call sites (nothing starts calling `hmap::thermal`
  outside the parity harness and the wasm bindings).
- Remaining GPU-only families (`hydraulic_{mcdonald,schott,vpipes}`, `rifts`,
  `advection_*`) — separate changes.

## Decisions

### D1: Port sources live in the upstream source tree

New files: `HighMap/src/erosion/thermal_cpu.cpp` (nine ports) and
`HighMap/include/highmap/erosion_thermal_cpu.hpp` (declarations). No existing
upstream file is edited; `wasm-build`'s "remain unmodified" requirement is
amended to allow new files for ported algorithms.

Why: the desktop glob compiles the port automatically, so the parity oracle and
the shipped artifact execute the *same translation unit* — the property the
`wasm-build` spec already uses to justify flagged bug fixes. It also means the
desktop library stops having GPU-only thermal algorithms, and the parity harness
needs no separate build target, no duplicated flag set, and no `#include`-of-a-
`.cpp` tricks.

Alternatives considered:

- `HighMap/wasm/ports/` (the research doc's plan, AGENTS.md's default): keeps
  the upstream tree pristine, but the desktop harness would have to compile the
  port outside the upstream target with a different flag set, and the desktop
  library would keep shipping no CPU thermal code. Rejected by user decision in
  favour of the same-source property.
- Naming the file `thermal.cpp`: that is the name upstream would most likely
  choose for its own CPU contribution, producing an add/add conflict. The
  `_cpu` suffix instead yields a loud duplicate-symbol link error if upstream
  implements the same functions, which is the signal we want.

The file header states the port's provenance, the kernel it transcribes per
function, and the rebase rule ("if upstream ships CPU implementations, delete
this file and the header").

### D2: Namespace `hmap`, signatures mirroring `hmap::gpu::*`

The ten functions are declared in `hmap` with the same parameter lists and
defaults as the GPU family:

| function | overloads (beyond the array form) |
|---|---|
| `thermal` | mask; scalar talus; bedrock; deposition |
| `thermal_auto_bedrock` | mask; scalar talus; deposition |
| `thermal_conserve` | mask; scalar talus; rate; bedrock; deposition |
| `thermal_flatten` | mask; `sigma_inf = 0.5`, `sigma_sup = 0` |
| `thermal_inflate` | mask |
| `thermal_olsen` | mask |
| `thermal_rib` | mask |
| `thermal_ridge` | mask; deposition |
| `thermal_scree` | mask; `zmax`; deposition |
| `thermal_schott` | existing upstream CPU implementation, declaration only |

`thermal_schott`'s overloads differ from the GPU family (mask is a non-const
`Array *` in the third position, no bedrock). The header declares them exactly
as upstream defines them and marks the function as not part of the port.

### D3: Transcription, not re-derivation

Each port is written by translating its kernel line by line, keeping the
kernel's constants verbatim (`1.41421356f` in most kernels, `1.414213562f` in
`thermal_conserve`), its accumulation order, and its branch structure. Shared
helpers exist only where the kernel shares them:

- `helper_thermal_exchange` (thermal family), `smoothstep3`,
  `almost_unit_identity`, `hash21u` (32-bit unsigned, defined behaviour).
- `apply_boundaries_io` → a file-local `freeze_border_1` that copies the
  adjacent interior cell with the kernel's **check order** (`gx == 0` first,
  then `gx == nx-1`, then `gy == 0`, then `gy == ny-1`), so corners match.
- `apply_boundaries_buffer_io(b)` → a file-local
  `clamp_border_band(b)` using the same clamp-to-`[b, n-b-1]` rule.

Notable fidelity points that a naive rewrite would get wrong:

- `clamp(x, 0, 1)` in the kernels is OpenCL's `fmin(fmax(x, 0), 1)`, which
  maps NaN to 0. The ports use `std::fmin(std::fmax(...))`, not `std::clamp`
  (which propagates NaN).
- `thermal_auto_bedrock` seeds `bedrock` to `-FLT_MAX` on the first iteration
  and then tracks `max(z_init, new_z)` per cell; the port does the seed before
  the loop and keeps the bedrock array across iterations.
- `thermal_conserve` has a closed boundary (border cells copied unchanged), has
  **no** post-loop extrapolation, applies bedrock only after the loop, and its
  `outflow` is `rate * 0.5 * excess_max` (not proportional to the sum).
- `thermal_olsen` uses the same `hash21u(gx, gy) & 7` rotation in the gain pass
  and accumulates in that rotated order.
- `thermal_flatten` scans neighbours with the same `(gx + gy) & 7` rotation for
  both the centre and the donating neighbour, and searches the neighbour's
  steepest receiver using the *centre's* rotation offset.
- `thermal_rib` subtracts `min_k |z - z_k| / c_k` (no talus, no clamp), then
  calls `extrapolate_borders(z, 3)`.

Border profiles reproduced per variant:

| variant | during iterations | after iterations |
|---|---|---|
| `thermal`, `thermal_auto_bedrock` | 1-cell freeze | `extrapolate_borders(z)` |
| `thermal_flatten` | 2-cell clamp band | `extrapolate_borders(z)` |
| `thermal_inflate`, `thermal_ridge`, `thermal_scree` | 1-cell freeze | `extrapolate_borders(z)` |
| `thermal_olsen` | 3-cell clamp band | `extrapolate_borders(z)` |
| `thermal_rib` | 1-cell freeze | `extrapolate_borders(z, 3)` |
| `thermal_conserve` | broken (border copied) | none |

`thermal_schott` keeps its upstream behaviour — interior-only sweep plus
`extrapolate_borders` **every** iteration — which differs from the GPU wrapper's
single post-loop extrapolation. That is an upstream inconsistency, not a port
artifact; the feature is bound as-is and excluded from parity bounds (documented
in the parity report).

### D4: Talus as number or grid at the binding boundary

`bindings.cpp` takes `const hmap::Array &talus` only. When JS passes a number,
`src/api/highmap.ts` creates a constant wasm grid with `makeArray`, runs the
call, and deletes it in a `finally`. Alternatives — two bindings per function
(one scalar, one array) or a raw nullable pointer — either double the manifest
or push pointer plumbing into JS for no user-visible gain.

Shape validation happens in JS before the call (grid-marshalling's contract),
because the C++ entry points silently return on a shape mismatch. `iterations`
is passed straight through; negative values behave like zero, as in C++.

### D5: Parity harness as a gtest in the upstream test target

`tests/src/test_thermal_cpu.cpp` is picked up by `tests/CMakeLists.txt`'s
`src/*.cpp` glob, so it compiles with the same flags as the library it tests and
runs in the same binary as the existing `GpuCpu` suite.

For each variant, fixed scenes (seeded fBm 128², a synthetic ramp with a
plateau, a single-spike grid) and a small parameter matrix (1 and 10 iterations;
talus `0.1/ni` and `2/ni`; `rate` 0.5; `sigma_inf` 0.5) are run through both
`hmap::gpu::X` and `hmap::X`. Each case reports `max_abs`, `rms_abs`, and the
input range as a CSV line (`TEST_CASE,variant,scene,iters,max_abs,rms_abs,range`)
and as gtest properties, and asserts the calibrated bounds.

Provisionary bounds, confirmed or tightened by the first oracle run and then
frozen in a reviewed table in the file:

- continuous variants (`thermal`, `thermal_auto_bedrock`, `thermal_conserve`,
  `thermal_inflate`, `thermal_ridge`, `thermal_scree`, `thermal_flatten`):
  `max_abs <= 1e-4 * range`, `rms_abs <= 1e-5 * range` at 10 iterations;
- selection-based variants (`thermal_olsen`, `thermal_rib`): `max_abs <= 1e-2 *
  range`, `rms_abs <= 1e-5 * range`, plus exact border equality where the
  variant freezes borders.

Calibration rule: for one representative variant of each kind (a continuous one,
e.g. `thermal`, and a selection-based one, e.g. `thermal_olsen`) a deliberate
one-branch mutation of the port (wrong neighbour, sign flip, dropped talus term)
must push that variant over its bound. That is checked once during
implementation, reverted, and recorded in the parity report.

When no OpenCL device is present the test calls `GTEST_SKIP()` with the reason;
the CI job treats a skip as a failure, so a device-less runner cannot produce a
hollow pass.

### D6: Additive parity CI workflow

`.github/workflows/thermal-parity.yml` (new file, no edits to `cmake.yml`):
ubuntu-latest, the same apt set as the upstream desktop job including
`pocl-opencl-icd`, configure with examples/benchmarks/tests-off-except-tests,
build `highmap_tests`, run `--gtest_filter=ThermalCpu.*` with
`--gtest_output=xml`, fail if the XML reports any skipped case, upload the XML
and the CSV log, and write the metric table into the job summary.

Alternative: extending the upstream `cmake.yml` — rejected because it is an
upstream file, and because it only builds, never runs, the test binary.

### D7: Manifest, bindings and JS layer growth

Ten manifest rows with JS names `thermal`, `thermalAutoBedrock`,
`thermalConserve`, `thermalFlatten`, `thermalInflate`, `thermalOlsen`,
`thermalRib`, `thermalRidge`, `thermalScree`, `thermalSchott`; `talus` is a
required argument (`number | GridLike`) wherever C++ takes one, `zmax` likewise
for `thermalScree`; options defaults `iterations: 10` (including `rib`,
`flatten`, `olsen`, whose C++ overloads declare no default), `rate: 0.5`,
`sigmaInf: 0.5`, `sigmaSup: 0`, `intensity: 0.2`.

The manifest gains a "Deferred" note for the mask and deposition overloads, in
the same style as the moisture-overload note (AD-013). The surface test's
expected name list is updated; a new `tests/thermal.test.ts` covers in-place
semantics, scalar-vs-grid talus, option defaults, shape rejection, determinism
and finiteness; two thermal scenes join the golden set (captured from the
artifact, AD-011).

### D8: Documentation

New ADs: port placement and provenance, the CI parity oracle including its
device limitation, deferred JS overloads, and the amended upstream-addition
rule. `README.md`'s API list and `docs/wasm-port-research.md`'s phase-2 status
are updated; `docs/thermal-parity.md` records the harness, the bound table, the
mutation check, the device used by CI, and the measured metrics of the first
green run.

## Risks / Trade-offs

- **Fast-math asymmetry** (desktop `-Ofast` vs wasm `-Os`): the port's bytes can
  differ slightly between the builds. Mitigation: parity is measured in the
  desktop build against the GPU, wasm behaviour is pinned by artifact-captured
  goldens, and both bounds are tolerance-based.
- **Selection-based variants can flip an argmax** (`olsen`, `rib`, `flatten`),
  producing a locally large but legitimate difference. Mitigation: per-variant
  bounds, a mean-error bound, and exactness checks on borders rather than a
  single global `max_abs` threshold.
- **First CI run may need calibration**: provisional bounds could be too tight
  for PoCL's relaxed math. Mitigation: the calibration task is explicit; bounds
  are only loosened with the measured numbers and a mutation check justifying
  them.
- **The port adds a file upstream may later duplicate**: mitigation — a loud
  duplicate-symbol link error; deleting the port file and header is the whole
  rebase fix, because bindings/tests already speak the `hmap::` names.
- **Default-argument collision**: if upstream later declares the CPU family in
  `erosion.hpp` with defaults, including both headers fails to compile.
  Mitigation: the header says "delete me when upstream declares these", and the
  failure is immediate and unambiguous.
- **Runtime cost in the browser**: single-threaded, stencil-per-cell; 500
  iterations at 1024² is seconds. Mitigation: documented cost note with measured
  numbers; users choose iteration counts; performance work is out of scope.
- **`thermal_scree` divides by `zmax`**: a zero ceiling yields non-finite
  values and is not guarded, matching C++. Mitigation: documented caveat in the
  manifest/README; the JS layer does not invent domain validation the C++
  entry points do not have.
- **No local OpenCL**: parity cannot be reproduced on the Windows dev box.
  Mitigation: CPU-side correctness is gated locally by vitest + goldens; parity
  numbers come from CI and are recorded in the report.

## Migration Plan

Purely additive. Rollout: land the ports and header, then the bindings/JSApi,
then the parity harness and workflow, then docs. Rollback: delete the new files,
drop the ten manifest rows and their binding lines, revert the `wasm-build`
source list entries and the JS/TS changes — no persisted data or on-disk format
is involved.

## Open Questions

- Final tolerance table — settled by the first oracle run (mechanism and
  calibration rule are fixed here).
- Whether the parity job should later also run on a GPU runner (no such runner
  exists today; PoCL is the reproducible oracle).
