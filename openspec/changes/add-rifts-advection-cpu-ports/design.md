# Design

## Context

See `proposal.md` — Why. The constraints that shape this design:

- **Upstream declares the three targets only in `hmap::gpu`.**
  `rifts` is declared in `HighMap/include/highmap/erosion.hpp:1448-1467`;
  `advection_particle` (six overloads) and `advection_warp` (two overloads) are
  declared in `HighMap/include/highmap/transform.hpp:551-675`. The
  implementations are launch wrappers in `src/erosion/rifts_gpu.cpp` and
  `src/transform/advection_{particle,warp}_gpu.cpp`. There is no CPU
  declaration or implementation surface.
- **The kernels are the specification.** `rifts.cl` carries the whole per-cell
  computation; `advection_particle.cl` carries the seeded walk; and
  `advection_warp.cl` carries the two-pass uphill walk. The wrappers add
  iteration, buffer plumbing, post-filtering and mask blending, but no
  arithmetic of their own.
- **Some kernel helpers have no CPU twin, and the CPU `lerp` is different.**
  `base_voronoi_f1df2` (`voronoi_base.cl`), `hash22f` and `wang_hash`/`rand`
  (`_common_rand.cl`), the smooth min/max family and `remap_from`
  (`_common_math.cl`) exist only as device functions. Upstream `hmap::lerp` is
  `std::lerp` (`src/math/core.cpp:63`), while the kernel `lerp` is
  `(1 - t) * a + t * b`; the ports must use the kernel form.
- **The pinned wrappers are partly broken.** The particle wrapper allocates
  `out`/`count` as read-write buffers but never uploads zeroed contents, and
  the kernel accumulates them non-atomically. The warp wrapper binds
  `CL_R`/`CL_FLOAT` images, a format the CI PoCL device has rejected before
  (`thermal_conserve`). The rifts wrapper is healthy. These are upstream gaps,
  not porting problems.
- **Build layout.** The desktop build globs `src/*.cpp`; the wasm build uses
  the curated `HighMap/wasm/sources.cmake`. The desktop compiles with
  `-Ofast -ffast-math`, the wasm target with `-Os` and no fast-math. No OpenCL
  device exists on the development machine; CI (PoCL on ubuntu-latest) is the
  only oracle.
- **The JS surface has no new-grid convention in the erosion/transform block.**
  `HArray` owns a wasm handle, `coerceGrid` copies plain inputs, and the
  mutating families return the same wrapper. `hillshade` and the algebra layer
  already return fresh grids, but the erosion/transform families do not. The
  two advection functions are the first functions there that produce a new
  grid, so the TS layer needs a helper.
- **Manifest convention binds fixed-size math types as scalars.** The noise
  family takes `kx, ky` as separate numbers even though `hmap::noise` takes a
  `glm::vec2`. There is no registered `glm::vec2` on the JS side, and
  `emscripten::val` is reserved for variable-length arrays.

## Goals / Non-Goals

**Goals:**

- Three CPU implementations with the same arithmetic as their kernels and the
  same public signatures/defaults as the `hmap::gpu::` declarations, split over
  two family translation units.
- Defined, deterministic behaviour everywhere the kernels leave undefined
  behaviour, with each deviation documented and kept away from the parity
  bounds.
- A JS surface for the three primary forms with explicit in-place versus
  new-grid semantics, tuple-to-scalar conversion at the binding boundary and a
  reusable new-grid helper.
- A parity harness that separates arithmetic fidelity from hash discontinuity,
  sampler precision and non-atomic accumulation, and that runs the real kernels
  where the pinned wrappers cannot.

**Non-Goals:**

- Fixing the upstream wrappers or kernels; the port must not require an
  upstream edit.
- Performance work: no OpenMP, no pthreads, no tiling, no SIMD. Cost is
  measured and documented.
- The JS surface for masks, noise-offset grids, vector/multi-field returns and
  explicit `dx`/`dy` forms. The C++ ports implement them and the manifest
  records the deferral.
- The GPU-only primitive noise generators (`primitives/primitives_gpu.cpp`),
  which are outside the phase-2 erosion/transform scope.
- Any change to the thermal or hydraulic ports, the showcase, or the existing
  `hillshade` path.

## Decisions

### D1: Ports live as two additive translation units in the upstream tree

