/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */

/* CPU invariants and GPU-versus-CPU parity harness for the rifts and
 * advection CPU ports (add-rifts-advection-cpu-ports).
 *
 * The CPU family transcribes the OpenCL kernels (src/gpu_opencl/kernels/
 * rifts.cl, advection_particle.cl, advection_warp.cl); this file first pins
 * the CPU-only invariants and later (task 5) runs both implementations on
 * fixed scenes and reports per-variant error metrics.
 *
 * Suites:
 *   RiftsAdvectionCpuInvariants - CPU-only properties (determinism, no-ops,
 *                                 mask endpoints and the reverse_mask quirk,
 *                                 noise offsets, center broadcast, bbox order
 *                                 and shape validation, plus the advection
 *                                 invariants: warp's zero-length / step-cap /
 *                                 sampler / mask cases, the particle
 *                                 normalization formula, the start-position
 *                                 draw order, the inertia guard, the
 *                                 `iterations` chain, the 64-field cap and
 *                                 the input-immutability checks). Run
 *                                 everywhere, no OpenCL device needed.
 *   RiftsAdvectionParity        - GPU-vs-CPU comparison (added in task 5).
 *                                 Skips with GTEST_SKIP() when no OpenCL
 *                                 device or no usable float image format is
 *                                 available; the erosion-parity CI job treats
 *                                 an unexpected skip as a failure.
 *
 * Parity cases print metric lines to stdout (design D7):
 *   PARITY,<variant>,<scene>,<iters>,<max_abs>,<rms_abs>,<range>
 *   PARITY_PCT,<variant>,<scene>,<iters>,<p99_abs>,<frac_over>,<threshold>,<range>
 *   PARITY_AGG,<variant>,<scene>,<iters>,<rel_total_pct>,<mean_abs>,<range>
 * PARITY and PARITY_PCT report absolute per-cell differences; the matching
 * gate is written as a fraction of the <range> column (the range of the input
 * terrain or advected field), so the same line can be judged at any bound.
 * p99_abs is the 99th percentile (nearest rank) of the per-cell absolute
 * error and frac_over is the fraction of cells whose absolute error exceeds
 * threshold = 1e-2 * range. PARITY_AGG is printed by multi-particle
 * advection: rel_total_pct is the relative total-mass difference in percent
 * and mean_abs is the mean per-cell absolute difference (absolute units, gated
 * against 1e-2 * range). The paired PARITY line carries the per-cell
 * max_abs/rms_abs as diagnostics only, because the particle kernel accumulates
 * with a non-atomic += and a many-particle GPU result is not guaranteed to be
 * reproducible (design D7, AD-028; the first oracle run measured a
 * bit-identical GPU repeat on PoCl, see `advection_particle_gpu_repeat`). A
 * PARITY_AGG line with the
 * `advection_particle_gpu_repeat` variant records GPU-vs-GPU variability and
 * is deliberately ungated.
 */

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "cl_wrapper/kernel_manager.hpp"
#include "cl_wrapper/run.hpp"

#include "highmap.hpp"
#include "highmap/erosion_rifts_cpu.hpp"
#include "highmap/gradient.hpp"
#include "highmap/math/array.hpp"
#include "highmap/transform_advection_cpu.hpp"

using namespace hmap;

