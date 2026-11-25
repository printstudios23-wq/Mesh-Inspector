// SPDX-License-Identifier: Apache-2.0
#include <cctype>
#include <cmath>

#include "loaders.h"

namespace mi {

using namespace parse;

bool loadObj(const char* data, size_t size, Mesh& out, std::string& error) {
    const char* p = data;
    const char* end = data + size;

    std::vector<long long> face;
    size_t badFaces = 0;
    size_t badTokens = 0;
    bool anyColor = false;

    while (p < end) {
        p = skipBlanks(p, end);
        if (p >= end) break;

        if (p[0] == 'v' && (p[1] == ' ' || p[1] == '\t')) {
            ++p;
            float x, y, z;
            if (readFloat(p, end, x) && readFloat(p, end, y) && readFloat(p, end, z)) {
                out.positions.push_back({x, y, z});
                // Optional vertex colour extension: "v x y z r g b"
                float r, g, b;
                if (readFloat(p, end, r) && readFloat(p, end, g) && readFloat(p, end, b)) {
                    if (!anyColor) {
                        anyColor = true;
                        out.colors.assign(out.positions.size() - 1, Vec3{1, 1, 1});
                    }
                    out.colors.push_back({r, g, b});
                } else if (anyColor) {
                    out.colors.push_back({1, 1, 1});
                }
            } else {
                ++badTokens;
                // keep indices aligned with the file even if a vertex line is broken
                out.positions.push_back({std::nanf(""), std::nanf(""), std::nanf("")});
                if (anyColor) out.colors.push_back({1, 1, 1});
            }
        } else if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
            ++p;
            face.clear();
            bool bad = false;
            const long long count = static_cast<long long>(out.positions.size());
            while (true) {
                p = skipBlanks(p, end);
                if (atLineEnd(p, end)) break;
                char* q = nullptr;
                long long idx = std::strtoll(p, &q, 10);
                if (q == p) {
                    bad = true;
                    break;
                }
                p = q;
                // skip "/vt/vn" part
                while (p < end && static_cast<unsigned char>(*p) > ' ') ++p;
                idx = idx < 0 ? count + idx : idx - 1;  // OBJ is 1-based, negatives are relative
                if (idx < 0 || idx >= count) {
                    bad = true;
                    break;
                }
                face.push_back(idx);
            }
            if (bad || face.size() < 3) {
                ++badFaces;
            } else {
                // Triangle fan; exact for triangles and convex polygons.
                for (size_t i = 1; i + 1 < face.size(); ++i) {
                    out.indices.push_back(static_cast<uint32_t>(face[0]));
                    out.indices.push_back(static_cast<uint32_t>(face[i]));
                    out.indices.push_back(static_cast<uint32_t>(face[i + 1]));
                }
            }
        }
        p = nextLine(p, end);
    }

    if (out.positions.empty()) {
        error = "No vertices found - this does not look like a valid OBJ file.";
        return false;
    }
    if (badFaces) out.warnings.push_back(std::to_string(badFaces) + " face(s) skipped (invalid or out-of-range vertex index).");
    if (badTokens) out.warnings.push_back(std::to_string(badTokens) + " vertex line(s) could not be parsed.");
    if (out.indices.empty()) out.warnings.push_back("File contains no faces (point data only).");
    out.format = "Wavefront OBJ";
    return true;
}

}  // namespace mi
