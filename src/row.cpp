#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/shared/monitor/MonitorRuleManager.hpp>
#include <hyprland/src/config/shared/workspace/WorkspaceRuleManager.hpp>
#include <hyprland/src/managers/EventManager.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/helpers/math/Direction.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <chrono>
#include <charconv>
#include <format>
#include <sstream>
#include <vector>
#include <cstdio>

#include "common.h"
#include "config.h"
#include "functions.h"
#include "row.h"
#include "overview.h"
#include "window_rule_effects.h"

extern std::unique_ptr<Overview> overviews;
extern std::function<SDispatchResult(std::string)> orig_moveFocusTo;
extern ScrollerSizes scroller_sizes;
extern bool suppressFocusLayout;
extern std::chrono::steady_clock::time_point lastNoCenterTime;

namespace {
WorkspaceConfigCache workspace_config_cache;
}

Row::Row(WORKSPACEID workspace, PHLMONITOR monitor)
    : workspace(workspace)
{
    post_event("overview");
    set_mode(scroller_sizes.get_mode(monitor));
    update_sizes(monitor);
}

Row::~Row()
{
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        if (col == pinned) {
            col->data()->pin(false);
        }
        delete col->data();
    }
    columns.clear();
}

void Row::find_auto_insert_point()
{
    auto auto_mode = modifier.get_auto_mode();
    if (auto_mode == ModeModifier::AUTO_AUTO) {
        const auto auto_param = static_cast<size_t>(std::max(modifier.get_auto_param(), 0));
        if (mode == Mode::Row) {
            if (active->data()->size() < auto_param) {
                mode = Mode::Column;
                return;
            }
            // Find another column with less than auto_param windows
            for (auto col = columns.first(); col != nullptr; col = col->next()) {
                if (col->data()->size() < auto_param) {
                    mode = Mode::Column;
                    active = col;
                    return;
                }
            }
        } else {
            // If there are less columns than auto_param, create a new one
            if (columns.size() < auto_param) {
                mode = Mode::Row;
                return;
            }
            // Create a new window in the active column only when all the other
            // columns have the same number of windows

            // Find the column with the highest number of windows
            auto node = columns.first();
            for (auto col = columns.first(); col != nullptr; col = col->next()) {
                if (col->data()->size() > node->data()->size())
                    node = col;
            }
            // Find a column with a lower number of windows than node, and insert
            // the window there
            for (auto col = columns.first(); col != nullptr; col = col->next()) {
                if (col->data()->size() < node->data()->size()) {
                    mode = Mode::Column;
                    active = col;
                    return;
                }
            }
            // All the columns have the same number of windows, and we have
            // the maximum allowed number of columns, add a window to the active
            // column
            return;
        }
    }
}

void Row::add_active_window(PHLWINDOW window)
{
    if (!window)
        return;

    bool overview_on = overview;
    if (overview)
        toggle_overview();

    eFullscreenMode fsmode;
    if (active != nullptr) {
        auto awindow = get_active_window();
        if (awindow) {
            fsmode = window_fullscreen_state(awindow);
            if (fsmode != eFullscreenMode::FSMODE_NONE) {
                toggle_window_fullscreen_internal(awindow, eFullscreenMode::FSMODE_NONE);
            }
        } else {
            fsmode = eFullscreenMode::FSMODE_NONE;
        }
    } else {
        fsmode = eFullscreenMode::FSMODE_NONE;
    }

    auto store_mode = mode;

    // Evaluate the plugin's dynamic window-rule effect.
    auto store_modifier = modifier;
    if (const auto rule_value = window_rule_effects::modemodifier(window)) {
        // params: row|column after|before|end|beginning focus|nofocus
        std::istringstream iss(*rule_value);
        std::string arg;
        while (iss >> arg) {
            if (arg == "row") {
                mode = Mode::Row;
            } else if (arg == "col" || arg == "column") {
                mode = Mode::Column;
            } else if (arg == "after") {
                modifier.set_position(ModeModifier::POSITION_AFTER);
            } else if (arg == "before") {
                modifier.set_position(ModeModifier::POSITION_BEFORE);
            } else if (arg == "end") {
                modifier.set_position(ModeModifier::POSITION_END);
            } else if (arg == "beg" || arg == "beginning") {
                modifier.set_position(ModeModifier::POSITION_BEGINNING);
            } else if (arg == "focus") {
                modifier.set_focus(ModeModifier::FOCUS_FOCUS);
            } else if (arg == "nofocus") {
                modifier.set_focus(ModeModifier::FOCUS_NOFOCUS);
            }
        }
    }

    auto store_active = active;
    find_auto_insert_point();

    // Grid mode: new windows create new columns in the current grid row
    if (grid_mode) {
        auto node = columns.emplace_after(active, new Column(window, this));
        // Assign the new window to the current grid row
        Window* win = node->data()->get_window(window);
        if (win) {
            win->set_grid_row(current_grid_row);
        }
        active = node;
        recalculate_grid_geometry();
        modifier = store_modifier;
        mode = store_mode;
        if (fsmode != eFullscreenMode::FSMODE_NONE) {
            toggle_window_fullscreen_internal(window, fsmode);
            force_focus_to_window(window);
        }
        return;
    }

    if (active && mode == Mode::Column) {
        active->data()->add_active_window(window);
        active->data()->recalculate_col_geometry(calculate_gap_x(active), gap, true);
        if (modifier.get_focus() == ModeModifier::FOCUS_NOFOCUS && store_active != nullptr)
            active = store_active;
    } else {
        auto focus = modifier.get_focus();
        auto node = active;

        // Check if clip window ordering is enforced
        const bool enforce_clip_order = g_scrollerConfig.clip_window_order_enforce();

        // Check if the new window has clip_when_inactive
        const bool new_window_is_clipped = window->m_ruleApplicator &&
            window->m_ruleApplicator->m_tagKeeper.isTagged("scroller:clip_when_inactive");

        // Enforce ordering: clipped windows must be to the right of non-clipped (if enabled)
        if (enforce_clip_order && new_window_is_clipped) {
            // Clipped window: always insert at the end
            node = columns.emplace_after(columns.last(), new Column(window, this));
            Log::logger->log(Log::DEBUG, "[CLIP] New clipped window inserted at end");
        } else if (enforce_clip_order && !new_window_is_clipped) {
            // Non-clipped window with enforcement: insert according to modifier, but before any clipped columns
            switch (modifier.get_position()) {
            case ModeModifier::POSITION_AFTER:
            default: {
                // Insert after active, but if active is clipped or next is clipped,
                // find the last non-clipped position instead
                auto insert_point = active;
                if (active && active->data()->has_clipped_window()) {
                    // Active is clipped, find position before first clipped
                    insert_point = nullptr;
                    for (auto col = columns.first(); col != nullptr; col = col->next()) {
                        if (col->data()->has_clipped_window()) {
                            if (col->prev())
                                insert_point = col->prev();
                            break;
                        }
                        insert_point = col;
                    }
                }
                if (insert_point)
                    node = columns.emplace_after(insert_point, new Column(window, this));
                else
                    node = columns.emplace_before(columns.first(), new Column(window, this));
                break;
            }
            case ModeModifier::POSITION_BEFORE: {
                auto insert_point = active;
                if (active && active->data()->has_clipped_window()) {
                    // Active is clipped, find position before first clipped
                    for (auto col = columns.first(); col != nullptr; col = col->next()) {
                        if (col->data()->has_clipped_window()) {
                            insert_point = col;
                            break;
                        }
                    }
                }
                node = columns.emplace_before(insert_point, new Column(window, this));
                break;
            }
            case ModeModifier::POSITION_END: {
                // Find last non-clipped column
                ListNode<Column *> *last_non_clipped = nullptr;
                for (auto col = columns.last(); col != nullptr; col = col->prev()) {
                    if (!col->data()->has_clipped_window()) {
                        last_non_clipped = col;
                        break;
                    }
                }
                if (last_non_clipped)
                    node = columns.emplace_after(last_non_clipped, new Column(window, this));
                else if (columns.first() && columns.first()->data()->has_clipped_window())
                    node = columns.emplace_before(columns.first(), new Column(window, this));
                else
                    node = columns.emplace_after(columns.last(), new Column(window, this));
                break;
            }
            case ModeModifier::POSITION_BEGINNING:
                node = columns.emplace_before(columns.first(), new Column(window, this));
                break;
            }
        } else {
            // No enforcement: use normal insertion logic
            switch (modifier.get_position()) {
            case ModeModifier::POSITION_AFTER:
            default:
                node = columns.emplace_after(active, new Column(window, this));
                break;
            case ModeModifier::POSITION_BEFORE:
                node = columns.emplace_before(active, new Column(window, this));
                break;
            case ModeModifier::POSITION_END:
                node = columns.emplace_after(columns.last(), new Column(window, this));
                break;
            case ModeModifier::POSITION_BEGINNING:
                node = columns.emplace_before(columns.first(), new Column(window, this));
                break;
            }
        }

        if (focus == ModeModifier::FOCUS_FOCUS || store_active == nullptr)
            active = node;
        else {
            active = store_active;
            window->m_noInitialFocus = true;
        }

        reorder = Reorder::Auto;
        recalculate_row_geometry();
    }
    modifier = store_modifier;
    mode = store_mode;

    if (fsmode != eFullscreenMode::FSMODE_NONE) {
        toggle_window_fullscreen_internal(window, fsmode);
        force_focus_to_window(window);
    }
    // NOTE: Don't force focus here for regular windows - geometry isn't calculated yet
    // and warpCursor() would warp to 0,0. Focus is handled after geometry calculation.
    if (overview_on)
        toggle_overview();
}

// Remove a window and re-adapt rows and columns, returning
// true if successful, or false if this is the last row
// so the layout can remove it.
bool Row::remove_window(PHLWINDOW window)
{
    bool overview_on = overview;
    if (overview)
        toggle_overview();

    eFullscreenMode fsmode = window_fullscreen_state(window);
    if (fsmode != eFullscreenMode::FSMODE_NONE) {
        toggle_window_fullscreen_internal(window, eFullscreenMode::FSMODE_NONE);
    }

    reorder = Reorder::Auto;
    for (auto c = columns.first(); c != nullptr; c = c->next()) {
        Column *col = c->data();
        if (col->has_window(window)) {
            col->remove_window(window);
            if (col->size() == 0) {
                if (c == pinned) {
                    pinned = nullptr;
                }
                if (c == active) {
                    // make NEXT one active before deleting (like PaperWM)
                    // If active was the only one left, doesn't matter
                    // whether it points to end() or not, the row will
                    // be deleted by the parent.
                    active = active != columns.last() ? active->next() : active->prev();
                }
                delete col;
                columns.erase(c);
                if (columns.empty()) {
                    return false;
                } else {
                    recalculate_row_geometry();
                    break;
                }
            } else {
                c->data()->recalculate_col_geometry(calculate_gap_x(c), gap, true);
                break;
            }
        }
    }
    // In v0.54, the layout manager handles focus transitions after removal
    // via getNextCandidate(). Don't force focus or restore fullscreen here.
    if (overview_on)
        toggle_overview();

    return true;
}