New files, no upstream edits:

| file | contents |
|---|---|
| `HighMap/src/erosion/rifts_cpu.cpp` | `hmap::rifts` |
| `HighMap/include/highmap/erosion_rifts_cpu.hpp` | its declaration |
| `HighMap/src/transform/advection_cpu.cpp` | `hmap::advection_particle` (six overloads) and `hmap::advection_warp` (two overloads) |
| `HighMap/include/highmap/transform_advection_cpu.hpp` | their declarations |

The desktop `file(GLOB_RECURSE ... src/*.cpp)` picks both translation units up
automatically; `HighMap/wasm/sources.cmake` gains two rows under a port comment,
so the parity oracle and the shipped artifact execute the same code. The files
include only upstream CPU headers (`array`, `gradient`, `filters`,
`internal/validation`) and the standard library — no `cl_wrapper`, no OpenCL
header, no OpenMP pragma; the implementation tasks grep for all three.

Two units rather than one because the families live in different upstream
directories and need different dependencies: the rifts port is self-contained,
the advection port needs `gradient.hpp` and `filters.hpp`. A single combined
file would mix family ownership and force transform includes on the erosion
port.

The `_cpu` suffix is deliberate: if upstream ever ships CPU implementations
with these names, the link fails loudly with duplicate symbols. Deleting the
port file and its header is the whole rebase fix, because bindings, tests and
the JS layer speak only the `hmap::` names.

Alternatives considered:

- `HighMap/wasm/ports/`: keeps the upstream tree pristine but makes the desktop
  parity harness compile the port with a different flag set, breaking the
  same-source property (rejected for the reasons in AD-017/AD-022).
- `HighMap/src/erosion/rifts.cpp` and `HighMap/src/transform/advection.cpp`:
  the names upstream would most likely choose for its own CPU contribution,
  producing an add/add conflict. Rejected.

The file header records provenance, the kernel transcribed, and the rebase rule
(“if upstream ships a CPU implementation, delete this file and the header”).

### D2: Namespace `hmap`, signatures mirroring `hmap::gpu::*`

The ports are declared in `hmap` with the same parameter lists, types and
defaults as the GPU declarations:

| function | overloads mirrored |
|---|---|
| `rifts` | one: `(z&, kw, angle, amplitude, seed, ...)` |
| `advection_particle` | six: z/field and z/vector, each with and without `iterations`, plus the two explicit `(dx, dy, ...)` forms |
| `advection_warp` | two: z-driven and explicit `(z, field, dx, dy, ...)` |

Defaults are copied verbatim from `erosion.hpp` / `transform.hpp`, including
`elevation_noise_shift = 0`, `k_smooth_bottom = 0.05`, `k_smooth_top = 0.05`,
`radial_spread_amp = 0.2`, `elevation_noise_amp = 0.1`, `clamp_vmin = 0`,
`remap_vmin = 0`, `apply_mask = true`, `reverse_mask = false`,
`mask_gamma = 1`, `center = {0.5, 0.5}`, `bbox = {0, 1, 0, 1}`,
`post_filter = true`, `post_filter_sigma = 0.125`, `advection_length = 0.1`,
`value_persistence = 0.99` (particle) / `0.9` (warp), `inertia = 0`.

`kw` stays a required argument with no default even though the upstream comment
suggests `{4, 1.2}`; the declaration has no default and the JS surface treats it
as required. The `iterations` overloads of `advection_particle` also keep no
default. `glm::vec2`/`glm::vec4` and `const Array *` stay in the C++ signatures;
the ports do not decompose or reorder them.

Read-only pointer inputs are declared `const` exactly as upstream already does.
`rifts` takes `Array &` because it mutates in place; the advection functions
take `const Array &` and return `Array` / `std::vector<Array>` by value.

The vector overloads are implemented even though the JS surface defers them:
the C++ family must stay complete for the parity harness and for a future
surface. The single-field forms delegate to the vector form exactly as the GPU
wrapper does, and the z-driven forms compute `dx = -gradient_x(z)`,
`dy = -gradient_y(z)` (downhill) for particle and `dx = gradient_x(z)`,
`dy = gradient_y(z)` (uphill) for warp.

### D3: Rifts is transcribed literally, warts included

