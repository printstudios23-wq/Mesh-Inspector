// SPDX-License-Identifier: Apache-2.0
#include "app.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <new>
#include <system_error>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "mesh_io.h"

namespace fs = std::filesystem;

namespace mi {

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

fs::path pathFromUtf8(const std::string& s) { return fs::u8path(s); }

}  // namespace

App::App() = default;

App::~App() {
    // Make sure a running load job is finished before the window goes away.
    if (job_.valid()) job_.wait();
    if (imguiReady_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    renderer_.shutdown();
    if (window_) glfwDestroyWindow(window_);
    if (glfwReady_) glfwTerminate();
}

// ============================================================================ start-up

bool App::init(const std::vector<std::string>& args) {
    glfwSetErrorCallback([](int code, const char* msg) { std::fprintf(stderr, "GLFW error %d: %s\n", code, msg); });
    if (!glfwInit()) {
        platform::showErrorMessage(kAppName, "Could not initialise the windowing system (GLFW).");
        return false;
    }
    glfwReady_ = true;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);  // shown once everything is set up
    glfwWindowHint(GLFW_SAMPLES, 4);

    // Default 1360x860 "logical" pixels, scaled for the monitor's DPI but at most 90% of the work area.
    int winW = 1360, winH = 860;
    if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) {
        float sx = 1.0f, sy = 1.0f;
        glfwGetMonitorContentScale(mon, &sx, &sy);
        int wx, wy, ww, wh;
        glfwGetMonitorWorkarea(mon, &wx, &wy, &ww, &wh);
        winW = std::min(static_cast<int>(winW * sx), static_cast<int>(ww * 0.9f));
        winH = std::min(static_cast<int>(winH * sy), static_cast<int>(wh * 0.9f));
        winW = std::max(winW, 760);
        winH = std::max(winH, 480);
    }
    window_ = glfwCreateWindow(winW, winH, kAppName, nullptr, nullptr);
    if (!window_) {
        glfwWindowHint(GLFW_SAMPLES, 0);  // some drivers / remote sessions have no multisampling
        window_ = glfwCreateWindow(winW, winH, kAppName, nullptr, nullptr);
    }
    if (!window_) {
        platform::showErrorMessage(kAppName,
                                   "Could not create an OpenGL 3.3 window.\n\nPlease update your graphics driver. "
                                   "(Remote desktop sessions and some virtual machines do not offer OpenGL 3.3.)");
        return false;
    }
    glfwSetWindowUserPointer(window_, this);
    glfwSetWindowSizeLimits(window_, 760, 480, GLFW_DONT_CARE, GLFW_DONT_CARE);

    if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) {  // centre on the primary monitor
        int mx, my, mw, mh, ww, wh;
        glfwGetMonitorWorkarea(mon, &mx, &my, &mw, &mh);
        glfwGetWindowSize(window_, &ww, &wh);
        glfwSetWindowPos(window_, mx + std::max(0, (mw - ww) / 2), my + std::max(0, (mh - wh) / 2));
    }

    // --native-frame keeps the normal system title bar (fallback in case the custom one misbehaves).
    const bool nativeFrame = std::any_of(args.begin(), args.end(), [](const std::string& a) { return a == "--native-frame"; });
    customFrame_ = platform::supportsCustomFrame() && !nativeFrame;
    if (customFrame_) platform::installCustomFrame(window_, &frameRegions_);

    glfwMakeContextCurrent(window_);
    const char* missing = nullptr;
    const auto getProc = [](const char* name) -> void* { return reinterpret_cast<void*>(glfwGetProcAddress(name)); };
    if (!gl::load(getProc, &missing)) {
        platform::showErrorMessage(kAppName, std::string("Your graphics driver does not provide OpenGL 3.3 (missing ") +
                                                 (missing ? missing : "?") + ").");
        return false;
    }
    std::string glError;
    if (!renderer_.init(glError)) {
        platform::showErrorMessage(kAppName, glError);
        return false;
    }
    glInfo_ = renderer_.glDescription();

    // Callbacks that must be in place before ImGui chains its own.
    glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* w, int, int) {
        if (auto* app = static_cast<App*>(glfwGetWindowUserPointer(w))) app->frame();
    });
    glfwSetWindowRefreshCallback(window_, [](GLFWwindow* w) {
        if (auto* app = static_cast<App*>(glfwGetWindowUserPointer(w))) app->frame();
    });
    glfwSetWindowContentScaleCallback(window_, [](GLFWwindow* w, float sx, float) {
        if (auto* app = static_cast<App*>(glfwGetWindowUserPointer(w))) app->pendingScale_ = sx;
    });
    glfwSetDropCallback(window_, [](GLFWwindow* w, int count, const char** paths) {
        auto* app = static_cast<App*>(glfwGetWindowUserPointer(w));
        if (app && count > 0) app->openFile(pathFromUtf8(paths[0]));
    });

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;  // no imgui.ini next to the exe
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    imguiReady_ = true;

    float sx = 1.0f, sy = 1.0f;
    glfwGetWindowContentScale(window_, &sx, &sy);
    pendingScale_ = uiScale_ = std::max(sx, 0.5f);
    applyUiScale(uiScale_);

    camera_.setView(35.0f, 22.0f);
    updateModelTransform(true);

    initDone_ = true;
    glfwShowWindow(window_);

    // args[0] is the executable; the first real argument is a file to open.
    for (size_t i = 1; i < args.size(); ++i) {
        if (!args[i].empty() && args[i][0] != '-') {
            openFile(pathFromUtf8(args[i]));
            break;
        }
    }
    return true;
}

