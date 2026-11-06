/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */

/* highmap-wasm CPU ports of the hydraulic erosion family.
 *
 * Each function below transcribes the matching OpenCL kernel from
 * src/gpu_opencl/kernels/ (hydraulic_mcdonald.cl, hydraulic_schott.cl,
 * hydraulic_vpipes.cl) and reproduces the launch behaviour of the matching
 * src/erosion/hydraulic_*_gpu.cpp wrapper: constants, traversal order,
 * float32 accumulation, clamp-to-edge sampling and output-map assignment. Do
 * not "clean up" the arithmetic - the parity harness
 * (tests/src/test_hydraulic_cpu.cpp) compares this code against the GPU
 * kernels on the desktop build.
 *
 * The only intentional deviation from the GPU code is McDonald's atomic adds:
 * the CPU port processes samples in a fixed index order, which makes the
 * result deterministic. The GPU's atomic scheduling order is not reproducible.
 *
 * Rebase rule: if upstream ever ships CPU implementations, delete this file and
 * the header. See AD-022 in docs/decisions.md for the placement and the
 * recorded upstream wrapper gaps.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "highmap/array.hpp"
#include "highmap/erosion_hydraulic_cpu.hpp"
#include "highmap/internal/validation.hpp"
#include "highmap/math/array.hpp"

namespace hmap
{

namespace
{

// --- shared kernel helpers --------------------------------------------------

// _common_rand.cl: hash21u (unsigned 32-bit wraparound is intentional)
inline std::uint32_t hash21u(std::uint32_t x, std::uint32_t y)
{
  std::uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u;
  h ^= h >> 13;
  h *= 0x85ebca6bu;
  h ^= h >> 16;
  return h;
}

// hydraulic_mcdonald.cl: one deterministic uniform in [0,1) per
// (seed, step, sample, salt)
inline float mcd_uniform(std::uint32_t seed,
                         std::uint32_t step,
                         std::uint32_t n,
                         std::uint32_t salt)
{
  return (float)(hash21u(n ^ (step * 0x9e3779b9u), seed + salt) >> 8) /
         16777216.f;
}

// clamp-to-edge image sampling: OpenCL's CLK_ADDRESS_CLAMP_TO_EDGE
inline int clamp_index(int i, int n)
{
  return std::max(0, std::min(i, n - 1));
}

inline float sample_clamp(const Array &a, int i, int j)
{
  return a(clamp_index(i, a.shape.x), clamp_index(j, a.shape.y));
}

// OpenCL CLK_FILTER_LINEAR with CLK_ADDRESS_CLAMP_TO_EDGE. OpenCL image
// coordinates place the center of texel (i, j) at (i + 0.5, j + 0.5).
inline float bilinear_sample_clamp(const Array &a, float x, float y)
{
  const float fx = x - 0.5f;
  const float fy = y - 0.5f;

  const int i0 = (int)std::floor(fx);
  const int j0 = (int)std::floor(fy);
  const float u = fx - (float)i0;
  const float v = fy - (float)j0;

  const float v00 = sample_clamp(a, i0, j0);
  const float v10 = sample_clamp(a, i0 + 1, j0);
  const float v01 = sample_clamp(a, i0, j0 + 1);
  const float v11 = sample_clamp(a, i0 + 1, j0 + 1);

  return (1.f - u) * (1.f - v) * v00 + u * (1.f - v) * v10 +
         (1.f - u) * v * v01 + u * v * v11;
}

// _common_math.cl: mix(x, y, a) = x + (y - x) * a
inline float mix(float a, float b, float t) { return a + t * (b - a); }

// _common_math.cl: pow_float(base, x) = exp(x * log(base))
inline float pow_float(float base, float x)
{
  return std::exp(x * std::log(base));
}

// --- Schott image-sampling helpers -----------------------------------------

inline float schott_get_slope(const Array &z, int px, int py, int qx, int qy)
{
  const float d = std::sqrt((float)((px - qx) * (px - qx) +
                                    (py - qy) * (py - qy)));
  return (sample_clamp(z, px, py) - sample_clamp(z, qx, qy)) / d;
}

inline float find_steepest_downslope_neighbor(const Array &z,
                                              int          px,
                                              int          py,
                                              int         &qx,
                                              int         &qy)
{
  float slope_max = 0.f;
  qx = px;
  qy = py;

  for (int r = -1; r <= 1; r++)
    for (int s = -1; s <= 1; s++)
      if (!(r == 0 && s == 0))
      {
        const int   pnx = px + r;
        const int   pny = py + s;
        const float slope = schott_get_slope(z, px, py, pnx, pny);

        if (slope > slope_max)
        {
          slope_max = slope;
          qx = pnx;
          qy = pny;
        }
      }

  return slope_max;
}

inline float schott_get_weight(const Array &z,
                               int          px,
                               int          py,
                               int          qx,
                               int          qy,
                               float        flow_routing_exponent)
{
  float weight = 0.f;
  float total_weight = 0.f;

  for (int r = -1; r <= 1; r++)
    for (int s = -1; s <= 1; s++)
      if (!(r == 0 && s == 0))
      {
        const int   pnx = px + r;
        const int   pny = py + s;
        const float slope = schott_get_slope(z, px, py, pnx, pny);

        if (slope > 0.f)
        {
          const float sp = pow_float(slope, flow_routing_exponent);
          total_weight += sp;

          if (pnx == qx && pny == qy) weight = sp;
        }
      }

  if (total_weight == 0.f) return 0.f;
  return weight / total_weight;
}

inline float schott_get_flow(const Array &z,
                             const Array &field,
                             int          px,
                             int          py,
                             float        flow_routing_exponent)
{
  float field_out = 0.f;

  for (int r = -1; r <= 1; r++)
    for (int s = -1; s <= 1; s++)
      if (!(r == 0 && s == 0))
      {
        const int qx = px + r;
        const int qy = py + s;

        if (sample_clamp(z, px, py) <= sample_clamp(z, qx, qy))
        {
          const float weight = schott_get_weight(z,
                                                 qx,
                                                 qy,
                                                 px,
                                                 py,
                                                 flow_routing_exponent);
          field_out += sample_clamp(field, qx, qy) * weight;
        }
      }

  return field_out;
}

// --- McDonald helpers -------------------------------------------------------

struct McdonaldParams
{
  int           steps;
  std::uint32_t seed;
  float         world_extent_km;
  float         z_scale_km;
  int           samples;
  int           maxage;
  float         lrate;
  float         time_step;
  float         rainfall;
  float         evap_rate;
  float         gravity;
  float         viscosity;
  float         bed_shear;
  float         crit_slope;
  float         settle_rate;
  float         thermal_rate;
  float         deposition_rate;
  float         suspension_rate;
  float         exit_slope;
};

// hydraulic_mcdonald.cl: mcd_grad5, a 5-point one-sided-aware gradient of
// (bed + sed), scaled by z_m / cell_m.
inline void mcd_grad5(const Array &bed,
                      const Array &sed,
                      int          px,
                      int          py,
                      float        zs,
                      float       &gx,
                      float       &gy)
{
  const int nx = bed.shape.x;
  const int ny = bed.shape.y;

  float vx[5];
  int   ox[5];
  float vy[5];
  int   oy[5];

  for (int k = 0; k < 5; ++k)
  {
    const int xi = px - 2 + k;
    const int yj = py - 2 + k;

    ox[k] = (xi < 0 || xi >= nx || py < 0 || py >= ny) ? 1 : 0;
    if (!ox[k])
    {
      const int idx = py * nx + xi;
      vx[k] = bed.vector[idx] + sed.vector[idx];
    }
    else
      vx[k] = 0.f;

    oy[k] = (yj < 0 || yj >= ny || px < 0 || px >= nx) ? 1 : 0;
    if (!oy[k])
    {
      const int idx = yj * nx + px;
      vy[k] = bed.vector[idx] + sed.vector[idx];
    }
    else
      vy[k] = 0.f;
  }

  gx = 0.f;
  if (!ox[0] && !ox[4])
    gx = (1.f * vx[0] - 8.f * vx[1] + 8.f * vx[3] - 1.f * vx[4]) / 12.f;
  else if (!ox[0] && !ox[3])
    gx = (1.f * vx[0] - 6.f * vx[1] + 3.f * vx[2] + 2.f * vx[3]) / 6.f;
  else if (!ox[0] && !ox[2])
    gx = (1.f * vx[0] - 4.f * vx[1] + 3.f * vx[2]) / 2.f;
  else if (!ox[1] && !ox[4])
    gx = (-2.f * vx[1] - 3.f * vx[2] + 6.f * vx[3] - 1.f * vx[4]) / 6.f;
  else if (!ox[2] && !ox[4])
    gx = (-3.f * vx[2] + 4.f * vx[3] - 1.f * vx[4]) / 2.f;
  else if (!ox[1] && !ox[3])
    gx = (-1.f * vx[1] + 1.f * vx[3]) / 2.f;
  else if (!ox[2] && !ox[3])
    gx = (-1.f * vx[2] + 1.f * vx[3]) / 1.f;
  else if (!ox[1] && !ox[2])
    gx = (-1.f * vx[1] + 1.f * vx[2]) / 1.f;

  gy = 0.f;
  if (!oy[0] && !oy[4])
    gy = (1.f * vy[0] - 8.f * vy[1] + 8.f * vy[3] - 1.f * vy[4]) / 12.f;
  else if (!oy[0] && !oy[3])
    gy = (1.f * vy[0] - 6.f * vy[1] + 3.f * vy[2] + 2.f * vy[3]) / 6.f;
  else if (!oy[0] && !oy[2])
    gy = (1.f * vy[0] - 4.f * vy[1] + 3.f * vy[2]) / 2.f;
  else if (!oy[1] && !oy[4])
    gy = (-2.f * vy[1] - 3.f * vy[2] + 6.f * vy[3] - 1.f * vy[4]) / 6.f;
  else if (!oy[2] && !oy[4])
    gy = (-3.f * vy[2] + 4.f * vy[3] - 1.f * vy[4]) / 2.f;
  else if (!oy[1] && !oy[3])
    gy = (-1.f * vy[1] + 1.f * vy[3]) / 2.f;
  else if (!oy[2] && !oy[3])
    gy = (-1.f * vy[2] + 1.f * vy[3]) / 1.f;
  else if (!oy[1] && !oy[2])
    gy = (-1.f * vy[1] + 1.f * vy[2]) / 1.f;

  gx *= zs;
  gy *= zs;
}

inline void normalize3(float x, float y, float z, float &nx, float &ny)
{
  const float len = std::sqrt(x * x + y * y + z * z);
  nx = x / len;
  ny = y / len;
}

// hydraulic_mcdonald.cl: mcdonald_solve, one sample. Atomic adds become direct
// updates in sample index order; this is the port's only intentional deviation.
inline void mcdonald_solve_sample(Array                &bed,
                                  Array                &sed,
                                  Array                &dis,
                                  Array                &mx,
                                  Array                &my,
                                  Array                &tr_d,
                                  Array                &tr_mx,
                                  Array                &tr_my,
                                  int                   n,
                                  std::uint32_t         step,
                                  const McdonaldParams &p,
                                  float                 cell_m,
                                  float                 z_m)
{
  const int   nx = bed.shape.x;
  const int   ny = bed.shape.y;
  const float cl_len = std::sqrt(cell_m * cell_m + cell_m * cell_m);
  const float Ac = cell_m * cell_m;
  const float zs = z_m / cell_m;

  float pos_x = mcd_uniform(p.seed, step, (std::uint32_t)n, 0u) * (float)nx;
  float pos_y = mcd_uniform(p.seed, step, (std::uint32_t)n, 1u) * (float)ny;
  int   ci = (int)pos_x;
  int   cj = (int)pos_y;
  if (ci >= nx || cj >= ny) return;
  int find = cj * nx + ci;

  const float P = 1.f / ((float)nx * (float)ny);
  const float Z = Ac * z_m;
  const float Q = P * (float)p.samples;

  float vol = Ac * p.rainfall;
  float sedm = 0.f;

  float gx, gy;
  mcd_grad5(bed, sed, ci, cj, zs, gx, gy);
  float nx3, ny3;
  normalize3(-gx, -gy, 1.f, nx3, ny3);

  float avg_x = 0.f;
  float avg_y = 0.f;
  if (dis.vector[find] > 0.f)
  {
    avg_x = mx.vector[find] / dis.vector[find];
    avg_y = my.vector[find] / dis.vector[find];
  }

  float speed_x = p.gravity * nx3 + p.viscosity * avg_x;
  float speed_y = p.gravity * ny3 + p.viscosity * avg_y;
  if (std::sqrt(speed_x * speed_x + speed_y * speed_y) == 0.f) return;

  float ds = cl_len / std::sqrt(speed_x * speed_x + speed_y * speed_y);
  float npos_x = pos_x + ds * (speed_x / cell_m);
  float npos_y = pos_y + ds * (speed_y / cell_m);
  float dspeed_x = speed_x;
  float dspeed_y = speed_y;

  const float track_scale = 1.1f / P / (float)p.samples;

  for (int age = 0; age < p.maxage; ++age)
  {
    tr_d.vector[find] += track_scale * vol;
    tr_mx.vector[find] += track_scale * vol * dspeed_x;
    tr_my.vector[find] += track_scale * vol * dspeed_y;

    const float discharge = dis.vector[find];
    float       slope = -p.exit_slope;
    const float h0 = (bed.vector[find] + sed.vector[find]) * z_m;
    float       h1 = h0 + slope * cl_len;

    const int ni = (int)npos_x;
    const int nj = (int)npos_y;
    if (ni >= 0 && ni < nx && nj >= 0 && nj < ny)
    {
      const int nind = nj * nx + ni;
      h1 = (bed.vector[nind] + sed.vector[nind]) * z_m;
      slope = (h1 - h0) / cl_len;
    }

    const float alpha = (slope < 0.f) ? 1.f : 0.f;
    const float suspend = p.time_step * p.suspension_rate * vol * slope * alpha *
                          std::pow(discharge, 0.4f);
    const float deposit = p.time_step * p.deposition_rate * sedm;

    float       transfer = deposit + suspend;
    const float maxtransfer = 0.1f * slope * cl_len / z_m * Z * Q;
    const float tmin = transfer *
                       std::fmin(1.f, std::fabs(maxtransfer / suspend));
    const float tmax = sedm;
    transfer = std::fmin(std::fmax(transfer, tmin), tmax);

    if (transfer > 0.f)
    {
      sed.vector[find] += transfer / Z / Q;
      sedm -= transfer;
    }
    else if (transfer < 0.f)
    {
      const float maxtransfer2 = 0.1f * sed.vector[find] * Z * Q;
      const float t1 = transfer *
                       std::fmin(1.f, std::fabs(maxtransfer2 / transfer));
      sed.vector[find] += t1 / Z / Q;
      sedm -= t1;

      transfer -= t1;
      bed.vector[find] += transfer / Z / Q;
      sedm -= transfer;
    }

    vol = 1.f / (1.f + ds * p.evap_rate) * vol;

    pos_x = npos_x;
    pos_y = npos_y;
    ci = (int)pos_x;
    cj = (int)pos_y;
    if (ci < 0 || ci >= nx || cj < 0 || cj >= ny) break;
    find = cj * nx + ci;

    mcd_grad5(bed, sed, ci, cj, zs, gx, gy);
    normalize3(-gx, -gy, 1.f, nx3, ny3);

    float avg2_x = 0.f;
    float avg2_y = 0.f;
    if (dis.vector[find] > 0.f)
    {
      avg2_x = mx.vector[find] / dis.vector[find];
      avg2_y = my.vector[find] / dis.vector[find];
    }

    speed_x = speed_x + ds * p.gravity * nx3;
    speed_y = speed_y + ds * p.gravity * ny3;

    const float k12 = p.bed_shear + p.viscosity;
    speed_x = 1.f / (1.f + ds * k12) * speed_x +
              ds * p.viscosity / (1.f + ds * k12) * avg2_x;
    speed_y = 1.f / (1.f + ds * k12) * speed_y +
              ds * p.viscosity / (1.f + ds * k12) * avg2_y;
    dspeed_x = 1.f / (1.f + ds * k12) * dspeed_x;
    dspeed_y = 1.f / (1.f + ds * k12) * dspeed_y;

    if (std::sqrt(speed_x * speed_x + speed_y * speed_y) == 0.f) break;

    ds = cl_len / std::sqrt(speed_x * speed_x + speed_y * speed_y);
    npos_x = pos_x + ds * (speed_x / cell_m);
    npos_y = pos_y + ds * (speed_y / cell_m);
  }
}

// hydraulic_mcdonald.cl: mcdonald_debris, one sample (256 ages).
inline void mcdonald_debris_sample(Array                &bed,
                                   Array                &sed,
                                   int                   n,
                                   std::uint32_t         step,
                                   const McdonaldParams &p,
                                   float                 cell_m,
                                   float                 z_m)
{
  const int   nx = bed.shape.x;
  const int   ny = bed.shape.y;
  const float Ac = cell_m * cell_m;
  const float zs = z_m / cell_m;
  const float P = 1.f / ((float)nx * (float)ny);
  const float Q = P * (float)p.samples;

  float mass = 0.f;

  float pos_x = mcd_uniform(p.seed, step, (std::uint32_t)n, 2u) * (float)nx;
  float pos_y = mcd_uniform(p.seed, step, (std::uint32_t)n, 3u) * (float)ny;

  for (int age = 0; age < 256; ++age)
  {
    const int pi = (int)pos_x;
    const int pj = (int)pos_y;
    if (pi < 0 || pi >= nx || pj < 0 || pj >= ny) return;

    float gx, gy;
    mcd_grad5(bed, sed, pi, pj, zs, gx, gy);
    float nx3, ny3;
    normalize3(-gx, -gy, 1.f, nx3, ny3);

    float speed_x = p.gravity * nx3;
    float speed_y = p.gravity * ny3;

    float npos_x = pos_x;
    float npos_y = pos_y;
    const float speed_len = std::sqrt(speed_x * speed_x + speed_y * speed_y);
    if (speed_len > 0.f)
    {
      npos_x = pos_x + speed_x / speed_len;
      npos_y = pos_y + speed_y / speed_len;
    }

    const int qi = (int)npos_x;
    const int qj = (int)npos_y;
    if (qi < 0 || qi >= nx || qj < 0 || qj >= ny) return;

    const int find = pj * nx + pi;
    const int nind = qj * nx + qi;

    const float dx = npos_x - pos_x;
    const float dy = npos_y - pos_y;
    const float dist = std::sqrt(cell_m * cell_m * (dx * dx + dy * dy));
    pos_x = npos_x;
    pos_y = npos_y;

    const float hf = z_m * bed.vector[find] +
                     std::fmax(0.f, z_m * sed.vector[find]);
    const float hf1 = std::fmax(0.f, z_m * sed.vector[find]);
    const float hn = z_m * bed.vector[nind] +
                     std::fmax(0.f, z_m * sed.vector[nind]);

    const float stable1 = hn + p.crit_slope * dist;

    const float deposit = p.time_step * p.settle_rate * mass;
    const float suspend = -p.time_step * p.thermal_rate *
                          std::fmax(0.f, hf - stable1) * Ac;
    float transfer = deposit + suspend;
    if (transfer == 0.f) continue;

    if (transfer > 0.f)
    {
      float maxtransfer = std::fmax(0.f, stable1 - hf) * Ac * Q;
      transfer = std::fmin(transfer, maxtransfer);
      transfer = std::fmin(transfer, mass);
      transfer = std::fmax(0.f, transfer);

      sed.vector[find] += transfer / Q / z_m / Ac;
      mass -= transfer;
    }
    else
    {
      float maxtransfer = std::fmax(0.f, hf - stable1) * Ac * Q;
      transfer = -std::fmin(-transfer, maxtransfer);

      const float maxt1 = hf1 * Ac * Q;
      const float t1 = transfer * std::fmin(1.f, std::fabs(maxt1 / transfer));
      sed.vector[find] += t1 / Q / z_m / Ac;
      mass -= t1;

      transfer -= t1;
      bed.vector[find] += transfer / Q / Ac / z_m;
      mass -= transfer;
    }
  }
}

inline void mcdonald_run_steps(Array                &bed,
                               Array                &sed,
                               Array                &dis,
                               Array                &mx,
                               Array                &my,
                               const McdonaldParams &p)
{
  const int   nx = bed.shape.x;
  const float cell_m = p.world_extent_km * 1e3f / (float)nx;
  const float z_m = p.z_scale_km * 1e3f;

  Array tr_d(bed.shape), tr_mx(bed.shape), tr_my(bed.shape);

  for (int step = 0; step < p.steps; ++step)
  {
    std::fill(tr_d.vector.begin(), tr_d.vector.end(), 0.f);
    std::fill(tr_mx.vector.begin(), tr_mx.vector.end(), 0.f);
    std::fill(tr_my.vector.begin(), tr_my.vector.end(), 0.f);

    for (int n = 0; n < p.samples; ++n)
      mcdonald_solve_sample(bed,
                            sed,
                            dis,
                            mx,
                            my,
                            tr_d,
                            tr_mx,
                            tr_my,
                            n,
                            (std::uint32_t)step,
                            p,
                            cell_m,
                            z_m);

    for (int j = 0; j < bed.shape.y; ++j)
      for (int i = 0; i < bed.shape.x; ++i)
      {
        const int idx = j * bed.shape.x + i;
        dis.vector[idx] = mix(dis.vector[idx], tr_d.vector[idx], p.lrate);
        mx.vector[idx] = mix(mx.vector[idx], tr_mx.vector[idx], p.lrate);
        my.vector[idx] = mix(my.vector[idx], tr_my.vector[idx], p.lrate);
      }

    for (int n = 0; n < p.samples; ++n)
      mcdonald_debris_sample(bed,
                             sed,
                             n,
                             (std::uint32_t)step,
                             p,
                             cell_m,
                             z_m);
  }
}

// --- vpipes pass helpers ---------------------------------------------------

// hydraulic_vpipes.cl: hydraulic_vpipes_flow_pass with outflow_boundaries = 0.
inline void vpipes_flow_pass(const Array &z,
                             const Array &d1,
                             const Array &fl,
                             const Array &fr,
                             const Array &ft,
                             const Array &fb,
                             Array       &fl_out,
                             Array       &fr_out,
                             Array       &ft_out,
                             Array       &fb_out,
                             int          nx,
                             int          ny,
                             float        dt,
                             bool         flux_diffusion,
                             float        flux_diffusion_strength)
{
  const float plength = 1.f;
  const float gv = 1.f;

  for (int j = 0; j < ny; j++)
    for (int i = 0; i < nx; i++)
    {
      const float d0 = d1(i, j);
      const float fl_val = fl(i, j);
      const float fr_val = fr(i, j);
      const float ft_val = ft(i, j);
      const float fb_val = fb(i, j);

      // fast early-exit: dry cell with no existing flux
      if (d0 <= 0.f && fl_val <= 0.f && fr_val <= 0.f && ft_val <= 0.f &&
          fb_val <= 0.f)
      {
        fl_out(i, j) = 0.f;
        fr_out(i, j) = 0.f;
        ft_out(i, j) = 0.f;
        fb_out(i, j) = 0.f;
        continue;
      }

      const float h0 = z(i, j) + d0;

      const float dhl = h0 - sample_clamp(z, i - 1, j) -
                        sample_clamp(d1, i - 1, j);
      const float dhr = h0 - sample_clamp(z, i + 1, j) -
                        sample_clamp(d1, i + 1, j);
      const float dht = h0 - sample_clamp(z, i, j + 1) -
                        sample_clamp(d1, i, j + 1);
      const float dhb = h0 - sample_clamp(z, i, j - 1) -
                        sample_clamp(d1, i, j - 1);

      float fl_new = std::fmax(0.f, fl_val + dt * gv * dhl / plength);
      float fr_new = std::fmax(0.f, fr_val + dt * gv * dhr / plength);
      float ft_new = std::fmax(0.f, ft_val + dt * gv * dht / plength);
      float fb_new = std::fmax(0.f, fb_val + dt * gv * dhb / plength);

      if (flux_diffusion)
      {
        const float c0 = flux_diffusion_strength;
        const float c1 = 1.f - 4.f * c0;
        const float f_diff = c0 * (fl_new + fr_new + ft_new + fb_new);

        fl_new = c1 * fl_new + f_diff;
        fr_new = c1 * fr_new + f_diff;
        ft_new = c1 * ft_new + f_diff;
        fb_new = c1 * fb_new + f_diff;
      }

      const float sum = fl_new + fr_new + ft_new + fb_new;
      float       k = 0.f;
      if (sum > 1e-5f) k = d0 * plength * plength / (sum * dt);
      k = std::fmin(std::fmax(k, 0.f), 1.f);

      fl_out(i, j) = fl_new * k;
      fr_out(i, j) = fr_new * k;
      ft_out(i, j) = ft_new * k;
      fb_out(i, j) = fb_new * k;
    }
}

// hydraulic_vpipes.cl: hydraulic_vpipes_water_pass with outflow_boundaries = 0.
inline void vpipes_water_pass(const Array &fl,
                              const Array &fr,
                              const Array &ft,
                              const Array &fb,
                              const Array &d1,
                              Array       &d2,
                              Array       &u,
                              Array       &v,
                              int          nx,
                              int          ny,
                              float        dt,
                              float        water_height,
                              float        evap_rate)
{
  const float plength = 1.f;

  for (int j = 0; j < ny; j++)
    for (int i = 0; i < nx; i++)
    {
      const float d0 = d1(i, j);
      const float fl_val = fl(i, j);
      const float fr_val = fr(i, j);
      const float ft_val = ft(i, j);
      const float fb_val = fb(i, j);

      const float in_l = sample_clamp(fr, i - 1, j);
      const float in_r = sample_clamp(fl, i + 1, j);
      const float in_b = sample_clamp(ft, i, j - 1);
      const float in_t = sample_clamp(fb, i, j + 1);

      // fast early-exit: dry cell with no incoming or outgoing flux
      if (d0 <= 0.f && fl_val <= 0.f && fr_val <= 0.f && ft_val <= 0.f &&
          fb_val <= 0.f && in_l <= 0.f && in_r <= 0.f && in_b <= 0.f &&
          in_t <= 0.f)
      {
        d2(i, j) = 0.f;
        u(i, j) = 0.f;
        v(i, j) = 0.f;
        continue;
      }

      const float dv = dt * (in_l + in_b + in_r + in_t - fl_val - fr_val -
                             ft_val - fb_val);
      float d2_new = std::fmax(0.f, d0 + dv / (plength * plength));

      if (evap_rate > 0.f)
        d2_new = std::fmax(0.f, d2_new * (1.f - evap_rate * dt));

      d2(i, j) = d2_new;

      float u_new = 0.5f * (in_l - fl_val + fr_val -
                            sample_clamp(fl, i + 1, j));
      float v_new = 0.5f * (in_b - fb_val + ft_val -
                            sample_clamp(fb, i, j + 1));

      const float dmean = std::fmax(0.001f * water_height, d2_new);
      u_new /= dmean;
      v_new /= dmean;

      u(i, j) = u_new;
      v(i, j) = v_new;
    }
}

// hydraulic_vpipes.cl: hydraulic_vpipes_erosion_pass.
inline void vpipes_erosion_pass(const Array &z,
                                const Array &d2,
                                const Array &u,
                                const Array &v,
                                const Array &s,
                                Array       &z_out,
                                Array       &s_out,
                                int          nx,
                                int          ny,
                                float        water_height,
                                float        k_capacity,
                                float        k_erode,
                                float        k_depose,
                                float        k_discharge_exp,
                                float        downcutting_max_depth_ratio)
{
  const float salpha_min = 0.01f;

  for (int j = 0; j < ny; j++)
    for (int i = 0; i < nx; i++)
    {
      const float dzx = 0.5f * (sample_clamp(z, i + 1, j) -
                                sample_clamp(z, i - 1, j) +
                                sample_clamp(s, i + 1, j) -
                                sample_clamp(s, i - 1, j));
      const float dzy = 0.5f * (sample_clamp(z, i, j + 1) -
                                sample_clamp(z, i, j - 1) +
                                sample_clamp(s, i, j + 1) -
                                sample_clamp(s, i, j - 1));

      const float talus = std::hypot(dzx, dzy);
      const float dzn = std::fmax(1e-3f, (float)nx * talus);
      const float dmax = water_height;

      const float salpha = std::fmax(salpha_min,
                                     dzn / std::hypot(1.f, dzn));
      const float speed = std::hypot(u(i, j), v(i, j));
      const float depth = std::fmin(d2(i, j) / dmax,
                                    downcutting_max_depth_ratio);
      const float discharge = depth * speed;
      const float capa = 1.1f * k_capacity *
                         std::pow(discharge, k_discharge_exp) * salpha;

      const float st = s(i, j);
      const float zt = z(i, j);

      if (capa > st)
      {
        const float amount = k_erode * (capa - st);
        z_out(i, j) = zt - amount;
        s_out(i, j) = st + amount;
      }
      else
      {
        const float amount = k_depose * (st - capa);
        z_out(i, j) = zt + amount;
        s_out(i, j) = st - amount;
      }
    }
}

// hydraulic_vpipes.cl: hydraulic_vpipes_sediment_transport_pass.
inline void vpipes_sediment_transport_pass(const Array &u,
                                           const Array &v,
                                           const Array &s,
                                           Array       &s_out,
                                           int          nx,
                                           int          ny,
                                           float        dt)
{
  for (int j = 0; j < ny; j++)
    for (int i = 0; i < nx; i++)
    {
      const float pos_x = (float)i + 0.5f - dt * u(i, j);
      const float pos_y = (float)j + 0.5f - dt * v(i, j);
      s_out(i, j) = bilinear_sample_clamp(s, pos_x, pos_y);
    }
}

} // namespace

// --- McDonald public entry points ------------------------------------------

void hydraulic_mcdonald(Array        &z,
                        int           steps,
                        std::uint32_t seed,
                        Array        *p_sediment_map,
                        Array        *p_discharge_map,
                        float         world_extent_km,
                        float         z_scale_km,
                        int           samples,
                        int           maxage,
                        float         lrate,
                        float         time_step,
                        float         rainfall,
                        float         evap_rate,
                        float         gravity,
                        float         viscosity,
                        float         bed_shear,
                        float         crit_slope,
                        float         settle_rate,
                        float         thermal_rate,
                        float         deposition_rate,
                        float         suspension_rate,
                        float         exit_slope)
{
  if (!validate_non_empty(z)) return;

  Array sed(z.shape), dis(z.shape), mx(z.shape), my(z.shape);

  const McdonaldParams p{steps,
                         seed,
                         world_extent_km,
                         z_scale_km,
                         samples,
                         maxage,
                         lrate,
                         time_step,
                         rainfall,
                         evap_rate,
                         gravity,
                         viscosity,
                         bed_shear,
                         crit_slope,
                         settle_rate,
                         thermal_rate,
                         deposition_rate,
                         suspension_rate,
                         exit_slope};

  mcdonald_run_steps(z, sed, dis, mx, my, p);

  if (p_sediment_map) *p_sediment_map = sed;
  if (p_discharge_map) *p_discharge_map = dis;

  // total surface = bedrock + sediment
  for (std::size_t k = 0; k < z.vector.size(); ++k)
    z.vector[k] += sed.vector[k];
}

void hydraulic_mcdonald_multiscale(Array                  &z,
                                   std::uint32_t           seed,
                                   const std::vector<int> &steps_per_level,
                                   Array                  *p_sediment_map,
                                   Array                  *p_discharge_map,
                                   float                   world_extent_km,
                                   float                   z_scale_km,
                                   int                     samples,
                                   int                     maxage,
                                   float                   lrate,
                                   float                   time_step,
                                   float                   rainfall,
                                   float                   evap_rate,
                                   float                   gravity,
                                   float                   viscosity,
                                   float                   bed_shear,
                                   float                   crit_slope,
                                   float                   settle_rate,
                                   float                   thermal_rate,
                                   float                   deposition_rate,
                                   float                   suspension_rate,
                                   float                   exit_slope)
{
  if (!validate_non_empty(z)) return;

  const int nlevels = (int)steps_per_level.size();
  if (nlevels == 0) return;

  // halving ladder, coarsest first; final level == z.shape
  std::vector<glm::ivec2> ladder(nlevels);
  for (int i = 0; i < nlevels; ++i)
  {
    const int shift = nlevels - 1 - i;
    ladder[i] = {std::max(2, z.shape.x >> shift),
                 std::max(2, z.shape.y >> shift)};
  }

  Array bed = z.resample_to_shape(ladder[0]);
  Array sed(ladder[0]), dis(ladder[0]), mx(ladder[0]), my(ladder[0]);

  for (int i = 0; i < nlevels; ++i)
  {
    if (i > 0)
    {
      bed = bed.resample_to_shape(ladder[i]);
      sed = sed.resample_to_shape(ladder[i]);
      dis = dis.resample_to_shape(ladder[i]);
      mx = mx.resample_to_shape(ladder[i]);
      my = my.resample_to_shape(ladder[i]);
    }

    const McdonaldParams p{steps_per_level[i],
                           seed + (std::uint32_t)i,
                           world_extent_km,
                           z_scale_km,
                           samples,
                           maxage,
                           lrate,
                           time_step,
                           rainfall,
                           evap_rate,
                           gravity,
                           viscosity,
                           bed_shear,
                           crit_slope,
                           settle_rate,
                           thermal_rate,
                           deposition_rate,
                           suspension_rate,
                           exit_slope};

    mcdonald_run_steps(bed, sed, dis, mx, my, p);
  }

  if (p_sediment_map) *p_sediment_map = sed;
  if (p_discharge_map) *p_discharge_map = dis;

  z = bed;
  for (std::size_t k = 0; k < z.vector.size(); ++k)
    z.vector[k] += sed.vector[k];
}

// --- Schott public entry points --------------------------------------------

void hydraulic_schott(Array       &z,
                      int          iterations,
                      const Array &talus,
                      float        c_erosion,
                      float        c_thermal,
                      float        c_deposition,
                      float        flow_acc_exponent,
                      float        flow_acc_exponent_depo,
                      float        flow_routing_exponent,
                      float        thermal_weight,
                      float        deposition_weight,
                      Array       *p_flow)
{
  if (!validate_non_empty(z) || !validate_same_shape(z, talus)) return;
  if (p_flow && !validate_same_shape(z, *p_flow)) return;

  Array flow = p_flow ? *p_flow : Array(z.shape, 1.f);
  Array sediment(z.shape, 0.f);

  // erosion weight is always 1
  const float sum_weight = 1.f + thermal_weight + deposition_weight;
  const int   erosion_it = (int)(10.f / sum_weight);
  const int   thermal_it = erosion_it +
                           (int)(10.f * thermal_weight / sum_weight);

  Array z_new(z.shape), flow_new(z.shape), sediment_new(z.shape);

  for (int it = 0; it < iterations; it++)
  {
    for (int j = 0; j < z.shape.y; j++)
      for (int i = 0; i < z.shape.x; i++)
      {
        int         qx, qy;
        const float slope_max = find_steepest_downslope_neighbor(z,
                                                                 i,
                                                                 j,
                                                                 qx,
                                                                 qy);
        const float speed = std::fmin(std::fmax(slope_max * slope_max, 0.f),
                                      1.f);

        float z_val = z(i, j);

        if (it % 10 < erosion_it)
        {
          // --- hydraulic erosion
          const float flow_val = sample_clamp(flow, i, j);
          const float spe = c_erosion *
                            std::fmin(3.f,
                                      pow_float(flow_val, flow_acc_exponent) *
                                          speed);
          const float z_steepest = sample_clamp(z, qx, qy);
          z_val = std::fmax(z_steepest, z_val - 0.5f * spe);
        }
        else if (it % 10 < thermal_it)
        {
          // --- thermal erosion
          const float talus_val = talus(i, j);
          int         up = 0;
          int         down = 0;

          for (int r = -1; r <= 1; r++)
            for (int s = -1; s <= 1; s++)
              if (!(r == 0 && s == 0))
              {
                const float slope = schott_get_slope(z, i, j, i + r, j + s);

                if (slope > talus_val)
                  down++;
                else if (slope < -talus_val)
                  up++;
              }

          z_val += c_thermal * (float)(up - down) * talus_val;
        }
        else
        {
          // --- deposition
          const float flow_val = sample_clamp(flow, i, j);
          const float spe = pow_float(flow_val, flow_acc_exponent_depo) *
                            speed;
          float sed = schott_get_flow(z,
                                      sediment,
                                      i,
                                      j,
                                      flow_routing_exponent);

          if (sed > spe)
          {
            const float deposit = std::fmin(sed,
                                            c_deposition * (sed - spe));
            z_val += deposit;
            sed -= deposit;
          }

          sed += c_deposition * spe;
          sediment_new(i, j) = sed;
        }

        z_new(i, j) = z_val;
        flow_new(i, j) = 1.f +
                         schott_get_flow(z,
                                         flow,
                                         i,
                                         j,
                                         flow_routing_exponent);

        // The kernel only writes sediment_new in the deposition phase; keep
        // the current sediment untouched in the other phases.
        if (it % 10 < thermal_it) sediment_new(i, j) = sediment(i, j);
      }

    z = z_new;
    flow = flow_new;
    sediment = sediment_new;
  }

  if (p_flow) *p_flow = flow;
}

void hydraulic_schott(Array       &z,
                      int          iterations,
                      const Array &talus,
                      const Array *p_mask,
                      float        c_erosion,
                      float        c_thermal,
                      float        c_deposition,
                      float        flow_acc_exponent,
                      float        flow_acc_exponent_depo,
                      float        flow_routing_exponent,
                      float        thermal_weight,
                      float        deposition_weight,
                      Array       *p_flow)
{
  if (!validate_non_empty(z) || !validate_same_shape(z, talus)) return;
  if (p_mask && !validate_same_shape(z, *p_mask)) return;
  if (p_flow && !validate_same_shape(z, *p_flow)) return;

  if (!p_mask)
    hydraulic_schott(z,
                     iterations,
                     talus,
                     c_erosion,
                     c_thermal,
                     c_deposition,
                     flow_acc_exponent,
                     flow_acc_exponent_depo,
                     flow_routing_exponent,
                     thermal_weight,
                     deposition_weight,
                     p_flow);
  else
  {
    Array z_f = z;
    hydraulic_schott(z_f,
                     iterations,
                     talus,
                     c_erosion,
                     c_thermal,
                     c_deposition,
                     flow_acc_exponent,
                     flow_acc_exponent_depo,
                     flow_routing_exponent,
                     thermal_weight,
                     deposition_weight,
                     p_flow);
    z = lerp(z, z_f, *(p_mask));
  }
}

void hydraulic_schott_erosion(Array       &z,
                              int          iterations,
                              float        c_erosion,
                              float        flow_acc_exponent,
                              float        flow_routing_exponent,
                              const Array *p_moisture_map,
                              Array       *p_flow)
{
  if (!validate_non_empty(z)) return;
  if (p_moisture_map && !validate_same_shape(z, *p_moisture_map)) return;
  if (p_flow && !validate_same_shape(z, *p_flow)) return;

  Array flow = p_flow ? *p_flow : Array(z.shape, 1.f);
  Array moisture_map = p_moisture_map ? *p_moisture_map
                                      : Array(z.shape, 1.f);

  Array z_new(z.shape), flow_new(z.shape);

  for (int it = 0; it < iterations; it++)
  {
    for (int j = 0; j < z.shape.y; j++)
      for (int i = 0; i < z.shape.x; i++)
      {
        int         qx, qy;
        const float slope_max = find_steepest_downslope_neighbor(z,
                                                                 i,
                                                                 j,
                                                                 qx,
                                                                 qy);
        const float speed = std::fmin(std::fmax(slope_max * slope_max, 0.f),
                                      1.f);
        const float z_val = z(i, j);
        const float spe = c_erosion *
                          std::fmin(3.f,
                                    pow_float(flow(i, j), flow_acc_exponent) *
                                        speed);
        const float z_steepest = sample_clamp(z, qx, qy);

        z_new(i, j) = std::fmax(z_steepest, z_val - 0.5f * spe);
        flow_new(i, j) = moisture_map(i, j) +
                         schott_get_flow(z,
                                         flow,
                                         i,
                                         j,
                                         flow_routing_exponent);
      }

    z = z_new;
    flow = flow_new;
  }

  if (p_flow) *p_flow = flow;
}

// --- vpipes public entry point ---------------------------------------------

void hydraulic_vpipes(Array       &z,
                      float        water_height,
                      bool         maintain_water_volume,
                      float        evap_rate,
                      int          iterations,
                      float        dt,
                      float        k_capacity,
                      float        k_erode,
                      float        k_depose,
                      float        k_discharge_exp,
                      float        downcutting_max_depth_ratio,
                      bool         flux_diffusion,
                      float        flux_diffusion_strength,
                      const Array *p_rain_map,
                      Array       *p_water_depth,
                      Array       *p_sediment,
                      Array       *p_vel_u,
                      Array       *p_vel_v)
{
  if (!validate_non_empty(z)) return;
  if (p_rain_map && !validate_same_shape(z, *p_rain_map)) return;

  const glm::ivec2 shape = z.shape;

  Array rain_map(shape, 1.f);
  if (p_rain_map) rain_map = *p_rain_map;

  Array d(shape, water_height);
  Array d1(shape), d2(shape);
  Array s(shape);

  Array fl(shape), fr(shape), ft(shape), fb(shape);
  Array fl_new(shape), fr_new(shape), ft_new(shape), fb_new(shape);

  Array u(shape), v(shape);
  Array z_new(shape), s_new(shape), s_transport(shape);

  for (std::size_t k = 0; k < d.vector.size(); ++k)
    d.vector[k] *= rain_map.vector[k];

  const float water_volume_init = d.sum();
  const float rain_map_volume = rain_map.sum();

  for (int it = 0; it < iterations; ++it)
  {
    if (maintain_water_volume)
    {
      const float water_volume = d.sum();
      const float rain_rate = (water_volume_init - water_volume) /
                              rain_map_volume;

      for (std::size_t k = 0; k < d.vector.size(); ++k)
        d.vector[k] += rain_rate * rain_map.vector[k];
    }

    d1 = d;

    vpipes_flow_pass(z,
                     d1,
                     fl,
                     fr,
                     ft,
                     fb,
                     fl_new,
                     fr_new,
                     ft_new,
                     fb_new,
                     shape.x,
                     shape.y,
                     dt,
                     flux_diffusion,
                     flux_diffusion_strength);
    std::swap(fl, fl_new);
    std::swap(fr, fr_new);
    std::swap(ft, ft_new);
    std::swap(fb, fb_new);

    vpipes_water_pass(fl,
                      fr,
                      ft,
                      fb,
                      d1,
                      d2,
                      u,
                      v,
                      shape.x,
                      shape.y,
                      dt,
                      water_height,
                      evap_rate);

    vpipes_erosion_pass(z,
                        d2,
                        u,
                        v,
                        s,
                        z_new,
                        s_new,
                        shape.x,
                        shape.y,
                        water_height,
                        k_capacity,
                        k_erode,
                        k_depose,
                        k_discharge_exp,
                        downcutting_max_depth_ratio);
    std::swap(z, z_new);
    std::swap(s, s_new);

    vpipes_sediment_transport_pass(u, v, s, s_transport, shape.x, shape.y, dt);
    std::swap(s, s_transport);

    d = d2;
  }

  if (p_water_depth) *p_water_depth = d;
  if (p_sediment) *p_sediment = s;
  if (p_vel_u) *p_vel_u = u;
  if (p_vel_v) *p_vel_v = v;
}

} // namespace hmap
