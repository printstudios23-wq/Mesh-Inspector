// SPDX-License-Identifier: Apache-2.0
// 3MF loader: a ZIP container holding XML model files (3D Manufacturing Format core spec, plus the
// production extension's multi-file layout used by common slicers).
#include <cstring>
#include <map>
#include <memory>
#include <string_view>

#include "loaders.h"
#include "zip.h"

namespace mi {

namespace {

// ------------------------------------------------------------------ tiny XML tokenizer

struct Tag {
    std::string_view name;
    std::string_view attrs;
    bool closing = false;
};

// Advances to the next element tag. Skips comments, processing instructions, CDATA and text.
bool nextTag(const char*& p, const char* end, Tag& t) {
    while (p < end) {
        const char* lt = static_cast<const char*>(std::memchr(p, '<', static_cast<size_t>(end - p)));
        if (!lt) return false;
        p = lt + 1;
        if (p >= end) return false;
        auto skipTo = [&](const char* terminator) {
            const size_t n = std::strlen(terminator);
            while (p + n <= end && std::strncmp(p, terminator, n) != 0) ++p;
            p = p + n <= end ? p + n : end;
        };
        if (parse::startsWith(p, end, "!--")) { skipTo("-->"); continue; }
        if (parse::startsWith(p, end, "![CDATA[")) { skipTo("]]>"); continue; }
        if (*p == '?' || *p == '!') { skipTo(">"); continue; }

        t.closing = *p == '/';
        if (t.closing) ++p;
        const char* nameStart = p;
        while (p < end && static_cast<unsigned char>(*p) > ' ' && *p != '/' && *p != '>') ++p;
        t.name = std::string_view(nameStart, static_cast<size_t>(p - nameStart));
        const char* attrStart = p;
        char quote = 0;
        while (p < end && (quote || *p != '>')) {  // '>' inside quoted attribute values does not end the tag
            if (quote) { if (*p == quote) quote = 0; }
            else if (*p == '"' || *p == '\'') quote = *p;
            ++p;
        }
        t.attrs = std::string_view(attrStart, static_cast<size_t>(p - attrStart));
        if (p < end) ++p;  // consume '>'
        return true;
    }
    return false;
}

// Finds attribute `name` (a namespace prefix such as "p:" is ignored when comparing).
bool attribute(std::string_view attrs, std::string_view name, std::string_view& value) {
    size_t i = 0;
    while (i < attrs.size()) {
        while (i < attrs.size() && (static_cast<unsigned char>(attrs[i]) <= ' ' || attrs[i] == '/')) ++i;
        const size_t nameStart = i;
        while (i < attrs.size() && attrs[i] != '=' && static_cast<unsigned char>(attrs[i]) > ' ') ++i;
        std::string_view key = attrs.substr(nameStart, i - nameStart);
        while (i < attrs.size() && (attrs[i] == ' ' || attrs[i] == '=')) ++i;
        if (i >= attrs.size() || (attrs[i] != '"' && attrs[i] != '\'')) break;
        const char quote = attrs[i++];
        const size_t valStart = i;
        while (i < attrs.size() && attrs[i] != quote) ++i;
        const std::string_view val = attrs.substr(valStart, i - valStart);
        ++i;
        const size_t colon = key.find(':');
        if (key == name || (colon != std::string_view::npos && key.substr(colon + 1) == name)) {
            value = val;
            return true;
        }
    }
    return false;
}

bool numberAttr(std::string_view attrs, const char* name, double& out) {
    std::string_view v;
    if (!attribute(attrs, name, v)) return false;
    // `v` is followed by the closing quote inside the document buffer, so strtod stops there.
    char* q = nullptr;
    out = std::strtod(v.data(), &q);
    return q != v.data();
}
bool intAttr(std::string_view attrs, const char* name, long long& out) {
    std::string_view v;
    if (!attribute(attrs, name, v)) return false;
    char* q = nullptr;
    out = std::strtoll(v.data(), &q, 10);
    return q != v.data();
}

// ------------------------------------------------------------------ model data

// 3MF transforms are 4x3 matrices for row vectors: p' = p * M + t.
struct Xform {
    double r[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}};

