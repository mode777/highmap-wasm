/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */

/* highmap-wasm CPU ports of the hydraulic erosion family.
 *
 * Upstream ships these algorithms as OpenCL kernels only
 * (src/gpu_opencl/kernels/hydraulic_{mcdonald,schott,vpipes}.cl, launched from
 * src/erosion/hydraulic_*_gpu.cpp) and declares them exclusively in namespace
 * hmap::gpu (include/highmap/erosion.hpp). This header declares the CPU
 * counterparts implemented in src/erosion/hydraulic_cpu.cpp, which transcribe
 * those kernels, so the family can run without OpenCL (the highmap-wasm
 * target) and can be validated against the GPU output on the desktop build
 * (tests/src/test_hydraulic_cpu.cpp).
 *
 * The signatures mirror the public hmap::gpu::* declarations one for one, so a
 * call site moves between the families by dropping the namespace prefix.
 * Input-only pointers are const where the GPU declaration is unnecessarily
 * non-const; callers that pass non-const pointers still compile. The
 * hydraulic_schott mask overload follows the public header's parameter order,
 * not the GPU definition's different order.
 *
 * Output maps are written only when their pointer is non-null. They are
 * caller-owned arrays and are assigned the final state, matching the GPU
 * wrappers.
 *
 * Rebase rule: if upstream ever declares CPU implementations of these
 * functions (in highmap/erosion.hpp or elsewhere), delete this header and
 * src/erosion/hydraulic_cpu.cpp. Duplicate definitions fail loudly at link
 * time; duplicate default arguments fail loudly at compile time. See AD-022 in
 * docs/decisions.md.
 */
#pragma once

#include <cstdint>
#include <vector>

#include "highmap/array.hpp"

namespace hmap
{

/* --- McDonald flow-coupled particle erosion -------------------------------- */

void hydraulic_mcdonald(Array        &z,
                        int           steps,
                        std::uint32_t seed,
                        Array        *p_sediment_map = nullptr,
                        Array        *p_discharge_map = nullptr,
                        float         world_extent_km = 40.f,
                        float         z_scale_km = 4.f,
                        int           samples = 8192,
                        int           maxage = 512,
                        float         lrate = 0.2f,
                        float         time_step = 10.f,
                        float         rainfall = 1.f,
                        float         evap_rate = 1e-4f,
                        float         gravity = 9.81f,
                        float         viscosity = 0.025f,
                        float         bed_shear = 0.01f,
                        float         crit_slope = 0.57f,
                        float         settle_rate = 0.1f,
                        float         thermal_rate = 2.5e-3f,
                        float         deposition_rate = 5e-3f,
                        float         suspension_rate = 2.5e-4f,
                        float         exit_slope = 0.01f);

void hydraulic_mcdonald_multiscale(
    Array                  &z,
    std::uint32_t           seed,
    const std::vector<int> &steps_per_level = {512, 256, 128},
    Array                  *p_sediment_map = nullptr,
    Array                  *p_discharge_map = nullptr,
    float                   world_extent_km = 40.f,
    float                   z_scale_km = 4.f,
    int                     samples = 8192,
    int                     maxage = 512,
    float                   lrate = 0.2f,
    float                   time_step = 10.f,
    float                   rainfall = 1.f,
    float                   evap_rate = 1e-4f,
    float                   gravity = 9.81f,
    float                   viscosity = 0.025f,
    float                   bed_shear = 0.01f,
    float                   crit_slope = 0.57f,
    float                   settle_rate = 0.1f,
    float                   thermal_rate = 2.5e-3f,
    float                   deposition_rate = 5e-3f,
    float                   suspension_rate = 2.5e-4f,
    float                   exit_slope = 0.01f);

/* --- Schott hydraulic erosion ---------------------------------------------- */

void hydraulic_schott(Array       &z,
                      int          iterations,
                      const Array &talus,
                      float        c_erosion = 1.f,
                      float        c_thermal = 0.1f,
                      float        c_deposition = 0.2f,
                      float        flow_acc_exponent = 0.8f,
                      float        flow_acc_exponent_depo = 0.8f,
                      float        flow_routing_exponent = 1.3f,
                      float        thermal_weight = 1.5f,
                      float        deposition_weight = 2.5f,
                      Array       *p_flow = nullptr);

void hydraulic_schott(Array       &z,
                      int          iterations,
                      const Array &talus,
                      const Array *p_mask,
                      float        c_erosion = 1.f,
                      float        c_thermal = 0.1f,
                      float        c_deposition = 0.2f,
                      float        flow_acc_exponent = 0.8f,
                      float        flow_acc_exponent_depo = 0.8f,
                      float        flow_routing_exponent = 1.3f,
                      float        thermal_weight = 1.5f,
                      float        deposition_weight = 2.5f,
                      Array       *p_flow = nullptr); ///< @overload

void hydraulic_schott_erosion(Array       &z,
                              int          iterations,
                              float        c_erosion = 1.f,
                              float        flow_acc_exponent = 0.8f,
                              float        flow_routing_exponent = 1.3f,
                              const Array *p_moisture_map = nullptr,
                              Array       *p_flow = nullptr);

/* --- Virtual-pipes hydraulic erosion --------------------------------------- */

void hydraulic_vpipes(Array       &z,
                      float        water_height = 1e-2f,
                      bool         maintain_water_volume = true,
                      float        evap_rate = 0.1f,
                      int          iterations = 50,
                      float        dt = 0.5f,
                      float        k_capacity = 0.5f,
                      float        k_erode = 0.001f,
                      float        k_depose = 0.01f,
                      float        k_discharge_exp = 1.f,
                      float        downcutting_max_depth_ratio = 10.f,
                      bool         flux_diffusion = true,
                      float        flux_diffusion_strength = 0.01f,
                      const Array *p_rain_map = nullptr,
                      Array       *p_water_depth = nullptr,
                      Array       *p_sediment = nullptr,
                      Array       *p_vel_u = nullptr,
                      Array       *p_vel_v = nullptr);

} // namespace hmap
