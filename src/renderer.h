// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "analysis.h"
#include "gl_loader.h"
#include "math3d.h"

namespace mi {

struct FrameParams {
    int fbWidth = 1, fbHeight = 1;     // whole framebuffer in pixels
    int vpX = 0, vpY = 0, vpW = 1, vpH = 1;  // scene viewport, GL coordinates (origin bottom-left)

    Mat4 model = Mat4::identity();
    Mat4 view = Mat4::identity();
    Mat4 proj = Mat4::identity();
    bool ortho = false;

    Vec3 background{0.11f, 0.12f, 0.14f};
    Vec3 meshColor{0.72f, 0.74f, 0.78f};
    Vec3 backColor{0.62f, 0.38f, 0.85f};

    bool wireframe = false;     // wireframe instead of shaded surface
    bool wireOverlay = false;   // edges on top of the shaded surface
    bool xrayWire = false;      // wireframe shows hidden edges too
    bool flatShading = true;
    bool highlightBackFaces = true;
    bool showGrid = true;
    bool useVertexColors = false;

    bool showProblems = true;
    bool showOpenEdges = true;
    bool showNonManifold = true;
    bool showInconsistent = true;
    bool showDegenerate = true;
    bool showSlivers = true;
    bool problemsXray = false;  // draw problem markers through the surface

    int selectedObject = -1;  // object tinted as selected (-1 = none)

    float uiScale = 1.0f;
};

// Owns every OpenGL object of the viewport. All calls need the GL context to be current.
class Renderer {
public:
    ~Renderer();

    bool init(std::string& error);
    void shutdown();

    void setMesh(const AnalyzedMesh& mesh);
    void clearMesh();
    // Per-object translations in the mesh's own coordinates (one entry per object).
    void setObjectOffsets(const std::vector<Vec3>& offsets);
    // Ground grid lying in the plane y = planeY, centred at (centerX, centerZ).
    void setGrid(float planeY, float cell, float centerX, float centerZ, int halfCount);

    void draw(const FrameParams& params);

    bool hasMesh() const { return mainSet_.count > 0; }
    bool meshHasColors() const { return meshHasColors_; }
    std::string glDescription() const { return glDescription_; }

private:
    struct Program {
        GLuint id = 0;
        GLint loc[16] = {};
    };
    struct IndexSet {
        GLuint vao = 0, ebo = 0;
        GLsizei count = 0;
    };

    void makeSet(IndexSet& set, const std::vector<uint32_t>& indices, bool fullAttribs);
    void destroySet(IndexSet& set);
    void setFlat(const Mat4& mvp, float r, float g, float b, float a, float bias, float pointSize = 1.0f, bool round = false,
                 bool objectOffsets = true);

    Program mesh_;
    Program flat_;
    GLuint vbo_ = 0;
    IndexSet mainSet_, boundarySet_, nonManifoldSet_, inconsistentSet_, degenerateSet_, sliverSet_;
    GLuint markerVao_ = 0, markerVbo_ = 0;
    GLsizei markerCount_ = 0;
    bool meshHasColors_ = false;

    GLuint offsetTex_ = 0;  // RGBA32F, 1024 texels wide: one translation per object

    GLuint gridVao_ = 0, gridVbo_ = 0;
    GLsizei gridMinorVerts_ = 0;

    float lineWidthMax_ = 1.0f;
    std::string glDescription_;
    bool ready_ = false;
};

}  // namespace mi
