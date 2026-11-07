/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */

/* highmap-wasm CPU ports of the advection family.
 *
 * Upstream ships these algorithms as OpenCL kernels only
 * (src/gpu_opencl/kernels/advection_particle.cl and advection_warp.cl,
 * launched from src/transform/advection_{particle,warp}_gpu.cpp) and declares
 * them exclusively in namespace hmap::gpu (include/highmap/transform.hpp).
 * This header declares the CPU counterparts implemented in
 * src/transform/advection_cpu.cpp, which transcribe those kernels, so the
 * family can run without OpenCL (the highmap-wasm target) and can be validated
 * against the GPU output on the desktop build
 * (tests/src/test_rifts_advection_cpu.cpp).
 *
 * The overloads mirror the public hmap::gpu::* declarations one for one, so a
 * call site moves between the families by dropping the namespace prefix. All
 * parameter types and defaults are copied verbatim from
 * include/highmap/transform.hpp. There are six advection_particle overloads
 * (z-driven single-field and std::vector<Array>, each with and without
 * iterations, plus the explicit dx/dy forms) and two advection_warp overloads
 * (z-driven and explicit dx/dy).
 *
 * Rebase rule: if upstream ever declares or defines CPU implementations of
 * these functions (in highmap/transform.hpp or elsewhere), delete this header
 * and src/transform/advection_cpu.cpp. Duplicate definitions fail loudly at
 * link time; duplicate default arguments fail loudly at compile time. Bindings
 * and tests speak only the hmap:: names, so no call site changes. See AD-026 in
 * docs/decisions.md.
 */
#pragma once

#include <cstdint>
#include <vector>

#include "highmap/array.hpp"

namespace hmap
{

/* --- ports of the GPU-only particle advection ----------------------------- */

Array advection_particle(const Array  &z,
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
                         const Array  *p_mask = nullptr);

std::vector<Array> advection_particle(const Array              &z,
                                      const std::vector<Array> &advected_fields,
                                      int                       iterations,
                                      int                       nparticles,
                                      std::uint32_t             seed,
                                      bool                      reverse = false,
                                      bool         post_filter = true,
                                      float        post_filter_sigma = 0.125f,
                                      float        advection_length = 0.1f,
                                      float        value_persistence = 0.99f,
                                      float        inertia = 0.f,
                                      const Array *p_advection_mask = nullptr,
                                      const Array *p_mask = nullptr);

Array advection_particle(const Array  &z,
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
                         const Array  *p_mask = nullptr);

std::vector<Array> advection_particle(const Array              &z,
                                      const std::vector<Array> &advected_fields,
                                      int                       nparticles,
                                      std::uint32_t             seed,
                                      bool                      reverse = false,
                                      bool         post_filter = true,
                                      float        post_filter_sigma = 0.125f,
                                      float        advection_length = 0.1f,
                                      float        value_persistence = 0.99f,
                                      float        inertia = 0.f,
                                      const Array *p_advection_mask = nullptr,
                                      const Array *p_mask = nullptr);

Array advection_particle(const Array  &dx,
                         const Array  &dy,
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
                         const Array  *p_mask = nullptr);

std::vector<Array> advection_particle(const Array              &dx,
                                      const Array              &dy,
                                      const std::vector<Array> &advected_fields,
                                      int                       nparticles,
                                      std::uint32_t             seed,
                                      bool                      reverse = false,
                                      bool         post_filter = true,
                                      float        post_filter_sigma = 0.125f,
                                      float        advection_length = 0.1f,
                                      float        value_persistence = 0.99f,
                                      float        inertia = 0.f,
                                      const Array *p_advection_mask = nullptr,
                                      const Array *p_mask = nullptr);

/* --- ports of the GPU-only warp advection --------------------------------- */

Array advection_warp(const Array &z,
                     const Array &advected_field,
                     float        advection_length = 0.1f,
                     float        value_persistence = 0.9f,
                     const Array *p_mask = nullptr);

Array advection_warp(const Array &z,
                     const Array &advected_field,
                     const Array &dx,
                     const Array &dy,
                     float        advection_length = 0.1f,
                     float        value_persistence = 0.9f,
                     const Array *p_mask = nullptr);

} // namespace hmap
