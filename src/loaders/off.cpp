// SPDX-License-Identifier: Apache-2.0
#include <cstring>

#include "loaders.h"

namespace mi {

using namespace parse;

namespace {

// Returns the start of the next line that holds data (skips blank and '#' comment lines).
const char* nextDataLine(const char* p, const char* end) {
    while (p < end) {
        const char* s = skipBlanks(p, end);
        if (!atLineEnd(s, end) && *s != '#') return s;
        p = nextLine(p, end);
    }
    return end;
}

}  // namespace

bool loadOff(const char* data, size_t size, Mesh& out, std::string& error) {
    const char* end = data + size;
    const char* p = nextDataLine(data, end);
    if (p >= end) {
        error = "Empty OFF file.";
        return false;
    }

    // Header keyword: [ST][C][N][4][n]OFF
    const char* kw = p;
    while (p < end && static_cast<unsigned char>(*p) > ' ') ++p;
    const std::string keyword(kw, p);
    if (keyword.size() < 3 || keyword.compare(keyword.size() - 3, 3, "OFF") != 0) {
        error = "Missing 'OFF' header.";
        return false;
    }
    const bool hasColor = keyword.find('C') != std::string::npos;
    const bool hasNormal = keyword.find('N') != std::string::npos;

    long long nv = 0, nf = 0, ne = 0;
    // Counts may be on the same line as the keyword or on the next data line.
    const char* q = p;
    if (!readInt(q, end, nv)) {
        q = nextDataLine(nextLine(p, end), end);
        if (!readInt(q, end, nv)) {
            error = "OFF header is missing the vertex count.";
            return false;
        }
    }
    if (!readInt(q, end, nf) || !readInt(q, end, ne)) {
        error = "OFF header is missing the face count.";
        return false;
    }
    if (nv < 0 || nf < 0 || nv > 500000000 || nf > 500000000) {
        error = "OFF header has implausible counts.";
        return false;
    }
    p = nextLine(q, end);

    out.positions.reserve(static_cast<size_t>(nv));
    if (hasColor) out.colors.reserve(static_cast<size_t>(nv));
    for (long long i = 0; i < nv; ++i) {
        p = nextDataLine(p, end);
        if (p >= end) {
            error = "OFF file ends before all vertices were read.";
            return false;
        }
        const char* l = p;
        float v[3];
        for (float& c : v) {
            if (!readFloat(l, end, c)) {
                error = "Malformed vertex line in OFF file.";
                return false;
            }
        }
        out.positions.push_back({v[0], v[1], v[2]});
        if (hasColor) {
            if (hasNormal) {
                float skip;
                for (int k = 0; k < 3; ++k) readFloat(l, end, skip);
            }
            float c[4] = {1, 1, 1, 1};
            int got = 0;
            while (got < 4 && readFloat(l, end, c[got])) ++got;
            // Colours are either 0..1 floats or 0..255 integers.
            const float scale = (c[0] > 1.0f || c[1] > 1.0f || c[2] > 1.0f) ? 1.0f / 255.0f : 1.0f;
            out.colors.push_back(got >= 3 ? Vec3{c[0] * scale, c[1] * scale, c[2] * scale} : Vec3{1, 1, 1});
        }
        p = nextLine(p, end);
    }

    size_t badFaces = 0;
    std::vector<long long> face;
    for (long long i = 0; i < nf; ++i) {
        p = nextDataLine(p, end);
        if (p >= end) {
            out.warnings.push_back("OFF file ends before all faces were read.");
            break;
        }
        const char* l = p;
        long long n = 0;
        if (!readInt(l, end, n) || n < 3) {
            ++badFaces;
            p = nextLine(p, end);
            continue;
        }
        face.clear();
        bool bad = false;
        for (long long k = 0; k < n; ++k) {
            long long idx;
            if (!readInt(l, end, idx) || idx < 0 || idx >= nv) {
                bad = true;
                break;
            }
            face.push_back(idx);
        }
        if (bad) {
            ++badFaces;
        } else {
            for (size_t k = 1; k + 1 < face.size(); ++k) {
                out.indices.push_back(static_cast<uint32_t>(face[0]));
                out.indices.push_back(static_cast<uint32_t>(face[k]));
                out.indices.push_back(static_cast<uint32_t>(face[k + 1]));
            }
        }
        p = nextLine(p, end);
    }
    if (badFaces) out.warnings.push_back(std::to_string(badFaces) + " face(s) skipped (invalid vertex index).");
    out.format = "OFF";
    return true;
}

}  // namespace mi