void Row::focus_window(PHLWINDOW window, bool apply_focus_layout_param)
{
    Log::logger->log(Log::DEBUG, "[ROW] focus_window called, apply_focus_layout_param = {}", apply_focus_layout_param);

    for (auto c = columns.first(); c != nullptr; c = c->next()) {
        if (c->data()->has_window(window)) {
            Log::logger->log(Log::DEBUG, "[ROW] Found window in column, calling column focus_window");
            c->data()->focus_window(window);

            // If apply_focus_layout_param is false (mouse-triggered with mouse_disable enabled),
            // only update focus within the column, but DON'T change the active column pointer
            // This keeps the layout consistent - the centered window stays centered
            if (!apply_focus_layout_param) {
                Log::logger->log(Log::DEBUG, "[ROW] apply_focus_layout_param is false, returning early (mouse focus)");
                return;
            }

            // For keyboard focus: update active column and recalculate layout
            active = c;
            Log::logger->log(Log::DEBUG, "[ROW] Updated active column");

            // If noCenterFocus was recently triggered, skip layout recalculation (for movefocus_nocenter)
            auto timeSinceNoCenter = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - lastNoCenterTime);
            if (timeSinceNoCenter.count() < 200) {
                return;
            }

            // Apply focus layout if enabled
            const bool focus_layout_enabled = g_scrollerConfig.focus_layout_enable();
            Log::logger->log(Log::DEBUG, "[ROW] focus_layout_enable = {}", focus_layout_enabled);
            if (focus_layout_enabled) {
                Log::logger->log(Log::DEBUG, "[ROW] Calling apply_focus_layout()");
                apply_focus_layout();
            } else {
                Log::logger->log(Log::DEBUG, "[ROW] Focus layout disabled, calling recalculate_row_geometry()");
                // Only recalculate geometry if focus layout is not applied
                recalculate_row_geometry();
            }
            return;
        }
    }
    Log::logger->log(Log::DEBUG, "[ROW] Window not found in any column");
}

bool Row::move_focus(Direction dir, bool focus_wrap)
{
    if (!active)
        return false;

    Log::logger->log(Log::DEBUG, "[GRID] move_focus called: dir={}, grid_mode={}", (int)dir, grid_mode);
    bool changed_workspace = false;

    switch (dir) {
    case Direction::Left:
        if (!move_focus_left(focus_wrap))
            changed_workspace = true;
        break;
    case Direction::Right:
        if (!move_focus_right(focus_wrap))
            changed_workspace = true;
        break;
    case Direction::Up:
        if (grid_mode) {
            // Grid mode: switch to previous grid row (never wrap)
            if (!grid_move_up())
                changed_workspace = true;
        } else {
            if (!active->data()->move_focus_up(focus_wrap))
                changed_workspace = true;
        }
        break;
    case Direction::Down:
        if (grid_mode) {
            // Grid mode: switch to next grid row (never wrap)
            if (!grid_move_down())
                changed_workspace = true;
        } else {
            if (!active->data()->move_focus_down(focus_wrap))
                changed_workspace = true;
        }
        break;
    case Direction::Begin:
        move_focus_begin();
        break;
    case Direction::End:
        move_focus_end();
        break;
    default:
        return false;
    }

    reorder = Reorder::Auto;
    recalculate_row_geometry();

    return changed_workspace;
}

bool Row::move_focus_nocenter(Direction dir, bool focus_wrap)
{
    if (!active)
        return false;

    bool changed_workspace = false;

    switch (dir) {
    case Direction::Left:
        if (!move_focus_left(focus_wrap))
            changed_workspace = true;
        break;
    case Direction::Right:
        if (!move_focus_right(focus_wrap))
            changed_workspace = true;
        break;
    case Direction::Up:
        if (grid_mode) {
            // Grid mode: switch to previous grid row (never wrap)
            if (!grid_move_up())
                changed_workspace = true;
        } else {
            if (!active->data()->move_focus_up(focus_wrap))
                changed_workspace = true;
        }
        break;
    case Direction::Down:
        if (grid_mode) {
            // Grid mode: switch to next grid row (never wrap)
            if (!grid_move_down())
                changed_workspace = true;
        } else {
            if (!active->data()->move_focus_down(focus_wrap))
                changed_workspace = true;
        }
        break;
    case Direction::Begin:
        move_focus_begin();
        break;
    case Direction::End:
        move_focus_end();
        break;
    default:
        return false;
    }

    // Skip recalculate_row_geometry() to avoid centering
    // Just update reorder state
    reorder = Reorder::Auto;

    return changed_workspace;
}

bool Row::move_focus_left(bool focus_wrap)
{
    if (!active) return false;
    if (active == columns.first()) {
        PHLMONITOR monitor = monitor_in_direction(Math::DIRECTION_LEFT);
        if (monitor == nullptr) {
            if (focus_wrap) {
                active = columns.last();
                return true;
            }
        }

        orig_moveFocusTo("l");
        return false;
    }
    active = active->prev();
    return true;
}

bool Row::move_focus_right(bool focus_wrap)
{
    if (!active) return false;
    if (active == columns.last()) {
        PHLMONITOR monitor = monitor_in_direction(Math::DIRECTION_RIGHT);
        if (monitor == nullptr) {
            if (focus_wrap) {
                active = columns.first();
                return true;
            }
        }

        orig_moveFocusTo("r");
        return false;
    }
    active = active->next();
    return true;
}

void Row::move_focus_begin()
{
    active = columns.first();
}

void Row::move_focus_end()
{
    active = columns.last();
}

// Calculate lateral gaps for a column
Vector2D Row::calculate_gap_x(const ListNode<Column *> *column) const
{
    // First and last columns need a different gap
    auto gap0 = column == columns.first() ? 0.0 : gap;
    auto gap1 = column == columns.last() ? 0.0 : gap;
    return Vector2D(gap0, gap1);
}

void Row::resize_active_column(int step)
{
    if (!active) return;
    if (active->data()->fullscreen())
        return;

    bool overview_on = overview;
    if (overview)
        toggle_overview();

    if (mode == Mode::Column) {
        active->data()->cycle_size_active_window(step, calculate_gap_x(active), gap);
    } else {
        StandardSize width = active->data()->get_width();
        if (width == StandardSize::Free) {
            // When cycle-resizing from Free mode, move back to closest or default
            if (g_scrollerConfig.cyclesize_closest()) {
                double fraction = active->data()->get_geom_w() / max.w;
                width = scroller_sizes.get_column_closest_width(Desktop::focusState()->monitor(), fraction, step);
            } else {
                width = scroller_sizes.get_column_default_width(get_active_window());
            }
        } else {
            width = scroller_sizes.get_next_column_width(width, step);
        }
        active->data()->update_width(width, max.w);
        reorder = Reorder::Auto;
        recalculate_row_geometry();
    }
    if (overview_on)
        toggle_overview();
}

void Row::size_active_column(StandardSize size)
{
    if (!active)
        return;
    if (active->data()->fullscreen())
        return;

    bool overview_on = overview;
    if (overview)
        toggle_overview();

    if (mode == Mode::Column) {
        active->data()->size_active_window(size, calculate_gap_x(active), gap);
    } else {
        active->data()->update_width(size, max.w);
        reorder = Reorder::Auto;
        recalculate_row_geometry();
    }
    if (overview_on)
        toggle_overview();
}

void Row::size_active_column(const std::string &fraction)
{
    if (!active || fraction.empty())
        return;

    StandardSize size;
    if (std::isdigit(static_cast<unsigned char>(fraction.front()))) {
        int index = 0;
        const auto [end, error] = std::from_chars(fraction.data(), fraction.data() + fraction.size(), index);
        if (error != std::errc{} || end != fraction.data() + fraction.size())
            return;
        size = mode == Mode::Row ?
            scroller_sizes.get_column_width(index) : scroller_sizes.get_window_height(index);
    } else {
        StandardSize default_size = mode == Mode::Row ?
            scroller_sizes.get_column_width(0) : scroller_sizes.get_window_height(0);
        size = scroller_sizes.get_size_from_string(fraction, default_size);
    }
    size_active_column(size);
}

void Row::resize_active_window(const Vector2D &delta)
{
    if (!active)
        return;

    // If the active window in the active column is fullscreen, ignore.
    if (active->data()->fullscreen())
        return;
    if (overview)
        return;

    active->data()->resize_active_window(calculate_gap_x(active), gap, delta);
    recalculate_row_geometry();
}

void Row::set_mode(Mode m, bool silent)
{
    if (m == Mode::Toggle){
        if (mode == Mode::Row)
            mode = Mode::Column;
        else if (mode == Mode::Column)
            mode = Mode::Row;
    } else
        mode = m;
    if (!silent) {
        post_event("mode");
    }
}

Mode Row::get_mode() const
{
    return mode;
}

void Row::set_mode_modifier(const ModeModifier &options)
{
    auto pos = options.get_position(false);
    if (pos != ModeModifier::POSITION_UNDEFINED)
        modifier.set_position(pos);
    auto focus = options.get_focus(false);
    if (focus != ModeModifier::FOCUS_UNDEFINED)
        modifier.set_focus(focus);
    auto auto_mode = options.get_auto_mode(false);
    if (auto_mode != ModeModifier::AUTO_UNDEFINED) {
        modifier.set_auto_mode(auto_mode);
        modifier.set_auto_param(options.get_auto_param());
    }
    auto center_column = options.center_column_override();
    if (center_column.has_value())
        modifier.set_center_column(center_column.value());
    auto center_window = options.center_window_override();
    if (center_window.has_value())
        modifier.set_center_window(center_window.value());

    post_event("mode");
}

ModeModifier Row::get_mode_modifier() const
{
    return modifier;
}

void Row::align_column(Direction dir)
{
    if (!active) return;
    if (active->data()->fullscreen())
        return;
    if (overview)
        return;

    switch (dir) {
    case Direction::Left:
        active->data()->set_geom_pos(max.x, max.y);
        break;
    case Direction::Right:
        active->data()->set_geom_pos(max.x + max.w - active->data()->get_geom_w(), max.y);
        break;
    case Direction::Center:
        if (mode == Mode::Column) {
            const Vector2D gap_x = calculate_gap_x(active);
            active->data()->align_window(Direction::Center, gap_x, gap);
            active->data()->recalculate_col_geometry(gap_x, gap, true);
            return;
        } else {
            center_active_column();
        }
        break;
    case Direction::Up:
    case Direction::Down: {
        const Vector2D gap_x = calculate_gap_x(active);
        active->data()->align_window(dir, gap_x, gap);
        active->data()->recalculate_col_geometry(gap_x, gap, true);
        return;
    } break;
    case Direction::Middle: {
        const Vector2D gap_x = calculate_gap_x(active);
        active->data()->align_window(Direction::Center, gap_x, gap);
        center_active_column();
        break;
    }
    default:
        return;
    }
    reorder = Reorder::Lazy;
    recalculate_row_geometry();
}

