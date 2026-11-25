// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cstring>

#include "loaders.h"

namespace mi {

using namespace parse;

namespace {

enum class Type { Int8, UInt8, Int16, UInt16, Int32, UInt32, Float32, Float64, Invalid };

Type parseType(const std::string& s) {
    if (s == "char" || s == "int8") return Type::Int8;
    if (s == "uchar" || s == "uint8") return Type::UInt8;
    if (s == "short" || s == "int16") return Type::Int16;
    if (s == "ushort" || s == "uint16") return Type::UInt16;
    if (s == "int" || s == "int32") return Type::Int32;
    if (s == "uint" || s == "uint32") return Type::UInt32;
    if (s == "float" || s == "float32") return Type::Float32;
    if (s == "double" || s == "float64") return Type::Float64;
    return Type::Invalid;
}

int typeSize(Type t) {
    switch (t) {
        case Type::Int8: case Type::UInt8: return 1;
        case Type::Int16: case Type::UInt16: return 2;
        case Type::Int32: case Type::UInt32: case Type::Float32: return 4;
        case Type::Float64: return 8;
        default: return 0;
    }
}

struct Property {
    std::string name;
    Type type = Type::Invalid;
    bool isList = false;
    Type countType = Type::Invalid;
};

struct Element {
    std::string name;
    size_t count = 0;
    std::vector<Property> props;
};

enum class Format { Ascii, BinaryLE, BinaryBE };

// Reads scalars from either the ASCII or the binary body of a PLY file.
class Reader {
public:
    Reader(const char* p, const char* end, Format f) : p_(p), end_(end), fmt_(f) {}

    bool ok() const { return ok_; }

