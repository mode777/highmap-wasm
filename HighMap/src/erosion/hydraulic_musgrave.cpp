/* Copyright (c) 2023 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */
#include <algorithm>
#include <cstdint>
#include <vector>

#include "highmap/array.hpp"
#include "highmap/boundary.hpp"
#include "highmap/erosion.hpp"
#include "highmap/filters.hpp"
#include "highmap/internal/validation.hpp"
#include "highmap/primitives/functions.hpp"

namespace hmap
{

#define LAPLACE_PERIOD 10
#define LAPLACE_SIGMA 0.05f
#define LAPLACE_ITERATIONS 1

//----------------------------------------------------------------------
// Main operator
//----------------------------------------------------------------------

void hydraulic_musgrave(Array &z,
                        Array &moisture_map,
                        int    iterations,
                        float  c_capacity,
                        float  c_erosion,
                        float  c_deposition,
                        float  water_level,
                        float  evap_rate)
{
  if (!validate_non_empty(z) || !validate_same_shape(z, moisture_map)) return;

  Array s = constant(z.shape);          // sediment level
  Array w = water_level * moisture_map; // backup initial moisture map

  std::vector<int>    di = HMAP_DI;
  std::vector<int>    dj = HMAP_DJ;
  std::vector<float>  c = HMAP_CD_INV;
  const std::uint32_t nb = di.size();

  for (int it = 0; it < iterations; it++)
  {
    w = (1 - evap_rate) * w + evap_rate * moisture_map * water_level;

    // modify neighbor search at each iterations to limit numerical
    // artifacts
    std::rotate(di.begin(), di.begin() + 1, di.end());
    std::rotate(dj.begin(), dj.begin() + 1, dj.end());
    std::rotate(c.begin(), c.begin() + 1, c.end());

    for (int j = 1; j < z.shape.y - 1; j++)
      for (int i = 1; i < z.shape.x - 1; i++)
        for (std::uint32_t k = 0; k < nb; k++) // loop over 1st neighbors
        {
          int   p = i + di[k];
          int   q = j + dj[k];
          float dw = std::min(w(i, j),
                              (w(i, j) + z(i, j) - w(p, q) - z(p, q)) * c[k]);

          if (dw > 0.f)
          {
            // water transfer
            w(i, j) -= dw;
            w(p, q) += dw;

            // sediment capacity
            float cs = c_capacity * dw;

            if (s(i, j) >= cs)
            {
              // deposition
              float ds = c_deposition * (s(i, j) - cs);
              s(i, j) -= ds;
              z(i, j) += ds;
            }
            else
            {
              // erosion
              float ds = c_erosion * (cs - s(i, j));
              s(i, j) += ds;
              z(i, j) -= ds;
            }

            // sediment transport with water
            // [highmap-wasm fork] Guard against w == 0: dw <= w is enforced
            // by the min() above, so w can hit exact zero here, and the
            // original s * dw / w becomes 0/0 = NaN. With no water left,
            // no sediment is transported (the zero-flux limit). Flagged as
            // the fork's only upstream edit; see AD-014 in
            // docs/decisions.md. Identical code runs in the desktop build.
            if (w(i, j) > 0.f)
            {
              float ds_transport = s(i, j) * dw / w(i, j);
              s(i, j) -= ds_transport;
              s(p, q) += ds_transport;
            }
          }
        }

    // regularize the surface to avoid high frequency numerical
    // artifacts
    if ((it % LAPLACE_PERIOD == 0) && (it != 0))
    {
      hmap::laplace(z, LAPLACE_SIGMA, LAPLACE_ITERATIONS);
    }
  }
}

void hydraulic_musgrave(Array &z,
                        int    iterations,
                        float  c_capacity,
                        float  c_erosion,
                        float  c_deposition,
                        float  water_level,
                        float  evap_rate)
{
  if (!validate_non_empty(z)) return;

  Array moisture_map = constant(z.shape, 1.f);
  hydraulic_musgrave(z,
                     moisture_map,
                     iterations,
                     c_capacity,
                     c_erosion,
                     c_deposition,
                     water_level,
                     evap_rate);
}

} // namespace hmap
