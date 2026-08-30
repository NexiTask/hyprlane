#ifndef SCROLLER_WORKSPACE_CONFIG_H
#define SCROLLER_WORKSPACE_CONFIG_H

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct Padding {
    int top = 0;
    int right = 0;
    int bottom = 0;
    int left = 0;

    [[nodiscard]] static Padding from_vertical_horizontal(int vertical, int horizontal) {
        return {vertical, horizontal, vertical, horizontal};
    }

    friend bool operator==(const Padding&, const Padding&) = default;
};

class WorkspaceConfigCache {
public:
    void update_focus_filter(std::string_view config);
    void update_padding(std::string_view config);

    [[nodiscard]] bool focus_layout_enabled(std::string_view workspace_name, std::string_view workspace_id) const;
    [[nodiscard]] Padding padding_for(std::string_view workspace_name, std::string_view workspace_id) const;

private:
    std::string focus_filter_config_;
    std::vector<std::string> disabled_workspaces_;
    std::string padding_config_;
    std::unordered_map<std::string, Padding> padding_by_workspace_;
};

#endif // SCROLLER_WORKSPACE_CONFIG_H
