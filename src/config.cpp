#include "config.h"

#include <vector>

namespace {
using Config::Values::Float;
using Config::Values::Int;
using Config::Values::String;

SP<Int> make_toggle(const char* name, const char* description, bool default_value) {
    return Config::Values::makeConfigValue<Int>(
        name,
        description,
        Config::INTEGER{default_value ? 1 : 0},
        {.min = Config::INTEGER{0}, .max = Config::INTEGER{1}});
}

SP<Int> make_non_negative_int(const char* name, const char* description, Config::INTEGER default_value) {
    return Config::Values::makeConfigValue<Int>(
        name,
        description,
        default_value,
        {.min = Config::INTEGER{0}});
}
} // namespace

ScrollerConfig g_scrollerConfig;

ScrollerConfig::ScrollerConfig()
    : column_default_width_(Config::Values::makeConfigValue<String>(
          "plugin:scroller:column_default_width", "default width for new columns", "onehalf")),
      window_default_height_(Config::Values::makeConfigValue<String>(
          "plugin:scroller:window_default_height", "default height for new windows", "one")),
      focus_wrap_(make_toggle("plugin:scroller:focus_wrap", "wrap focus at layout edges", true)),
      movefocus_changes_workspace_(make_toggle(
          "plugin:scroller:movefocus_changes_workspace", "change workspace when vertical focus reaches an edge", false)),
      focus_edge_ms_(make_non_negative_int(
          "plugin:scroller:focus_edge_ms", "milliseconds before pointer edge focus activates", 400)),
      cyclesize_wrap_(make_toggle("plugin:scroller:cyclesize_wrap", "wrap size cycling", true)),
      cyclesize_closest_(make_toggle("plugin:scroller:cyclesize_closest", "cycle from the closest configured size", true)),
      center_row_if_space_available_(make_toggle(
          "plugin:scroller:center_row_if_space_available", "center a row when all columns fit", false)),
      center_active_window_(make_toggle("plugin:scroller:center_active_window", "center the active window vertically", false)),
      center_active_column_(make_toggle("plugin:scroller:center_active_column", "center the active column horizontally", false)),
      overview_scale_content_(make_toggle("plugin:scroller:overview_scale_content", "scale window contents in overview", true)),
      overview_render_layers_(make_toggle("plugin:scroller:overview_render_layers", "render layer surfaces in overview", true)),
      column_widths_(Config::Values::makeConfigValue<String>(
          "plugin:scroller:column_widths", "ordered standard widths used while cycling columns", "onethird onehalf twothirds one")),
      window_heights_(Config::Values::makeConfigValue<String>(
          "plugin:scroller:window_heights", "ordered standard heights used while cycling windows", "onethird onehalf twothirds one")),
      monitor_options_(Config::Values::makeConfigValue<String>(
          "plugin:scroller:monitor_options", "per-monitor scroller mode and size options", "")),
      gesture_sensitivity_(Config::Values::makeConfigValue<Float>(
          "plugin:scroller:gesture_sensitivity", "gesture movement multiplier", Config::FLOAT{1.0F})),
      gesture_overview_enable_(make_toggle("plugin:scroller:gesture_overview_enable", "enable overview gestures", true)),
      gesture_overview_distance_(make_non_negative_int(
          "plugin:scroller:gesture_overview_distance", "distance required to trigger overview gestures", 5)),
      gesture_overview_fingers_(make_non_negative_int(
          "plugin:scroller:gesture_overview_fingers", "finger count for overview gestures", 4)),
      gesture_scroll_enable_(make_toggle("plugin:scroller:gesture_scroll_enable", "enable layout scrolling gestures", true)),
      gesture_scroll_fingers_(make_non_negative_int(
          "plugin:scroller:gesture_scroll_fingers", "finger count for layout scrolling gestures", 3)),
      gesture_workspace_switch_enable_(make_toggle(
          "plugin:scroller:gesture_workspace_switch_enable", "enable workspace switching gestures", true)),
      gesture_workspace_switch_distance_(make_non_negative_int(
          "plugin:scroller:gesture_workspace_switch_distance", "distance required to trigger workspace switching", 5)),
      gesture_workspace_switch_fingers_(make_non_negative_int(
          "plugin:scroller:gesture_workspace_switch_fingers", "finger count for workspace switching gestures", 4)),
      gesture_workspace_switch_prefix_(Config::Values::makeConfigValue<String>(
          "plugin:scroller:gesture_workspace_switch_prefix", "prefix for gesture workspace dispatcher arguments", "")),
      selection_border_(Config::Values::makeConfigValue<Int>(
          "plugin:scroller:col.selection_border", "border color for selected windows", Config::INTEGER{0xff9e1515ULL})),
      jump_labels_font_(Config::Values::makeConfigValue<String>(
          "plugin:scroller:jump_labels_font", "font family used for jump labels", "")),
      jump_labels_scale_(Config::Values::makeConfigValue<Float>(
          "plugin:scroller:jump_labels_scale", "jump label size relative to the window", Config::FLOAT{0.5F})),
      jump_labels_color_(Config::Values::makeConfigValue<Int>(
          "plugin:scroller:jump_labels_color", "text color for jump labels", Config::INTEGER{0x80159e30ULL})),
      jump_labels_keys_(Config::Values::makeConfigValue<String>(
          "plugin:scroller:jump_labels_keys", "characters used to generate jump labels", "1234")),
      avoid_focus_on_float_close_(make_toggle(
          "plugin:scroller:avoid_focus_on_float_close", "do not restore tiled focus when a floating window closes", false)),
      avoid_focus_on_xwayland_float_close_(make_toggle(
          "plugin:scroller:avoid_focus_on_xwayland_float_close", "do not restore tiled focus when an XWayland floating window closes", false)),
      focus_layout_enable_(make_toggle("plugin:scroller:focus_layout_enable", "enable the Niri-style focus layout", false)),
      focus_layout_mouse_disable_(make_toggle(
          "plugin:scroller:focus_layout_mouse_disable", "ignore mouse-triggered focus changes in the focus layout", false)),
      focus_layout_disable_workspaces_(Config::Values::makeConfigValue<String>(
          "plugin:scroller:focus_layout_disable_workspaces", "comma-separated workspaces excluded from the focus layout", "")),
      focus_layout_center_active_(make_toggle(
          "plugin:scroller:focus_layout_center_active", "keep the active focus-layout column centered", true)),
      workspace_padding_(Config::Values::makeConfigValue<String>(
          "plugin:scroller:workspace_padding", "workspace-specific CSS-style padding", "")),
      clip_window_order_enforce_(make_toggle(
          "plugin:scroller:clip_window_order_enforce", "keep clipped inactive windows to the right of normal windows", true)) {}

