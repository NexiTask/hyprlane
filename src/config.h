#ifndef SCROLLER_CONFIG_H
#define SCROLLER_CONFIG_H

#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <string>

class ScrollerConfig {
public:
    ScrollerConfig();

    [[nodiscard]] bool register_values(HANDLE handle) const;

    [[nodiscard]] std::string column_default_width() const { return column_default_width_->value(); }
    [[nodiscard]] std::string window_default_height() const { return window_default_height_->value(); }
    [[nodiscard]] bool focus_wrap() const { return focus_wrap_->value() != 0; }
    [[nodiscard]] bool movefocus_changes_workspace() const { return movefocus_changes_workspace_->value() != 0; }
    [[nodiscard]] Config::INTEGER focus_edge_ms() const { return focus_edge_ms_->value(); }
    [[nodiscard]] bool cyclesize_wrap() const { return cyclesize_wrap_->value() != 0; }
    [[nodiscard]] bool cyclesize_closest() const { return cyclesize_closest_->value() != 0; }
    [[nodiscard]] bool center_row_if_space_available() const { return center_row_if_space_available_->value() != 0; }
    [[nodiscard]] bool center_active_window() const { return center_active_window_->value() != 0; }
    [[nodiscard]] bool center_active_column() const { return center_active_column_->value() != 0; }
    [[nodiscard]] bool overview_scale_content() const { return overview_scale_content_->value() != 0; }
    [[nodiscard]] bool overview_render_layers() const { return overview_render_layers_->value() != 0; }
    [[nodiscard]] std::string column_widths() const { return column_widths_->value(); }
    [[nodiscard]] std::string window_heights() const { return window_heights_->value(); }
    [[nodiscard]] std::string monitor_options() const { return monitor_options_->value(); }
    [[nodiscard]] Config::FLOAT gesture_sensitivity() const { return gesture_sensitivity_->value(); }
    [[nodiscard]] bool gesture_overview_enable() const { return gesture_overview_enable_->value() != 0; }
    [[nodiscard]] Config::INTEGER gesture_overview_distance() const { return gesture_overview_distance_->value(); }
    [[nodiscard]] Config::INTEGER gesture_overview_fingers() const { return gesture_overview_fingers_->value(); }
    [[nodiscard]] bool gesture_scroll_enable() const { return gesture_scroll_enable_->value() != 0; }
    [[nodiscard]] Config::INTEGER gesture_scroll_fingers() const { return gesture_scroll_fingers_->value(); }
    [[nodiscard]] bool gesture_workspace_switch_enable() const { return gesture_workspace_switch_enable_->value() != 0; }
    [[nodiscard]] Config::INTEGER gesture_workspace_switch_distance() const { return gesture_workspace_switch_distance_->value(); }
    [[nodiscard]] Config::INTEGER gesture_workspace_switch_fingers() const { return gesture_workspace_switch_fingers_->value(); }
    [[nodiscard]] std::string gesture_workspace_switch_prefix() const { return gesture_workspace_switch_prefix_->value(); }
    [[nodiscard]] Config::INTEGER selection_border() const { return selection_border_->value(); }
    [[nodiscard]] std::string jump_labels_font() const { return jump_labels_font_->value(); }
    [[nodiscard]] Config::FLOAT jump_labels_scale() const { return jump_labels_scale_->value(); }
    [[nodiscard]] Config::INTEGER jump_labels_color() const { return jump_labels_color_->value(); }
    [[nodiscard]] std::string jump_labels_keys() const { return jump_labels_keys_->value(); }
    [[nodiscard]] bool avoid_focus_on_float_close() const { return avoid_focus_on_float_close_->value() != 0; }
    [[nodiscard]] bool avoid_focus_on_xwayland_float_close() const { return avoid_focus_on_xwayland_float_close_->value() != 0; }
    [[nodiscard]] bool focus_layout_enable() const { return focus_layout_enable_->value() != 0; }
    [[nodiscard]] bool focus_layout_mouse_disable() const { return focus_layout_mouse_disable_->value() != 0; }
    [[nodiscard]] std::string focus_layout_disable_workspaces() const { return focus_layout_disable_workspaces_->value(); }
    [[nodiscard]] bool focus_layout_center_active() const { return focus_layout_center_active_->value() != 0; }
    [[nodiscard]] std::string workspace_padding() const { return workspace_padding_->value(); }
    [[nodiscard]] bool clip_window_order_enforce() const { return clip_window_order_enforce_->value() != 0; }

private:
    using IntValue = Config::Values::Int;
    using FloatValue = Config::Values::Float;
    using StringValue = Config::Values::String;

    SP<StringValue> column_default_width_;
    SP<StringValue> window_default_height_;
    SP<IntValue> focus_wrap_;
    SP<IntValue> movefocus_changes_workspace_;
    SP<IntValue> focus_edge_ms_;
    SP<IntValue> cyclesize_wrap_;
    SP<IntValue> cyclesize_closest_;
    SP<IntValue> center_row_if_space_available_;
    SP<IntValue> center_active_window_;
    SP<IntValue> center_active_column_;
    SP<IntValue> overview_scale_content_;
    SP<IntValue> overview_render_layers_;
    SP<StringValue> column_widths_;
    SP<StringValue> window_heights_;
    SP<StringValue> monitor_options_;
    SP<FloatValue> gesture_sensitivity_;
    SP<IntValue> gesture_overview_enable_;
    SP<IntValue> gesture_overview_distance_;
    SP<IntValue> gesture_overview_fingers_;
    SP<IntValue> gesture_scroll_enable_;
    SP<IntValue> gesture_scroll_fingers_;
    SP<IntValue> gesture_workspace_switch_enable_;
    SP<IntValue> gesture_workspace_switch_distance_;
    SP<IntValue> gesture_workspace_switch_fingers_;
    SP<StringValue> gesture_workspace_switch_prefix_;
    SP<IntValue> selection_border_;
    SP<StringValue> jump_labels_font_;
    SP<FloatValue> jump_labels_scale_;
    SP<IntValue> jump_labels_color_;
    SP<StringValue> jump_labels_keys_;
    SP<IntValue> avoid_focus_on_float_close_;
    SP<IntValue> avoid_focus_on_xwayland_float_close_;
    SP<IntValue> focus_layout_enable_;
    SP<IntValue> focus_layout_mouse_disable_;
    SP<StringValue> focus_layout_disable_workspaces_;
    SP<IntValue> focus_layout_center_active_;
    SP<StringValue> workspace_padding_;
    SP<IntValue> clip_window_order_enforce_;
};

extern ScrollerConfig g_scrollerConfig;

#endif // SCROLLER_CONFIG_H
