/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */

/* highmap-wasm CPU ports of the thermal erosion family.
 *
 * Each function below transcribes the matching OpenCL kernel from
 * src/gpu_opencl/kernels/ (thermal.cl, thermal_flatten.cl, thermal_inflate.cl,
 * thermal_olsen.cl, thermal_rib.cl, thermal_ridge.cl, thermal_scree.cl) and
 * reproduces the launch behaviour of src/erosion/thermal_gpu.cpp: neighbour
 * tables, coefficients, float32 accumulation order, border rules and the final
 * extrapolate_borders() pass. Do not "clean up" the arithmetic - the parity
 * harness (tests/src/test_thermal_cpu.cpp) compares this code against the GPU
 * family on the desktop build.
 *
 * thermal_schott is not implemented here: upstream already ships a CPU version
 * in src/erosion/thermal_schott.cpp (see erosion_thermal_cpu.hpp).
 *
 * Rebase rule: if upstream ever ships CPU implementations, delete this file and
 * the header.
 */

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <vector>

#include "highmap/array.hpp"
#include "highmap/boundary.hpp"
#include "highmap/erosion_thermal_cpu.hpp"
#include "highmap/internal/validation.hpp"
#include "highmap/math/array.hpp"
#include "highmap/operator.hpp"
#include "highmap/range.hpp"

