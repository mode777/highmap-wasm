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

#include "highmap/array.hpp"
#include "highmap/boundary.hpp"
#include "highmap/erosion.hpp"
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
}