bool ScrollerConfig::register_values(HANDLE handle) const {
    const std::vector<SP<Config::Values::IValue>> values = {
        column_default_width_,
        window_default_height_,
        focus_wrap_,
        movefocus_changes_workspace_,
        focus_edge_ms_,
        cyclesize_wrap_,
        cyclesize_closest_,
        center_row_if_space_available_,
        center_active_window_,
        center_active_column_,
        overview_scale_content_,
        overview_render_layers_,
        column_widths_,
        window_heights_,
        monitor_options_,
        gesture_sensitivity_,
        gesture_overview_enable_,
        gesture_overview_distance_,
        gesture_overview_fingers_,
        gesture_scroll_enable_,
        gesture_scroll_fingers_,
        gesture_workspace_switch_enable_,
        gesture_workspace_switch_distance_,
        gesture_workspace_switch_fingers_,
        gesture_workspace_switch_prefix_,
        selection_border_,
        jump_labels_font_,
        jump_labels_scale_,
        jump_labels_color_,
        jump_labels_keys_,
        avoid_focus_on_float_close_,
        avoid_focus_on_xwayland_float_close_,
        focus_layout_enable_,
        focus_layout_mouse_disable_,
        focus_layout_disable_workspaces_,
        focus_layout_center_active_,
        workspace_padding_,
        clip_window_order_enforce_,
    };

    bool success = true;
    for (const auto& value : values)
        success = HyprlandAPI::addConfigValueV2(handle, value) && success;
    return success;
}
