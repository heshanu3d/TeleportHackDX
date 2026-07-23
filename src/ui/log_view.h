// Small ring-buffer log widget, replaces main_window.py's QPlainTextEdit
// log view (`_log_view` / `_log`).
#pragma once

#include <deque>
#include <string>

namespace th {

class LogView {
public:
    explicit LogView(size_t max_lines = 500) : max_lines_(max_lines) {}

    void add(const std::string& line) {
        lines_.push_back(line);
        if (lines_.size() > max_lines_) lines_.pop_front();
    }

    const std::deque<std::string>& lines() const { return lines_; }
    void clear() { lines_.clear(); }

private:
    size_t max_lines_;
    std::deque<std::string> lines_;
};

} // namespace th
