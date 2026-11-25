// SPDX-License-Identifier: Apache-2.0
// All Dear ImGui user interface code: custom title bar + menus, report panel, overlays, dialogs.
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

#include "app.h"
#include "imgui.h"
#include "mesh_io.h"

#ifndef MESHINSPECTOR_VERSION
#define MESHINSPECTOR_VERSION "dev"
#endif

namespace mi {

namespace {

const ImVec4 kGood{0.30f, 0.78f, 0.46f, 1.0f};
const ImVec4 kBad{0.96f, 0.34f, 0.34f, 1.0f};
const ImVec4 kWarn{0.96f, 0.72f, 0.24f, 1.0f};
const ImVec4 kInfo{0.45f, 0.62f, 0.90f, 1.0f};

// Colours used for the problem highlights in the 3D view (must match renderer.cpp).
const ImVec4 kOpenEdgeColor{1.00f, 0.22f, 0.22f, 1.0f};
const ImVec4 kNonManifoldColor{1.00f, 0.15f, 1.00f, 1.0f};
const ImVec4 kInconsistentColor{0.20f, 0.80f, 1.00f, 1.0f};
const ImVec4 kDegenerateColor{1.00f, 0.90f, 0.15f, 1.0f};
const ImVec4 kSliverColor{1.00f, 0.60f, 0.12f, 1.0f};

ImU32 u32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }
ImVec4 withAlpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}

std::string fmtCount(size_t n) {
    std::string s = std::to_string(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), ",");
    return s;
}
std::string fmtNum(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.5g", v);
    return buf;
}
std::string fmtBytes(uintmax_t b) {
    char buf[64];
    if (b >= (1ull << 30)) std::snprintf(buf, sizeof buf, "%.2f GB", static_cast<double>(b) / (1ull << 30));
    else if (b >= (1ull << 20)) std::snprintf(buf, sizeof buf, "%.2f MB", static_cast<double>(b) / (1ull << 20));
    else if (b >= (1ull << 10)) std::snprintf(buf, sizeof buf, "%.1f KB", static_cast<double>(b) / (1ull << 10));
    else std::snprintf(buf, sizeof buf, "%llu B", static_cast<unsigned long long>(b));
    return buf;
}

void drawLogo(ImDrawList* dl, ImVec2 c, float r) {
    ImVec2 p[6];
    for (int i = 0; i < 6; ++i) {
        const float a = radians(-90.0f + 60.0f * static_cast<float>(i));
        p[i] = ImVec2(c.x + r * std::cos(a), c.y + r * std::sin(a));
    }
    const ImVec2 top[4] = {p[5], p[0], p[1], c};
    const ImVec2 right[4] = {c, p[1], p[2], p[3]};
    const ImVec2 left[4] = {c, p[3], p[4], p[5]};
    dl->AddConvexPolyFilled(top, 4, IM_COL32(130, 190, 255, 255));
    dl->AddConvexPolyFilled(right, 4, IM_COL32(70, 125, 215, 255));
    dl->AddConvexPolyFilled(left, 4, IM_COL32(45, 90, 175, 255));
}

enum class Sev { Good, Bad, Warn, Info };
ImVec4 sevColor(Sev s) {
    switch (s) {
        case Sev::Good: return kGood;
        case Sev::Bad: return kBad;
        case Sev::Warn: return kWarn;
        default: return kInfo;
    }
}

}  // namespace

// ============================================================================ layout

void App::buildUi() {
    const ImVec2 size = ImGui::GetMainViewport()->Size;
    if (view_.hideUi) {
        // Viewer-only mode: the model fills the whole window. With no title bar left to grab, the
        // window can still be resized from its edges; H or Esc brings the interface back.
        viewport_ = {0, 0, size.x, size.y};
        frameRegions_.titleBarHeight = 0;
        frameRegions_.menuLeft = frameRegions_.menuRight = 0;
        frameRegions_.buttonsLeft = static_cast<int>(size.x) + 1;
        drawEmptyState();
        drawHideHint();
        drawModals();
        return;
    }
    viewport_.x0 = 0;
    viewport_.y0 = titleBarH_;
    viewport_.x1 = view_.showReport ? size.x - panelW_ : size.x;
    viewport_.y1 = size.y - statusBarH_;

    drawTitleBar();
    if (view_.showReport) drawReportPanel();
    drawStatusBar();
    drawToolbar();
    drawGizmo();
    drawEmptyState();
    drawModals();
}

// ============================================================================ title bar