`rifts_cpu.cpp` is a line-by-line translation of `rifts.cl` plus the device
helpers it uses. The helpers are re-declared file-local in an anonymous
namespace with their kernel names (`base_voronoi_f1df2`, `hash22f`, `smin`,
`smax`, `max_smooth`, `min_smooth`, `pow_int`, `remap_from`, `wang_hash`,
`rand`, `lerp`), so the diff against the kernel stays readable. Keeping them
local is also necessary for correctness: no upstream CPU helper provides them,
and `hmap::lerp` is `std::lerp`.

Fidelity points a naive rewrite would get wrong:

- `wang_hash` and `rand` (`xorshift` then `* (1.f / 4294967296.f)`) derive one
  `fseed` from the call seed. The kernel runs them per work-item, but the input
  is the same for every cell, so `fseed` is a global constant; the port computes
  it once. Cell variation comes from `hash22f(i + dr, fseed)`, not from
  `fseed`.
- `hash22f` uses `dot(p, (127.1, 311.7))` and `dot(p, (269.5, 183.3))`, then
  `fract(sin(p + fseed) * 43758.5453123f)` with `fract(x) = x - floor(x)`.
- `base_voronoi_f1df2` scans the 3×3 neighbourhood with `FLT_MAX` seeds and the
  kernel's two-line update order: `new_min1 = smin(min1, dist, k)`;
  `new_min2 = smin(min2, smax(min1, dist, k), k)`; then assign both. The result
  is `min1 / min2`, not a distance difference.
- `smin`/`smax` dispatch on `k > 0` to `max_smooth`/`min_smooth`, which use
  `pow_int(h, 3)` — a loop, not `std::pow`.
- `g_to_xy` divides by `nx`/`ny`, **not** `nx - 1`, and has no half-pixel shift:
  `x = gx / nx`, then `x = 1 * (x * (bbox.y - bbox.x) + bbox.x) + 1 * dx`, with
  the per-cell noise offsets added after the bbox scaling. The bbox component
  order is `(xmin, xmax, ymin, ymax)`.
- The center broadcast quirk is preserved verbatim: `dr_x` uses
  `dot(pos - center.x, dir)` and `dr_y` uses `dot(pos - center.y, perp)`, where
  the scalar `center.x`/`center.y` is broadcast to both components. In scalar
  form: `dr_x = (pos.x - center.x) * c + (pos.y - center.x) * s` and
  `dr_y = (pos.x - center.y) * (-s) + (pos.y - center.y) * c`. This is not
  `pos - center`; “fixing” it would move the rift field.
- The radial spread uses the literal sigmoid expression
  `-2 / (1 + exp(-20 * dr_x)) + 1`, not the mathematically equivalent
  `-tanh(10 * dr_x)`, to keep the rounding.
- `noise_v = smax(min(noise_v, 1), clamp_vmin, k_smooth_top)` then
  `noise_v = remap_from(noise_v, 1, remap_vmin, clamp_vmin, 1)`. The
  `vmin`/`vmax` argument order is intentional.
- The elevation mask is `t = apply_mask ? pow(val0, mask_gamma) : 1`, then
  `reverse_mask` flips `t` **even when `apply_mask` is false**. So
  `apply_mask = false, reverse_mask = true` disables the rifts entirely. This
  quirk is preserved and tested.
- Both blends are `lerp(val0, val, t)` with the kernel form
  `(1 - t) * a + t * b`; `val0` is always the original cell value.
- The angle constant is `angle / 180 * 3.141592f`.
- The unused `dir` local of the kernel is dropped with a comment; nothing else
  is dropped.
- `pow(val0, mask_gamma)` on a negative `val0` is implementation-defined in
  OpenCL for non-integral exponents. The port calls `std::pow`; parity scenes
  use non-negative elevation and the report records the domain caveat.

`rifts` has no border pass, no iteration and no shared state: each cell is
independent, reads only its own value, and writes back in place. The serial
loop is `j` outer, `i` inner, matching the kernel's row-major
`linear_index(i, j, nx) = j * nx + i`.

### D4: Warp reproduces the mirrored-repeat sampler and the two-pass walk

`advection_warp` depends on OpenCL image sampling:
`CLK_NORMALIZED_COORDS_TRUE | CLK_ADDRESS_MIRRORED_REPEAT | CLK_FILTER_LINEAR`.
The port implements that sampler as a file-local function over `hmap::Array`:

