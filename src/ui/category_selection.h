#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace th {
class CategorySelection {
public:
    const std::vector<std::string>& items() const { return items_; }
    bool contains(const std::string& item) const;
    void clear();
    void retain(const std::vector<std::string>& visible);
    void click(const std::vector<std::string>& visible, std::size_t index, bool ctrl, bool shift);
private:
    std::vector<std::string> items_;
    std::string anchor_;
    bool has_anchor_ = false;
};
}
