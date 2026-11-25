// SPDX-License-Identifier: Apache-2.0
// Saving and loading the user's settings (colours, display options, window size ...).
#pragma once

#include <filesystem>
#include <string>

#include "view_settings.h"

namespace mi {

struct WindowState {
    int width = 0;  // 0 = not saved yet
    int height = 0;
    bool maximized = false;
};

struct Settings {
    ViewSettings view;
    WindowState window;
};

// Plain "key = value" text. Transient state (such as the hidden-interface mode) is not included.
std::string serializeSettings(const Settings& settings);

// Lenient: unknown keys, malformed lines and out-of-range values are ignored, so a hand-edited or
// older / newer settings file can never stop the program from starting. Values that are not
// mentioned keep what `settings` already holds.
void parseSettings(const std::string& text, Settings& settings);

bool loadSettingsFile(const std::filesystem::path& file, Settings& settings);
// Writes via a temporary file and a rename, so a crash never leaves a half-written file.
bool saveSettingsFile(const std::filesystem::path& file, const Settings& settings);

}  // namespace mi