    double scalar(Type t) {
        if (fmt_ == Format::Ascii) {
            p_ = skipWhitespace(p_, end_);
            char* q = nullptr;
            const double v = std::strtod(p_, &q);
            if (q == p_) {
                ok_ = false;
                return 0;
            }
            p_ = q;
            return v;
        }
        const int n = typeSize(t);
        if (end_ - p_ < n) {
            ok_ = false;
            return 0;
        }
        unsigned char b[8];
        std::memcpy(b, p_, static_cast<size_t>(n));
        p_ += n;
        if (fmt_ == Format::BinaryBE) std::reverse(b, b + n);
        // b is now little-endian; assemble explicitly so the host byte order does not matter.
        uint64_t u = 0;
        for (int i = n - 1; i >= 0; --i) u = (u << 8) | b[i];
        switch (t) {
            case Type::Int8: return static_cast<int8_t>(u);
            case Type::UInt8: return static_cast<uint8_t>(u);
            case Type::Int16: return static_cast<int16_t>(u);
            case Type::UInt16: return static_cast<uint16_t>(u);
            case Type::Int32: return static_cast<int32_t>(u);
            case Type::UInt32: return static_cast<uint32_t>(u);
            case Type::Float32: {
                const uint32_t w = static_cast<uint32_t>(u);
                float f;
                std::memcpy(&f, &w, 4);
                return f;
            }
            case Type::Float64: {
                double d;
                std::memcpy(&d, &u, 8);
                return d;
            }
            default: ok_ = false; return 0;
        }
    }

private:
    const char* p_;
    const char* end_;
    Format fmt_;
    bool ok_ = true;
};

}  // namespace

bool loadPly(const char* data, size_t size, Mesh& out, std::string& error) {
    const char* p = data;
    const char* end = data + size;

    auto readLine = [&](std::string& line) {
        if (p >= end) return false;
        const char* e = endOfLine(p, end);
        line.assign(p, e);
        p = nextLine(e, end);
        return true;
    };

    std::string line;
    if (!readLine(line) || line.compare(0, 3, "ply") != 0) {
        error = "Missing 'ply' magic - not a PLY file.";
        return false;
    }

    Format fmt = Format::Ascii;
    std::vector<Element> elements;
    bool headerDone = false;
    while (readLine(line)) {
        const char* l = line.data();
        const char* le = l + line.size();
        std::vector<std::string> tok;
        while (true) {
            l = skipBlanks(l, le);
            if (l >= le) break;
            const char* s = l;
            while (l < le && *l != ' ' && *l != '\t') ++l;
            tok.emplace_back(s, l);
        }
        if (tok.empty()) continue;
        if (tok[0] == "end_header") {
            headerDone = true;
            break;
        } else if (tok[0] == "format" && tok.size() >= 2) {
            if (tok[1] == "ascii") fmt = Format::Ascii;
            else if (tok[1] == "binary_little_endian") fmt = Format::BinaryLE;
            else if (tok[1] == "binary_big_endian") fmt = Format::BinaryBE;
            else {
                error = "Unknown PLY format '" + tok[1] + "'.";
                return false;
            }
        } else if (tok[0] == "element" && tok.size() >= 3) {
            Element e;
            e.name = tok[1];
            e.count = static_cast<size_t>(std::strtoull(tok[2].c_str(), nullptr, 10));
            elements.push_back(std::move(e));
        } else if (tok[0] == "property" && !elements.empty()) {
            Property pr;
            if (tok.size() >= 5 && tok[1] == "list") {
                pr.isList = true;
                pr.countType = parseType(tok[2]);
                pr.type = parseType(tok[3]);
                pr.name = tok[4];
                if (pr.countType == Type::Invalid || pr.type == Type::Invalid) {
                    error = "Unsupported list property type in PLY header.";
                    return false;
                }
            } else if (tok.size() >= 3) {
                pr.type = parseType(tok[1]);
                pr.name = tok[2];
                if (pr.type == Type::Invalid) {
                    error = "Unsupported property type '" + tok[1] + "' in PLY header.";
                    return false;
                }
            } else {
                continue;
            }
            elements.back().props.push_back(std::move(pr));
        }
    }
    if (!headerDone) {
        error = "PLY header is not terminated by 'end_header'.";
        return false;
    }

    Reader rd(p, end, fmt);
    bool sawVertex = false;
    size_t skippedFaces = 0;

    for (const Element& el : elements) {
        const bool isVertex = el.name == "vertex";
        const bool isFace = el.name == "face";

        int ix = -1, iy = -1, iz = -1, ir = -1, ig = -1, ib = -1, faceList = -1;
        Type colorType = Type::UInt8;
        for (size_t i = 0; i < el.props.size(); ++i) {
            const Property& pr = el.props[i];
            const int idx = static_cast<int>(i);
            if (isVertex && !pr.isList) {
                if (pr.name == "x") ix = idx;
                else if (pr.name == "y") iy = idx;
                else if (pr.name == "z") iz = idx;
                else if (pr.name == "red" || pr.name == "r") { ir = idx; colorType = pr.type; }
                else if (pr.name == "green" || pr.name == "g") ig = idx;
                else if (pr.name == "blue" || pr.name == "b") ib = idx;
            }
            if (isFace && pr.isList && (pr.name == "vertex_indices" || pr.name == "vertex_index" || faceList < 0)) faceList = idx;
        }
        if (isVertex) {
            if (ix < 0 || iy < 0 || iz < 0) {
                error = "PLY vertex element has no x/y/z properties.";
                return false;
            }
            sawVertex = true;
        }
        const bool wantColor = isVertex && ir >= 0 && ig >= 0 && ib >= 0;
        const float colorScale = (colorType == Type::Float32 || colorType == Type::Float64) ? 1.0f : 1.0f / 255.0f;
        if (isVertex) {
            out.positions.reserve(el.count);
            if (wantColor) out.colors.reserve(el.count);
        }

        std::vector<double> vals(el.props.size());
        std::vector<long long> face;
        for (size_t n = 0; n < el.count; ++n) {
            face.clear();
            for (size_t i = 0; i < el.props.size(); ++i) {
                const Property& pr = el.props[i];
                if (pr.isList) {
                    const long long cnt = static_cast<long long>(rd.scalar(pr.countType));
                    if (!rd.ok() || cnt < 0 || cnt > 1000000) {
                        error = "Corrupt or truncated PLY data (element '" + el.name + "').";
                        return false;
                    }
                    const bool keep = isFace && static_cast<int>(i) == faceList;
                    for (long long k = 0; k < cnt; ++k) {
                        const double v = rd.scalar(pr.type);
                        if (keep) face.push_back(static_cast<long long>(v));
                    }
                } else {
                    vals[i] = rd.scalar(pr.type);
                }
            }
            if (!rd.ok()) {
                error = "Corrupt or truncated PLY data (element '" + el.name + "').";
                return false;
            }
            if (isVertex) {
                out.positions.push_back({static_cast<float>(vals[ix]), static_cast<float>(vals[iy]), static_cast<float>(vals[iz])});
                if (wantColor)
                    out.colors.push_back({static_cast<float>(vals[ir]) * colorScale, static_cast<float>(vals[ig]) * colorScale,
                                          static_cast<float>(vals[ib]) * colorScale});
            } else if (isFace && faceList >= 0) {
                bool bad = face.size() < 3;
                for (long long v : face)
                    if (v < 0 || static_cast<size_t>(v) >= out.positions.size()) bad = true;
                if (bad) {
                    ++skippedFaces;
                } else {
                    for (size_t k = 1; k + 1 < face.size(); ++k) {
                        out.indices.push_back(static_cast<uint32_t>(face[0]));
                        out.indices.push_back(static_cast<uint32_t>(face[k]));
                        out.indices.push_back(static_cast<uint32_t>(face[k + 1]));
                    }
                }
            }
        }
    }

    if (!sawVertex) {
        error = "PLY file has no vertex element.";
        return false;
    }
    if (skippedFaces) out.warnings.push_back(std::to_string(skippedFaces) + " face(s) skipped (invalid vertex index).");
    if (out.indices.empty()) out.warnings.push_back("File contains no faces (point cloud only).");
    out.format = fmt == Format::Ascii ? "ASCII PLY" : "Binary PLY";
    return true;
}

}  // namespace mi
