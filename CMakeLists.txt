cmake_minimum_required(VERSION 3.15)

# Single source of truth for the version number -- also read directly by
# .github/workflows/build.yml to name release artifacts.
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

# --------------------------------------------------------- shared core lib
# Domain/services/repository/memory-backends/UI: identical source shared by
# both the injected DLL and the standalone desktop app (see src/ui/app.h
# for why -- MemoryBackend abstracts away in-process vs. attach-over-RPM).
set(CORE_SOURCES
    src/domain/models.cpp
    src/domain/versions.cpp
    src/memory/inprocess_backend.cpp
    src/memory/outofprocess_backend.cpp
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
    src/util/process_list.cpp
)

add_library(th_core STATIC ${CORE_SOURCES})
target_include_directories(th_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# PUBLIC: app.cpp calls into imgui/comdlg32 symbols that only get resolved
# once a final DLL/EXE links against th_core, so both must propagate to
# whichever target consumes th_core.
target_link_libraries(th_core PUBLIC imgui comdlg32)

if(MSVC)
    target_compile_options(th_core PRIVATE /W4 /permissive-)
endif()

# --------------------------------------------------------------- main DLL
# Injected into WoW.exe; hooks D3D9 EndScene/Reset via MinHook.
add_library(TeleportHackDX SHARED
    src/dllmain.cpp
    src/core/hook.cpp
)
target_link_libraries(TeleportHackDX PRIVATE th_core minhook d3d9)
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

# --------------------------------------------------------------- tests
enable_testing()
add_executable(orientation_tests
    tests/orientation_tests.cpp
)
target_link_libraries(orientation_tests PRIVATE th_core)
if(MSVC)
    target_compile_options(orientation_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME orientation_compatibility COMMAND orientation_tests)

# Standard Windows SDK ships d3d9.h/d3d9.lib -- no legacy DirectX SDK
# (June 2010) required, unlike some older tutorials.
