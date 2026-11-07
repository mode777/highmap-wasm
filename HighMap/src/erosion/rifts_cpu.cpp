/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */

/* highmap-wasm CPU port of the rifts erosion.
 *
 * This function transcribes the OpenCL kernel src/gpu_opencl/kernels/rifts.cl
 * and reproduces the launch behaviour of src/erosion/rifts_gpu.cpp. Do not
 * "clean up" the arithmetic - the parity harness
 * (tests/src/test_rifts_advection_cpu.cpp) compares this code against the GPU
 * kernel on the desktop build.
 *
 * The device helpers rifts.cl depends on (wang_hash, rand, hash22f, fract,
 * smin, smax, min_smooth, max_smooth, pow_int, remap_from, g_to_xy,
 * base_voronoi_f1df2 and the kernel-form lerp) are re-declared file-local
 * below under their kernel names. No upstream CPU helper provides them, and
 * hmap::lerp is std::lerp, which is not the kernel's (1 - t) * a + t * b.
 *
 * The kernel quirks are preserved on purpose: the scalar-broadcast of
 * center.x / center.y in the rotation, the literal sigmoid sign operator, the
 * smax/remap_from argument order and the reverse_mask flip that is applied
 * even when apply_mask is false.
 *
 * Rebase rule: if upstream ever ships a CPU implementation, delete this file
 * and the header. See AD-026 in docs/decisions.md for the placement and the
 * rebase rule.
 */

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "highmap/erosion_rifts_cpu.hpp"

#include "highmap/array.hpp"
#include "highmap/internal/validation.hpp"

