// SPDX-License-Identifier: Apache-2.0
// Internal interface of the individual format loaders.
//
// The byte-based loaders receive a buffer that is guaranteed to be followed by a '\0'
// (data[size] == 0), so C string functions may safely run to the end of the buffer.
#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>

#include "../mesh.h"

namespace mi {

bool loadObj(const char* data, size_t size, Mesh& out, std::string& error);
bool loadStl(const char* data, size_t size, Mesh& out, std::string& error);
bool loadPly(const char* data, size_t size, Mesh& out, std::string& error);
bool loadOff(const char* data, size_t size, Mesh& out, std::string& error);
bool loadThreeMf(const char* data, size_t size, Mesh& out, std::string& error);
bool loadGltf(const std::filesystem::path& path, Mesh& out, std::string& error);

namespace parse {

inline const char* skipBlanks(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    return p;
}
inline const char* skipWhitespace(const char* p, const char* end) {
    while (p < end && static_cast<unsigned char>(*p) <= ' ') ++p;
    return p;
}
inline const char* endOfLine(const char* p, const char* end) {
    while (p < end && *p != '\n' && *p != '\r') ++p;
    return p;
}
inline const char* nextLine(const char* p, const char* end) {
    p = endOfLine(p, end);
    while (p < end && (*p == '\n' || *p == '\r')) ++p;
    return p;
}
inline bool atLineEnd(const char* p, const char* end) { return p >= end || *p == '\n' || *p == '\r'; }

// Reads a float on the current line. Returns false at end of line or on garbage.
inline bool readFloat(const char*& p, const char* end, float& out) {
    p = skipBlanks(p, end);
    if (atLineEnd(p, end)) return false;
    char* q = nullptr;
    out = std::strtof(p, &q);
    if (q == p) return false;
    p = q;
    return true;
}
inline bool readDouble(const char*& p, const char* end, double& out) {
    p = skipBlanks(p, end);
    if (atLineEnd(p, end)) return false;
    char* q = nullptr;
    out = std::strtod(p, &q);
    if (q == p) return false;
    p = q;
    return true;
}
inline bool readInt(const char*& p, const char* end, long long& out) {
    p = skipBlanks(p, end);
    if (atLineEnd(p, end)) return false;
    char* q = nullptr;
    out = std::strtoll(p, &q, 10);
    if (q == p) return false;
    p = q;
    return true;
}
inline bool startsWith(const char* p, const char* end, const char* word) {
    for (; *word; ++word, ++p)
        if (p >= end || *p != *word) return false;
    return true;
}

}  // namespace parse
}  // namespace mi
