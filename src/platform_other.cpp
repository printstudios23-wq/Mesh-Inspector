// SPDX-License-Identifier: Apache-2.0
// Non-Windows fallback: keeps the normal system window frame.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "platform.h"

namespace mi::platform {

bool supportsCustomFrame() { return false; }

void installCustomFrame(GLFWwindow*, FrameRegions*) {}

std::string openFileDialog(GLFWwindow*) {
    // Best effort: use zenity if it is installed. Otherwise drag & drop or a command line argument work.
    std::string result;
    if (FILE* p = popen("zenity --file-selection --title='Open 3D model' 2>/dev/null", "r")) {
        char buf[4096];
        while (fgets(buf, sizeof buf, p)) result += buf;
        pclose(p);
    }
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();
    return result;
}

std::string configDirectory() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) return std::string(xdg) + "/meshinspector";
    if (const char* home = std::getenv("HOME"); home && *home) return std::string(home) + "/.config/meshinspector";
    return {};
}

void showErrorMessage(const std::string& title, const std::string& message) {
    std::fprintf(stderr, "%s: %s\n", title.c_str(), message.c_str());
}

std::vector<std::string> commandLineArguments(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
    return args;
}

}  // namespace mi::platform
