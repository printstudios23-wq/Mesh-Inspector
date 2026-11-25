// SPDX-License-Identifier: Apache-2.0
#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <vector>

namespace mi {

using namespace mi::gl;

namespace {

const char* kMeshVert = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in float aObject;
uniform mat4 uMV;
uniform mat4 uProj;
uniform mat3 uNormalMat;
uniform sampler2D uOffsets;
out vec3 vViewPos;
out vec3 vNormal;
out vec3 vColor;
flat out float vObject;
void main() {
    int id = int(aObject + 0.5);
    vec3 offset = texelFetch(uOffsets, ivec2(id & 1023, id >> 10), 0).xyz;
    vObject = aObject;
    vec4 vp = uMV * vec4(aPos + offset, 1.0);
    vViewPos = vp.xyz;
    vNormal = uNormalMat * aNormal;
    vColor = aColor;
    gl_Position = uProj * vp;
}
)GLSL";

const char* kMeshFrag = R"GLSL(
#version 330 core
in vec3 vViewPos;
in vec3 vNormal;
in vec3 vColor;
flat in float vObject;
uniform float uSelected;
uniform int uFlat;
uniform int uOrtho;
uniform int uUseVertexColor;
uniform int uHighlightBack;
uniform vec3 uBaseColor;
uniform vec3 uBackColor;
out vec4 fragColor;
void main() {
    // Flat shading: reconstruct the true face normal from screen-space derivatives so the
    // viewer shows the real triangles rather than interpolated normals.
    vec3 n = (uFlat != 0) ? normalize(cross(dFdx(vViewPos), dFdy(vViewPos))) : normalize(vNormal);
    vec3 v = (uOrtho != 0) ? vec3(0.0, 0.0, 1.0) : normalize(-vViewPos);
    if (dot(n, v) < 0.0) n = -n;  // light both sides of a surface

    vec3 base = (uUseVertexColor != 0) ? vColor : uBaseColor;
    if (uHighlightBack != 0 && !gl_FrontFacing) base = uBackColor;
    if (abs(vObject - uSelected) < 0.5) base = mix(base, vec3(0.30, 0.62, 1.0), 0.40);  // selected object

    vec3 l1 = normalize(vec3(0.35, 0.55, 1.0));
    vec3 l2 = normalize(vec3(-0.6, -0.2, 0.5));
    float diffuse = max(dot(n, l1), 0.0) * 0.62 + max(dot(n, l2), 0.0) * 0.22;
    float ambient = 0.30 + 0.12 * n.y;
    float spec = pow(max(dot(n, normalize(l1 + v)), 0.0), 48.0) * 0.12;
    fragColor = vec4(base * (ambient + diffuse) + vec3(spec), 1.0);
}
)GLSL";

const char* kFlatVert = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 3) in float aObject;
uniform mat4 uMVP;
uniform float uBias;
uniform float uPointSize;
uniform int uUseOffsets;
uniform sampler2D uOffsets;
void main() {
    vec3 offset = vec3(0.0);
    if (uUseOffsets != 0) {
        int id = int(aObject + 0.5);
        offset = texelFetch(uOffsets, ivec2(id & 1023, id >> 10), 0).xyz;
    }
    gl_Position = uMVP * vec4(aPos + offset, 1.0);
    gl_Position.z -= uBias * gl_Position.w;  // pull towards the camera to avoid z-fighting
    gl_PointSize = uPointSize;
}
)GLSL";

const char* kFlatFrag = R"GLSL(
#version 330 core
uniform vec4 uColor;
uniform int uRoundPoint;
out vec4 fragColor;
void main() {
    if (uRoundPoint != 0) {
        vec2 c = gl_PointCoord * 2.0 - 1.0;
        if (dot(c, c) > 1.0) discard;
    }
    fragColor = uColor;
}
)GLSL";

GLuint compileShader(GLenum type, const char* src, std::string& error) {
    GLuint s = CreateShader(type);
    ShaderSource(s, 1, &src, nullptr);
    CompileShader(s);
    GLint ok = 0;
    GetShaderiv(s, COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        GetShaderInfoLog(s, sizeof log - 1, nullptr, log);
        error = std::string("Shader compile error: ") + log;
        DeleteShader(s);
        return 0;
    }
    return s;
}