void App::setupStyle(float scale) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    ImGui::StyleColorsDark(&st);

    st.WindowRounding = 0.0f;
    st.PopupRounding = 6.0f;
    st.FrameRounding = 4.0f;
    st.GrabRounding = 4.0f;
    st.ChildRounding = 4.0f;
    st.WindowBorderSize = 0.0f;
    st.PopupBorderSize = 1.0f;
    st.FrameBorderSize = 0.0f;
    st.WindowPadding = ImVec2(10, 8);
    st.FramePadding = ImVec2(8, 4);
    st.ItemSpacing = ImVec2(8, 6);
    st.ItemInnerSpacing = ImVec2(6, 4);
    st.ScrollbarSize = 12.0f;
    st.ScrollbarRounding = 6.0f;

    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.125f, 0.133f, 0.153f, 1.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.140f, 0.149f, 0.169f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.26f, 0.28f, 0.32f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.21f, 0.24f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.27f, 0.31f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.29f, 0.31f, 0.36f, 1.0f);
    c[ImGuiCol_CheckMark] = ImVec4(0.38f, 0.68f, 1.00f, 1.0f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.38f, 0.60f, 0.92f, 1.0f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.45f, 0.68f, 1.00f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.25f, 0.28f, 0.34f, 1.0f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.35f, 0.44f, 1.0f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.33f, 0.39f, 0.50f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.24f, 0.28f, 0.35f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.36f, 0.46f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.34f, 0.42f, 0.55f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.26f, 0.28f, 0.32f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.58f, 0.64f, 1.0f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);

    st.ScaleAllSizes(scale);
}

void App::applyUiScale(float scale) {
    uiScale_ = scale;
    pendingScale_ = scale;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const float px = std::round(15.0f * scale);
    ImFont* font = nullptr;
    // Segoe UI is the native Windows UI font. Elsewhere try a few common fonts. If none exists
    // ImGui's built-in font is used.
    static const char* const kFontCandidates[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
    };
    for (const char* candidate : kFontCandidates) {
        std::error_code ec;
        if (!font && fs::exists(candidate, ec)) font = io.Fonts->AddFontFromFileTTF(candidate, px);
    }
    if (!font) {
        ImFontConfig cfg;
        cfg.SizePixels = px;
        io.Fonts->AddFontDefault(&cfg);
    }
    setupStyle(scale);

    titleBarH_ = std::round(34.0f * scale);
    statusBarH_ = std::round(26.0f * scale);
    panelW_ = std::round(340.0f * scale);

    if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplOpenGL3_DestroyDeviceObjects();  // rebuilds the font texture
}

void App::resetOptions() {
    view_ = ViewSettings{};
    appliedVsync_ = -1;
    updateModelTransform(true);
    if (model_.raw) reanalyze();
}

// ============================================================================ main loop

int App::run() {
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window_, GLFW_ICONIFIED)) {
            glfwWaitEventsTimeout(0.1);
            continue;
        }
        frame();
    }
    return 0;
}

