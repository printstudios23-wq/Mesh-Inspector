// SPDX-License-Identifier: Apache-2.0
#include "platform.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <algorithm>

namespace mi::platform {

namespace {

WNDPROC g_originalProc = nullptr;
FrameRegions* g_regions = nullptr;

std::string toUtf8(const wchar_t* w) {
    if (!w || !*w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// GetDpiForWindow / GetSystemMetricsForDpi need Windows 10 1607. Resolve them at run time so the
// program still starts (with 96 dpi metrics) on anything older.
UINT dpiForWindow(HWND hwnd) {
    using Fn = UINT(WINAPI*)(HWND);
    static const Fn fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    return fn ? fn(hwnd) : 96;
}

int systemMetricForDpi(int index, UINT dpi) {
    using Fn = int(WINAPI*)(int, UINT);
    static const Fn fn =
        reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi")));
    return fn ? fn(index, dpi) : GetSystemMetrics(index);
}

// Thickness of the (invisible) sizing border of a normal top-level window, in pixels.
int sizingBorder(HWND hwnd) {
    const UINT dpi = dpiForWindow(hwnd);
    return systemMetricForDpi(SM_CYSIZEFRAME, dpi) + systemMetricForDpi(SM_CXPADDEDBORDER, dpi);
}

LRESULT CALLBACK frameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCCALCSIZE: {
            if (!wp) break;
            // Let Windows compute the normal frame (left / right / bottom keep their invisible
            // sizing borders), then take the title bar area back for ourselves.
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
            const LONG originalTop = params->rgrc[0].top;
            const LRESULT r = DefWindowProcW(hwnd, msg, wp, lp);
            if (r != 0) return r;
            params->rgrc[0].top = originalTop;
            if (IsZoomed(hwnd)) {
                // A maximised window overhangs the monitor by its sizing border; pull the
                // content back inside so the menus are not cut off.
                params->rgrc[0].top += sizingBorder(hwnd);
            }
            return 0;
        }

        case WM_NCACTIVATE:
            // Stops Windows from repainting a caption bar we do not have when focus changes.
            return DefWindowProcW(hwnd, msg, wp, -1);

        case WM_NCHITTEST: {
            // Borders on the left / right / bottom come from the default handling.
            const LRESULT hit = CallWindowProcW(g_originalProc, hwnd, msg, wp, lp);
            if (hit != HTCLIENT || !g_regions) return hit;

            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            RECT rc;
            GetClientRect(hwnd, &rc);
            const UINT dpi = dpiForWindow(hwnd);
            const bool maximized = IsZoomed(hwnd) != 0;

            if (!maximized && pt.y >= 0) {
                const int edge = MulDiv(5, static_cast<int>(dpi), 96);
                const int corner = MulDiv(16, static_cast<int>(dpi), 96);
                if (pt.y < edge) {
                    if (pt.x < corner) return HTTOPLEFT;
                    if (pt.x >= rc.right - corner) return HTTOPRIGHT;
                    return HTTOP;
                }
            }
            if (pt.y >= 0 && pt.y < g_regions->titleBarHeight) {
                const bool onMenus = pt.x >= g_regions->menuLeft && pt.x < g_regions->menuRight;
                const bool onButtons = pt.x >= g_regions->buttonsLeft;
                if (!onMenus && !onButtons) return HTCAPTION;  // drag, double-click to maximise, right-click system menu
            }
            return HTCLIENT;
        }
        default:
            break;
    }
    return CallWindowProcW(g_originalProc, hwnd, msg, wp, lp);
}

}  // namespace

bool supportsCustomFrame() { return true; }

void installCustomFrame(GLFWwindow* window, FrameRegions* regions) {
    HWND hwnd = glfwGetWin32Window(window);
    g_regions = regions;
    g_originalProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&frameProc)));
    // Make Windows re-run WM_NCCALCSIZE with the new handler.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

std::string openFileDialog(GLFWwindow* window) {
    wchar_t file[32768] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = glfwGetWin32Window(window);
    ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(std::size(file));
    ofn.lpstrTitle = L"Open 3D model";
    ofn.lpstrFilter =
        L"All supported 3D files\0*.obj;*.stl;*.ply;*.off;*.glb;*.gltf\0"
        L"Wavefront OBJ (*.obj)\0*.obj\0"
        L"STL (*.stl)\0*.stl\0"
        L"PLY (*.ply)\0*.ply\0"
        L"OFF (*.off)\0*.off\0"
        L"glTF / GLB (*.glb;*.gltf)\0*.glb;*.gltf\0"
        L"All files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return {};
    return toUtf8(file);
}

void showErrorMessage(const std::string& title, const std::string& message) {
    auto widen = [](const std::string& s) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring w(static_cast<size_t>(std::max(n, 1)), L'\0');
        if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
        return w;
    };
    MessageBoxW(nullptr, widen(message).c_str(), widen(title).c_str(), MB_OK | MB_ICONERROR);
}

std::vector<std::string> commandLineArguments(int, char**) {
    std::vector<std::string> args;
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argvW) return args;
    for (int i = 0; i < argc; ++i) args.push_back(toUtf8(argvW[i]));
    LocalFree(argvW);
    return args;
}

}  // namespace mi::platform
