/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */

/* highmap-wasm CPU ports of the advection family.
 *
 * Each function below transcribes the matching OpenCL kernel from
 * src/gpu_opencl/kernels/ (advection_particle.cl, advection_warp.cl) and
 * reproduces the launch behaviour of the matching
 * src/transform/advection_{particle,warp}_gpu.cpp wrapper. Do not "clean up"
 * the arithmetic - the parity harness
 * (tests/src/test_rifts_advection_cpu.cpp) compares this code against the GPU
 * kernels on the desktop build.
 *
 * The device helpers the kernels depend on are re-declared file-local below
 * under their kernel names (pcg_hash, rand, the kernel-form lerp,
 * g_to_xy_pixel_centered, linear_index_g and the
 * CLK_ADDRESS_MIRRORED_REPEAT / CLK_FILTER_LINEAR sampler). hmap::lerp is
 * std::lerp, which is not the kernel's (1 - t) * a + t * b, hence the local
 * twin.
 *
 * Two kernel constructs are undefined behaviour and are defined here
 * (design D4/D5): advection_warp.cl leaves `dir` uninitialized and uses it on
 * a flat first step (pinned to (0, 0)), and advection_particle.cl normalizes
 * an inertia-blended zero vector (pinned to (0, 0), i.e. no movement). The
 * particle accumulators and the count grid are explicitly zeroed - the GPU
 * wrapper allocates them read-write but never uploads zeros - and particles
 * are accumulated in ascending id order and fields in ascending order, whereas
 * the kernel uses a non-atomic +=. The two RNG draws of the particle start
 * position are sequenced x-then-y (kernel element order) instead of being
 * passed as two arguments of one constructor call, whose evaluation order is
 * unspecified in C++ and decides which draw lands in x (see draw_start_pos).
 * All deviations are documented and kept out of the parity bounds.
 *
 * Rebase rule: if upstream ever ships CPU implementations, delete this file
 * and the header. See AD-026 in docs/decisions.md for the placement and the
 * rebase rule.
 */

#include "highmap/transform_advection_cpu.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "highmap/array.hpp"
#include "highmap/filters.hpp"
#include "highmap/gradient.hpp"
#include "highmap/internal/validation.hpp"
#include "highmap/math/array.hpp"

