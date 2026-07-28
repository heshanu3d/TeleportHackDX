#include "core/hook.h"

#include <MinHook.h>
#include <imm.h>

#include <string>

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "ui/app.h"

// imgui_impl_win32.h deliberately comments out this declaration (to avoid
// dragging <windows.h> into that header); we already include <Windows.h>
// via core/hook.h, so declare it here ourselves at global scope (must NOT
// live inside the anonymous namespace below, or it silently gets internal
// linkage and fails to resolve to the real definition in
// imgui_impl_win32.cpp).
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                              LPARAM lParam);

namespace {

typedef HRESULT(APIENTRY* EndSceneFn)(LPDIRECT3DDEVICE9);
typedef HRESULT(APIENTRY* ResetFn)(LPDIRECT3DDEVICE9, D3DPRESENT_PARAMETERS*);

EndSceneFn oEndScene = nullptr;
ResetFn oReset = nullptr;
WNDPROC oWndProc = nullptr;

struct ImeControlRequest {
    bool want_text = false;
    bool want_visible = false;
    LONG input_x = 0;
    LONG input_y = 0;
};

bool g_initialized = false;
bool g_show_gui = true;
bool g_ime_text_owned = false;
bool g_ime_transitioning = false;
bool g_ime_associated_default = false;
bool g_ime_opened_by_overlay = false;
HWND g_game_hwnd = nullptr;
HWND g_ime_hwnd = nullptr;
HIMC g_ime_context = nullptr;
UINT g_ime_control_message = 0;
SRWLOCK g_pending_ime_lock = SRWLOCK_INIT;
ImeControlRequest g_pending_ime_request;
ImeControlRequest g_applied_ime_request;

using PlatformSetImeDataFn =
    void (*)(ImGuiContext*, ImGuiViewport*, ImGuiPlatformImeData*);
PlatformSetImeDataFn g_original_set_ime_data = nullptr;

th::App g_app;

bool OwnsImeMessages() { return g_ime_text_owned || g_ime_transitioning; }

void ResetImeState() {
    g_ime_context = nullptr;
    g_ime_hwnd = nullptr;
    g_ime_associated_default = false;
    g_ime_opened_by_overlay = false;
    g_ime_text_owned = false;
    g_ime_transitioning = false;
}

void RestoreOverlayOpenedContext() {
    if (g_ime_opened_by_overlay && g_ime_context && ImmGetOpenStatus(g_ime_context)) {
        ImmSetOpenStatus(g_ime_context, FALSE);
    }
    g_ime_opened_by_overlay = false;
}

void ReleaseImeOwnership() {
    if (!OwnsImeMessages()) return;
    g_ime_transitioning = true;

    HIMC current = g_ime_hwnd ? ImmGetContext(g_ime_hwnd) : nullptr;
    if (current) {
        ImmNotifyIME(current, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
    }
    RestoreOverlayOpenedContext();
    if (current) ImmReleaseContext(g_ime_hwnd, current);
    if (g_ime_hwnd && g_ime_associated_default) {
        ImmAssociateContext(g_ime_hwnd, nullptr);
    }
    ResetImeState();
}

void AcquireImeOwnership(HWND hwnd) {
    if (g_ime_text_owned && g_ime_hwnd != hwnd) ReleaseImeOwnership();

    // Set ownership before IMM calls: they can synchronously re-enter WndProc
    // with WM_IME_NOTIFY/WM_IME_SETCONTEXT messages.
    g_ime_text_owned = true;
    g_ime_transitioning = true;
    g_ime_hwnd = hwnd;

    HIMC context = ImmGetContext(hwnd);
    if (!context && ImmAssociateContextEx(hwnd, nullptr, IACE_DEFAULT)) {
        context = ImmGetContext(hwnd);
        g_ime_associated_default = context != nullptr;
        if (!context) ImmAssociateContext(hwnd, nullptr);
    }
    if (!context) {
        g_ime_transitioning = false;
        return; // keep text ownership; a later callback can retry association
    }

    if (context != g_ime_context) {
        if (g_ime_context) RestoreOverlayOpenedContext();
        g_ime_context = context;
        if (!ImmGetOpenStatus(context)) {
            g_ime_opened_by_overlay = ImmSetOpenStatus(context, TRUE) != FALSE;
        }
    }
    ImmReleaseContext(hwnd, context);
    g_ime_transitioning = false;
}

void PositionImeWindows(HWND hwnd, LONG x, LONG y) {
    HIMC context = ImmGetContext(hwnd);
    if (!context) return;

    COMPOSITIONFORM composition = {};
    composition.dwStyle = CFS_FORCE_POSITION;
    composition.ptCurrentPos.x = x;
    composition.ptCurrentPos.y = y;
    ImmSetCompositionWindow(context, &composition);

    CANDIDATEFORM candidate = {};
    candidate.dwStyle = CFS_CANDIDATEPOS;
    candidate.ptCurrentPos.x = x;
    candidate.ptCurrentPos.y = y;
    ImmSetCandidateWindow(context, &candidate);
    ImmReleaseContext(hwnd, context);
}

void ApplyImeControl(HWND hwnd, const ImeControlRequest& request) {
    g_applied_ime_request = request;
    if (request.want_text) {
        AcquireImeOwnership(hwnd);
        if (request.want_visible && g_ime_context) {
            PositionImeWindows(hwnd, request.input_x, request.input_y);
        }
    } else {
        ReleaseImeOwnership();
    }
}

void DispatchImeControl(HWND hwnd, const ImeControlRequest& request) {
    if (!hwnd) return;
    DWORD window_thread = GetWindowThreadProcessId(hwnd, nullptr);
    if (window_thread == GetCurrentThreadId() || g_ime_control_message == 0) {
        ApplyImeControl(hwnd, request);
    } else {
        AcquireSRWLockExclusive(&g_pending_ime_lock);
        g_pending_ime_request = request;
        ReleaseSRWLockExclusive(&g_pending_ime_lock);
        PostMessage(hwnd, g_ime_control_message, 0, 0);
    }
}

void SetOverlayImeData(ImGuiContext*, ImGuiViewport* viewport, ImGuiPlatformImeData* data) {
    HWND hwnd = static_cast<HWND>(viewport->PlatformHandleRaw);
    ImeControlRequest request;
    request.want_text = data->WantTextInput;
    request.want_visible = data->WantVisible;
    request.input_x = static_cast<LONG>(data->InputPos.x);
    request.input_y = static_cast<LONG>(data->InputPos.y);
    DispatchImeControl(hwnd, request);
}

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
            return OwnsImeMessages() || io.WantCaptureKeyboard;
        default:
            return false;
    }
}

