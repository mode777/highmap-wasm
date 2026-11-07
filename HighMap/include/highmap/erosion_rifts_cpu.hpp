/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */

/* highmap-wasm CPU port of the rifts erosion.
 *
 * Upstream ships rifts as an OpenCL kernel only
 * (src/gpu_opencl/kernels/rifts.cl, launched from src/erosion/rifts_gpu.cpp)
 * and declares it exclusively in namespace hmap::gpu
 * (include/highmap/erosion.hpp). This header declares the CPU counterpart
 * implemented in src/erosion/rifts_cpu.cpp, which transcribes that kernel, so
 * rifts can run without OpenCL (the highmap-wasm target) and can be validated
 * against the GPU output on the desktop build
 * (tests/src/test_rifts_advection_cpu.cpp).
 *
 * The signature mirrors the public hmap::gpu::rifts declaration one for one,
 * so a call site moves between the families by dropping the namespace prefix.
 * All parameter types and defaults are copied verbatim from
 * include/highmap/erosion.hpp.
 *
 * Rebase rule: if upstream ever declares or defines a CPU implementation of
 * rifts (in highmap/erosion.hpp or elsewhere), delete this header and
 * src/erosion/rifts_cpu.cpp. Duplicate definitions fail loudly at link time;
 * duplicate default arguments fail loudly at compile time. Bindings and tests
 * speak only the hmap:: name, so no call site changes. See AD-026 in
 * docs/decisions.md.
 */
#pragma once

#include <cstdint>

#include "highmap/array.hpp"

namespace hmap
{

/* --- port of the GPU-only kernel ------------------------------------------ */

void rifts(Array           &z,
           const glm::vec2 &kw,
           float            angle, // degs
           float            amplitude,
           std::uint32_t    seed,
           float            elevation_noise_shift = 0.f,
           float            k_smooth_bottom = 0.05f,
           float            k_smooth_top = 0.05f,
           float            radial_spread_amp = 0.2f,
           float            elevation_noise_amp = 0.1f,
           float            clamp_vmin = 0.f,
           float            remap_vmin = 0.f,
           bool             apply_mask = true,
           bool             reverse_mask = false,
           float            mask_gamma = 1.f,
           const Array     *p_noise_x = nullptr,
           const Array     *p_noise_y = nullptr,
           const Array     *p_mask = nullptr,
           const glm::vec2 &center = {0.5f, 0.5f},
           const glm::vec4 &bbox = {0.f, 1.f, 0.f, 1.f});

} // namespace hmap
