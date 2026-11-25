// SPDX-License-Identifier: Apache-2.0
// Separate objects: selecting them, the move gizmo, and the slicer-style "separate objects" arrange.
#include <algorithm>
#include <cmath>

#include "app.h"
#include "imgui.h"
#include "objects.h"

namespace mi {

namespace {

struct Pt {
    float x = 0, y = 0;
};

float distToSegment(Pt p, Pt a, Pt b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len2 = dx * dx + dy * dy;
    float t = len2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float cx = a.x + t * dx - p.x, cy = a.y + t * dy - p.y;
    return std::sqrt(cx * cx + cy * cy);
}

bool insideQuad(Pt p, const Pt q[4]) {
    bool pos = false, neg = false;
    for (int i = 0; i < 4; ++i) {
        const Pt a = q[i], b = q[(i + 1) % 4];
        const float c = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        if (c > 0) pos = true;
        if (c < 0) neg = true;
    }
    return !(pos && neg);
}

const ImU32 kAxisColor[3] = {IM_COL32(236, 84, 84, 255), IM_COL32(118, 205, 98, 255), IM_COL32(84, 148, 240, 255)};

ImU32 brighten(ImU32 c, float amount) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.x = std::min(1.0f, v.x + amount);
    v.y = std::min(1.0f, v.y + amount);
    v.z = std::min(1.0f, v.z + amount);
    return ImGui::ColorConvertFloat4ToU32(v);
}

}  // namespace

// Screen-space description of the gizmo for the selected object.
struct App::GizmoGeometry {
    Vec3 centerWorld;
    Vec3 axisWorld[3];  // unit directions of the file's X / Y / Z axes in world space
    float length = 0;   // world length of the arrows
    Pt center, tip[3];
    Pt plane[3][4];     // plane k is perpendicular to axis k
};

// ============================================================================ state

void App::resetObjectState(bool keepOffsets) {
    const size_t n = objectCount();
    if (!keepOffsets || objectOffsets_.size() != n) {
        objectOffsets_.assign(n, Vec3{});
        selectedObject_ = -1;
    }
    if (selectedObject_ >= static_cast<int>(n)) selectedObject_ = -1;
    gizmoDrag_ = gizmoHover_ = -1;
    renderer_.setObjectOffsets(objectOffsets_);
}

void App::selectObject(int id) {
    selectedObject_ = (id >= 0 && static_cast<size_t>(id) < objectCount()) ? id : -1;
    gizmoHover_ = -1;
}

void App::separateObjects() {
    if (objectCount() < 2) return;
    std::vector<AABB> boxes;
    for (const ObjectInfo& o : model_.analyzed->objects) boxes.push_back(o.bounds);
    objectOffsets_ = arrangeObjects(boxes, view_.zUp ? 2 : 1);
    renderer_.setObjectOffsets(objectOffsets_);
    updateModelTransform(false);
    frameModel();
}

void App::resetObjectPositions() {
    objectOffsets_.assign(objectCount(), Vec3{});
    renderer_.setObjectOffsets(objectOffsets_);
    updateModelTransform(false);
    frameModel();
}

// ============================================================================ geometry

// Cursor position inside the 3D view as normalised device coordinates.
bool App::viewportPoint(float& ndcX, float& ndcY, float& aspect) const {
    const float w = viewport_.x1 - viewport_.x0, h = viewport_.y1 - viewport_.y0;
    if (w < 1 || h < 1) return false;
    const ImVec2 mp = ImGui::GetIO().MousePos;
    ndcX = ((mp.x - viewport_.x0) / w) * 2.0f - 1.0f;
    ndcY = 1.0f - ((mp.y - viewport_.y0) / h) * 2.0f;
    aspect = w / h;
    return true;
}