bool IsImeMessage(UINT msg) {
    switch (msg) {
        case WM_IME_SETCONTEXT:
        case WM_IME_NOTIFY:
        case WM_IME_CONTROL:
        case WM_IME_COMPOSITIONFULL:
        case WM_IME_SELECT:
        case WM_IME_CHAR:
        case WM_IME_REQUEST:
        case WM_IME_KEYDOWN:
        case WM_IME_KEYUP:
        case WM_IME_STARTCOMPOSITION:
        case WM_IME_COMPOSITION:
        case WM_IME_ENDCOMPOSITION:
            return true;
        default:
            return false;
    }
}

LRESULT DefaultWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return IsWindowUnicode(hwnd) ? DefWindowProcW(hwnd, msg, wParam, lParam)
                                 : DefWindowProcA(hwnd, msg, wParam, lParam);
}

LRESULT APIENTRY WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (g_ime_control_message && uMsg == g_ime_control_message) {
        ImeControlRequest request;
        AcquireSRWLockExclusive(&g_pending_ime_lock);
        request = g_pending_ime_request;
        ReleaseSRWLockExclusive(&g_pending_ime_lock);
        ApplyImeControl(hwnd, request);
        return 0;
    }

    // Global hotkeys (teleport binds) work regardless of overlay visibility.
    if (uMsg == WM_HOTKEY) {
        g_app.on_wndproc(uMsg, wParam, lParam);
        return 0;
    }

    if (IsImeMessage(uMsg)) {
        if (!OwnsImeMessages()) {
            // When WoW owns text input, don't let ImGui touch IME messages at
            // all. Its MBCS handler calls DefWindowProcW internally, so also
            // forwarding to WoW would process the same composition twice.
            return CallWindowProc(oWndProc, hwnd, uMsg, wParam, lParam);
        }

        LRESULT imgui_result = ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam);
        if (uMsg == WM_IME_COMPOSITION) {
            return imgui_result; // backend already called DefWindowProcW
        }
        if (uMsg == WM_IME_CHAR && !IsWindowUnicode(hwnd)) {
            return imgui_result; // backend converted and consumed the DBCS character
        }
        LRESULT result = DefaultWindowProc(hwnd, uMsg, wParam, lParam);
        if (uMsg == WM_IME_SELECT && g_ime_text_owned) {
            ApplyImeControl(hwnd, g_applied_ime_request);
        }
        return result;
    }

    if (g_show_gui) {
        ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam);
        if (uMsg == WM_INPUTLANGCHANGE && g_ime_text_owned) {
            LRESULT result = CallWindowProc(oWndProc, hwnd, uMsg, wParam, lParam);
            ApplyImeControl(hwnd, g_applied_ime_request);
            return result;
        }
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
        io.IniFilename = nullptr; // App serializes this mode's layout into settings.json
        io.MouseDrawCursor = false;
        LoadUiFont();

        ImGui_ImplWin32_Init(params.hFocusWindow);
        ImGui_ImplDX9_Init(pDevice);

        g_game_hwnd = params.hFocusWindow;
        g_ime_control_message = RegisterWindowMessageA("TeleportHackDX.ImeControl.v1");
        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
        g_original_set_ime_data = platform_io.Platform_SetImeDataFn;
        platform_io.Platform_SetImeDataFn = SetOverlayImeData;

        oWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtr(params.hFocusWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));

        g_app.initialize(params.hFocusWindow, th::BackendMode::InProcess);

        g_initialized = true;
    }

    // Win+Insert (per the reference project's README) toggles the overlay;
    // implemented as: Insert edge-trigger while either Windows key is down.
    bool win_down = (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);
    if (win_down && (GetAsyncKeyState(VK_INSERT) & 1)) {
        if (g_show_gui) DispatchImeControl(g_game_hwnd, ImeControlRequest{});
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
    // Called from DllMain under the loader lock. IMM teardown can synchronously
    // send window messages, so only normal overlay hide performs restoration;
    // process teardown lets the OS discard any remaining association.
    ResetImeState();
    g_app.shutdown();
    if (g_initialized) {
        ImGui::GetPlatformIO().Platform_SetImeDataFn = g_original_set_ime_data;
        g_original_set_ime_data = nullptr;
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    g_game_hwnd = nullptr;
    g_ime_control_message = 0;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
}