1. For normalized coordinate `u` and size `N`, take `s = u * N`.
2. Fold into the mirrored period: `m = fmod(s, 2N)`; if `m < 0`, `m += 2N`;
   if `m > N`, `m = 2N - m`.
3. Convert to a continuous texel coordinate `q = m - 0.5`, then
   `i0 = floor(q)`, `f = q - i0`, `i1 = i0 + 1`.
4. Clamp `i0` and `i1` to `[0, N - 1]` and interpolate
   `(1 - f) * texel(i0) + f * texel(i1)`.

The same folding runs on both axes for bilinear reads. This is equivalent to
texel centers at `(i + 0.5) / N`, mirrored-repeat addressing and linear
filtering; `u = 0` and `u = 1` land on the edge texels. The kernel reads only
`.x` from each image, so 1D sampling per axis is sufficient.

Alternatives considered:

- `Array::bilinear_interp` uses pixel coordinates and clamp-to-edge, so it is
  wrong at the borders and on the normalized mapping. Rejected.
- A precomputed mirror LUT: extra memory and no behaviour gain for the few
  samples per cell. Rejected.
- Clamp instead of mirror: diverges exactly where the path touches a border,
  which is the common case for a gradient-driven walk. Rejected.

The walk itself is a literal transcription:

- The z-driven form computes `hmap::gradient_x(z)` and `hmap::gradient_y(z)`
  (uphill, no negation); the explicit form takes `dx`/`dy`.
- `pos0 = ((gx + 0.5) / nx, (gy + 0.5) / ny)`, `drx = 1 / nx`, `dry = 1 / ny`,
  `eps = 0`, `z_prev = sample(z, pos0)`.
- `max_steps = ceil(advection_length / min(drx, dry))`,
  `limit = min(max_steps, 1024)`.
- Upward pass: store `pos`; sample `vel`; `len = hypot(vel.x, vel.y)`; if
  `len > 0` set `dir = vel / len` (otherwise keep the previous direction);
  advance one pixel; break if `pos` leaves `[0, 1]²`; break if
  `z_current - z_prev < -1e-5`; update `z_prev`.
- Downward pass: `val = 0`; if the path is non-empty, seed it from
  `field(path.back())`; then walk backwards, re-reading `field(path[i])` (the
  last position is read twice), with
  `ratio = mask * value_persistence` and
  `val = ratio * val + (1 - ratio) * new_val`. A null mask is a constant 1, as
  the wrapper materializes.

Two quirks are preserved rather than fixed:

- `advection_length = 0` yields `limit = 0`, an empty path and an **all-zero**
  output, not a copy of the input. The CPU port does the same and tests it.
- `MAX_STEPS = 1024` caps the path even when `advection_length / min(drx, dry)`
  is larger (very small grids or long advection lengths).

One deviation is forced by undefined behaviour: the kernel declares `dir`
uninitialized and uses it on the first step if the start is flat. The CPU port
initializes it to `(0, 0)`, so a flat start does not move until a non-zero
gradient appears. This is documented in the port and the parity report; flat
scenes are excluded from the warp parity bounds.

The path is a `std::vector<glm::vec2>` reserved to `limit` and reused across
cells, so memory is bounded by 1024 positions and no per-cell allocation
churn dominates.

### D5: Particle advection is deterministic, explicitly zeroed and guarded

The port has one solver, `advection_particle_once`, and the six overloads
orchestrate it exactly like the wrapper:

- The z-driven forms compute `dx = -gradient_x(z)`, `dy = -gradient_y(z)`
  (downhill); the explicit forms use the supplied `dx`/`dy`. Single-field
  forms delegate to the vector form, and the `iterations` forms loop the
  single-pass solver with the same seed.
- Input validation uses the upstream `validate_non_empty` /
  `validate_same_shape` helpers and returns an empty array/vector on mismatch,
  matching the GPU wrappers.

The solver is transcribed from `advection_particle.cl`:

- `rng = pcg_hash(seed + id * 2u)`; `rand` is the same xorshift and `2^-32`
  normalization.
