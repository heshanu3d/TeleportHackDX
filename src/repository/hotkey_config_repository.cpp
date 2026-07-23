#include "repository/hotkey_config_repository.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace th {

namespace {
std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}
} // namespace

bool HotkeyConfigRepository::load(std::vector<HotkeyBinding>& out, std::string& error) const {
    out.clear();
    if (!std::filesystem::exists(path_)) return true;

    std::ifstream fh(path_, std::ios::binary);
    if (!fh) {
        error = "cannot open " + path_ + " for reading";
        return false;
    }
    std::ostringstream ss;
    ss << fh.rdbuf();
    std::string content = ss.str();

    std::vector<std::string> cleaned;
    std::string line;
    std::istringstream lines(content);
    while (std::getline(lines, line)) {
        std::string t = trim(line);
        if (t.empty()) continue;
        if (t[0] == '#') continue;
        cleaned.push_back(t);
    }
    for (size_t i = 0; i + 1 < cleaned.size(); i += 2) {
        out.push_back(HotkeyBinding{cleaned[i], cleaned[i + 1]});
    }
    return true;
}

} // namespace th
