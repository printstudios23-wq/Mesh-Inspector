// SPDX-License-Identifier: Apache-2.0
// Operating-system specific bits: native file dialog and the custom window frame.
#pragma once

#include <string>
#include <vector>

struct GLFWwindow;

namespace mi::platform {

// Regions of the in-app title bar, in client-area pixels. The UI refreshes this every frame;
// the window procedure uses it to decide which parts of the bar drag the window.
struct FrameRegions {
    int titleBarHeight = 0;
    int menuLeft = 0;      // [menuLeft, menuRight) holds clickable menu headers
    int menuRight = 0;
    int buttonsLeft = 0;   // [buttonsLeft, window width) holds minimise / maximise / close
};

// True if this platform draws its own title bar (the menus then live in the title bar and the
// native caption is removed). Elsewhere the normal system frame is kept.
bool supportsCustomFrame();

// Removes the native caption but keeps resizing, snapping, shadow and the system menu.
// Call right after creating the (hidden) window. `regions` must outlive the window.
void installCustomFrame(GLFWwindow* window, FrameRegions* regions);

// Shows the native "Open" dialog. Returns the chosen path as UTF-8, or "" if cancelled.
std::string openFileDialog(GLFWwindow* window);

// Shows a blocking error message box (stderr elsewhere). Used for start-up failures, because a
// Windows GUI program has no console to print to.
void showErrorMessage(const std::string& title, const std::string& message);

// Command line arguments as UTF-8 (the plain argv is not Unicode-safe on Windows).
std::vector<std::string> commandLineArguments(int argc, char** argv);

}  // namespace mi::platform
