#pragma once
#include <array>

namespace th {
// Called inside the favourites table before its first row.
void setup_favourite_columns(const std::array<bool, 5>& show_columns);
}