GLuint buildProgram(const char* vs, const char* fs, std::string& error) {
    GLuint v = compileShader(VERTEX_SHADER, vs, error);
    if (!v) return 0;
    GLuint f = compileShader(FRAGMENT_SHADER, fs, error);
    if (!f) {
        DeleteShader(v);
        return 0;
    }
    GLuint p = CreateProgram();
    AttachShader(p, v);
    AttachShader(p, f);
    LinkProgram(p);
    DeleteShader(v);
    DeleteShader(f);
    GLint ok = 0;
    GetProgramiv(p, LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        GetProgramInfoLog(p, sizeof log - 1, nullptr, log);
        error = std::string("Shader link error: ") + log;
        DeleteProgram(p);
        return 0;
    }
    return p;
}

float luminance(const Vec3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

}  // namespace

Renderer::~Renderer() { shutdown(); }

bool Renderer::init(std::string& error) {
    mesh_.id = buildProgram(kMeshVert, kMeshFrag, error);
    if (!mesh_.id) return false;
    flat_.id = buildProgram(kFlatVert, kFlatFrag, error);
    if (!flat_.id) return false;

    // mesh_.loc: 0 MV, 1 Proj, 2 NormalMat, 3 Flat, 4 Ortho, 5 UseVertexColor, 6 HighlightBack, 7 BaseColor, 8 BackColor
    const char* meshUniforms[] = {"uMV", "uProj", "uNormalMat", "uFlat", "uOrtho", "uUseVertexColor", "uHighlightBack", "uBaseColor", "uBackColor", "uSelected", "uOffsets"};
    for (size_t i = 0; i < std::size(meshUniforms); ++i) mesh_.loc[i] = GetUniformLocation(mesh_.id, meshUniforms[i]);
    // flat_.loc: 0 MVP, 1 Bias, 2 PointSize, 3 Color, 4 RoundPoint
    const char* flatUniforms[] = {"uMVP", "uBias", "uPointSize", "uColor", "uRoundPoint", "uUseOffsets", "uOffsets"};
    for (size_t i = 0; i < std::size(flatUniforms); ++i) flat_.loc[i] = GetUniformLocation(flat_.id, flatUniforms[i]);

    // Both programs read the object offsets from texture unit 0.
    UseProgram(mesh_.id);
    Uniform1i(mesh_.loc[10], 0);
    UseProgram(flat_.id);
    Uniform1i(flat_.loc[6], 0);
    UseProgram(0);
    glGenTextures(1, &offsetTex_);
    setObjectOffsets({});

    GLfloat range[2] = {1, 1};
    glGetFloatv(GL_LINE_WIDTH_RANGE, range);
    lineWidthMax_ = std::max(1.0f, range[1]);

    const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* version = glGetString(GL_VERSION);
    glDescription_ = std::string(renderer ? reinterpret_cast<const char*>(renderer) : "?") + " - OpenGL " +
                     (version ? reinterpret_cast<const char*>(version) : "?");
    while (glGetError() != GL_NO_ERROR) {
    }
    ready_ = true;
    return true;
}

void Renderer::destroySet(IndexSet& s) {
    if (s.vao) DeleteVertexArrays(1, &s.vao);
    if (s.ebo) DeleteBuffers(1, &s.ebo);
    s = IndexSet{};
}

void Renderer::clearMesh() {
    destroySet(mainSet_);
    destroySet(boundarySet_);
    destroySet(nonManifoldSet_);
    destroySet(inconsistentSet_);
    destroySet(degenerateSet_);
    destroySet(sliverSet_);
    if (vbo_) DeleteBuffers(1, &vbo_);
    vbo_ = 0;
    if (markerVao_) DeleteVertexArrays(1, &markerVao_);
    if (markerVbo_) DeleteBuffers(1, &markerVbo_);
    markerVao_ = markerVbo_ = 0;
    markerCount_ = 0;
    meshHasColors_ = false;
}

void Renderer::shutdown() {
    if (!ready_) return;
    clearMesh();
    if (gridVao_) DeleteVertexArrays(1, &gridVao_);
    if (gridVbo_) DeleteBuffers(1, &gridVbo_);
    gridVao_ = gridVbo_ = 0;
    if (offsetTex_) glDeleteTextures(1, &offsetTex_);
    offsetTex_ = 0;
    if (mesh_.id) DeleteProgram(mesh_.id);
    if (flat_.id) DeleteProgram(flat_.id);
    mesh_.id = flat_.id = 0;
    ready_ = false;
}

