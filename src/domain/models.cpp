#include "domain/models.h"

#include <cstdio>
#include <unordered_map>

namespace th {

std::string format_float(double value) {
    double rounded = std::round(value);
    if (std::fabs(value - rounded) < 1e-9) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(rounded));
        return buf;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6f", value);
    std::string s(buf);
    // Trim trailing zeros, then a trailing '.'.
    size_t last_non_zero = s.find_last_not_of('0');
    if (last_non_zero != std::string::npos) s.erase(last_non_zero + 1);
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s.empty() ? "0" : s;
}

std::string TeleportPoint::category() const {
    auto dash = description.find('-');
    if (dash == std::string::npos) return OTHER_CATEGORY;
    return description.substr(0, dash);
}

std::vector<Category> points_to_categories(const std::vector<TeleportPoint>& points) {
    std::vector<std::string> order;
    std::unordered_map<std::string, size_t> index;
    std::vector<Category> result;
    for (const auto& p : points) {
        std::string cat = p.category();
        auto it = index.find(cat);
        if (it == index.end()) {
            index[cat] = result.size();
            result.push_back(Category{cat, {}});
        }
        result[index[cat]].points.push_back(p);
    }
    return result;
}

std::vector<Modifier> HotkeyBinding::modifiers() const {
    std::vector<Modifier> mods;
    for (char c : raw_combo) {
        if (c == '+') mods.push_back(Modifier::Shift);
        else if (c == '^') mods.push_back(Modifier::Ctrl);
        else if (c == '!') mods.push_back(Modifier::Alt);
        else break;
    }
    return mods;
}

std::string HotkeyBinding::key() const {
    size_t i = 0;
    while (i < raw_combo.size() &&
           (raw_combo[i] == '+' || raw_combo[i] == '^' || raw_combo[i] == '!')) {
        ++i;
    }
    return raw_combo.substr(i);
}

} // namespace th
