cmake_minimum_required(VERSION 3.22)

# Standalone Emscripten project for the highmap-wasm port (AD-006).
# NEVER add_subdirectory() this from the upstream build; configure directly:
#   emcmake cmake -S HighMap/wasm -B build/wasm
# Requires only the Emscripten toolchain and the repo's git submodules.

project(highmap_wasm LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(HIGHMAP_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
set(HIGHMAP_SRC ${HIGHMAP_DIR}/src)
set(EXTERNAL_DIR ${CMAKE_CURRENT_LIST_DIR}/../../external)

# --- glm (header-only; a system package in the upstream build) -------------
include(FetchContent)
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)

# --- Eigen (header-only; used by the GSL replacement shim, AD-007) ---------
FetchContent_Declare(
  eigen
  GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
  GIT_TAG 3.4.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(eigen)

# --- vendored dependencies (interface include targets, mirroring external/) --

add_library(FastNoiseLite INTERFACE)
add_library(FastNoiseLite::FastNoiseLite ALIAS FastNoiseLite)
target_include_directories(FastNoiseLite INTERFACE ${EXTERNAL_DIR}/FastNoiseLite/Cpp)

add_library(delaunator INTERFACE)
add_library(delaunator::delaunator ALIAS delaunator)
target_include_directories(delaunator INTERFACE ${EXTERNAL_DIR}/delaunator/include)

# PointSampler is header-only in practice; its pinned layout nests the
# library one directory below the submodule root
add_library(point_sampler_headers INTERFACE)
target_include_directories(point_sampler_headers INTERFACE
  ${EXTERNAL_DIR}/PointSampler/PointSampler/include
)

target_include_directories(delaunator INTERFACE ${EXTERNAL_DIR}/nanoflann/include)
target_include_directories(delaunator INTERFACE ${EXTERNAL_DIR}/dkm/include)

# --- NoiseLib (parberry noise): compiled here without OpenMP -----------------
# (its own CMakeLists requires OpenMP, which the wasm build does not use)

set(NOISE_LIB_DIR ${EXTERNAL_DIR}/Noise/NoiseLib)
add_library(noise_lib STATIC
  ${NOISE_LIB_DIR}/source/math2d.cpp
  ${NOISE_LIB_DIR}/source/math3d.cpp
  ${NOISE_LIB_DIR}/source/perlin.cpp
  ${NOISE_LIB_DIR}/source/spline.cpp
  ${NOISE_LIB_DIR}/source/utils.cpp
)
target_include_directories(noise_lib PUBLIC
  ${EXTERNAL_DIR}/Noise
  ${NOISE_LIB_DIR}/include
)

# --- the curated highmap source set ------------------------------------------

include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)

add_executable(highmap_wasm
  ${HIGHMAP_WASM_SOURCES}
  ${CMAKE_CURRENT_LIST_DIR}/bindings.cpp
  ${CMAKE_CURRENT_LIST_DIR}/shims.cpp
)

set_target_properties(highmap_wasm PROPERTIES OUTPUT_NAME "highmap" SUFFIX ".js")

target_include_directories(highmap_wasm PRIVATE
  ${HIGHMAP_DIR}/include
  ${CMAKE_CURRENT_LIST_DIR}/stubs   # cv::Mat forward declarations (AD-007)
)

target_compile_definitions(highmap_wasm PRIVATE
  GLM_FORCE_SWIZZLE
  GLM_FORCE_INLINE
  GLM_ENABLE_EXPERIMENTAL
  _USE_MATH_DEFINES
)

target_link_libraries(highmap_wasm PRIVATE
  glm::glm
  Eigen3::Eigen
  FastNoiseLite::FastNoiseLite
  delaunator::delaunator
  point_sampler_headers
  noise_lib
)

# --- Emscripten flags (design D10) -------------------------------------------
# Pinned toolchain: Emscripten 6.0.13 (see scripts/build-wasm.sh).

if(WASM_ASAN)
  target_compile_options(highmap_wasm PRIVATE -fsanitize=address -g)
else()
  target_compile_options(highmap_wasm PRIVATE -Os -fwasm-exceptions)
endif()

target_link_options(highmap_wasm PRIVATE
  -Os
  -fwasm-exceptions
  "-lembind"
  "SHELL:-s MODULARIZE=1"
  "SHELL:-s EXPORT_ES6=1"
  "SHELL:-s EXPORT_NAME=HighmapFactory"
  "SHELL:-s ENVIRONMENT=node,web,worker"
  "SHELL:-s ALLOW_MEMORY_GROWTH=1"
  "SHELL:-s EXPORTED_RUNTIME_METHODS=wasmMemory"
)

if(WASM_ASAN)
  target_link_options(highmap_wasm PRIVATE
    -fsanitize=address
    "SHELL:-s INITIAL_MEMORY=256MB"
    "SHELL:-s ABORT_ON_WASM_EXCEPTIONS=1"
  )
endif()