void Renderer::makeSet(IndexSet& s, const std::vector<uint32_t>& indices, bool fullAttribs) {
    destroySet(s);
    if (indices.empty()) return;
    constexpr GLsizei kStride = 10 * sizeof(float);
    GenVertexArrays(1, &s.vao);
    BindVertexArray(s.vao);
    BindBuffer(ARRAY_BUFFER, vbo_);
    EnableVertexAttribArray(0);
    VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, reinterpret_cast<const void*>(0));
    if (fullAttribs) {
        EnableVertexAttribArray(1);
        VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, kStride, reinterpret_cast<const void*>(3 * sizeof(float)));
        EnableVertexAttribArray(2);
        VertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, kStride, reinterpret_cast<const void*>(6 * sizeof(float)));
    }
    EnableVertexAttribArray(3);  // object id
    VertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, kStride, reinterpret_cast<const void*>(9 * sizeof(float)));
    GenBuffers(1, &s.ebo);
    BindBuffer(ELEMENT_ARRAY_BUFFER, s.ebo);
    BufferData(ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)), indices.data(), STATIC_DRAW);
    s.count = static_cast<GLsizei>(indices.size());
    BindVertexArray(0);
}

void Renderer::setMesh(const AnalyzedMesh& m) {
    clearMesh();
    const RenderMesh& r = m.render;
    if (r.positions.empty() || r.indices.empty()) return;

    meshHasColors_ = !r.colors.empty();
    std::vector<float> data;
    data.reserve(r.positions.size() * 10);
    for (size_t i = 0; i < r.positions.size(); ++i) {
        const Vec3& p = r.positions[i];
        const Vec3& n = r.normals[i];
        const Vec3 c = meshHasColors_ ? r.colors[i] : Vec3{1, 1, 1};
        const float obj = i < r.objectIds.size() ? static_cast<float>(r.objectIds[i]) : 0.0f;
        data.insert(data.end(), {p.x, p.y, p.z, n.x, n.y, n.z, c.x, c.y, c.z, obj});
    }
    GenBuffers(1, &vbo_);
    BindBuffer(ARRAY_BUFFER, vbo_);
    BufferData(ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data(), STATIC_DRAW);

    makeSet(mainSet_, r.indices, true);
    makeSet(boundarySet_, m.overlays.boundaryEdges, false);
    makeSet(nonManifoldSet_, m.overlays.nonManifoldEdges, false);
    makeSet(inconsistentSet_, m.overlays.inconsistentEdges, false);
    makeSet(degenerateSet_, m.overlays.degenerateEdges, false);
    makeSet(sliverSet_, m.overlays.sliverTriangles, false);

    if (!m.overlays.degenerateMarkers.empty()) {
        GenVertexArrays(1, &markerVao_);
        GenBuffers(1, &markerVbo_);
        BindVertexArray(markerVao_);
        BindBuffer(ARRAY_BUFFER, markerVbo_);
        std::vector<float> markers;
        markers.reserve(m.overlays.degenerateMarkers.size() * 4);
        for (size_t i = 0; i < m.overlays.degenerateMarkers.size(); ++i) {
            const Vec3& p = m.overlays.degenerateMarkers[i];
            const float obj = i < m.overlays.degenerateMarkerObjects.size() ? static_cast<float>(m.overlays.degenerateMarkerObjects[i]) : 0.0f;
            markers.insert(markers.end(), {p.x, p.y, p.z, obj});
        }
        BufferData(ARRAY_BUFFER, static_cast<GLsizeiptr>(markers.size() * sizeof(float)), markers.data(), STATIC_DRAW);
        EnableVertexAttribArray(0);
        VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<const void*>(0));
        EnableVertexAttribArray(3);
        VertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));
        BindVertexArray(0);
        markerCount_ = static_cast<GLsizei>(m.overlays.degenerateMarkers.size());
    }
}

void Renderer::setGrid(float planeY, float cell, float cx, float cz, int halfCount) {
    if (gridVao_) DeleteVertexArrays(1, &gridVao_);
    if (gridVbo_) DeleteBuffers(1, &gridVbo_);
    gridVao_ = gridVbo_ = 0;
    gridMinorVerts_ = 0;
    if (!(cell > 0) || halfCount < 1) return;

    // Snap the centre to the grid so lines stay on round numbers.
    const float gx = std::round(cx / cell) * cell;
    const float gz = std::round(cz / cell) * cell;
    const float ext = cell * static_cast<float>(halfCount);
    std::vector<float> v;
    for (int i = -halfCount; i <= halfCount; ++i) {
        const float x = gx + cell * static_cast<float>(i);
        const float z = gz + cell * static_cast<float>(i);
        v.insert(v.end(), {x, planeY, gz - ext, x, planeY, gz + ext});
        v.insert(v.end(), {gx - ext, planeY, z, gx + ext, planeY, z});
    }
    gridMinorVerts_ = static_cast<GLsizei>(v.size() / 3);
    // world axes through the origin, drawn only if they fall inside the grid
    const bool xAxisVisible = std::fabs(0.0f - gz) <= ext;
    const bool zAxisVisible = std::fabs(0.0f - gx) <= ext;
    if (xAxisVisible) v.insert(v.end(), {gx - ext, planeY, 0.0f, gx + ext, planeY, 0.0f});
    else v.insert(v.end(), {0, 0, 0, 0, 0, 0});
    if (zAxisVisible) v.insert(v.end(), {0.0f, planeY, gz - ext, 0.0f, planeY, gz + ext});
    else v.insert(v.end(), {0, 0, 0, 0, 0, 0});

    GenVertexArrays(1, &gridVao_);
    GenBuffers(1, &gridVbo_);
    BindVertexArray(gridVao_);
    BindBuffer(ARRAY_BUFFER, gridVbo_);
    BufferData(ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(float)), v.data(), STATIC_DRAW);
    EnableVertexAttribArray(0);
    VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<const void*>(0));
    BindVertexArray(0);
}

