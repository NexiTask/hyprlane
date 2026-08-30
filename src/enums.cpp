#include <hyprutils/string/VarList.hpp>

#include <algorithm>
#include <charconv>

#include "config.h"
#include "enums.h"

using Hyprutils::String::CVarList;

ModeModifier::ModeModifier() = default;

ModeModifier::ModeModifier(const std::string &arg) : ModeModifier() {
    const auto args = CVarList(arg);
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "after")
            set_position(ModeModifier::POSITION_AFTER);
        else if (args[i] == "before")
            set_position(ModeModifier::POSITION_BEFORE);
        else if (args[i] == "end")
            set_position(ModeModifier::POSITION_END);
        else if (args[i] == "beginning" || args[i] == "beg")
            set_position(ModeModifier::POSITION_BEGINNING);

        if (args[i] == "focus")
            set_focus(ModeModifier::FOCUS_FOCUS);
        else if (args[i] == "nofocus")
            set_focus(ModeModifier::FOCUS_NOFOCUS);

        if (args[i].starts_with("auto:")) {
            const auto value = std::string_view(args[i]).substr(5);
            int parameter = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parameter);
            if (error == std::errc{} && end == value.data() + value.size()) {
                set_auto_mode(ModeModifier::AUTO_AUTO);
                set_auto_param(parameter);
            }
        } else if (args[i] == "manual")
            set_auto_mode(ModeModifier::AUTO_MANUAL);

        if (args[i] == "center_column")
            set_center_column(true);
        else if (args[i] == "nocenter_column")
            set_center_column(false);
        if (args[i] == "center_window")
            set_center_window(true);
        else if (args[i] == "nocenter_window")
            set_center_window(false);
    }
}

void ModeModifier::set_position(int p) {
    position = p;
}

int ModeModifier::get_position(bool force_default) const {
    if (force_default && position == POSITION_UNDEFINED)
        return POSITION_AFTER;
    return position;
}

std::string ModeModifier::get_position_string() const {
    auto pos = get_position();
    switch (pos) {
    case POSITION_AFTER:
    default:
        return "after";
    case POSITION_BEFORE:
        return "before";
    case POSITION_END:
        return "end";
    case POSITION_BEGINNING:
        return "beginning";
    }
}

void ModeModifier::set_focus(int f) {
    focus = f;
}

int ModeModifier::get_focus(bool force_default) const {
    if (force_default && focus == FOCUS_UNDEFINED)
        return FOCUS_FOCUS;
    return focus;
}

std::string ModeModifier::get_focus_string() const {
    if (get_focus() == FOCUS_FOCUS)
        return "focus";
    return "nofocus";
}

void ModeModifier::set_auto_mode(int mode) {
    auto_mode = mode;
}

void ModeModifier::set_auto_param(int param) {
    auto_param = std::max(param, 1);
}

int ModeModifier::get_auto_mode(bool force_default) const {
    if (force_default && auto_mode == AUTO_UNDEFINED)
        return AUTO_MANUAL;
    return auto_mode;
}

std::string ModeModifier::get_auto_mode_string() const {
    if (get_auto_mode() == AUTO_MANUAL)
        return "manual";
    return "auto";
}

int ModeModifier::get_auto_param() const {
    return auto_param;
}

void ModeModifier::set_center_column(bool c) {
    center_column = c;
}

bool ModeModifier::center_column_enabled() const {
    return center_column.value_or(g_scrollerConfig.center_active_column());
}

std::optional<bool> ModeModifier::center_column_override() const {
    return center_column;
}

std::string ModeModifier::get_center_column_string() const {
    if (!center_column_enabled())
        return "nocenter_column";
    return "center_column";
}

void ModeModifier::set_center_window(bool c) {
    center_window = c;
}

bool ModeModifier::center_window_enabled() const {
    return center_window.value_or(g_scrollerConfig.center_active_window());
}

std::optional<bool> ModeModifier::center_window_override() const {
    return center_window;
}

std::string ModeModifier::get_center_window_string() const {
    if (!center_window_enabled())
        return "nocenter_window";
    return "center_window";
}
