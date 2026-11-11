#pragma once

#include <cstdint>
#include <string>
#include "domain/models.h"

namespace th {
class TeleportService;

enum class FavouriteTeleportRoute {
    Normal,
    Api,
    MissingDestinationMap,
    CurrentMapUnavailable,
};

// The disabled path does not consult the map reader at all. When enabled,
// unknown map IDs fail closed; only confirmed different maps use the API.
FavouriteTeleportRoute choose_favourite_teleport_route(bool api_enabled, const Position& position,
                                                       TeleportService* teleport);

// Builds only a validated numeric payload. Missing orientation is represented
// by "-" and is resolved to the player's current facing by the server.
bool build_teleport_api_command(const Position& position, uint32_t token,
                                std::string& command, std::string& error);
// Input must be the validated command produced by build_teleport_api_command.
std::string build_teleport_api_script(const std::string& command);
}