void Renderer::setFlat(const Mat4& mvp, float r, float g, float b, float a, float bias, float pointSize, bool round, bool objectOffsets) {
    UniformMatrix4fv(flat_.loc[0], 1, GL_FALSE, mvp.m);
    Uniform1f(flat_.loc[1], bias);
    Uniform1f(flat_.loc[2], pointSize);
    Uniform4f(flat_.loc[3], r, g, b, a);
    Uniform1i(flat_.loc[4], round ? 1 : 0);
    Uniform1i(flat_.loc[5], objectOffsets ? 1 : 0);
}

void Renderer::setObjectOffsets(const std::vector<Vec3>& offsets) {
    constexpr int kWidth = 1024;
    const int rows = std::max(1, static_cast<int>((offsets.size() + kWidth - 1) / kWidth));
    std::vector<float> texels(static_cast<size_t>(kWidth) * static_cast<size_t>(rows) * 4, 0.0f);
    for (size_t i = 0; i < offsets.size(); ++i) {
        texels[i * 4] = offsets[i].x;
        texels[i * 4 + 1] = offsets[i].y;
        texels[i * 4 + 2] = offsets[i].z;
    }
    ActiveTexture(TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, offsetTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(RGBA32F), kWidth, rows, 0, GL_RGBA, GL_FLOAT, texels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(CLAMP_TO_EDGE));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(CLAMP_TO_EDGE));
}