    Vec3d apply(const Vec3d& p) const {
        return {p.x * r[0][0] + p.y * r[1][0] + p.z * r[2][0] + r[3][0], p.x * r[0][1] + p.y * r[1][1] + p.z * r[2][1] + r[3][1],
                p.x * r[0][2] + p.y * r[1][2] + p.z * r[2][2] + r[3][2]};
    }
    // first `inner`, then `outer`
    static Xform compose(const Xform& inner, const Xform& outer) {
        Xform m;
        for (int i = 0; i < 4; ++i) {
            const Vec3d row{inner.r[i][0], inner.r[i][1], inner.r[i][2]};
            Vec3d v = outer.apply(row);
            if (i < 3) v = v - Vec3d{outer.r[3][0], outer.r[3][1], outer.r[3][2]};  // directions ignore translation
            m.r[i][0] = v.x;
            m.r[i][1] = v.y;
            m.r[i][2] = v.z;
        }
        return m;
    }
    double determinant() const {
        return r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
               r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    }
};

bool parseTransform(std::string_view attrs, Xform& out) {
    std::string_view v;
    if (!attribute(attrs, "transform", v)) return false;
    const std::string text(v);
    const char* p = text.c_str();
    double m[12];
    for (double& d : m) {
        char* q = nullptr;
        d = std::strtod(p, &q);
        if (q == p) return false;
        p = q;
    }
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 3; ++j) out.r[i][j] = m[i * 3 + j];
    return true;
}

struct Component {
    long long id = 0;
    std::string path;  // empty: same file
    Xform xf;
};

struct Object {
    std::vector<Vec3> verts;
    std::vector<uint32_t> tris;
    std::vector<Component> comps;
    bool skip = false;  // support structures are not part of the printed model
};

struct BuildItem {
    long long id = 0;
    Xform xf;
};

struct ModelFile {
    std::map<long long, Object> objects;
    std::vector<BuildItem> build;
};