bool App::computeGizmo(GizmoGeometry& g) const {
    if (selectedObject_ < 0 || static_cast<size_t>(selectedObject_) >= objectCount() || view_.hideUi) return false;
    const float w = viewport_.x1 - viewport_.x0, h = viewport_.y1 - viewport_.y0;
    if (w < 1 || h < 1) return false;
    const ObjectInfo& obj = model_.analyzed->objects[static_cast<size_t>(selectedObject_)];

    g.centerWorld = transformPoint(modelMatrix_, obj.bounds.center() + objectOffset(static_cast<size_t>(selectedObject_)));
    const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int i = 0; i < 3; ++i) g.axisWorld[i] = transformPoint(modelMatrix_, axes[i]);

    const float depth = dot(g.centerWorld - camera_.position(), camera_.forward());
    if (!camera_.orthographic && depth <= 1e-4f) return false;
    g.length = 88.0f * uiScale_ * camera_.worldPerPixel(depth, h);

    const Mat4 vp = camera_.projectionMatrix(w / h) * camera_.viewMatrix();
    auto project = [&](const Vec3& p, Pt& out) {
        const float cw = vp.at(3, 0) * p.x + vp.at(3, 1) * p.y + vp.at(3, 2) * p.z + vp.at(3, 3);
        if (cw <= 1e-6f) return false;
        const float cx = (vp.at(0, 0) * p.x + vp.at(0, 1) * p.y + vp.at(0, 2) * p.z + vp.at(0, 3)) / cw;
        const float cy = (vp.at(1, 0) * p.x + vp.at(1, 1) * p.y + vp.at(1, 2) * p.z + vp.at(1, 3)) / cw;
        out.x = viewport_.x0 + (cx * 0.5f + 0.5f) * w;
        out.y = viewport_.y0 + (1.0f - (cy * 0.5f + 0.5f)) * h;
        return true;
    };
    if (!project(g.centerWorld, g.center)) return false;
    for (int i = 0; i < 3; ++i)
        if (!project(g.centerWorld + g.axisWorld[i] * g.length, g.tip[i])) return false;
    for (int k = 0; k < 3; ++k) {
        const Vec3 a = g.axisWorld[(k + 1) % 3], b = g.axisWorld[(k + 2) % 3];
        const float lo = 0.28f * g.length, hi = 0.55f * g.length;
        const Vec3 corners[4] = {a * lo + b * lo, a * hi + b * lo, a * hi + b * hi, a * lo + b * hi};
        for (int c = 0; c < 4; ++c)
            if (!project(g.centerWorld + corners[c], g.plane[k][c])) return false;
    }
    return true;
}

