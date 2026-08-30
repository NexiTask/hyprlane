#ifndef SCROLLER_ROW_H
#define SCROLLER_ROW_H

#include "column.h"
#include "workspace_config.h"

#include <vector>
#include <string>

class Row {
public:
    Row(WORKSPACEID workspace, PHLMONITOR monitor);
    ~Row();
    size_t size() const {
        return columns.size();
    }
    WORKSPACEID get_workspace() const { return workspace; }
    const Box &get_max() const { return max; }
    bool has_window(PHLWINDOW window) const {
        for (auto col = columns.first(); col != nullptr; col = col->next()) {
            if (col->data()->has_window(window))
                return true;
        }
        return false;
    }
    Column *get_active_column() const {
        if (!active) return nullptr;
        return active->data();
    }
    PHLWINDOW get_active_window() const {
        if (!active) return nullptr;
        return active->data()->get_active_window();
    }
    bool is_active(PHLWINDOW window) const {
        return get_active_window() == window;
    }
    void get_windows(std::vector<PHLWINDOWREF> &windows) {
        for (auto col = columns.first(); col != nullptr; col = col->next()) {
            col->data()->get_windows(windows);
        }
    }
    void add_active_window(PHLWINDOW window);
    // Remove a window and re-adapt rows and columns, returning
    // true if successful, or false if this is the last row
    // so the layout can remove it.
    bool remove_window(PHLWINDOW window);
    void focus_window(PHLWINDOW window, bool apply_focus_layout = true); 
    bool move_focus(Direction dir, bool focus_wrap);
    bool move_focus_nocenter(Direction dir, bool focus_wrap);

    void resize_active_column(int step);
    void size_active_column(const std::string &arg);
    void resize_active_window(const Vector2D &delta);
    void set_mode(Mode m, bool silent = false);
    Mode get_mode() const;
    void set_mode_modifier(const ModeModifier &m);
    ModeModifier get_mode_modifier() const;
    void find_auto_insert_point();
    void align_column(Direction dir);
    void pin();
    Column *get_pinned_column() const;
    void selection_toggle();
    void selection_set(PHLWINDOWREF window);
    void selection_all();
    void selection_reset();
    void selection_move(const List<Column *> &columns, Direction direction);
    void selection_get(const Row *row, List<Column *> &selection);
    bool selection_exists() const;
    void move_active_window_to_group(const std::string &name);
    void move_active_column(Direction dir);
    void move_active_window(Direction dir);
    void admit_window(AdmitExpelDirection dir);
    void expel_window(AdmitExpelDirection dir);
    Vector2D predict_window_size() const;
    void post_event(const std::string &event);
    // Returns the old viewport
    bool update_sizes(PHLMONITOR monitor);
    void set_fullscreen_mode_windows(eFullscreenMode mode);
    void set_fullscreen_mode(PHLWINDOW window, eFullscreenMode cur_mode, eFullscreenMode new_mode);
    void fit_size(FitSize fitsize);
    bool is_overview() const;
    void toggle_overview();
    void update_windows(const Box &oldmax, bool force);
    void recalculate_row_geometry(bool apply_focus_layout_after = true);
    bool should_apply_focus_layout() const;
    Padding get_workspace_padding() const;
    void apply_focus_layout();
    void apply_centered_carousel_layout(double total_width, double focused_width, double neighbor_width);
    void apply_position_based_layout();

    void scroll_update(Direction dir, const Vector2D &delta);
    void scroll_end(Direction dir);

    // Check if a point is in the hidden region of any clipped window
    bool is_point_in_clipped_hidden_region(const Vector2D &point) const;

    // Grid mode support
    bool is_grid_mode() const { return grid_mode; }
    void set_grid_mode(bool enabled);
    void toggle_grid_mode();
    uint32_t get_current_grid_row() const { return current_grid_row; }
    uint32_t get_max_grid_row() const { return max_grid_row; }

private:
    // Grid mode helpers
    void assign_grid_rows();
    void recalculate_grid_geometry();
    bool grid_move_up();
    bool grid_move_down();
    void find_and_focus_column_at_grid_row(uint32_t grid_row);

    bool move_focus_left(bool focus_wrap);
    bool move_focus_right(bool focus_wrap);
    void move_focus_begin();
    void move_focus_end();
    void center_active_column();
    // Calculate lateral gaps for a column
    Vector2D calculate_gap_x(const ListNode<Column *> *column) const;
    // Adjust all the columns in the row using 'column' as anchor
    void adjust_columns(ListNode<Column *> *column);
    // Adjust all the columns in the overview
    void adjust_overview_columns();
    void size_active_column(StandardSize size);
    ListNode<Column *> *get_mouse_column() const;

    WORKSPACEID workspace = WORKSPACE_INVALID;
    Box full{};
    Box max{};
    bool overview = false;
    eFullscreenMode preoverview_fsmode = eFullscreenMode::FSMODE_NONE;
    int gap = 0;
    Reorder reorder = Reorder::Auto;
    Mode mode = Mode::Row;
    ModeModifier modifier;
    ListNode<Column *> *pinned = nullptr;
    ListNode<Column *> *active = nullptr;
    List<Column *> columns;

    // Grid mode state
    bool grid_mode = false;
    uint32_t current_grid_row = 0;
    uint32_t max_grid_row = 0;
};

#endif // SCROLLER_ROW_H
