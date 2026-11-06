/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */

/* GPU-versus-CPU parity harness for the hydraulic family
 * (add-hydraulic-cpu-ports).
 *
 * The CPU family (src/erosion/hydraulic_cpu.cpp) transcribes the OpenCL
 * kernels; this file runs both implementations on fixed scenes and reports
 * per-variant error metrics. It is the oracle that decides whether "parity"
 * may be claimed.
 *
 * Suites:
 *   HydraulicCpuInvariants - CPU-only properties (determinism, output maps,
 *                            zero-iteration no-ops, mask blends). Run
 *                            everywhere, no OpenCL needed.
 *   HydraulicParity        - GPU-vs-CPU comparison. Skips with GTEST_SKIP()
 *                            when no OpenCL device or no usable float image
 *                            format is available; the erosion-parity CI job
 *                            treats an unexpected skip as a failure.
 *
 * Every parity case prints one line to stdout:
 *   PARITY,<variant>,<scene>,<iterations>,<max_abs>,<rms_abs>,<range>
 * which the CI job turns into the job summary and the uploaded CSV.
 */

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "cl_wrapper/kernel_manager.hpp"
#include "cl_wrapper/run.hpp"

#include "highmap.hpp"
#include "highmap/erosion_hydraulic_cpu.hpp"
#include "highmap/math/array.hpp"

using namespace hmap;

