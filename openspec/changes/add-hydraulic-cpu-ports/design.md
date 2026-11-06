# Design

## Context

See `proposal.md` — Why. The constraints that shape this design:

- **Upstream declares the hydraulic family only in `hmap::gpu`.**
  `HighMap/include/highmap/erosion.hpp` closes `namespace hmap` at line 714 and
  declares `hydraulic_mcdonald`, `hydraulic_mcdonald_multiscale`,
  `hydraulic_schott` (two overloads), `hydraulic_schott_erosion` and
  `hydraulic_vpipes` inside `hmap::gpu`. The implementations live in
  `src/erosion/hydraulic_{mcdonald,schott,vpipes}_gpu.cpp`; there is no CPU
  declaration surface.
- **The kernels are the specification.** `hydraulic_mcdonald.cl` contains the
  particle solver, filter and debris flow; `hydraulic_schott.cl` contains the
  full phase-cycling procedure and the erosion-only form; `hydraulic_vpipes.cl`
  contains the flux, water, erosion, sediment-transport and rain passes.
  `hydraulic_vpipes_gpu.cpp` in the pinned tree still launches the pre-refactor
  kernel names (`hydraulic_vpipes_flow_simulation`, `_velocity`,
  `_erosion_deposition`, `_advection`), which no longer exist. The
  `hydraulic_schott` wrapper never binds the kernel's final `it` argument. The
  `hydraulic_schott` mask overload is declared with a different parameter order
  than its definition. These are upstream launch bugs, not porting problems.
- **McDonald is atomic and therefore not bit-reproducible on the GPU.** The
  kernel uses `atomic_add_float` on the bed, sediment and track fields. The
  upstream header says results are reproducible "up to atomic scheduling
  order". A CPU port can be deterministic only by choosing a fixed sample order,
  which will not match an arbitrary GPU order.
- **The thermal family is the precedent.** AD-017 puts ports in new files under
  `HighMap/src`/`HighMap/include`; AD-018 puts the GPU oracle in CI; AD-019
  deferred masks and output maps because the JS surface had no concept for
  them. This change follows the placement and parity pattern, but exposes output
  maps because sediment/discharge/flow/water-depth/velocity are the point of the
  hydraulic models.
- **The JS surface has no output-grid concept yet.** `HArray` owns a wasm
  handle (`getRaw()`), `coerceGrid` copies plain inputs, and mutating functions
  return the same `HArray`. The binding layer has no nullable-pointer precedent,
  but embind's `allow_raw_pointers()` accepts `null` for non-reference pointer
  arguments.
- **The desktop build globs `src/*.cpp`; the wasm build uses a curated list.**
  A new file under `HighMap/src` is picked up by both once listed in
  `HighMap/wasm/sources.cmake`. No OpenCL device exists on the development
  machine; CI (PoCL on ubuntu-latest) is the only oracle.

## Goals / Non-Goals

**Goals:**

- One CPU implementation per hydraulic function, in one translation unit, with
  the same arithmetic as the kernels and the same public signatures/defaults as
  the GPU declarations.
- A deterministic CPU family: fixed sample order, no RNG state, no shared
  mutable state between calls.
- An output-map contract that fits the existing `HArray` API: caller-allocated
  outputs, validated before the wasm call, no aliasing of the terrain grid.
- A parity harness that runs the real kernels where the wrappers are broken,
  and that separates McDonald's arithmetic fidelity from its atomic-order
  sensitivity.

**Non-Goals:**

- Showcase demos (a separate change, as with the thermal family).
- The JS surface for the mask, moisture-map and rain-map input overloads; the
  C++ ports implement them and the manifest records the deferral.
- Fixing the upstream GPU wrappers; the port must not require an upstream edit.
- Performance work: no OpenMP, no pthreads, no tiling. Cost is measured and
  documented, not optimized.
- Any change to the existing thermal, Musgrave or stream-erosion paths.

## Decisions