void App::frame() {
    // The refresh / resize callbacks can fire while we are still starting up or already drawing.
    if (inFrame_ || !initDone_) return;
    inFrame_ = true;

    if (pendingScale_ != uiScale_) applyUiScale(pendingScale_);
    if (openDialogRequested_) {
        openDialogRequested_ = false;
        const std::string chosen = platform::openFileDialog(window_);
        if (!chosen.empty()) openFile(pathFromUtf8(chosen));
    }
    const int swap = view_.vsync ? 1 : 0;
    if (swap != appliedVsync_) {
        glfwSwapInterval(swap);
        appliedVsync_ = swap;
    }
    pollJob();
    pollAutoReload();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    fps_ = fps_ * 0.9f + ImGui::GetIO().Framerate * 0.1f;
    buildUi();
    handleShortcuts();
    handleViewportInput();
    renderScene();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window_);
    inFrame_ = false;
}

// ============================================================================ loading

void App::startJob(const fs::path& path, std::shared_ptr<const Mesh> reuseRaw, bool keepCamera) {
    if (jobRunning_) {  // one job at a time; the most recent request wins
        hasPending_ = true;
        pendingPath_ = path;
        pendingKeepCamera_ = keepCamera;
        return;
    }
    jobRunning_ = true;
    jobLabel_ = path.filename().u8string();
    const AnalysisSettings settings = view_.analysis;

    job_ = std::async(std::launch::async, [path, reuseRaw, keepCamera, settings]() {
        JobResult r;
        r.keepCamera = keepCamera;
        r.model.path = path;
        try {
            std::error_code ec;
            r.model.fileBytes = fs::file_size(path, ec);
            r.model.mtime = fs::last_write_time(path, ec);

            if (reuseRaw) {
                r.model.raw = reuseRaw;
            } else {
                const auto t0 = Clock::now();
                auto mesh = std::make_shared<Mesh>();
                if (!loadMeshFile(path, *mesh, r.error)) return r;
                r.model.loadSeconds = secondsSince(t0);
                r.model.raw = std::move(mesh);
            }
            const auto t1 = Clock::now();
            r.model.analyzed = std::make_shared<const AnalyzedMesh>(analyzeMesh(*r.model.raw, settings));
            r.model.analyzeSeconds = secondsSince(t1);
            r.ok = true;
        } catch (const std::bad_alloc&) {
            r.ok = false;
            r.error = "Out of memory while processing the file.";
        } catch (const std::exception& e) {
            r.ok = false;
            r.error = std::string("Unexpected error: ") + e.what();
        }
        return r;
    });
}

void App::pollJob() {
    if (jobRunning_ && job_.valid() && job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        JobResult result = job_.get();
        jobRunning_ = false;
        onJobFinished(std::move(result));
    }
    if (!jobRunning_ && hasPending_) {
        hasPending_ = false;
        startJob(pendingPath_, nullptr, pendingKeepCamera_);
    }
}

void App::onJobFinished(JobResult&& r) {
    if (!r.ok) {
        errorText_ = "Could not open \"" + r.model.path.filename().u8string() + "\"\n\n" + r.error;
        errorPopupPending_ = true;
        return;
    }
    // Re-loading or re-analysing the file that is already open keeps the camera where it is.
    const bool reframe = !(r.keepCamera && !model_.path.empty() && model_.path == r.model.path);
    model_ = std::move(r.model);
    seenMtime_ = model_.mtime;
    seenSize_ = model_.fileBytes;

    renderer_.setMesh(*model_.analyzed);
    updateModelTransform(reframe);
    setWindowTitleFor(model_.path.filename().u8string());
}

void App::reload() {
    if (!model_.path.empty()) startJob(model_.path, nullptr, true);
}

void App::reanalyze() {
    if (model_.raw) startJob(model_.path, model_.raw, true);
}