void Row::pin()
{
    if (!active)
        return;

    if (pinned != nullptr) {
        pinned->data()->pin(false);
        pinned = nullptr;
    } else {
        pinned = active;
        pinned->data()->pin(true);
    }
}

Column *Row::get_pinned_column() const
{
    return pinned != nullptr ? pinned->data() : nullptr;
}

void Row::selection_toggle()
{
    if (!active) return;
    active->data()->selection_toggle();
}

void Row::selection_set(PHLWINDOWREF window)
{
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        col->data()->selection_set(window);
    }
}

void Row::selection_all()
{
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        col->data()->selection_all();
    }
}

void Row::selection_reset()
{
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        col->data()->selection_reset();
    }
}

static void insert_selection_before(List <Column *> &columns, ListNode<Column *> *node, const List<Column *> &selection)
{
    for (auto col = selection.first(); col != nullptr; col = col->next()) {
        columns.insert_before(node, col->data());
    }
}

static void insert_selection_after(List <Column *> &columns, ListNode<Column *> *node, const List<Column *> &selection)
{
    for (auto col = selection.last(); col != nullptr; col = col->prev()) {
        columns.insert_after(node, col->data());
    }
}

void Row::selection_move(const List<Column *> &selection, Direction direction)
{
    if (selection.empty())
        return;

    if (columns.size() == 0) {
        for (auto col = selection.first(); col != nullptr; col = col->next()) {
            columns.push_back(col->data());
        }
        active = columns.first();
    } else {
        switch (direction) {
        case Direction::Left:
            insert_selection_before(columns, active, selection);
            break;
        case Direction::Begin:
            insert_selection_before(columns, columns.first(), selection);
            break;
        case Direction::End:
            insert_selection_after(columns, columns.last(), selection);
            break;
        case Direction::Right:
        default:
            insert_selection_after(columns, active, selection);
            break;
        }
    }
}

bool Row::selection_exists() const
{
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        if (col->data()->selection_exists())
            return true;
    }
    return false;
}

void Row::selection_get(const Row *row, List<Column *> &selection)
{
    bool overview_on = overview;
    if (overview)
        toggle_overview();

    auto col = columns.first();
    while (col != nullptr) {
        auto next = col->next();
        Column *column = col->data()->selection_get(row);
        if (column != nullptr) {
            // Unpin the windows that are moving
            if (col == pinned) {
                column->pin(false);
            }
            selection.push_back(column);
            if (col->data()->size() == 0) {
                // If the column is left empty, remove pin
                if (col == pinned)
                    pinned = nullptr;
                // Removed all windows
                if (col == active) {
                    active = active != columns.last() ? active->next() : active->prev();
                }
                auto col_data = col->data();
                columns.erase(col);
                delete col_data;
            }
        }
        col = next;
    }

    if (overview_on)
        toggle_overview();
}

void Row::center_active_column()
{
    if (!active)
        return;
    Column *column = active->data();
    if (column->fullscreen())
        return;

    switch (column->get_width()) {
    case StandardSize::OneEighth:
        column->set_geom_pos(max.x + 7.0 * max.w / 16.0, max.y);
        break;
    case StandardSize::OneSixth:
        column->set_geom_pos(max.x + 5.0 * max.w / 12.0, max.y);
        break;
    case StandardSize::OneFourth:
        column->set_geom_pos(max.x + 3.0 * max.w / 8.0, max.y);
        break;
    case StandardSize::OneThird:
        column->set_geom_pos(max.x + max.w / 3.0, max.y);
        break;
    case StandardSize::ThreeEighths:
        column->set_geom_pos(max.x + 5.0 * max.w / 16.0, max.y);
        break;
    case StandardSize::OneHalf:
        column->set_geom_pos(max.x + max.w / 4.0, max.y);
        break;
    case StandardSize::FiveEighths:
        column->set_geom_pos(max.x + 3.0 * max.w / 16.0, max.y);
        break;
    case StandardSize::TwoThirds:
        column->set_geom_pos(max.x + max.w / 6.0, max.y);
        break;
    case StandardSize::ThreeQuarters:
        column->set_geom_pos(max.x + max.w / 8.0, max.y);
        break;
    case StandardSize::FiveSixths:
        column->set_geom_pos(max.x + max.w / 12.0, max.y);
        break;
    case StandardSize::SevenEighths:
        column->set_geom_pos(max.x + 1.0 * max.w / 16.0, max.y);
        break;
    case StandardSize::One:
        column->set_geom_pos(max.x, max.y);
        break;
    case StandardSize::Free:
        column->set_geom_pos(max.x + 0.5 * (max.w - column->get_geom_w()), max.y);
        break;
    default:
        break;
    }
}

void Row::move_active_window_to_group(const std::string &name)
{
    if (!active) return;

    for (auto c = columns.first(); c != nullptr; c = c->next()) {
        Column *col = c->data();
        if (col->get_name() == name) {
            if (col == active->data())
                return;

            PHLWINDOW window = active->data()->get_active_window();
            const PHLWINDOW target_anchor = col->get_active_window();
            if (!window || !target_anchor)
                return;

            // Removing the source may delete its column and relink the list.
            // Resolve the destination again through one of its windows instead
            // of retaining raw node/column pointers across that mutation.
            if (!remove_window(window))
                return;

            ListNode<Column*>* target_node = nullptr;
            for (auto node = columns.first(); node != nullptr; node = node->next()) {
                if (node->data()->has_window(target_anchor)) {
                    target_node = node;
                    break;
                }
            }
            if (!target_node) {
                add_active_window(window);
                return;
            }

            target_node->data()->add_active_window(window);
            if (!Fullscreen::controller()->isFullscreen(window))
                target_node->data()->recalculate_col_geometry(calculate_gap_x(target_node), gap, true);
            active = target_node;
            if (!Fullscreen::controller()->isFullscreen(window))
                recalculate_row_geometry();
            else {
                force_focus_to_window(window);
            }
            return;
        }
    }
    active->data()->set_name(name);
}

void Row::move_active_column(Direction dir)
{
    if (!active) return;

    // Reset nocenter timestamp so this operation can apply focus layout
    lastNoCenterTime = std::chrono::steady_clock::time_point{};

    bool overview_on = overview;
    if (overview)
        toggle_overview();

    auto window = active->data()->get_active_window();
    update_relative_cursor_coords(window);
    eFullscreenMode fsmode = window_fullscreen_state(window);
    if (fsmode != eFullscreenMode::FSMODE_NONE) {
        toggle_window_fullscreen_internal(window, eFullscreenMode::FSMODE_NONE);
    }

    // Check if clip window ordering is enforced
    const bool enforce_clip_order = g_scrollerConfig.clip_window_order_enforce();

    // Check if active column has clipped windows
    bool active_is_clipped = active->data()->has_clipped_window();

    switch (dir) {
    case Direction::Right:
        if (active != columns.last()) {
            auto next = active->next();
            // Don't allow non-clipped to move right of clipped (if enforced)
            if (enforce_clip_order && !active_is_clipped && next->data()->has_clipped_window()) {
                Log::logger->log(Log::DEBUG, "[CLIP] Cannot move non-clipped column right of clipped column");
                break;
            }
            columns.move_after(next, active);
        }
        break;
    case Direction::Left:
        if (active != columns.first()) {
            auto prev = active->prev();
            // Don't allow clipped to move left of non-clipped (if enforced)
            if (enforce_clip_order && active_is_clipped && !prev->data()->has_clipped_window()) {
                Log::logger->log(Log::DEBUG, "[CLIP] Cannot move clipped column left of non-clipped column");
                break;
            }
            columns.move_before(prev, active);
        }
        break;
    case Direction::Up:
        active->data()->move_active_up();
        break;
    case Direction::Down:
        active->data()->move_active_down();
        break;
    case Direction::Begin: {
        if (active == columns.first())
            break;
        // Don't allow clipped to move to beginning (if enforced)
        if (enforce_clip_order && active_is_clipped) {
            // Find the first clipped column and move before it instead
            for (auto col = columns.first(); col != nullptr; col = col->next()) {
                if (col->data()->has_clipped_window()) {
                    if (col != active)
                        columns.move_before(col, active);
                    break;
                }
            }
            Log::logger->log(Log::DEBUG, "[CLIP] Clipped column moved to beginning of clipped section");
        } else {
            columns.move_before(columns.first(), active);
        }
        break;
    }
    case Direction::End: {
        if (active == columns.last())
            break;
        // Don't allow non-clipped to move to end if there are clipped columns (if enforced)
        if (enforce_clip_order && !active_is_clipped) {
            // Find the last non-clipped column and move after it instead
            ListNode<Column *> *last_non_clipped = nullptr;
            for (auto col = columns.last(); col != nullptr; col = col->prev()) {
                if (!col->data()->has_clipped_window()) {
                    last_non_clipped = col;
                    break;
                }
            }
            if (last_non_clipped && last_non_clipped != active) {
                columns.move_after(last_non_clipped, active);
                Log::logger->log(Log::DEBUG, "[CLIP] Non-clipped column moved to end of non-clipped section");
            }
        } else {
            columns.move_after(columns.last(), active);
        }
        break;
    }
    case Direction::Center:
    default:
        return;
    }

    reorder = Reorder::Auto;
    recalculate_row_geometry();

    if (fsmode != eFullscreenMode::FSMODE_NONE) {
        window = active->data()->get_active_window();
        toggle_window_fullscreen_internal(window, fsmode);
    }
    force_focus_to_window(window);

    if (overview_on)
        toggle_overview();
}