### D1: Port sources live in the upstream source tree

New files: `HighMap/src/erosion/hydraulic_cpu.cpp` (five ports) and
`HighMap/include/highmap/erosion_hydraulic_cpu.hpp` (declarations). No existing
upstream file is edited. This is AD-017 applied to the next family: the desktop
glob compiles the port automatically, so the parity oracle and the shipped
artifact execute the same translation unit, and the desktop library stops
having GPU-only hydraulic algorithms.

Alternatives considered:

- `HighMap/wasm/ports/` keeps the upstream tree pristine, but the desktop
  parity harness would compile the port outside the upstream target with a
  different flag set, and the desktop library would keep shipping no CPU
  hydraulic code. Rejected for the same reason as AD-017.
- Editing the GPU wrappers to fix the broken launches would make the desktop
  API correct but adds rebase cost and violates the additive rule. Rejected;
  the harness uses a direct-kernel oracle instead (D5).
- Naming the file `hydraulic.cpp`: upstream would likely choose that name for
  its own CPU contribution. The `_cpu` suffix yields a loud duplicate-symbol
  link error if upstream implements the same functions, which is the signal we
  want.

The file header states the port's provenance, the kernel transcribed per
function, and the rebase rule ("if upstream ships CPU implementations, delete
this file and the header").

### D2: Namespace `hmap`, signatures mirroring the public GPU declarations

The five functions are declared in `hmap` with the same parameter lists and
defaults as the public GPU declarations:

| function | overloads / notes |
|---|---|
| `hydraulic_mcdonald` | array form; optional sediment and discharge outputs |
| `hydraulic_mcdonald_multiscale` | `steps_per_level` default `{512, 256, 128}`; optional sediment and discharge outputs |
| `hydraulic_schott` | non-mask form; mask overload with the public header order (`z, iterations, talus, p_mask, ...`) |
| `hydraulic_schott_erosion` | optional moisture map and flow output |
| `hydraulic_vpipes` | optional rain map, water depth, sediment and velocity outputs |

Input-only pointers are declared `const` on the CPU side even where the GPU
declaration is unnecessarily non-const (`p_mask`, `p_rain_map`); callers that
pass non-const pointers still compile. The Schott mask overload follows the
public header's order, not the GPU definition's different order; the GPU
definition is an upstream inconsistency and is not a call target for the
parity harness.

Defaults are copied verbatim from `erosion.hpp`, including
`world_extent_km = 40`, `z_scale_km = 4`, `samples = 8192`, `maxage = 512`,
`lrate = 0.2`, `time_step = 10`, `rainfall = 1`, `evap_rate = 1e-4`,
`gravity = 9.81`, `viscosity = 0.025`, `bed_shear = 0.01`,
`crit_slope = 0.57`, `settle_rate = 0.1`, `thermal_rate = 2.5e-3`,
`deposition_rate = 5e-3`, `suspension_rate = 2.5e-4`, `exit_slope = 0.01`,
and the vpipes defaults `water_height = 1e-2`, `maintain_water_volume = true`,
`evap_rate = 0.1`, `iterations = 50`, `dt = 0.5`, `k_capacity = 0.5`,
`k_erode = 0.001`, `k_depose = 0.01`, `k_discharge_exp = 1`,
`downcutting_max_depth_ratio = 10`, `flux_diffusion = true`,
`flux_diffusion_strength = 0.01`.

### D3: Transcription, not re-derivation

Each port translates its kernel line by line. Shared helpers exist only where
the kernels share them.

**McDonald**

- `hash21u` and `mcd_uniform(seed, step, n, salt)` exactly as
  `_common_rand.cl` and `hydraulic_mcdonald.cl` define them, including the
  `>> 8` / `16777216.f` normalization.
- `mcd_grad5` with the kernel's five-point stencil, its one-sided-aware
  coefficient table and its `zs = z_m / cell_m` scaling. The kernel's boundary
  checks (`p.y` for the x-gradient, `p.x` for the y-gradient) are kept
  literally; `p` is always inside the grid, so they reduce to the expected
  axis checks.
- `mcdonald_solve`: per-sample start position, early exits, velocity update,
  discharge/slope transfer, sediment/bed exchange, evaporation and age loop.
  The only intentional deviation is that `atomic_add_float` becomes a direct
  `+=` in sample index order. That is what makes the CPU result deterministic.
- `mcdonald_filter`: `mix(a, b, rate) = a + rate * (b - a)`, evaluated in that
  order.
- `mcdonald_debris`: the 256-age loop, `fmax`/`fmin` clamps, bank-stability
  threshold and sediment/bed transfer. `length` becomes `std::sqrt` of the
  sum of squares.
- `hydraulic_mcdonald` and `_multiscale` mirror the wrapper's state: bedrock,
  sediment, discharge, momentum x/y; per-step track reset; solve, filter,
  debris; multiscale halving ladder with `resample_to_shape` and `seed + level`.
  The final terrain is `bed + sed`, and the outputs are the final `sed` and
  `dis`.

**Schott**

- `schott_get_slope`, `find_steepest_downslope_neighbor`, `schott_get_weight`
  and `schott_get_flow` use clamp-to-edge sampling. The CPU helpers clamp the
  sampled coordinate to `[0, nx-1]`/`[0, ny-1]`; they do not wrap or zero-fill.
- The full procedure runs the kernel's phase cycle: `erosion_it` and
  `thermal_it` are computed from the weights exactly as the wrapper does, then
  `it % 10` selects erosion, thermal or deposition. The kernel's `it` argument
  is part of the port even though the pinned wrapper never binds it.
- The erosion phase uses `z_val = max(z_steepest, z_val - spe)` with
  `spe = c_erosion * min(3, pow(flow, flow_acc_exponent) * speed)`.
- The thermal phase counts up/down neighbours against the local talus and adds
  `c_thermal * (up - down) * talus`.
- The deposition phase routes sediment with `schott_get_flow`, applies
  `min(sed, c_deposition * (sed - spe))` and adds `c_deposition * spe`.
- The erosion-only form mirrors `hydraulic_schott_erosion`: flow starts at 1 or
  the supplied flow map, moisture starts at 1 or the supplied map, and the
  output flow is `moisture + schott_get_flow(...)`.
- The mask overload runs the unmasked form on a copy and returns
  `lerp(z, z_eroded, mask)`, matching `hydraulic_stream`'s mask convention.

**Vpipes**

- The pass sequence follows the fixed orchestration from the upstream `dev`
  branch's `hydraulic_vpipes_gpu.cpp`, not the stale pinned wrapper:
  1. host-side rain increment when `maintain_water_volume` is set;
  2. `flow_pass`: `d1 = d`, flux update from `z` and `d1`, optional flux
     diffusion, `sum > 1e-5` normalization with `k = clamp(d0 / (sum * dt),
     0, 1)`;
  3. `water_pass`: incoming/outgoing flux divergence, `max(0, ...)` depth,
     evaporation, velocity from flux differences and
     `dmean = max(0.001 * water_height, d2)`;
  4. `erosion_pass`: central gradient of `z + s`, `dzn = max(1e-3, nx * talus)`,
     `salpha = max(0.01, dzn / hypot(1, dzn))`, capacity
     `k_capacity * pow(depth * speed, k_discharge_exp) * salpha`, then
     erosion or deposition with `k_erode`/`k_depose`;
  5. `sediment_transport_pass`: bilinear sample of `s` at
     `(i + 0.5 - dt * u, j + 0.5 - dt * v)` with clamp-to-edge.
- `outflow_boundaries` is hardcoded to 0, matching the fixed upstream wrapper;
  the pinned wrapper exposes no such parameter. This is recorded as an
  assumption, not a new option.
- All stencil passes write to fresh arrays and swap, so no cell reads a
  neighbour that has already been updated in the same pass. This is the CPU
  equivalent of the GPU's separate input/output images.

No port calls `extrapolate_borders`. The hydraulic kernels use clamp-to-edge or
per-particle bounds; there is no post-loop border pass to reproduce.

### D4: Output maps as caller-allocated `HArray`s

The JS options object carries optional output maps as `HArray`s:

| function | output options |
|---|---|
| `hydraulicMcdonald` | `sediment`, `discharge` |
| `hydraulicMcdonaldMultiscale` | `sediment`, `discharge` |
| `hydraulicSchott` | `flow` |
| `hydraulicSchottErosion` | `flow` |
| `hydraulicVpipes` | `waterDepth`, `sediment`, `velU`, `velV` |

The caller allocates them with `makeGrid(ni, nj)`; the API layer validates that
each is an `HArray`, alive, the same shape as the terrain grid, and not the same
wrapper as the terrain grid or another output. It then passes the raw handles to
the binding, or `null` for an omitted map. The C++ port assigns the output only
when the pointer is non-null. The terrain function still returns the same
`HArray` as an ordinary in-place call.

Why caller-allocated outputs instead of a result object:

- The existing mutating functions return `HArray`; a union of `HArray` and
  `{ grid, maps }` would complicate every call site and the TypeScript surface.
- The C++ API already speaks `Array*` outputs; passing handles is a direct
  mapping.
- The caller controls lifetime and can free maps it does not need.

Alternatives considered: always returning a result object (ergonomic for maps,
but breaks the in-place return convention and forces `.grid` everywhere);
auto-allocating maps and returning them in an object (hides memory and lifetime,
and makes "no maps requested" a special case); separate `*WithMaps` functions
(doubles the surface and the tests).

Input maps and masks are deferred: the C++ ports implement `hydraulic_schott`'s
mask, `hydraulic_schott_erosion`'s moisture map and `hydraulic_vpipes`'s rain
map, but the JS surface binds only the default-map forms. The manifest records
the deferral, as it did for the thermal mask/deposition overloads (AD-019).

### D5: Parity harness with a direct-kernel oracle

`tests/src/test_hydraulic_cpu.cpp` is picked up by `tests/CMakeLists.txt`'s
`src/*.cpp` glob. It has two suites:

- `HydraulicCpuInvariants` — CPU-only properties: determinism, output shapes,
  mask blend, zero-iteration no-op, non-negative water depth, finite maps.
- `HydraulicParity` — GPU-vs-CPU comparison; skips only when no OpenCL device
  or no usable float image format is available, and the CI job treats a skip as
  a failure except for the recorded image-support exception.

**McDonald** uses two case types:

- *Single-particle arithmetic*: one sample, one or two steps, a small grid with
  exact values. This compares `z`, `sediment`, `discharge`, `mx` and `my` per
  cell against the working `hmap::gpu::hydraulic_mcdonald` wrapper (or direct
  kernels if the wrapper is unavailable). Bounds are continuous-variant
  bounds.
- *Multi-particle behaviour*: many samples over a few steps. Because GPU atomic
  order is not reproducible, this compares aggregate metrics — total sediment,
  total discharge, mean absolute elevation change, and the spread of elevation
  changes — against calibrated relative bounds. Per-cell max/RMS are reported
  for diagnosis but do not gate the case.

**Schott** uses a test-local direct-kernel reference that mirrors the wrapper
but passes the kernel's `it` argument. It binds `z`, `flow`, `sediment` and
`talus` as read-only images, `z_new`, `flow_new` and `sediment_new` as
write-only images, and loops the phase cycle. Cases isolate the phases by
weights: erosion-only (`thermal_weight = 0`, `deposition_weight = 0`),
erosion + thermal (`deposition_weight = 0`), and the default mix. The
erosion-only public form is compared through its working wrapper or the same
direct-kernel helper.

**Vpipes** uses a test-local direct-kernel reference that mirrors the fixed
`dev`-branch orchestration: `flow_pass`, `water_pass`, `erosion_pass`,
`sediment_transport_pass`, with the host-side rain increment. It compares `z`,
`water_depth`, `sediment`, `vel_u` and `vel_v` per cell. Cases cover
`flux_diffusion` on/off, `maintain_water_volume` on/off, `evap_rate = 0` vs
default, and one vs several iterations.

**Image format.** The pinned CLWrapper binds `CL_R`/`CL_FLOAT` images. The CI
PoCL device rejects that format for `thermal_conserve`, so the hydraulic
harness probes `CL_R` first and falls back to `CL_RGBA`/`CL_FLOAT` through raw
`cl::Image2D` bindings in the test. The kernels read `.x`, so `CL_RGBA` is
behaviourally equivalent. If neither format works, Schott/vpipes cases skip
with the explicit "no float image support" message, which the CI guard allows
and the parity report records.

Provisionary bounds, calibrated and frozen by the first oracle run:

- continuous passes (McDonald single-particle, Schott erosion/deposition,
  vpipes): `max <= 1e-5 * range`, `rms <= 2e-6 * range`;
- Schott thermal phase (discrete up/down counts can flip): `max <= 2e-2 *
  range`, `rms <= 2e-4 * range`;
- McDonald multi-particle aggregates: relative bounds on total sediment and
  discharge (start at 5%) and `mean |Δz| <= 1e-2 * range`, tightened or
  loosened only with measured numbers.

Calibration rule: one deliberate mutation per algorithm must break its bound —
a McDonald transfer coefficient, a Schott phase threshold or erosion
coefficient, and a vpipes capacity coefficient. The mutation is applied once,
recorded, and reverted.

### D6: Parity CI workflow

The existing `.github/workflows/thermal-parity.yml` already builds the desktop
library with PoCL and runs the thermal suite. Rather than duplicate the
75-minute build in a second workflow, the file is renamed to
`.github/workflows/erosion-parity.yml` and gains a second run step for
`HydraulicCpuInvariants.*:HydraulicParity.*`. The job summary and uploaded
artifacts include both metric sets; the skip guard allows only the known
image-support messages and fails on any other skip, including a missing device.
The `no_device` dispatch input stays.

Alternative: a separate `hydraulic-parity.yml` is simpler to read but doubles
the build cost. Rejected. Extending the upstream `cmake.yml` remains rejected
because it is an upstream file and never runs the test binary.

### D7: Manifest, bindings and JS layer growth

Five manifest rows with JS names `hydraulicMcdonald`,
`hydraulicMcdonaldMultiscale`, `hydraulicSchott`, `hydraulicSchottErosion` and
`hydraulicVpipes`. Required positional arguments:

- `hydraulicMcdonald(grid, steps, seed, options?)`;
- `hydraulicMcdonaldMultiscale(grid, seed, options?)`;
- `hydraulicSchott(grid, talus, options?)`;
- `hydraulicSchottErosion(grid, options?)`;
- `hydraulicVpipes(grid, options?)`.

Option defaults mirror the C++ declarations. `iterations` defaults to 10 for
the two Schott functions, whose C++ declarations have no default, following the
thermal precedent (AD-020); vpipes keeps its C++ default of 50. `talus` is a
required positional argument and accepts `number | GridLike`, exactly like the
thermal family.

The binding layer uses `allow_raw_pointers()` for the output `hmap::Array*`
arguments and accepts `null`. `hydraulicMcdonaldMultiscale`'s
`steps_per_level` is bound through an `emscripten::val` array-to-vector helper,
because a registered `std::vector<int>` would force JS callers to construct an
embind vector.

The API layer gets an `inplaceWithOutputs` helper alongside `inplaceWithAux`:
it coerces the terrain grid, validates the output `HArray`s, passes nullable
raw handles, and returns the same terrain wrapper. `scripts/index.d.ts` gains
the five functions and the option interfaces; `src/index.ts` exports the new
types; `tests/surface.test.ts`'s expected name list grows.

`tests/hydraulic.test.ts` covers in-place semantics, plain-input isolation,
required arguments, option defaults, scalar-vs-grid talus, output-map shape and
aliasing rejection, omitted outputs, determinism and finiteness. The golden set
gains small deterministic scenes for McDonald, Schott and vpipes, captured from
the artifact (AD-011).

### D8: Documentation

New AD entries: the hydraulic port placement and provenance; the
caller-allocated output-map contract; the direct-kernel parity oracle and the
three upstream wrapper gaps; and the McDonald atomic-order parity split.
`README.md`'s API list and `docs/wasm-port-research.md`'s phase-2 status are
updated. `docs/hydraulic-parity.md` records the oracle device/driver, the exact
harness invocation, the bound table, the mutation checks, the measured metrics,
the image-format fallback and the wrapper gaps.

## Risks / Trade-offs

- **McDonald's GPU result is not reproducible** → the harness separates
  single-particle per-cell fidelity from multi-particle aggregate behaviour;
  the CPU port is deterministic and the report states that a per-cell
  multi-particle bound would be meaningless.
- **Broken upstream wrappers** → the direct-kernel oracle tests the kernels
  without editing upstream; the report names each gap and records a candidate
  upstream issue. If upstream fixes a wrapper, the direct-kernel helper can be
  retired or kept as a second oracle.
- **Image-format support on CI** → probe `CL_R` then `CL_RGBA`; skip only if
  neither works; the CI guard allows only that explicit message and the report
  records the gap. If `CL_RGBA` also fails, Schott/vpipes parity is not claimed
  on that device and the CPU invariants carry the coverage.
- **Output-map aliasing and lifetime** → the JS layer validates `HArray`
  liveness, shape and distinctness before the call; the caller owns the maps;
  the C++ port writes only when the pointer is non-null.
- **Many vpipes state arrays** → the port allocates several full-grid arrays
  (depth, depth copy, fluxes, velocity, sediment, rain map). Memory is
  O(10 × grid); documented, not optimized. McDonald is dominated by
  `steps × samples × maxage`; Schott is a per-cell stencil.
- **Source-list dependencies** → `math.cpp`, `array/methods.cpp` and
  `interpolate/interpolate_array.cpp` are already in `sources.cmake`; the wasm
  build task verifies the new translation unit links without OpenCL or OpenCV.
- **Upstream duplicate** → the `_cpu` suffix yields a loud duplicate-symbol
  link error; deleting the port file and header is the whole rebase fix because
  bindings and tests speak only the `hmap::` names.
- **Schott mask overload inconsistency** → the CPU header uses the public
  header order; the JS surface defers masks; the mask blend is covered as a CPU
  invariant, not through the broken GPU overload.
- **Vpipes `outflow_boundaries` is not exposed** → the fixed upstream wrapper
  hardcodes 0; the CPU port does the same. If upstream exposes it later, add it
  as a backward-compatible option.
- **The JS surface grows by five functions with many options** → the manifest
  is the source of truth, the option interfaces are exported, and the surface
  test asserts the exact name list.

## Migration Plan

Purely additive. Rollout: land the port file and header, then the bindings and
JS layer, then the parity harness and workflow, then docs. Rollback: delete the
new files, drop the five manifest rows and their binding lines, revert the
`sources.cmake` entry and the JS/TS/workflow/doc changes. No persisted data or
on-disk format is involved.

## Open Questions

- Final tolerance table and McDonald aggregate bounds — settled by the first
  oracle run; the mechanism and calibration rule are fixed here.
- Whether the CI PoCL device accepts `CL_RGBA`/`CL_FLOAT` — settled by the
  first run; if not, the report records the image-support gap.
- Whether McDonald's multi-particle bound should be relative to GPU-vs-GPU
  variability or a fixed calibrated bound — settled by the first run's
  measured spread.