namespace
{

// --- scenes ---------------------------------------------------------------

Array scene_ramp(int n = 32)
{
  Array z(glm::ivec2(n, n));

  for (int j = 0; j < n; j++)
    for (int i = 0; i < n; i++)
      z(i, j) = 0.2f + 0.6f * (float)i / (float)(n - 1) +
                0.1f * (float)j / (float)(n - 1);

  return z;
}

Array scene_fbm(int n = 32)
{
  Array z = noise_fbm(NoiseType::PERLIN, {n, n}, {4.f, 4.f}, 11);
  remap(z);
  return z;
}

struct Scene
{
  const char *name;
  Array (*build)();
};

std::vector<Scene> all_scenes()
{
  return {{"fbm", []() { return scene_fbm(32); }},
          {"ramp", []() { return scene_ramp(32); }}};
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

// --- metrics --------------------------------------------------------------

struct Metrics
{
  float max_abs;
  float rms_abs;
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

double array_sum(const Array &a)
{
  double s = 0.0;
  for (float v : a.vector) s += (double)v;
  return s;
}

float mean_abs_diff(const Array &a, const Array &b)
{
  double s = 0.0;
  for (std::size_t k = 0; k < a.vector.size(); k++)
    s += (double)std::fabs(a.vector[k] - b.vector[k]);
  return (float)(s / (double)a.vector.size());
}

std::string fmt(double v)
{
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.9g", v);
  return std::string(buf);
}

void print_csv_header_once()
{
  static bool done = false;
  if (done) return;
  done = true;
  std::printf("PARITY,variant,scene,iterations,max_abs,rms_abs,range\n");
}

void print_agg_header_once()
{
  static bool done = false;
  if (done) return;
  done = true;
  std::printf(
      "PARITY_AGG,variant,scene,iterations,rel_sediment,rel_discharge,mean_dz\n");
}

// Continuous passes: only float rounding separates the two implementations.
constexpr float CONTINUOUS_MAX_RATIO = 1e-5f;
constexpr float CONTINUOUS_RMS_RATIO = 2e-6f;

// Selection-based passes (argmax/argmin flips on near-ties) get a wider
// single-cell bound but a mean-error bound.
constexpr float SELECTION_MAX_RATIO = 2e-2f;
constexpr float SELECTION_RMS_RATIO = 2e-4f;

void expect_parity(const char *variant,
                   const char *scene,
                   int         iterations,
                   const Array &cpu,
                   const Array &gpu,
                   float        range,
                   float        max_ratio,
                   float        rms_ratio)
{
  const Metrics m = diff_metrics(cpu, gpu);

  const float max_bound = max_ratio * range;
  const float rms_bound = rms_ratio * range;

  print_csv_header_once();
  std::printf("PARITY,%s,%s,%d,%.9g,%.9g,%.9g\n",
              variant,
              scene,
              iterations,
              (double)m.max_abs,
              (double)m.rms_abs,
              (double)range);
  std::fflush(stdout);

  const std::string key = std::string(variant) + "." + scene + "." +
                          std::to_string(iterations);
  testing::Test::RecordProperty(key + ".max_abs", fmt(m.max_abs));
  testing::Test::RecordProperty(key + ".rms_abs", fmt(m.rms_abs));
  testing::Test::RecordProperty(key + ".range", fmt(range));

  EXPECT_LE(m.max_abs, max_bound)
      << variant << "/" << scene << "/" << iterations
      << " iterations: max|cpu-gpu| = " << m.max_abs << " > " << max_bound;
  EXPECT_LE(m.rms_abs, rms_bound)
      << variant << "/" << scene << "/" << iterations
      << " iterations: rms|cpu-gpu| = " << m.rms_abs << " > " << rms_bound;
}

// --- direct-kernel oracle helpers -----------------------------------------

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

void read_image(const cl::Image2D       &img,
                Array                   &a,
                const FloatImageFormat  &fmt,
                const cl::CommandQueue  &queue)
{
  const std::size_t n = (std::size_t)a.shape.x * (std::size_t)a.shape.y;
  std::vector<float> host(n * (std::size_t)fmt.channels);

  std::size_t origin[3] = {0, 0, 0};
  std::size_t region[3] = {(std::size_t)a.shape.x,
                           (std::size_t)a.shape.y,
                           1};

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

// Test-local reference for hydraulic_schott: mirrors the wrapper but passes the
// kernel's `it` argument, which the pinned wrapper never binds.
Array gpu_schott_reference(const Array             &z0,
                           int                      iterations,
                           const Array             &talus,
                           float                    c_erosion,
                           float                    c_thermal,
                           float                    c_deposition,
                           float                    flow_acc_exponent,
                           float                    flow_acc_exponent_depo,
                           float                    flow_routing_exponent,
                           float                    thermal_weight,
                           float                    deposition_weight,
                           const FloatImageFormat  &fmt)
{
  const int nx = z0.shape.x;
  const int ny = z0.shape.y;

  Array z = z0;
  Array flow(z.shape, 1.f);
  Array sediment(z.shape, 0.f);
  Array z_new(z.shape), flow_new(z.shape), sediment_new(z.shape);

  const float sum_weight = 1.f + thermal_weight + deposition_weight;
  const int   erosion_it = (int)(10.f / sum_weight);
  const int   thermal_it = erosion_it +
                           (int)(10.f * thermal_weight / sum_weight);

  clwrapper::Run     run("hydraulic_schott");
  const cl::Image2D talus_img = make_input_image(talus, fmt);

  for (int it = 0; it < iterations; ++it)
  {
    const cl::Image2D z_img = make_input_image(z, fmt);
    const cl::Image2D flow_img = make_input_image(flow, fmt);
    const cl::Image2D sed_img = make_input_image(sediment, fmt);
    const cl::Image2D z_out = make_output_image(z.shape, fmt);
    const cl::Image2D flow_out = make_output_image(flow.shape, fmt);
    const cl::Image2D sed_out = make_output_image(sediment.shape, fmt);

    set_image(run, 0, z_img);
    set_image(run, 1, flow_img);
    set_image(run, 2, sed_img);
    set_image(run, 3, talus_img);
    set_image(run, 4, z_out);
    set_image(run, 5, flow_out);
    set_image(run, 6, sed_out);
    run.set_argument(7, nx);
    run.set_argument(8, ny);
    run.set_argument(9, c_erosion);
    run.set_argument(10, c_thermal);
    run.set_argument(11, c_deposition);
    run.set_argument(12, flow_acc_exponent);
    run.set_argument(13, flow_acc_exponent_depo);
    run.set_argument(14, flow_routing_exponent);
    run.set_argument(15, erosion_it);
    run.set_argument(16, thermal_it);
    run.set_argument(17, it);

    run.execute({nx, ny});

    const cl::CommandQueue queue = run.get_queue();
    read_image(z_out, z_new, fmt, queue);
    read_image(flow_out, flow_new, fmt, queue);

    z = z_new;
    flow = flow_new;

    if (it % 10 >= thermal_it)
    {
      read_image(sed_out, sediment_new, fmt, queue);
      sediment = sediment_new;
    }
  }

  return z;
}

// Test-local reference for hydraulic_vpipes: the fixed dev-branch pass order,
// because the pinned wrapper names kernels that no longer exist.
Array gpu_vpipes_reference(const Array            &z0,
                           float                   water_height,
                           bool                    maintain_water_volume,
                           float                   evap_rate,
                           int                     iterations,
                           float                   dt,
                           float                   k_capacity,
                           float                   k_erode,
                           float                   k_depose,
                           float                   k_discharge_exp,
                           float                   downcutting_max_depth_ratio,
                           bool                    flux_diffusion,
                           float                   flux_diffusion_strength,
                           const FloatImageFormat &fmt,
                           Array                  *p_water_depth = nullptr,
                           Array                  *p_sediment = nullptr,
                           Array                  *p_vel_u = nullptr,
                           Array                  *p_vel_v = nullptr)
{
  const int nx = z0.shape.x;
  const int ny = z0.shape.y;

  Array z = z0;
  Array d(z.shape, water_height);
  Array d1(z.shape), d2(z.shape);
  Array s(z.shape);
  Array fl(z.shape), fr(z.shape), ft(z.shape), fb(z.shape);
  Array u(z.shape), v(z.shape);
  Array fl_new(z.shape), fr_new(z.shape), ft_new(z.shape), fb_new(z.shape);
  Array z_new(z.shape), s_new(z.shape), s_transport(z.shape);

  const float water_volume_init = d.sum();
  const float rain_map_volume = (float)nx * (float)ny;

  clwrapper::Run run_flow("hydraulic_vpipes_flow_pass");
  clwrapper::Run run_water("hydraulic_vpipes_water_pass");
  clwrapper::Run run_erosion("hydraulic_vpipes_erosion_pass");
  clwrapper::Run run_transport("hydraulic_vpipes_sediment_transport_pass");

  for (int it = 0; it < iterations; ++it)
  {
    if (maintain_water_volume)
    {
      const float water_volume = d.sum();
      const float rain_rate = (water_volume_init - water_volume) /
                              rain_map_volume;

      for (std::size_t k = 0; k < d.vector.size(); ++k)
        d.vector[k] += rain_rate;
    }

    d1 = d;

    {
      const cl::Image2D z_img = make_input_image(z, fmt);
      const cl::Image2D fl_img = make_input_image(fl, fmt);
      const cl::Image2D fr_img = make_input_image(fr, fmt);
      const cl::Image2D ft_img = make_input_image(ft, fmt);
      const cl::Image2D fb_img = make_input_image(fb, fmt);
      const cl::Image2D d1_img = make_input_image(d1, fmt);
      const cl::Image2D fl_out = make_output_image(fl.shape, fmt);
      const cl::Image2D fr_out = make_output_image(fr.shape, fmt);
      const cl::Image2D ft_out = make_output_image(ft.shape, fmt);
      const cl::Image2D fb_out = make_output_image(fb.shape, fmt);

      set_image(run_flow, 0, z_img);
      set_image(run_flow, 1, fl_img);
      set_image(run_flow, 2, fr_img);
      set_image(run_flow, 3, ft_img);
      set_image(run_flow, 4, fb_img);
      set_image(run_flow, 5, d1_img);
      set_image(run_flow, 6, fl_out);
      set_image(run_flow, 7, fr_out);
      set_image(run_flow, 8, ft_out);
      set_image(run_flow, 9, fb_out);
      run_flow.set_argument(10, nx);
      run_flow.set_argument(11, ny);
      run_flow.set_argument(12, dt);
      run_flow.set_argument(13, flux_diffusion ? 1 : 0);
      run_flow.set_argument(14, flux_diffusion_strength);
      run_flow.set_argument(15, 0);

      run_flow.execute({nx, ny});

      const cl::CommandQueue queue = run_flow.get_queue();
      read_image(fl_out, fl_new, fmt, queue);
      read_image(fr_out, fr_new, fmt, queue);
      read_image(ft_out, ft_new, fmt, queue);
      read_image(fb_out, fb_new, fmt, queue);

      std::swap(fl, fl_new);
      std::swap(fr, fr_new);
      std::swap(ft, ft_new);
      std::swap(fb, fb_new);
    }

    {
      const cl::Image2D z_img = make_input_image(z, fmt);
      const cl::Image2D fl_img = make_input_image(fl, fmt);
      const cl::Image2D fr_img = make_input_image(fr, fmt);
      const cl::Image2D ft_img = make_input_image(ft, fmt);
      const cl::Image2D fb_img = make_input_image(fb, fmt);
      const cl::Image2D d1_img = make_input_image(d1, fmt);
      const cl::Image2D d2_out = make_output_image(d2.shape, fmt);
      const cl::Image2D u_out = make_output_image(u.shape, fmt);
      const cl::Image2D v_out = make_output_image(v.shape, fmt);

      set_image(run_water, 0, z_img);
      set_image(run_water, 1, fl_img);
      set_image(run_water, 2, fr_img);
      set_image(run_water, 3, ft_img);
      set_image(run_water, 4, fb_img);
      set_image(run_water, 5, d1_img);
      set_image(run_water, 6, d2_out);
      set_image(run_water, 7, u_out);
      set_image(run_water, 8, v_out);
      run_water.set_argument(9, nx);
      run_water.set_argument(10, ny);
      run_water.set_argument(11, dt);
      run_water.set_argument(12, water_height);
      run_water.set_argument(13, evap_rate);
      run_water.set_argument(14, 0);

      run_water.execute({nx, ny});

      const cl::CommandQueue queue = run_water.get_queue();
      read_image(d2_out, d2, fmt, queue);
      read_image(u_out, u, fmt, queue);
      read_image(v_out, v, fmt, queue);
    }

    {
      const cl::Image2D z_img = make_input_image(z, fmt);
      const cl::Image2D d2_img = make_input_image(d2, fmt);
      const cl::Image2D u_img = make_input_image(u, fmt);
      const cl::Image2D v_img = make_input_image(v, fmt);
      const cl::Image2D s_img = make_input_image(s, fmt);
      const cl::Image2D z_out = make_output_image(z.shape, fmt);
      const cl::Image2D s_out = make_output_image(s.shape, fmt);

      set_image(run_erosion, 0, z_img);
      set_image(run_erosion, 1, d2_img);
      set_image(run_erosion, 2, u_img);
      set_image(run_erosion, 3, v_img);
      set_image(run_erosion, 4, s_img);
      set_image(run_erosion, 5, z_out);
      set_image(run_erosion, 6, s_out);
      run_erosion.set_argument(7, nx);
      run_erosion.set_argument(8, ny);
      run_erosion.set_argument(9, water_height);
      run_erosion.set_argument(10, k_capacity);
      run_erosion.set_argument(11, k_erode);
      run_erosion.set_argument(12, k_depose);
      run_erosion.set_argument(13, k_discharge_exp);
      run_erosion.set_argument(14, downcutting_max_depth_ratio);

      run_erosion.execute({nx, ny});

      const cl::CommandQueue queue = run_erosion.get_queue();
      read_image(z_out, z_new, fmt, queue);
      read_image(s_out, s_new, fmt, queue);

      std::swap(z, z_new);
      std::swap(s, s_new);
    }

    {
      const cl::Image2D u_img = make_input_image(u, fmt);
      const cl::Image2D v_img = make_input_image(v, fmt);
      const cl::Image2D s_img = make_input_image(s, fmt);
      const cl::Image2D s_out = make_output_image(s.shape, fmt);

      set_image(run_transport, 0, u_img);
      set_image(run_transport, 1, v_img);
      set_image(run_transport, 2, s_img);
      set_image(run_transport, 3, s_out);
      run_transport.set_argument(4, nx);
      run_transport.set_argument(5, ny);
      run_transport.set_argument(6, dt);

      run_transport.execute({nx, ny});

      const cl::CommandQueue queue = run_transport.get_queue();
      read_image(s_out, s_transport, fmt, queue);
      std::swap(s, s_transport);
    }

    d = d2;
  }

  if (p_water_depth) *p_water_depth = d;
  if (p_sediment) *p_sediment = s;
  if (p_vel_u) *p_vel_u = u;
  if (p_vel_v) *p_vel_v = v;

  return z;
}

} // namespace

// --- McDonald CPU invariants ----------------------------------------------

TEST(HydraulicCpuInvariants, McDonaldDeterministic)
{
  Array a = scene_fbm();
  Array b = scene_fbm();

  hydraulic_mcdonald(a, 2, 7, nullptr, nullptr, 40.f, 4.f, 64, 16);
  hydraulic_mcdonald(b, 2, 7, nullptr, nullptr, 40.f, 4.f, 64, 16);

  expect_identical(a, b);
  EXPECT_TRUE(all_finite(a));
}

TEST(HydraulicCpuInvariants, McDonaldOutputMaps)
{
  Array z0 = scene_ramp();
  Array z1 = z0;

  Array sediment, discharge;
  hydraulic_mcdonald(z1, 2, 3, &sediment, &discharge, 40.f, 4.f, 64, 16);

  EXPECT_EQ(sediment.shape, z1.shape);
  EXPECT_EQ(discharge.shape, z1.shape);
  EXPECT_TRUE(all_finite(sediment));
  EXPECT_TRUE(all_finite(discharge));

  Array z2 = z0;
  hydraulic_mcdonald(z2, 2, 3, nullptr, nullptr, 40.f, 4.f, 64, 16);

  expect_identical(z1, z2);
}

TEST(HydraulicCpuInvariants, McDonaldZeroStepsNoOp)
{
  Array z0 = scene_ramp();
  Array z = z0;
  Array sediment, discharge;

  hydraulic_mcdonald(z, 0, 3, &sediment, &discharge, 40.f, 4.f, 64, 16);

  expect_identical(z, z0);
  EXPECT_EQ(sediment.shape, z0.shape);
  EXPECT_EQ(discharge.shape, z0.shape);

  for (float v : sediment.vector) EXPECT_EQ(v, 0.f);
  for (float v : discharge.vector) EXPECT_EQ(v, 0.f);
}

TEST(HydraulicCpuInvariants, McDonaldMultiscaleDeterministic)
{
  Array a = scene_fbm(64);
  Array b = scene_fbm(64);

  hydraulic_mcdonald_multiscale(a,
                                5,
                                {4, 2},
                                nullptr,
                                nullptr,
                                40.f,
                                4.f,
                                32,
                                8);
  hydraulic_mcdonald_multiscale(b,
                                5,
                                {4, 2},
                                nullptr,
                                nullptr,
                                40.f,
                                4.f,
                                32,
                                8);

  expect_identical(a, b);
  EXPECT_TRUE(all_finite(a));
}

// --- Schott CPU invariants -------------------------------------------------

TEST(HydraulicCpuInvariants, SchottDeterministic)
{
  Array a = scene_fbm(32);
  Array b = scene_fbm(32);
  Array talus(a.shape, 0.01f);

  hydraulic_schott(a, 3, talus);
  hydraulic_schott(b, 3, talus);

  expect_identical(a, b);
  EXPECT_TRUE(all_finite(a));
}

TEST(HydraulicCpuInvariants, SchottZeroIterationsNoOp)
{
  Array z0 = scene_ramp(16);
  Array z = z0;
  Array talus(z.shape, 0.01f);
  Array flow(z.shape, 1.f);

  hydraulic_schott(z, 0, talus, 1.f, 0.1f, 0.2f, 0.8f, 0.8f, 1.3f, 1.5f,
                   2.5f, &flow);

  expect_identical(z, z0);
  for (float v : flow.vector) EXPECT_EQ(v, 1.f);
}

TEST(HydraulicCpuInvariants, SchottMaskBlend)
{
  Array z0 = scene_fbm(32);
  Array talus(z0.shape, 0.01f);
  Array mask(z0.shape, 0.f);

  for (int j = 8; j < 24; j++)
    for (int i = 8; i < 24; i++) mask(i, j) = 1.f;

  Array z_unmasked = z0;
  hydraulic_schott(z_unmasked, 2, talus);

  Array z_masked = z0;
  hydraulic_schott(z_masked, 2, talus, &mask);

  Array expected = lerp(z0, z_unmasked, mask);
  expect_identical(z_masked, expected);
}

TEST(HydraulicCpuInvariants, SchottFlowOutput)
{
  Array z = scene_fbm(32);
  Array talus(z.shape, 0.01f);
  Array flow(z.shape, 1.f);

  hydraulic_schott(z, 2, talus, 1.f, 0.1f, 0.2f, 0.8f, 0.8f, 1.3f, 1.5f,
                   2.5f, &flow);

  EXPECT_EQ(flow.shape, z.shape);
  EXPECT_TRUE(all_finite(flow));
}

TEST(HydraulicCpuInvariants, SchottErosionDeterministic)
{
  Array a = scene_fbm(32);
  Array b = scene_fbm(32);
  Array flow_a(a.shape, 1.f);
  Array flow_b(b.shape, 1.f);

  hydraulic_schott_erosion(a, 3, 1.f, 0.8f, 1.3f, nullptr, &flow_a);
  hydraulic_schott_erosion(b, 3, 1.f, 0.8f, 1.3f, nullptr, &flow_b);

  expect_identical(a, b);
  expect_identical(flow_a, flow_b);
  EXPECT_TRUE(all_finite(a));
  EXPECT_TRUE(all_finite(flow_a));
}

// --- vpipes CPU invariants -------------------------------------------------

TEST(HydraulicCpuInvariants, VpipesDeterministic)
{
  Array a = scene_fbm(32);
  Array b = scene_fbm(32);

  hydraulic_vpipes(a, 1e-2f, true, 0.1f, 3, 0.5f, 0.5f, 0.001f, 0.01f, 1.f,
                   10.f, true, 0.01f);
  hydraulic_vpipes(b, 1e-2f, true, 0.1f, 3, 0.5f, 0.5f, 0.001f, 0.01f, 1.f,
                   10.f, true, 0.01f);

  expect_identical(a, b);
  EXPECT_TRUE(all_finite(a));
}

TEST(HydraulicCpuInvariants, VpipesZeroIterationsNoOp)
{
  Array z0 = scene_ramp(16);
  Array z = z0;
  Array water_depth, sediment, vel_u, vel_v;

  hydraulic_vpipes(z,
                   1e-2f,
                   true,
                   0.1f,
                   0,
                   0.5f,
                   0.5f,
                   0.001f,
                   0.01f,
                   1.f,
                   10.f,
                   true,
                   0.01f,
                   nullptr,
                   &water_depth,
                   &sediment,
                   &vel_u,
                   &vel_v);

  expect_identical(z, z0);
  EXPECT_EQ(water_depth.shape, z.shape);
  EXPECT_EQ(sediment.shape, z.shape);
  EXPECT_EQ(vel_u.shape, z.shape);
  EXPECT_EQ(vel_v.shape, z.shape);

  for (float v : water_depth.vector) EXPECT_NEAR(v, 1e-2f, 1e-7f);
  for (float v : sediment.vector) EXPECT_EQ(v, 0.f);
  for (float v : vel_u.vector) EXPECT_EQ(v, 0.f);
  for (float v : vel_v.vector) EXPECT_EQ(v, 0.f);
}

TEST(HydraulicCpuInvariants, VpipesOutputMaps)
{
  Array z = scene_fbm(32);
  Array water_depth, sediment, vel_u, vel_v;

  hydraulic_vpipes(z,
                   1e-2f,
                   true,
                   0.1f,
                   3,
                   0.5f,
                   0.5f,
                   0.001f,
                   0.01f,
                   1.f,
                   10.f,
                   true,
                   0.01f,
                   nullptr,
                   &water_depth,
                   &sediment,
                   &vel_u,
                   &vel_v);

  EXPECT_EQ(water_depth.shape, z.shape);
  EXPECT_EQ(sediment.shape, z.shape);
  EXPECT_EQ(vel_u.shape, z.shape);
  EXPECT_EQ(vel_v.shape, z.shape);

  EXPECT_TRUE(all_finite(water_depth));
  EXPECT_TRUE(all_finite(sediment));
  EXPECT_TRUE(all_finite(vel_u));
  EXPECT_TRUE(all_finite(vel_v));

  for (float v : water_depth.vector) EXPECT_GE(v, 0.f);
}

TEST(HydraulicCpuInvariants, VpipesFluxDiffusionOption)
{
  Array a = scene_fbm(32);
  Array b = scene_fbm(32);

  hydraulic_vpipes(a, 1e-2f, true, 0.1f, 3, 0.5f, 0.5f, 0.001f, 0.01f, 1.f,
                   10.f, true, 0.01f);
  hydraulic_vpipes(b, 1e-2f, true, 0.1f, 3, 0.5f, 0.5f, 0.001f, 0.01f, 1.f,
                   10.f, false, 0.01f);

  EXPECT_NE(a.vector, b.vector);
}

TEST(HydraulicCpuInvariants, VpipesMaintainVolumeOption)
{
  Array a = scene_fbm(32);
  Array b = scene_fbm(32);
  Array depth_a, depth_b;

  hydraulic_vpipes(a, 1e-2f, true, 0.1f, 3, 0.5f, 0.5f, 0.001f, 0.01f, 1.f,
                   10.f, true, 0.01f, nullptr, &depth_a);
  hydraulic_vpipes(b, 1e-2f, false, 0.1f, 3, 0.5f, 0.5f, 0.001f, 0.01f, 1.f,
                   10.f, true, 0.01f, nullptr, &depth_b);

  EXPECT_NE(depth_a.vector, depth_b.vector);
}

TEST(HydraulicCpuInvariants, VpipesEvapRateOption)
{
  Array a = scene_fbm(32);
  Array b = scene_fbm(32);
  Array depth_a, depth_b;

  hydraulic_vpipes(a, 1e-2f, true, 0.f, 3, 0.5f, 0.5f, 0.001f, 0.01f, 1.f,
                   10.f, true, 0.01f, nullptr, &depth_a);
  hydraulic_vpipes(b, 1e-2f, true, 0.1f, 3, 0.5f, 0.5f, 0.001f, 0.01f, 1.f,
                   10.f, true, 0.01f, nullptr, &depth_b);

  EXPECT_NE(depth_a.vector, depth_b.vector);
}

// --- GPU-vs-CPU parity -----------------------------------------------------

class HydraulicParity : public ::testing::Test
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

TEST_F(HydraulicParity, McDonaldSingleParticle)
{
  for (const auto &scene : all_scenes())
  {
    const Array base = scene.build();
    const float range = array_range(base);

    Array cpu = base;
    Array gpu_z = base;
    Array cpu_sed, cpu_dis, gpu_sed, gpu_dis;

    hydraulic_mcdonald(cpu,
                       1,
                       7,
                       &cpu_sed,
                       &cpu_dis,
                       40.f,
                       4.f,
                       1,
                       8,
                       0.2f,
                       10.f,
                       1.f,
                       1e-4f,
                       9.81f,
                       0.025f,
                       0.01f,
                       0.57f,
                       0.1f,
                       2.5e-3f,
                       5e-3f,
                       2.5e-4f,
                       0.01f);

    gpu::hydraulic_mcdonald(gpu_z,
                            1,
                            7,
                            &gpu_sed,
                            &gpu_dis,
                            40.f,
                            4.f,
                            1,
                            8,
                            0.2f,
                            10.f,
                            1.f,
                            1e-4f,
                            9.81f,
                            0.025f,
                            0.01f,
                            0.57f,
                            0.1f,
                            2.5e-3f,
                            5e-3f,
                            2.5e-4f,
                            0.01f);

    expect_parity("hydraulic_mcdonald_single",
                  scene.name,
                  1,
                  cpu,
                  gpu_z,
                  range,
                  CONTINUOUS_MAX_RATIO,
                  CONTINUOUS_RMS_RATIO);

    const float sed_range = std::max(array_range(cpu_sed),
                                     array_range(gpu_sed));
    expect_parity("hydraulic_mcdonald_sediment",
                  scene.name,
                  1,
                  cpu_sed,
                  gpu_sed,
                  sed_range,
                  CONTINUOUS_MAX_RATIO,
                  CONTINUOUS_RMS_RATIO);

    const float dis_range = std::max(array_range(cpu_dis),
                                     array_range(gpu_dis));
    expect_parity("hydraulic_mcdonald_discharge",
                  scene.name,
                  1,
                  cpu_dis,
                  gpu_dis,
                  dis_range,
                  CONTINUOUS_MAX_RATIO,
                  CONTINUOUS_RMS_RATIO);
  }
}

TEST_F(HydraulicParity, McDonaldAggregate)
{
  const Array base = scene_fbm(32);
  const float range = array_range(base);

  Array cpu = base;
  Array gpu_z = base;
  Array cpu_sed, cpu_dis, gpu_sed, gpu_dis;

  hydraulic_mcdonald(cpu,
                     2,
                     7,
                     &cpu_sed,
                     &cpu_dis,
                     40.f,
                     4.f,
                     256,
                     16,
                     0.2f,
                     10.f,
                     1.f,
                     1e-4f,
                     9.81f,
                     0.025f,
                     0.01f,
                     0.57f,
                     0.1f,
                     2.5e-3f,
                     5e-3f,
                     2.5e-4f,
                     0.01f);

  gpu::hydraulic_mcdonald(gpu_z,
                          2,
                          7,
                          &gpu_sed,
                          &gpu_dis,
                          40.f,
                          4.f,
                          256,
                          16,
                          0.2f,
                          10.f,
                          1.f,
                          1e-4f,
                          9.81f,
                          0.025f,
                          0.01f,
                          0.57f,
                          0.1f,
                          2.5e-3f,
                          5e-3f,
                          2.5e-4f,
                          0.01f);

  const double sed_cpu = array_sum(cpu_sed);
  const double sed_gpu = array_sum(gpu_sed);
  const double dis_cpu = array_sum(cpu_dis);
  const double dis_gpu = array_sum(gpu_dis);

  const double rel_sed = std::fabs(sed_cpu - sed_gpu) /
                         std::max(1e-6, std::fabs(sed_gpu));
  const double rel_dis = std::fabs(dis_cpu - dis_gpu) /
                         std::max(1e-6, std::fabs(dis_gpu));
  const float mean_dz = mean_abs_diff(cpu, gpu_z);

  const Metrics diag = diff_metrics(cpu, gpu_z);

  print_csv_header_once();
  std::printf("PARITY,hydraulic_mcdonald_aggregate,fbm,2,%.9g,%.9g,%.9g\n",
              (double)diag.max_abs,
              (double)diag.rms_abs,
              (double)range);

  print_agg_header_once();
  std::printf(
      "PARITY_AGG,hydraulic_mcdonald_aggregate,fbm,2,%.9g,%.9g,%.9g\n",
      rel_sed,
      rel_dis,
      (double)mean_dz);
  std::fflush(stdout);

  testing::Test::RecordProperty("hydraulic_mcdonald_aggregate.rel_sediment",
                                fmt(rel_sed));
  testing::Test::RecordProperty("hydraulic_mcdonald_aggregate.rel_discharge",
                                fmt(rel_dis));
  testing::Test::RecordProperty("hydraulic_mcdonald_aggregate.mean_dz",
                                fmt(mean_dz));

  EXPECT_LE(rel_sed, 0.05) << "total sediment differs by " << rel_sed;
  EXPECT_LE(rel_dis, 0.05) << "total discharge differs by " << rel_dis;
  EXPECT_LE(mean_dz, 1e-2f * range) << "mean |dz| = " << mean_dz;
}

TEST_F(HydraulicParity, Schott)
{
  const FloatImageFormat fmt = choose_float_image_format();

  if (!fmt.valid)
    GTEST_SKIP() << "OpenCL device cannot create float images (no float image "
                    "support)";

  struct Case
  {
    const char *name;
    float       thermal_weight;
    float       deposition_weight;
    int         iterations;
  };

  const Case cases[] = {{"schott_erosion_only", 0.f, 0.f, 2},
                        {"schott_erosion_thermal", 1.5f, 0.f, 5},
                        {"schott_default_mix", 1.5f, 2.5f, 8}};

  for (const auto &scene : all_scenes())
  {
    const Array base = scene.build();
    const float range = array_range(base);
    const Array talus(base.shape, 0.01f);

    for (const auto &c : cases)
    {
      Array cpu = base;
      hydraulic_schott(cpu,
                       c.iterations,
                       talus,
                       1.f,
                       0.1f,
                       0.2f,
                       0.8f,
                       0.8f,
                       1.3f,
                       c.thermal_weight,
                       c.deposition_weight);

      const Array gpu_z = gpu_schott_reference(base,
                                               c.iterations,
                                               talus,
                                               1.f,
                                               0.1f,
                                               0.2f,
                                               0.8f,
                                               0.8f,
                                               1.3f,
                                               c.thermal_weight,
                                               c.deposition_weight,
                                               fmt);

      expect_parity(c.name,
                    scene.name,
                    c.iterations,
                    cpu,
                    gpu_z,
                    range,
                    SELECTION_MAX_RATIO,
                    SELECTION_RMS_RATIO);
    }
  }
}

TEST_F(HydraulicParity, SchottErosionOnly)
{
  for (const auto &scene : all_scenes())
  {
    const Array base = scene.build();
    const float range = array_range(base);

    Array cpu = base;
    Array gpu_z = base;

    hydraulic_schott_erosion(cpu, 2);
    gpu::hydraulic_schott_erosion(gpu_z, 2);

    expect_parity("hydraulic_schott_erosion",
                  scene.name,
                  2,
                  cpu,
                  gpu_z,
                  range,
                  SELECTION_MAX_RATIO,
                  SELECTION_RMS_RATIO);
  }
}

TEST_F(HydraulicParity, Vpipes)
{
  const FloatImageFormat fmt = choose_float_image_format();

  if (!fmt.valid)
    GTEST_SKIP() << "OpenCL device cannot create float images (no float image "
                    "support)";

  struct Case
  {
    const char *name;
    bool        maintain_water_volume;
    float       evap_rate;
    bool        flux_diffusion;
    int         iterations;
  };

  const Case cases[] = {{"vpipes_default_1", true, 0.1f, true, 1},
                        {"vpipes_default_2", true, 0.1f, true, 2},
                        {"vpipes_no_flux_diffusion", true, 0.1f, false, 2},
                        {"vpipes_no_maintain", false, 0.1f, true, 2},
                        {"vpipes_no_evap", true, 0.f, true, 2}};

  for (const auto &scene : all_scenes())
  {
    const Array base = scene.build();
    const float range = array_range(base);

    for (const auto &c : cases)
    {
      Array cpu = base;
      Array cpu_depth, cpu_sed, cpu_u, cpu_v;
      Array gpu_depth, gpu_sed, gpu_u, gpu_v;

      hydraulic_vpipes(cpu,
                       1e-2f,
                       c.maintain_water_volume,
                       c.evap_rate,
                       c.iterations,
                       0.5f,
                       0.5f,
                       0.001f,
                       0.01f,
                       1.f,
                       10.f,
                       c.flux_diffusion,
                       0.01f,
                       nullptr,
                       &cpu_depth,
                       &cpu_sed,
                       &cpu_u,
                       &cpu_v);

      const Array gpu_z = gpu_vpipes_reference(base,
                                               1e-2f,
                                               c.maintain_water_volume,
                                               c.evap_rate,
                                               c.iterations,
                                               0.5f,
                                               0.5f,
                                               0.001f,
                                               0.01f,
                                               1.f,
                                               10.f,
                                               c.flux_diffusion,
                                               0.01f,
                                               fmt,
                                               &gpu_depth,
                                               &gpu_sed,
                                               &gpu_u,
                                               &gpu_v);

      expect_parity(c.name,
                    scene.name,
                    c.iterations,
                    cpu,
                    gpu_z,
                    range,
                    CONTINUOUS_MAX_RATIO,
                    CONTINUOUS_RMS_RATIO);

      expect_parity("vpipes_depth",
                    scene.name,
                    c.iterations,
                    cpu_depth,
                    gpu_depth,
                    std::max(array_range(cpu_depth), array_range(gpu_depth)),
                    CONTINUOUS_MAX_RATIO,
                    CONTINUOUS_RMS_RATIO);

      expect_parity("vpipes_sediment",
                    scene.name,
                    c.iterations,
                    cpu_sed,
                    gpu_sed,
                    std::max(array_range(cpu_sed), array_range(gpu_sed)),
                    CONTINUOUS_MAX_RATIO,
                    CONTINUOUS_RMS_RATIO);

      expect_parity("vpipes_vel_u",
                    scene.name,
                    c.iterations,
                    cpu_u,
                    gpu_u,
                    std::max(array_range(cpu_u), array_range(gpu_u)),
                    CONTINUOUS_MAX_RATIO,
                    CONTINUOUS_RMS_RATIO);

      expect_parity("vpipes_vel_v",
                    scene.name,
                    c.iterations,
                    cpu_v,
                    gpu_v,
                    std::max(array_range(cpu_v), array_range(gpu_v)),
                    CONTINUOUS_MAX_RATIO,
                    CONTINUOUS_RMS_RATIO);
    }
  }
}