- The initial position is `((nx - 1) * rand, (ny - 1) * rand)` — `nx - 1`, not
  `nx`. With an advection mask, up to 100 rejection draws are made; the loop
  breaks when `advection_mask[idx] > 0`, despite the comment saying “outside
  the advection zone”. If no draw passes, the last draw is used, exactly as the
  kernel does.
- `dist_max = advection_length * nx`; `dist` increments by 1 per iteration
  while `dist < dist_max`.
- Per step: `vel = (dx[idx], dy[idx])`; if `len > 1e-9`, blend
  `inertia * dir_prev + (1 - inertia) * dir` and normalize; move one pixel
  with `advection_sign` (`-1` when `reverse`); skip accumulation when both
  position components moved by less than `1e-9`; truncate to `(int)` for the
  cell index; stop outside the grid or where the advection mask is zero.
- Per field: `val[f] = lerp(field[idx], val[f], value_persistence)` with the
  kernel `lerp`; `out[f][idx] += val[f]`; `count[idx] += 1`.

The CPU implementation differs from the pinned GPU path only where the GPU path
is broken or undefined, and each difference is explicit:

- **Explicit zeroing.** `out` (one grid per field) and `count` are zeroed at
  the start. The wrapper allocates them read-write but never uploads zeros, so
  the GPU kernel accumulates into undefined memory; the CPU port is correct by
  construction and the parity reference must upload zeros the same way.
- **Deterministic accumulation.** Particles are processed in ascending id
  order and fields in ascending field order. The kernel uses non-atomic
  `+=`, so a multi-particle GPU run is not reproducible; the CPU order is
  fixed and documented. The wrapper's host stage is reproduced exactly:
  `out = (accum + original) / (count + 1)` clamped to `[0, 1]`, or the original
  where `count == 0`; optional `hmap::laplace(out, p_advection_mask,
  post_filter_sigma, 1)`; optional `lerp(out, original, *p_mask)`.
- **Inertia guard.** With `inertia == 1` and `dir_prev == (0, 0)`, the kernel
  calls `normalize((0, 0))`, which is undefined. The port keeps
  `dir = (0, 0)` when the blended vector is below `1e-9`, so the particle does
  not move; the position-skip branch then behaves as the kernel's `continue`.
  `inertia == 1` is excluded from parity.
- **Dead branch.** The kernel's `if (true) { ... } else { ... }` accumulation
  branch is kept as the only branch; the unreachable `else` is omitted with a
  comment.
- **64-field cap.** `num_f = min(num_fields, 64)` as in the kernel. Fields
  beyond the cap receive the wrapper's post-processing of a zero accumulator,
  which reproduces the GPU outcome (they come back as their original values
  after the filter/mask stage). The JS surface binds one field; the cap is
  documented for C++ callers.

Memory is `num_fields × grid` accumulators plus one `count` grid, allocated
once per call; the solver itself is O(iterations × nparticles × fields ×
steps). Cost is measured, not optimized.

### D6: JS surface: one in-place and two new-grid functions

Three manifest rows:

| JS name | signature | semantics | defaults |
|---|---|---|---|
| `rifts` | `(grid, kw, angle, amplitude, seed, options?)` | in place, returns the input `HArray` | see below |
| `advectionParticle` | `(grid, field, nparticles, seed, options?)` | new grid | `iterations: 1`, `reverse: false`, `postFilter: true`, `postFilterSigma: 0.125`, `advectionLength: 0.1`, `valuePersistence: 0.99`, `inertia: 0` |
| `advectionWarp` | `(grid, field, options?)` | new grid | `advectionLength: 0.1`, `valuePersistence: 0.9` |

`rifts` also exposes `elevationNoiseShift: 0`, `kSmoothBottom: 0.05`,
`kSmoothTop: 0.05`, `radialSpreadAmp: 0.2`, `elevationNoiseAmp: 0.1`,
`clampVmin: 0`, `remapVmin: 0`, `applyMask: true`, `reverseMask: false`,
`maskGamma: 1`, `center: [0.5, 0.5]` and `bbox: [0, 1, 0, 1]`.

`kw` is a required `[number, number]` tuple. At the binding boundary the glm
types become scalars, following the noise family's `kx, ky` convention rather
than `emscripten::val` or a registered `glm::vec2`:

- `kw` → `kwX, kwY`;
- `center` → `centerX, centerY`;
- `bbox` → `bboxXmin, bboxXmax, bboxYmin, bboxYmax`.

