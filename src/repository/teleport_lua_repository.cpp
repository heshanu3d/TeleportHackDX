#include "repository/teleport_lua_repository.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace th {

namespace {

constexpr char kBom[] = "\xEF\xBB\xBF";

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

// Decodes a Lua double-quoted string literal starting at `text[i]` (which
// must be '"'). Advances `i` to just past the closing quote. Returns
// false if the string is unterminated. Mirrors gen_fav.py's
// double_quote_lua(), which only ever escapes backslash and double-quote.
bool parse_lua_string(const std::string& text, size_t& i, std::string& decoded) {
    if (i >= text.size() || text[i] != '"') return false;
    ++i;
    decoded.clear();
    while (i < text.size() && text[i] != '"') {
        if (text[i] == '\\' && i + 1 < text.size()) {
            decoded += text[i + 1];
            i += 2;
        } else {
            decoded += text[i];
            ++i;
        }
    }
    if (i >= text.size()) return false; // unterminated string
    ++i; // skip closing quote
    return true;
}

// Splits `text` on top-level commas (there's no nesting to worry about
// once we're past the name -- the remaining fields are all plain
// numbers/mapId).
std::vector<std::string> split_commas(const std::string& text) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == ',') {
            parts.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    parts.push_back(text.substr(start));
    return parts;
}

// Attempts to parse one FAV table row line: `<indent>{"name", mapId, x, y,
// z, o}<trailing comma/whitespace>`. On success, fills in every piece
// needed to both compare the name and reconstruct the line with new
// x/y/z/o values while leaving the name and mapId tokens byte-for-byte
// untouched.
struct ParsedRow {
    std::string indent;
    std::string quoted_name; // raw, still-quoted substring (verbatim)
    std::string name;        // decoded, for comparison
    std::string map_id_token;
    std::string x_token, y_token, z_token, o_token;
    std::string trailing; // whatever follows the closing '}' (usually ",")
};

bool parse_row(const std::string& line, ParsedRow& out) {
    size_t i = 0;
    size_t n = line.size();
    while (i < n && (line[i] == ' ' || line[i] == '\t')) ++i;
    out.indent = line.substr(0, i);
    if (i >= n || line[i] != '{') return false;
    ++i;
    while (i < n && (line[i] == ' ' || line[i] == '\t')) ++i;

    size_t name_start = i;
    if (!parse_lua_string(line, i, out.name)) return false;
    out.quoted_name = line.substr(name_start, i - name_start);

    size_t close_brace = line.rfind('}');
    if (close_brace == std::string::npos || close_brace < i) return false;

    std::string middle = line.substr(i, close_brace - i);
    // `middle` looks like: ", 0, -9023.236, 465.42, 94.621, 0" -- drop the
    // leading comma (the one right after the name) before splitting the
    // remaining numeric fields.
    size_t first_comma = middle.find(',');
    if (first_comma == std::string::npos) return false;
    std::vector<std::string> fields = split_commas(middle.substr(first_comma + 1));
    if (fields.size() != 5) return false;

    out.map_id_token = trim(fields[0]);
    out.x_token = trim(fields[1]);
    out.y_token = trim(fields[2]);
    out.z_token = trim(fields[3]);
    out.o_token = trim(fields[4]);
    out.trailing = line.substr(close_brace + 1);
    return true;
}

std::string rebuild_row(const ParsedRow& row, const std::string& new_x, const std::string& new_y,
                         const std::string& new_z, const std::string& new_o) {
    std::ostringstream oss;
    oss << row.indent << "{" << row.quoted_name << ", " << row.map_id_token << ", " << new_x
        << ", " << new_y << ", " << new_z << ", " << new_o << "}" << row.trailing;
    return oss.str();
}

} // namespace

bool TeleportLuaRepository::update_entry(const std::string& description, const Position& pos,
                                          std::string& error) const {
    if (!std::filesystem::exists(path_)) {
        error = path_ + " does not exist";
        return false;
    }
    std::ifstream fh(path_, std::ios::binary);
    if (!fh) {
        error = "cannot open " + path_ + " for reading";
        return false;
    }
    std::ostringstream ss;
    ss << fh.rdbuf();
    std::string content = ss.str();

    bool had_bom = content.rfind(kBom, 0) == 0;
    if (had_bom) content.erase(0, 3);

    bool use_crlf = content.find("\r\n") != std::string::npos;
    content.erase(std::remove(content.begin(), content.end(), '\r'), content.end());

    std::vector<std::string> lines;
    {
        size_t start = 0;
        for (size_t i = 0; i <= content.size(); ++i) {
            if (i == content.size() || content[i] == '\n') {
                lines.push_back(content.substr(start, i - start));
                start = i + 1;
            }
        }
    }

    // Locate the `local FAV = { ... }` table (see gen_fav.py's
    // build_fav_lines(): the opening line always starts with this exact
    // prefix, and the table is closed by a line that is just "}").
    const std::string kFavStart = "local FAV = {";
    size_t fav_begin = std::string::npos, fav_end = std::string::npos;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].rfind(kFavStart, 0) == 0) {
            fav_begin = i;
            break;
        }
    }
    if (fav_begin == std::string::npos) {
        error = "could not find 'local FAV = {' table in " + path_ +
                " (has this file been through gen_fav.py?)";
        return false;
    }
    for (size_t i = fav_begin + 1; i < lines.size(); ++i) {
        if (trim(lines[i]) == "}") {
            fav_end = i;
            break;
        }
    }
    if (fav_end == std::string::npos) {
        error = "found 'local FAV = {' but no closing '}' in " + path_;
        return false;
    }

    for (size_t i = fav_begin + 1; i < fav_end; ++i) {
        ParsedRow row;
        if (!parse_row(lines[i], row)) continue;
        if (row.name != description) continue;

        std::string new_o = pos.orientation.has_value() ? format_float(*pos.orientation)
                                                          : row.o_token;
        lines[i] = rebuild_row(row, format_float(pos.x), format_float(pos.y),
                                format_float(pos.z), new_o);

        std::string sep = use_crlf ? "\r\n" : "\n";
        std::string out;
        for (size_t j = 0; j < lines.size(); ++j) {
            if (j) out += sep;
            out += lines[j];
        }
        if (had_bom) out = kBom + out;

        std::ofstream out_fh(path_, std::ios::binary | std::ios::trunc);
        if (!out_fh) {
            error = "cannot open " + path_ + " for writing";
            return false;
        }
        out_fh << out;
        return true;
    }

    error = "no FAV entry named '" + description + "' found in " + path_ +
            " (run gen_fav.py to sync it from favlist.fav first?)";
    return false;
}

} // namespace th
