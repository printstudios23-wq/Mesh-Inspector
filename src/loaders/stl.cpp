// SPDX-License-Identifier: Apache-2.0
#include <cstring>

#include "loaders.h"

namespace mi {

using namespace parse;

namespace {

float readLeFloat(const unsigned char* p) {
    // STL is little-endian; assemble bytes so this also works on big-endian hosts.
    uint32_t v = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
                 (static_cast<uint32_t>(p[3]) << 24);
    float f;
    std::memcpy(&f, &v, sizeof f);
    return f;
}

bool loadBinary(const unsigned char* d, size_t size, uint32_t triCount, Mesh& out) {
    (void)size;
    out.positions.reserve(static_cast<size_t>(triCount) * 3);
    out.indices.reserve(static_cast<size_t>(triCount) * 3);
    const unsigned char* rec = d + 84;
    for (uint32_t t = 0; t < triCount; ++t, rec += 50) {
        for (int v = 0; v < 3; ++v) {
            const unsigned char* q = rec + 12 + v * 12;  // skip the facet normal
            out.positions.push_back({readLeFloat(q), readLeFloat(q + 4), readLeFloat(q + 8)});
            out.indices.push_back(static_cast<uint32_t>(out.positions.size() - 1));
        }
    }
    out.format = "Binary STL";
    return true;
}

}  // namespace

bool loadStl(const char* data, size_t size, Mesh& out, std::string& error) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(data);

    // Binary STL: 80 byte header, uint32 triangle count, 50 bytes per triangle.
    // Some binary files start with "solid", so the size check comes first.
    if (size >= 84) {
        const uint32_t n = static_cast<uint32_t>(bytes[80]) | (static_cast<uint32_t>(bytes[81]) << 8) |
                           (static_cast<uint32_t>(bytes[82]) << 16) | (static_cast<uint32_t>(bytes[83]) << 24);
        const uint64_t expected = 84ull + 50ull * n;
        if (expected == size) return loadBinary(bytes, size, n, out);
        const bool looksAscii = startsWith(skipWhitespace(data, data + size), data + size, "solid");
        if (!looksAscii && expected < size) {
            out.warnings.push_back("Binary STL has trailing bytes after the last triangle.");
            return loadBinary(bytes, size, n, out);
        }
        if (!looksAscii && expected > size) {
            // truncated: load what is there
            const uint32_t avail = static_cast<uint32_t>((size - 84) / 50);
            out.warnings.push_back("Binary STL is truncated: header says " + std::to_string(n) + " triangles, only " +
                                   std::to_string(avail) + " present.");
            return loadBinary(bytes, size, avail, out);
        }
    }

    // ASCII STL
    const char* p = data;
    const char* end = data + size;
    while (p < end) {
        p = skipBlanks(p, end);
        if (startsWith(p, end, "vertex")) {
            p += 6;
            float x, y, z;
            if (readFloat(p, end, x) && readFloat(p, end, y) && readFloat(p, end, z)) {
                out.positions.push_back({x, y, z});
                out.indices.push_back(static_cast<uint32_t>(out.positions.size() - 1));
            }
        }
        p = nextLine(p, end);
    }
    if (out.positions.empty()) {
        error = "No triangles found - this does not look like a valid STL file.";
        return false;
    }
    if (out.positions.size() % 3 != 0) {
        const size_t extra = out.positions.size() % 3;
        out.positions.resize(out.positions.size() - extra);
        out.indices.resize(out.indices.size() - extra);
        out.warnings.push_back("ASCII STL ended in the middle of a facet; the incomplete facet was dropped.");
    }
    out.format = "ASCII STL";
    return true;
}

}  // namespace mi