void App::pollAutoReload() {
    if (!view_.autoReload || model_.path.empty() || jobRunning_) return;
    const double now = glfwGetTime();
    if (now - lastPollTime_ < 0.5) return;
    lastPollTime_ = now;

    std::error_code ec;
    const auto mt = fs::last_write_time(model_.path, ec);
    if (ec) return;
    const auto sz = fs::file_size(model_.path, ec);
    if (ec) return;
    if (mt == model_.mtime && sz == model_.fileBytes) return;
    // Changed on disk. Wait until it stops changing so half-written files are not loaded.
    if (mt == seenMtime_ && sz == seenSize_) {
        seenMtime_ = model_.mtime;  // avoid retrigger loops if the load fails
        seenSize_ = model_.fileBytes;
        startJob(model_.path, nullptr, true);
    } else {
        seenMtime_ = mt;
        seenSize_ = sz;
    }
}

void App::setWindowTitleFor(const std::string& name) {
    const std::string title = name.empty() ? std::string(kAppName) : name + " - " + kAppName;
    glfwSetWindowTitle(window_, title.c_str());
}

// ============================================================================ camera & scene

void App::updateModelTransform(bool reframe) {
    modelMatrix_ = view_.zUp ? rotationX(radians(-90.0f)) : Mat4::identity();
    worldBounds_ = AABB{};
    if (!model_.analyzed || !model_.analyzed->report.bounds.valid()) {
        camera_.sceneCenter = {0, 0, 0};
        camera_.sceneRadius = 1.0f;
        if (reframe) camera_.frame({0, 0, 0}, 1.0f);
        renderer_.setGrid(0.0f, 0.25f, 0.0f, 0.0f, 20);
        return;
    }
    const AABB& b = model_.analyzed->report.bounds;
    for (int i = 0; i < 8; ++i) {
        const Vec3 corner{(i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y, (i & 4) ? b.max.z : b.min.z};
        worldBounds_.expand(transformPoint(modelMatrix_, corner));
    }
    const Vec3 center = worldBounds_.center();
    const float radius = std::max(worldBounds_.diagonal() * 0.5f, 1e-6f);
    if (reframe) {
        camera_.frame(center, radius);
    } else {
        camera_.sceneCenter = center;
        camera_.sceneRadius = radius;
    }

    // grid: a "nice" cell size (1, 2 or 5 x 10^n) giving roughly a dozen cells per model radius
    const float raw = radius / 4.0f;
    const float mag = std::pow(10.0f, std::floor(std::log10(raw)));
    const float frac = raw / mag;
    const float cell = mag * (frac < 1.5f ? 1.0f : frac < 3.5f ? 2.0f : frac < 7.5f ? 5.0f : 10.0f);
    const int half = std::clamp(static_cast<int>(std::ceil(radius * 3.0f / cell)), 8, 60);
    renderer_.setGrid(worldBounds_.min.y, cell, center.x, center.z, half);
}

void App::frameModel() {
    if (model_.analyzed && model_.analyzed->report.bounds.valid()) camera_.frame(worldBounds_.center(), std::max(worldBounds_.diagonal() * 0.5f, 1e-6f));
}

void App::resetView() {
    camera_.setView(35.0f, 22.0f);
    frameModel();
}

void App::focusOn(const Vec3& p, float radius) {
    camera_.target = transformPoint(modelMatrix_, p);
    camera_.distance = std::max(radius * 3.0f, camera_.sceneRadius * 0.02f);
}

void App::handleViewportInput() {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mp = io.MousePos;
    const bool inside = mp.x >= viewport_.x0 && mp.x < viewport_.x1 && mp.y >= viewport_.y0 && mp.y < viewport_.y1;
    const float vpW = viewport_.x1 - viewport_.x0, vpH = viewport_.y1 - viewport_.y0;
    if (vpW < 1 || vpH < 1) return;

    if (dragButton_ < 0 && inside && !io.WantCaptureMouse) {
        for (int b = 0; b < 3; ++b) {
            if (ImGui::IsMouseClicked(b)) {
                dragButton_ = b;
                break;
            }
        }
    }
    if (dragButton_ >= 0) {
        if (!ImGui::IsMouseDown(dragButton_)) {
            dragButton_ = -1;
        } else {
            const float dx = io.MouseDelta.x, dy = io.MouseDelta.y;
            if (dragButton_ == 0 && io.KeyCtrl) {
                camera_.zoomCentered(std::pow(1.01f, dy));  // Ctrl + left drag: zoom
            } else if (dragButton_ == 0 && !io.KeyShift) {
                camera_.orbit(dx, dy);                      // left drag: orbit
            } else {
                camera_.pan(dx, dy, vpH);                   // right / middle / Shift + left drag: pan
            }
        }
    }

    if (inside && !io.WantCaptureMouse && io.MouseWheel != 0.0f) {  // wheel: zoom towards the cursor
        const float ndcX = ((mp.x - viewport_.x0) / vpW) * 2.0f - 1.0f;
        const float ndcY = 1.0f - ((mp.y - viewport_.y0) / vpH) * 2.0f;
        camera_.zoomAt(std::pow(0.88f, io.MouseWheel), ndcX, ndcY, vpW / vpH);
    }
    if (inside && !io.WantCaptureMouse && ImGui::IsMouseDoubleClicked(0)) frameModel();
}

void App::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };

    if (io.KeyCtrl && pressed(ImGuiKey_O)) openDialogRequested_ = true;
    if (pressed(ImGuiKey_F5)) reload();
    if (io.KeyCtrl || io.KeyAlt) return;

    if (pressed(ImGuiKey_W)) view_.wireframe = !view_.wireframe;
    if (pressed(ImGuiKey_G)) view_.showGrid = !view_.showGrid;
    if (pressed(ImGuiKey_F)) frameModel();
    if (pressed(ImGuiKey_Home)) resetView();
    if (pressed(ImGuiKey_Tab)) view_.showReport = !view_.showReport;
    if (pressed(ImGuiKey_1)) camera_.setView(0, 0);       // front
    if (pressed(ImGuiKey_2)) camera_.setView(180, 0);     // back
    if (pressed(ImGuiKey_3)) camera_.setView(-90, 0);     // left
    if (pressed(ImGuiKey_4)) camera_.setView(90, 0);      // right
    if (pressed(ImGuiKey_5)) camera_.setView(0, 90);      // top
    if (pressed(ImGuiKey_6)) camera_.setView(0, -90);     // bottom
}