namespace hmap
{

namespace
{

// --- shared kernel helpers --------------------------------------------------

// _common_rand.cl: wang_hash (unsigned 32-bit wraparound is intentional)
inline std::uint32_t wang_hash(std::uint32_t seed)
{
  seed = (seed ^ 61u) ^ (seed >> 16);
  seed *= 9u;
  seed = seed ^ (seed >> 4);
  seed *= 0x27d4eb2du;
  seed = seed ^ (seed >> 15);
  return seed;
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

// _common_rand.cl: fract(x, &pi) returns x - floor(x)
inline float fract(float x) { return x - std::floor(x); }

// _common_rand.cl: hash22f. The two dot products are stored in p before the
// sine hash is applied, exactly as the kernel does.
inline glm::vec2 hash22f(glm::vec2 p, float fseed)
{
  p = glm::vec2(p.x * 127.1f + p.y * 311.7f,
                p.x * 269.5f + p.y * 183.3f);

  return glm::vec2(fract(std::sin(p.x + fseed) * 43758.5f),
                   fract(std::sin(p.y + fseed) * 43758.5453123f));
}

// _common_math.cl: pow_int, a loop rather than std::pow
inline float pow_int(float base, int exp)
{
  float result = 1.f;
  for (int i = 0; i < std::abs(exp); ++i)
    result *= base;
  return exp < 0 ? 1.f / result : result;
}

// _common_math.cl: max_smooth
inline float max_smooth(float a, float b, float k)
{
  const float h = std::fmax(k - std::fabs(a - b), 0.f) / k;
  return std::fmax(a, b) + pow_int(h, 3) * k / 6.f;
}

// _common_math.cl: min_smooth
inline float min_smooth(float a, float b, float k)
{
  const float h = std::fmax(k - std::fabs(a - b), 0.f) / k;
  return std::fmin(a, b) - pow_int(h, 3) * k / 6.f;
}

// _common_math.cl: smax, dispatching on k > 0
inline float smax(float a, float b, float k)
{
  if (k > 0.f) return max_smooth(a, b, k);
  return std::fmax(a, b);
}

// _common_math.cl: smin, dispatching on k > 0
inline float smin(float a, float b, float k)
{
  if (k > 0.f) return min_smooth(a, b, k);
  return std::fmin(a, b);
}

// _common_math.cl: remap_from
inline float remap_from(float x,
                        float vmin,
                        float vmax,
                        float from_min,
                        float from_max)
{
  return (x - from_min) / (from_max - from_min) * (vmax - vmin) + vmin;
}

// _common_math.cl: lerp, the kernel form (1 - t) * a + t * b, not std::lerp
inline float lerp(float a, float b, float t) { return (1.f - t) * a + t * b; }

// _common_index.cl: g_to_xy. The cell position divides by nx / ny (not
// nx - 1) and has no half-pixel shift; the per-cell noise offsets are added
// after the bbox scaling. The bbox order is (xmin, xmax, ymin, ymax).
inline glm::vec2 g_to_xy(int          gx,
                         int          gy,
                         int          nx,
                         int          ny,
                         float        kx,
                         float        ky,
                         float        dx,
                         float        dy,
                         const glm::vec4 &bbox)
{
  float x = (float)gx / (float)nx;
  float y = (float)gy / (float)ny;

  x = kx * (x * (bbox.y - bbox.x) + bbox.x) + kx * dx;
  y = ky * (y * (bbox.w - bbox.z) + bbox.z) + ky * dy;

  return glm::vec2(x, y);
}

// voronoi_base.cl: base_voronoi_f1df2. Returns min1 / min2, not a distance
// difference.
inline float base_voronoi_f1df2(const glm::vec2 &p,
                                const glm::vec2 &jitter,
                                float            k_smoothing,
                                float            fseed)
{
  const glm::vec2 i(std::floor(p.x), std::floor(p.y));
  const glm::vec2 f(fract(p.x), fract(p.y));

  float min1 = FLT_MAX;
  float min2 = FLT_MAX;

  for (int dx = -1; dx <= 1; dx++)
    for (int dy = -1; dy <= 1; dy++)
    {
      const glm::vec2 dr((float)dx, (float)dy);
      const glm::vec2 h = hash22f(i + dr, fseed);
      const glm::vec2 feature_point(dr.x + jitter.x * h.x,
                                    dr.y + jitter.y * h.y);
      const glm::vec2 diff(f.x - feature_point.x, f.y - feature_point.y);
      const float     dist = diff.x * diff.x + diff.y * diff.y;

      // the kernel's two-line update order
      const float new_min1 = smin(min1, dist, k_smoothing);
      const float new_min2 = smin(min2, smax(min1, dist, k_smoothing),
                                  k_smoothing);
      min1 = new_min1;
      min2 = new_min2;
    }

  return min1 / min2;
}

} // namespace

void rifts(Array           &z,
           const glm::vec2 &kw,
           float            angle,
           float            amplitude,
           std::uint32_t    seed,
           float            elevation_noise_shift,
           float            k_smooth_bottom,
           float            k_smooth_top,
           float            radial_spread_amp,
           float            elevation_noise_amp,
           float            clamp_vmin,
           float            remap_vmin,
           bool             apply_mask,
           bool             reverse_mask,
           float            mask_gamma,
           const Array     *p_noise_x,
           const Array     *p_noise_y,
           const Array     *p_mask,
           const glm::vec2 &center,
           const glm::vec4 &bbox)
{
  // same contract as src/erosion/rifts_gpu.cpp: nothing is written when the
  // terrain is empty or an optional input has a different shape
  if (!validate_non_empty(z)) return;
  if (p_noise_x && !validate_same_shape(z, *p_noise_x)) return;
  if (p_noise_y && !validate_same_shape(z, *p_noise_y)) return;
  if (p_mask && !validate_same_shape(z, *p_mask)) return;

  const int nx = z.shape.x;
  const int ny = z.shape.y;

  // the kernel derives fseed per work-item from the same seed, so it is a
  // global constant; the per-cell variation comes from hash22f(i + dr, fseed)
  std::uint32_t rng_state = wang_hash(seed);
  const float   fseed = rand(rng_state);

  const float alpha = angle / 180.f * 3.141592f;
  const float ca = std::cos(alpha);
  const float sa = std::sin(alpha);

  for (int j = 0; j < ny; j++)
    for (int i = 0; i < nx; i++)
    {
      const int index = j * nx + i;

      const float dx = p_noise_x ? (*p_noise_x)(i, j) : 0.f;
      const float dy = p_noise_y ? (*p_noise_y)(i, j) : 0.f;

      const glm::vec2 pos = g_to_xy(i, j, nx, ny, 1.f, 1.f, dx, dy, bbox);

      // parameters
      const float val0 = z.vector[index];
      float       val = val0;
      // the kernel's `dir` local, (cos(alpha), sin(alpha)), is unused; dropped.

      // rotate. The kernel broadcasts the scalar center.x / center.y to both
      // components (dot(pos - center.x, dir) and dot(pos - center.y, perp));
      // this is not `pos - center` and is kept verbatim.
      const float dr_x = (pos.x - center.x) * ca + (pos.y - center.x) * sa;
      const float dr_y = (pos.x - center.y) * (-sa) + (pos.y - center.y) * ca;

      float dist_to_center = std::sqrt(dr_x * dr_x + dr_y * dr_y);
      // use a sigmoid as a smooth 'sign' operator (literal expression, not
      // -tanh(10 * dr_x))
      dist_to_center *= -2.f / (1.f + std::exp(-20.f * dr_x)) + 1.f;

      const glm::vec2 pos_r(
          kw.x * (dr_x + radial_spread_amp * dist_to_center +
                  elevation_noise_amp * (val0 - elevation_noise_shift)),
          kw.y * dr_y);

      float noise_v = base_voronoi_f1df2(pos_r,
                                         glm::vec2(1.f, 1.f),
                                         k_smooth_bottom,
                                         fseed);
      noise_v = smax(std::fmin(noise_v, 1.f), clamp_vmin, k_smooth_top);
      noise_v = remap_from(noise_v, 1.f, remap_vmin, clamp_vmin, 1.f);

      val += amplitude * noise_v;

      // elevation mask. The kernel calls pow; a negative val0 with a
      // non-integral mask_gamma is implementation-defined in OpenCL. The
      // parity scenes use non-negative elevation (see the parity report).
      {
        float t = apply_mask ? std::pow(val0, mask_gamma) : 1.f;
        if (reverse_mask) t = 1.f - t;
        val = lerp(val0, val, t);
      }

      // input mask
      if (p_mask)
      {
        const float t = (*p_mask)(i, j);
        val = lerp(val0, val, t);
      }

      // out
      z.vector[index] = val;
    }
}

} // namespace hmap
