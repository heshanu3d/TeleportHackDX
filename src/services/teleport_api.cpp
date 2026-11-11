#include "services/teleport_api.h"
#include "services/teleport_service.h"

#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace th {
FavouriteTeleportRoute choose_favourite_teleport_route(bool api_enabled, const Position& pos,
                                                       TeleportService* teleport) {
    if (!api_enabled) return FavouriteTeleportRoute::Normal;
    if (!pos.map_id) return FavouriteTeleportRoute::MissingDestinationMap;
    uint32_t current = 0;
    if (!teleport || !teleport->read_map_id(current))
        return FavouriteTeleportRoute::CurrentMapUnavailable;
    return current == *pos.map_id ? FavouriteTeleportRoute::Normal : FavouriteTeleportRoute::Api;
}

bool build_teleport_api_command(const Position& pos, uint32_t token,
                                std::string& command, std::string& error) {
    command.clear();
    error.clear();
    if (!pos.map_id) { error = "destination map ID is missing"; return false; }
    if (token == 0 || token > 999999999) { error = "invalid request token"; return false; }
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z) ||
        std::abs(pos.x) > 17066.0 || std::abs(pos.y) > 17066.0 ||
        std::abs(pos.z) > 400000.0 ||
        (pos.orientation && (!std::isfinite(*pos.orientation) ||
                             std::abs(*pos.orientation) > 12.566370))) {
        error = "invalid coordinate or facing";
        return false;
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << ".synthopen tp xyz " << token << ' ' << *pos.map_id << ' '
        << std::fixed << std::setprecision(6) << pos.x << ' ' << pos.y << ' ' << pos.z << ' ';
    if (pos.orientation) out << *pos.orientation; else out << '-';
    command = out.str();
    return true;
}

std::string build_teleport_api_script(const std::string& command) {
    return "if not TeleportHackDXApiFrame then "
        "TeleportHackDXApiFrame=CreateFrame('Frame'); "
        "TeleportHackDXApiFrame:RegisterEvent('CHAT_MSG_ADDON'); "
        "TeleportHackDXApiFrame:SetScript('OnEvent',function() "
        "if arg1=='TELEPORTUI' and arg4==UnitName('player') then "
        "local _,_,id,status=string.find(arg2,'^D:(%d+):(%a+)$'); "
        "if id then DEFAULT_CHAT_FRAME:AddMessage('[Teleport API] '..id..': '..status) end "
        "end end) end; "
        "if UnitName('player') then SendChatMessage('" + command + "','SAY') "
        "else DEFAULT_CHAT_FRAME:AddMessage('[Teleport API] player not available') end";
}
} // namespace th
