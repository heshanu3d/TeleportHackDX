#include "services/teleport_api.h"
#include <iostream>
#include <limits>
#include <string>

int main(int argc, char** argv) {
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };
    th::Position pos{1.25, -2.5, 3.0, std::nullopt};
    std::string command, error;
    check(!th::build_teleport_api_command(pos, 1, command, error) && command.empty(),
          "missing map is rejected");
    pos.map_id = 0;
    if (argc == 2 && std::string(argv[1]) == "--emit-script") {
        if (!th::build_teleport_api_command(pos, 42, command, error)) return 1;
        std::cout << th::build_teleport_api_script(command);
        return 0;
    }
    check(th::build_teleport_api_command(pos, 42, command, error) &&
              command == ".synthopen tp xyz 42 0 1.250000 -2.500000 3.000000 -",
          "map zero and omitted facing are preserved");
    pos.map_id = 230;
    pos.orientation = 1.5;
    check(th::build_teleport_api_command(pos, 99, command, error) &&
              command == ".synthopen tp xyz 99 230 1.250000 -2.500000 3.000000 1.500000",
          "instance map and facing are encoded");
    check(!th::build_teleport_api_command(pos, 0, command, error), "zero token rejected");
    pos.x = std::numeric_limits<double>::infinity();
    check(!th::build_teleport_api_command(pos, 1, command, error), "infinite coordinate rejected");
    pos.x = 0; pos.orientation = std::numeric_limits<double>::quiet_NaN();
    check(!th::build_teleport_api_command(pos, 1, command, error), "NaN facing rejected");
    pos.orientation = 0; pos.y = 17067;
    check(!th::build_teleport_api_command(pos, 1, command, error), "out of bounds rejected");
    if (failures) return 1;
    std::cout << "teleport API tests passed\n";
}