The TS layer converts the tuples and validates their length and finiteness. The
bbox order mirrors the C++ `glm::vec4` and is **not** reordered to
`[xmin, ymin, xmax, ymax]`; changing it would silently change output relative to
C++. The manifest and the option doc state the order explicitly, and a test
uses an asymmetric bbox.

New-grid helper. The API layer gains a small helper next to `inplace` and
`inplaceWithOutputs`:

```ts
const newGrid = (
  inputs: { value: GridLike; name: string }[],
  run: (raws: RawArray[]) => RawArray,
): HArray;
```

It coerces every input, validates that each later input matches the first
grid's shape (the advected field must be the same shape as the terrain grid),
calls the binding, wraps the returned raw handle, and frees every temporary
created for plain inputs in a `finally`. HArray inputs pass through untouched;
the returned grid is always a fresh wrapper. Aliasing `grid` and `field` is
allowed because both are read-only; only the result is new. `hillshade` is left
on its existing path (a pre-existing temporary-freeing gap, out of scope).

`advectionParticle` binds the `iterations` overload and defaults to 1, which is
exactly the no-iteration overload's single pass; `iterations: 0` is therefore a
documented no-op. `advectionWarp` binds the z-driven overload.

Deferred, recorded as prose in the manifest rather than table rows (the surface
test parses the table): rifts' `p_noise_x`/`p_noise_y`/`p_mask`, particle's
`p_advection_mask`/`p_mask` and multi-field/explicit `dx`/`dy` forms, and
warp's `p_mask` and explicit `dx`/`dy` form. The C++ ports implement all of
them; the JS surface has no mask/input-grid or array-of-grids concept yet
(AD-019 precedent). The deferral list is written next to the family rows.

`src/loader/module.ts` gets raw signatures, `scripts/index.d.ts` the
hand-written typings and option interfaces, `src/index.ts` the type exports,
`tests/surface.test.ts` the three names, and `tests/rifts-advection.test.ts`
the behaviour tests: in-place versus new-grid identity, plain-input isolation,
required `kw`/`angle`/`amplitude`/`seed`, option defaults, shape rejection,
freed inputs, determinism, finiteness, `iterations: 0`, `advectionLength: 0`,
the `applyMask`/`reverseMask` quirk and an asymmetric bbox. Golden scenes for
the three functions are captured from the wasm artifact (AD-011):
`scripts/golden-capture.mjs` gains the scenes, and `tests/golden.test.ts` /
`tests/golden/scenes.json` pin their hashes.

### D7: Parity oracle: distribution gates and direct-kernel references

`tests/src/test_rifts_advection_cpu.cpp` is picked up by
`tests/CMakeLists.txt`'s `src/*.cpp` glob and has two suites:

- `RiftsAdvectionCpuInvariants` — CPU-only: determinism across two calls,
  output shapes, rifts in-place isolation, the `applyMask`/`reverseMask` quirk,
  warp's zero-length → zero output and finite flat-start output, particle's
  `iterations: 0` no-op, the accumulation/post-processing formula, the inertia
  guard and the mask blends.
- `RiftsAdvectionParity` — GPU-vs-CPU comparison. It skips only with no OpenCL
  device or no usable float image format, and CI treats every other skip as a
  failure.

Oracle per function:

| function | oracle | why |
|---|---|---|
| `rifts` | `hmap::gpu::rifts` | the wrapper binds every argument and has no known gap |
| `advection_warp` | direct `advection_warp` kernel with `CL_R` → `CL_RGBA` probe | the wrapper binds `CL_R` float images, which PoCL may reject; the direct path uses the existing `choose_float_image_format()` idiom |
| `advection_particle` | direct `advection_particle` kernel with explicit zero uploads | the wrapper never uploads `out`/`count` and the kernel accumulates non-atomically |

Scenes are small and deterministic: seeded clamped fBm 32² and a synthetic ramp
32², one 64² case for the rifts neighbourhood, `nparticles ≤ 1000`, and
`advection_length ≤ 0.1`. The warp direct reference computes `dx`/`dy` with the
same `hmap::gradient_x/y` call the wrapper uses, binds `z`, `field`, `dx`,
`dy`, a constant-one mask unless a case supplies one, and `out`, then reads the
output. The particle direct reference binds all buffers including zeroed `out`
and `count` via explicit `write_buffer` calls, executes `nparticles` items and
replicates the wrapper's host post-processing in the test.