void App::drawTitleBar() {
    const float s = uiScale_;
    const float h = titleBarH_;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float W = vp->Size.x;
    const ImVec2 o = vp->Pos;

    ImGui::SetNextWindowPos(o);
    ImGui::SetNextWindowSize(ImVec2(W, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.082f, 0.088f, 0.102f, 1.0f));
    ImGui::Begin("##titlebar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float textY = o.y + (h - ImGui::GetFontSize()) * 0.5f;

    float x = 12.0f * s;
    drawLogo(dl, ImVec2(o.x + x + 9.0f * s, o.y + h * 0.5f), 9.0f * s);
    x += 26.0f * s;
    dl->AddText(ImVec2(o.x + x, textY), IM_COL32(205, 210, 222, 255), kAppName);
    x += ImGui::CalcTextSize(kAppName).x + 18.0f * s;

    // ---- menu headers
    static const char* kNames[4] = {"File", "View", "Options", "Help"};
    static const char* kIds[4] = {"##menu_file", "##menu_view", "##menu_options", "##menu_help"};
    frameRegions_.menuLeft = static_cast<int>(o.x + x);
    int toOpen = -2;  // -2 nothing, -1 close, >=0 open that menu
    for (int i = 0; i < 4; ++i) {
        const float w = ImGui::CalcTextSize(kNames[i]).x + 24.0f * s;
        menuX_[i] = o.x + x;
        ImGui::SetCursorScreenPos(ImVec2(o.x + x, o.y));
        ImGui::PushID(i);
        ImGui::InvisibleButton("header", ImVec2(w, h));
        if (ImGui::IsItemActivated()) toOpen = (openMenu_ == i) ? -1 : i;
        // Allow hover-to-switch while another menu is open (popups normally block hovering).
        const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        if (hovered && openMenu_ >= 0 && openMenu_ != i) toOpen = i;
        ImGui::PopID();
        if (openMenu_ == i || hovered)
            dl->AddRectFilled(ImVec2(o.x + x, o.y), ImVec2(o.x + x + w, o.y + h), IM_COL32(255, 255, 255, openMenu_ == i ? 38 : 22));
        dl->AddText(ImVec2(o.x + x + 12.0f * s, textY), IM_COL32(236, 239, 246, 255), kNames[i]);
        x += w;
    }
    frameRegions_.menuRight = static_cast<int>(o.x + x);

    // ---- window buttons (only when we draw our own frame)
    const float bw = 46.0f * s;
    if (customFrame_) {
        frameRegions_.buttonsLeft = static_cast<int>(o.x + W - 3.0f * bw);
        enum { kMin, kMax, kClose };
        const bool maximized = glfwGetWindowAttrib(window_, GLFW_MAXIMIZED) == GLFW_TRUE;
        for (int b = 0; b < 3; ++b) {
            const float bx = o.x + W - (3 - b) * bw;
            ImGui::SetCursorScreenPos(ImVec2(bx, o.y));
            ImGui::PushID(100 + b);
            const bool clicked = ImGui::InvisibleButton("winbtn", ImVec2(bw, h));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            if (hovered) {
                const ImU32 bg = b == kClose ? IM_COL32(232, 17, 35, 255) : IM_COL32(255, 255, 255, 28);
                dl->AddRectFilled(ImVec2(bx, o.y), ImVec2(bx + bw, o.y + h), bg);
            }
            const ImVec2 c(bx + bw * 0.5f, o.y + h * 0.5f);
            const float k = 5.0f * s;
            const ImU32 ink = IM_COL32(240, 242, 248, 255);
            const float th = std::max(1.0f, std::round(s));
            if (b == kMin) {
                dl->AddLine(ImVec2(c.x - k, c.y), ImVec2(c.x + k, c.y), ink, th);
            } else if (b == kMax) {
                if (maximized) {
                    dl->AddRect(ImVec2(c.x - k, c.y - k + 2 * s), ImVec2(c.x + k - 2 * s, c.y + k), ink, 0, 0, th);
                    dl->AddLine(ImVec2(c.x - k + 2 * s, c.y - k + 2 * s), ImVec2(c.x - k + 2 * s, c.y - k), ink, th);
                    dl->AddLine(ImVec2(c.x - k + 2 * s, c.y - k), ImVec2(c.x + k, c.y - k), ink, th);
                    dl->AddLine(ImVec2(c.x + k, c.y - k), ImVec2(c.x + k, c.y + k - 2 * s), ink, th);
                    dl->AddLine(ImVec2(c.x + k - 2 * s, c.y + k - 2 * s), ImVec2(c.x + k, c.y + k - 2 * s), ink, th);
                } else {
                    dl->AddRect(ImVec2(c.x - k, c.y - k), ImVec2(c.x + k, c.y + k), ink, 0, 0, th);
                }
            } else {
                dl->AddLine(ImVec2(c.x - k, c.y - k), ImVec2(c.x + k, c.y + k), ink, th);
                dl->AddLine(ImVec2(c.x - k, c.y + k), ImVec2(c.x + k, c.y - k), ink, th);
            }
            if (clicked) {
                if (b == kMin) glfwIconifyWindow(window_);
                else if (b == kMax) maximized ? glfwRestoreWindow(window_) : glfwMaximizeWindow(window_);
                else glfwSetWindowShouldClose(window_, GLFW_TRUE);
            }
        }
    } else {
        frameRegions_.buttonsLeft = static_cast<int>(W) + 1;
    }
    frameRegions_.titleBarHeight = static_cast<int>(h);

    // ---- file name in the middle of the bar
    if (!model_.path.empty()) {
        const std::string label = model_.path.filename().u8string();
        const float tw = ImGui::CalcTextSize(label.c_str()).x;
        const float lo = static_cast<float>(frameRegions_.menuRight) + 24.0f * s;
        const float hi = customFrame_ ? o.x + W - 3.0f * bw - 24.0f * s : o.x + W - 24.0f * s;
        float tx = o.x + (W - tw) * 0.5f;
        tx = std::max(tx, lo);
        if (tx + tw <= hi) dl->AddText(ImVec2(tx, textY), IM_COL32(150, 156, 170, 255), label.c_str());
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // ---- the drop-down itself
    if (toOpen == -1) {
        openMenu_ = -1;
    } else if (toOpen >= 0 && toOpen != openMenu_) {
        openMenu_ = toOpen;
        ImGui::OpenPopup(kIds[toOpen]);
    }
    if (openMenu_ >= 0) {
        ImGui::SetNextWindowPos(ImVec2(menuX_[openMenu_], o.y + h));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f * s, 6.0f * s));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f * s);
        if (ImGui::BeginPopup(kIds[openMenu_], ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
            drawMenu(openMenu_);
            ImGui::EndPopup();
        } else {
            openMenu_ = -1;  // closed by clicking elsewhere or choosing an item
        }
        ImGui::PopStyleVar(2);
    }
}

