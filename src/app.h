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

struct GLFWwindow;

namespace mi {

inline constexpr const char* kAppName = "Mesh Inspector";

struct ViewSettings {
    // surface
    bool wireframe = false;
    bool wireOverlay = false;
    bool xrayWire = false;
    bool flatShading = true;
    bool highlightBackFaces = true;
    bool useVertexColors = true;
    // scene
    bool showGrid = true;
    bool showGizmo = true;
    bool showReport = true;
    bool hideUi = false;  // "viewer only" mode: nothing but the model
    bool orthographic = false;
    bool zUp = true;  // treat the file's +Z as "up" (STL / CAD / 3D printing convention)
    // problem overlays
    bool showProblems = true;
    bool showOpenEdges = true;
    bool showNonManifold = true;
    bool showInconsistent = true;
    bool showDegenerate = true;
    bool showSlivers = true;
    bool problemsXray = false;
    // app
    bool vsync = true;
    bool autoReload = false;
    // colours
    float background[3] = {0.105f, 0.115f, 0.135f};
    float meshColor[3] = {0.72f, 0.74f, 0.78f};
    float backColor[3] = {0.62f, 0.38f, 0.85f};
    // analysis
    float weldExponent = 6.0f;  // weld tolerance = 10^-weldExponent of the model size
    AnalysisSettings analysis;
};

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
    void setHideUi(bool hide);

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
    float fps_ = 0;
    double hideHintUntil_ = 0;  // time until which the "press H" hint is shown
    std::string glInfo_;
};

}  // namespace mi
