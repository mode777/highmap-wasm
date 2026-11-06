/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */

/* highmap-wasm CPU ports of the thermal erosion family.
 *
 * Upstream ships the thermal family as OpenCL kernels only
 * (src/gpu_opencl/kernels/thermal*.cl, launched from
 * src/erosion/thermal_gpu.cpp) and declares them exclusively in namespace
 * hmap::gpu (include/highmap/erosion.hpp). This header declares the CPU
 * counterparts implemented in src/erosion/thermal_cpu.cpp, which transcribe
 * those kernels one for one, so the family can run without OpenCL (the
 * highmap-wasm target) and can be validated against the GPU output on the
 * desktop build (tests/src/test_thermal_cpu.cpp).
 *
 * The signatures mirror hmap::gpu::* one for one, so a call site moves between
 * the families by dropping the namespace prefix.
 *
 * thermal_schott is NOT a port: upstream already implements it on the CPU in
 * src/erosion/thermal_schott.cpp, but no header declares it. Its four
 * definitions are declared here as-is (with the defaults the GPU declarations
 * use). It extrapolates borders after every iteration, unlike the GPU wrapper,
 * and is therefore excluded from the parity bounds.
 *
 * Rebase rule: if upstream ever declares CPU implementations of these
 * functions (in highmap/erosion.hpp or elsewhere), delete this header and
 * src/erosion/thermal_cpu.cpp. Duplicate definitions fail loudly at link time;
 * duplicate default arguments fail loudly at compile time.
 */
#pragma once

#include "highmap/array.hpp"

namespace hmap
{

/* --- ports of the GPU-only kernels ---------------------------------------- */

void thermal(Array       &z,
             const Array &talus,
             int          iterations = 10,
             const Array *p_bedrock = nullptr,
             Array       *p_deposition_map = nullptr);

void thermal(Array       &z,
             const Array *p_mask,
             const Array &talus,
             int          iterations = 10,
             const Array *p_bedrock = nullptr,
             Array       *p_deposition_map = nullptr); ///< @overload

void thermal(Array       &z,
             float        talus,
             int          iterations = 10,
             const Array *p_bedrock = nullptr,
             Array       *p_deposition_map = nullptr); ///< @overload

void thermal_auto_bedrock(Array       &z,
                          const Array &talus,
                          int          iterations = 10,
                          Array       *p_deposition_map = nullptr);

void thermal_auto_bedrock(Array       &z,
                          const Array *p_mask,
                          const Array &talus,
                          int          iterations = 10,
                          Array *p_deposition_map = nullptr); ///< @overload

void thermal_auto_bedrock(Array &z,
                          float  talus,
                          int    iterations = 10,
                          Array *p_deposition_map = nullptr); ///< @overload

void thermal_conserve(Array       &z,
                      const Array &talus,
                      int          iterations = 10,
                      float        rate = 0.5f,
                      const Array *p_bedrock = nullptr,
                      Array       *p_deposition_map = nullptr);

void thermal_conserve(Array       &z,
                      const Array *p_mask,
                      const Array &talus,
                      int          iterations = 10,
                      float        rate = 0.5f,
                      const Array *p_bedrock = nullptr,
                      Array       *p_deposition_map = nullptr); ///< @overload

void thermal_conserve(Array       &z,
                      float        talus,
                      int          iterations = 10,
                      float        rate = 0.5f,
                      const Array *p_bedrock = nullptr,
                      Array       *p_deposition_map = nullptr); ///< @overload

void thermal_flatten(Array       &z,
                     const Array &talus,
                     int          iterations,
                     float        sigma_inf = 0.5f,
                     float        sigma_sup = 0.f);

void thermal_flatten(Array       &z,
                     const Array *p_mask,
                     const Array &talus,
                     int          iterations,
                     float        sigma_inf = 0.5f,
                     float        sigma_sup = 0.f); ///< @overload

void thermal_inflate(Array &z, const Array &talus, int iterations = 10);

void thermal_inflate(Array       &z,
                     const Array *p_mask,
                     const Array &talus,
                     int          iterations = 10); ///< @overload

void thermal_olsen(Array &z, const Array &talus, int iterations);

void thermal_olsen(Array       &z,
                   const Array *p_mask,
                   const Array &talus,
                   int          iterations); ///< @overload

void thermal_rib(Array &z, int iterations);

void thermal_rib(Array &z, const Array *p_mask, int iterations); ///< @overload

void thermal_ridge(Array       &z,
                   const Array &talus,
                   int          iterations = 10,
                   Array       *p_deposition_map = nullptr);

void thermal_ridge(Array       &z,
                   const Array *p_mask,
                   const Array &talus,
                   int          iterations = 10,
                   Array       *p_deposition_map = nullptr); ///< @overload

void thermal_scree(Array       &z,
                   const Array &talus,
                   const Array &zmax,
                   int          iterations = 10,
                   Array       *p_deposition_map = nullptr);

void thermal_scree(Array       &z,
                   const Array *p_mask,
                   const Array &talus,
                   const Array &zmax,
                   int          iterations = 10,
                   Array       *p_deposition_map = nullptr); ///< @overload

/* --- upstream CPU implementation, declaration only ------------------------ */

void thermal_schott(Array       &z,
                    const Array &talus,
                    int          iterations = 10,
                    float        intensity = 0.2f);

void thermal_schott(Array       &z,
                    const Array &talus,
                    Array       *p_mask,
                    int          iterations = 10,
                    float        intensity = 0.2f); ///< @overload

void thermal_schott(Array &z,
                    float  talus,
                    int    iterations = 10,
                    float  intensity = 0.2f); ///< @overload

void thermal_schott(Array &z,
                    float  talus,
                    Array *p_mask,
                    int    iterations = 10,
                    float  intensity = 0.2f); ///< @overload

} // namespace hmap
