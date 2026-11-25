# Third-party notices

Mesh Inspector itself is licensed under the Apache License 2.0 (see `LICENSE`).
It downloads and links the following libraries when you build it (see `CMakeLists.txt` for the exact versions):

| Library | Use | License |
| --- | --- | --- |
| [GLFW](https://www.glfw.org/) 3.4 | window, input, OpenGL context | zlib/libpng |
| [Dear ImGui](https://github.com/ocornut/imgui) 1.91.9 | user interface | MIT |
| [cgltf](https://github.com/jkuhlmann/cgltf) 1.14 | glTF / GLB parsing | MIT |

Their license texts are included in the respective source trees that CMake fetches into the build directory.
On Windows the UI uses the system font Segoe UI if present; no font files are redistributed.
