cmake_minimum_required(VERSION 3.15)

# CMake project version. CI artifact names come from Git tags; VERSION is
# only the fallback base when the repository has no semantic-version tag yet.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" TELEPORTHACKDX_VERSION)
string(STRIP "${TELEPORTHACKDX_VERSION}" TELEPORTHACKDX_VERSION)

project(TeleportHackDX VERSION ${TELEPORTHACKDX_VERSION} LANGUAGES CXX C)

# --- 32-bit only: these WoW clients (1.12.1 / 1.12.3 / 3.3.5) all ship as
# 32-bit executables, and every hardcoded memory address in
# src/domain/versions.cpp assumes a 32-bit address space / pointer size.
if(NOT CMAKE_GENERATOR_PLATFORM)
    set(CMAKE_GENERATOR_PLATFORM Win32)
endif()
set(CMAKE_VS_PLATFORM_NAME "Win32")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------------------------------------------------------------- MinHook
add_library(minhook STATIC
    vendor/minhook/src/buffer.c
    vendor/minhook/src/hook.c
    vendor/minhook/src/trampoline.c
    vendor/minhook/src/hde/hde32.c
)
target_include_directories(minhook PUBLIC vendor/minhook/include)
target_include_directories(minhook PRIVATE vendor/minhook/src vendor/minhook/src/hde)

# ------------------------------------------------------------------ ImGui
add_library(imgui STATIC
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/imgui/backends/imgui_impl_dx9.cpp
    vendor/imgui/backends/imgui_impl_win32.cpp
)
target_include_directories(imgui PUBLIC vendor/imgui vendor/imgui/backends)

# ------------------------------------------------------- business core
# Attach-mode business logic (domain / services / favourites /
# out-of-process memory backend), no UI and no dependencies on Win32
# widgets, ImGui, or SEH. Consumed by both th_core (which layers ImGui
# App plus injection-only pieces on top) and TeleportHackCApi (which
# exposes the same services to external C# UIs). Keeping this cut
# narrow lets the C ABI DLL stay free of ImGui / D3D / comdlg32 pulls.
set(BUSINESS_SOURCES
    src/domain/models.cpp
    src/domain/versions.cpp
    src/domain/category_groups.cpp
    src/domain/app_info.cpp
    src/memory/outofprocess_backend.cpp
    src/services/teleport_service.cpp
    src/services/feature_service.cpp
    src/services/favourites_service.cpp
    src/services/favourites_porter.cpp
    src/services/recent_teleports.cpp
    src/repository/favlist_repository.cpp
    src/repository/settings_repository.cpp
    src/repository/teleport_lua_repository.cpp
    src/util/process_list.cpp
)

# Version string shown on every UI's "关于" tab. CI passes the computed
# artifact version (e.g. 0.1.3rc); local builds just say "dev".
set(TH_BUILD_VERSION "dev" CACHE STRING "Version shown on the About tab")
set_source_files_properties(src/domain/app_info.cpp PROPERTIES
    COMPILE_DEFINITIONS "TH_BUILD_VERSION=\"${TH_BUILD_VERSION}\"")
if(MSVC)
    # app_info.cpp holds UTF-8 Chinese release notes as plain literals.
    set_source_files_properties(src/domain/app_info.cpp PROPERTIES COMPILE_OPTIONS "/utf-8")
endif()