bool parseModel(const std::string& xml, ModelFile& model, size_t& badTriangles) {
    const char* p = xml.data();
    const char* end = p + xml.size();
    Tag t;
    Object* cur = nullptr;
    bool any = false;
    while (nextTag(p, end, t)) {
        if (t.closing) {
            if (t.name == "object") cur = nullptr;
            continue;
        }
        if (t.name == "object") {
            long long id = 0;
            if (!intAttr(t.attrs, "id", id)) continue;
            cur = &model.objects[id];
            std::string_view type;
            if (attribute(t.attrs, "type", type) && (type == "support" || type == "solidsupport")) cur->skip = true;
            any = true;
        } else if (t.name == "vertex" && cur) {
            double x = 0, y = 0, z = 0;
            numberAttr(t.attrs, "x", x);
            numberAttr(t.attrs, "y", y);
            numberAttr(t.attrs, "z", z);
            cur->verts.push_back({static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
        } else if (t.name == "triangle" && cur) {
            long long a = -1, b = -1, c = -1;
            intAttr(t.attrs, "v1", a);
            intAttr(t.attrs, "v2", b);
            intAttr(t.attrs, "v3", c);
            const long long n = static_cast<long long>(cur->verts.size());
            if (a < 0 || b < 0 || c < 0 || a >= n || b >= n || c >= n) {
                ++badTriangles;
            } else {
                cur->tris.insert(cur->tris.end(), {static_cast<uint32_t>(a), static_cast<uint32_t>(b), static_cast<uint32_t>(c)});
            }
        } else if (t.name == "component" && cur) {
            Component c;
            if (!intAttr(t.attrs, "objectid", c.id)) continue;
            std::string_view path;
            if (attribute(t.attrs, "path", path)) c.path = std::string(path);
            parseTransform(t.attrs, c.xf);
            cur->comps.push_back(std::move(c));
        } else if (t.name == "item") {
            BuildItem item;
            if (!intAttr(t.attrs, "objectid", item.id)) continue;
            parseTransform(t.attrs, item.xf);
            model.build.push_back(item);
        }
    }
    return any;
}

// ------------------------------------------------------------------ assembling the scene

class Builder {
public:
    Builder(const ZipArchive& zip, Mesh& out) : zip_(zip), out_(out) {}

    ModelFile* load(const std::string& path, std::string& error) {
        auto it = files_.find(path);
        if (it != files_.end()) return it->second.get();
        std::string xml;
        if (!zip_.extract(path, xml, error)) return nullptr;
        auto model = std::make_unique<ModelFile>();
        if (!parseModel(xml, *model, badTriangles)) {
            error = "'" + path + "' contains no objects.";
            return nullptr;
        }
        return (files_[path] = std::move(model)).get();
    }

    void emit(const std::string& file, long long id, const Xform& xf, int depth) {
        if (depth > 16 || out_.positions.size() > 0xFFFFFF00u) return;
        std::string err;
        ModelFile* model = load(file, err);
        if (!model) {
            ++missingRefs;
            return;
        }
        const auto it = model->objects.find(id);
        if (it == model->objects.end()) {
            ++missingRefs;
            return;
        }
        const Object& obj = it->second;
        if (obj.skip) return;

        if (!obj.tris.empty()) {
            const uint32_t base = static_cast<uint32_t>(out_.positions.size());
            for (const Vec3& v : obj.verts) out_.positions.push_back(Vec3(xf.apply(Vec3d(v))));
            const bool mirrored = xf.determinant() < 0;  // a mirroring transform turns triangles inside out
            for (size_t i = 0; i + 2 < obj.tris.size(); i += 3) {
                out_.indices.push_back(base + obj.tris[i]);
                out_.indices.push_back(base + obj.tris[mirrored ? i + 2 : i + 1]);
                out_.indices.push_back(base + obj.tris[mirrored ? i + 1 : i + 2]);
            }
        }
        for (const Component& c : obj.comps) {
            const std::string child = c.path.empty() ? file : c.path;
            emit(child, c.id, Xform::compose(c.xf, xf), depth + 1);
        }
    }

    size_t badTriangles = 0;
    size_t missingRefs = 0;

private:
    const ZipArchive& zip_;
    Mesh& out_;
    std::map<std::string, std::unique_ptr<ModelFile>> files_;
};

std::string findModelPath(const ZipArchive& zip) {
    std::string rels;
    std::string err;
    if (zip.extract("_rels/.rels", rels, err)) {
        const char* p = rels.data();
        const char* end = p + rels.size();
        Tag t;
        while (nextTag(p, end, t)) {
            std::string_view type, target;
            if (!t.closing && t.name == "Relationship" && attribute(t.attrs, "Type", type) && attribute(t.attrs, "Target", target) &&
                type.size() >= 7 && type.substr(type.size() - 7) == "3dmodel") {
                return std::string(target);
            }
        }
    }
    if (zip.contains("3D/3dmodel.model")) return "3D/3dmodel.model";
    return zip.findBySuffix(".model");
}

}  // namespace

bool loadThreeMf(const char* data, size_t size, Mesh& out, std::string& error) {
    ZipArchive zip;
    if (!zip.open(data, size, error)) {
        error = "Not a valid 3MF file: " + error;
        return false;
    }
    const std::string mainPath = findModelPath(zip);
    if (mainPath.empty()) {
        error = "This ZIP file does not contain a 3MF model (no 3D/3dmodel.model).";
        return false;
    }

    Builder builder(zip, out);
    ModelFile* main = builder.load(mainPath, error);
    if (!main) return false;

    if (!main->build.empty()) {
        for (const BuildItem& item : main->build) builder.emit(mainPath, item.id, item.xf, 0);
    } else {
        // No <build> section: show every object that is not just a part of another one.
        std::map<long long, bool> referenced;
        for (const auto& kv : main->objects)
            for (const Component& c : kv.second.comps)
                if (c.path.empty()) referenced[c.id] = true;
        for (const auto& kv : main->objects)
            if (!referenced.count(kv.first)) builder.emit(mainPath, kv.first, Xform{}, 0);
    }

    if (builder.badTriangles)
        out.warnings.push_back(std::to_string(builder.badTriangles) + " triangle(s) skipped (invalid vertex index).");
    if (builder.missingRefs)
        out.warnings.push_back(std::to_string(builder.missingRefs) + " referenced object(s) could not be found in the file.");
    if (out.indices.empty()) {
        error = "The 3MF file contains no triangle meshes.";
        return false;
    }
    out.format = "3MF";
    return true;
}

}  // namespace mi