void Row::move_active_window(Direction dir)
{
    if (!active) return;

    // Reset nocenter timestamp so this operation can apply focus layout
    lastNoCenterTime = std::chrono::steady_clock::time_point{};

    bool overview_on = overview;
    if (overview)
        toggle_overview();

    auto window = active->data()->get_active_window();
    update_relative_cursor_coords(window);
    eFullscreenMode fsmode = window_fullscreen_state(window);
    if (fsmode != eFullscreenMode::FSMODE_NONE) {
        toggle_window_fullscreen_internal(window, eFullscreenMode::FSMODE_NONE);
    }

    switch (dir) {
    case Direction::Right:
        if (active->data()->size() == 1) {
            if (active != columns.last()) {
                // Need to admit the window in the col to its right
                admit_window(AdmitExpelDirection::Right);
            }
        } else {
            // Need to expel the window (to the right)
            expel_window(AdmitExpelDirection::Right);
        }
        break;
    case Direction::Left:
        if (active->data()->size() == 1) {
            if (active != columns.first()) {
                // Need to admit the window in the col to its left
                admit_window(AdmitExpelDirection::Left);
            }
        } else {
            // Need to expel the window to the left
            expel_window(AdmitExpelDirection::Left);
        }
        break;
    case Direction::Up:
        active->data()->move_active_up();
        break;
    case Direction::Down:
        active->data()->move_active_down();
        break;
    case Direction::Begin: {
        if (active->data()->size() == 1) {
            if (active == columns.first())
                break;
            columns.move_before(columns.first(), active);
        } else {
            // Expel the window and create a column at the beginning
            expel_window(AdmitExpelDirection::Left);
            columns.move_before(columns.first(), active);
        }
        break;
    }
    case Direction::End: {
        if (active->data()->size() == 1) {
            if (active == columns.last())
                break;
            columns.move_after(columns.last(), active);
        } else {
            // Expel window and create a column at the end
            expel_window(AdmitExpelDirection::Right);
            columns.move_after(columns.last(), active);
        }
        break;
    }
    case Direction::Center:
    default:
        return;
    }

    reorder = Reorder::Auto;
    recalculate_row_geometry();

    if (fsmode != eFullscreenMode::FSMODE_NONE) {
        window = active->data()->get_active_window();
        toggle_window_fullscreen_internal(window, fsmode);
    }
    force_focus_to_window(window);

    if (overview_on)
        toggle_overview();
}

void Row::admit_window(AdmitExpelDirection dir)
{
    if (!active)
        return;
    if (active->data()->fullscreen())
        return;
    if (dir == AdmitExpelDirection::Left && active == columns.first())
        return;
    if (dir == AdmitExpelDirection::Right && active == columns.last())
        return;

    bool overview_on = overview;
    if (overview)
        toggle_overview();

    // We extract from active, but insert left or right of it, so we know at
    // least one gap will change
    Vector2D gap_x = calculate_gap_x(active);
    if (dir == AdmitExpelDirection::Left)
        gap_x.y = gap;
    else
        gap_x.x = gap;

    auto w = active->data()->expel_active(gap_x);
    if (!w) {
        if (overview_on)
            toggle_overview();
        return;
    }
    if (active == pinned)
        w->pin(false);

    ListNode<Column *> *node;
    if (dir == AdmitExpelDirection::Left) {
        node = active->prev();
    } else {
        node = active->next();
    }
    if (active->data()->size() == 0) {
        if (active == pinned)
            pinned = nullptr;
        columns.erase(active);
    }
    active = node;
    if (active == pinned)
        w->pin(true);
    active->data()->admit_window(w);

    reorder = Reorder::Auto;
    recalculate_row_geometry();

    post_event("admitwindow");

    if (overview_on)
        toggle_overview();
}

void Row::expel_window(AdmitExpelDirection dir)
{
    if (!active)
        return;
    if (active->data()->fullscreen())
        return;
    if (active->data()->size() == 1)
        // nothing to expel
        return;

    bool overview_on = overview;
    if (overview)
        toggle_overview();

    // The new column will be on the right of the active, so its gap to the right
    // will be the same, and on the left there will be a gap (to the column it left)
    Vector2D gap_x = calculate_gap_x(active);
    if (dir == AdmitExpelDirection::Left)
        gap_x.y = gap;
    else
        gap_x.x = gap;

    auto w = active->data()->expel_active(gap_x);
    if (!w) {
        if (overview_on)
            toggle_overview();
        return;
    }
    StandardSize width = w->get_width();
    if (active == pinned) {
        w->pin(false);
    }

    double maxw = width == StandardSize::Free ? w->get_geom_w(gap_x) : max.w;
    if (dir == AdmitExpelDirection::Left) {
        active = columns.emplace_before(active, new Column(w, width, maxw, this));
        // Initialize the position so it is located before the next column
        // This helps the heuristic in recalculate_row_geometry()
        active->data()->set_geom_pos(active->next()->data()->get_geom_x() - active->data()->get_geom_w(), max.y);
    } else {
        active = columns.emplace_after(active, new Column(w, width, maxw, this));
        // Initialize the position so it is located after the previous column
        // This helps the heuristic in recalculate_row_geometry()
        active->data()->set_geom_pos(active->prev()->data()->get_geom_x() + active->prev()->data()->get_geom_w(), max.y);
    }

    reorder = Reorder::Auto;
    recalculate_row_geometry();

    post_event("expelwindow");

    if (overview_on)
        toggle_overview();
}

Vector2D Row::predict_window_size() const
{
    return Vector2D(0.5 * max.w, max.h);
}

void Row::post_event(const std::string &event)
{
    if (event == "mode") {
        auto str_mode = mode == Mode::Row ? "row" : "column";
        g_pEventManager->postEvent(SHyprIPCEvent{"scroller", std::format("mode, {}, {}, {}, {}:{}, {}, {}", str_mode,
            modifier.get_position_string(), modifier.get_focus_string(), modifier.get_auto_mode_string(), modifier.get_auto_param(),
            modifier.get_center_column_string(), modifier.get_center_window_string())});
    } else if (event == "overview") {
        g_pEventManager->postEvent(SHyprIPCEvent{"scroller", std::format("overview, {}", overview ? 1 : 0)});
    } else if (event == "admitwindow") {
        g_pEventManager->postEvent(SHyprIPCEvent{"scroller", "admitwindow"});
    } else if (event == "expelwindow") {
        g_pEventManager->postEvent(SHyprIPCEvent{"scroller", "expelwindow"});
    }
}

// Returns true/false if columns/windows need to be recalculated
bool Row::update_sizes(PHLMONITOR monitor)
{
    Log::logger->log(Log::DEBUG, "[ROW] update_sizes called for workspace {} with monitor {}",
               workspace, monitor ? monitor->m_id : -1);

    if (!monitor) {
        Log::logger->log(Log::ERR, "[ROW] update_sizes called with null monitor!");
        return false;
    }

    // for gaps outer
    static auto PGAPSINDATA = CConfigValue<Config::IComplexConfigValue>("general:gaps_in");
    static auto PGAPSOUTDATA = CConfigValue<Config::IComplexConfigValue>("general:gaps_out");
    const auto *const PGAPSIN = static_cast<const Config::CCssGapData *>(PGAPSINDATA.ptr());
    const auto *const PGAPSOUT = static_cast<const Config::CCssGapData *>(PGAPSOUTDATA.ptr());
    const auto WORKSPACERULE = Config::workspaceRuleMgr()->getWorkspaceRuleFor(workspace_by_id(workspace));
    // For now, support only constant CCssGapData
    auto gaps_in = WORKSPACERULE && WORKSPACERULE->m_gapsIn ? WORKSPACERULE->m_gapsIn->m_top : PGAPSIN->m_top;
    auto gaps_out = WORKSPACERULE && WORKSPACERULE->m_gapsOut ? *WORKSPACERULE->m_gapsOut : *PGAPSOUT;
    const auto SIZE = monitor->m_size;
    const auto POS = monitor->m_position;
    const auto& reservedArea = monitor->m_reservedArea;

    Log::logger->log(Log::DEBUG, "[ROW] Monitor geometry: pos=({},{}) size=({},{})",
               POS.x, POS.y, SIZE.x, SIZE.y);

    full = Box(POS, SIZE);

    // Get workspace-specific padding
    auto padding = get_workspace_padding();

    Log::logger->log(Log::DEBUG, "[ROW] Padding: top={} right={} bottom={} left={}",
               padding.top, padding.right, padding.bottom, padding.left);
    Log::logger->log(Log::DEBUG, "[ROW] gaps_out: top={} right={} bottom={} left={}",
               gaps_out.m_top, gaps_out.m_right, gaps_out.m_bottom, gaps_out.m_left);
    Log::logger->log(Log::DEBUG, "[ROW] reservedArea: top={} right={} bottom={} left={}",
               reservedArea.top(), reservedArea.right(), reservedArea.bottom(), reservedArea.left());

    const Box newmax = Box(POS.x + reservedArea.left() + gaps_out.m_left + padding.left,
                           POS.y + reservedArea.top() + gaps_out.m_top + padding.top,
                           std::max(SIZE.x - reservedArea.left() - reservedArea.right() - gaps_out.m_left - gaps_out.m_right - padding.left - padding.right, 1.0),
                           std::max(SIZE.y - reservedArea.top() - reservedArea.bottom() - gaps_out.m_top - gaps_out.m_bottom - padding.top - padding.bottom, 1.0));
    bool changed = gap != gaps_in;
    gap = gaps_in;

    if (max != newmax)
        changed = true;

    max = newmax;
    Log::logger->log(Log::DEBUG, "[ROW] Final max box: x={} y={} w={} h={}",
               max.x, max.y, max.w, max.h);
    return changed;
}

void Row::set_fullscreen_mode_windows(eFullscreenMode mode)
{
    if (!active)
        return;
    Column *column = active->data();
    switch (mode) {
    case eFullscreenMode::FSMODE_NONE:
        break;
    case eFullscreenMode::FSMODE_FULLSCREEN:
        column->set_active_window_geometry(full);
        break;
    case eFullscreenMode::FSMODE_MAXIMIZED:
        column->set_active_window_geometry(max);
        break;
    default:
        break;
    }
}

void Row::set_fullscreen_mode(PHLWINDOW window, eFullscreenMode cur_mode, eFullscreenMode new_mode)
{
    reorder = Reorder::Auto;
    Window *win = nullptr;
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        win = col->data()->get_window(window);
        if (win != nullptr)
            break;
    }
    if (win != nullptr) {
        switch (new_mode) {
        case eFullscreenMode::FSMODE_NONE:
            win->pop_fullscreen_geom();
            suppressFocusLayout = false;
            recalculate_row_geometry();
            break;
        case eFullscreenMode::FSMODE_FULLSCREEN:
            if (cur_mode == eFullscreenMode::FSMODE_NONE)
                win->push_fullscreen_geom();
            win->set_geometry(full);
            break;
        case eFullscreenMode::FSMODE_MAXIMIZED:
            if (cur_mode == eFullscreenMode::FSMODE_NONE)
                win->push_fullscreen_geom();
            win->set_geometry(max);
            break;
        default:
            return;
        }
    }
}

