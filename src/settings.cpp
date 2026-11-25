// SPDX-License-Identifier: Apache-2.0
#include "settings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

namespace mi {

namespace {

// One description of every saved setting, shared by the writer and the reader.
template <class V>
void visitSettings(Settings& s, V& v) {
    ViewSettings& x = s.view;
    v.flag("wireframe", x.wireframe);
    v.flag("wire_overlay", x.wireOverlay);
    v.flag("wire_hidden_edges", x.xrayWire);
    v.flag("flat_shading", x.flatShading);
    v.flag("highlight_back_faces", x.highlightBackFaces);
    v.flag("use_vertex_colors", x.useVertexColors);
    v.flag("show_grid", x.showGrid);
    v.flag("show_gizmo", x.showGizmo);
    v.flag("show_report", x.showReport);
    v.flag("orthographic", x.orthographic);
    v.flag("z_up", x.zUp);
    v.flag("show_problems", x.showProblems);
    v.flag("show_open_edges", x.showOpenEdges);
    v.flag("show_non_manifold", x.showNonManifold);
    v.flag("show_inconsistent", x.showInconsistent);
    v.flag("show_degenerate", x.showDegenerate);
    v.flag("show_slivers", x.showSlivers);
    v.flag("problems_visible_through_surface", x.problemsXray);
    v.flag("vsync", x.vsync);
    v.flag("auto_reload", x.autoReload);
    v.color("background_color", x.background);
    v.color("surface_color", x.meshColor);
    v.color("back_face_color", x.backColor);
    v.number("weld_tolerance_exponent", x.weldExponent, 3.0, 9.0);
    v.number("sliver_angle_degrees", x.analysis.sliverAngleDeg, 0.1, 10.0);
    v.number("window_width", s.window.width, 0.0, 20000.0);
    v.number("window_height", s.window.height, 0.0, 20000.0);
    v.flag("window_maximized", s.window.maximized);
}

struct Writer {
    std::ostringstream out;
    void flag(const char* k, bool& v) { out << k << " = " << (v ? "true" : "false") << "\n"; }
    template <class T>
    void number(const char* k, T& v, double, double) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.6g", static_cast<double>(v));
        out << k << " = " << buf << "\n";
    }
    void color(const char* k, float (&c)[3]) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "%.4f %.4f %.4f", static_cast<double>(c[0]), static_cast<double>(c[1]), static_cast<double>(c[2]));
        out << k << " = " << buf << "\n";
    }
};

struct Reader {
    std::map<std::string, std::string> values;

    const std::string* find(const char* k) const {
        const auto it = values.find(k);
        return it == values.end() ? nullptr : &it->second;
    }
    void flag(const char* k, bool& v) {
        const std::string* s = find(k);
        if (!s) return;
        if (*s == "true" || *s == "1") v = true;
        else if (*s == "false" || *s == "0") v = false;
    }
    template <class T>
    void number(const char* k, T& v, double lo, double hi) {
        const std::string* s = find(k);
        if (!s) return;
        char* end = nullptr;
        const double d = std::strtod(s->c_str(), &end);
        if (end == s->c_str() || !std::isfinite(d)) return;
        v = static_cast<T>(std::clamp(d, lo, hi));
    }
    void color(const char* k, float (&c)[3]) {
        const std::string* s = find(k);
        if (!s) return;
        const char* p = s->c_str();
        float tmp[3];
        for (float& f : tmp) {
            char* end = nullptr;
            const double d = std::strtod(p, &end);
            if (end == p || !std::isfinite(d)) return;
            f = static_cast<float>(std::clamp(d, 0.0, 1.0));
            p = end;
        }
        std::copy(tmp, tmp + 3, c);
    }
};

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

}  // namespace

std::string serializeSettings(const Settings& settings) {
    Settings copy = settings;
    Writer w;
    w.out << "# Mesh Inspector settings. Delete this file to restore the defaults.\n";
    visitSettings(copy, w);
    return w.out.str();
}

void parseSettings(const std::string& text, Settings& settings) {
    Reader r;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        r.values[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    visitSettings(settings, r);
    // keep the derived value consistent with the exponent that was read
    settings.view.analysis.weldTolerance = std::pow(10.0, -static_cast<double>(settings.view.weldExponent));
}

bool loadSettingsFile(const std::filesystem::path& file, Settings& settings) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    parseSettings(ss.str(), settings);
    return true;
}

bool saveSettingsFile(const std::filesystem::path& file, const Settings& settings) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const std::filesystem::path tmp = file.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << serializeSettings(settings);
        if (!f) return false;
    }
    std::filesystem::rename(tmp, file, ec);  // replaces an existing file on both Windows and POSIX
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

}  // namespace mi
