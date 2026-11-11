#pragma once
#include <Windows.h>
#include <string>

// Only used by the injected DLL, never by the external desktop process.
bool queue_game_teleport_api(HWND hwnd, const std::string& command, std::string& error);
bool handle_game_teleport_api_message(UINT message);
void shutdown_game_teleport_api();
