cmake_minimum_required(VERSION 3.15)
project(TeleportHackDX CXX C)

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

# --------------------------------------------------------------- main DLL
set(SOURCES
    src/dllmain.cpp
    src/core/hook.cpp
    src/domain/models.cpp
    src/domain/versions.cpp
    src/memory/inprocess_backend.cpp
    src/services/teleport_service.cpp
    src/services/feature_service.cpp
    src/services/favourites_service.cpp
    src/services/favourites_porter.cpp
    src/services/hotkey_service.cpp
    src/repository/favlist_repository.cpp
    src/repository/hotkey_config_repository.cpp
    src/repository/settings_repository.cpp
    src/ui/app.cpp
    src/util/paths.cpp
)

add_library(TeleportHackDX SHARED ${SOURCES})

target_include_directories(TeleportHackDX PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(TeleportHackDX PRIVATE
    imgui
    minhook
    d3d9
    comdlg32
)

# /W4 /permissive- scoped to just our own code -- vendored imgui/minhook
# build at their normal warning level (they have a handful of harmless
# upstream warnings that aren't worth fighting).
if(MSVC)
    target_compile_options(TeleportHackDX PRIVATE /W4 /permissive-)
endif()

# Standard Windows SDK ships d3d9.h/d3d9.lib -- no legacy DirectX SDK
# (June 2010) required, unlike some older tutorials.