void App::renderScene() {
    camera_.orthographic = view_.orthographic;

    int fbW = 0, fbH = 0, winW = 0, winH = 0;
    glfwGetFramebufferSize(window_, &fbW, &fbH);
    glfwGetWindowSize(window_, &winW, &winH);
    if (fbW <= 0 || fbH <= 0 || winW <= 0 || winH <= 0) return;
    const float sx = static_cast<float>(fbW) / static_cast<float>(winW);
    const float sy = static_cast<float>(fbH) / static_cast<float>(winH);

    FrameParams p;
    p.fbWidth = fbW;
    p.fbHeight = fbH;
    p.vpX = static_cast<int>(std::lround(viewport_.x0 * sx));
    p.vpW = static_cast<int>(std::lround((viewport_.x1 - viewport_.x0) * sx));
    p.vpH = static_cast<int>(std::lround((viewport_.y1 - viewport_.y0) * sy));
    p.vpY = fbH - static_cast<int>(std::lround(viewport_.y1 * sy));
    const float aspect = p.vpH > 0 ? static_cast<float>(p.vpW) / static_cast<float>(p.vpH) : 1.0f;

    p.model = modelMatrix_;
    p.view = camera_.viewMatrix();
    p.proj = camera_.projectionMatrix(aspect);
    p.ortho = view_.orthographic;
    p.background = {view_.background[0], view_.background[1], view_.background[2]};
    p.meshColor = {view_.meshColor[0], view_.meshColor[1], view_.meshColor[2]};
    p.backColor = {view_.backColor[0], view_.backColor[1], view_.backColor[2]};
    p.wireframe = view_.wireframe;
    p.wireOverlay = view_.wireOverlay;
    p.xrayWire = view_.xrayWire;
    p.flatShading = view_.flatShading;
    p.highlightBackFaces = view_.highlightBackFaces;
    p.showGrid = view_.showGrid;
    p.useVertexColors = view_.useVertexColors;
    p.showProblems = view_.showProblems;
    p.showOpenEdges = view_.showOpenEdges;
    p.showNonManifold = view_.showNonManifold;
    p.showInconsistent = view_.showInconsistent;
    p.showDegenerate = view_.showDegenerate;
    p.showSlivers = view_.showSlivers;
    p.problemsXray = view_.problemsXray;
    p.uiScale = uiScale_;
    renderer_.draw(p);
}

}  // namespace mi