void Row::fit_size(FitSize fitsize)
{
    if (!active) return;
    if (columns.size() == 0) return;
    if (active->data()->fullscreen()) {
        return;
    }
    if (overview) {
        return;
    }
    if (mode == Mode::Column) {
        active->data()->fit_size(fitsize, calculate_gap_x(active), gap);
        return;
    }
    ListNode<Column *> *from = nullptr;
    ListNode<Column *> *to = nullptr;
    switch (fitsize) {
    case FitSize::Active:
        from = to = active;
        break;
    case FitSize::Visible:
        for (auto c = columns.first(); c != nullptr; c = c->next()) {
            Column *col = c->data();
            auto c0 = col->get_geom_x();
            auto c1 = std::round(col->get_geom_x() + col->get_geom_w());
            if ((c0 < max.x + max.w && c0 >= max.x) ||
                (c1 > max.x && c1 <= max.x + max.w) ||
                // should never happen as columns are never wider than the screen
                (c0 < max.x && c1 >= max.x + max.w)) {
                from = c;
                break;
            }
        }
        for (auto c = columns.last(); c != nullptr; c = c->prev()) {
            Column *col = c->data();
            auto c0 = col->get_geom_x();
            auto c1 = std::round(col->get_geom_x() + col->get_geom_w());
            if ((c0 < max.x + max.w && c0 >= max.x) ||
                (c1 > max.x && c1 <= max.x + max.w) ||
                // should never happen as columns are never wider than the screen
                (c0 < max.x && c1 >= max.x + max.w)) {
                to = c;
                break;
            }
        }
        break;
    case FitSize::All:
        from = columns.first();
        to = columns.last();
        break;
    case FitSize::ToEnd:
        from = active;
        to = columns.last();
        break;
    case FitSize::ToBeg:
        from = columns.first();
        to = active;
        break;
    default:
        return;
    }

    // Now align from to left edge of the screen (max.x), split width of
    // screen (max.w) among from->to, and readapt the rest
    if (from != nullptr && to != nullptr) {
        double total = 0.0;
        for (auto c = from; c != to->next(); c = c->next()) {
            total += c->data()->get_geom_w();
        }
        // Prevent division by zero
        if (total <= 0.0) {
            return;
        }
        for (auto c = from; c != to->next(); c = c->next()) {
            Column *col = c->data();
            col->set_width_free();
            col->set_geom_w(col->get_geom_w() / total * max.w);
            // Set the width for all windows of each column
            double maxw = col->get_geom_w();
            col->update_width(StandardSize::Free, maxw);
        }
        from->data()->set_geom_pos(max.x, max.y);

        adjust_columns(from);
    }
}

bool Row::is_overview() const
{
    return overview;
}

void Row::toggle_overview()
{
    if (columns.size() == 0)
        return;
    auto window = get_active_window();
    if (!window || !window->m_workspace)
        return;
    PHLMONITOR monitor = window->m_workspace->m_monitor.lock();
    if (!monitor)
        return;
    overview = !overview;
    post_event("overview");
    if (overview) {
        // Turn off fullscreen mode if enabled
        preoverview_fsmode = window_fullscreen_state(window);
        if (preoverview_fsmode != eFullscreenMode::FSMODE_NONE) {
            toggle_window_fullscreen_internal(window, preoverview_fsmode);
        }
        // Find the bounding box
        Vector2D bmin(max.x + max.w, max.y + max.h);
        Vector2D bmax(max.x, max.y);
        for (auto c = columns.first(); c != nullptr; c = c->next()) {
            auto cx0 = c->data()->get_geom_x();
            auto cx1 = cx0 + c->data()->get_geom_w();
            Vector2D cheight = c->data()->get_height();
            if (cx0 < bmin.x)
                bmin.x = cx0;
            if (cx1 > bmax.x)
                bmax.x = cx1;
            if (cheight.x < bmin.y)
                bmin.y = cheight.x;
            if (cheight.y > bmax.y)
                bmax.y = cheight.y;
        }
        const double w = std::max(bmax.x - bmin.x, 1.0);
        const double h = std::max(bmax.y - bmin.y, 1.0);
        double scale = std::min(max.w / w, max.h / h);

        bool overview_scaled;
        if (g_scrollerConfig.overview_scale_content() && overviews && overviews->enable(workspace)) {
            overview_scaled = true;
        } else {
            overview_scaled = false;
        }

        if (overview_scaled) {
            Vector2D offset(0.5 * (monitor->m_size.x - w * scale) / scale, 0.5 * (monitor->m_size.y - h * scale) / scale);
            for (auto c = columns.first(); c != nullptr; c = c->next()) {
                Column *col = c->data();
                col->push_overview_geom();
                Vector2D cheight = col->get_height();
                col->set_geom_pos(offset.x + monitor->m_position.x + (col->get_geom_x() - bmin.x), offset.y + monitor->m_position.y + (cheight.x - bmin.y));
            }
            adjust_overview_columns();

            g_layoutManager->recalculateMonitor(monitor);
            g_pHyprRenderer->damageMonitor(monitor);
            Config::monitorRuleMgr()->ensureVRR(monitor);
            Desktop::globalWindowController()->updateSuspendedStates();

            overviews->set_scale(workspace, scale);
        } else {
            Vector2D offset(0.5 * (max.w - w * scale), 0.5 * (max.h - h * scale));
            for (auto c = columns.first(); c != nullptr; c = c->next()) {
                Column *col = c->data();
                col->push_overview_geom();
                Vector2D cheight = col->get_height();
                col->set_geom_pos(offset.x + max.x + (col->get_geom_x() - bmin.x) * scale, offset.y + max.y + (cheight.x - bmin.y) * scale);
                col->set_geom_w(col->get_geom_w() * scale);
                Vector2D start(offset.x + max.x, offset.y + max.y);
                col->scale(bmin, start, scale, gap);
            }
            adjust_overview_columns();
            if (overviews)
                overviews->set_scale(workspace, 1.0F);
        }
    } else {
        if (overviews)
            overviews->disable(workspace);
        for (auto c = columns.first(); c != nullptr; c = c->next()) {
            Column *col = c->data();
            col->pop_overview_geom();
        }
        // Try to maintain the positions except if the active is not visible,
        // in that case, make it visible.
        Column *acolumn = active->data();
        if (acolumn->get_geom_x() < max.x) {
            acolumn->set_geom_pos(max.x, max.y);
        } else if (acolumn->get_geom_x() + acolumn->get_geom_w() > max.x + max.w) {
            acolumn->set_geom_pos(max.x + max.w - acolumn->get_geom_w(), max.y);
        }
        adjust_columns(active);
        // Ensure suppressFocusLayout is cleared so recalculate_row_geometry
        // inside recalculateWorkspace actually runs (mouse movements during
        // overview could have set this flag)
        suppressFocusLayout = false;
        g_layoutManager->recalculateMonitor(monitor);
        g_pHyprRenderer->damageMonitor(monitor);
        Config::monitorRuleMgr()->ensureVRR(monitor);
        Desktop::globalWindowController()->updateSuspendedStates();
        // Turn fullscreen mode back on if enabled
        auto window = get_active_window();
        window->warpCursor();
        if (preoverview_fsmode != eFullscreenMode::FSMODE_NONE) {
            toggle_window_fullscreen_internal(window, preoverview_fsmode);
        }
    }

    for (auto const& m : State::monitorState()->monitors()) {
        g_pHyprRenderer->damageMonitor(m);
    }
}

void Row::update_windows(const Box &oldmax, bool force)
{
    if (!force)
        return;

    // Update active column position
    if (active && oldmax != max) {
        // Prevent division by zero if oldmax has zero dimensions
        if (oldmax.w <= 0.0 || oldmax.h <= 0.0) {
            recalculate_row_geometry();
            return;
        }
        double posx = max.x + max.w * (active->data()->get_geom_x() - oldmax.x) / oldmax.w;
        double posy = max.y + max.h * (active->data()->get_geom_vy() - oldmax.y) / oldmax.h;
        active->data()->set_geom_pos(posx, posy);
    }
    // Redo all columns: widths according to "width" (unless Free)
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        Column *column = col->data();
        StandardSize width = column->get_width();
        double maxw = width == StandardSize::Free ? column->get_geom_w() : max.w;
        column->update_width(width, maxw, false);
        // Redo all windows for each column according to "height" (unless Free)
        column->update_heights();
    }
    recalculate_row_geometry();
}

