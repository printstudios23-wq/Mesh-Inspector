// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "analysis.h"
#include "camera.h"
#include "mesh.h"
#include "platform.h"
#include "renderer.h"
#include "settings.h"

struct GLFWwindow;

namespace mi {

inline constexpr const char* kAppName = "Mesh Inspector";

struct LoadedModel {
    std::filesystem::path path;
    std::shared_ptr<const Mesh> raw;
    std::shared_ptr<const AnalyzedMesh> analyzed;
    double loadSeconds = 0;
    double analyzeSeconds = 0;
    uintmax_t fileBytes = 0;
    std::filesystem::file_time_type mtime{};
};

struct JobResult {
    bool ok = false;
    std::string error;
    LoadedModel model;
    bool keepCamera = false;
};

class App {
public:
    App();
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool init(const std::vector<std::string>& args);
    int run();

private:
    // ---- app.cpp
    void frame();
    void setupStyle(float scale);
    void applyUiScale(float scale);
    void startJob(const std::filesystem::path& path, std::shared_ptr<const Mesh> reuseRaw, bool keepCamera);
    void pollJob();
    void pollAutoReload();
    void onJobFinished(JobResult&& result);
    void openFile(const std::filesystem::path& path) { startJob(path, nullptr, false); }
    void reload();
    void reanalyze();
    void updateModelTransform(bool reframe);
    void frameModel();
    void resetView();
    void focusOn(const Vec3& modelSpacePoint, float radius);
    void handleViewportInput();
    void handleShortcuts();
    void renderScene();
    void setWindowTitleFor(const std::string& name);
    void resetOptions();
    void saveSettings();
    void setHideUi(bool hide);

    // ---- objects_ui.cpp: separate objects, selection and the move gizmo
    struct GizmoGeometry;
    size_t objectCount() const { return model_.analyzed ? model_.analyzed->objects.size() : 0; }
    Vec3 objectOffset(size_t id) const { return id < objectOffsets_.size() ? objectOffsets_[id] : Vec3{}; }
    void resetObjectState(bool keepOffsets);
    void selectObject(int id);
    void separateObjects();
    void resetObjectPositions();
    bool viewportPoint(float& ndcX, float& ndcY, float& aspect) const;
    bool computeGizmo(GizmoGeometry& g) const;
    int hitTestGizmo(const GizmoGeometry& g, float mx, float my) const;
    bool beginGizmoDrag(int handle, float mx, float my);
    void updateGizmoDrag(float mx, float my);
    void pickAt(float mx, float my);
    void drawObjectGizmo();

    // ---- ui.cpp
    void buildUi();
    void drawTitleBar();
    void drawMenu(int index);
    void drawToolbar();
    void drawReportPanel();
    void drawStatusBar();
    void drawGizmo();
    void drawEmptyState();
    void drawModals();
    void drawObjectsSection();
    void drawHideHint();

    GLFWwindow* window_ = nullptr;
    Renderer renderer_;
    Camera camera_;
    ViewSettings view_;
    bool glfwReady_ = false;
    bool imguiReady_ = false;

    // platform / DPI
    platform::FrameRegions frameRegions_;
    bool customFrame_ = false;
    float uiScale_ = 1.0f;
    float pendingScale_ = 1.0f;
    bool inFrame_ = false;
    bool initDone_ = false;
    int appliedVsync_ = -1;

    // model + background job
    LoadedModel model_;
    std::future<JobResult> job_;
    bool jobRunning_ = false;
    std::string jobLabel_;
    bool hasPending_ = false;
    std::filesystem::path pendingPath_;
    bool pendingKeepCamera_ = false;
    double lastPollTime_ = 0;
    std::filesystem::file_time_type seenMtime_{};
    uintmax_t seenSize_ = 0;
    Mat4 modelMatrix_ = Mat4::identity();
    AABB worldBounds_;

    // layout (ImGui / window coordinates)
    float titleBarH_ = 34, statusBarH_ = 24, panelW_ = 330;
    struct Rect {
        float x0 = 0, y0 = 0, x1 = 1, y1 = 1;
    } viewport_;

    // ui state
    int openMenu_ = -1;
    float menuX_[4] = {0, 0, 0, 0};
    bool openDialogRequested_ = false;
    bool showControls_ = false;
    bool showAbout_ = false;
    std::string errorText_;
    bool errorPopupPending_ = false;
    int dragButton_ = -1;
    float dragDistance_ = 0;  // how far the mouse moved during the current left-button drag

    // separate objects + move gizmo
    std::vector<Vec3> objectOffsets_;  // per object translation, in the mesh's own coordinates
    int selectedObject_ = -1;
    Mat4 invModelMatrix_ = Mat4::identity();
    int gizmoHover_ = -1;     // 0..2: axis arrows (X, Y, Z of the file); 3..5: plane handles
    int gizmoDrag_ = -1;      // handle being dragged, -1 if none
    Vec3 gizmoStartCenter_;   // world-space gizmo centre when the drag began
    Vec3 gizmoStartOffset_;   // object offset when the drag began
    Vec3 gizmoStartHit_;      // plane drag: where the mouse ray first hit the plane
    float gizmoStartT_ = 0;   // axis drag: starting parameter along the axis
    float fps_ = 0;

    // persisted settings
    std::filesystem::path settingsFile_;
    std::string lastSavedSettings_;
    double lastSettingsCheck_ = 0;
    int normalWinW_ = 0, normalWinH_ = 0;  // last window size while not maximized
    double hideHintUntil_ = 0;  // time until which the "press H" hint is shown
    std::string glInfo_;
};

}  // namespace mi
