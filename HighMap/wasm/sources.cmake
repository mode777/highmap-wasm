# Curated CPU-only source list for the highmap_wasm target (AD-006, AD-007).
#
# Explicit and intentionally minimal-per-surface: upstream files enter this
# list only when a bound function needs them. Excluded by policy: gpu_opencl/,
# *_gpu.cpp, export/, OpenCV/assimp/GSL/PointSampler-cpp dependents, io.cpp
# (shimmed in shims.cpp instead). Expect upstream renames to fail loudly here
# at rebase time - that is the point of an explicit list.

set(HIGHMAP_WASM_SOURCES

  # --- core Array ----------------------------------------------------------
  ${HIGHMAP_SRC}/array/array.cpp
  ${HIGHMAP_SRC}/array/methods.cpp

  # --- operator layer (fill_array_using_xy_function) -----------------------
  ${HIGHMAP_SRC}/operator/fill_array.cpp

  # --- math ----------------------------------------------------------------
  ${HIGHMAP_SRC}/math/core.cpp

  # --- noise function objects + factory ------------------------------------
  ${HIGHMAP_SRC}/functions/functions.cpp
  ${HIGHMAP_SRC}/functions/fbm_functions.cpp
  ${HIGHMAP_SRC}/functions/field_function.cpp
  ${HIGHMAP_SRC}/functions/noise_functions.cpp
  ${HIGHMAP_SRC}/functions/parberry_function.cpp

  # --- coherent noise (hmap::noise family) ---------------------------------
  ${HIGHMAP_SRC}/primitives/coherent_noise/noise.cpp

  # --- dependencies of the noise chain --------------------------------------
  ${HIGHMAP_SRC}/primitives/random/white.cpp
  ${HIGHMAP_SRC}/geometry/grid.cpp
  ${HIGHMAP_SRC}/geometry/point_sampling.cpp
  ${HIGHMAP_SRC}/range/range.cpp

  # --- erosion (CPU twins) ---------------------------------------------------
  ${HIGHMAP_SRC}/erosion/hydraulic_musgrave.cpp
  ${HIGHMAP_SRC}/erosion/hydraulic_stream.cpp

  # --- filters / boundary / shading (pyHighMap surface) ----------------------
  ${HIGHMAP_SRC}/filters/filters.cpp
  ${HIGHMAP_SRC}/boundary/boundary.cpp
  ${HIGHMAP_SRC}/shadows/shadows.cpp
  ${HIGHMAP_SRC}/transform/warp.cpp
  ${HIGHMAP_SRC}/transform/transform.cpp

  # --- math (all CPU, header-declared deps of the chain above) ---------------
  ${HIGHMAP_SRC}/math/core.cpp
  ${HIGHMAP_SRC}/math/distance_functions.cpp
  ${HIGHMAP_SRC}/math/erosion_profile_function.cpp
  ${HIGHMAP_SRC}/math/math.cpp
  ${HIGHMAP_SRC}/math/phasor_profile_function.cpp
  ${HIGHMAP_SRC}/math/radial_profile_function.cpp

  # --- operator / geometry / primitives deps of the chain --------------------
  ${HIGHMAP_SRC}/operator/compare.cpp
  ${HIGHMAP_SRC}/operator/operator.cpp
  ${HIGHMAP_SRC}/operator/vector.cpp
  ${HIGHMAP_SRC}/primitives/functions/cone.cpp
  ${HIGHMAP_SRC}/primitives/functions/primitives.cpp
  ${HIGHMAP_SRC}/hydrology/flow_accumulation_dinf.cpp
  ${HIGHMAP_SRC}/convolve/convolve.cpp
  ${HIGHMAP_SRC}/kernels/kernels.cpp
  ${HIGHMAP_SRC}/local_metrics/local_metrics.cpp
  ${HIGHMAP_SRC}/local_metrics/local_metrics_wrapper.cpp
  ${HIGHMAP_SRC}/gradient/gradient.cpp

  # --- interpolate (pulled in by Array methods) -----------------------------
  ${HIGHMAP_SRC}/interpolate/interpolate2d.cpp
  ${HIGHMAP_SRC}/interpolate/interpolate_array.cpp
)