namespace hmap
{

namespace
{

// --- shared kernel helpers --------------------------------------------------

// _common_rand.cl: pcg_hash (unsigned 32-bit wraparound is intentional)
inline std::uint32_t pcg_hash(std::uint32_t v)
{
  v = v * 747796405u + 2891336453u;
  std::uint32_t word = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
  return (word >> 22u) ^ word;
}

// _common_rand.cl: rand, i.e. the Marsaglia xorshift then the 2^-32
// normalization
inline float rand(std::uint32_t &rng_state)
{
  rng_state ^= (rng_state << 13);
  rng_state ^= (rng_state >> 17);
  rng_state ^= (rng_state << 5);
  return (float)rng_state * (1.f / 4294967296.f);
}

// _common_math.cl: lerp, the kernel form (1 - t) * a + t * b, not std::lerp
inline float lerp(float a, float b, float t) { return (1.f - t) * a + t * b; }

// _common_index.cl: g_to_xy_pixel_centered, i.e. the center of the cell
// (gx, gy) in normalized coordinates. The kernel writes it as
// 0.5 * dr + g * dr rather than (g + 0.5) / n; the two forms are algebraically
// identical and the kernel form is kept.
inline glm::vec2 g_to_xy_pixel_centered(int gx, int gy, int nx, int ny)
{
  const float drx = 1.f / (float)nx;
  const float dry = 1.f / (float)ny;

  return glm::vec2(0.5f * drx + (float)gx * drx, 0.5f * dry + (float)gy * dry);
}

// _common_index.cl: linear_index_g
inline int linear_index_g(int gx, int gy, int nx) { return gy * nx + gx; }

// advection_particle.cl:36 and :45 draw the starting position as
//
//   pos = (float2)((nx - 1) * rand(&rng_state), (ny - 1) * rand(&rng_state));
//
// Both calls mutate the same state, so their order decides which draw lands in
// x. In OpenCL C the vector literal is emitted element by element (clang): x
// takes the FIRST draw. A C++ two-argument constructor leaves that order
// unspecified - clang evaluates it left-to-right, GCC right-to-left - so the
// two draws are sequenced explicitly here instead of being passed as two
// arguments of one constructor call. This is not cosmetic: swapping them
// transposes every starting cell and was the entire CPU-vs-GPU divergence
// measured by the first parity run (see docs/rifts-advection-parity.md,
// "Particle draw order").
inline glm::vec2 draw_start_pos(std::uint32_t &rng_state, int nx, int ny)
{
  const float rx = rand(rng_state); // x first, in kernel element order
  const float ry = rand(rng_state); // then y

  return glm::vec2((float)(nx - 1) * rx, (float)(ny - 1) * ry);
}

// --- mirrored-repeat linear sampler -----------------------------------------

// advection_warp.cl samples every image with
//   CLK_NORMALIZED_COORDS_TRUE | CLK_ADDRESS_MIRRORED_REPEAT | CLK_FILTER_LINEAR
// (lines 22-23) and reads only the .x component of each image, so a 1D
// mapping per axis is enough. OpenCL places the center of texel i at
// (i + 0.5) / N in normalized space. For a normalized coordinate u:
//   s = u * N                       (continuous texel coordinate)
//   m = fmod(s, 2N)                 (fold into the mirrored period)
//   m += 2N when negative
//   m = 2N - m when m > N           (mirror at the right edge)
//   q = m - 0.5                     (texel index space, centers at integers)
//   i0 = floor(q), f = q - i0, i1 = i0 + 1
// Clamping i0 / i1 to [0, N - 1] duplicates the edge texels, which is what
// mirrored repeat does at the border. u = 0 and u = 1 therefore land exactly
// on the first / last texel.
inline void mirror_coord(float u, int n, int &i0, int &i1, float &f)
{
  const float size = (float)n;
  const float period = 2.f * size;

  float m = std::fmod(u * size, period);
  if (m < 0.f) m += period;
  if (m > size) m = period - m;

  const float q = m - 0.5f;
  const int   q0 = (int)std::floor(q);

  f = q - (float)q0;

  i0 = std::clamp(q0, 0, n - 1);
  i1 = std::clamp(q0 + 1, 0, n - 1);
}

// bilinear sample of `a` at the normalized coordinate `pos`, one axis at a
// time: (1 - f) * texel(i0) + f * texel(i1)
inline float sample_mirrored_repeat(const Array &a, const glm::vec2 &pos)
{
  int   i0, i1, j0, j1;
  float fu, fv;

  mirror_coord(pos.x, a.shape.x, i0, i1, fu);
  mirror_coord(pos.y, a.shape.y, j0, j1, fv);

  const float v0 = (1.f - fu) * a(i0, j0) + fu * a(i1, j0);
  const float v1 = (1.f - fu) * a(i0, j1) + fu * a(i1, j1);

  return (1.f - fv) * v0 + fv * v1;
}

// advection_warp.cl:6
const int MAX_STEPS = 1024;

// --- particle solver --------------------------------------------------------

// advection_particle.cl: the per-particle walk. Particles are processed in
// ascending id order and fields in ascending order; the kernel accumulates
// with a non-atomic +=, so a multi-particle GPU run is not reproducible. That
// fixed order is the port's documented deviation (design D5). `accum` holds one
// zeroed grid per field and `count` is a zeroed grid; the kernel caps the
// number of accumulated fields at 64, so fields beyond the cap keep their zero
// accumulator.
void advection_particle_walk(const Array              &dx,
                             const Array              &dy,
                             const std::vector<Array> &fields,
                             int                       nparticles,
                             std::uint32_t             seed,
                             bool                      reverse,
                             float                     advection_length,
                             float                     value_persistence,
                             float                     inertia,
                             const Array              *p_advection_mask,
                             std::vector<Array>       &accum,
                             Array                    &count)
{
  const int nx = dx.shape.x;
  const int ny = dx.shape.y;

  const float drx = 1.f;
  const float dry = 1.f;
  const float eps = 1e-9f;
  const float dist_max = advection_length * (float)nx;
  const float advection_sign = reverse ? -1.f : 1.f;

  const int num_fields = (int)fields.size();

  // advection_particle.cl:56-57
  int num_f = num_fields;
  if (num_f > 64) num_f = 64;

  const bool has_advection_mask = p_advection_mask != nullptr;

  for (int id = 0; id < nparticles; id++)
  {
    std::uint32_t rng_state = pcg_hash(seed + (std::uint32_t)id * 2u);

    float dist = 0.f;

    // advection_particle.cl:33-50
    glm::vec2 pos;
    if (!has_advection_mask)
    {
      pos = draw_start_pos(rng_state, nx, ny);
    }
    else
    {
      // find a source point outside the advection zone. NB the loop breaks when
      // the mask is > 0 (the kernel comment says the opposite), and the last
      // draw is kept when no draw passes; both quirks are kept.
      int tries_count = 0;
      while (tries_count < 100)
      {
        tries_count++;
        pos = draw_start_pos(rng_state, nx, ny);

        const glm::ivec2 g_try((int)pos.x, (int)pos.y);
        if ((*p_advection_mask).vector[linear_index_g(g_try.x, g_try.y, nx)] >
            0.f)
          break;
      }
    }

    glm::ivec2 g((int)pos.x, (int)pos.y);
    int        idx = linear_index_g(g.x, g.y, nx);

    float val[64];
    for (int f = 0; f < num_f; ++f)
      val[f] = fields[f].vector[idx];

    glm::vec2 dir(0.f, 0.f);
    glm::vec2 dir_prev(0.f, 0.f);

    while (dist < dist_max)
    {
      // particle direction
      const glm::vec2 vel(dx.vector[idx], dy.vector[idx]);

      const float len = std::sqrt(vel.x * vel.x + vel.y * vel.y);
      if (len > eps)
      {
        // (NB - if too flat, keep the direction of the previous step)
        glm::vec2 dir_vel = vel / len;

        const glm::vec2 blended = inertia * dir_prev +
                                  (1.f - inertia) * dir_vel;

        // the kernel calls normalize() on the blended vector, which is
        // undefined for (0, 0) (inertia == 1 with dir_prev == (0, 0)); the
        // port keeps a zero direction, so the particle does not move and the
        // position-skip branch below behaves as the kernel's continue
        const float blended_len = std::sqrt(blended.x * blended.x +
                                            blended.y * blended.y);
        if (blended_len < eps)
          dir = glm::vec2(0.f, 0.f);
        else
          dir = blended / blended_len;

        dir_prev = dir;
      }

      dist += 1.f;

      // move
      const glm::vec2 pos_prev = pos;

      pos.x += advection_sign * dir.x * drx;
      pos.y += advection_sign * dir.y * dry;

      if (std::fabs(pos.x - pos_prev.x) < eps &&
          std::fabs(pos.y - pos_prev.y) < eps)
        continue;

      g = glm::ivec2((int)pos.x, (int)pos.y);
      idx = linear_index_g(g.x, g.y, nx);

      // stop if the particle reaches the domain limits
      if (g.x < 0 || g.x > nx - 1 || g.y < 0 || g.y > ny - 1) break;

      // stop if outside the advection mask
      if (has_advection_mask && (*p_advection_mask).vector[idx] == 0.f) break;

      // advection_particle.cl:104-112 is an `if (true) { ... } else { ... }`
      // pair; only the accumulating branch is reachable, so it is the only
      // branch here. The unreachable else (a 3x3 scattered deposit, weighted
      // by sqrt((ir - |p|) * (ir - |q|))) is omitted with this comment.
      for (int f = 0; f < num_f; ++f)
      {
        val[f] = lerp(fields[f].vector[idx], val[f], value_persistence);
        accum[f].vector[idx] += val[f];
      }
      count.vector[idx] += 1.f;
    }
  }
}

} // namespace

Array advection_particle(const Array  &z,
                         const Array  &advected_field,
                         int           iterations,
                         int           nparticles,
                         std::uint32_t seed,
                         bool          reverse,
                         bool          post_filter,
                         float         post_filter_sigma,
                         float         advection_length,
                         float         value_persistence,
                         float         inertia,
                         const Array  *p_advection_mask,
                         const Array  *p_mask)
{
  if (!validate_non_empty(z)) return Array();

  auto res = advection_particle(z,
                                std::vector<Array>{advected_field},
                                iterations,
                                nparticles,
                                seed,
                                reverse,
                                post_filter,
                                post_filter_sigma,
                                advection_length,
                                value_persistence,
                                inertia,
                                p_advection_mask,
                                p_mask);
  if (res.empty()) return Array();
  return res[0];
}

std::vector<Array> advection_particle(const Array              &z,
                                      const std::vector<Array> &advected_fields,
                                      int                       iterations,
                                      int                       nparticles,
                                      std::uint32_t             seed,
                                      bool                      reverse,
                                      bool                      post_filter,
                                      float        post_filter_sigma,
                                      float        advection_length,
                                      float        value_persistence,
                                      float        inertia,
                                      const Array *p_advection_mask,
                                      const Array *p_mask)
{
  if (!validate_non_empty(z)) return {};

  // same as src/transform/advection_particle_gpu.cpp: every pass runs the
  // single-pass solver with the same seed, and the previous output is the next
  // pass's advected field. iterations == 0 therefore returns a copy of the
  // inputs.
  std::vector<Array> out = advected_fields;

  for (int it = 0; it < iterations; ++it)
    out = advection_particle(z,
                             out,
                             nparticles,
                             seed,
                             reverse,
                             post_filter,
                             post_filter_sigma,
                             advection_length,
                             value_persistence,
                             inertia,
                             p_advection_mask,
                             p_mask);

  return out;
}

Array advection_particle(const Array  &z,
                         const Array  &advected_field,
                         int           nparticles,
                         std::uint32_t seed,
                         bool          reverse,
                         bool          post_filter,
                         float         post_filter_sigma,
                         float         advection_length,
                         float         value_persistence,
                         float         inertia,
                         const Array  *p_advection_mask,
                         const Array  *p_mask)
{
  if (!validate_non_empty(z)) return Array();

  auto res = advection_particle(z,
                                std::vector<Array>{advected_field},
                                nparticles,
                                seed,
                                reverse,
                                post_filter,
                                post_filter_sigma,
                                advection_length,
                                value_persistence,
                                inertia,
                                p_advection_mask,
                                p_mask);
  if (res.empty()) return Array();
  return res[0];
}

std::vector<Array> advection_particle(const Array              &z,
                                      const std::vector<Array> &advected_fields,
                                      int                       nparticles,
                                      std::uint32_t             seed,
                                      bool                      reverse,
                                      bool                      post_filter,
                                      float        post_filter_sigma,
                                      float        advection_length,
                                      float        value_persistence,
                                      float        inertia,
                                      const Array *p_advection_mask,
                                      const Array *p_mask)
{
  if (!validate_non_empty(z)) return {};

  // downhill: same as src/transform/advection_particle_gpu.cpp:136-137
  Array dx = -hmap::gradient_x(z);
  Array dy = -hmap::gradient_y(z);

  return advection_particle(dx,
                            dy,
                            advected_fields,
                            nparticles,
                            seed,
                            reverse,
                            post_filter,
                            post_filter_sigma,
                            advection_length,
                            value_persistence,
                            inertia,
                            p_advection_mask,
                            p_mask);
}

Array advection_particle(const Array  &dx,
                         const Array  &dy,
                         const Array  &advected_field,
                         int           nparticles,
                         std::uint32_t seed,
                         bool          reverse,
                         bool          post_filter,
                         float         post_filter_sigma,
                         float         advection_length,
                         float         value_persistence,
                         float         inertia,
                         const Array  *p_advection_mask,
                         const Array  *p_mask)
{
  if (!validate_non_empty(dx)) return Array();

  auto res = advection_particle(dx,
                                dy,
                                std::vector<Array>{advected_field},
                                nparticles,
                                seed,
                                reverse,
                                post_filter,
                                post_filter_sigma,
                                advection_length,
                                value_persistence,
                                inertia,
                                p_advection_mask,
                                p_mask);
  if (res.empty()) return Array();
  return res[0];
}

std::vector<Array> advection_particle(const Array              &dx,
                                      const Array              &dy,
                                      const std::vector<Array> &advected_fields,
                                      int                       nparticles,
                                      std::uint32_t             seed,
                                      bool                      reverse,
                                      bool                      post_filter,
                                      float        post_filter_sigma,
                                      float        advection_length,
                                      float        value_persistence,
                                      float        inertia,
                                      const Array *p_advection_mask,
                                      const Array *p_mask)
{
  // same validation contract as
  // src/transform/advection_particle_gpu.cpp:201-209
  if (!validate_non_empty(dx) || !validate_same_shape(dx, dy)) return {};
  if (!validate_non_empty(advected_fields, "advected_fields")) return {};

  for (const auto &field : advected_fields)
    if (!validate_same_shape(dx, field)) return {};

  if (p_advection_mask && !validate_same_shape(dx, *p_advection_mask))
    return {};
  if (p_mask && !validate_same_shape(dx, *p_mask)) return {};

  const glm::ivec2 shape = dx.shape;
  const int        num_fields = (int)advected_fields.size();

  // explicit zeroing: the GPU wrapper allocates out / count as read-write
  // buffers but never uploads zeroed contents (design D5). Array(shape)
  // values-initializes the buffer to 0.
  std::vector<Array> accum;
  accum.reserve(num_fields);
  for (int f = 0; f < num_fields; ++f) accum.emplace_back(shape);

  Array count(shape);

  advection_particle_walk(dx,
                          dy,
                          advected_fields,
                          nparticles,
                          seed,
                          reverse,
                          advection_length,
                          value_persistence,
                          inertia,
                          p_advection_mask,
                          accum,
                          count);

  // deconcatenate and post-process; same host stage as
  // src/transform/advection_particle_gpu.cpp:257-293. The wrapper's loop bounds
  // read j < shape.x / i < shape.y (swapped), which is out of bounds for
  // non-square grids; the port sweeps the whole grid, which is identical for
  // square grids and the only defined behaviour otherwise.
  std::vector<Array> out_fields;
  out_fields.reserve(num_fields);

  for (int f = 0; f < num_fields; ++f)
  {
    Array       out(shape);
    const Array &advected_field = advected_fields[f];

    for (int j = 0; j < shape.y; ++j)
      for (int i = 0; i < shape.x; ++i)
      {
        if (count(i, j))
        {
          out(i, j) = accum[f](i, j) + advected_field(i, j);
          out(i, j) /= (count(i, j) + 1.f);
          out(i, j) = std::clamp(out(i, j), 0.f, 1.f);
        }
        else
        {
          out(i, j) = advected_field(i, j);
        }
      }

    // post-processing
    if (post_filter)
    {
      int max_it = 1;
      hmap::laplace(out, p_advection_mask, post_filter_sigma, max_it);
    }

    if (p_mask)
      out_fields.push_back(hmap::lerp(out, advected_field, *p_mask));
    else
      out_fields.push_back(out);
  }

  return out_fields;
}

Array advection_warp(const Array &z,
                     const Array &advected_field,
                     float        advection_length,
                     float        value_persistence,
                     const Array *p_mask)
{
  if (!validate_non_empty(z) || !validate_same_shape(z, advected_field))
    return Array();
  if (p_mask && !validate_same_shape(z, *p_mask)) return Array();

  // uphill: same as src/transform/advection_warp_gpu.cpp:68-69 (no negation)
  Array dx = -hmap::gradient_x(z);
  Array dy = hmap::gradient_y(z);

  return advection_warp(z,
                        advected_field,
                        dx,
                        dy,
                        advection_length,
                        value_persistence,
                        p_mask);
}

Array advection_warp(const Array &z,
                     const Array &advected_field,
                     const Array &dx,
                     const Array &dy,
                     float        advection_length,
                     float        value_persistence,
                     const Array *p_mask)
{
  // same validation contract as
  // src/transform/advection_warp_gpu.cpp:23-26
  if (!validate_non_empty(z) || !validate_same_shape(z, advected_field) ||
      !validate_same_shape(z, dx) || !validate_same_shape(z, dy))
    return Array();
  if (p_mask && !validate_same_shape(z, *p_mask)) return Array();

  const int nx = z.shape.x;
  const int ny = z.shape.y;

  Array out(z.shape);

  const float drx = 1.f / (float)nx;
  const float dry = 1.f / (float)ny;
  const float eps = 0.f; // advection_warp.cl:27

  // advection_warp.cl:33-34
  const int max_steps = (int)std::ceil(advection_length / std::fmin(drx, dry));
  const int limit = std::min(max_steps, MAX_STEPS);

  // path buffer reused across cells; bounded by MAX_STEPS (advection_warp.cl:37)
  std::vector<glm::vec2> path;
  if (limit > 0) path.reserve((std::size_t)limit);

  // the wrapper materializes a constant-one image when p_mask is null
  // (advection_warp_gpu.cpp:39-47); sampling a constant-one image returns 1.f,
  // so the value is used directly instead of allocating a full grid
  const bool has_mask = p_mask != nullptr;

  for (int gy = 0; gy < ny; gy++)
    for (int gx = 0; gx < nx; gx++)
    {
      const glm::vec2 pos0 = g_to_xy_pixel_centered(gx, gy, nx, ny);

      glm::vec2 pos = pos0;

      float z_prev = sample_mirrored_repeat(z, pos0);

      // advection_warp.cl:44 declares `dir` uninitialized and uses it on the
      // first step when the start is flat (undefined behaviour). The port
      // initializes it to (0, 0): a flat start does not move until a non-zero
      // velocity appears. Documented deviation (design D4); flat scenes are
      // excluded from the warp parity bounds.
      glm::vec2 dir(0.f, 0.f);

      path.clear();

      // STEP 1 : upward pass - record all visited positions
      // (advection_warp.cl:46-69)
      for (int i = 0; i < limit; ++i)
      {
        path.push_back(pos); // store current position

        const glm::vec2 vel(sample_mirrored_repeat(dx, pos),
                            sample_mirrored_repeat(dy, pos));

        const float len = std::hypot(vel.x, vel.y);
        if (len > eps)
        {
          // (NB - if too flat, keep the direction of the previous step)
          dir = vel / len;
        }

        pos.x += dir.x * drx;
        pos.y += dir.y * dry;

        if (pos.x < 0.f || pos.x > 1.f || pos.y < 0.f || pos.y > 1.f) break;

        const float z_current = sample_mirrored_repeat(z, pos);
        if (z_current - z_prev < -1e-5f) break;

        z_prev = z_current;
      }

      // STEP 2 : downward pass - walk backward through the stored path
      // (advection_warp.cl:75-87). The last position is read twice: once to
      // seed `val` and once more as the first loop iteration.
      float val = 0.f;
      if (!path.empty())
        val = sample_mirrored_repeat(advected_field, path.back());

      for (int i = (int)path.size() - 1; i >= 0; --i)
      {
        pos = path[(std::size_t)i];

        const float new_val = sample_mirrored_repeat(advected_field, pos);
        const float mask_v = has_mask ? sample_mirrored_repeat(*p_mask, pos)
                                      : 1.f;
        const float ratio = mask_v * value_persistence;

        val = ratio * val + (1.f - ratio) * new_val;
      }

      out(gx, gy) = val;
    }

  return out;
}

} // namespace hmap