void Row::recalculate_row_geometry(bool apply_focus_layout_after)
{
    Log::logger->log(Log::DEBUG, "[GEOM] recalculate_row_geometry called, apply_focus_layout_after = {}", apply_focus_layout_after);

    // Cache frequently accessed config values at function entry
    const bool focus_enabled = g_scrollerConfig.focus_layout_enable();
    const bool center_enabled = g_scrollerConfig.center_row_if_space_available();

    // Check global suppress flag - if true, don't do ANY geometry recalculation (mouse focus with mouse_disable)
    if (suppressFocusLayout) {
        Log::logger->log(Log::DEBUG, "[GEOM] suppressFocusLayout is true, returning");
        return;
    }

    if (active == nullptr) {
        Log::logger->log(Log::DEBUG, "[GEOM] active is nullptr, returning");
        return;
    }

    if (active->data()->fullscreen()) {
        Log::logger->log(Log::DEBUG, "[GEOM] active window is fullscreen, returning");
        return;
    }
    if (overview) {
        Log::logger->log(Log::DEBUG, "[GEOM] overview mode, calling adjust_overview_columns");
        adjust_overview_columns();
        return;
    }

    // ISOLATION POINT: Grid mode uses completely separate layout
    if (grid_mode) {
        Log::logger->log(Log::DEBUG, "[GEOM] grid mode, calling recalculate_grid_geometry");
        recalculate_grid_geometry();
        return;
    }

    Log::logger->log(Log::DEBUG, "[GEOM] columns.size() = {}", columns.size());

    // If only one column remains, make it fullscreen
    if (columns.size() == 1) {
        Log::logger->log(Log::DEBUG, "[GEOM] Only 1 column, making it fullscreen");
        Column* col = columns.first()->data();
        if (col) {
            col->update_width(StandardSize::One, max.w, true);
            col->set_geom_pos(max.x, max.y);
            auto gap0 = 0.0;
            auto gap1 = 0.0;
            col->recalculate_col_geometry(Vector2D(gap0, gap1), gap, true);
        }
        return;
    }

    if (center_enabled && pinned == nullptr) {
        double lwidth = 0.0, rwidth = 0.0;
        for (auto col = columns.first(); col != active; col = col->next()) {
            lwidth += col->data()->get_geom_w();
        }
        for (auto col = active; col != nullptr; col = col->next()) {
            rwidth += col->data()->get_geom_w();
        }
        double width = lwidth + rwidth;
        if (width < max.w) {
            double start = max.x + 0.5 * (max.w - width);
            active->data()->set_geom_pos(start + lwidth, max.y);
        }
    }

    auto a_w = active->data()->get_geom_w();
    auto a_x = active->data()->get_geom_x();
    // Pinned will stay in place, with active having second priority to fit in
    // the screen on either side of pinned.
    if (pinned != nullptr) {
        // If pinned got kicked out of the screen (overview, for example),
        // bring it back in
        auto p_w = pinned->data()->get_geom_w();
        auto p_x = pinned->data()->get_geom_x();
        if (p_x < max.x) {
            pinned->data()->set_geom_pos(max.x, max.y);
        } else if (std::round(p_x + p_w) > max.x + max.w) {
            // pin overflows to the right, move to end of viewport
            pinned->data()->set_geom_pos(max.x + max.w - p_w, max.y);
        }
        if (a_x < max.x || std::round(a_x + a_w) > max.x + max.w) {
            // Active doesn't fit, move it next to pinned
            // Find space
            auto p_w = pinned->data()->get_geom_w();
            auto p_x = pinned->data()->get_geom_x();
            auto const lt = p_x - max.x;
            auto const rt = max.x + max.w - p_x - p_w;
            // From pinned to active, try to fit as many columns as possible
            if (pinned != active) {
                int p = 0, a = 0;
                int i = 0;
                for (auto col = columns.first(); col != nullptr; col = col->next(), ++i) {
                    if (col == pinned)
                        p = i;
                    if (col == active)
                        a = i;
                }
                if (p > a) {
                    // Pinned is after active
                    // The priority is to keep active before pinned if it fits.
                    // If it doesn't fit, see if it fits right after, otherwise
                    // move it to where there is more room, and if equal, leave
                    // it where it is.
                    auto w = a_w;
                    auto swap = active, col = active;
                    while (col != pinned) {
                        if (0 <= std::round(lt - w)) {
                            swap = col;
                            col = col->next();
                            w += col->data()->get_geom_w();
                        } else {
                            break;
                        }
                    }
                    if (0 <= std::round(lt - a_w)) {
                        // fits on the left
                        columns.move_after(swap, pinned);
                    } else {
                        if (0 <= std::round(rt - a_w) || rt > lt) {
                            // fits on the right
                            columns.move_before(active, pinned);
                        } else {
                            // doesn't fit, and there is the same or more
                            // room on the side where it is now, leave it
                            // there (right before pinned)
                            columns.move_after(active, pinned);
                        }
                    }
                } else {
                    // Pinned is before active
                    // The priority is to keep active after pinned if it fits.
                    // If it doesn't fit, see if it fits right before, otherwise
                    // move it to where there is more room, and if equal, leave
                    // it where it is.
                    auto w = a_w;
                    auto swap = active, col = active;
                    while (col != pinned) {
                        if (0 <= std::round(rt - w)) {
                            swap = col;
                            col = col->prev();
                            w += col->data()->get_geom_w();
                        } else {
                            break;
                        }
                    }
                    if (0 <= std::round(rt - a_w)) {
                        // fits on the right
                        columns.move_before(swap, pinned);
                    } else {
                        if (0 <= std::round(lt - a_w) || lt > rt) {
                            // fits on the left or there is more room there
                            columns.move_after(active, pinned);
                        } else {
                            // doesn't fit, and there is the same or more
                            // room on the side where it is now, leave it
                            // there (right after pinned)
                            columns.move_before(active, pinned);
                        }
                    }
                }
            }
        }
        // Now, we know pinned is in the right position (it doesn't move)
        adjust_columns(pinned);
        return;
    }

    if (modifier.center_column_enabled()) {
        double start = max.x + 0.5 * (max.w - active->data()->get_geom_w());
        active->data()->set_geom_pos(start, max.y);
        adjust_columns(active);
        return;
    }

    if (a_x < max.x) {
        // active starts outside on the left
        // set it on the left edge
        active->data()->set_geom_pos(max.x, max.y);
    } else if (std::round(a_x + a_w) > max.x + max.w) {
        // active overflows to the right, move to end of viewport
        active->data()->set_geom_pos(max.x + max.w - a_w, max.y);
    } else {
        // active is inside the viewport
        if (reorder == Reorder::Auto) {
            // The active column should always be completely in the viewport.
            // If any of the windows next to it on its right or left are
            // in the viewport, keep the current position.
            bool keep_current = false;
            if (active->prev() != nullptr) {
                Column *prev = active->prev()->data();
                if (prev->get_geom_x() >= max.x && std::round(prev->get_geom_x() + prev->get_geom_w()) <= max.x + max.w) {
                    keep_current = true;
                }
            }
            if (!keep_current && active->next() != nullptr) {
                Column *next = active->next()->data();
                if (next->get_geom_x() >= max.x && std::round(next->get_geom_x() + next->get_geom_w()) <= max.x + max.w) {
                    keep_current = true;
                }
            }
            if (!keep_current) {
                // If not:
                // We try to fit the column next to it on the right if it fits
                // completely, otherwise the one on the left. If none of them fit,
                // we leave it as it is.
                if (active->next() != nullptr) {
                    if (std::round(a_w + active->next()->data()->get_geom_w()) <= max.w) {
                        // set next at the right edge of the viewport
                        active->data()->set_geom_pos(max.x + max.w - a_w - active->next()->data()->get_geom_w(), max.y);
                    } else if (active->prev() != nullptr) {
                        if (std::round(active->prev()->data()->get_geom_w() + a_w) <= max.w) {
                            // set previous at the left edge of the viewport
                            active->data()->set_geom_pos(max.x + active->prev()->data()->get_geom_w(), max.y);
                        } else {
                            // none of them fit, leave active as it is
                            active->data()->set_geom_pos(a_x, max.y);
                        }
                    } else {
                        // nothing on the left, move active to left edge of viewport
                        active->data()->set_geom_pos(max.x, max.y);
                    }
                } else if (active->prev() != nullptr) {
                    if (std::round(active->prev()->data()->get_geom_w() + a_w) <= max.w) {
                        // set previous at the left edge of the viewport
                        active->data()->set_geom_pos(max.x + active->prev()->data()->get_geom_w(), max.y);
                    } else {
                        // it doesn't fit and nothing on the right, move active to right edge of viewport
                        active->data()->set_geom_pos(max.x + max.w - a_w, max.y);
                    }
                } else {
                    // nothing on the right or left, the window is in a correct position
                    active->data()->set_geom_pos(a_x, max.y);
                }
            } else {
                // the window is in a correct position
                // if the window is first or last, and some windows don't fit,
                // ensure it is at the edge
                // Columns can be unsorted when calling this function, so get the full
                // width by adding all widths
                double w = 0.0;
                for (auto col = columns.first(); col != nullptr; col = col->next()) {
                    w += col->data()->get_geom_w();
                }
                if (std::round(w) >= max.w) {
                    if (active == columns.first()) {
                        active->data()->set_geom_pos(max.x, max.y);
                    } else if (active == columns.last()) {
                        active->data()->set_geom_pos(max.x + max.w - a_w, max.y);
                    } else {
                        active->data()->set_geom_pos(a_x, max.y);
                    }
                } else {
                    active->data()->set_geom_pos(a_x, max.y);
                }
            }
        } else { // lazy
            // Try to avoid moving the active column unless it is out of the screen.
            // the window is in a correct position
            active->data()->set_geom_pos(a_x, max.y);
        }
    }

    adjust_columns(active);

    Log::logger->log(Log::DEBUG, "[GEOM] About to check apply_focus_layout_after: {}", apply_focus_layout_after);

    // Apply focus layout at the end if enabled and requested
    // Also check nocenter timestamp to prevent centering from nocenter focus
    auto timeSinceNoCenter = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - lastNoCenterTime);
    bool noCenterActive = timeSinceNoCenter.count() < 200;

    if (apply_focus_layout_after && !noCenterActive) {
        Log::logger->log(Log::DEBUG, "[GEOM] focus_layout_enable = {}", focus_enabled);
        if (focus_enabled) {
            Log::logger->log(Log::DEBUG, "[GEOM] Calling apply_focus_layout()");
            apply_focus_layout();
        }
    } else {
        Log::logger->log(Log::DEBUG, "[GEOM] apply_focus_layout_after is false or nocenter active, skipping");
    }

    Log::logger->log(Log::DEBUG, "[GEOM] recalculate_row_geometry completed");
}

// Adjust all the columns in the row using 'column' as anchor
void Row::adjust_columns(ListNode<Column *> *column)
{
    if (!column)
        return;

    // Adjust the positions of the columns to the left
    for (auto col = column->prev(), prev = column; col != nullptr; prev = col, col = col->prev()) {
        col->data()->set_geom_pos(prev->data()->get_geom_x() - col->data()->get_geom_w(), max.y);
    }
    // Adjust the positions of the columns to the right
    for (auto col = column->next(), prev = column; col != nullptr; prev = col, col = col->next()) {
        col->data()->set_geom_pos(prev->data()->get_geom_x() + prev->data()->get_geom_w(), max.y);
    }

    // Apply column geometry
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        // First and last columns need a different gap
        auto gap0 = col == columns.first() ? 0.0 : gap;
        auto gap1 = col == columns.last() ? 0.0 : gap;
        col->data()->recalculate_col_geometry(Vector2D(gap0, gap1), gap, true);
    }
}

// Adjust all the columns in the overview
void Row::adjust_overview_columns()
{
    // Apply column geometry
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        // First and last columns need a different gap
        auto gap0 = col == columns.first() ? 0.0 : gap;
        auto gap1 = col == columns.last() ? 0.0 : gap;
        col->data()->recalculate_col_geometry_overview(Vector2D(gap0, gap1), gap);
    }
}

// Find the column where the mouse pointer is, or return active
ListNode<Column *> *Row::get_mouse_column() const {
    // Find the column where the cursor is
    auto pos = g_pInputManager->getMouseCoordsInternal();
    auto column = active;
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        const auto x0 = col->data()->get_geom_x();
        const auto x1 = x0 + col->data()->get_geom_w();
        if (pos.x >= x0 && pos.x < x1) {
            column = col;
            break;
        }
    }
    return column;
}
void Row::scroll_update(Direction dir, const Vector2D &delta) {
    switch (dir) {
    case Direction::Up:
    case Direction::Down: {
        auto column = get_mouse_column();
        if (column)
            column->data()->scroll_update(delta.y);
        break;
    }
    case Direction::Left:
    case Direction::Right: {
        // Apply column geometry
        for (auto col = columns.first(); col != nullptr; col = col->next()) {
            col->data()->set_geom_pos(col->data()->get_geom_x() + delta.x, max.y);
            // First and last columns need a different gap
            auto gap0 = col == columns.first() ? 0.0 : gap;
            auto gap1 = col == columns.last() ? 0.0 : gap;
            col->data()->recalculate_col_geometry(Vector2D(gap0, gap1), gap, false);
        }
        break;
    }
    default:
        break;
    }

    const auto row_workspace = workspace_by_id(workspace);
    if (row_workspace) {
        if (const auto monitor = row_workspace->m_monitor.lock())
            g_pHyprRenderer->damageMonitor(monitor);
    }
}

