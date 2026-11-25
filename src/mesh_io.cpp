// SPDX-License-Identifier: Apache-2.0
#include "mesh_io.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <new>

#include "loaders/loaders.h"

namespace mi {

namespace {

std::string lowerExt(const std::filesystem::path& p) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

// Reads the whole file and appends a '\0' that is NOT counted in the returned size.
bool readFile(const std::filesystem::path& path, std::vector<char>& buf, size_t& size, std::string& error) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        error = "Could not open the file.";
        return false;
    }
    const std::streamoff len = f.tellg();
    if (len < 0) {
        error = "Could not determine the file size.";
        return false;
    }
    f.seekg(0);
    buf.resize(static_cast<size_t>(len) + 1);
    if (len > 0 && !f.read(buf.data(), len)) {
        error = "Error while reading the file.";
        return false;
    }
    buf[static_cast<size_t>(len)] = '\0';
    size = static_cast<size_t>(len);
    return true;
}

}  // namespace

bool isSupportedMeshFile(const std::filesystem::path& path) {
    const std::string e = lowerExt(path);
    return e == ".obj" || e == ".stl" || e == ".ply" || e == ".off" || e == ".glb" || e == ".gltf";
}

const char* supportedFormatsText() { return "OBJ, STL, PLY, OFF, glTF / GLB"; }

bool loadMeshFile(const std::filesystem::path& path, Mesh& out, std::string& error) {
    out = Mesh{};
    const std::string ext = lowerExt(path);

    try {
        bool ok = false;
        if (ext == ".gltf" || ext == ".glb") {
            ok = loadGltf(path, out, error);
        } else if (ext == ".obj" || ext == ".stl" || ext == ".ply" || ext == ".off") {
            std::vector<char> buf;
            size_t size = 0;
            if (!readFile(path, buf, size, error)) return false;
            if (ext == ".obj") ok = loadObj(buf.data(), size, out, error);
            else if (ext == ".stl") ok = loadStl(buf.data(), size, out, error);
            else if (ext == ".ply") ok = loadPly(buf.data(), size, out, error);
            else ok = loadOff(buf.data(), size, out, error);
        } else {
            error = "Unsupported file type '" + ext + "'. Supported: " + supportedFormatsText() + ".";
            return false;
        }
        if (!ok) return false;
    } catch (const std::bad_alloc&) {
        error = "Out of memory while loading the file.";
        return false;
    }

    if (out.positions.size() >= 0xFFFFFFF0u) {
        error = "The mesh has too many vertices (limit is about 4 billion).";
        return false;
    }
    return true;
}

}  // namespace mi