void App::drawMenu(int index) {
    const float s = uiScale_;
    switch (index) {
        case 0:  // File
            if (ImGui::MenuItem("Open...", "Ctrl+O")) openDialogRequested_ = true;
            if (ImGui::MenuItem("Reload", "F5", false, !model_.path.empty())) reload();
            ImGui::MenuItem("Auto-reload when the file changes", nullptr, &view_.autoReload);
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) glfwSetWindowShouldClose(window_, GLFW_TRUE);
            break;

        case 1:  // View
            ImGui::MenuItem("Wireframe", "W", &view_.wireframe);
            ImGui::MenuItem("Wireframe overlay on surface", nullptr, &view_.wireOverlay);
            ImGui::MenuItem("Wireframe shows hidden edges", nullptr, &view_.xrayWire);
            ImGui::Separator();
            ImGui::MenuItem("Flat shading", nullptr, &view_.flatShading);
            ImGui::MenuItem("Highlight back faces", nullptr, &view_.highlightBackFaces);
            if (ImGui::BeginMenu("Problem highlights")) {
                ImGui::MenuItem("Show problem highlights", nullptr, &view_.showProblems);
                ImGui::Separator();
                ImGui::MenuItem("Open edges (gaps)", nullptr, &view_.showOpenEdges);
                ImGui::MenuItem("Non-manifold edges", nullptr, &view_.showNonManifold);
                ImGui::MenuItem("Inconsistent winding", nullptr, &view_.showInconsistent);
                ImGui::MenuItem("Degenerate triangles", nullptr, &view_.showDegenerate);
                ImGui::MenuItem("Sliver triangles", nullptr, &view_.showSlivers);
                ImGui::Separator();
                ImGui::MenuItem("Visible through the surface", nullptr, &view_.problemsXray);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            ImGui::MenuItem("Grid", "G", &view_.showGrid);
            ImGui::MenuItem("Orientation gizmo", nullptr, &view_.showGizmo);
            ImGui::MenuItem("Orthographic projection", nullptr, &view_.orthographic);
            ImGui::MenuItem("Mesh report panel", "Tab", &view_.showReport);
            if (ImGui::MenuItem("Hide interface", "H")) setHideUi(true);
            ImGui::Separator();
            if (ImGui::BeginMenu("Standard views")) {
                if (ImGui::MenuItem("Front", "1")) camera_.setView(0, 0);
                if (ImGui::MenuItem("Back", "2")) camera_.setView(180, 0);
                if (ImGui::MenuItem("Left", "3")) camera_.setView(-90, 0);
                if (ImGui::MenuItem("Right", "4")) camera_.setView(90, 0);
                if (ImGui::MenuItem("Top", "5")) camera_.setView(0, 90);
                if (ImGui::MenuItem("Bottom", "6")) camera_.setView(0, -90);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Separate objects", nullptr, false, objectCount() >= 2)) separateObjects();
            if (ImGui::MenuItem("Reset object positions", nullptr, false, objectCount() >= 1)) resetObjectPositions();
            ImGui::Separator();
            if (ImGui::MenuItem("Frame model", "F", false, !model_.path.empty())) frameModel();
            if (ImGui::MenuItem("Reset view", "Home")) resetView();
            break;

        case 2:  // Options
            if (ImGui::BeginMenu("Up axis")) {
                if (ImGui::MenuItem("Y is up   (glTF, OBJ)", nullptr, !view_.zUp)) {
                    view_.zUp = false;
                    updateModelTransform(true);
                }
                if (ImGui::MenuItem("Z is up   (STL, CAD, 3D printing)", nullptr, view_.zUp)) {
                    view_.zUp = true;
                    updateModelTransform(true);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Mesh analysis")) {
                ImGui::TextDisabled("Changes are applied when you release the slider.");
                ImGui::SetNextItemWidth(240.0f * s);
                ImGui::SliderFloat("Weld tolerance", &view_.weldExponent, 3.0f, 9.0f, "1e-%.1f of model size");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    view_.analysis.weldTolerance = std::pow(10.0, -static_cast<double>(view_.weldExponent));
                    reanalyze();
                }
                ImGui::TextDisabled("Vertices closer than this are one point; thinner");
                ImGui::TextDisabled("triangles count as degenerate.");
                ImGui::Spacing();
                float angle = static_cast<float>(view_.analysis.sliverAngleDeg);
                ImGui::SetNextItemWidth(240.0f * s);
                ImGui::SliderFloat("Sliver angle", &angle, 0.1f, 10.0f, "%.1f deg");
                view_.analysis.sliverAngleDeg = angle;
                if (ImGui::IsItemDeactivatedAfterEdit()) reanalyze();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Colors")) {
                ImGui::ColorEdit3("Background", view_.background, ImGuiColorEditFlags_NoInputs);
                ImGui::ColorEdit3("Surface", view_.meshColor, ImGuiColorEditFlags_NoInputs);
                ImGui::ColorEdit3("Back faces", view_.backColor, ImGuiColorEditFlags_NoInputs);
                ImGui::EndMenu();
            }
            ImGui::MenuItem("Use colors stored in the file", nullptr, &view_.useVertexColors);
            ImGui::MenuItem("Vertical sync", nullptr, &view_.vsync);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset all options")) resetOptions();
            break;

        case 3:  // Help
            if (ImGui::MenuItem("Controls and shortcuts...")) showControls_ = true;
            if (ImGui::MenuItem("About...")) showAbout_ = true;
            break;
        default:
            break;
    }
}

// ============================================================================ viewport overlays

void App::drawToolbar() {
    const float s = uiScale_;
    ImGui::SetNextWindowPos(ImVec2(viewport_.x0 + 12.0f * s, viewport_.y0 + 10.0f * s));
    ImGui::SetNextWindowBgAlpha(0.78f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * s, 8.0f * s));
    ImGui::Begin("##toolbar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    ImGui::Checkbox("Wireframe", &view_.wireframe);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle wireframe  (W)");
    ImGui::SameLine(0.0f, 18.0f * s);
    ImGui::Checkbox("Highlight problems", &view_.showProblems);
    if (objectCount() >= 2) {
        ImGui::SameLine(0.0f, 18.0f * s);
        if (ImGui::Button("Separate objects")) separateObjects();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lay the objects out side by side on the bed, like a slicer's Arrange.");
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void App::drawGizmo() {
    if (!view_.showGizmo) return;
    const float s = uiScale_;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float r = 36.0f * s;
    const ImVec2 c(viewport_.x0 + 24.0f * s + r, viewport_.y1 - 24.0f * s - r);

    struct Axis {
        float depth;
        ImVec2 tip;
        ImU32 color;
        const char* label;
    } axes[3];
    const Vec3 dirs[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const ImU32 colors[3] = {IM_COL32(236, 84, 84, 255), IM_COL32(118, 205, 98, 255), IM_COL32(84, 148, 240, 255)};
    const char* labels[3] = {"X", "Y", "Z"};
    const Vec3 R = camera_.right(), U = camera_.up(), F = camera_.forward();
    for (int i = 0; i < 3; ++i) {
        const Vec3 v = transformPoint(modelMatrix_, dirs[i]);  // the file's own axes
        axes[i] = {dot(v, F), ImVec2(c.x + dot(v, R) * r, c.y - dot(v, U) * r), colors[i], labels[i]};
    }
    std::sort(axes, axes + 3, [](const Axis& a, const Axis& b) { return a.depth > b.depth; });  // far first

    dl->AddCircleFilled(c, r + 10.0f * s, IM_COL32(0, 0, 0, 55));
    for (const Axis& a : axes) {
        dl->AddLine(c, a.tip, a.color, 2.5f * s);
        dl->AddCircleFilled(a.tip, 8.5f * s, a.color);
        const ImVec2 ts = ImGui::CalcTextSize(a.label);
        dl->AddText(ImVec2(a.tip.x - ts.x * 0.5f, a.tip.y - ts.y * 0.5f), IM_COL32(20, 22, 28, 255), a.label);
    }
}

void App::drawHideHint() {
    const double left = hideHintUntil_ - glfwGetTime();
    if (left <= 0) return;
    const float s = uiScale_;
    const char* text = "Press H or Esc to show the interface";
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float alpha = static_cast<float>(std::min(1.0, left));  // fade out during the last second
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 a((viewport_.x0 + viewport_.x1 - ts.x) * 0.5f - 14.0f * s, viewport_.y1 - ts.y - 40.0f * s);
    const ImVec2 b(a.x + ts.x + 28.0f * s, a.y + ts.y + 14.0f * s);
    dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, static_cast<int>(170 * alpha)), 6.0f * s);
    dl->AddText(ImVec2(a.x + 14.0f * s, a.y + 7.0f * s), IM_COL32(235, 238, 245, static_cast<int>(255 * alpha)), text);
}

void App::drawEmptyState() {
    if (!model_.path.empty() && !jobRunning_) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float cx = (viewport_.x0 + viewport_.x1) * 0.5f, cy = (viewport_.y0 + viewport_.y1) * 0.5f;
    const float s = uiScale_;
    ImFont* font = ImGui::GetFont();
    const float big = ImGui::GetFontSize() * 1.6f;

    const std::string title = jobRunning_ ? "Loading " + jobLabel_ + "..." : "Drop a 3D file here";
    const ImVec2 ts = font->CalcTextSizeA(big, FLT_MAX, 0.0f, title.c_str());
    dl->AddText(font, big, ImVec2(cx - ts.x * 0.5f, cy - ts.y - 4.0f * s), IM_COL32(190, 198, 214, 255), title.c_str());
    if (!jobRunning_) {
        const std::string sub = std::string("or press Ctrl+O     ") + supportedFormatsText();
        const ImVec2 ss = ImGui::CalcTextSize(sub.c_str());
        dl->AddText(ImVec2(cx - ss.x * 0.5f, cy + 6.0f * s), IM_COL32(125, 133, 150, 255), sub.c_str());
    }
}

void App::drawStatusBar() {
    const float s = uiScale_;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - statusBarH_));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, statusBarH_));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * s, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.082f, 0.088f, 0.102f, 1.0f));
    ImGui::Begin("##statusbar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
    const float y = (statusBarH_ - ImGui::GetFontSize()) * 0.5f;
    ImGui::SetCursorPosY(y);

    if (jobRunning_) {
        ImGui::TextColored(kInfo, "Working on %s ...", jobLabel_.c_str());
    } else if (model_.analyzed) {
        const MeshReport& r = model_.analyzed->report;
        if (r.triangles == 0) {
            ImGui::TextColored(kWarn, "No triangles in this file");
        } else if (r.closedManifold && r.degenerateTriangles == 0 && r.duplicateTriangles == 0 && r.normalsOutward) {
            ImGui::TextColored(kGood, "Watertight - no problems found");
        } else {
            ImGui::TextColored(kBad, "%s open edges   %s non-manifold   %s degenerate triangles", fmtCount(r.boundaryEdges).c_str(),
                               fmtCount(r.nonManifoldEdges).c_str(), fmtCount(r.degenerateTriangles).c_str());
        }
        ImGui::SameLine(0, 18.0f * s);
        ImGui::TextDisabled("%s triangles   %s vertices", fmtCount(r.triangles).c_str(), fmtCount(r.weldedVertices).c_str());
    } else {
        ImGui::TextDisabled("Ready");
    }

    char right[64];
    std::snprintf(right, sizeof right, "%.0f fps", fps_);
    const float rw = ImGui::CalcTextSize(right).x;
    ImGui::SameLine(vp->Size.x - rw - 12.0f * s);
    ImGui::TextDisabled("%s", right);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ============================================================================ report panel

void App::drawReportPanel() {
    const float s = uiScale_;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - panelW_, vp->Pos.y + titleBarH_));
    ImGui::SetNextWindowSize(ImVec2(panelW_, vp->Size.y - titleBarH_ - statusBarH_));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * s, 12.0f * s));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.118f, 0.126f, 0.145f, 1.0f));
    ImGui::Begin("##report", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (!model_.analyzed) {
        ImGui::TextDisabled("Mesh report");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped("Open a model to see its quality report: open edges (gaps), degenerate triangles, non-manifold edges, "
                           "orientation and more.");
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        return;
    }

    const MeshReport& r = model_.analyzed->report;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ---- file
    ImGui::TextUnformatted(model_.path.filename().u8string().c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s  -  %s", model_.raw ? model_.raw->format.c_str() : "?", fmtBytes(model_.fileBytes).c_str());
    char timing[96];
    std::snprintf(timing, sizeof timing, "loaded in %.0f ms, analysed in %.0f ms", model_.loadSeconds * 1000.0, model_.analyzeSeconds * 1000.0);
    ImGui::TextUnformatted(timing);
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // ---- verdict banner
    std::vector<const char*> issues;
    if (r.triangles > 0) {
        if (r.boundaryEdges) issues.push_back("open edges");
        if (r.nonManifoldEdges) issues.push_back("non-manifold edges");
        if (r.inconsistentEdges) issues.push_back("inconsistent winding");
        if (r.degenerateTriangles) issues.push_back("degenerate triangles");
        if (r.duplicateTriangles) issues.push_back("duplicate triangles");
        if (r.closedManifold && !r.normalsOutward) issues.push_back("inverted normals");
    }
    {
        ImVec4 tone = kGood;
        std::string title, sub;
        if (r.triangles == 0) {
            tone = kWarn;
            title = "Nothing to analyse";
            sub = "The file has no triangles.";
        } else if (issues.empty()) {
            title = "No problems found";
            sub = "Watertight, manifold, consistently oriented.";
            const size_t warnings = r.sliverTriangles + r.looseVertices;
            if (warnings) {
                tone = kWarn;
                sub += " " + fmtCount(warnings) + " minor warning(s).";
            }
        } else {
            tone = kBad;
            title = std::to_string(issues.size()) + (issues.size() == 1 ? " issue to fix" : " issues to fix");
            for (size_t i = 0; i < issues.size(); ++i) sub += (i ? ", " : "") + std::string(issues[i]);
        }
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const float titleSize = ImGui::GetFontSize() * 1.2f;
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + w - 24.0f * s);
        const ImVec2 subSize = ImGui::CalcTextSize(sub.c_str(), nullptr, false, w - 24.0f * s);
        ImGui::PopTextWrapPos();
        const float hh = 12.0f * s + titleSize + 4.0f * s + subSize.y + 12.0f * s;
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + hh), u32(withAlpha(tone, 0.16f)), 6.0f * s);
        dl->AddRect(p, ImVec2(p.x + w, p.y + hh), u32(withAlpha(tone, 0.55f)), 6.0f * s);
        dl->AddRectFilled(p, ImVec2(p.x + 4.0f * s, p.y + hh), u32(tone), 6.0f * s, ImDrawFlags_RoundCornersLeft);
        dl->AddText(ImGui::GetFont(), titleSize, ImVec2(p.x + 14.0f * s, p.y + 10.0f * s), u32(tone), title.c_str());
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x + 14.0f * s, p.y + 12.0f * s + titleSize + 2.0f * s),
                    IM_COL32(200, 206, 218, 255), sub.c_str(), nullptr, w - 24.0f * s);
        ImGui::Dummy(ImVec2(w, hh));
    }
    ImGui::Spacing();

    // ---- checks
    auto row = [&](const char* label, const std::string& value, Sev sev, bool* visible, const ImVec4& visColor, const char* tip) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float lh = ImGui::GetTextLineHeight();
        dl->AddCircleFilled(ImVec2(p.x + 6.0f * s, p.y + lh * 0.5f), 4.5f * s, u32(sevColor(sev)));
        ImGui::Dummy(ImVec2(1, lh));
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(label);
        if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::TableSetColumnIndex(2);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth() - ImGui::CalcTextSize(value.c_str()).x -
                             ImGui::GetStyle().CellPadding.x);
        ImGui::TextUnformatted(value.c_str());
        ImGui::TableSetColumnIndex(3);
        if (visible) {
            ImGui::PushID(label);
            ImGui::PushStyleColor(ImGuiCol_CheckMark, visColor);
            ImGui::Checkbox("##show", visible);
            ImGui::PopStyleColor();
            ImGui::PopID();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Highlight in the 3D view");
        }
    };
    auto subRow = [&](const std::string& text) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("%s", text.c_str());
    };

    ImGui::TextDisabled("CHECKS");
    if (ImGui::BeginTable("checks", 4, ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 15.0f * s);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 66.0f * s);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 28.0f * s);

        row("Open edges (gaps)", fmtCount(r.boundaryEdges), r.boundaryEdges ? Sev::Bad : Sev::Good, &view_.showOpenEdges, kOpenEdgeColor,
            "Edges that belong to only one triangle. A mesh with open edges has holes and is not watertight.");
        if (r.boundaryEdges) {
            subRow(fmtCount(r.holeLoops) + (r.holeLoops == 1 ? " hole" : " holes"));
        }
        row("Non-manifold edges", fmtCount(r.nonManifoldEdges), r.nonManifoldEdges ? Sev::Bad : Sev::Good, &view_.showNonManifold, kNonManifoldColor,
            "Edges shared by three or more triangles. Slicers cannot tell what is inside.");
        row("Inconsistent winding", fmtCount(r.inconsistentEdges), r.inconsistentEdges ? Sev::Bad : Sev::Good, &view_.showInconsistent,
            kInconsistentColor, "Neighbouring triangles that disagree about which side is outside (flipped triangles).");
        row("Degenerate triangles", fmtCount(r.degenerateTriangles), r.degenerateTriangles ? Sev::Bad : Sev::Good, &view_.showDegenerate,
            kDegenerateColor, "Zero-area triangles: collapsed corners, collinear corners or NaN coordinates.");
        if (r.degenerateTriangles) {
            std::string detail = fmtCount(r.collapsedTriangles) + " collapsed, " + fmtCount(r.zeroAreaTriangles) + " zero-area";
            if (r.invalidTriangles) detail += ", " + fmtCount(r.invalidTriangles) + " NaN";
            subRow(detail);
        }
        row("Duplicate triangles", fmtCount(r.duplicateTriangles), r.duplicateTriangles ? Sev::Bad : Sev::Good, nullptr, kInfo,
            "Triangles that appear more than once.");
        row("Sliver triangles", fmtCount(r.sliverTriangles), r.sliverTriangles ? Sev::Warn : Sev::Good, &view_.showSlivers, kSliverColor,
            "Very thin triangles (smallest angle below the sliver angle in Options > Mesh analysis). Informational.");

        const char* orient = !r.closedManifold ? "n/a" : (r.normalsOutward ? "outward" : "inverted");
        row("Normal orientation", orient, !r.closedManifold ? Sev::Info : (r.normalsOutward ? Sev::Good : Sev::Bad), nullptr, kInfo,
            "Needs a closed mesh to be judged. Negative volume means the triangles are wound inside-out.");
        row("Loose vertices", fmtCount(r.looseVertices), r.looseVertices ? Sev::Warn : Sev::Good, nullptr, kInfo,
            "Points that no triangle uses.");
        row("Separate shells", fmtCount(r.shells), r.shells > 1 ? Sev::Info : Sev::Good, nullptr, kInfo,
            "Connected pieces. Several shells are fine if you expect several parts.");
        ImGui::EndTable();
    }
    ImGui::Spacing();

    // ---- geometry
    ImGui::TextDisabled("GEOMETRY");
    auto kv = [&](const char* k, const std::string& v) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", k);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth() - ImGui::CalcTextSize(v.c_str()).x - ImGui::GetStyle().CellPadding.x);
        ImGui::TextUnformatted(v.c_str());
    };
    if (ImGui::BeginTable("geometry", 2, ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 112.0f * s);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        kv("Triangles", fmtCount(r.triangles));
        kv("Vertices", fmtCount(r.weldedVertices) + (r.fileVertices != r.weldedVertices ? "  (file: " + fmtCount(r.fileVertices) + ")" : std::string()));
        kv("Edges", fmtCount(r.edges));
        std::string euler = std::to_string(r.eulerCharacteristic);
        if (r.closedManifold) euler += "  (genus " + std::to_string(r.genus) + ")";
        kv("Euler char.", euler);
        const Vec3 sz = r.bounds.size();
        kv("Size (XYZ)", fmtNum(sz.x) + " x " + fmtNum(sz.y) + " x " + fmtNum(sz.z));
        kv("Surface area", fmtNum(r.surfaceArea));
        kv("Volume", r.closedManifold ? fmtNum(std::fabs(r.volume)) : std::string("n/a (not closed)"));
        kv("Weld distance", fmtNum(r.weldDistance));
        ImGui::EndTable();
    }
    ImGui::Spacing();

    // ---- loader warnings
    if (model_.raw && !model_.raw->warnings.empty()) {
        ImGui::TextDisabled("FILE WARNINGS");
        for (const std::string& w : model_.raw->warnings) ImGui::TextColored(kWarn, "%s", w.c_str());
        ImGui::Spacing();
    }

    // ---- holes
    if (!r.holes.empty()) {
        char head[96];
        std::snprintf(head, sizeof head, "Holes (%s)###holes", fmtCount(r.holeLoops).c_str());
        if (ImGui::CollapsingHeader(head, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("Click a hole to fly to it. Largest first.");
            ImGui::PopStyleColor();
            const int shown = static_cast<int>(std::min<size_t>(r.holes.size(), 300));
            ImGuiListClipper clipper;
            clipper.Begin(shown);
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const HoleLoop& h = r.holes[static_cast<size_t>(i)];
                    char label[160];
                    std::snprintf(label, sizeof label, "#%d   %u edge%s   %s%s", i + 1, h.edgeCount, h.edgeCount == 1 ? "" : "s",
                                  fmtNum(h.perimeter).c_str(), h.closed ? "" : "   open");
                    if (ImGui::Selectable(label)) focusOn(h.center + objectOffset(h.object), h.radius);
                }
            }
            if (r.holeLoops > static_cast<size_t>(shown)) ImGui::TextDisabled("... and %s more", fmtCount(r.holeLoops - static_cast<size_t>(shown)).c_str());
        }
    }

    // ---- objects
    drawObjectsSection();

    // ---- display
    if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Wireframe", &view_.wireframe);
        ImGui::Checkbox("Wireframe overlay", &view_.wireOverlay);
        ImGui::Checkbox("Flat shading", &view_.flatShading);
        ImGui::Checkbox("Highlight back faces", &view_.highlightBackFaces);
        ImGui::Checkbox("Grid", &view_.showGrid);
        ImGui::Checkbox("Problems visible through surface", &view_.problemsXray);
        ImGui::Checkbox("Z is up", &view_.zUp);
        if (ImGui::IsItemDeactivatedAfterEdit()) updateModelTransform(true);
        if (model_.analyzed && !model_.analyzed->render.colors.empty()) ImGui::Checkbox("Use colors from file", &view_.useVertexColors);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ============================================================================ dialogs

