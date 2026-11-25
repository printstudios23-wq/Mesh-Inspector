// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>

#include "mesh.h"

namespace mi {

// Loads a mesh, choosing the loader from the file extension. On failure returns false
// and fills `error` with a message suitable for showing to the user.
bool loadMeshFile(const std::filesystem::path& path, Mesh& out, std::string& error);

bool isSupportedMeshFile(const std::filesystem::path& path);

// "OBJ, STL, PLY, OFF, glTF/GLB" style description for UI text.
const char* supportedFormatsText();

}  // namespace mi
