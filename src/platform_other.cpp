// SPDX-License-Identifier: Apache-2.0
// Non-Windows fallback: keeps the normal system window frame.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "platform.h"

namespace mi::platform {

bool supportsCustomFrame() { return false; }

void installCustomFrame(GLFWwindow*, FrameRegions*) {}

bool hasNativeFileDialog() { return false; }

std::string openFileDialog(GLFWwindow*) { return {}; }  // not used: the program shows its own file browser

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