void App::drawModals() {
    const float s = uiScale_;
    if (errorPopupPending_) {
        ImGui::OpenPopup("Error##modal");
        errorPopupPending_ = false;
    }
    if (showControls_) {
        ImGui::OpenPopup("Controls##modal");
        showControls_ = false;
    }
    if (showAbout_) {
        ImGui::OpenPopup("About##modal");
        showAbout_ = false;
    }
    const ImVec2 center((viewport_.x0 + viewport_.x1) * 0.5f, (viewport_.y0 + viewport_.y1) * 0.5f);

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Error##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 420.0f * s);
        ImGui::TextColored(kBad, "Something went wrong");
        ImGui::Spacing();
        ImGui::TextUnformatted(errorText_.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(100.0f * s, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Controls##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (ImGui::BeginTable("controls", 2, ImGuiTableFlags_SizingFixedFit)) {
            const char* rows[][2] = {
                {"Left mouse drag", "Orbit"},
                {"Right / middle drag", "Pan"},
                {"Shift + left drag", "Pan"},
                {"Mouse wheel", "Zoom towards the cursor"},
                {"Ctrl + left drag", "Zoom"},
                {"Double-click", "Frame the model"},
                {"Click an object", "Select it (shows the move gizmo)"},
                {"Drag a gizmo handle", "Move the selected object (arrows: one axis, squares: a plane)"},
                {"Esc", "Deselect"},
                {"W", "Toggle wireframe"},
                {"G", "Toggle grid"},
                {"F  /  Home", "Frame model  /  reset view"},
                {"1 2 3 4 5 6", "Front, back, left, right, top, bottom"},
                {"Tab", "Show / hide the report panel"},
                {"H  /  Esc", "Hide the whole interface (viewer only) / bring it back"},
                {"Ctrl + O", "Open a file"},
                {"F5", "Reload the current file"},
                {"Drag & drop", "Drop a model file onto the window"},
            };
            for (const auto& rw : rows) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(kInfo, "%s", rw[0]);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(rw[1]);
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(100.0f * s, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("About##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("%s  %s", kAppName, MESHINSPECTOR_VERSION);
        ImGui::TextDisabled("A small 3D mesh viewer and quality checker.");
        ImGui::Spacing();
        ImGui::TextUnformatted("Copyright 2026 Print Studios");
        ImGui::TextUnformatted("Licensed under the Apache License, Version 2.0.");
        ImGui::Spacing();
        ImGui::TextDisabled("Built with Dear ImGui (MIT), GLFW (zlib) and cgltf (MIT).");
        ImGui::TextDisabled("%s", glInfo_.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(100.0f * s, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

}  // namespace mi
