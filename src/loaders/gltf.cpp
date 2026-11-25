// SPDX-License-Identifier: Apache-2.0
// glTF 2.0 (.gltf / .glb) loader built on cgltf (MIT, https://github.com/jkuhlmann/cgltf).
#include <fstream>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "loaders.h"

namespace mi {

namespace {

// File access goes through std::filesystem so non-ASCII Windows paths work
// (cgltf's default implementation uses fopen with a narrow string).
cgltf_result readFileCb(const cgltf_memory_options* mem, const cgltf_file_options*, const char* path, cgltf_size* size,
                        void** data) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
    if (!f) return cgltf_result_file_not_found;
    const std::streamoff len = f.tellg();
    if (len < 0) return cgltf_result_io_error;
    f.seekg(0);
    void* buf = mem->alloc_func ? mem->alloc_func(mem->user_data, static_cast<cgltf_size>(len)) : std::malloc(static_cast<size_t>(len));
    if (!buf) return cgltf_result_out_of_memory;
    if (len > 0 && !f.read(static_cast<char*>(buf), len)) {
        if (mem->free_func) mem->free_func(mem->user_data, buf);
        else std::free(buf);
        return cgltf_result_io_error;
    }
    *size = static_cast<cgltf_size>(len);
    *data = buf;
    return cgltf_result_success;
}

void releaseFileCb(const cgltf_memory_options* mem, const cgltf_file_options*, void* data) {
    if (mem->free_func) mem->free_func(mem->user_data, data);
    else std::free(data);
}

const char* resultName(cgltf_result r) {
    switch (r) {
        case cgltf_result_file_not_found: return "file not found";
        case cgltf_result_io_error: return "I/O error";
        case cgltf_result_out_of_memory: return "out of memory";
        case cgltf_result_data_too_short: return "data too short";
        case cgltf_result_unknown_format: return "unknown format";
        case cgltf_result_invalid_json: return "invalid JSON";
        case cgltf_result_invalid_gltf: return "invalid glTF";
        case cgltf_result_legacy_gltf: return "legacy glTF 1.0 is not supported";
        default: return "unknown error";
    }
}

struct Context {
    Mesh* out;
    size_t skippedPrims = 0;
    bool unsupportedCompression = false;
    bool anyColor = false;
};

void addPrimitive(Context& ctx, const cgltf_primitive& prim, const float* world) {
    Mesh& m = *ctx.out;
    if (prim.type != cgltf_primitive_type_triangles && prim.type != cgltf_primitive_type_triangle_strip &&
        prim.type != cgltf_primitive_type_triangle_fan) {
        ++ctx.skippedPrims;  // points / lines carry no surface
        return;
    }
    if (prim.has_draco_mesh_compression) {
        ctx.unsupportedCompression = true;
        return;
    }
    const cgltf_accessor* pos = nullptr;
    const cgltf_accessor* col = nullptr;
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        const cgltf_attribute& a = prim.attributes[i];
        if (a.type == cgltf_attribute_type_position && !pos) pos = a.data;
        if (a.type == cgltf_attribute_type_color && a.index == 0 && !col) col = a.data;
    }
    if (!pos) return;
    if (!pos->buffer_view && !pos->is_sparse) {
        ctx.unsupportedCompression = true;  // e.g. EXT_meshopt_compression
        return;
    }

    const uint32_t base = static_cast<uint32_t>(m.positions.size());
    const cgltf_size vc = pos->count;
    m.positions.reserve(m.positions.size() + vc);
    for (cgltf_size i = 0; i < vc; ++i) {
        float v[3] = {0, 0, 0};
        cgltf_accessor_read_float(pos, i, v, 3);
        m.positions.push_back({world[0] * v[0] + world[4] * v[1] + world[8] * v[2] + world[12],
                               world[1] * v[0] + world[5] * v[1] + world[9] * v[2] + world[13],
                               world[2] * v[0] + world[6] * v[1] + world[10] * v[2] + world[14]});
    }