void Row::scroll_end(Direction dir)
{
    if (!active)
        return;

    if (dir == Direction::Left) {
        auto newactive = columns.last();
        // Take the first after active that has its left edge in the viewport
        for (auto col = active->next(); col != nullptr; col = col->next()) {
            const auto x0 = col->data()->get_geom_x();
            if (x0 > max.x && x0 < max.x + max.w) {
                newactive = col;
                break;
            }
        }
        active = newactive;
    } else if (dir == Direction::Right) {
        auto newactive = columns.first();
        // Take the first abefore active that has its right edge in the viewport
        for (auto col = active->prev(); col != nullptr; col = col->prev()) {
            const auto x0 = col->data()->get_geom_x();
            const auto x1 = x0 + col->data()->get_geom_w();
            if (x1 > max.x && x1 < max.x + max.w) {
                newactive = col;
                break;
            }
        }
        active = newactive;
    } else if (dir == Direction::Up || dir == Direction::Down) {
        // This column should be the same while swiping. Mouse coordinates don't change while swiping
        auto column = get_mouse_column();
        if (column)
            column->data()->scroll_end(dir, gap);
    }
    recalculate_row_geometry();
    Desktop::focusState()->fullWindowFocus(get_active_window(), Desktop::FOCUS_REASON_OTHER);
}

bool Row::should_apply_focus_layout() const
{
    if (!g_pCompositor)
        return true;

    workspace_config_cache.update_focus_filter(g_scrollerConfig.focus_layout_disable_workspaces());

    const WORKSPACEID workspace_id = get_workspace();
    if (workspace_id == WORKSPACE_INVALID)
        return true;

    const auto workspace = workspace_by_id(workspace_id);
    if (!workspace)
        return true;

    return workspace_config_cache.focus_layout_enabled(workspace->m_name, std::to_string(workspace_id));
}

Padding Row::get_workspace_padding() const
{
    if (!g_pCompositor) {
        Log::logger->log(Log::DEBUG, "[PADDING] No compositor");
        return {};
    }

    workspace_config_cache.update_padding(g_scrollerConfig.workspace_padding());

    const WORKSPACEID workspace_id = get_workspace();
    if (workspace_id == WORKSPACE_INVALID) {
        Log::logger->log(Log::DEBUG, "[PADDING] Invalid workspace ID");
        return {};
    }

    const auto workspace = workspace_by_id(workspace_id);
    if (!workspace) {
        Log::logger->log(Log::DEBUG, "[PADDING] Workspace not found for ID {}", workspace_id);
        return {};
    }

    return workspace_config_cache.padding_for(workspace->m_name, std::to_string(workspace_id));
}

void Row::apply_focus_layout()
{
    Log::logger->log(Log::DEBUG, "[FOCUS] apply_focus_layout() called");

    // Safety check: ensure we have an active column
    if (!active || !active->data()) {
        Log::logger->log(Log::DEBUG, "[FOCUS] No active column, returning");
        return;
    }

    // Note: Windows with clip_when_inactive tag will be handled specially in the layout
    // They'll keep a fixed size while other windows use focus layout sizing

    // Skip if overview mode is active or fullscreen window is active
    if (overview || active->data()->fullscreen()) {
        Log::logger->log(Log::DEBUG, "[FOCUS] Overview or fullscreen active, returning");
        return;
    }

    // Skip if there's a pinned column (pinned columns take precedence)
    if (pinned != nullptr) {
        Log::logger->log(Log::DEBUG, "[FOCUS] Pinned column exists, returning");
        return;
    }

    // Check if focus layout is enabled for this workspace
    bool should_apply = should_apply_focus_layout();
    Log::logger->log(Log::DEBUG, "[FOCUS] should_apply_focus_layout() = {}", should_apply);
    if (!should_apply) {
        return;
    }

    // Safety check: ensure columns list is valid
    if (columns.size() == 0) {
        Log::logger->log(Log::DEBUG, "[FOCUS] No columns, returning");
        return;
    }

    Log::logger->log(Log::DEBUG, "[FOCUS] columns.size() = {}", columns.size());

    // Skip focus layout for single column - just let it use normal fullscreen
    if (columns.size() == 1) {
        Log::logger->log(Log::DEBUG, "[FOCUS] Only 1 column, returning");
        return;
    }

    // Check if centered carousel mode is enabled
    const bool center_active = g_scrollerConfig.focus_layout_center_active();

    double total_width = max.w;
    double focused_width = total_width * 0.75;  // 3/4
    double neighbor_width = total_width * 0.125; // 1/8

    Log::logger->log(Log::DEBUG, "[FOCUS] center_active = {}, total_width = {:.0f}", center_active, total_width);

    if (center_active) {
        Log::logger->log(Log::DEBUG, "[FOCUS] Applying centered carousel layout");
        // CENTERED CAROUSEL LAYOUT:
        // - Focused window is always centered on screen (3/4 width)
        // - Immediate neighbors are visible (1/8 width each)
        // - Other windows are positioned off-screen
        apply_centered_carousel_layout(total_width, focused_width, neighbor_width);
    } else {
        Log::logger->log(Log::DEBUG, "[FOCUS] Applying position-based layout");
        // POSITION-BASED LAYOUT (Original):
        // - Windows stay in their physical positions
        // - Focused gets 3/4, adjacent get 1/8
        // - All windows visible, laid out left to right
        apply_position_based_layout();
    }
}

// Helper: Check if a column contains a window with clip_when_inactive tag
static bool column_has_clip_tag(Column* col) {
    if (!col) return false;
    PHLWINDOW win = col->get_active_window();
    return win && win->m_ruleApplicator &&
        win->m_ruleApplicator->m_tagKeeper.isTagged("scroller:clip_when_inactive");
}

void Row::apply_centered_carousel_layout(double total_width, double focused_width, double neighbor_width)
{
    // Safety check: ensure active column exists
    if (!active) {
        Log::logger->log(Log::ERR, "[SCROLLER] apply_centered_carousel_layout: active is nullptr!");
        return;
    }
    if (columns.size() == 0) return;

    if (columns.size() == 1) {
        // Single column: centered, full width
        Column* focused_col = active->data();
        focused_col->update_width(StandardSize::One, max.w, true);
        focused_col->set_geom_pos(max.x, max.y);
    } else if (columns.size() == 2) {
        // Two columns: focused centered at 3/4, other at 1/8
        // BUT: Windows with clip_when_inactive stay at fixed 1/2 width
        Column* focused_col = active->data();
        ListNode<Column *>* other_node = (active == columns.first()) ? columns.last() : columns.first();
        Column* other_col = other_node->data();

        // Check if columns have clip tag and use fixed size
        StandardSize focused_size = column_has_clip_tag(focused_col) ? StandardSize::ThreeQuarters : StandardSize::ThreeQuarters;
        StandardSize other_size = column_has_clip_tag(other_col) ? StandardSize::ThreeQuarters : StandardSize::OneEighth;

        focused_col->update_width(focused_size, max.w, true);
        other_col->update_width(other_size, max.w, true);

        // Center the focused column
        double focused_x = max.x + (total_width - focused_width) / 2.0;
        focused_col->set_geom_pos(focused_x, max.y);

        // Position the other column
        if (other_node == columns.first()) {
            // Other is to the left of focused
            other_col->set_geom_pos(focused_x - neighbor_width, max.y);
        } else {
            // Other is to the right of focused
            other_col->set_geom_pos(focused_x + focused_width, max.y);
        }
    } else {
        // Three or more columns: carousel layout with focused centered

        // Calculate center position for focused column
        double focused_x = max.x + (total_width - focused_width) / 2.0;

        // PERFORMANCE FIX: Calculate active column index once, not in nested loop
        int active_index = 0;
        int idx = 0;
        for (auto check = columns.first(); check != nullptr; check = check->next()) {
            if (check == active) {
                active_index = idx;
                break;
            }
            idx++;
        }

        // Set widths and positions for all columns
        int col_index = 0;
        for (auto col = columns.first(); col != nullptr; col = col->next()) {
            Column* current_col = col->data();
            if (!current_col) {
                continue; // Skip null columns
            }

            // Check if this column has clip tag for fixed sizing
            bool has_clip = column_has_clip_tag(current_col);

            if (col == active) {
                // Focused column: centered at 3/4 width (stays 3/4 even if clipped)
                StandardSize width = StandardSize::ThreeQuarters;
                current_col->update_width(width, max.w, true);
                current_col->set_geom_pos(focused_x, max.y);
            } else if (col == active->prev()) {
                // Left neighbor: 1/8 width (or 3/4 if clipped), positioned to the left of focused
                StandardSize width = has_clip ? StandardSize::ThreeQuarters : StandardSize::OneEighth;
                current_col->update_width(width, max.w, true);
                current_col->set_geom_pos(focused_x - neighbor_width, max.y);
            } else if (col == active->next()) {
                // Right neighbor: 1/8 width (or 3/4 if clipped), positioned to the right of focused
                StandardSize width = has_clip ? StandardSize::ThreeQuarters : StandardSize::OneEighth;
                current_col->update_width(width, max.w, true);
                current_col->set_geom_pos(focused_x + focused_width, max.y);
            } else {
                // Non-adjacent columns: position progressively further from focused (or 3/4 if clipped)
                StandardSize width = has_clip ? StandardSize::ThreeQuarters : StandardSize::OneEighth;
                current_col->update_width(width, max.w, true);

                // PERFORMANCE FIX: Calculate distance using pre-computed indices
                bool is_left_of_focused = (col_index < active_index);
                int distance_from_active = is_left_of_focused ?
                    (active_index - col_index) : (col_index - active_index);

                double pos_x;
                if (is_left_of_focused) {
                    // Position progressively to the left
                    pos_x = focused_x - (neighbor_width * distance_from_active);
                } else {
                    // Position progressively to the right
                    pos_x = focused_x + focused_width + (neighbor_width * (distance_from_active - 1));
                }
                current_col->set_geom_pos(pos_x, max.y);
            }
            col_index++;
        }
    }

    // Apply column geometry without repositioning
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        auto gap0 = col == columns.first() ? 0.0 : gap;
        auto gap1 = col == columns.last() ? 0.0 : gap;
        col->data()->recalculate_col_geometry(Vector2D(gap0, gap1), gap, true);
    }

    // CLIP MODE: Store full sizes AFTER active window has been resized
    // This captures the "active" size (3/4 width) for clipping when inactive
    if (active) {
        active->data()->store_full_sizes_for_clipping();
    }
}

