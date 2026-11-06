/* highmap-wasm embind surface (AD-005, AD-008).
 *
 * The exposed function set is governed by HighMap/wasm/manifest.md; this
 * file grows one entry per manifest row, never by architectural change.
 * glm types stay at the boundary: JS numbers/arrays are converted here.
 *
 * Semantics: functions taking `Array&` mutate in place (the JS layer
 * returns the same wrapper); functions returning `Array` produce a new
 * handle. */

#include <emscripten/bind.h>
#include <cstdint>
#include <vector>

#include "highmap/array.hpp"
#include "highmap/boundary.hpp"
#include "highmap/erosion.hpp"
#include "highmap/erosion_hydraulic_cpu.hpp"
#include "highmap/erosion_thermal_cpu.hpp"
#include "highmap/filters.hpp"
#include "highmap/primitives/coherent_noise.hpp"
#include "highmap/range.hpp"
#include "highmap/shadows.hpp"
#include "highmap/transform.hpp"

using namespace emscripten;

namespace hmap_wasm
{

// --- Array accessors ---------------------------------------------------------
// The JS wrapper (HArray) reads/writes data through typed-array views over
// wasm memory; these helpers expose the pointer and extents it needs.

int array_ni(const hmap::Array &a) { return a.shape.x; }
int array_nj(const hmap::Array &a) { return a.shape.y; }

std::uintptr_t array_data_ptr(const hmap::Array &a)
{
  return reinterpret_cast<std::uintptr_t>(a.vector.data());
}

hmap::Array make_array(int ni, int nj, float value)
{
  return hmap::Array(glm::ivec2(ni, nj), value);
}

// --- noise family --------------------------------------------------------------

hmap::Array noise(hmap::NoiseType noise_type,
                  int ni,
                  int nj,
                  float kx,
                  float ky,
                  std::uint32_t seed)
{
  return hmap::noise(noise_type, glm::ivec2(ni, nj), glm::vec2(kx, ky), seed);
}

hmap::Array noise_fbm(hmap::NoiseType noise_type,
                      int ni,
                      int nj,
                      float kx,
                      float ky,
                      std::uint32_t seed,
                      int octaves,
                      float weight,
                      float persistence,
                      float lacunarity)
{
  return hmap::noise_fbm(noise_type,
                         glm::ivec2(ni, nj),
                         glm::vec2(kx, ky),
                         seed,
                         octaves,
                         weight,
                         persistence,
                         lacunarity);
}

hmap::Array noise_pingpong(hmap::NoiseType noise_type,
                           int ni,
                           int nj,
                           float kx,
                           float ky,
                           std::uint32_t seed,
                           int octaves,
                           float weight,
                           float persistence,
                           float lacunarity)
{
  return hmap::noise_pingpong(noise_type,
                              glm::ivec2(ni, nj),
                              glm::vec2(kx, ky),
                              seed,
                              octaves,
                              weight,
                              persistence,
                              lacunarity);
}

hmap::Array noise_ridged(hmap::NoiseType noise_type,
                         int ni,
                         int nj,
                         float kx,
                         float ky,
                         std::uint32_t seed,
                         int octaves,
                         float weight,
                         float persistence,
                         float lacunarity,
                         float k_smoothing)
{
  return hmap::noise_ridged(noise_type,
                            glm::ivec2(ni, nj),
                            glm::vec2(kx, ky),
                            seed,
                            octaves,
                            weight,
                            persistence,
                            lacunarity,
                            k_smoothing);
}

// --- value range / filters (in place) ------------------------------------------

void clamp_values(hmap::Array &a, float vmin, float vmax)
{
  hmap::clamp(a, vmin, vmax);
}

void remap_values(hmap::Array &a, float vmin, float vmax)
{
  hmap::remap(a, vmin, vmax);
}

void falloff(hmap::Array &a, float strength, hmap::DistanceFunction dist_fct)
{
  hmap::falloff(a, strength, dist_fct);
}

void plateau(hmap::Array &a, int ir, float factor)
{
  hmap::plateau(a, ir, factor);
}

void smooth_fill(hmap::Array &a, int ir, float k)
{
  hmap::smooth_fill(a, ir, k, nullptr);
}

// --- shading (returns new array) ------------------------------------------------

hmap::Array hillshade(const hmap::Array &z,
                      float azimuth,
                      float zenith,
                      float talus_ref)
{
  return hmap::hillshade(z, azimuth, zenith, talus_ref);
}

// --- transforms (in place) --------------------------------------------------------

void warp(hmap::Array &a, const hmap::Array &dx, const hmap::Array &dy)
{
  hmap::warp(a, &dx, &dy);
}

// --- algebra layer (new arrays; AD-009) ---------------------------------------------

hmap::Array add(const hmap::Array &a, const hmap::Array &b) { return a + b; }
hmap::Array subtract(const hmap::Array &a, const hmap::Array &b)
{
  return a - b;
}
hmap::Array multiply(const hmap::Array &a, const hmap::Array &b)
{
  return a * b;
}
hmap::Array divide(const hmap::Array &a, const hmap::Array &b) { return a / b; }

hmap::Array add_scalar(const hmap::Array &a, float v) { return a + v; }
hmap::Array subtract_scalar(const hmap::Array &a, float v) { return a - v; }
hmap::Array multiply_scalar(const hmap::Array &a, float v) { return a * v; }
hmap::Array divide_scalar(const hmap::Array &a, float v) { return a / v; }

void scale(hmap::Array &a, float factor) { a *= factor; }

// --- erosion (in place) --------------------------------------------------------------

void hydraulic_musgrave(hmap::Array &z,
                        int iterations,
                        float c_capacity,
                        float c_erosion,
                        float c_deposition,
                        float water_level,
                        float evap_rate)
{
  hmap::hydraulic_musgrave(z,
                           iterations,
                           c_capacity,
                           c_erosion,
                           c_deposition,
                           water_level,
                           evap_rate);
}

void hydraulic_stream(hmap::Array &z,
                      float c_erosion,
                      float talus_ref,
                      int ir,
                      float clipping_ratio)
{
  hmap::hydraulic_stream(z,
                         c_erosion,
                         talus_ref,
                         nullptr,
                         nullptr,
                         nullptr,
                         ir,
                         clipping_ratio);
}

// --- thermal family (CPU ports, add-thermal-cpu-port) ------------------------

void thermal(hmap::Array &z, const hmap::Array &talus, int iterations)
{
  hmap::thermal(z, talus, iterations);
}

void thermal_auto_bedrock(hmap::Array       &z,
                          const hmap::Array &talus,
                          int                iterations)
{
  hmap::thermal_auto_bedrock(z, talus, iterations);
}

void thermal_conserve(hmap::Array       &z,
                      const hmap::Array &talus,
                      int                iterations,
                      float              rate)
{
  hmap::thermal_conserve(z, talus, iterations, rate);
}

void thermal_flatten(hmap::Array       &z,
                     const hmap::Array &talus,
                     int                iterations,
                     float              sigma_inf,
                     float              sigma_sup)
{
  hmap::thermal_flatten(z, talus, iterations, sigma_inf, sigma_sup);
}

void thermal_olsen(hmap::Array       &z,
                   const hmap::Array &talus,
                   int                iterations)
{
  hmap::thermal_olsen(z, talus, iterations);
}

void thermal_rib(hmap::Array &z, int iterations)
{
  hmap::thermal_rib(z, iterations);
}

void thermal_inflate(hmap::Array       &z,
                     const hmap::Array &talus,
                     int                iterations)
{
  hmap::thermal_inflate(z, talus, iterations);
}

void thermal_ridge(hmap::Array       &z,
                   const hmap::Array &talus,
                   int                iterations)
{
  hmap::thermal_ridge(z, talus, iterations);
}

void thermal_scree(hmap::Array       &z,
                   const hmap::Array &talus,
                   const hmap::Array &zmax,
                   int                iterations)
{
  hmap::thermal_scree(z, talus, zmax, iterations);
}

void thermal_schott(hmap::Array       &z,
                    const hmap::Array &talus,
                    int                iterations,
                    float              intensity)
{
  hmap::thermal_schott(z, talus, iterations, intensity);
}

// --- hydraulic family (CPU ports, add-hydraulic-cpu-ports) -------------------

std::vector<int> to_int_vector(const emscripten::val &values)
{
  std::vector<int> out;
  const unsigned   length = values["length"].as<unsigned>();
  out.reserve(length);
  for (unsigned i = 0; i < length; ++i) out.push_back(values[i].as<int>());
  return out;
}

void hydraulic_mcdonald(hmap::Array        &z,
                        int                 steps,
                        std::uint32_t       seed,
                        hmap::Array        *p_sediment_map,
                        hmap::Array        *p_discharge_map,
                        float               world_extent_km,
                        float               z_scale_km,
                        int                 samples,
                        int                 maxage,
                        float               lrate,
                        float               time_step,
                        float               rainfall,
                        float               evap_rate,
                        float               gravity,
                        float               viscosity,
                        float               bed_shear,
                        float               crit_slope,
                        float               settle_rate,
                        float               thermal_rate,
                        float               deposition_rate,
                        float               suspension_rate,
                        float               exit_slope)
{
  hmap::hydraulic_mcdonald(z,
                           steps,
                           seed,
                           p_sediment_map,
                           p_discharge_map,
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
                           exit_slope);
}

void hydraulic_mcdonald_multiscale(hmap::Array        &z,
                                   std::uint32_t       seed,
                                   const emscripten::val &steps_per_level,
                                   hmap::Array        *p_sediment_map,
                                   hmap::Array        *p_discharge_map,
                                   float               world_extent_km,
                                   float               z_scale_km,
                                   int                 samples,
                                   int                 maxage,
                                   float               lrate,
                                   float               time_step,
                                   float               rainfall,
                                   float               evap_rate,
                                   float               gravity,
                                   float               viscosity,
                                   float               bed_shear,
                                   float               crit_slope,
                                   float               settle_rate,
                                   float               thermal_rate,
                                   float               deposition_rate,
                                   float               suspension_rate,
                                   float               exit_slope)
{
  hmap::hydraulic_mcdonald_multiscale(z,
                                      seed,
                                      to_int_vector(steps_per_level),
                                      p_sediment_map,
                                      p_discharge_map,
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
                                      exit_slope);
}

void hydraulic_schott(hmap::Array       &z,
                      int                iterations,
                      const hmap::Array &talus,
                      hmap::Array       *p_flow,
                      float              c_erosion,
                      float              c_thermal,
                      float              c_deposition,
                      float              flow_acc_exponent,
                      float              flow_acc_exponent_depo,
                      float              flow_routing_exponent,
                      float              thermal_weight,
                      float              deposition_weight)
{
  hmap::hydraulic_schott(z,
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
}

void hydraulic_schott_erosion(hmap::Array &z,
                              int          iterations,
                              hmap::Array *p_flow,
                              float        c_erosion,
                              float        flow_acc_exponent,
                              float        flow_routing_exponent)
{
  hmap::hydraulic_schott_erosion(z,
                                 iterations,
                                 c_erosion,
                                 flow_acc_exponent,
                                 flow_routing_exponent,
                                 nullptr,
                                 p_flow);
}

void hydraulic_vpipes(hmap::Array &z,
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
                      hmap::Array *p_water_depth,
                      hmap::Array *p_sediment,
                      hmap::Array *p_vel_u,
                      hmap::Array *p_vel_v)
{
  hmap::hydraulic_vpipes(z,
                         water_height,
                         maintain_water_volume,
                         evap_rate,
                         iterations,
                         dt,
                         k_capacity,
                         k_erode,
                         k_depose,
                         k_discharge_exp,
                         downcutting_max_depth_ratio,
                         flux_diffusion,
                         flux_diffusion_strength,
                         nullptr,
                         p_water_depth,
                         p_sediment,
                         p_vel_u,
                         p_vel_v);
}

} // namespace hmap_wasm

EMSCRIPTEN_BINDINGS(highmap_wasm)
{
  enum_<hmap::NoiseType>("NoiseType")
      .value("PARBERRY", hmap::NoiseType::PARBERRY)
      .value("PERLIN", hmap::NoiseType::PERLIN)
      .value("PERLIN_BILLOW", hmap::NoiseType::PERLIN_BILLOW)
      .value("PERLIN_HALF", hmap::NoiseType::PERLIN_HALF)
      .value("SIMPLEX2", hmap::NoiseType::SIMPLEX2)
      .value("SIMPLEX2S", hmap::NoiseType::SIMPLEX2S)
      .value("VALUE", hmap::NoiseType::VALUE)
      .value("VALUE_CUBIC", hmap::NoiseType::VALUE_CUBIC)
      .value("WORLEY", hmap::NoiseType::WORLEY)
      .value("WORLEY_DOUBLE", hmap::NoiseType::WORLEY_DOUBLE)
      .value("WORLEY_VALUE", hmap::NoiseType::WORLEY_VALUE);

  enum_<hmap::DistanceFunction>("DistanceFunction")
      .value("CHEBYSHEV", hmap::DistanceFunction::CHEBYSHEV)
      .value("EUCLIDIAN", hmap::DistanceFunction::EUCLIDIAN)
      .value("EUCLISHEV", hmap::DistanceFunction::EUCLISHEV)
      .value("MANHATTAN", hmap::DistanceFunction::MANHATTAN);

  class_<hmap::Array>("RawArray")
      .constructor<>()
      .function("ni", &hmap_wasm::array_ni)
      .function("nj", &hmap_wasm::array_nj)
      .function("dataPtr", &hmap_wasm::array_data_ptr);

  function("makeArray", &hmap_wasm::make_array);
  function("noise", &hmap_wasm::noise);
  function("noiseFbm", &hmap_wasm::noise_fbm);
  function("noisePingpong", &hmap_wasm::noise_pingpong);
  function("noiseRidged", &hmap_wasm::noise_ridged);
  function("clampValues", &hmap_wasm::clamp_values);
  function("remapValues", &hmap_wasm::remap_values);
  function("falloff", &hmap_wasm::falloff);
  function("plateau", &hmap_wasm::plateau);
  function("smoothFill", &hmap_wasm::smooth_fill);
  function("hillshade", &hmap_wasm::hillshade);
  function("warp", &hmap_wasm::warp);
  function("add", &hmap_wasm::add);
  function("subtract", &hmap_wasm::subtract);
  function("multiply", &hmap_wasm::multiply);
  function("divide", &hmap_wasm::divide);
  function("addScalar", &hmap_wasm::add_scalar);
  function("subtractScalar", &hmap_wasm::subtract_scalar);
  function("multiplyScalar", &hmap_wasm::multiply_scalar);
  function("divideScalar", &hmap_wasm::divide_scalar);
  function("scale", &hmap_wasm::scale);
  function("hydraulicMusgrave", &hmap_wasm::hydraulic_musgrave);
  function("hydraulicStream", &hmap_wasm::hydraulic_stream);
  function("thermal", &hmap_wasm::thermal);
  function("thermalAutoBedrock", &hmap_wasm::thermal_auto_bedrock);
  function("thermalConserve", &hmap_wasm::thermal_conserve);
  function("thermalFlatten", &hmap_wasm::thermal_flatten);
  function("thermalOlsen", &hmap_wasm::thermal_olsen);
  function("thermalRib", &hmap_wasm::thermal_rib);
  function("thermalInflate", &hmap_wasm::thermal_inflate);
  function("thermalRidge", &hmap_wasm::thermal_ridge);
  function("thermalScree", &hmap_wasm::thermal_scree);
  function("thermalSchott", &hmap_wasm::thermal_schott);
  function("hydraulicMcdonald",
           &hmap_wasm::hydraulic_mcdonald,
           allow_raw_pointers());
  function("hydraulicMcdonaldMultiscale",
           &hmap_wasm::hydraulic_mcdonald_multiscale,
           allow_raw_pointers());
  function("hydraulicSchott",
           &hmap_wasm::hydraulic_schott,
           allow_raw_pointers());
  function("hydraulicSchottErosion",
           &hmap_wasm::hydraulic_schott_erosion,
           allow_raw_pointers());
  function("hydraulicVpipes",
           &hmap_wasm::hydraulic_vpipes,
           allow_raw_pointers());
}
