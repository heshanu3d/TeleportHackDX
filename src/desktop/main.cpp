// Standalone desktop entry point for TeleportHackDesktop.exe.
//
// Bootstraps an ordinary Win32 window + Direct3D9 device (adapted from
// Dear ImGui's examples/example_win32_directx9/main.cpp) and drives the
// exact same th::App used by the injected DLL (src/core/hook.cpp), just
// in BackendMode::Attach instead of BackendMode::InProcess: it attaches
// to a separately running WoW.exe over ReadProcessMemory/WriteProcessMemory
// via the "WoW 进程" picker in the UI, rather than operating in-process.
//
// This exists so the UI/favlist/hotkey logic can be exercised and
// iterated on without re-injecting a DLL for every change.
#include <Windows.h>
#include <d3d9.h>

#include <string>

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "repository/settings_repository.h"
#include "ui/app.h"
#include "util/paths.h"

#pragma comment(lib, "d3d9.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                              LPARAM lParam);

namespace {

IDirect3D9* g_pD3D = nullptr;
IDirect3DDevice9* g_pd3dDevice = nullptr;
D3DPRESENT_PARAMETERS g_d3dpp = {};
bool g_device_lost = false;
UINT g_resize_width = 0, g_resize_height = 0;

th::App g_app;

bool CreateDeviceD3D(HWND hwnd) {
    g_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_pD3D) return false;

    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                              D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp,
                              &g_pd3dDevice) < 0) {
        return false;
    }
    return true;
}

void CleanupDeviceD3D() {
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
    if (g_pD3D) {
        g_pD3D->Release();
        g_pD3D = nullptr;
    }
}

void ResetDevice() {
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
    if (hr == D3DERR_INVALIDCALL) {
        // Extremely unlikely for a plain windowed device; nothing sane to
        // do besides bail rather than spin forever.
        return;
    }
    ImGui_ImplDX9_CreateDeviceObjects();
}

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_HOTKEY) {
        g_app.on_wndproc(msg, wParam, lParam);
        return 0;
    }

    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;

    switch (msg) {
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) return 0;
            g_resize_width = static_cast<UINT>(LOWORD(lParam));
            g_resize_height = static_cast<UINT>(HIWORD(lParam));
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0; // disable ALT menu
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

} // namespace

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Read just the window bounds up front (App::initialize() does its own
    // full settings load later, once it knows the window handle) so the
    // OS window is created at whatever size/position the user left it.
    th::SettingsRepository bounds_repo(th::resolve_path("settings.json"));
    th::Settings initial = bounds_repo.load();
    int start_x = initial.window_x >= 0 ? initial.window_x : 100;
    int start_y = initial.window_y >= 0 ? initial.window_y : 100;
    int start_w = initial.window_width > 0 ? initial.window_width : 500;
    int start_h = initial.window_height > 0 ? initial.window_height : 900;

    WNDCLASSEXA wc = {sizeof(wc),  CS_CLASSDC, WndProc, 0L,      0L,
                       hInstance,  nullptr,    nullptr, nullptr, nullptr,
                       "TeleportHackDesktopWndClass", nullptr};
    RegisterClassExA(&wc);
    HWND hwnd = CreateWindowA(wc.lpszClassName, "TeleportHack Desktop (Attach mode)",
                               WS_OVERLAPPEDWINDOW, start_x, start_y, start_w, start_h, nullptr,
                               nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassA(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);

    // Best-effort CJK font so favlist descriptions render correctly; falls
    // back to the default ASCII-only font if the system doesn't have it.
    {
        char windir[MAX_PATH] = {0};
        GetWindowsDirectoryA(windir, MAX_PATH);
        const ImWchar* ranges = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
        const char* candidates[] = {"\\Fonts\\msyh.ttc", "\\Fonts\\simsun.ttc",
                                     "\\Fonts\\simhei.ttf"};
        bool loaded = false;
        for (const char* rel : candidates) {
            std::string path = std::string(windir) + rel;
            if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
                ImFontConfig cfg;
                io.Fonts->AddFontFromFileTTF(path.c_str(), 18.0f, &cfg, ranges);
                loaded = true;
                break;
            }
        }
        if (!loaded) io.Fonts->AddFontDefault();
    }

    g_app.initialize(hwnd, th::BackendMode::Attach);

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_device_lost) {
            HRESULT hr = g_pd3dDevice->TestCooperativeLevel();
            if (hr == D3DERR_DEVICELOST) {
                Sleep(10);
                continue;
            }
            if (hr == D3DERR_DEVICENOTRESET) ResetDevice();
            g_device_lost = false;
        }
        if (g_resize_width != 0 && g_resize_height != 0) {
            g_d3dpp.BackBufferWidth = g_resize_width;
            g_d3dpp.BackBufferHeight = g_resize_height;
            g_resize_width = g_resize_height = 0;
            ResetDevice();
        }

        g_app.tick();

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        g_app.render();

        ImGui::EndFrame();
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        g_pd3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                             D3DCOLOR_RGBA(30, 30, 30, 255), 1.0f, 0);
        if (g_pd3dDevice->BeginScene() >= 0) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDevice->EndScene();
        }
        HRESULT result = g_pd3dDevice->Present(nullptr, nullptr, nullptr, nullptr);
        if (result == D3DERR_DEVICELOST) g_device_lost = true;
    }

    // Persist wherever the user ended up leaving the window.
    RECT rect;
    if (GetWindowRect(hwnd, &rect)) {
        g_app.set_window_bounds(rect.left, rect.top, rect.right - rect.left,
                                 rect.bottom - rect.top);
    }

    g_app.shutdown();
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return 0;
}
