/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */

/* GPU-versus-CPU parity harness for the thermal family (add-thermal-cpu-port).
 *
 * The CPU family (src/erosion/thermal_cpu.cpp) transcribes the OpenCL kernels;
 * this file runs both implementations on fixed scenes and reports per-variant
 * error metrics. It is the oracle that decides whether "parity" may be claimed
 * (design D5).
 *
 * Suites:
 *   ThermalCpuInvariants - CPU-only properties (borders, determinism, masks,
 *                          deposition). Run everywhere, no OpenCL needed.
 *   ThermalParity        - GPU-vs-CPU comparison. Skips with GTEST_SKIP() when
 *                          no OpenCL device is available; the thermal-parity CI
 *                          job treats a skipped case as a failure.
 *
 * Every parity case prints one line to stdout:
 *   PARITY,<variant>,<scene>,<iterations>,<max_abs>,<rms_abs>,<range>
 * which the CI job turns into the job summary and the uploaded CSV.
 *
 * Tolerance calibration (design D5): bounds are per variant and expressed as a
 * fraction of the input range. A deliberate one-branch mutation of a port must
 * push its variant over its bound (checked once, recorded in
 * docs/thermal-parity.md).
 */

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "highmap.hpp"
#include "highmap/erosion_thermal_cpu.hpp"

using namespace hmap;