Metrics keep the existing `PARITY,<variant>,<scene>,<iters>,<max_abs>,<rms_abs>,<range>`
line for every case, and add two lines in the `PARITY_AGG` spirit:

- `PARITY_PCT,<variant>,<scene>,<iters>,<p99_abs>,<frac_over>,<threshold>,<range>`
  for rifts and warp;
- `PARITY_AGG,<variant>,<scene>,<iters>,<rel_total>,<mean_dz>,<max_abs_diag>,<range>`
  for multi-particle particle advection.

All values are normalised by the input terrain/field range unless marked
relative. `p99_abs` is the 99th percentile of per-cell absolute error;
`frac_over` is the fraction of cells above `threshold = 1e-2 * range`; the
multi-particle case gates on relative total mass and `mean |Δz|`, with per-cell
max/RMS recorded only for diagnosis.

Provisionary bounds, frozen by the first full oracle run:

| case | gate |
|---|---|
| rifts | `rms <= 2e-3 · range`, `p99 <= 2e-2 · range`, `frac_over(1e-2) <= 2e-2`; max diagnostic |
| warp | `rms <= 1e-3 · range`, `p99 <= 1e-2 · range`, `frac_over(1e-2) <= 1e-2`; max diagnostic |
| particle, single particle | `max <= 1e-5 · range`, `rms <= 2e-6 · range` |
| particle, many particles | `rel_total <= 5%`, `mean |Δz| <= 1e-2 · range`; per-cell metrics diagnostic |

Calibration rule: one deliberate mutation per function must break its gate —
a rifts hash or smoothing constant, the warp persistence factor or downhill
sign, and the particle persistence or step sign. Each mutation is applied once,
measured, recorded in `docs/rifts-advection-parity.md`, and reverted. The
multi-particle case is run twice on the GPU side to record GPU-vs-GPU
variability before the aggregate bound is tightened.

The direct-kernel image/buffer helpers (`choose_float_image_format`,
`make_image`, `read_image`, `set_image`, and the buffer equivalents) are copied
from `tests/src/test_hydraulic_cpu.cpp` into this test's anonymous namespace
rather than extracted into a shared header; extraction is deferred (Open
Questions) so this change does not refactor a passing suite.

### D8: The parity workflow gains a third suite

`.github/workflows/erosion-parity.yml` already builds the desktop library with
PoCL and runs the thermal and hydraulic suites. It gains:

- a third run step, `--gtest_filter='RiftsAdvectionCpuInvariants.*:RiftsAdvectionParity.*'`,
  with its own `timeout-minutes: 20`, XML output and a `^PARITY,` presence
  check;
- `PARITY_PCT,` and `PARITY_AGG,` sections in the job summary, next to the
  existing metric blocks;
- the third XML in the skip guard, which still allows only the explicit
  float-image-support message and fails on any other skip;
- the third XML and log in the uploaded artifact set;
- a job timeout raised from 120 to 180 minutes (about 75 build + 20 thermal +
  20 hydraulic + 15 rifts/advection + headroom).

The `no_device` dispatch input stays and must fail the job through the new
suite as well. A separate workflow remains rejected because the 75-minute
build dominates and would be duplicated (same reasoning as AD-022/AD-024).

### D9: Decision records and the parity report

- **AD-026 — rifts/advection CPU ports live in two family translation units.**
  Records the placement, the two-file split and the rebase rule.
- **AD-027 — new-grid JS functions and tuple binding for fixed-size math
  types.** Records the `newGrid` helper, in-place versus new-grid semantics,
  the scalarized `kw`/`center`/`bbox` boundary and the deferred masks/overloads.
- **AD-028 — parity-oracle gaps for rifts, warp and particle advection.**
  Records the hash discontinuity, the mirrored-repeat sampler, the missing zero
  uploads, non-atomic accumulation and the UB deviations.

