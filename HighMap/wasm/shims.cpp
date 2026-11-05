/* Replacement shims for excluded upstream translation units (AD-007).
 *
 * Kept translation units (e.g. array.cpp's filename constructor) reference
 * io functions whose real implementations live in OpenCV-dependent files
 * that are excluded from the wasm build. These stubs satisfy the linker and
 * fail loudly at runtime if ever reached. */

#include <Eigen/Dense>

#include <stdexcept>
#include <string>

#include "highmap/array.hpp"
#include "highmap/convolve.hpp"

namespace hmap
{

Array read_to_array(const std::string &fname, bool flip_j, bool remap)
{
  (void)flip_j;
  (void)remap;
  throw std::runtime_error(
      "highmap-wasm: file input is not available in the WebAssembly build ("
      + fname + ")");
}

/* Eigen-based replacement for the GSL-backed convolve2d_svd (research doc
 * §3: "GSL -> replace with Eigen"). Same semantics as
 * src/convolve/convolve2d_svd.cpp: rank-truncated separable convolution
 * using the SVD of the convolution kernel. Eigen's JacobiSVD, like GSL's
 * SV_decomp, returns singular values in descending order. */
Array convolve2d_svd(const Array &array, const Array &kernel, int rank)
{
  if (array.shape.x < 1 || array.shape.y < 1 || kernel.shape.x < 1 ||
      kernel.shape.y < 1)
    return Array(array.shape);
  if (kernel.ptp() == 0.f) return array;

  if (kernel.shape.x < kernel.shape.y)
  {
    // mirrors the upstream warning path
    return Array(array.shape);
  }

  Array array_out = Array(array.shape);

  Eigen::MatrixXd ker(kernel.shape.x, kernel.shape.y);
  for (int j = 0; j < kernel.shape.y; j++)
    for (int i = 0; i < kernel.shape.x; i++)
      ker(i, j) = static_cast<double>(kernel(i, j));

  Eigen::JacobiSVD<Eigen::MatrixXd> svd(
      ker,
      Eigen::ComputeThinU | Eigen::ComputeThinV);

  for (int p = 0; p < std::min(rank, kernel.shape.y); p++)
  {
    std::vector<float> ki(kernel.shape.x);
    std::vector<float> kj(kernel.shape.y);

    for (int i = 0; i < kernel.shape.x; i++)
      ki[i] = static_cast<float>(svd.matrixU()(i, p));

    for (int j = 0; j < kernel.shape.y; j++)
      kj[j] = static_cast<float>(svd.matrixV()(j, p));

    Array c2d = convolve1d_i(array, ki);
    c2d = convolve1d_j(c2d, kj);

    array_out += static_cast<float>(svd.singularValues()(p)) * c2d;
  }

  return array_out;
}

} // namespace hmap
