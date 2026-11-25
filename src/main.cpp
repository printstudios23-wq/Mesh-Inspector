// SPDX-License-Identifier: Apache-2.0
#include <exception>
#include <string>

#include "app.h"
#include "platform.h"

int main(int argc, char** argv) {
    try {
        mi::App app;
        if (!app.init(mi::platform::commandLineArguments(argc, argv))) return 1;
        return app.run();
    } catch (const std::exception& e) {
        mi::platform::showErrorMessage(mi::kAppName, std::string("Unexpected error: ") + e.what());
        return 2;
    }
}