void Renderer::draw(const FrameParams& f) {
    if (!ready_) return;

    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, f.fbWidth, f.fbHeight);
    glDepthMask(GL_TRUE);
    glClearColor(f.background.x, f.background.y, f.background.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (f.vpW <= 0 || f.vpH <= 0) return;

    glViewport(f.vpX, f.vpY, f.vpW, f.vpH);
    glEnable(GL_SCISSOR_TEST);
    glScissor(f.vpX, f.vpY, f.vpW, f.vpH);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glEnable(MULTISAMPLE);
    glEnable(PROGRAM_POINT_SIZE);

    const Mat4 viewProj = f.proj * f.view;
    const Mat4 mv = f.view * f.model;
    const Mat4 mvp = f.proj * mv;
    const bool lightBg = luminance(f.background) > 0.5f;

    ActiveTexture(TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, offsetTex_);
    UseProgram(flat_.id);

    // ------------------------------------------------------------- ground grid
    if (f.showGrid && gridVao_) {
        const Vec3 gridCol = lightBg ? f.background * 0.80f : Vec3{f.background.x + 0.13f, f.background.y + 0.13f, f.background.z + 0.13f};
        BindVertexArray(gridVao_);
        glLineWidth(1.0f);
        setFlat(viewProj, gridCol.x, gridCol.y, gridCol.z, 1.0f, 0.0f, 1.0f, false, false);
        glDrawArrays(GL_LINES, 0, gridMinorVerts_);
        setFlat(viewProj, 0.80f, 0.28f, 0.28f, 1.0f, 0.0f, 1.0f, false, false);  // X axis
        glDrawArrays(GL_LINES, gridMinorVerts_, 2);
        setFlat(viewProj, 0.30f, 0.45f, 0.90f, 1.0f, 0.0f, 1.0f, false, false);  // Z axis
        glDrawArrays(GL_LINES, gridMinorVerts_ + 2, 2);
        BindVertexArray(0);
    }

    if (!hasMesh()) {
        UseProgram(0);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        return;
    }

    // ------------------------------------------------------------- surface
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    if (!f.wireframe) {
        UseProgram(mesh_.id);
        UniformMatrix4fv(mesh_.loc[0], 1, GL_FALSE, mv.m);
        UniformMatrix4fv(mesh_.loc[1], 1, GL_FALSE, f.proj.m);
        const float nm[9] = {mv.at(0, 0), mv.at(1, 0), mv.at(2, 0), mv.at(0, 1), mv.at(1, 1), mv.at(2, 1), mv.at(0, 2), mv.at(1, 2), mv.at(2, 2)};
        UniformMatrix3fv(mesh_.loc[2], 1, GL_FALSE, nm);
        Uniform1i(mesh_.loc[3], f.flatShading ? 1 : 0);
        Uniform1i(mesh_.loc[4], f.ortho ? 1 : 0);
        Uniform1i(mesh_.loc[5], (f.useVertexColors && meshHasColors_) ? 1 : 0);
        Uniform1i(mesh_.loc[6], f.highlightBackFaces ? 1 : 0);
        Uniform3f(mesh_.loc[7], f.meshColor.x, f.meshColor.y, f.meshColor.z);
        Uniform3f(mesh_.loc[8], f.backColor.x, f.backColor.y, f.backColor.z);
        Uniform1f(mesh_.loc[9], static_cast<float>(f.selectedObject));
        BindVertexArray(mainSet_.vao);
        glDrawElements(GL_TRIANGLES, mainSet_.count, GL_UNSIGNED_INT, nullptr);
        UseProgram(flat_.id);
    } else if (!f.xrayWire) {
        // Hidden-line wireframe: lay down depth only, then draw the edges on top.
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        setFlat(mvp, 0, 0, 0, 1, 0.0f);
        BindVertexArray(mainSet_.vao);
        glDrawElements(GL_TRIANGLES, mainSet_.count, GL_UNSIGNED_INT, nullptr);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);

    // ------------------------------------------------------------- wireframe
    if (f.wireframe || f.wireOverlay) {
        if (f.wireframe && f.xrayWire) glDisable(GL_DEPTH_TEST);
        glLineWidth(1.0f);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        BindVertexArray(mainSet_.vao);
        if (f.wireframe) {
            if (lightBg) setFlat(mvp, 0.08f, 0.09f, 0.11f, 1.0f, 0.0f);
            else setFlat(mvp, 0.80f, 0.86f, 0.95f, 1.0f, 0.0f);
            glDrawElements(GL_TRIANGLES, mainSet_.count, GL_UNSIGNED_INT, nullptr);
        } else {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            setFlat(mvp, 0.0f, 0.0f, 0.0f, 0.45f, 0.0f);
            glDrawElements(GL_TRIANGLES, mainSet_.count, GL_UNSIGNED_INT, nullptr);
            glDisable(GL_BLEND);
        }
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glEnable(GL_DEPTH_TEST);
    }

    // ------------------------------------------------------------- problem highlights
    if (f.showProblems) {
        if (f.problemsXray) glDisable(GL_DEPTH_TEST);
        constexpr float kBias = 4e-4f;
        glLineWidth(std::min(lineWidthMax_, 2.5f * f.uiScale));

        if (f.showSlivers && sliverSet_.count) {
            setFlat(mvp, 1.0f, 0.60f, 0.12f, 1.0f, kBias);
            BindVertexArray(sliverSet_.vao);
            glDrawElements(GL_TRIANGLES, sliverSet_.count, GL_UNSIGNED_INT, nullptr);
        }
        auto lines = [&](const IndexSet& s, float r, float g, float b) {
            if (!s.count) return;
            setFlat(mvp, r, g, b, 1.0f, kBias * 1.5f);
            BindVertexArray(s.vao);
            glDrawElements(GL_LINES, s.count, GL_UNSIGNED_INT, nullptr);
        };
        if (f.showInconsistent) lines(inconsistentSet_, 0.20f, 0.80f, 1.00f);
        if (f.showNonManifold) lines(nonManifoldSet_, 1.00f, 0.15f, 1.00f);
        if (f.showOpenEdges) lines(boundarySet_, 1.00f, 0.22f, 0.22f);
        if (f.showDegenerate) {
            lines(degenerateSet_, 1.00f, 0.90f, 0.15f);
            if (markerCount_) {
                setFlat(mvp, 1.00f, 0.90f, 0.15f, 1.0f, kBias * 3.0f, 10.0f * f.uiScale, true);
                BindVertexArray(markerVao_);
                glDrawArrays(GL_POINTS, 0, markerCount_);
            }
        }
        glEnable(GL_DEPTH_TEST);
    }

    BindVertexArray(0);
    UseProgram(0);
    glLineWidth(1.0f);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
}

}  // namespace mi