namespace
{

// --- scenes ---------------------------------------------------------------

Array scene_fbm(int n = 32, std::uint32_t seed = 11)
{
  Array z = noise_fbm(NoiseType::PERLIN, {n, n}, {4.f, 4.f}, seed);
  remap(z);
  return z;
}

bool all_finite(const Array &a)
{
  for (float v : a.vector)
    if (!std::isfinite(v)) return false;
  return true;
}

void expect_identical(const Array &a, const Array &b)
{
  ASSERT_EQ(a.shape, b.shape);
  ASSERT_EQ(a.vector.size(), b.vector.size());
  EXPECT_EQ(std::memcmp(a.vector.data(),
                        b.vector.data(),
                        a.vector.size() * sizeof(float)),
            0);
}

float max_abs_diff(const Array &a, const Array &b)
{
  float m = 0.f;
  for (std::size_t k = 0; k < a.vector.size(); k++)
    m = std::max(m, std::fabs(a.vector[k] - b.vector[k]));
  return m;
}

std::size_t cells_over(const Array &a, const Array &b, float threshold)
{
  std::size_t n = 0;
  for (std::size_t k = 0; k < a.vector.size(); k++)
    if (std::fabs(a.vector[k] - b.vector[k]) > threshold) n++;
  return n;
}

// --- advection overload selection ------------------------------------------
// The z-driven `advection_particle` forms with and without `iterations` share
// the (Array, Array, int, int, ...) prefix, so a call whose arguments fit both
// shifted signatures is ambiguous - the upstream hmap::gpu family has exactly
// the same shape. These thin wrappers select the overload through a typed
// function pointer so the cases below stay readable.

using ParticleSingleFn = Array (*)(const Array &,
                                   const Array &,
                                   int,
                                   std::uint32_t,
                                   bool,
                                   bool,
                                   float,
                                   float,
                                   float,
                                   float,
                                   const Array *,
                                   const Array *);

using ParticleIterationsFn = Array (*)(const Array &,
                                       const Array &,
                                       int,
                                       int,
                                       std::uint32_t,
                                       bool,
                                       bool,
                                       float,
                                       float,
                                       float,
                                       float,
                                       const Array *,
                                       const Array *);

using ParticleVectorFn = std::vector<Array> (*)(const Array &,
                                                const std::vector<Array> &,
                                                int,
                                                std::uint32_t,
                                                bool,
                                                bool,
                                                float,
                                                float,
                                                float,
                                                float,
                                                const Array *,
                                                const Array *);

using ParticleVectorIterationsFn =
    std::vector<Array> (*)(const Array &,
                           const std::vector<Array> &,
                           int,
                           int,
                           std::uint32_t,
                           bool,
                           bool,
                           float,
                           float,
                           float,
                           float,
                           const Array *,
                           const Array *);

// z-driven single-field form, no iterations
Array particle(const Array  &z,
               const Array  &advected_field,
               int           nparticles,
               std::uint32_t seed,
               bool          reverse = false,
               bool          post_filter = true,
               float         post_filter_sigma = 0.125f,
               float         advection_length = 0.1f,
               float         value_persistence = 0.99f,
               float         inertia = 0.f,
               const Array  *p_advection_mask = nullptr,
               const Array  *p_mask = nullptr)
{
  return static_cast<ParticleSingleFn>(&advection_particle)(
      z,
      advected_field,
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

// z-driven single-field form, with iterations
Array particle_iterations(const Array  &z,
                          const Array  &advected_field,
                          int           iterations,
                          int           nparticles,
                          std::uint32_t seed,
                          bool          reverse = false,
                          bool          post_filter = true,
                          float         post_filter_sigma = 0.125f,
                          float         advection_length = 0.1f,
                          float         value_persistence = 0.99f,
                          float         inertia = 0.f,
                          const Array  *p_advection_mask = nullptr,
                          const Array  *p_mask = nullptr)
{
  return static_cast<ParticleIterationsFn>(&advection_particle)(
      z,
      advected_field,
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
}

// z-driven multi-field form, no iterations
std::vector<Array> particles(const Array              &z,
                             const std::vector<Array> &advected_fields,
                             int                       nparticles,
                             std::uint32_t             seed,
                             bool                      reverse = false,
                             bool                      post_filter = true,
                             float post_filter_sigma = 0.125f,
                             float advection_length = 0.1f,
                             float value_persistence = 0.99f,
                             float inertia = 0.f,
                             const Array *p_advection_mask = nullptr,
                             const Array *p_mask = nullptr)
{
  return static_cast<ParticleVectorFn>(&advection_particle)(
      z,
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

// z-driven multi-field form, with iterations
std::vector<Array> particles_iterations(
    const Array              &z,
    const std::vector<Array> &advected_fields,
    int                       iterations,
    int                       nparticles,
    std::uint32_t             seed,
    bool                      reverse = false,
    bool                      post_filter = true,
    float                     post_filter_sigma = 0.125f,
    float                     advection_length = 0.1f,
    float                     value_persistence = 0.99f,
    float                     inertia = 0.f,
    const Array *p_advection_mask = nullptr,
    const Array *p_mask = nullptr)
{
  return static_cast<ParticleVectorIterationsFn>(&advection_particle)(
      z,
      advected_fields,
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
}

// --- test-local component-wise reference (CenterBroadcast) -----------------
// A transcription of the same per-cell math as src/erosion/rifts_cpu.cpp for
// the fixed parameter set used by the CenterBroadcast case. The bool
// `broadcast_center` selects the kernel's scalar-broadcast center (true) or a
// component-wise `pos - center` (false); that is the only difference between
// the two variants.

float ref_fract(float x) { return x - std::floor(x); }

float ref_pow_int(float base, int exp)
{
  float result = 1.f;
  for (int i = 0; i < std::abs(exp); ++i)
    result *= base;
  return exp < 0 ? 1.f / result : result;
}

float ref_min_smooth(float a, float b, float k)
{
  const float h = std::fmax(k - std::fabs(a - b), 0.f) / k;
  return std::fmin(a, b) - ref_pow_int(h, 3) * k / 6.f;
}

float ref_max_smooth(float a, float b, float k)
{
  const float h = std::fmax(k - std::fabs(a - b), 0.f) / k;
  return std::fmax(a, b) + ref_pow_int(h, 3) * k / 6.f;
}

float ref_smin(float a, float b, float k)
{
  if (k > 0.f) return ref_min_smooth(a, b, k);
  return std::fmin(a, b);
}

float ref_smax(float a, float b, float k)
{
  if (k > 0.f) return ref_max_smooth(a, b, k);
  return std::fmax(a, b);
}

float ref_remap_from(float x,
                     float vmin,
                     float vmax,
                     float from_min,
                     float from_max)
{
  return (x - from_min) / (from_max - from_min) * (vmax - vmin) + vmin;
}

float ref_lerp(float a, float b, float t) { return (1.f - t) * a + t * b; }

std::uint32_t ref_wang_hash(std::uint32_t seed)
{
  seed = (seed ^ 61u) ^ (seed >> 16);
  seed *= 9u;
  seed = seed ^ (seed >> 4);
  seed *= 0x27d4eb2du;
  seed = seed ^ (seed >> 15);
  return seed;
}

float ref_rand(std::uint32_t &rng_state)
{
  rng_state ^= (rng_state << 13);
  rng_state ^= (rng_state >> 17);
  rng_state ^= (rng_state << 5);
  return (float)rng_state * (1.f / 4294967296.f);
}

std::uint32_t ref_pcg_hash(std::uint32_t v)
{
  v = v * 747796405u + 2891336453u;
  const std::uint32_t word = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
  return (word >> 22u) ^ word;
}

glm::vec2 ref_hash22f(glm::vec2 p, float fseed)
{
  p = glm::vec2(p.x * 127.1f + p.y * 311.7f,
                p.x * 269.5f + p.y * 183.3f);
  return glm::vec2(ref_fract(std::sin(p.x + fseed) * 43758.5453123f),
                   ref_fract(std::sin(p.y + fseed) * 43758.5453123f));
}

float ref_base_voronoi_f1df2(const glm::vec2 &p,
                             const glm::vec2 &jitter,
                             float            k_smoothing,
                             float            fseed)
{
  const glm::vec2 i(std::floor(p.x), std::floor(p.y));
  const glm::vec2 f(ref_fract(p.x), ref_fract(p.y));

  float min1 = FLT_MAX;
  float min2 = FLT_MAX;

  for (int dx = -1; dx <= 1; dx++)
    for (int dy = -1; dy <= 1; dy++)
    {
      const glm::vec2 dr((float)dx, (float)dy);
      const glm::vec2 h = ref_hash22f(i + dr, fseed);
      const glm::vec2 feature_point(dr.x + jitter.x * h.x,
                                    dr.y + jitter.y * h.y);
      const glm::vec2 diff(f.x - feature_point.x, f.y - feature_point.y);
      const float     dist = diff.x * diff.x + diff.y * diff.y;

      const float new_min1 = ref_smin(min1, dist, k_smoothing);
      const float new_min2 = ref_smin(min2,
                                      ref_smax(min1, dist, k_smoothing),
                                      k_smoothing);
      min1 = new_min1;
      min2 = new_min2;
    }

  return min1 / min2;
}

Array reference_rifts(const Array     &z0,
                      const glm::vec2 &kw,
                      float            angle,
                      float            amplitude,
                      std::uint32_t    seed,
                      const glm::vec2 &center,
                      bool             broadcast_center)
{
  const int nx = z0.shape.x;
  const int ny = z0.shape.y;

  std::uint32_t rng_state = ref_wang_hash(seed);
  const float   fseed = ref_rand(rng_state);

  const float alpha = angle / 180.f * 3.141592f;
  const float ca = std::cos(alpha);
  const float sa = std::sin(alpha);

  Array z = z0;

  for (int j = 0; j < ny; j++)
    for (int i = 0; i < nx; i++)
    {
      // g_to_xy with kx = ky = 1, no noise offsets and bbox {0, 1, 0, 1}
      const float pos_x = (float)i / (float)nx;
      const float pos_y = (float)j / (float)ny;

      const float val0 = z0(i, j);
      float       val = val0;

      float dr_x, dr_y;
      if (broadcast_center)
      {
        // the kernel broadcasts center.x / center.y to both components
        dr_x = (pos_x - center.x) * ca + (pos_y - center.x) * sa;
        dr_y = (pos_x - center.y) * (-sa) + (pos_y - center.y) * ca;
      }
      else
      {
        // component-wise `pos - center`
        dr_x = (pos_x - center.x) * ca + (pos_y - center.y) * sa;
        dr_y = (pos_x - center.x) * (-sa) + (pos_y - center.y) * ca;
      }

      float dist_to_center = std::sqrt(dr_x * dr_x + dr_y * dr_y);
      dist_to_center *= -2.f / (1.f + std::exp(-20.f * dr_x)) + 1.f;

      const glm::vec2 pos_r(kw.x * (dr_x + 0.2f * dist_to_center +
                                    0.1f * (val0 - 0.f)),
                            kw.y * dr_y);

      float noise_v = ref_base_voronoi_f1df2(pos_r,
                                             glm::vec2(1.f, 1.f),
                                             0.05f,
                                             fseed);
      noise_v = ref_smax(std::fmin(noise_v, 1.f), 0.f, 0.05f);
      noise_v = ref_remap_from(noise_v, 1.f, 0.f, 0.f, 1.f);

      val += amplitude * noise_v;

      // apply_mask = false, reverse_mask = false
      val = ref_lerp(val0, val, 1.f);

      z(i, j) = val;
    }

  return z;
}

// --- parity scenes ---------------------------------------------------------
// The parity scenes mirror the hydraulic harness: a seeded Perlin fBm, which
// remap() clamps to [0, 1] (so rifts' pow(val0, mask_gamma) elevation mask
// stays inside its domain), and a synthetic ramp whose gradient is constant
// and non-degenerate.

Array scene_ramp(int n = 32)
{
  Array z(glm::ivec2(n, n));

  for (int j = 0; j < n; j++)
    for (int i = 0; i < n; i++)
      z(i, j) = 0.2f + 0.6f * (float)i / (float)(n - 1) +
                0.1f * (float)j / (float)(n - 1);

  return z;
}

// A second synthetic scene, oriented across scene_ramp so the two velocity
// fields are not colinear. Used as the advected field when the terrain is the
// ramp.
Array scene_ramp_field(int n = 32)
{
  Array z(glm::ivec2(n, n));

  for (int j = 0; j < n; j++)
    for (int i = 0; i < n; i++)
      z(i, j) = 0.15f + 0.7f * (float)j / (float)(n - 1) -
                0.05f * (float)i / (float)(n - 1);

  return z;
}

// A radial mask in [0, 1] for the rifts supplied-mask case: 1 in the middle,
// tapering to 0 at the corners, so the mask actually modulates the blend.
Array make_rifts_mask(glm::ivec2 shape)
{
  Array m(shape);

  for (int j = 0; j < shape.y; j++)
    for (int i = 0; i < shape.x; i++)
    {
      const float dx = (float)i / (float)(shape.x - 1) - 0.5f;
      const float dy = (float)j / (float)(shape.y - 1) - 0.5f;
      m(i, j) = std::clamp(1.f - 1.6f * std::sqrt(dx * dx + dy * dy),
                           0.f,
                           1.f);
    }

  return m;
}

// --- parity metrics --------------------------------------------------------

struct Metrics
{
  float max_abs;
  float rms_abs;
};

struct PctMetrics
{
  float p99_abs;
  float frac_over;
};

float array_range(const Array &a)
{
  float vmin = FLT_MAX;
  float vmax = -FLT_MAX;

  for (float v : a.vector)
  {
    vmin = std::min(vmin, v);
    vmax = std::max(vmax, v);
  }

  return vmax - vmin;
}

Metrics diff_metrics(const Array &a, const Array &b)
{
  double sum = 0.0;
  float  m = 0.f;

  for (std::size_t k = 0; k < a.vector.size(); k++)
  {
    const float d = std::fabs(a.vector[k] - b.vector[k]);
    m = std::max(m, d);
    sum += (double)d * (double)d;
  }

  return {m, (float)std::sqrt(sum / (double)a.vector.size())};
}

float mean_abs_diff(const Array &a, const Array &b)
{
  double s = 0.0;
  for (std::size_t k = 0; k < a.vector.size(); k++)
    s += (double)std::fabs(a.vector[k] - b.vector[k]);
  return (float)(s / (double)a.vector.size());
}

double array_sum(const Array &a)
{
  double s = 0.0;
  for (float v : a.vector) s += (double)v;
  return s;
}

// 99th percentile (nearest rank) and the fraction of cells strictly above the
// threshold. Sorting is fine at 32x32 / 64x64 scene sizes.
PctMetrics pct_metrics(const Array &a, const Array &b, float threshold)
{
  std::vector<float> d;
  d.reserve(a.vector.size());

  for (std::size_t k = 0; k < a.vector.size(); k++)
    d.push_back(std::fabs(a.vector[k] - b.vector[k]));

  std::sort(d.begin(), d.end());

  const std::size_t n = d.size();
  std::size_t       rank = (std::size_t)std::ceil(0.99 * (double)n);
  if (rank == 0) rank = 1;

  const float p99 = d[std::min(n - 1, rank - 1)];

  std::size_t over = 0;
  for (float v : d)
    if (v > threshold) over++;

  return {p99, (float)((double)over / (double)n)};
}

std::string fmt(double v)
{
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.9g", v);
  return std::string(buf);
}

void print_parity_header_once()
{
  static bool done = false;
  if (done) return;
  done = true;
  std::printf("PARITY,variant,scene,iterations,max_abs,rms_abs,range\n");
}

void print_pct_header_once()
{
  static bool done = false;
  if (done) return;
  done = true;
  std::printf(
      "PARITY_PCT,variant,scene,iterations,p99_abs,frac_over,threshold,range\n");
}

void print_agg_header_once()
{
  static bool done = false;
  if (done) return;
  done = true;
  std::printf(
      "PARITY_AGG,variant,scene,iterations,rel_total_pct,mean_abs,range\n");
}

std::string property_key(const char *variant, const char *scene, int iters)
{
  return std::string(variant) + "." + scene + "." + std::to_string(iters);
}

void print_parity_line(const char *variant,
                       const char *scene,
                       int         iterations,
                       const Metrics &m,
                       float         range)
{
  print_parity_header_once();
  std::printf("PARITY,%s,%s,%d,%.9g,%.9g,%.9g\n",
              variant,
              scene,
              iterations,
              (double)m.max_abs,
              (double)m.rms_abs,
              (double)range);
  std::fflush(stdout);

  const std::string key = property_key(variant, scene, iterations);
  testing::Test::RecordProperty(key + ".max_abs", fmt(m.max_abs));
  testing::Test::RecordProperty(key + ".rms_abs", fmt(m.rms_abs));
  testing::Test::RecordProperty(key + ".range", fmt(range));
}

void print_pct_line(const char       *variant,
                    const char       *scene,
                    int               iterations,
                    const PctMetrics &p,
                    float             threshold,
                    float             range)
{
  print_pct_header_once();
  std::printf("PARITY_PCT,%s,%s,%d,%.9g,%.9g,%.9g,%.9g\n",
              variant,
              scene,
              iterations,
              (double)p.p99_abs,
              (double)p.frac_over,
              (double)threshold,
              (double)range);
  std::fflush(stdout);

  const std::string key = property_key(variant, scene, iterations);
  testing::Test::RecordProperty(key + ".p99_abs", fmt(p.p99_abs));
  testing::Test::RecordProperty(key + ".frac_over", fmt(p.frac_over));
  testing::Test::RecordProperty(key + ".threshold", fmt(threshold));
}

// Per-cell comparison for cases whose arithmetic must match cell by cell
// (one particle per cell).
void expect_parity_cells(const char   *variant,
                         const char   *scene,
                         int           iterations,
                         const Array  &cpu,
                         const Array  &gpu,
                         float         range,
                         float         max_ratio,
                         float         rms_ratio,
                         bool          gate)
{
  const Metrics m = diff_metrics(cpu, gpu);

  print_parity_line(variant, scene, iterations, m, range);

  if (!gate) return;

  EXPECT_LE(m.max_abs, max_ratio * range)
      << variant << "/" << scene << "/" << iterations
      << ": max|cpu-gpu| = " << m.max_abs << " > " << max_ratio * range;
  EXPECT_LE(m.rms_abs, rms_ratio * range)
      << variant << "/" << scene << "/" << iterations
      << ": rms|cpu-gpu| = " << m.rms_abs << " > " << rms_ratio * range;
}

// Distribution-based comparison for rifts and warp: the kernel hash and the
// sampling path are discontinuous, so a single-cell maximum is diagnostic and
// the gates are RMS, p99 and the fraction of cells above 1e-2 * range.
void expect_parity_distribution(const char  *variant,
                                const char  *scene,
                                int          iterations,
                                const Array &cpu,
                                const Array &gpu,
                                float        range,
                                float        rms_ratio,
                                float        p99_ratio,
                                float        frac_bound,
                                bool         gate)
{
  const Metrics    m = diff_metrics(cpu, gpu);
  const float      threshold = 1e-2f * range;
  const PctMetrics p = pct_metrics(cpu, gpu, threshold);

  print_parity_line(variant, scene, iterations, m, range);
  print_pct_line(variant, scene, iterations, p, threshold, range);

  if (!gate) return;

  EXPECT_LE(m.rms_abs, rms_ratio * range)
      << variant << "/" << scene << "/" << iterations
      << ": rms|cpu-gpu| = " << m.rms_abs << " > " << rms_ratio * range;
  EXPECT_LE(p.p99_abs, p99_ratio * range)
      << variant << "/" << scene << "/" << iterations
      << ": p99|cpu-gpu| = " << p.p99_abs << " > " << p99_ratio * range;
  EXPECT_LE(p.frac_over, frac_bound)
      << variant << "/" << scene << "/" << iterations
      << ": frac_over = " << p.frac_over << " > " << frac_bound;
}

// Aggregate comparison for many-particle advection: the per-cell maps are
// diagnostics only (non-atomic +=), the gates are total mass and mean |dz|.
// The mean bound stays at 1e-2 * range until the next oracle run re-freezes it
// from the post-fix numbers (it is expected to drop to the arithmetic level
// together with the one-per-cell gate).
void expect_parity_aggregate(const char  *variant,
                             const char  *scene,
                             int          iterations,
                             const Array &cpu,
                             const Array &gpu,
                             float        range,
                             bool         gate)
{
  const Metrics diag = diff_metrics(cpu, gpu);

  const double sum_cpu = array_sum(cpu);
  const double sum_gpu = array_sum(gpu);
  const double rel_total_pct = 100.0 * std::fabs(sum_cpu - sum_gpu) /
                               std::max(1e-12, std::fabs(sum_gpu));
  const float mean_abs = mean_abs_diff(cpu, gpu);

  print_parity_line(variant, scene, iterations, diag, range);

  print_agg_header_once();
  std::printf("PARITY_AGG,%s,%s,%d,%.9g,%.9g,%.9g\n",
              variant,
              scene,
              iterations,
              rel_total_pct,
              (double)mean_abs,
              (double)range);
  std::fflush(stdout);

  const std::string key = property_key(variant, scene, iterations);
  testing::Test::RecordProperty(key + ".rel_total_pct", fmt(rel_total_pct));
  testing::Test::RecordProperty(key + ".mean_abs", fmt(mean_abs));

  if (!gate) return;

  EXPECT_LE(rel_total_pct, 5.0)
      << variant << "/" << scene << "/" << iterations
      << ": relative total mass differs by " << rel_total_pct << "%";
  EXPECT_LE(mean_abs, 1e-2f * range)
      << variant << "/" << scene << "/" << iterations
      << ": mean |dz| = " << mean_abs << " > " << 1e-2f * range;
}

// --- direct-kernel oracle helpers -----------------------------------------
// Copied from tests/src/test_hydraulic_cpu.cpp into this file's anonymous
// namespace: extraction into a shared header stays deferred (design D7, Open
// Questions) so the passing hydraulic suite is not refactored by this change.

struct FloatImageFormat
{
  cl::ImageFormat format = cl::ImageFormat(CL_R, CL_FLOAT);
  int             channels = 0;
  bool            valid = false;
};

FloatImageFormat choose_float_image_format()
{
  FloatImageFormat out;

  const cl::ImageFormat candidates[] = {cl::ImageFormat(CL_R, CL_FLOAT),
                                        cl::ImageFormat(CL_RGBA, CL_FLOAT)};

  for (const auto &fmt : candidates)
  {
    cl_int err = CL_SUCCESS;
    try
    {
      cl::Image2D probe(clwrapper::KernelManager::context(),
                        CL_MEM_READ_WRITE,
                        fmt,
                        1,
                        1,
                        0,
                        nullptr,
                        &err);
      if (err == CL_SUCCESS)
      {
        out.format = fmt;
        out.channels = (fmt.image_channel_order == CL_RGBA) ? 4 : 1;
        out.valid = true;
        return out;
      }
    }
    catch (...)
    {
    }
  }

  return out;
}

cl::Image2D make_input_image(const Array &a, const FloatImageFormat &fmt)
{
  if (!fmt.valid) throw std::runtime_error("no usable float image format");

  cl_int             err = CL_SUCCESS;
  std::vector<float> host;
  void              *host_ptr = const_cast<float *>(a.vector.data());

  if (fmt.channels == 4)
  {
    host.resize(a.vector.size() * 4);
    for (std::size_t k = 0; k < a.vector.size(); ++k)
    {
      const float v = a.vector[k];
      host[4 * k + 0] = v;
      host[4 * k + 1] = v;
      host[4 * k + 2] = v;
      host[4 * k + 3] = v;
    }
    host_ptr = host.data();
  }

  cl::Image2D img(clwrapper::KernelManager::context(),
                  CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                  fmt.format,
                  a.shape.x,
                  a.shape.y,
                  0,
                  host_ptr,
                  &err);
  if (err != CL_SUCCESS)
    throw std::runtime_error("failed to create a float input image");

  return img;
}

cl::Image2D make_output_image(glm::ivec2 shape, const FloatImageFormat &fmt)
{
  if (!fmt.valid) throw std::runtime_error("no usable float image format");

  cl_int      err = CL_SUCCESS;
  cl::Image2D img(clwrapper::KernelManager::context(),
                  CL_MEM_WRITE_ONLY,
                  fmt.format,
                  shape.x,
                  shape.y,
                  0,
                  nullptr,
                  &err);
  if (err != CL_SUCCESS)
    throw std::runtime_error("failed to create a float output image");

  return img;
}

void read_image(const cl::Image2D      &img,
                Array                  &a,
                const FloatImageFormat &fmt,
                const cl::CommandQueue &queue)
{
  const std::size_t  n = (std::size_t)a.shape.x * (std::size_t)a.shape.y;
  std::vector<float> host(n * (std::size_t)fmt.channels);

  std::size_t origin[3] = {0, 0, 0};
  std::size_t region[3] = {(std::size_t)a.shape.x, (std::size_t)a.shape.y, 1};

  const cl_int err = clEnqueueReadImage(queue(),
                                        img(),
                                        CL_TRUE,
                                        origin,
                                        region,
                                        0,
                                        0,
                                        host.data(),
                                        0,
                                        nullptr,
                                        nullptr);
  if (err != CL_SUCCESS)
    throw std::runtime_error("failed to read a float image");

  if (fmt.channels == 1)
    std::copy(host.begin(), host.end(), a.vector.begin());
  else
    for (std::size_t k = 0; k < n; ++k) a.vector[k] = host[4 * k];
}

void set_image(clwrapper::Run &run, int index, const cl::Image2D &img)
{
  run.set_argument(index, img);
}

// --- direct-kernel buffer oracle helpers -----------------------------------
// The hydraulic harness has image helpers only; the particle kernel works on
// plain buffers. bind_buffer() registers a host vector but does NOT upload it,
// so the reference must write every buffer - in particular the zeroed `out`
// and `count` grids that the pinned wrapper never uploads (design D7, AD-028).

void bind_and_upload(clwrapper::Run &run,
                     const std::string &id,
                     std::vector<float> &host)
{
  run.bind_buffer<float>(id, host);
  run.write_buffer(id);
}

// --- direct-kernel advection_warp reference ---------------------------------
// Mirrors src/transform/advection_warp_gpu.cpp:34-53 argument order:
//   z, field, dx, dy, mask, out (images), nx, ny, advection_length,
//   value_persistence (scalars).
Array gpu_warp_reference(const Array            &z,
                         const Array            &advected_field,
                         const Array            &dx,
                         const Array            &dy,
                         const Array            *p_mask,
                         float                   advection_length,
                         float                   value_persistence,
                         const FloatImageFormat &fmt)
{
  const int nx = z.shape.x;
  const int ny = z.shape.y;

  Array mask_one;
  if (!p_mask) mask_one = Array(z.shape, 1.f);

  clwrapper::Run run("advection_warp");

  const cl::Image2D z_img = make_input_image(z, fmt);
  const cl::Image2D field_img = make_input_image(advected_field, fmt);
  const cl::Image2D dx_img = make_input_image(dx, fmt);
  const cl::Image2D dy_img = make_input_image(dy, fmt);
  const cl::Image2D mask_img = make_input_image(p_mask ? *p_mask : mask_one, fmt);
  const cl::Image2D out_img = make_output_image(z.shape, fmt);

  set_image(run, 0, z_img);
  set_image(run, 1, field_img);
  set_image(run, 2, dx_img);
  set_image(run, 3, dy_img);
  set_image(run, 4, mask_img);
  set_image(run, 5, out_img);
  run.set_argument(6, nx);
  run.set_argument(7, ny);
  run.set_argument(8, advection_length);
  run.set_argument(9, value_persistence);

  run.execute({nx, ny});

  Array out(z.shape);
  read_image(out_img, out, fmt, run.get_queue());

  return out;
}

// --- direct-kernel advection_particle reference ----------------------------
// Mirrors src/transform/advection_particle_gpu.cpp:230-255 buffer order:
//   field, dx, dy, out, count, advection_mask (buffers), then
//   nx, ny, nparticles, seed, advection_sign, advection_length,
//   value_persistence, inertia, has_advection_mask, num_fields (scalars).
// Differences from the pinned wrapper, both required by design D7:
//   - `out` and `count` are explicitly uploaded as zeros (the wrapper allocates
//     them read-write but never uploads any contents);
//   - the host post-processing loop sweeps the whole grid. The wrapper's loop
//     bounds are swapped (j < shape.x / i < shape.y), which is out of bounds on
//     a non-square grid; it is identical on the square scenes used here.

Array particle_host_stage(const Array  &accum,
                          const Array  &advected_field,
                          const Array  &count,
                          bool          post_filter,
                          float         post_filter_sigma,
                          const Array  *p_advection_mask,
                          const Array  *p_mask)
{
  Array out(advected_field.shape);

  for (int j = 0; j < advected_field.shape.y; ++j)
    for (int i = 0; i < advected_field.shape.x; ++i)
    {
      if (count(i, j) != 0.f)
      {
        out(i, j) = (accum(i, j) + advected_field(i, j)) / (count(i, j) + 1.f);
        out(i, j) = std::clamp(out(i, j), 0.f, 1.f);
      }
      else
      {
        out(i, j) = advected_field(i, j);
      }
    }

  // same host stage as advection_particle_gpu.cpp:283-291
  if (post_filter) hmap::laplace(out, p_advection_mask, post_filter_sigma, 1);

  if (p_mask)
    return hmap::lerp(out, advected_field, *p_mask);
  else
    return out;
}

Array gpu_particle_reference(const Array  &dx,
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
  const glm::ivec2 shape = dx.shape;
  const int        nx = shape.x;
  const int        ny = shape.y;
  const std::size_t n = (std::size_t)nx * (std::size_t)ny;

  clwrapper::Run run("advection_particle");

  // one grid per field: with num_fields = 1 the concatenated field buffer is
  // the advected field itself (the wrapper reserves num_fields * stride floats)
  std::vector<float> field_host = advected_field.vector;
  std::vector<float> dx_host = dx.vector;
  std::vector<float> dy_host = dy.vector;
  std::vector<float> accum_host(n, 0.f);
  std::vector<float> count_host(n, 0.f);
  std::vector<float> mask_dummy(1, 0.f);

  std::vector<float> mask_host;
  if (p_advection_mask) mask_host = p_advection_mask->vector;

  bind_and_upload(run, "advected_field", field_host);
  bind_and_upload(run, "dx", dx_host);
  bind_and_upload(run, "dy", dy_host);

  // explicit zero uploads - the pinned wrapper allocates out/count without
  // ever writing them (candidate upstream issue, docs/rifts-advection-parity.md)
  bind_and_upload(run, "out", accum_host);
  bind_and_upload(run, "count", count_host);

  if (p_advection_mask)
  {
    bind_and_upload(run, "advection_mask", mask_host);
  }
  else
  {
    // the kernel always reads a mask pointer; has_advection_mask == 0 makes it
    // unreachable, so a one-float dummy mirrors helper_bind_optional_buffer
    bind_and_upload(run, "advection_mask", mask_dummy);
  }

  run.bind_arguments(nx,
                     ny,
                     nparticles,
                     seed,
                     reverse ? -1.f : 1.f,
                     advection_length,
                     value_persistence,
                     inertia,
                     p_advection_mask ? 1 : 0,
                     1);

  run.execute(nparticles);

  run.read_buffer("out");
  run.read_buffer("count");

  Array accum(shape);
  accum.vector = accum_host;

  Array count(shape);
  count.vector = count_host;

  return particle_host_stage(accum,
                             advected_field,
                             count,
                             post_filter,
                             post_filter_sigma,
                             p_advection_mask,
                             p_mask);
}

} // namespace

// --- rifts CPU invariants --------------------------------------------------

TEST(RiftsAdvectionCpuInvariants, RiftsSmoke)
{
  const glm::vec2 kw(4.f, 1.2f);

  // determinism across two calls
  Array a = scene_fbm(32);
  Array b = scene_fbm(32);

  rifts(a, kw, 30.f, 0.1f, 11);
  rifts(b, kw, 30.f, 0.1f, 11);

  expect_identical(a, b);
  EXPECT_TRUE(all_finite(a));

  // amplitude 0 is a no-op. apply_mask is disabled so the elevation-mask
  // blend uses t = 1 and lerp(val0, val0, 1) is exactly val0 (with the
  // default t = val0 the blend may round by an ULP).
  Array z0 = scene_fbm(32);
  Array z = z0;

  rifts(z, kw, 30.f, 0.f, 11, 0.f, 0.05f, 0.05f, 0.2f, 0.1f, 0.f, 0.f, false);

  expect_identical(z, z0);
}

TEST(RiftsAdvectionCpuInvariants, ApplyMaskEndpoints)
{
  const glm::vec2 kw(4.f, 1.2f);
  Array           z0 = scene_fbm(32);

  Array mask_zero(z0.shape, 0.f);
  Array mask_one(z0.shape, 1.f);

  const Array mask_zero_before = mask_zero;
  const Array mask_one_before = mask_one;

  Array z_unmasked = z0;
  rifts(z_unmasked, kw, 30.f, 0.1f, 11);

  // a zero input mask leaves every cell unchanged (t = 0)
  Array z_masked_zero = z0;
  rifts(z_masked_zero,
        kw,
        30.f,
        0.1f,
        11,
        0.f,
        0.05f,
        0.05f,
        0.2f,
        0.1f,
        0.f,
        0.f,
        true,
        false,
        1.f,
        nullptr,
        nullptr,
        &mask_zero);
  expect_identical(z_masked_zero, z0);
  expect_identical(mask_zero, mask_zero_before);

  // a one input mask is the mask-free call (t = 1)
  Array z_masked_one = z0;
  rifts(z_masked_one,
        kw,
        30.f,
        0.1f,
        11,
        0.f,
        0.05f,
        0.05f,
        0.2f,
        0.1f,
        0.f,
        0.f,
        true,
        false,
        1.f,
        nullptr,
        nullptr,
        &mask_one);
  expect_identical(z_masked_one, z_unmasked);
  expect_identical(mask_one, mask_one_before);
}

TEST(RiftsAdvectionCpuInvariants, ReverseMaskQuirk)
{
  const glm::vec2 kw(4.f, 1.2f);

  // apply_mask = false, reverse_mask = true flips t from 1 to 0, so the
  // rifts are disabled entirely and the grid comes back unchanged
  {
    Array z0 = scene_fbm(32);
    Array z = z0;

    rifts(z,
          kw,
          30.f,
          0.1f,
          11,
          0.f,
          0.05f,
          0.05f,
          0.2f,
          0.1f,
          0.f,
          0.f,
          false,
          true,
          1.f);

    expect_identical(z, z0);
  }

  // on a grid holding zero in one region: apply_mask = true with
  // reverse_mask = false gives t = pow(0, 1) = 0 there, so the region stays
  // untouched; reverse_mask = true flips t to 1 and deforms it
  {
    Array z0 = scene_fbm(32);

    for (int j = 8; j < 24; j++)
      for (int i = 8; i < 24; i++) z0(i, j) = 0.f;

    Array z_normal = z0;
    rifts(z_normal,
          kw,
          30.f,
          0.1f,
          11,
          0.f,
          0.05f,
          0.05f,
          0.2f,
          0.1f,
          0.f,
          0.f,
          true,
          false,
          1.f);

    Array z_reverse = z0;
    rifts(z_reverse,
          kw,
          30.f,
          0.1f,
          11,
          0.f,
          0.05f,
          0.05f,
          0.2f,
          0.1f,
          0.f,
          0.f,
          true,
          true,
          1.f);

    std::size_t normal_changed = 0;
    std::size_t reverse_changed = 0;

    for (int j = 8; j < 24; j++)
      for (int i = 8; i < 24; i++)
      {
        if (z_normal(i, j) != z0(i, j)) normal_changed++;
        if (z_reverse(i, j) != z0(i, j)) reverse_changed++;
      }

    EXPECT_EQ(normal_changed, 0u);
    EXPECT_GT(reverse_changed, 0u);
  }
}

TEST(RiftsAdvectionCpuInvariants, NoiseOffsets)
{
  const glm::vec2 kw(4.f, 1.2f);

  Array z0 = scene_fbm(32);
  Array noise_x = scene_fbm(32);
  Array noise_y = scene_fbm(32);

  const Array noise_x_before = noise_x;
  const Array noise_y_before = noise_y;

  Array z_plain = z0;
  rifts(z_plain, kw, 30.f, 0.1f, 11);

  Array z_offset = z0;
  rifts(z_offset,
        kw,
        30.f,
        0.1f,
        11,
        0.f,
        0.05f,
        0.05f,
        0.2f,
        0.1f,
        0.f,
        0.f,
        true,
        false,
        1.f,
        &noise_x,
        &noise_y);

  // the offsets move pos before the Voronoi lookup, so the result changes
  expect_identical(noise_x, noise_x_before);
  expect_identical(noise_y, noise_y_before);
  EXPECT_NE(std::memcmp(z_plain.vector.data(),
                        z_offset.vector.data(),
                        z0.vector.size() * sizeof(float)),
            0);
}

TEST(RiftsAdvectionCpuInvariants, CenterBroadcast)
{
  const glm::vec2   kw(4.f, 1.2f);
  const glm::vec2   center(0.3f, 0.7f); // unequal components
  const std::uint32_t seed = 11;
  const float       angle = 30.f;
  const float       amplitude = 0.1f;

  Array z0 = scene_fbm(32);
  Array z_port = z0;

  rifts(z_port, kw, angle, amplitude, seed, 0.f, 0.05f, 0.05f, 0.2f, 0.1f,
        0.f, 0.f, false, false, 1.f, nullptr, nullptr, nullptr, center);

  const Array z_broadcast = reference_rifts(z0,
                                            kw,
                                            angle,
                                            amplitude,
                                            seed,
                                            center,
                                            true);
  const Array z_componentwise = reference_rifts(z0,
                                                kw,
                                                angle,
                                                amplitude,
                                                seed,
                                                center,
                                                false);

  // the two references differ only in the center handling, and that
  // difference is macroscopic on this scene
  EXPECT_GT(max_abs_diff(z_broadcast, z_componentwise), 1e-2f);
  EXPECT_GT(cells_over(z_broadcast, z_componentwise, 1e-3f),
            z0.vector.size() / 10);

  // the port follows the kernel's scalar-broadcast form: against a
  // component-wise subtraction reference the difference is macroscopic
  EXPECT_GT(max_abs_diff(z_port, z_componentwise), 1e-2f);
  EXPECT_GT(cells_over(z_port, z_componentwise, 1e-3f),
            z0.vector.size() / 10);
}

TEST(RiftsAdvectionCpuInvariants, AsymmetricBbox)
{
  const glm::vec2 kw(4.f, 1.2f);

  Array z0 = scene_fbm(32);

  // bbox order is (xmin, xmax, ymin, ymax)
  glm::vec4 bbox_xyxy(0.f, 1.f, 0.25f, 0.75f);

  // the swapped interpretation would read the four components as
  // (xmin, xmax, xmin, xmax), i.e. shrink x and invert y
  glm::vec4 bbox_swapped(0.f, 0.25f, 1.f, 0.75f);

  Array z_xyxy = z0;
  rifts(z_xyxy, kw, 30.f, 0.1f, 11, 0.f, 0.05f, 0.05f, 0.2f, 0.1f, 0.f, 0.f,
        true, false, 1.f, nullptr, nullptr, nullptr, glm::vec2(0.5f, 0.5f),
        bbox_xyxy);

  Array z_swapped = z0;
  rifts(z_swapped, kw, 30.f, 0.1f, 11, 0.f, 0.05f, 0.05f, 0.2f, 0.1f, 0.f,
        0.f, true, false, 1.f, nullptr, nullptr, nullptr,
        glm::vec2(0.5f, 0.5f), bbox_swapped);

  EXPECT_GT(cells_over(z_xyxy, z_swapped, 1e-6f), 0u);
  EXPECT_NE(std::memcmp(z_xyxy.vector.data(),
                        z_swapped.vector.data(),
                        z0.vector.size() * sizeof(float)),
            0);
}

TEST(RiftsAdvectionCpuInvariants, ShapeValidation)
{
  const glm::vec2 kw(4.f, 1.2f);

  Array z0 = scene_fbm(32);
  Array bad(glm::ivec2(16, 16), 0.5f);

  // a mismatched mask or noise-offset shape returns without writing the
  // terrain, matching src/erosion/rifts_gpu.cpp
  {
    Array z = z0;
    rifts(z, kw, 30.f, 0.1f, 11, 0.f, 0.05f, 0.05f, 0.2f, 0.1f, 0.f, 0.f,
          true, false, 1.f, nullptr, nullptr, &bad);
    expect_identical(z, z0);
  }

  {
    Array z = z0;
    rifts(z, kw, 30.f, 0.1f, 11, 0.f, 0.05f, 0.05f, 0.2f, 0.1f, 0.f, 0.f,
          true, false, 1.f, &bad, nullptr, nullptr);
    expect_identical(z, z0);
  }

  {
    Array z = z0;
    rifts(z, kw, 30.f, 0.1f, 11, 0.f, 0.05f, 0.05f, 0.2f, 0.1f, 0.f, 0.f,
          true, false, 1.f, nullptr, &bad, nullptr);
    expect_identical(z, z0);
  }
}

// --- warp CPU invariants ---------------------------------------------------
// advection_warp transcribes advection_warp.cl: the normalized
// mirrored-repeat sampler, the capped uphill walk and the backward
// persistence blend (design D4).

TEST(RiftsAdvectionCpuInvariants, WarpZeroLengthIsZeros)
{
  Array z = scene_fbm(32);
  Array field = scene_fbm(32, 12);

  // advection_length = 0 gives limit = 0, an empty path and an all-zero
  // output - not a copy of the input field (kernel quirk, design D4)
  Array out = advection_warp(z, field, 0.f, 0.9f);

  ASSERT_EQ(out.shape, z.shape);
  for (float v : out.vector) EXPECT_EQ(v, 0.f);

  EXPECT_NE(std::memcmp(out.vector.data(),
                        field.vector.data(),
                        field.vector.size() * sizeof(float)),
            0);
}

TEST(RiftsAdvectionCpuInvariants, WarpExplicitGradientEqualsGradientDriven)
{
  Array z = scene_fbm(32);
  Array field = scene_fbm(32, 12);

  // the z-driven form computes dx = gradient_x(z), dy = gradient_y(z)
  // (uphill, no negation) and delegates to the explicit form
  Array dx = gradient_x(z);
  Array dy = gradient_y(z);

  expect_identical(advection_warp(z, field),
                   advection_warp(z, field, dx, dy));
}

TEST(RiftsAdvectionCpuInvariants, WarpStepCap)
{
  // 4096x1 grid, constant dx = 1 / dy = 0 and a flat z. Both lengths ask for
  // at least 1024 steps (1.5 / (1 / 4096) and 0.25 / (1 / 4096) = 1024), so
  // both are capped at MAX_STEPS and produce the same path and output.
  const int nx = 4096;

  Array z(glm::ivec2(nx, 1), 0.5f);
  Array dx(glm::ivec2(nx, 1), 1.f);
  Array dy(glm::ivec2(nx, 1), 0.f);

  Array field(glm::ivec2(nx, 1));
  for (int i = 0; i < nx; i++) field(i, 0) = (float)i / (float)(nx - 1);

  Array out_long = advection_warp(z, field, dx, dy, 1.5f, 0.9f);
  Array out_short = advection_warp(z, field, dx, dy, 0.25f, 0.9f);

  expect_identical(out_long, out_short);
  EXPECT_TRUE(all_finite(out_long));
}

TEST(RiftsAdvectionCpuInvariants, WarpMaskBlend)
{
  Array z = scene_fbm(32);
  Array field = scene_fbm(32, 12);

  Array dx = gradient_x(z);
  Array dy = gradient_y(z);

  Array mask_zero(z.shape, 0.f);
  Array mask_one(z.shape, 1.f);

  Array no_mask = advection_warp(z, field, dx, dy);
  Array with_one = advection_warp(z, field, dx, dy, 0.1f, 0.9f, &mask_one);
  Array with_zero = advection_warp(z, field, dx, dy, 0.1f, 0.9f, &mask_zero);

  // a null mask is the wrapper's constant-one image
  expect_identical(no_mask, with_one);

  // ratio = mask * value_persistence = 0 makes the backward pass take the
  // new value at every step, so the last evaluated position is the start cell
  // and the result is the field read at the cell centers
  expect_identical(with_zero, field);

  EXPECT_NE(std::memcmp(with_zero.vector.data(),
                        no_mask.vector.data(),
                        no_mask.vector.size() * sizeof(float)),
            0);
}

TEST(RiftsAdvectionCpuInvariants, WarpFlatStartFinite)
{
  Array z_flat(glm::ivec2(32, 32), 0.5f);
  Array field = scene_fbm(32, 12);

  // the kernel's `dir` is uninitialized on a flat start; the port pins it to
  // (0, 0), so the walk stays on the start cell and the blend returns the
  // field value there (documented deviation, design D4)
  Array out = advection_warp(z_flat, field);

  EXPECT_TRUE(all_finite(out));
  EXPECT_LT(max_abs_diff(out, field), 1e-5f);
}

TEST(RiftsAdvectionCpuInvariants, WarpDeterministic)
{
  Array z = scene_fbm(32);
  Array field = scene_fbm(32, 12);

  expect_identical(advection_warp(z, field), advection_warp(z, field));
}

// --- particle CPU invariants ----------------------------------------------
// advection_particle transcribes advection_particle.cl: the seeded walk, the
// deterministic zeroed accumulation and the wrapper host stage (design D5).

TEST(RiftsAdvectionCpuInvariants, ParticleExplicitVelocityEqualsGradient)
{
  Array z = scene_fbm(32);
  Array field = scene_fbm(32, 12);

  // the z-driven form computes dx = -gradient_x(z), dy = -gradient_y(z)
  // (downhill) and delegates to the explicit form
  Array dx = -gradient_x(z);
  Array dy = -gradient_y(z);

  expect_identical(particle(z, field, 512, 11),
                   advection_particle(dx, dy, field, 512, 11));
}

TEST(RiftsAdvectionCpuInvariants, ParticleNormalizationFormula)
{
  // 2x1 grid, one particle per cell. With nx = 2 every draw starts in cell 0
  // ((int)((nx - 1) * rand) == 0), the constant +x velocity moves it one cell
  // and advection_length = 0.5 / nx gives exactly one step, so every particle
  // accumulates once in cell 1. count = 2 and accum = val + val.
  Array dx(glm::ivec2(2, 1), 1.f);
  Array dy(glm::ivec2(2, 1), 0.f);

  Array field(glm::ivec2(2, 1));
  field(0, 0) = 0.2f;
  field(1, 0) = 0.8f;

  const float p = 0.99f;

  // val = lerp(field[idx], val, value_persistence) with the kernel form
  const float val = (1.f - p) * field(1, 0) + p * field(0, 0);

  // out = clamp((accum + original) / (count + 1), 0, 1)
  const float expected = std::clamp((2.f * val + field(1, 0)) / 3.f, 0.f, 1.f);

  Array out = advection_particle(dx,
                                 dy,
                                 field,
                                 2,
                                 11u,
                                 false,
                                 false,
                                 0.125f,
                                 0.5f,
                                 p,
                                 0.f);

  // cell 0 is never visited (count == 0) and comes back as the original
  EXPECT_EQ(out(0, 0), field(0, 0));
  EXPECT_NEAR(out(1, 0), expected, 1e-6f);
}

TEST(RiftsAdvectionCpuInvariants, ParticleStartDrawOrder)
{
  // advection_particle.cl:36 draws the starting position as
  //   pos = (float2)((nx - 1) * rand(&rng_state), (ny - 1) * rand(&rng_state));
  // and an OpenCL vector literal takes its elements in source order: x gets the
  // FIRST draw. Passing both calls as the two arguments of one C++ constructor
  // leaves that order unspecified (clang evaluates left-to-right, GCC
  // right-to-left), and swapping the two transposes every starting cell - that
  // swap was the entire CPU-vs-GPU divergence of the first parity run
  // (docs/rifts-advection-parity.md, "Particle draw order"). This pins the
  // element order on any compiler.
  const int nx = 32;
  const int ny = 32;

  // particle id 0: rng_state = pcg_hash(seed + id * 2u)
  std::uint32_t rng_state = ref_pcg_hash(11u);
  const int     gx = (int)((float)(nx - 1) * ref_rand(rng_state));
  const int     gy = (int)((float)(ny - 1) * ref_rand(rng_state));

  // guards that make the transposed start observable: the start, its +x
  // neighbour and the transposed start / neighbour are four distinct cells
  ASSERT_LT(gx, nx - 1);
  ASSERT_LT(gy, ny - 1);
  ASSERT_NE(gx, gy);
  ASSERT_NE(gx + 1, gy);
  ASSERT_NE(gy + 1, gx);

  // constant +x velocity and advection_length 0.02 (dist_max = 0.64): exactly
  // one step, so the single particle deposits on (gx + 1, gy)
  Array dx(glm::ivec2(nx, ny), 1.f);
  Array dy(glm::ivec2(nx, ny), 0.f);

  // the start cell is the only non-zero cell, so a transposed start lands on a
  // zero cell and leaves the expected deposit cell untouched
  Array field(glm::ivec2(nx, ny), 0.f);
  field(gx, gy) = 1.f;

  Array out = advection_particle(dx,
                                 dy,
                                 field,
                                 1,
                                 11u,
                                 false,
                                 false,
                                 0.125f,
                                 0.02f,
                                 0.99f,
                                 0.f);

  const float val = (1.f - 0.99f) * field(gx + 1, gy) + 0.99f * field(gx, gy);

  EXPECT_NEAR(out(gx + 1, gy), (val + field(gx + 1, gy)) / 2.f, 1e-6f);
  EXPECT_EQ(out(gx, gy), field(gx, gy)); // count == 0 at the start cell
  EXPECT_EQ(out(gy, gx), 0.f);
  EXPECT_EQ(out(gy + 1, gx), 0.f);
}

TEST(RiftsAdvectionCpuInvariants, ParticleInertiaGuard)
{
  Array z = scene_fbm(32);
  Array field = scene_fbm(32, 12);

  // inertia = 1 with dir_prev = (0, 0) makes the kernel normalize a zero
  // vector (undefined); the port keeps dir = (0, 0), the particle never
  // moves and the count stays zero, so the field comes back unchanged
  Array out = particle(z,
                       field,
                       256,
                       11,
                       false,
                       false,
                       0.125f,
                       0.1f,
                       0.99f,
                       1.f);

  expect_identical(out, field);
  EXPECT_TRUE(all_finite(out));
}

TEST(RiftsAdvectionCpuInvariants, ParticlePostFilterOff)
{
  Array z = scene_fbm(32);
  Array field = scene_fbm(32, 12);

  // zero particles: nothing is accumulated, so post_filter = false returns
  // the original field and post_filter = true runs the wrapper's
  // hmap::laplace(out, p_advection_mask, post_filter_sigma, 1)
  Array unfiltered = particle(z, field, 0, 11, false, false);
  Array filtered = particle(z, field, 0, 11, false, true);

  expect_identical(unfiltered, field);

  EXPECT_NE(std::memcmp(filtered.vector.data(),
                        field.vector.data(),
                        field.vector.size() * sizeof(float)),
            0);
  EXPECT_TRUE(all_finite(filtered));
}

TEST(RiftsAdvectionCpuInvariants, ParticleAdvectionMask)
{
  Array z = scene_fbm(32);
  Array field = scene_fbm(32, 12);

  Array mask_zero(z.shape, 0.f);
  Array mask_one(z.shape, 1.f);

  // left half one, right half zero
  Array mask_half(z.shape, 0.f);
  for (int j = 0; j < z.shape.y; j++)
    for (int i = 0; i < z.shape.x / 2; i++) mask_half(i, j) = 1.f;

  Array no_mask = particle(z, field, 256, 11, false, false);
  Array with_one = particle(z,
                            field,
                            256,
                            11,
                            false,
                            false,
                            0.125f,
                            0.1f,
                            0.99f,
                            0.f,
                            &mask_one);
  Array with_zero = particle(z,
                             field,
                             256,
                             11,
                             false,
                             false,
                             0.125f,
                             0.1f,
                             0.99f,
                             0.f,
                             &mask_zero);
  Array with_half = particle(z,
                             field,
                             256,
                             11,
                             false,
                             false,
                             0.125f,
                             0.1f,
                             0.99f,
                             0.f,
                             &mask_half);

  // a constant-one mask takes the same one draw as the mask-free path and
  // never stops the walk
  expect_identical(no_mask, with_one);

  // a zero mask keeps the last rejection draw, then the first post-move check
  // stops the particle, so nothing is accumulated
  expect_identical(with_zero, field);

  // a particle stops before accumulating once it steps into a zero cell, so
  // the zero region is never touched
  for (int j = 0; j < z.shape.y; j++)
    for (int i = z.shape.x / 2; i < z.shape.x; i++)
      EXPECT_EQ(with_half(i, j), field(i, j));

  EXPECT_NE(std::memcmp(with_half.vector.data(),
                        field.vector.data(),
                        field.vector.size() * sizeof(float)),
            0);
}

TEST(RiftsAdvectionCpuInvariants, ParticleDeterministic)
{
  Array z = scene_fbm(32);
  Array field = scene_fbm(32, 12);

  expect_identical(particle(z, field, 256, 11),
                   particle(z, field, 256, 11));
}

// --- particle iterations, multi-field and the 64-field cap -----------------

TEST(RiftsAdvectionCpuInvariants, ParticleIterationsChain)
{
  Array z = scene_fbm(32);
  Array field = scene_fbm(32, 12);

  // the iterations form loops the single-pass solver with the same seed and
  // feeds each output back as the next advected field
  Array chained = particle_iterations(z, field, 3, 512, 11, false, false);

  Array manual = field;
  for (int k = 0; k < 3; k++)
    manual = particle(z, manual, 512, 11, false, false);

  expect_identical(chained, manual);
}

TEST(RiftsAdvectionCpuInvariants, ParticleMultiFieldEqualsPerField)
{
  Array z = scene_fbm(32);

  std::vector<Array> fields{scene_fbm(32, 12),
                            scene_fbm(32, 13),
                            scene_fbm(32, 14)};

  auto multi = advection_particle(z, fields, 256, 11);

  ASSERT_EQ(multi.size(), fields.size());
  for (std::size_t k = 0; k < fields.size(); k++)
    expect_identical(multi[k], particle(z, fields[k], 256, 11));
}

TEST(RiftsAdvectionCpuInvariants, ParticleVectorIterationsForm)
{
  Array z = scene_fbm(32);

  std::vector<Array> fields{scene_fbm(32, 12), scene_fbm(32, 13)};

  // the iterations form loops the multi-field solver with the same seed and
  // feeds each output back as the next pass's advected field, so every field
  // evolves independently of the others
  auto multi = particles_iterations(z, fields, 2, 256, 11, false, false);

  ASSERT_EQ(multi.size(), fields.size());
  for (std::size_t k = 0; k < fields.size(); k++)
  {
    const Array per_field = particle_iterations(z,
                                                fields[k],
                                                2,
                                                256,
                                                11,
                                                false,
                                                false);
    expect_identical(multi[k], per_field);
    EXPECT_TRUE(all_finite(multi[k]));
  }
}

TEST(RiftsAdvectionCpuInvariants, ParticleFieldCap)
{
  Array z = scene_fbm(32);

  // strictly positive values so the unaccumulated post-processing always
  // differs from the original at a visited cell
  Array field(z.shape);
  for (int j = 0; j < z.shape.y; j++)
    for (int i = 0; i < z.shape.x; i++)
      field(i, j) = 0.25f + 0.5f * (float)((i * 7 + j * 13) % 17) / 16.f;

  std::vector<Array> fields(65, field);

  auto out = particles(z, fields, 256, 11, false, false);

  ASSERT_EQ(out.size(), fields.size());

  // advection_particle.cl caps num_f at 64, so the first 64 identical fields
  // accumulate identically
  for (int f = 0; f < 64; f++) expect_identical(out[0], out[f]);

  // field 64 keeps its zero accumulator and only receives the host-stage
  // division by (count + 1)
  EXPECT_NE(std::memcmp(out[64].vector.data(),
                        out[0].vector.data(),
                        out[0].vector.size() * sizeof(float)),
            0);
  EXPECT_GT(cells_over(out[64], field, 0.f), 0u);
}

TEST(RiftsAdvectionCpuInvariants, ParticleShapeValidation)
{
  Array z = scene_fbm(32);
  Array field = scene_fbm(32, 12);
  Array bad(glm::ivec2(16, 16), 0.5f);

  // mismatched advected field, single-field and vector forms
  EXPECT_TRUE(particle(z, bad, 64, 11).vector.empty());
  EXPECT_TRUE(advection_particle(z, std::vector<Array>{bad}, 64, 11).empty());

  // mismatched dy, field, empty dx and empty field list
  Array dx = gradient_x(z);
  Array dy = gradient_y(z);

  EXPECT_TRUE(advection_particle(dx, dy, bad, 64, 11).vector.empty());
  EXPECT_TRUE(advection_particle(dx, bad, field, 64, 11).vector.empty());
  EXPECT_TRUE(advection_particle(dx,
                                 dy,
                                 std::vector<Array>{},
                                 64,
                                 11)
                  .empty());

  // empty terrain
  EXPECT_TRUE(particle(Array(), field, 64, 11).vector.empty());

  // mismatched advection mask and post-mask
  Array bad_mask(glm::ivec2(16, 16), 1.f);

  EXPECT_TRUE(advection_particle(dx,
                                 dy,
                                 field,
                                 64,
                                 11,
                                 false,
                                 true,
                                 0.125f,
                                 0.1f,
                                 0.99f,
                                 0.f,
                                 &bad_mask)
                  .vector.empty());
  EXPECT_TRUE(advection_particle(dx,
                                 dy,
                                 field,
                                 64,
                                 11,
                                 false,
                                 true,
                                 0.125f,
                                 0.1f,
                                 0.99f,
                                 0.f,
                                 nullptr,
                                 &bad_mask)
                  .vector.empty());
}

// --- advection edge-case invariants ---------------------------------------

TEST(RiftsAdvectionCpuInvariants, InputsAreByteIdentical)
{
  Array z = scene_fbm(32);
  Array field = scene_fbm(32, 12);
  Array dx = gradient_x(z);
  Array dy = gradient_y(z);
  Array mask(glm::ivec2(32, 32), 1.f);

  const Array z_before = z;
  const Array field_before = field;
  const Array dx_before = dx;
  const Array dy_before = dy;
  const Array mask_before = mask;

  advection_warp(z, field, dx, dy, 0.1f, 0.9f, &mask);
  particle(z,
           field,
           256,
           11,
           false,
           true,
           0.125f,
           0.1f,
           0.99f,
           0.f,
           &mask,
           &mask);

  expect_identical(z, z_before);
  expect_identical(field, field_before);
  expect_identical(dx, dx_before);
  expect_identical(dy, dy_before);
  expect_identical(mask, mask_before);
}

TEST(RiftsAdvectionCpuInvariants, OmittedPointersEqualNullPointers)
{
  Array z = scene_fbm(32);
  Array field = scene_fbm(32, 12);
  Array dx = gradient_x(z);
  Array dy = gradient_y(z);

  expect_identical(particle(z, field, 256, 11),
                   particle(z,
                            field,
                            256,
                            11,
                            false,
                            true,
                            0.125f,
                            0.1f,
                            0.99f,
                            0.f,
                            nullptr,
                            nullptr));

  expect_identical(advection_warp(z, field),
                   advection_warp(z, field, 0.1f, 0.9f, nullptr));

  expect_identical(advection_warp(z, field, dx, dy),
                   advection_warp(z, field, dx, dy, 0.1f, 0.9f, nullptr));
}

TEST(RiftsAdvectionCpuInvariants, ZeroParticlesAndZeroIterations)
{
  Array z = scene_fbm(32);
  Array field = scene_fbm(32, 12);

  // zero particles with post_filter = false returns the original field
  Array out = particle(z, field, 0, 11, false, false);

  expect_identical(out, field);
  EXPECT_NE(out.vector.data(), field.vector.data());

  const Array field_before = field;
  out(0, 0) += 1.f;
  expect_identical(field, field_before);

  // iterations = 0 skips the solver loop and returns a copy of the input
  Array out_zero = particle_iterations(z, field, 0, 256, 11);

  expect_identical(out_zero, field);
  EXPECT_NE(out_zero.vector.data(), field.vector.data());

  out_zero(0, 0) += 1.f;
  expect_identical(field, field_before);
}

TEST(RiftsAdvectionCpuInvariants, ReverseDirection)
{
  // one row, constant +x velocity and a single step: forward accumulation
  // lands one cell to the right (cell 0 is never reached) and reverse
  // accumulation one cell to the left (the last cell is never reached)
  const int nx = 64;

  Array dx(glm::ivec2(nx, 1), 1.f);
  Array dy(glm::ivec2(nx, 1), 0.f);

  Array field(glm::ivec2(nx, 1));
  for (int i = 0; i < nx; i++)
    field(i, 0) = 0.2f + 0.6f * (float)((i * 37) % 61) / 60.f;

  Array forward = advection_particle(dx,
                                     dy,
                                     field,
                                     512,
                                     11u,
                                     false,
                                     false,
                                     0.125f,
                                     0.01f,
                                     0.99f,
                                     0.f);
  Array reverse = advection_particle(dx,
                                     dy,
                                     field,
                                     512,
                                     11u,
                                     true,
                                     false,
                                     0.125f,
                                     0.01f,
                                     0.99f,
                                     0.f);

  EXPECT_EQ(forward(0, 0), field(0, 0));
  EXPECT_EQ(reverse(nx - 1, 0), field(nx - 1, 0));

  EXPECT_GT(cells_over(forward, field, 0.f), 0u);
  EXPECT_GT(cells_over(reverse, field, 0.f), 0u);

  EXPECT_NE(std::memcmp(forward.vector.data(),
                        reverse.vector.data(),
                        field.vector.size() * sizeof(float)),
            0);
}

TEST(RiftsAdvectionCpuInvariants, InertiaDeterminism)
{
  Array z = scene_fbm(32);
  Array field = scene_fbm(32, 12);

  Array inertia_zero_a = particle(z, field, 256, 11, false, false, 0.125f,
                                  0.1f, 0.99f, 0.f);
  Array inertia_zero_b = particle(z, field, 256, 11, false, false, 0.125f,
                                  0.1f, 0.99f, 0.f);
  Array inertia_point_two_a = particle(z, field, 256, 11, false, false,
                                       0.125f, 0.1f, 0.99f, 0.2f);
  Array inertia_point_two_b = particle(z, field, 256, 11, false, false,
                                       0.125f, 0.1f, 0.99f, 0.2f);

  expect_identical(inertia_zero_a, inertia_zero_b);
  expect_identical(inertia_point_two_a, inertia_point_two_b);

  // inertia is not inert: the two settings walk different paths
  EXPECT_NE(std::memcmp(inertia_zero_a.vector.data(),
                        inertia_point_two_a.vector.data(),
                        field.vector.size() * sizeof(float)),
            0);
}

TEST(RiftsAdvectionCpuInvariants, RiftsAdvectionCallOrder)
{
  // both functions are free of hidden state and never write to their inputs,
  // so each one's output depends only on its own inputs: rifts returns the
  // same grid whether an advection call happened before or after it, and
  // advection returns the same grid whenever it is given the same terrain
  const glm::vec2 kw(4.f, 1.2f);

  Array z0 = scene_fbm(32);
  Array field = scene_fbm(32, 12);

  // order A: rifts, then advection
  Array z_a = z0;
  rifts(z_a, kw, 30.f, 0.1f, 11);
  Array advection_a = particle(z_a, field, 256, 11, false, false);

  // order B: advection, then rifts
  Array z_b = z0;
  Array advection_b = particle(z_b, field, 256, 11, false, false);
  rifts(z_b, kw, 30.f, 0.1f, 11);

  // rifts' output is identical in both orders (advection left z_b untouched)
  expect_identical(z_a, z_b);

  // advection's output is also identical in both orders: the run started
  // before the rifts call matches a fresh run on the pristine terrain (rifts
  // only ever wrote to its own copy), and the run on the rifts output is
  // reproducible whichever order the rifts call happened in
  expect_identical(advection_b, particle(z0, field, 256, 11, false, false));

  Array advection_a_again = particle(z_a, field, 256, 11, false, false);
  expect_identical(advection_a, advection_a_again);

  // and advection does depend on the terrain, so the comparison is not vacuous
  EXPECT_NE(std::memcmp(advection_a.vector.data(),
                        advection_b.vector.data(),
                        field.vector.size() * sizeof(float)),
            0);
}

// --- GPU-vs-CPU parity -----------------------------------------------------

class RiftsAdvectionParity : public ::testing::Test
{
protected:
  void SetUp() override
  {
    static const bool opencl_ready = gpu::init_opencl();

    if (!opencl_ready)
      GTEST_SKIP() << "no OpenCL platform/device available - parity cannot be "
                      "verified on this machine";
  }
};

// --- rifts -----------------------------------------------------------------
// Oracle: the pinned hmap::gpu::rifts wrapper, which binds every argument and
// has no known gap (design D7). The rifts hash is fract(sin(...)), so a 1-ULP
// trig difference can flip a whole Voronoi cell: the maximum is recorded as a
// diagnostic and the gates are RMS, p99 and the fraction of cells above
// 1e-2 * range.

TEST_F(RiftsAdvectionParity, Rifts)
{
  const glm::vec2     kw(4.f, 1.2f);
  const std::uint32_t seed = 11;

  struct Case
  {
    const char *name;
    const char *scene;
    Array (*build)();
    float angle;
    float amplitude;
    int   extra; // 0 = none, 1 = supplied mask, 2 = noise offsets
  };

  const Case cases[] = {
      {"rifts_a0_amp005",
       "fbm",
       []() { return scene_fbm(32); },
       0.f,
       0.05f,
       0},
      {"rifts_a90_amp02",
       "fbm",
       []() { return scene_fbm(32); },
       90.f,
       0.2f,
       0},
      {"rifts_a0_amp02",
       "ramp",
       []() { return scene_ramp(32); },
       0.f,
       0.2f,
       0},
      {"rifts_a90_amp005",
       "ramp",
       []() { return scene_ramp(32); },
       90.f,
       0.05f,
       0},
      {"rifts_64_neighborhood",
       "fbm64",
       []() { return scene_fbm(64); },
       30.f,
       0.1f,
       0},
      {"rifts_supplied_mask",
       "fbm",
       []() { return scene_fbm(32); },
       30.f,
       0.1f,
       1},
      {"rifts_noise_offsets",
       "fbm",
       []() { return scene_fbm(32); },
       30.f,
       0.1f,
       2},
  };

  for (const auto &c : cases)
  {
    const Array base = c.build();
    const float range = array_range(base);

    Array mask, noise_x, noise_y;

    if (c.extra == 1) mask = make_rifts_mask(base.shape);
    if (c.extra == 2)
    {
      // the kernel adds the per-cell offsets after the bbox scaling
      // (rifts.cl); [0, 1] values shift the Voronoi lookup by up to one
      // domain unit and keep the pow(val0, mask_gamma) input untouched
      noise_x = scene_fbm(base.shape.x, 21);
      noise_y = scene_fbm(base.shape.x, 22);
    }

    const Array *p_mask = (c.extra == 1) ? &mask : nullptr;
    const Array *p_noise_x = (c.extra == 2) ? &noise_x : nullptr;
    const Array *p_noise_y = (c.extra == 2) ? &noise_y : nullptr;

    // every option spelled out on both sides so the calls cannot drift apart
    Array gpu_z = base;
    gpu::rifts(gpu_z,
               kw,
               c.angle,
               c.amplitude,
               seed,
               0.f,
               0.05f,
               0.05f,
               0.2f,
               0.1f,
               0.f,
               0.f,
               true,
               false,
               1.f,
               p_noise_x,
               p_noise_y,
               p_mask);

    Array cpu_z = base;
    rifts(cpu_z,
          kw,
          c.angle,
          c.amplitude,
          seed,
          0.f,
          0.05f,
          0.05f,
          0.2f,
          0.1f,
          0.f,
          0.f,
          true,
          false,
          1.f,
          p_noise_x,
          p_noise_y,
          p_mask);

    // Frozen bounds (task 5.4, first oracle run on PoCl, tree e349bec1). The
    // transcription is exact at reading/rounding level - the low-amplitude
    // scenes measure rms 9.0e-4 / 4.5e-4 and frac_over 0 - but the Voronoi
    // hash fract(sin(...)) flips whole cells under -cl-fast-relaxed-math
    // (design D7 / AD-028), so the worst scenes (rifts_a0_amp02 ramp and
    // rifts_noise_offsets) measure rms 1.08e-2 / 1.04e-2, p99 5.0e-2 / 4.3e-2
    // and frac_over 0.134 / 0.193. Bounds carry ~2x headroom over the worst
    // measured value and stay far below an O(1) formula error. max_abs is
    // recorded as an ungated diagnostic.
    expect_parity_distribution(c.name,
                               c.scene,
                               1,
                               cpu_z,
                               gpu_z,
                               range,
                               2.5e-2f, // rms bound (worst measured 1.08e-2)
                               1e-1f,   // p99 bound (worst measured 5.0e-2)
                               4e-1f,   // frac_over bound (worst measured 0.193)
                               true);
  }
}

// --- advection_warp --------------------------------------------------------
// Oracle: a direct kernel launch with the CL_R -> CL_RGBA probe, because the
// pinned wrapper binds CL_R/CL_FLOAT images that PoCL may reject (design D7).
// The kernel reads z, dx, dy and mask through a mirrored-repeat linear sampler,
// so path flips and sampler precision put single-cell maxima out of scope; the
// gates are RMS, p99 and frac_over. Flat starts are excluded (the kernel's
// uninitialized `dir`); they only run in the diagnostic case below.

TEST_F(RiftsAdvectionParity, Warp)
{
  const FloatImageFormat fmt = choose_float_image_format();

  if (!fmt.valid)
    GTEST_SKIP() << "OpenCL device cannot create float images (no float image "
                    "support)";

  testing::Test::RecordProperty("advection_warp.image_format",
                                fmt.channels == 4 ? "CL_RGBA" : "CL_R");

  struct SceneCase
  {
    const char *name;
    Array (*terrain)();
    Array (*field)();
  };

  const SceneCase scenes[] = {
      {"fbm",
       []() { return scene_fbm(32); },
       []() { return scene_fbm(32, 12); }},
      {"ramp",
       []() { return scene_ramp(32); },
       []() { return scene_ramp_field(32); }},
  };

  const float lengths[] = {0.01f, 0.05f, 0.1f};
  const float persistences[] = {0.5f, 0.96f, 1.f};

  for (const auto &scene : scenes)
  {
    const Array z = scene.terrain();
    const Array field = scene.field();
    const Array dx = gradient_x(z);
    const Array dy = gradient_y(z);
    const float range = array_range(field);

    for (float length : lengths)
      for (float persistence : persistences)
      {
        char variant[64];
        std::snprintf(variant,
                      sizeof(variant),
                      "advection_warp_l%03d_p%04d",
                      (int)std::lround(1000.f * length),
                      (int)std::lround(1000.f * persistence));

        // the z-driven wrapper computes dx = gradient_x(z), dy = gradient_y(z)
        // on the host (uphill, no negation); the direct kernel binds exactly
        // those grids, so both implementations see the same velocity field
        const Array cpu = advection_warp(z,
                                         field,
                                         dx,
                                         dy,
                                         length,
                                         persistence);
        const Array gpu = gpu_warp_reference(z,
                                             field,
                                             dx,
                                             dy,
                                             nullptr,
                                             length,
                                             persistence,
                                             fmt);

        expect_parity_distribution(variant,
                                   scene.name,
                                   1,
                                   cpu,
                                   gpu,
                                   range,
                                   1e-3f, // provisional rms bound, task 5.4
                                   1e-2f, // provisional p99 bound
                                   1e-2f, // provisional frac_over bound
                                   true);
      }
  }

  // supplied mask: the left half is zero, so the backward pass takes the new
  // value at every step there and persistence only applies on the right half
  {
    const Array z = scene_fbm(32);
    const Array field = scene_fbm(32, 12);
    const Array dx = gradient_x(z);
    const Array dy = gradient_y(z);

    Array mask(z.shape, 1.f);
    for (int j = 0; j < z.shape.y; j++)
      for (int i = 0; i < z.shape.x / 2; i++) mask(i, j) = 0.f;

    const Array cpu = advection_warp(z,
                                     field,
                                     dx,
                                     dy,
                                     0.05f,
                                     0.9f,
                                     &mask);
    const Array gpu = gpu_warp_reference(z,
                                         field,
                                         dx,
                                         dy,
                                         &mask,
                                         0.05f,
                                         0.9f,
                                         fmt);

    expect_parity_distribution("advection_warp_supplied_mask",
                               "fbm",
                               1,
                               cpu,
                               gpu,
                               array_range(field),
                               1e-3f,
                               1e-2f,
                               1e-2f,
                               true);
  }

  // explicit constant dx/dy: no host gradient, so this isolates the sampler and
  // the walk from hmap::gradient_x/y
  {
    const Array z = scene_ramp(32);
    const Array field = scene_ramp_field(32);
    const Array dx(glm::ivec2(32, 32), 0.02f);
    const Array dy(glm::ivec2(32, 32), -0.01f);

    const Array cpu = advection_warp(z,
                                     field,
                                     dx,
                                     dy,
                                     0.05f,
                                     0.9f);
    const Array gpu = gpu_warp_reference(z,
                                         field,
                                         dx,
                                         dy,
                                         nullptr,
                                         0.05f,
                                         0.9f,
                                         fmt);

    expect_parity_distribution("advection_warp_constant_velocity",
                               "ramp",
                               1,
                               cpu,
                               gpu,
                               array_range(field),
                               1e-3f,
                               1e-2f,
                               1e-2f,
                               true);
  }
}

TEST_F(RiftsAdvectionParity, WarpFlatStartDiagnostic)
{
  const FloatImageFormat fmt = choose_float_image_format();

  if (!fmt.valid)
    GTEST_SKIP() << "OpenCL device cannot create float images (no float image "
                    "support)";

  testing::Test::RecordProperty("advection_warp_flat_start_diag.image_format",
                                fmt.channels == 4 ? "CL_RGBA" : "CL_R");

  // advection_warp.cl declares `dir` uninitialized and uses it on the first
  // step when the start is flat (undefined behaviour); the CPU port pins it to
  // (0, 0). The metrics document the deviation and are never gated.
  const Array z(glm::ivec2(32, 32), 0.5f);
  const Array field = scene_fbm(32, 12);
  const Array dx(z.shape, 0.f);
  const Array dy(z.shape, 0.f);

  const Array cpu = advection_warp(z, field, dx, dy);
  const Array gpu = gpu_warp_reference(z,
                                       field,
                                       dx,
                                       dy,
                                       nullptr,
                                       0.1f,
                                       0.9f,
                                       fmt);

  expect_parity_distribution("advection_warp_flat_start_diag",
                             "flat",
                             1,
                             cpu,
                             gpu,
                             array_range(field),
                             1e-3f,
                             1e-2f,
                             1e-2f,
                             false);
}

// --- advection_particle ----------------------------------------------------
// Oracle: a direct kernel launch with explicitly zero-uploaded `out` and
// `count` buffers, because the pinned wrapper never uploads them, plus a
// test-local replica of the wrapper host stage (design D7).

TEST_F(RiftsAdvectionParity, ParticleOnePerCell)
{
  struct SceneCase
  {
    const char *name;
    Array (*terrain)();
    Array (*field)();
  };

  const SceneCase scenes[] = {
      {"fbm",
       []() { return scene_fbm(32); },
       []() { return scene_fbm(32, 12); }},
      {"ramp",
       []() { return scene_ramp(32); },
       []() { return scene_ramp_field(32); }},
  };

  for (const auto &scene : scenes)
  {
    const Array z = scene.terrain();
    const Array field = scene.field();
    const Array dx = -gradient_x(z); // downhill, as in the wrapper
    const Array dy = -gradient_y(z);

    // one particle per cell; advection_length 0.02 gives dist_max = 0.64, so
    // each particle deposits at most once. post_filter is off so the host
    // stage is the normalization formula alone.
    const int nparticles = z.shape.x * z.shape.y;

    const Array cpu = advection_particle(dx,
                                         dy,
                                         field,
                                         nparticles,
                                         11u,
                                         false,
                                         false,
                                         0.125f,
                                         0.02f,
                                         0.99f,
                                         0.f);

    const Array gpu = gpu_particle_reference(dx,
                                             dy,
                                             field,
                                             nparticles,
                                             11u,
                                             false,
                                             false,
                                             0.125f,
                                             0.02f,
                                             0.99f,
                                             0.f,
                                             nullptr,
                                             nullptr);

    expect_parity_cells("advection_particle_one_per_cell",
                        scene.name,
                        1,
                        cpu,
                        gpu,
                        array_range(field),
                        1e-5f, // max bound: rounding level, the GPU is
                               // bit-repeatable (warp measured <= 3.3e-7)
                        2e-6f, // rms bound, same rationale
                        true);
  }
}

TEST_F(RiftsAdvectionParity, ParticleAggregate)
{
  struct SceneCase
  {
    const char *name;
    Array (*terrain)();
    Array (*field)();
  };

  const SceneCase scenes[] = {
      {"fbm",
       []() { return scene_fbm(32); },
       []() { return scene_fbm(32, 12); }},
      {"ramp",
       []() { return scene_ramp(32); },
       []() { return scene_ramp_field(32); }},
  };

  struct Case
  {
    const char *name;
    int         iterations;
    bool        reverse;
    float       inertia;
  };

  const Case cases[] = {{"particle_agg_iter1", 1, false, 0.f},
                        {"particle_agg_iter2", 2, false, 0.f},
                        {"particle_agg_reverse", 1, true, 0.f},
                        {"particle_agg_inertia02", 1, false, 0.2f}};

  const int   nparticles = 1000; // bounded by design D7 (<= 1000)
  const float length = 0.1f;
  const float persistence = 0.99f;

  for (const auto &scene : scenes)
  {
    const Array z = scene.terrain();
    const Array field = scene.field();
    const Array dx = -gradient_x(z);
    const Array dy = -gradient_y(z);
    const float range = array_range(field);

    for (const auto &c : cases)
    {
      Array cpu = field;
      Array gpu = field;

      // the `iterations` overloads are z-driven only, so both sides chain the
      // single-pass solver with the same seed (wrapper loop, design D5)
      for (int it = 0; it < c.iterations; ++it)
      {
        cpu = advection_particle(dx,
                                 dy,
                                 cpu,
                                 nparticles,
                                 11u,
                                 c.reverse,
                                 false,
                                 0.125f,
                                 length,
                                 persistence,
                                 c.inertia);

        gpu = gpu_particle_reference(dx,
                                     dy,
                                     gpu,
                                     nparticles,
                                     11u,
                                     c.reverse,
                                     false,
                                     0.125f,
                                     length,
                                     persistence,
                                     c.inertia,
                                     nullptr,
                                     nullptr);
      }

      // gated only on relative total mass and mean |dz|; per-cell max/rms are
      // diagnostics on the paired PARITY line
      expect_parity_aggregate(c.name,
                              scene.name,
                              c.iterations,
                              cpu,
                              gpu,
                              range,
                              true);
    }
  }
}

TEST_F(RiftsAdvectionParity, ParticleGpuRepeatability)
{
  // The kernel accumulates with a non-atomic +=, so a many-particle GPU result
  // is not reproducible. This runs one representative aggregate case twice
  // through the direct reference and records the GPU-vs-GPU spread on an
  // explicitly ungated PARITY_AGG line (design D7).
  const Array z = scene_fbm(32);
  const Array field = scene_fbm(32, 12);
  const Array dx = -gradient_x(z);
  const Array dy = -gradient_y(z);

  const Array gpu_a = gpu_particle_reference(dx,
                                             dy,
                                             field,
                                             1000,
                                             11u,
                                             false,
                                             false,
                                             0.125f,
                                             0.1f,
                                             0.99f,
                                             0.f,
                                             nullptr,
                                             nullptr);

  const Array gpu_b = gpu_particle_reference(dx,
                                             dy,
                                             field,
                                             1000,
                                             11u,
                                             false,
                                             false,
                                             0.125f,
                                             0.1f,
                                             0.99f,
                                             0.f,
                                             nullptr,
                                             nullptr);

  expect_parity_aggregate("advection_particle_gpu_repeat",
                          "fbm",
                          1,
                          gpu_a,
                          gpu_b,
                          array_range(field),
                          false);
}
