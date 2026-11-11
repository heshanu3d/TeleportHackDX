#include "services/search_history.h"

#include <algorithm>
#include "repository/settings_repository.h"

namespace th {
bool valid_search_query(const std::string& value) {
    if (value.size() > kMaxSearchBytes) return false;
    for (unsigned char ch : value) if (ch < 0x20 || ch == 0x7f) return false;
    return true;
}

std::string search_for_category(const Settings& settings, const std::string& category) {
    if (!settings.independent_search) return settings.global_search;
    const auto it = settings.category_searches.find(category);
    return it == settings.category_searches.end() ? std::string() : it->second;
}

bool set_search_for_category(Settings& settings, const std::string& category, const std::string& value) {
    if (!valid_search_query(value)) return false;
    if (!settings.independent_search) {
        if (settings.global_search == value) return false;
        settings.global_search = value;
        return true;
    }
    const auto it = settings.category_searches.find(category);
    if (value.empty()) {
        if (it == settings.category_searches.end()) return false;
        settings.category_searches.erase(it);
        return true;
    }
    if (it != settings.category_searches.end() && it->second == value) return false;
    settings.category_searches[category] = value;
    return true;
}

bool valid_search_history_entry(const std::string& value) {
    if (value.empty() || !valid_search_query(value)) return false;
    bool non_space = false;
    for (unsigned char ch : value) {
        if (ch != ' ') non_space = true;
    }
    return non_space;
}

bool remember_search(std::vector<std::string>& history, const std::string& value) {
    if (!valid_search_history_entry(value)) return false;
    const auto it = std::find(history.begin(), history.end(), value);
    if (it != history.end() && it == history.begin()) return false;
    if (it != history.end()) history.erase(it);
    history.insert(history.begin(), value);
    return true;
}

bool edit_search_history(std::vector<std::string>& history, std::size_t index,
                         const std::string& value, std::string& error) {
    if (index >= history.size()) { error = "no history entry selected"; return false; }
    if (!valid_search_history_entry(value)) { error = "history entry is empty or invalid"; return false; }
    const auto it = std::find(history.begin(), history.end(), value);
    if (it != history.end() && static_cast<std::size_t>(it - history.begin()) != index) {
        error = "history entry already exists";
        return false;
    }
    history[index] = value;
    return true;
}

bool erase_search_history(std::vector<std::string>& history, std::size_t index) {
    if (index >= history.size()) return false;
    history.erase(history.begin() + index);
    return true;
}
}
