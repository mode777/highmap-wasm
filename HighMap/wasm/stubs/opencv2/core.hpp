/* OpenCV stub for the wasm build (AD-007).
 *
 * Upstream headers (array.hpp, methods.cpp, ...) include <opencv2/core.hpp>,
 * but only reference cv::Mat by pointer/reference in declarations. The
 * OpenCV-dependent translation units (io.cpp, opencv_wrapper.cpp, export/)
 * are excluded from the curated wasm source list, so a forward declaration
 * is all that is needed for the remaining units to parse. */

#pragma once

// Default-argument constants used in upstream declarations (to_png etc.);
// the corresponding definitions are excluded from the wasm build.
#define CV_8U 0
#define CV_16U 2
#define CV_32F 5

namespace cv
{
class Mat;
}
