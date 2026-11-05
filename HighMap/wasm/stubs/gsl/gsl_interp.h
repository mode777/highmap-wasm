/* GSL stub for the wasm build (AD-007).
 *
 * interpolate1d.hpp (pulled in via header chains) declares members holding
 * gsl_spline / gsl_interp_accel pointers. The GSL-backed translation unit
 * (interpolate/interpolate1d.cpp) is excluded from the curated wasm source
 * list; these opaque forward declarations are enough for the headers to
 * parse. */

#pragma once

#include <cstddef>

typedef struct
{
  size_t size;
} gsl_interp_accel;

typedef struct
{
  size_t size;
} gsl_interp;

typedef struct
{
  gsl_interp *interp;
} gsl_spline;
