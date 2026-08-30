#include "list.h"
#include "workspace_config.h"

#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace {
int failures = 0;

void check(bool condition, std::string_view message) {
    if (condition)
        return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

std::vector<int> values(const List<int>& list) {
    std::vector<int> result;
    for (auto* node = list.first(); node != nullptr; node = node->next())
        result.push_back(node->data());
    return result;
}

void test_list_ordering() {
    List<int> list;
    list.push_back(2);
    auto* two = list.first();
    auto* one = list.emplace_before(two, 1);
    auto* three = list.emplace_after(two, 3);

    check(values(list) == std::vector<int>({1, 2, 3}), "list inserts preserve order");
    check(list.size() == 3, "list tracks inserted size");

    list.move_after(three, one);
    check(values(list) == std::vector<int>({2, 3, 1}), "list move_after relinks a node");
    list.move_before(list.first(), one);
    check(values(list) == std::vector<int>({1, 2, 3}), "list move_before relinks a node");

    auto* active = two;
    auto* previous = active->prev();
    list.swap(active, previous);
    check(values(list) == std::vector<int>({2, 1, 3}), "list swap moves payload order");
    check(active->data() == 2, "list swap keeps the active payload selected");

    list.erase(list.first());
    list.pop_back();
    check(values(list) == std::vector<int>({1}), "list erase and pop update links");
    list.clear();
    check(list.empty() && list.first() == nullptr && list.last() == nullptr, "list clear resets invariants");
}

void test_list_ownership_and_moves() {
    List<int> list;
    list.push_back(2);
    list.emplace_after(nullptr, 3);
    list.emplace_before(nullptr, 1);
    check(values(list) == std::vector<int>({1, 2, 3}), "null insertion points use list boundaries");

    List<int> moved(std::move(list));
    check(list.empty(), "moving a list clears the source");
    check(values(moved) == std::vector<int>({1, 2, 3}), "moving a list preserves links and payloads");

    List<std::unique_ptr<int>> owned;
    owned.push_back(std::make_unique<int>(42));
    check(owned.size() == 1 && *owned.first()->data() == 42, "list supports move-only ownership");
    owned.erase(owned.first());
    check(owned.empty(), "erasing a move-only payload releases its node");
}

void test_workspace_filter() {
    WorkspaceConfigCache config;
    config.update_focus_filter(" 1, web, special:scratchpad, , 5 ");
    check(!config.focus_layout_enabled("chat", "1"), "workspace filter matches numeric ids");
    check(!config.focus_layout_enabled("web", "2"), "workspace filter matches names");
    check(!config.focus_layout_enabled("special:scratchpad", "-99"), "workspace filter matches special names");
    check(config.focus_layout_enabled("code", "4"), "workspace filter leaves other workspaces enabled");

    config.update_focus_filter("code");
    check(config.focus_layout_enabled("web", "2"), "workspace filter invalidates old entries");
    check(!config.focus_layout_enabled("code", "4"), "workspace filter applies updated entries");
}

void test_workspace_padding() {
    WorkspaceConfigCache config;
    config.update_padding(
        "special:social:50:25, special:terminal:50:30:40:20, 3:7:9, special:team:room:11:13");

    check(config.padding_for("special:social", "-1") == Padding{50, 25, 50, 25}, "two-value padding follows CSS order");
    check(config.padding_for("special:terminal", "-2") == Padding{50, 30, 40, 20}, "four-value padding follows CSS order");
    check(config.padding_for("workspace-three", "3") == Padding{7, 9, 7, 9}, "padding falls back to workspace ids");
    check(config.padding_for("special:team:room", "-3") == Padding{11, 13, 11, 13}, "padding supports names with multiple colons");
    check(config.padding_for("missing", "9") == Padding{}, "missing padding returns zero edges");

    config.update_padding("bad:10px:20, valid:+4:-2");
    check(config.padding_for("bad", "1") == Padding{}, "padding rejects partially numeric values");
    check(config.padding_for("valid", "2") == Padding{4, -2, 4, -2}, "padding accepts signed integers");
}
} // namespace

int main() {
    test_list_ordering();
    test_list_ownership_and_moves();
    test_workspace_filter();
    test_workspace_padding();

    if (failures != 0)
        std::cerr << failures << " test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
