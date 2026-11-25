// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "analysis.h"

namespace mi {

// Everything the user can change in the interface. The values here are the defaults; the
// non-transient ones are saved between sessions (see settings.h).
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

}  // namespace mi
