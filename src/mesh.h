// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "math3d.h"

namespace mi {

// A triangle mesh exactly as read from a file (no welding / cleanup applied).
struct Mesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> colors;       // empty, or one RGB (0..1) per position
    std::vector<uint32_t> indices;  // 3 per triangle
    std::string format;             // e.g. "Binary STL"
    std::vector<std::string> warnings;

    size_t triangleCount() const { return indices.size() / 3; }
};

}  // namespace mi