int App::hitTestGizmo(const GizmoGeometry& g, float mx, float my) const {
    const Pt m{mx, my};
    const float reach = 9.0f * uiScale_;
    int best = -1;
    float bestDist = reach;
    for (int i = 0; i < 3; ++i) {
        const float d = distToSegment(m, g.center, g.tip[i]);
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    if (best >= 0) return best;
    for (int k = 0; k < 3; ++k)
        if (insideQuad(m, g.plane[k])) return 3 + k;
    return -1;
}

// ============================================================================ interaction

bool App::beginGizmoDrag(int handle, float mx, float my) {
    (void)mx;
    (void)my;
    GizmoGeometry g;
    float nx, ny, aspect;
    if (!computeGizmo(g) || !viewportPoint(nx, ny, aspect)) return false;
    Vec3 o, d;
    camera_.ray(nx, ny, aspect, o, d);
    gizmoStartCenter_ = g.centerWorld;
    gizmoStartOffset_ = objectOffset(static_cast<size_t>(selectedObject_));
    if (handle < 3) {
        if (!rayLineParameter(o, d, g.centerWorld, g.axisWorld[handle], gizmoStartT_)) return false;
    } else {
        if (!rayPlaneIntersection(o, d, g.centerWorld, g.axisWorld[handle - 3], gizmoStartHit_)) return false;
    }
    gizmoDrag_ = handle;
    return true;
}

void App::updateGizmoDrag(float, float) {
    float nx, ny, aspect;
    if (gizmoDrag_ < 0 || selectedObject_ < 0 || !viewportPoint(nx, ny, aspect)) return;
    Vec3 o, d;
    camera_.ray(nx, ny, aspect, o, d);

    // The drag is evaluated against the gizmo as it was when the drag began, so the object does not
    // "run away" from the cursor while it moves.
    const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    Vec3 offset = gizmoStartOffset_;
    if (gizmoDrag_ < 3) {
        const Vec3 axisWorld = transformPoint(modelMatrix_, axes[gizmoDrag_]);
        float t;
        if (!rayLineParameter(o, d, gizmoStartCenter_, axisWorld, t)) return;
        offset = offset + axes[gizmoDrag_] * (t - gizmoStartT_);  // the model matrix is a pure rotation: lengths are preserved
    } else {
        const Vec3 normal = transformPoint(modelMatrix_, axes[gizmoDrag_ - 3]);
        Vec3 hit;
        if (!rayPlaneIntersection(o, d, gizmoStartCenter_, normal, hit)) return;
        offset = offset + transformPoint(invModelMatrix_, hit - gizmoStartHit_);
    }
    objectOffsets_[static_cast<size_t>(selectedObject_)] = offset;
    renderer_.setObjectOffsets(objectOffsets_);
}

void App::pickAt(float mx, float my) {
    (void)mx;
    (void)my;
    float nx, ny, aspect;
    if (!model_.analyzed || objectCount() == 0 || !viewportPoint(nx, ny, aspect)) return;
    Vec3 o, d;
    camera_.ray(nx, ny, aspect, o, d);
    // into the mesh's own coordinates (the model matrix is a pure rotation)
    const Vec3 om = transformPoint(invModelMatrix_, o);
    const Vec3 dm = normalize(transformPoint(invModelMatrix_, d));
    selectObject(pickObject(*model_.analyzed, objectOffsets_, om, dm));
}

// ============================================================================ drawing

void App::drawObjectGizmo() {
    gizmoHover_ = -1;
    GizmoGeometry g;
    if (!computeGizmo(g)) return;
    const ImGuiIO& io = ImGui::GetIO();
    const float s = uiScale_;
    const bool inside = io.MousePos.x >= viewport_.x0 && io.MousePos.x < viewport_.x1 && io.MousePos.y >= viewport_.y0 &&
                        io.MousePos.y < viewport_.y1;
    if (gizmoDrag_ < 0 && inside && !io.WantCaptureMouse && dragButton_ < 0) gizmoHover_ = hitTestGizmo(g, io.MousePos.x, io.MousePos.y);
    const int active = gizmoDrag_ >= 0 ? gizmoDrag_ : gizmoHover_;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    // plane handles first, so the arrows stay on top
    for (int k = 0; k < 3; ++k) {
        ImVec2 q[4];
        for (int c = 0; c < 4; ++c) q[c] = ImVec2(g.plane[k][c].x, g.plane[k][c].y);
        const bool on = active == 3 + k;
        ImVec4 col = ImGui::ColorConvertU32ToFloat4(kAxisColor[k]);
        dl->AddConvexPolyFilled(q, 4, ImGui::ColorConvertFloat4ToU32(ImVec4(col.x, col.y, col.z, on ? 0.75f : 0.35f)));
        dl->AddPolyline(q, 4, ImGui::ColorConvertFloat4ToU32(ImVec4(col.x, col.y, col.z, on ? 1.0f : 0.8f)), ImDrawFlags_Closed, (on ? 2.0f : 1.2f) * s);
    }
    for (int i = 0; i < 3; ++i) {
        const bool on = active == i;
        const ImU32 col = on ? brighten(kAxisColor[i], 0.25f) : kAxisColor[i];
        const ImVec2 c(g.center.x, g.center.y), t(g.tip[i].x, g.tip[i].y);
        float dx = t.x - c.x, dy = t.y - c.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.0f) continue;
        dx /= len;
        dy /= len;
        const float head = 13.0f * s;
        const ImVec2 base(t.x - dx * head, t.y - dy * head);
        dl->AddLine(ImVec2(c.x + 0.0f, c.y), base, IM_COL32(0, 0, 0, 120), (on ? 5.5f : 4.5f) * s);  // dark outline for contrast
        dl->AddLine(c, base, col, (on ? 3.5f : 2.5f) * s);
        const ImVec2 left(base.x - dy * head * 0.45f, base.y + dx * head * 0.45f);
        const ImVec2 right(base.x + dy * head * 0.45f, base.y - dx * head * 0.45f);
        dl->AddTriangleFilled(t, left, right, col);
    }
    dl->AddCircleFilled(ImVec2(g.center.x, g.center.y), 4.0f * s, IM_COL32(245, 247, 252, 255));
}

void App::drawObjectsSection() {
    const size_t n = objectCount();
    if (n < 2) return;
    char head[64];
    std::snprintf(head, sizeof head, "Objects (%zu)###objects", n);
    if (!ImGui::CollapsingHeader(head, ImGuiTreeNodeFlags_DefaultOpen)) return;

    if (ImGui::Button("Separate objects")) separateObjects();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lay the objects out side by side on the bed, like a slicer's Arrange.");
    ImGui::SameLine();
    if (ImGui::Button("Reset positions")) resetObjectPositions();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Click an object (here or in the 3D view) to select it, then drag the arrows to move it.");
    ImGui::PopStyleColor();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(n));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const ObjectInfo& o = model_.analyzed->objects[static_cast<size_t>(i)];
            char label[96];
            std::snprintf(label, sizeof label, "Object %d   %u triangles###obj%d", i + 1, o.triangles, i);
            if (ImGui::Selectable(label, selectedObject_ == i)) selectObject(selectedObject_ == i ? -1 : i);
        }
    }
}

}  // namespace mi
