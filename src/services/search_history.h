#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace th {
struct Settings;
constexpr std::size_t kMaxSearchBytes = 127;
bool valid_search_query(const std::string& value);
std::string search_for_category(const Settings& settings, const std::string& category);
bool set_search_for_category(Settings& settings, const std::string& category, const std::string& value);
bool valid_search_history_entry(const std::string& value);
bool remember_search(std::vector<std::string>& history, const std::string& value);
bool edit_search_history(std::vector<std::string>& history, std::size_t index,
                         const std::string& value, std::string& error);
bool erase_search_history(std::vector<std::string>& history, std::size_t index);
}
