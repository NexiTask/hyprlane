#include "workspace_config.h"

#include <charconv>
#include <system_error>
#include <utility>

namespace {
std::string_view trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\n\r");
    if (first == std::string_view::npos)
        return {};
    const auto last = value.find_last_not_of(" \t\n\r");
    return value.substr(first, last - first + 1);
}

std::vector<std::string_view> split(std::string_view value, char delimiter) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find(delimiter, start);
        if (end == std::string_view::npos) {
            parts.push_back(value.substr(start));
            break;
        }
        parts.push_back(value.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

bool parse_integer(std::string_view text, int& result) {
    text = trim(text);
    if (text.empty())
        return false;

    if (text.front() == '+') {
        text.remove_prefix(1);
        if (text.empty())
            return false;
    }

    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    return error == std::errc{} && end == text.data() + text.size();
}

std::string join_workspace_name(const std::vector<std::string_view>& parts, std::size_t count) {
    std::string name;
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0)
            name.push_back(':');
        name.append(trim(parts[index]));
    }
    return std::string(trim(name));
}

bool parse_four_value_padding(const std::vector<std::string_view>& parts, std::string& workspace, Padding& padding) {
    if (parts.size() < 5)
        return false;

    const auto value_start = parts.size() - 4;
    int top = 0;
    int right = 0;
    int bottom = 0;
    int left = 0;
    if (!parse_integer(parts[value_start], top) ||
        !parse_integer(parts[value_start + 1], right) ||
        !parse_integer(parts[value_start + 2], bottom) ||
        !parse_integer(parts[value_start + 3], left)) {
        return false;
    }

    workspace = join_workspace_name(parts, value_start);
    padding = {top, right, bottom, left};
    return !workspace.empty();
}

bool parse_two_value_padding(const std::vector<std::string_view>& parts, std::string& workspace, Padding& padding) {
    if (parts.size() < 3)
        return false;

    const auto value_start = parts.size() - 2;
    int vertical = 0;
    int horizontal = 0;
    if (!parse_integer(parts[value_start], vertical) || !parse_integer(parts[value_start + 1], horizontal))
        return false;

    workspace = join_workspace_name(parts, value_start);
    padding = Padding::from_vertical_horizontal(vertical, horizontal);
    return !workspace.empty();
}
} // namespace

void WorkspaceConfigCache::update_focus_filter(std::string_view config) {
    if (config == focus_filter_config_)
        return;

    focus_filter_config_ = config;
    disabled_workspaces_.clear();
    for (const auto item : split(config, ',')) {
        const auto workspace = trim(item);
        if (!workspace.empty())
            disabled_workspaces_.emplace_back(workspace);
    }
}

void WorkspaceConfigCache::update_padding(std::string_view config) {
    if (config == padding_config_)
        return;

    padding_config_ = config;
    padding_by_workspace_.clear();
    for (const auto item : split(config, ',')) {
        const auto entry = trim(item);
        if (entry.empty())
            continue;

        const auto parts = split(entry, ':');
        std::string workspace;
        Padding padding;
        if (parse_four_value_padding(parts, workspace, padding) || parse_two_value_padding(parts, workspace, padding))
            padding_by_workspace_[std::move(workspace)] = padding;
    }
}

bool WorkspaceConfigCache::focus_layout_enabled(std::string_view workspace_name, std::string_view workspace_id) const {
    for (const auto& disabled_workspace : disabled_workspaces_) {
        if (disabled_workspace == workspace_name || disabled_workspace == workspace_id)
            return false;
    }
    return true;
}

Padding WorkspaceConfigCache::padding_for(std::string_view workspace_name, std::string_view workspace_id) const {
    if (const auto by_name = padding_by_workspace_.find(std::string(workspace_name)); by_name != padding_by_workspace_.end())
        return by_name->second;
    if (const auto by_id = padding_by_workspace_.find(std::string(workspace_id)); by_id != padding_by_workspace_.end())
        return by_id->second;
    return {};
}