`docs/rifts-advection-parity.md` follows the hydraulic report: oracle device
and driver, exact harness invocation, bound table, measured metrics, the
mutation-check table, known oracle gaps (including the wrapper gaps and the
`CL_R` fallback) and measured wasm cost. `docs/porting-learnings.md` gains §12
on hash-based noise (per-cell maxima are the wrong gate), image-sampler
fidelity and non-atomic accumulation. `docs/wasm-port-research.md`'s phase-2
status marks the three functions done and leaves the primitives family as the
only remaining GPU-only block, outside phase-2 scope. `README.md`'s API list
grows by three names.

## Risks / Trade-offs

- **Rifts' `fract(sin(...))` hash is discontinuous** → a 1-ULP `sin` difference
  can flip a whole Voronoi cell, so a per-cell max bound is meaningless. Gate
  on RMS, p99 and the fraction of cells over a threshold; calibrate on the
  first CI run; record the hash sensitivity in the report.
- **Warp path flips and sampler precision** → mirrored-repeat linear sampling is
  implementation-defined (OpenCL guarantees ≥9-bit subpixel precision) and a
  one-step-early stop changes the local output. Use the same percentile and
  fraction gates, exclude flat starts from parity, and use the direct kernel
  with the `CL_RGBA` fallback when PoCL rejects `CL_R`.
- **Particle non-atomic accumulation and uninitialized GPU buffers** → the
  multi-particle GPU result is not reproducible. Compare one particle per cell
  arithmetically, compare many particles only by aggregate diagnostics, and
  zero `out`/`count` explicitly in the direct reference.
- **Undefined-behaviour quirks** → warp's uninitialized `dir` and the particle
  `normalize((0, 0))` would make a literal port non-deterministic. Define both
  on the CPU, document the deviation, test the defined path and keep those
  parameter settings out of the parity bounds.
- **CI budget** → the job is already close to its 120-minute limit. Raise it to
  180, keep scenes at 32²/64² with at most 1000 particles, and give the new
  step a 20-minute cap.
- **Fast-math asymmetry** → desktop `-Ofast -ffast-math` and wasm `-Os` can
  produce different bytes. Parity is measured on the desktop build; wasm is
  pinned by artifact-captured goldens; both gates are tolerance-based.
- **`kw`/`center`/`bbox` tuple order** → the bbox is `(xmin, xmax, ymin, ymax)`
  in C++, which is easy to misread. Document the order in the manifest and the
  option type, and test with an asymmetric bbox.
- **New-grid lifetime** → plain inputs create temporary wasm arrays. The
  helper frees them in `finally`; shape mismatches are rejected in JS before
  the call; the returned wrapper is always fresh and independent of the inputs.
- **64-field cap** → fields beyond 64 silently return the wrapper's
  zero-accumulator result. The JS surface binds one field; the C++ cap is
  documented and covered by a CPU invariant rather than a silent assumption.
- **Upstream kernel drift** → the port mirrors today's kernels and the header
  records the source. If upstream changes a kernel, the parity suite is the
  detector; a divergence is fixed by re-transcribing, not by loosening the
  bound.

## Migration Plan

Purely additive. Rollout order:

1. The two port files and headers; add the `sources.cmake` rows in the same
   step so the wasm and desktop builds stay linkable.
2. Bindings and manifest rows, then the TS surface, typings and exports.
3. `tests/rifts-advection.test.ts`, the surface name list and regenerated
   golden scenes.
4. `tests/src/test_rifts_advection_cpu.cpp` and the third workflow step with
   the raised timeout.
5. Docs: AD-026/027/028, the parity report, porting-learnings §12, research
   status, README.

Upstream sync and rollback:

- The ports are new files; a rebase that brings an upstream CPU implementation
  of the same names fails at link with duplicate symbols (or at compile with
  duplicate default arguments). The fix is to delete the port file and header
  and drop the two curated-list rows; bindings, tests and JS names stay.
- Full rollback is a revert of the change: delete the four new files and the
  test/report, drop the three manifest rows and binding lines, revert the
  `sources.cmake`, JS/TS, workflow and docs edits. No persisted data, file
  format or runtime state is involved; golden scenes are regenerated from the
  artifact when the change returns.

## Open Questions

- Should the duplicated direct-kernel image/buffer helpers in the hydraulic
  and rifts/advection parity tests be extracted into a shared test header? The
  helpers are copied for now to keep this change from refactoring a passing
  suite; the extraction is a later cleanup that does not affect the specs or
  the approach.