void Row::apply_position_based_layout()
{
    // Original position-based layout: windows stay in their positions
    // Focused gets 3/4, adjacent columns get 1/8

    if (columns.size() == 1) {
        // Single column: full width
        Column* focused_col = active->data();
        focused_col->update_width(StandardSize::One, max.w, true);
        focused_col->set_geom_pos(max.x, max.y);
    } else if (columns.size() == 2) {
        // Two columns: focused gets 3/4, other gets 1/4
        Column* focused_col = active->data();
        ListNode<Column *>* other_node = (active == columns.first()) ? columns.last() : columns.first();
        Column* other_col = other_node->data();

        focused_col->update_width(StandardSize::ThreeQuarters, max.w, true);
        other_col->update_width(StandardSize::OneFourth, max.w, true);

        // Position based on their order
        if (active == columns.first()) {
            // Focused is first
            focused_col->set_geom_pos(max.x, max.y);
            other_col->set_geom_pos(max.x + focused_col->get_geom_w(), max.y);
        } else {
            // Focused is last
            other_col->set_geom_pos(max.x, max.y);
            focused_col->set_geom_pos(max.x + other_col->get_geom_w(), max.y);
        }
    } else {
        // Three or more columns: focused gets 3/4, adjacent get 1/8
        for (auto col = columns.first(); col != nullptr; col = col->next()) {
            Column* current_col = col->data();
            if (!current_col) {
                continue;
            }

            if (col == active) {
                // Focused column gets 3/4 width
                current_col->update_width(StandardSize::ThreeQuarters, max.w, true);
            } else if (col == active->prev() || col == active->next()) {
                // Adjacent columns get 1/8 width
                current_col->update_width(StandardSize::OneEighth, max.w, true);
            } else {
                // Non-adjacent columns get 1/8 width
                current_col->update_width(StandardSize::OneEighth, max.w, true);
            }
        }

        // Position columns from left to right
        double current_x = max.x;
        for (auto col = columns.first(); col != nullptr; col = col->next()) {
            if (col->data()) {
                col->data()->set_geom_pos(current_x, max.y);
                current_x += col->data()->get_geom_w();
            }
        }
    }

    // Apply column geometry without repositioning
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        auto gap0 = col == columns.first() ? 0.0 : gap;
        auto gap1 = col == columns.last() ? 0.0 : gap;
        col->data()->recalculate_col_geometry(Vector2D(gap0, gap1), gap, true);
    }

    // CLIP MODE: Store full sizes AFTER active window has been resized
    if (active) {
        active->data()->store_full_sizes_for_clipping();
    }
}

bool Row::is_point_in_clipped_hidden_region(const Vector2D &point) const
{
    // Iterate through all columns and windows to find clipped windows
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        // Get all windows in this column
        std::vector<PHLWINDOWREF> windows;
        col->data()->get_windows(windows);

        for (auto &winRef : windows) {
            auto pWindow = winRef.lock();
            if (!pWindow) continue;

            // Skip if this is the active window
            if (active && pWindow == active->data()->get_active_window()) continue;

            // Check if this window has clip_when_inactive
            if (!pWindow->m_ruleApplicator ||
                !pWindow->m_ruleApplicator->m_tagKeeper.isTagged("scroller:clip_when_inactive")) continue;

            // Get the Window wrapper to access full_width
            Window *win = col->data()->get_window(pWindow);
            if (!win) continue;

            double full_width = win->get_full_width();
            if (full_width <= 0) continue;  // Not currently clipped

            // Calculate the hidden region
            // When clipped: window is at position (x, y) with size (full_width, full_height)
            // The visible portion is the rightmost (current_size.x - hidden_width) pixels
            // Hidden width = full_width - target_width
            // Since window size IS full_width when clipped, we need target_width
            // target_width = original column width (before clipping)

            double window_left = window_position(pWindow).x;
            double window_top = window_position(pWindow).y;
            double window_bottom = window_top + window_size(pWindow).y;

            // The hidden region is the leftmost (full_width - visible_width) pixels
            // When clipped, the window's m_size.x = full_width
            // The visible width is approximately the column width
            double hidden_width = full_width - col->data()->get_geom_w();

            // Check if point is in the hidden region (left side of the window)
            if (point.x >= window_left && point.x < window_left + hidden_width &&
                point.y >= window_top && point.y <= window_bottom) {
                Log::logger->log(Log::DEBUG, "[CLIP] Point ({:.0f}, {:.0f}) is in hidden region of clipped window. Hidden region: [{:.0f}, {:.0f}]",
                          point.x, point.y, window_left, window_left + hidden_width);
                return true;
            }
        }
    }

    return false;
}

// ============================================================================
// Grid Mode Implementation
// ============================================================================

void Row::toggle_grid_mode()
{
    set_grid_mode(!grid_mode);
}

void Row::set_grid_mode(bool enabled)
{
    if (grid_mode == enabled) return;

    // Exit overview mode if active
    if (overview) {
        toggle_overview();
    }

    grid_mode = enabled;

    if (enabled) {
        // Initialize: assign grid_row to each window based on position in column
        assign_grid_rows();
        // Set current_grid_row to that of the active window
        if (active && active->data()->get_active_window()) {
            Window* win = active->data()->get_window(active->data()->get_active_window());
            if (win) {
                current_grid_row = win->get_grid_row();
            }
        }
    } else {
        // Exiting grid mode: reset column hidden state
        // Windows will be repositioned by recalculate_row_geometry
        for (auto col = columns.first(); col != nullptr; col = col->next()) {
            col->data()->set_hidden(false);
        }
    }

    recalculate_row_geometry();
    post_event("gridmode");
}

void Row::assign_grid_rows()
{
    max_grid_row = 0;
    int col_idx = 0;
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        uint32_t row_idx = 0;
        for (auto win = col->data()->windows_first(); win != nullptr; win = win->next()) {
            win->data()->set_grid_row(row_idx);
            Log::logger->log(Log::DEBUG, "[GRID] assign_grid_rows: col={}, window grid_row={}", col_idx, row_idx);
            if (row_idx > max_grid_row) {
                max_grid_row = row_idx;
            }
            row_idx++;
        }
        col_idx++;
    }
    Log::logger->log(Log::DEBUG, "[GRID] assign_grid_rows: max_grid_row={}", max_grid_row);
}

void Row::recalculate_grid_geometry()
{
    if (active == nullptr) return;

    Log::logger->log(Log::DEBUG, "[GRID] recalculate_grid_geometry: current_grid_row={}", current_grid_row);

    // Count visible columns (those with a window in current_grid_row)
    int visible_count = 0;
    double total_width = 0.0;
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        Window* visible_win = col->data()->get_window_at_grid_row(current_grid_row);
        if (visible_win != nullptr) {
            visible_count++;
            total_width += col->data()->get_geom_w();
        }
    }

    Log::logger->log(Log::DEBUG, "[GRID] recalculate_grid_geometry: visible_count={}", visible_count);
    if (visible_count == 0) return;

    // Calculate starting x position (center if space available)
    double x;
    if (total_width + (visible_count - 1) * gap < max.w) {
        x = max.x + 0.5 * (max.w - total_width - (visible_count - 1) * gap);
    } else {
        x = max.x;
    }

    // Layout visible columns
    bool first_visible = true;
    for (auto col = columns.first(); col != nullptr; col = col->next()) {
        Window* visible_win = col->data()->get_window_at_grid_row(current_grid_row);

        if (visible_win == nullptr) {
            // Column has no window in this row - mark hidden, skip layout
            col->data()->set_hidden(true);
            continue;
        }

        col->data()->set_hidden(false);

        // Position column
        double w = col->data()->get_geom_w();
        col->data()->set_geom_pos(x, max.y);

        // Calculate gaps
        double gap0 = first_visible ? 0.0 : gap;
        double gap1 = 0.0;  // Will be updated for last visible column
        first_visible = false;

        x += w + gap;

        // Layout only the visible window in this column
        Log::logger->log(Log::DEBUG, "[GRID] About to call recalculate_grid_col_geometry for column with {} windows", col->data()->size());
        col->data()->recalculate_grid_col_geometry(current_grid_row, Vector2D(gap0, gap1), gap);
        Log::logger->log(Log::DEBUG, "[GRID] Done calling recalculate_grid_col_geometry");
    }
}

bool Row::grid_move_up()
{
    Log::logger->log(Log::DEBUG, "[GRID] grid_move_up: current_grid_row={}, max_grid_row={}", current_grid_row, max_grid_row);
    if (current_grid_row == 0) {
        Log::logger->log(Log::DEBUG, "[GRID] grid_move_up: already at top row 0, returning false");
        return false;  // At top, never wrap
    }

    current_grid_row--;
    Log::logger->log(Log::DEBUG, "[GRID] grid_move_up: moved to row {}", current_grid_row);

    // Update active to window in current column at new grid row
    if (active) {
        Window* w = active->data()->get_window_at_grid_row(current_grid_row);
        if (w) {
            Log::logger->log(Log::DEBUG, "[GRID] grid_move_up: found window at grid_row {}, focusing", current_grid_row);
            active->data()->focus_window(w->get_window());
        } else {
            Log::logger->log(Log::DEBUG, "[GRID] grid_move_up: no window in current column at row {}, searching", current_grid_row);
            // Current column has no window in this row
            // Find nearest column with a window in this row
            find_and_focus_column_at_grid_row(current_grid_row);
        }
    }

    recalculate_grid_geometry();
    return true;
}

bool Row::grid_move_down()
{
    Log::logger->log(Log::DEBUG, "[GRID] grid_move_down: current_grid_row={}, max_grid_row={}", current_grid_row, max_grid_row);
    if (current_grid_row >= max_grid_row) {
        Log::logger->log(Log::DEBUG, "[GRID] grid_move_down: already at bottom row {}, returning false", max_grid_row);
        return false;  // At bottom, never wrap
    }

    current_grid_row++;
    Log::logger->log(Log::DEBUG, "[GRID] grid_move_down: moved to row {}", current_grid_row);

    // Update active to window in current column at new grid row
    if (active) {
        Window* w = active->data()->get_window_at_grid_row(current_grid_row);
        if (w) {
            Log::logger->log(Log::DEBUG, "[GRID] grid_move_down: found window at grid_row {}, focusing", current_grid_row);
            active->data()->focus_window(w->get_window());
        } else {
            Log::logger->log(Log::DEBUG, "[GRID] grid_move_down: no window in current column at row {}, searching", current_grid_row);
            // Current column has no window in this row
            // Find nearest column with a window in this row
            find_and_focus_column_at_grid_row(current_grid_row);
        }
    }

    recalculate_grid_geometry();
    return true;
}

void Row::find_and_focus_column_at_grid_row(uint32_t grid_row)
{
    // Search left and right from active column for nearest with window in grid_row
    if (!active) return;

    // Search right first
    for (auto col = active->next(); col != nullptr; col = col->next()) {
        Window* w = col->data()->get_window_at_grid_row(grid_row);
        if (w) {
            active = col;
            active->data()->focus_window(w->get_window());
            return;
        }
    }

    // Search left
    for (auto col = active->prev(); col != nullptr; col = col->prev()) {
        Window* w = col->data()->get_window_at_grid_row(grid_row);
        if (w) {
            active = col;
            active->data()->focus_window(w->get_window());
            return;
        }
    }

    // No column found with window in this grid row - this shouldn't happen
    // if max_grid_row is calculated correctly
}