    const bool haveCol = col && col->count == vc && (col->buffer_view || col->is_sparse);
    if (haveCol && !ctx.anyColor) {
        ctx.anyColor = true;
        m.colors.assign(base, Vec3{1, 1, 1});
    }
    if (ctx.anyColor) {
        for (cgltf_size i = 0; i < vc; ++i) {
            float c[4] = {1, 1, 1, 1};
            if (haveCol) cgltf_accessor_read_float(col, i, c, 4);
            m.colors.push_back({c[0], c[1], c[2]});
        }
    }

    const cgltf_size ic = prim.indices ? prim.indices->count : vc;
    auto idx = [&](cgltf_size i) -> uint32_t {
        return base + static_cast<uint32_t>(prim.indices ? cgltf_accessor_read_index(prim.indices, i) : i);
    };
    auto tri = [&](uint32_t a, uint32_t b, uint32_t c) {
        if (a >= base + vc || b >= base + vc || c >= base + vc) return;  // corrupt index
        m.indices.push_back(a);
        m.indices.push_back(b);
        m.indices.push_back(c);
    };
    if (prim.type == cgltf_primitive_type_triangles) {
        for (cgltf_size i = 0; i + 2 < ic; i += 3) tri(idx(i), idx(i + 1), idx(i + 2));
    } else if (prim.type == cgltf_primitive_type_triangle_strip) {
        for (cgltf_size i = 0; i + 2 < ic; ++i) {
            if (i & 1) tri(idx(i + 1), idx(i), idx(i + 2));
            else tri(idx(i), idx(i + 1), idx(i + 2));
        }
    } else {
        for (cgltf_size i = 1; i + 1 < ic; ++i) tri(idx(0), idx(i), idx(i + 1));
    }
}

void visit(Context& ctx, const cgltf_node* node, int depth) {
    if (depth > 128) return;
    if (node->mesh) {
        float world[16];
        cgltf_node_transform_world(node, world);
        for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) addPrimitive(ctx, node->mesh->primitives[p], world);
    }
    for (cgltf_size i = 0; i < node->children_count; ++i) visit(ctx, node->children[i], depth + 1);
}

}  // namespace

bool loadGltf(const std::filesystem::path& path, Mesh& out, std::string& error) {
    cgltf_options options{};
    options.file.read = &readFileCb;
    options.file.release = &releaseFileCb;

    const std::string u8 = path.u8string();
    cgltf_data* data = nullptr;
    cgltf_result r = cgltf_parse_file(&options, u8.c_str(), &data);
    if (r != cgltf_result_success) {
        error = std::string("Could not read glTF: ") + resultName(r) + ".";
        return false;
    }
    r = cgltf_load_buffers(&options, data, u8.c_str());
    if (r != cgltf_result_success) {
        error = std::string("Could not load glTF buffers: ") + resultName(r) + " (external .bin next to the file?).";
        cgltf_free(data);
        return false;
    }

    Context ctx;
    ctx.out = &out;
    const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
    if (scene) {
        for (cgltf_size i = 0; i < scene->nodes_count; ++i) visit(ctx, scene->nodes[i], 0);
    } else {
        for (cgltf_size i = 0; i < data->nodes_count; ++i)
            if (!data->nodes[i].parent) visit(ctx, &data->nodes[i], 0);
    }
    cgltf_free(data);

    if (ctx.unsupportedCompression && out.indices.empty()) {
        error = "This glTF uses mesh compression (Draco / meshopt), which is not supported yet.";
        return false;
    }
    if (ctx.unsupportedCompression)
        out.warnings.push_back("Some primitives use mesh compression (Draco / meshopt) and were skipped.");
    if (ctx.skippedPrims) out.warnings.push_back(std::to_string(ctx.skippedPrims) + " non-triangle primitive(s) skipped.");
    if (out.positions.empty()) {
        error = "The glTF file contains no mesh geometry.";
        return false;
    }
    out.format = path.extension() == ".glb" || path.extension() == ".GLB" ? "glTF Binary (GLB)" : "glTF";
    return true;
}

}  // namespace mi