add_library(th_business STATIC ${BUSINESS_SOURCES})
target_include_directories(th_business PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(th_business PRIVATE /W4 /permissive-)
endif()

# --------------------------------------------------------- shared core lib
# Adds the ImGui-driven App layer plus injection-only helpers (SEH-guarded
# in-process backend, Win32 hotkeys, Teleport.lua editor, DLL-directory
# path resolver) on top of th_business. Consumed only by the injected DLL
# and the ImGui desktop EXE.
add_library(th_core STATIC
    src/memory/inprocess_backend.cpp
    src/services/hotkey_service.cpp
    src/repository/hotkey_config_repository.cpp
    src/util/paths.cpp
    src/ui/app.cpp
)
target_link_libraries(th_core PUBLIC th_business imgui comdlg32)

if(MSVC)
    target_compile_options(th_core PRIVATE /W4 /permissive-)
endif()

# --------------------------------------------------------------- main DLL
# Injected into WoW.exe; hooks D3D9 EndScene/Reset via MinHook.
add_library(TeleportHackDX SHARED
    src/dllmain.cpp
    src/core/hook.cpp
)
target_link_libraries(TeleportHackDX PRIVATE th_core minhook d3d9 imm32)
if(MSVC)
    target_compile_options(TeleportHackDX PRIVATE /W4 /permissive-)
endif()

# ------------------------------------------------------- standalone desktop
# Ordinary Win32 window + its own D3D9 device; attaches to a separately
# running WoW.exe instead of being injected. Lets the UI/favlist/hotkey
# logic be exercised without re-injecting for every change.
add_executable(TeleportHackDesktop WIN32
    src/desktop/main.cpp
)
target_link_libraries(TeleportHackDesktop PRIVATE th_core d3d9)
if(MSVC)
    target_compile_options(TeleportHackDesktop PRIVATE /W4 /permissive-)
endif()

# ------------------------------------------------------------- C ABI DLL
# Loaded by external processes (WPF/WinForms C# UIs). Wraps th_business's
# Attach-mode services behind a minimal `extern "C"` surface (see
# src/capi/api.h). Kept independent of ImGui/D3D so a caller pays no
# CLR/graphics startup cost. Static-links th_core so the injection DLL's
# runtime remains untouched.
add_library(TeleportHackCApi SHARED
    src/capi/api.cpp
)
target_link_libraries(TeleportHackCApi PRIVATE th_business)
target_compile_definitions(TeleportHackCApi PRIVATE TH_CAPI_BUILDING)
if(MSVC)
    target_compile_options(TeleportHackCApi PRIVATE /W4 /permissive-)
endif()

# --------------------------------------------------------------- launcher
# Console launcher: starts configured WoW.exe suspended, resumes process
# initialization, then injects the sibling TeleportHackDX.dll. Kept independent
# of th_core so it doesn't pull ImGui/D3D into the small launcher executable.
add_executable(TeleportHackLauncher
    src/launcher/main.cpp
    src/domain/models.cpp
    src/repository/settings_repository.cpp
)
target_include_directories(TeleportHackLauncher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(TeleportHackLauncher PRIVATE /W4 /permissive-)
endif()

# --------------------------------------------------------------- tests
enable_testing()
add_executable(orientation_tests
    tests/orientation_tests.cpp
)
target_link_libraries(orientation_tests PRIVATE th_business)
if(MSVC)
    target_compile_options(orientation_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME orientation_compatibility COMMAND orientation_tests)

add_executable(teleport_lua_tests
    tests/teleport_lua_repository_tests.cpp
)
target_link_libraries(teleport_lua_tests PRIVATE th_business)
if(MSVC)
    target_compile_options(teleport_lua_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME teleport_lua_repository COMMAND teleport_lua_tests)

# TeleportHackOnVanilla d462c4c..309516d sync: map IDs, 1.12.x facing and
# read order, cross-map guard, recent teleports, category navigation groups.
add_executable(sync_tests
    tests/sync_tests.cpp
)
target_link_libraries(sync_tests PRIVATE th_business)
if(MSVC)
    target_compile_options(sync_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME vanilla_sync COMMAND sync_tests)

add_executable(settings_repository_tests
    tests/settings_repository_tests.cpp
)
target_link_libraries(settings_repository_tests PRIVATE th_core)
if(MSVC)
    target_compile_options(settings_repository_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME settings_repository COMMAND settings_repository_tests)

# End-to-end exercise of the C ABI: statically links the same api.cpp
# implementation the SHARED library exports so we can call every entry
# point directly from the test executable without dealing with runtime
# loader differences between Linux/Windows. Verifies success paths,
# error propagation, buffer truncation semantics, and favourites parsing
# against a synthetic favlist.
add_executable(capi_tests
    tests/capi_tests.cpp
    src/capi/api.cpp
)
target_link_libraries(capi_tests PRIVATE th_business)
target_include_directories(capi_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(capi_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME capi COMMAND capi_tests)


# Standard Windows SDK ships d3d9.h/d3d9.lib -- no legacy DirectX SDK
# (June 2010) required, unlike some older tutorials.