namespace
{

// --- scenes ---------------------------------------------------------------

Array scene_fbm()
{
  const glm::ivec2 shape = {128, 128};
  const glm::vec2  kw = {4.f, 4.f};
  return noise_fbm(NoiseType::PERLIN, shape, kw, 11);
}

Array scene_ramp_plateau()
{
  const glm::ivec2 shape = {128, 128};
  Array            z(shape);

  for (int j = 0; j < shape.y; j++)
    for (int i = 0; i < shape.x; i++)
    {
      const float x = (float)i / (float)(shape.x - 1);
      const float y = (float)j / (float)(shape.y - 1);

      float v = 0.8f * x + 0.2f * y;
      if (x > 0.55f && x < 0.75f && y > 0.3f && y < 0.7f) v = 0.5f;

      z(i, j) = v;
    }

  return z;
}

Array scene_spike()
{
  const glm::ivec2 shape = {64, 64};
  Array            z(shape, 0.5f);
  z(shape.x / 2, shape.y / 2) = 1.f;
  return z;
}

struct Scene
{
  const char *name;
  Array (*build)();
};

std::vector<Scene> all_scenes()
{
  return {{"fbm", &scene_fbm},
          {"ramp", &scene_ramp_plateau},
          {"spike", &scene_spike}};
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

// --- reporting / assertions -----------------------------------------------

// Continuous variants: the kernel arithmetic is branch-continuous, so only
// float rounding separates the two implementations.
constexpr float CONTINUOUS_MAX_RATIO = 1e-4f;
constexpr float CONTINUOUS_RMS_RATIO = 1e-5f;

// Selection-based variants (argmin/argmax flips on near-ties) get a wider
// single-cell bound but a mean-error bound. Provisional: tightened in
// docs/thermal-parity.md from the first oracle run.
constexpr float SELECTION_MAX_RATIO = 5e-2f;
constexpr float SELECTION_RMS_RATIO = 1e-3f;

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

} // namespace

// --- CPU-only invariants ---------------------------------------------------

TEST(ThermalCpuInvariants, ZeroIterationsOnlyExtrapolatesBorder)
{
  const Array z0 = scene_fbm();

  Array z = z0;
  thermal(z, 0.01f, 0);

  for (int j = 1; j < z.shape.y - 1; j++)
    for (int i = 1; i < z.shape.x - 1; i++)
      EXPECT_EQ(z(i, j), z0(i, j))
          << "interior cell changed at " << i << "," << j;

  Array expected = z0;
  extrapolate_borders(expected);

  for (std::size_t k = 0; k < z.vector.size(); k++)
    EXPECT_EQ(z.vector[k], expected.vector[k]) << "border cell " << k;
}

TEST(ThermalCpuInvariants, RepeatedCallsAreByteIdentical)
{
  for (const auto &scene : all_scenes())
  {
    const Array base = scene.build();

    Array a = base;
    Array b = base;

    thermal(a, 0.01f, 5);
    thermal(b, 0.01f, 5);

    EXPECT_EQ(a.vector, b.vector) << scene.name;
  }
}

TEST(ThermalCpuInvariants, ScalarTalusMatchesConstantTalusGrid)
{
  const Array base = scene_fbm();
  const float talus = 0.01f;

  Array a = base;
  Array b = base;

  thermal(a, talus, 3);
  thermal(b, Array(base.shape, talus), 3);

  EXPECT_EQ(a.vector, b.vector);
}

TEST(ThermalCpuInvariants, MaskBlendsUntouchedAndFullyErodedGrids)
{
  const Array base = scene_fbm();

  Array mask(base.shape, 0.f);
  for (int j = 0; j < mask.shape.y; j++)
    for (int i = 0; i < mask.shape.x; i++)
      mask(i, j) = (i < mask.shape.x / 2) ? 0.f : 1.f;

  Array full = base;
  thermal(full, 0.01f, 3);

  Array masked = base;
  thermal(masked, &mask, 0.01f, 3);

  for (std::size_t k = 0; k < base.vector.size(); k++)
  {
    const float expected = base.vector[k] +
                           mask.vector[k] * (full.vector[k] - base.vector[k]);
    EXPECT_NEAR(masked.vector[k], expected, 1e-6f) << "cell " << k;
  }
}

TEST(ThermalCpuInvariants, DepositionIsThePositiveElevationGain)
{
  const Array z0 = scene_fbm();

  Array z = z0;
  Array dep(z.shape);

  thermal(z, 0.01f, 5, nullptr, &dep);

  const Array expected = maximum(z - z0, 0.f);

  for (std::size_t k = 0; k < dep.vector.size(); k++)
  {
    EXPECT_GE(dep.vector[k], 0.f) << "negative deposition at cell " << k;
    EXPECT_EQ(dep.vector[k], expected.vector[k]) << "cell " << k;
  }
}

TEST(ThermalCpuInvariants, AutoBedrockNeverErodesBelowInitialSurface)
{
  for (const auto &scene : all_scenes())
  {
    const Array z0 = scene.build();

    Array z = z0;
    thermal_auto_bedrock(z, 0.01f, 5);

    // The border is excluded: extrapolate_borders() can move a border cell
    // below its own starting value. The kernel clamps the interior.
    for (int j = 1; j < z.shape.y - 1; j++)
      for (int i = 1; i < z.shape.x - 1; i++)
        EXPECT_GE(z(i, j), z0(i, j)) << scene.name << " cell " << i << ","
                                     << j;
  }
}

TEST(ThermalCpuInvariants, ConserveKeepsBoundaryAndTotalMass)
{
  const Array z0 = scene_fbm();

  Array z = z0;
  thermal_conserve(z, 0.01f, 5);

  const int ni = z.shape.x;
  const int nj = z.shape.y;

  for (int i = 0; i < ni; i++)
  {
    EXPECT_EQ(z(i, 0), z0(i, 0));
    EXPECT_EQ(z(i, nj - 1), z0(i, nj - 1));
  }
  for (int j = 0; j < nj; j++)
  {
    EXPECT_EQ(z(0, j), z0(0, j));
    EXPECT_EQ(z(ni - 1, j), z0(ni - 1, j));
  }

  double s0 = 0.0;
  double s1 = 0.0;
  for (std::size_t k = 0; k < z0.vector.size(); k++)
  {
    s0 += (double)z0.vector[k];
    s1 += (double)z.vector[k];
  }

  EXPECT_LT(std::fabs(s1 - s0), 1e-2 * std::max(1.0, std::fabs(s0)));
}

// --- GPU-vs-CPU parity -----------------------------------------------------

class ThermalParity : public ::testing::Test
{
protected:
  void SetUp() override
  {
    if (!gpu::init_opencl())
      GTEST_SKIP() << "no OpenCL platform/device available - parity cannot be "
                      "verified on this machine";
  }
};

TEST_F(ThermalParity, Thermal)
{
  for (const auto &scene : all_scenes())
  {
    const Array base = scene.build();
    const float range = array_range(base);

    for (const float talus_coeff : {0.1f, 2.f})
    {
      const float talus = talus_coeff / (float)base.shape.x;

      for (const int iterations : {1, 10})
      {
        Array cpu = base;
        Array gpu_z = base;

        thermal(cpu, talus, iterations);
        gpu::thermal(gpu_z, talus, iterations);

        expect_parity("thermal",
                      scene.name,
                      iterations,
                      cpu,
                      gpu_z,
                      range,
                      CONTINUOUS_MAX_RATIO,
                      CONTINUOUS_RMS_RATIO);
      }
    }
  }
}

TEST_F(ThermalParity, ThermalAutoBedrock)
{
  for (const auto &scene : all_scenes())
  {
    const Array base = scene.build();
    const float range = array_range(base);

    for (const float talus_coeff : {0.1f, 2.f})
    {
      const float talus = talus_coeff / (float)base.shape.x;

      for (const int iterations : {1, 10})
      {
        Array cpu = base;
        Array gpu_z = base;

        thermal_auto_bedrock(cpu, talus, iterations);
        gpu::thermal_auto_bedrock(gpu_z, talus, iterations);

        expect_parity("thermal_auto_bedrock",
                      scene.name,
                      iterations,
                      cpu,
                      gpu_z,
                      range,
                      CONTINUOUS_MAX_RATIO,
                      CONTINUOUS_RMS_RATIO);
      }
    }
  }
}

TEST_F(ThermalParity, ThermalConserve)
{
  for (const auto &scene : all_scenes())
  {
    const Array base = scene.build();
    const float range = array_range(base);

    for (const float talus_coeff : {0.1f, 2.f})
    {
      const float talus = talus_coeff / (float)base.shape.x;

      for (const int iterations : {1, 10})
      {
        Array cpu = base;
        Array gpu_z = base;

        thermal_conserve(cpu, talus, iterations, 0.5f);
        gpu::thermal_conserve(gpu_z, talus, iterations, 0.5f);

        expect_parity("thermal_conserve",
                      scene.name,
                      iterations,
                      cpu,
                      gpu_z,
                      range,
                      CONTINUOUS_MAX_RATIO,
                      CONTINUOUS_RMS_RATIO);
      }
    }
  }
}

TEST_F(ThermalParity, ThermalFlatten)
{
  for (const auto &scene : all_scenes())
  {
    const Array base = scene.build();
    const float range = array_range(base);

    for (const float talus_coeff : {0.1f, 2.f})
    {
      const float talus = talus_coeff / (float)base.shape.x;

      for (const int iterations : {1, 10})
      {
        Array cpu = base;
        Array gpu_z = base;

        thermal_flatten(cpu, talus, iterations, 0.5f, 0.f);
        gpu::thermal_flatten(gpu_z, talus, iterations, 0.5f, 0.f);

        expect_parity("thermal_flatten",
                      scene.name,
                      iterations,
                      cpu,
                      gpu_z,
                      range,
                      SELECTION_MAX_RATIO,
                      SELECTION_RMS_RATIO);
      }
    }
  }
}

TEST_F(ThermalParity, ThermalOlsen)
{
  for (const auto &scene : all_scenes())
  {
    const Array base = scene.build();
    const float range = array_range(base);

    for (const float talus_coeff : {0.1f, 2.f})
    {
      const float talus = talus_coeff / (float)base.shape.x;

      for (const int iterations : {1, 10})
      {
        Array cpu = base;
        Array gpu_z = base;

        thermal_olsen(cpu, talus, iterations);
        gpu::thermal_olsen(gpu_z, talus, iterations);

        expect_parity("thermal_olsen",
                      scene.name,
                      iterations,
                      cpu,
                      gpu_z,
                      range,
                      SELECTION_MAX_RATIO,
                      SELECTION_RMS_RATIO);
      }
    }
  }
}

TEST_F(ThermalParity, ThermalRib)
{
  for (const auto &scene : all_scenes())
  {
    const Array base = scene.build();
    const float range = array_range(base);

    for (const int iterations : {1, 10})
    {
      Array cpu = base;
      Array gpu_z = base;

      thermal_rib(cpu, iterations);
      gpu::thermal_rib(gpu_z, iterations);

      expect_parity("thermal_rib",
                    scene.name,
                    iterations,
                    cpu,
                    gpu_z,
                    range,
                    SELECTION_MAX_RATIO,
                    SELECTION_RMS_RATIO);
    }
  }
}

TEST_F(ThermalParity, ThermalInflate)
{
  for (const auto &scene : all_scenes())
  {
    const Array base = scene.build();
    const float range = array_range(base);

    for (const float talus_coeff : {0.1f, 2.f})
    {
      const float talus = talus_coeff / (float)base.shape.x;

      for (const int iterations : {1, 10})
      {
        Array cpu = base;
        Array gpu_z = base;

        thermal_inflate(cpu, talus, iterations);
        gpu::thermal_inflate(gpu_z, talus, iterations);

        expect_parity("thermal_inflate",
                      scene.name,
                      iterations,
                      cpu,
                      gpu_z,
                      range,
                      CONTINUOUS_MAX_RATIO,
                      CONTINUOUS_RMS_RATIO);
      }
    }
  }
}

TEST_F(ThermalParity, ThermalRidge)
{
  for (const auto &scene : all_scenes())
  {
    const Array base = scene.build();
    const float range = array_range(base);

    for (const float talus_coeff : {0.1f, 2.f})
    {
      const float talus = talus_coeff / (float)base.shape.x;

      for (const int iterations : {1, 10})
      {
        Array cpu = base;
        Array gpu_z = base;

        thermal_ridge(cpu, talus, iterations);
        gpu::thermal_ridge(gpu_z, talus, iterations);

        expect_parity("thermal_ridge",
                      scene.name,
                      iterations,
                      cpu,
                      gpu_z,
                      range,
                      CONTINUOUS_MAX_RATIO,
                      CONTINUOUS_RMS_RATIO);
      }
    }
  }
}

TEST_F(ThermalParity, ThermalScree)
{
  for (const auto &scene : all_scenes())
  {
    const Array base = scene.build();
    const float range = array_range(base);

    // the scree ceiling is the input maximum, so the amplitude ramp is active
    const Array zmax(base.shape, base.vector[0] + range);

    for (const float talus_coeff : {0.1f, 2.f})
    {
      const float talus = talus_coeff / (float)base.shape.x;

      for (const int iterations : {1, 10})
      {
        Array cpu = base;
        Array gpu_z = base;

        thermal_scree(cpu, talus, zmax, iterations);
        gpu::thermal_scree(gpu_z, talus, zmax, iterations);

        expect_parity("thermal_scree",
                      scene.name,
                      iterations,
                      cpu,
                      gpu_z,
                      range,
                      CONTINUOUS_MAX_RATIO,
                      CONTINUOUS_RMS_RATIO);
      }
    }
  }
}

TEST(ThermalCpuInvariants, SchottIsDeterministicAndErodesApexes)
{
  Array spike(32, 32, 0.5f);
  spike(16, 16) = 1.f;

  Array a = spike;
  Array b = spike;

  thermal_schott(a, 0.01f, 5);
  thermal_schott(b, 0.01f, 5);

  EXPECT_EQ(a.vector, b.vector);
  EXPECT_LT(a(16, 16), 1.f);

  for (std::size_t k = 0; k < a.vector.size(); k++)
    EXPECT_TRUE(std::isfinite(a.vector[k])) << "cell " << k;
}

TEST(ThermalCpuInvariants, BedrockIsALowerBoundForThermal)
{
  const Array z0 = scene_fbm();
  Array       bedrock = z0;

  Array z = z0;
  thermal(z, 0.01f, 5, &bedrock);

  // interior only: extrapolate_borders() rewrites the one-cell border
  for (int j = 1; j < z.shape.y - 1; j++)
    for (int i = 1; i < z.shape.x - 1; i++)
      EXPECT_GE(z(i, j), bedrock(i, j)) << "cell " << i << "," << j;
}

TEST(ThermalCpuInvariants, RidgeDepositionIsTheAbsoluteChange)
{
  const Array z0 = scene_fbm();

  Array z = z0;
  Array dep(z.shape);

  thermal_ridge(z, 0.01f, 5, &dep);

  const Array expected = abs(z - z0);

  for (std::size_t k = 0; k < dep.vector.size(); k++)
  {
    EXPECT_GE(dep.vector[k], 0.f) << "negative deposition at cell " << k;
    EXPECT_EQ(dep.vector[k], expected.vector[k]) << "cell " << k;
  }
}
