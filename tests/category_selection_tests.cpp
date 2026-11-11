#include <iostream>
#include <string>
#include <vector>
#include "ui/category_selection.h"
#include "domain/category_groups.h"

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };
    const std::vector<std::string> visible{"A", "B", "C", "D", "E"};
    th::CategorySelection selection;
    selection.click(visible, 1, false, false);
    selection.click(visible, 3, true, false);
    check(selection.items() == std::vector<std::string>({"B", "D"}), "ctrl adds");
    selection.click(visible, 1, true, false);
    check(selection.items() == std::vector<std::string>({"D"}), "ctrl toggles");
    selection.click(visible, 4, false, false);
    selection.click(visible, 1, false, true);
    check(selection.items() == std::vector<std::string>({"B", "C", "D", "E"}), "reverse shift");
    selection.click(visible, 0, true, true);
    check(selection.items().size() == 5 && selection.contains("A"), "ctrl shift merges");
    selection.click(visible, 0, false, false);
    selection.click(visible, 2, false, true);
    check(selection.items() == std::vector<std::string>({"A", "B", "C"}), "forward shift");
    selection.retain({"B", "C", "D"});
    selection.click({"B", "C", "D"}, 2, false, true);
    check(selection.items() == std::vector<std::string>({"D"}), "missing anchor resets selection");
    selection.clear();
    check(selection.items().empty(), "clear selection");

    std::vector<th::CategoryGroup> groups{{"source", {"A", "hidden", "B", "C", "D", "E"}}, {"target", {"X"}}};
    const auto original = groups[0].categories;
    check(!th::move_categories_between_groups(groups, 0, 1, {"A", "missing"}) && groups[0].categories == original && groups[1].categories == std::vector<std::string>({"X"}), "invalid move atomic");
    check(th::reorder_categories(groups, 0, {"B", "D"}, -1, visible) && groups[0].categories == std::vector<std::string>({"B", "hidden", "A", "D", "C", "E"}), "nonadjacent move up preserves hidden");
    check(th::reorder_categories(groups, 0, {"B", "D"}, 1, visible) && groups[0].categories == original, "nonadjacent move down");
    check(!th::reorder_categories(groups, 0, {"hidden"}, 1, visible) && groups[0].categories == original, "invisible selection rejected");
    check(!th::reorder_categories(groups, 0, {"A"}, -1, visible), "boundary no change");
    check(th::move_categories_between_groups(groups, 0, 1, {"D", "B"}) && groups[0].categories == std::vector<std::string>({"A", "hidden", "C", "E"}) && groups[1].categories == std::vector<std::string>({"X", "B", "D"}), "bulk move follows source order");
    return failures ? 1 : 0;
}
