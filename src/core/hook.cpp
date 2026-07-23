#include "core/hook.h"

#include <MinHook.h>

#include <string>

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "ui/app.h"

namespace {

typedef HRESULT(APIENTRY* EndSceneFn)(LPDIRECT3DDEVICE9);
typedef HRESULT(APIENTRY* ResetFn)(LPDIRECT3DDEVICE9, D3DPRESENT_PARAMETERS*);

EndSceneFn oEndScene = nullptr;
ResetFn oReset = nullptr;
WNDPROC oWndProc = nullptr;

bool g_initialized = false;
bool g_show_gui = true;

th::App g_app;

// Load a CJK-capable system font so favlist descriptions (which are UTF-8
// Chinese, see favlist.fav / hotkey.txt) render as glyphs instead of
// tofu boxes. Falls back to ImGui's built-in font if none is found.
void LoadUiFont() {
    ImGuiIO& io = ImGui::GetIO();
    static const ImWchar* ranges = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();

    char windir[MAX_PATH] = {0};
    GetWindowsDirectoryA(windir, MAX_PATH);
    const char* candidates[] = {"\\Fonts\\msyh.ttc", "\\Fonts\\msyhbd.ttc",
                                 "\\Fonts\\simsun.ttc", "\\Fonts\\simhei.ttf"};
    for (const char* rel : candidates) {
        std::string path = std::string(windir) + rel;
        if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            ImFontConfig cfg;
            cfg.OversampleH = cfg.OversampleV = 1;
            io.Fonts->AddFontFromFileTTF(path.c_str(), 18.0f, &cfg, ranges);
            return;
        }
    }
    io.Fonts->AddFontDefault(); // ASCII-only fallback; Chinese text will
                                // show as missing glyphs.
}

// Toggles game input capture while the overlay wants the mouse/keyboard,
// so clicking a button doesn't also fire in the game world.
bool ShouldBlockMessage(UINT msg) {
    ImGuiIO& io = ImGui::GetIO();
    switch (msg) {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
            return io.WantCaptureMouse;
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_CHAR:
            return io.WantCaptureKeyboard;
        default:
            return false;
    }
}

LRESULT APIENTRY WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Global hotkeys (teleport binds) work regardless of overlay visibility.
    if (uMsg == WM_HOTKEY) {
        g_app.on_wndproc(uMsg, wParam, lParam);
        return 0;
    }

    if (g_show_gui) {
        ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam);
        if (ShouldBlockMessage(uMsg)) return TRUE;
    }

    return CallWindowProc(oWndProc, hwnd, uMsg, wParam, lParam);
}

HRESULT APIENTRY HookedEndScene(LPDIRECT3DDEVICE9 pDevice) {
    if (!g_initialized) {
        D3DDEVICE_CREATION_PARAMETERS params;
        pDevice->GetCreationParameters(&params);

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; // don't clutter the game folder with imgui.ini
        io.MouseDrawCursor = false;
        LoadUiFont();

        ImGui_ImplWin32_Init(params.hFocusWindow);
        ImGui_ImplDX9_Init(pDevice);

        oWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtr(params.hFocusWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));

        g_app.initialize(params.hFocusWindow);

        g_initialized = true;
    }

    // Win+Insert (per the reference project's README) toggles the overlay;
    // implemented as: Insert edge-trigger while either Windows key is down.
    bool win_down = (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);
    if (win_down && (GetAsyncKeyState(VK_INSERT) & 1)) {
        g_show_gui = !g_show_gui;
        ImGui::GetIO().MouseDrawCursor = g_show_gui;
    }

    g_app.tick(); // advance step-teleport session every frame regardless of visibility

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (g_show_gui) {
        g_app.render();
    }

    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

    return oEndScene(pDevice);
}

HRESULT APIENTRY HookedReset(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pParams) {
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT result = oReset(pDevice, pParams);
    if (SUCCEEDED(result)) {
        ImGui_ImplDX9_CreateDeviceObjects();
    }
    return result;
}

} // namespace

void InitializeHook() {
    if (MH_Initialize() != MH_OK) return;

    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return;

    D3DPRESENT_PARAMETERS d3dpp = {};
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = GetForegroundWindow();
    d3dpp.Windowed = TRUE;

    IDirect3DDevice9* pDevice = nullptr;
    HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp.hDeviceWindow,
                                     D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDevice);
    if (FAILED(hr) || !pDevice) {
        pD3D->Release();
        return;
    }

    void** vtable = *reinterpret_cast<void***>(pDevice);

    if (MH_CreateHook(vtable[42], reinterpret_cast<LPVOID>(&HookedEndScene),
                       reinterpret_cast<LPVOID*>(&oEndScene)) != MH_OK ||
        MH_CreateHook(vtable[16], reinterpret_cast<LPVOID>(&HookedReset),
                       reinterpret_cast<LPVOID*>(&oReset)) != MH_OK) {
        pDevice->Release();
        pD3D->Release();
        return;
    }

    MH_EnableHook(vtable[42]);
    MH_EnableHook(vtable[16]);

    pDevice->Release();
    pD3D->Release();
}

void CleanupHook() {
    g_app.shutdown();
    if (g_initialized) {
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
}