namespace hmap
{

namespace
{

// --- neighbour pattern (Moore, same order as the OpenCL kernels) ------------
// clang-format off
constexpr int DI[8] = {-1, 0, 0, 1, -1, -1, 1, 1};
constexpr int DJ[8] = {0, 1, -1, 0, -1, 1, -1, 1};
// clang-format on

// Kernel literals kept verbatim. Both round to the same float32 value; they are
// kept separate so a diff against the .cl files stays line-by-line readable.
constexpr float CD[8] = {1.f,
                         1.f,
                         1.f,
                         1.f,
                         1.41421356f,
                         1.41421356f,
                         1.41421356f,
                         1.41421356f};

constexpr float CD_CONSERVE[8] = {1.f,
                                  1.f,
                                  1.f,
                                  1.f,
                                  1.414213562f,
                                  1.414213562f,
                                  1.414213562f,
                                  1.414213562f};

// reverse direction index: from neighbour k back to center (thermal_conserve,
// thermal_olsen)
constexpr int REV[8] = {3, 2, 1, 0, 7, 6, 5, 4};

// --- kernel helpers -------------------------------------------------------

// thermal.cl: helper_thermal_exchange
inline float helper_thermal_exchange(float self,
                                     float other,
                                     float dist,
                                     float talus)
{
  const float max_dif = dist * talus;
  const float rate = 0.25f;

  if (self > other)
  {
    if (self - other > max_dif)
      return -rate * ((self - other) - max_dif) / dist;
    return 0.f;
  }

  if (other - self > max_dif)
    return rate * ((other - self) - max_dif) / dist;
  return 0.f;
}

// _common_math.cl: smoothstep3
inline float smoothstep3(float x) { return x * x * (3.f - 2.f * x); }

// _common_math.cl: almost_unit_identity
inline float almost_unit_identity(float x) { return (2.f - x) * x * x; }

// OpenCL clamp(x, 0, 1) is fmin(fmax(x, 0), 1) - NaN maps to 0. std::clamp
// would propagate NaN, so the composition is explicit.
inline float clamp01(float x) { return std::fmin(std::fmax(x, 0.f), 1.f); }

// _common_rand.cl: hash21u (unsigned 32-bit wraparound is intentional)
inline std::uint32_t hash21u(std::uint32_t x, std::uint32_t y)
{
  std::uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u;
  h ^= h >> 13;
  h *= 0x85ebca6bu;
  h ^= h >> 16;
  return h;
}

// _common_index.cl: apply_boundaries_io - border cells receive the adjacent
// interior cell of the *input* buffer, in the kernel's check order (the gx
// tests win on the corners). Grids too small for a one-cell border are left
// untouched instead of reading out of bounds.
void freeze_border_1(const Array &z_in, Array &z_out)
{
  const int ni = z_in.shape.x;
  const int nj = z_in.shape.y;
  if (ni < 3 || nj < 3) return;

  for (int j = 0; j < nj; j++)
    for (int i = 0; i < ni; i++)
    {
      if (i == 0)
        z_out(i, j) = z_in(1, j);
      else if (i == ni - 1)
        z_out(i, j) = z_in(ni - 2, j);
      else if (j == 0)
        z_out(i, j) = z_in(i, 1);
      else if (j == nj - 1)
        z_out(i, j) = z_in(i, nj - 2);
    }
}

// _common_index.cl: apply_boundaries_buffer_io(z_in, z_out, ..., b) - cells
// outside the b-wide interior band are clamped to the nearest interior cell.
void clamp_border_band(const Array &z_in, Array &z_out, int b)
{
  const int ni = z_in.shape.x;
  const int nj = z_in.shape.y;
  if (ni < 2 * b + 1 || nj < 2 * b + 1) return;

  for (int j = 0; j < nj; j++)
    for (int i = 0; i < ni; i++)
    {
      if (i >= b && i < ni - b && j >= b && j < nj - b) continue;

      const int ci = std::clamp(i, b, ni - b - 1);
      const int cj = std::clamp(j, b, nj - b - 1);
      z_out(i, j) = z_in(ci, cj);
    }
}

} // namespace

void thermal(Array       &z,
             const Array &talus,
             int          iterations,
             const Array *p_bedrock,
             Array       *p_deposition_map)
{
  if (!validate_non_empty(z) || !validate_same_shape(z, talus)) return;
  if (p_bedrock && !validate_same_shape(z, *p_bedrock)) return;

  Array z_bckp = Array();
  if (p_deposition_map != nullptr) z_bckp = z;

  const bool with_bedrock = (p_bedrock != nullptr);
  Array      z_out(z.shape);

  for (int it = 0; it < iterations; it++)
  {
    freeze_border_1(z, z_out);

    for (int j = 1; j < z.shape.y - 1; j++)
      for (int i = 1; i < z.shape.x - 1; i++)
      {
        const float val = z(i, j);
        const float zb = with_bedrock ? (*p_bedrock)(i, j) : 0.f;

        float res = val;

        if (!with_bedrock || val >= zb)
        {
          const float talus_val = talus(i, j);
          float       amount = 0.f;

          for (int k = 0; k < 8; k++)
            amount += helper_thermal_exchange(val,
                                              z(i + DI[k], j + DJ[k]),
                                              CD[k],
                                              talus_val);

          res = val + amount;
        }

        z_out(i, j) = with_bedrock ? std::max(res, zb) : res;
      }

    std::swap(z.vector, z_out.vector);
  }

  extrapolate_borders(z);

  if (p_deposition_map) *p_deposition_map = maximum(z - z_bckp, 0.f);
}

void thermal(Array       &z,
             const Array *p_mask,
             const Array &talus,
             int          iterations,
             const Array *p_bedrock,
             Array       *p_deposition_map)
{
  apply_with_mask(z,
                  p_mask,
                  [&](Array &a)
                  {
                    thermal(a, talus, iterations, p_bedrock, p_deposition_map);
                  });
}

void thermal(Array       &z,
             float        talus,
             int          iterations,
             const Array *p_bedrock,
             Array       *p_deposition_map)
{
  if (!validate_non_empty(z)) return;
  if (p_bedrock && !validate_same_shape(z, *p_bedrock)) return;

  Array talus_map(z.shape, talus);
  thermal(z, talus_map, iterations, p_bedrock, p_deposition_map);
}

void thermal_auto_bedrock(Array       &z,
                          const Array &talus,
                          int          iterations,
                          Array       *p_deposition_map)
{
  if (!validate_non_empty(z) || !validate_same_shape(z, talus)) return;

  Array z_bckp = Array();
  if (p_deposition_map != nullptr) z_bckp = z;

  Array z_init = z;
  Array bedrock(z.shape);

  // thermal.cl seeds `bedrock[index] = -FLT_MAX` on the first iteration, then
  // tracks `max(z_init, new_z)` per cell.
  for (auto &v : bedrock.vector)
    v = -FLT_MAX;

  Array z_out(z.shape);

  for (int it = 0; it < iterations; it++)
  {
    freeze_border_1(z, z_out);

    for (int j = 1; j < z.shape.y - 1; j++)
      for (int i = 1; i < z.shape.x - 1; i++)
      {
        const float val = z(i, j);
        const float zb = bedrock(i, j);
        const float zi = z_init(i, j);

        if (val >= zb)
        {
          const float talus_val = talus(i, j);
          float       amount = 0.f;

          for (int k = 0; k < 8; k++)
            amount += helper_thermal_exchange(val,
                                              z(i + DI[k], j + DJ[k]),
                                              CD[k],
                                              talus_val);

          const float new_z = std::max(std::max(val + amount, zi), zb);
          z_out(i, j) = new_z;
          bedrock(i, j) = std::max(zi, new_z);
        }
        else
        {
          z_out(i, j) = std::max(val, zb);
        }
      }

    std::swap(z.vector, z_out.vector);
  }

  extrapolate_borders(z);

  if (p_deposition_map) *p_deposition_map = maximum(z - z_bckp, 0.f);
}

void thermal_auto_bedrock(Array       &z,
                          const Array *p_mask,
                          const Array &talus,
                          int          iterations,
                          Array       *p_deposition_map)
{
  apply_with_mask(
      z,
      p_mask,
      [&](Array &a)
      { thermal_auto_bedrock(a, talus, iterations, p_deposition_map); });
}

void thermal_auto_bedrock(Array &z,
                          float  talus,
                          int    iterations,
                          Array *p_deposition_map)
{
  if (!validate_non_empty(z)) return;

  Array talus_map(z.shape, talus);
  thermal_auto_bedrock(z, talus_map, iterations, p_deposition_map);
}

void thermal_conserve(Array       &z,
                      const Array &talus,
                      int          iterations,
                      float        rate,
                      const Array *p_bedrock,
                      Array       *p_deposition_map)
{
  if (!validate_non_empty(z) || !validate_same_shape(z, talus)) return;
  if (p_bedrock && !validate_same_shape(z, *p_bedrock)) return;

  Array z_bckp = Array();
  if (p_deposition_map != nullptr) z_bckp = z;

  const int ni = z.shape.x;
  const int nj = z.shape.y;

  Array z_out(z.shape);

  for (int it = 0; it < iterations; it++)
  {
    for (int j = 0; j < nj; j++)
      for (int i = 0; i < ni; i++)
      {
        const float z_c = z(i, j);

        // boundary cells: strictly inactive, unchanged (closed boundary)
        if (i <= 0 || i >= ni - 1 || j <= 0 || j >= nj - 1)
        {
          z_out(i, j) = z_c;
          continue;
        }

        const float talus_c = talus(i, j);

        // --- outflow from this active cell to other active cells
        float excess_sum = 0.f;
        float excess_max = 0.f;

        for (int k = 0; k < 8; k++)
        {
          const int ii = i + DI[k];
          const int jj = j + DJ[k];

          if (ii <= 0 || ii >= ni - 1 || jj <= 0 || jj >= nj - 1) continue;

          const float e = z_c - z(ii, jj) - CD_CONSERVE[k] * talus_c;

          if (e > 0.f)
          {
            excess_sum += e;
            excess_max = std::max(excess_max, e);
          }
        }

        float outflow = 0.f;
        if (excess_sum > 0.f) outflow = rate * 0.5f * excess_max;

        // --- inflow: for each active neighbour, recompute its outflow and how
        //     much of it is directed toward this cell
        float inflow = 0.f;

        for (int k = 0; k < 8; k++)
        {
          const int ii = i + DI[k];
          const int jj = j + DJ[k];

          if (ii <= 0 || ii >= ni - 1 || jj <= 0 || jj >= nj - 1) continue;

          const float z_n = z(ii, jj);
          const float talus_n = talus(ii, jj);

          float n_excess_sum = 0.f;
          float n_excess_max = 0.f;
          float n_excess_to_c = 0.f;

          for (int p = 0; p < 8; p++)
          {
            const int qi = ii + DI[p];
            const int qj = jj + DJ[p];

            if (qi <= 0 || qi >= ni - 1 || qj <= 0 || qj >= nj - 1) continue;

            const float e = z_n - z(qi, qj) - CD_CONSERVE[p] * talus_n;

            if (e > 0.f)
            {
              n_excess_sum += e;
              n_excess_max = std::max(n_excess_max, e);

              // direction from the neighbour back to the centre is REV[k]
              if (p == REV[k]) n_excess_to_c = e;
            }
          }

          if (n_excess_to_c > 0.f && n_excess_sum > 0.f)
          {
            const float n_outflow = rate * 0.5f * n_excess_max;
            inflow += n_outflow * n_excess_to_c / n_excess_sum;
          }
        }

        z_out(i, j) = z_c - outflow + inflow;
      }

    std::swap(z.vector, z_out.vector);
  }

  // bedrock limit applied once, after the iterations (kernel launch code)
  if (p_bedrock)
  {
    for (std::size_t k = 0; k < z.vector.size(); k++)
      z.vector[k] = std::max(z.vector[k], p_bedrock->vector[k]);
  }

  // no extrapolate_borders(): the boundary is closed

  if (p_deposition_map) *p_deposition_map = maximum(z - z_bckp, 0.f);
}

void thermal_conserve(Array       &z,
                      const Array *p_mask,
                      const Array &talus,
                      int          iterations,
                      float        rate,
                      const Array *p_bedrock,
                      Array       *p_deposition_map)
{
  apply_with_mask(z,
                  p_mask,
                  [&](Array &a)
                  {
                    thermal_conserve(a,
                                     talus,
                                     iterations,
                                     rate,
                                     p_bedrock,
                                     p_deposition_map);
                  });
}

void thermal_conserve(Array       &z,
                      float        talus,
                      int          iterations,
                      float        rate,
                      const Array *p_bedrock,
                      Array       *p_deposition_map)
{
  if (!validate_non_empty(z)) return;
  if (p_bedrock && !validate_same_shape(z, *p_bedrock)) return;

  Array talus_map(z.shape, talus);
  thermal_conserve(z, talus_map, iterations, rate, p_bedrock, p_deposition_map);
}

void thermal_flatten(Array       &z,
                     const Array &talus,
                     int          iterations,
                     float        sigma_inf,
                     float        sigma_sup)
{
  if (!validate_non_empty(z) || !validate_same_shape(z, talus)) return;

  Array z_out(z.shape);

  for (int it = 0; it < iterations; it++)
  {
    // kernel border rule: 2-cell band clamped to the nearest interior cell
    clamp_border_band(z, z_out, 2);

    for (int j = 2; j < z.shape.y - 2; j++)
      for (int i = 2; i < z.shape.x - 2; i++)
      {
        const int   rotation_offset = (i + j) & 7;
        const float zc = z(i, j);

        const float slope_min = 0.f;

        // --- remove material from the current cell (steepest receiver)
        float delta = 0.f;
        float dmax = 0.f;

        for (int n = 0; n < 8; n++)
        {
          const int   k = (n + rotation_offset) & 7;
          const float dz = (zc - z(i + DI[k], j + DJ[k])) / CD[k];

          if (dz > dmax) dmax = dz;
        }

        const float excess = std::max(dmax - slope_min, 0.f);

        if (dmax < talus(i, j)) delta -= sigma_inf * excess;
        if (dmax > talus(i, j)) delta -= sigma_sup * excess;

        // --- material received from neighbours whose steepest receiver is us
        for (int n = 0; n < 8; n++)
        {
          const int k  = (n + rotation_offset) & 7;
          const int pi = i - DI[k];
          const int pj = j - DJ[k];

          const float zp = z(pi, pj);

          float dmax_p = 0.f;
          int   ka_p = -1;

          for (int m = 0; m < 8; m++)
          {
            const int   kk = (m + rotation_offset) & 7;
            const float dz = (zp - z(pi + DI[kk], pj + DJ[kk])) / CD[kk];

            if (dz > dmax_p)
            {
              dmax_p = dz;
              ka_p = kk;
            }
          }

          if (ka_p == k && dmax_p > 0.f && dmax_p < talus(pi, pj))
            delta += 0.5f * dmax_p;
        }

        z_out(i, j) = zc + delta;
      }

    std::swap(z.vector, z_out.vector);
  }

  extrapolate_borders(z);
}

void thermal_flatten(Array       &z,
                     const Array *p_mask,
                     const Array &talus,
                     int          iterations,
                     float        sigma_inf,
                     float        sigma_sup)
{
  apply_with_mask(
      z,
      p_mask,
      [&](Array &a)
      { thermal_flatten(a, talus, iterations, sigma_inf, sigma_sup); });
}

void thermal_flatten(Array &z,
                     float  talus,
                     int    iterations,
                     float  sigma_inf,
                     float  sigma_sup)
{
  if (!validate_non_empty(z)) return;

  Array talus_map(z.shape, talus);
  thermal_flatten(z, talus_map, iterations, sigma_inf, sigma_sup);
}

void thermal_olsen(Array &z, const Array &talus, int iterations)
{
  if (!validate_non_empty(z) || !validate_same_shape(z, talus)) return;

  Array z_out(z.shape);

  for (int it = 0; it < iterations; it++)
  {
    // kernel border rule: 3-cell band clamped to the nearest interior cell
    clamp_border_band(z, z_out, 3);

    for (int j = 3; j < z.shape.y - 3; j++)
      for (int i = 3; i < z.shape.x - 3; i++)
      {
        const float z_val = z(i, j);
        const float talus_val = talus(i, j);

        float delta = 0.f;

        // 1. loss from the current cell to lower neighbours
        float dsum = 0.f;
        float dmax = 0.f;
        float dz[8];

        for (int p = 0; p < 8; p++)
        {
          const float diff = z_val - z(i + DI[p], j + DJ[p]);
          dz[p] = diff;

          if (diff > talus_val * CD[p])
          {
            dsum += diff;
            dmax = std::max(dmax, diff);
          }
        }

        if (dmax > 0.f && dsum > 0.f)
        {
          for (int p = 0; p < 8; p++)
            if (dz[p] > talus_val * CD[p])
            {
              const float amount = 0.30f * (dmax - talus_val * CD[p]) *
                                   dz[p] / dsum;
              delta -= amount;
            }
        }

        // 2. gain from higher neighbours, visited in a position-hashed order
        const std::uint32_t shift = hash21u((std::uint32_t)i,
                                            (std::uint32_t)j) &
                                    7u;

        for (int k = 0; k < 8; k++)
        {
          const int s    = (k + (int)shift) & 7;
          const int gk_i = i + DI[s];
          const int gk_j = j + DJ[s];

          const float talus_k = talus(gk_i, gk_j);
          const float z_k = z(gk_i, gk_j);

          float dsum_k = 0.f;
          float dmax_k = 0.f;
          float dz_k[8];

          for (int p = 0; p < 8; p++)
          {
            const float diff = z_k - z(gk_i + DI[p], gk_j + DJ[p]);
            dz_k[p] = diff;

            if (diff > talus_k * CD[p])
            {
              dsum_k += diff;
              dmax_k = std::max(dmax_k, diff);
            }
          }

          if (dmax_k > 0.f && dsum_k > 0.f)
          {
            const int   rev_p = REV[s];
            const float diff_to_c = dz_k[rev_p];

            if (diff_to_c > talus_k * CD[rev_p])
            {
              const float amount = 0.30f * (dmax_k - talus_k * CD[rev_p]) *
                                   diff_to_c / dsum_k;
              delta += amount;
            }
          }
        }

        z_out(i, j) = z_val + delta;
      }

    std::swap(z.vector, z_out.vector);
  }

  extrapolate_borders(z);
}

void thermal_olsen(Array       &z,
                   const Array *p_mask,
                   const Array &talus,
                   int          iterations)
{
  apply_with_mask(
      z, p_mask, [&](Array &a) { thermal_olsen(a, talus, iterations); });
}

void thermal_olsen(Array &z, float talus, int iterations)
{
  if (!validate_non_empty(z)) return;

  Array talus_map(z.shape, talus);
  thermal_olsen(z, talus_map, iterations);
}

void thermal_rib(Array &z, int iterations)
{
  if (!validate_non_empty(z)) return;

  Array z_out(z.shape);

  for (int it = 0; it < iterations; it++)
  {
    freeze_border_1(z, z_out);

    for (int j = 1; j < z.shape.y - 1; j++)
      for (int i = 1; i < z.shape.x - 1; i++)
      {
        float       delta_min = FLT_MAX;
        const float val = z(i, j);

        for (int k = 0; k < 8; k++)
        {
          const float dz = std::fabs(val - z(i + DI[k], j + DJ[k])) / CD[k];
          delta_min = std::min(delta_min, dz);
        }

        z_out(i, j) = val - delta_min;
      }

    std::swap(z.vector, z_out.vector);
  }

  extrapolate_borders(z, 3);
}

void thermal_rib(Array &z, const Array *p_mask, int iterations)
{
  apply_with_mask(z, p_mask, [&](Array &a) { thermal_rib(a, iterations); });
}

void thermal_inflate(Array &z, const Array &talus, int iterations)
{
  if (!validate_non_empty(z) || !validate_same_shape(z, talus)) return;

  Array z_out(z.shape);

  for (int it = 0; it < iterations; it++)
  {
    freeze_border_1(z, z_out);

    for (int j = 1; j < z.shape.y - 1; j++)
      for (int i = 1; i < z.shape.x - 1; i++)
      {
        const float val = z(i, j);
        float       sum = 0.f;
        float       slope_max = 0.f;

        for (int k = 0; k < 8; k++)
        {
          const float dz = (val - z(i + DI[k], j + DJ[k])) / CD[k];

          if (dz < 0.f) sum += dz;

          slope_max = std::max(slope_max, std::fabs(dz));
        }

        const float t = talus(i, j);

        // OpenCL clamp(): a flat cell (slope_max == 0) yields amp == 0
        float amp = clamp01(1.f - t / slope_max);
        amp = smoothstep3(amp);

        z_out(i, j) = val + 0.25f * (t - 0.5f * sum) * amp;
      }

    std::swap(z.vector, z_out.vector);
  }

  extrapolate_borders(z);
}

void thermal_inflate(Array       &z,
                     const Array *p_mask,
                     const Array &talus,
                     int          iterations)
{
  apply_with_mask(
      z, p_mask, [&](Array &a) { thermal_inflate(a, talus, iterations); });
}

void thermal_inflate(Array &z, float talus, int iterations)
{
  if (!validate_non_empty(z)) return;

  Array talus_map(z.shape, talus);
  thermal_inflate(z, talus_map, iterations);
}

void thermal_ridge(Array       &z,
                   const Array &talus,
                   int          iterations,
                   Array       *p_deposition_map)
{
  if (!validate_non_empty(z) || !validate_same_shape(z, talus)) return;

  Array z_bckp = Array();
  if (p_deposition_map != nullptr) z_bckp = z;

  Array z_out(z.shape);

  for (int it = 0; it < iterations; it++)
  {
    freeze_border_1(z, z_out);

    for (int j = 1; j < z.shape.y - 1; j++)
      for (int i = 1; i < z.shape.x - 1; i++)
      {
        const float val = z(i, j);
        float       sum = 0.f;
        float       slope_max = 0.f;

        for (int k = 0; k < 8; k++)
        {
          const float dz = (val - z(i + DI[k], j + DJ[k])) / CD[k];

          if (dz > 0.f) sum += dz;

          slope_max = std::max(slope_max, std::fabs(dz));
        }

        const float t = talus(i, j);

        float amp = clamp01(1.f - t / slope_max);
        amp = smoothstep3(amp);

        z_out(i, j) = val + 0.25f * (t - 0.5f * sum) * amp;
      }

    std::swap(z.vector, z_out.vector);
  }

  extrapolate_borders(z);

  if (p_deposition_map) *p_deposition_map = abs(z - z_bckp);
}

void thermal_ridge(Array       &z,
                   const Array *p_mask,
                   const Array &talus,
                   int          iterations,
                   Array       *p_deposition_map)
{
  apply_with_mask(z,
                  p_mask,
                  [&](Array &a)
                  { thermal_ridge(a, talus, iterations, p_deposition_map); });
}

void thermal_ridge(Array       &z,
                   float        talus,
                   int          iterations,
                   Array       *p_deposition_map)
{
  if (!validate_non_empty(z)) return;

  Array talus_map(z.shape, talus);
  thermal_ridge(z, talus_map, iterations, p_deposition_map);
}

void thermal_scree(Array       &z,
                   const Array &talus,
                   const Array &zmax,
                   int          iterations,
                   Array       *p_deposition_map)
{
  if (!validate_non_empty(z) || !validate_same_shape(z, talus)) return;
  if (!validate_same_shape(z, zmax)) return;

  Array z_bckp = Array();
  if (p_deposition_map != nullptr) z_bckp = z;

  Array z_out(z.shape);

  for (int it = 0; it < iterations; it++)
  {
    freeze_border_1(z, z_out);

    for (int j = 1; j < z.shape.y - 1; j++)
      for (int i = 1; i < z.shape.x - 1; i++)
      {
        const float val = z(i, j);
        float       sum = 0.f;
        float       slope_max = 0.f;

        for (int k = 0; k < 8; k++)
        {
          const float dz = (val - z(i + DI[k], j + DJ[k])) / CD[k];

          if (dz < 0.f) sum += dz;

          slope_max = std::max(slope_max, std::fabs(dz));
        }

        const float t = talus(i, j);

        float amp = clamp01(1.f - t / slope_max);
        amp = smoothstep3(amp);

        amp *= almost_unit_identity(clamp01(1.f - val / zmax(i, j)));

        z_out(i, j) = val + 0.25f * (t - 0.5f * sum) * amp;
      }

    std::swap(z.vector, z_out.vector);
  }

  extrapolate_borders(z);

  if (p_deposition_map) *p_deposition_map = maximum(z - z_bckp, 0.f);
}

void thermal_scree(Array       &z,
                   const Array *p_mask,
                   const Array &talus,
                   const Array &zmax,
                   int          iterations,
                   Array       *p_deposition_map)
{
  apply_with_mask(
      z,
      p_mask,
      [&](Array &a)
      { thermal_scree(a, talus, zmax, iterations, p_deposition_map); });
}

void thermal_scree(Array       &z,
                   float        talus,
                   const Array &zmax,
                   int          iterations,
                   Array       *p_deposition_map)
{
  if (!validate_non_empty(z)) return;
  if (!validate_same_shape(z, zmax)) return;

  Array talus_map(z.shape, talus);
  thermal_scree(z, talus_map, zmax, iterations, p_deposition_map);
}

} // namespace hmap
