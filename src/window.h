#ifndef SCROLLER_WINDOW_H
#define SCROLLER_WINDOW_H

#include "common.h"
#include "sizes.h"
#include "decorations.h"

class Window {
public:
    Window(PHLWINDOW window, double maxy, double box_h, StandardSize width);
    ~Window() {
        PHLWINDOW w = window.lock();
        if (w && decoration) {
            w->removeWindowDeco(decoration);
        }
    }
    PHLWINDOW get_window() { return window.lock(); }
    double get_geom_h() const { return box_h; }
    void set_geom_h(double geom_h) { box_h = geom_h; }

    void set_geom_x(double x, const Vector2D &gap_x) {
        PHLWINDOW w = window.lock();
        if (!w) return;
        SBoxExtents reserved_area = w->getFullWindowReservedArea();
        const Vector2D topL = reserved_area.topLeft;
        auto position = window_position(w);
        position.x = x + topL.x + gap_x.x;
        set_window_position(w, position);
    }
    double get_geom_y(double gap0) const {
        PHLWINDOW w = window.lock();
        if (!w) return 0.0;
        SBoxExtents reserved_area = w->getFullWindowReservedArea();
        const Vector2D topL = reserved_area.topLeft;
        return window_position(w).y - topL.y - gap0;
    }
    void push_fullscreen_geom() {
        push_geom(mem_fs);
    }
    void pop_fullscreen_geom() {
        pop_geom(mem_fs);
    }
    void push_overview_geom() {
        push_geom(mem_ov);
    }
    void pop_overview_geom() {
        pop_geom(mem_ov);
    }
    StandardSize get_height() const { return height; }
    void update_height(StandardSize h, double max);
    void set_height_free() { height = StandardSize::Free; }

    // Called by the parent column on the active window every time it changes width
    // This allows windows to have a independently stored width when they leave
    // the column
    void set_width(StandardSize w) { width = w; }
    StandardSize get_width() const { return width; }
    void set_geom_w(double geomw, const Vector2D &gap_x) {
        PHLWINDOW w = window.lock();
        if (!w) return;
        SBoxExtents reserved_area = w->getFullWindowReservedArea();
        const Vector2D topL = reserved_area.topLeft, botR = reserved_area.bottomRight;
        geom_w = geomw - topL.x - botR.x - gap_x.x - gap_x.y;
    }
    double get_geom_w(const Vector2D &gap_x) const {
        PHLWINDOW w = window.lock();
        if (!w) return geom_w;
        SBoxExtents reserved_area = w->getFullWindowReservedArea();
        Vector2D topL = reserved_area.topLeft, botR = reserved_area.bottomRight;
        return geom_w + topL.x + botR.x + gap_x.x + gap_x.y;
    }

    void set_geometry(const Box &box) {
        PHLWINDOW w = window.lock();
        if (!w) return;
        set_window_position(w, Vector2D(box.x, box.y), true);
        set_window_size(w, Vector2D(box.w, box.h), true);
        w->sendWindowSize();
    }
    bool is_window(PHLWINDOW w) const {
        return window == w;
    }
    
    eFullscreenMode fullscreen_state() const {
        PHLWINDOW w = window.lock();
        if (!w) return eFullscreenMode::FSMODE_NONE;
        return Fullscreen::controller()->getFullscreenModes(w).internal;
    }

    void scale(const Vector2D &bmin, const Vector2D &start, double scale, double gap0, double gap1) {
        PHLWINDOW w = window.lock();
        if (!w) return;
        set_geom_h(get_geom_h() * scale);
        SBoxExtents reserved_area = w->getFullWindowReservedArea();
        auto position = start + reserved_area.topLeft + (window_position(w) - reserved_area.topLeft - bmin) * scale;
        position.y += gap0;
        auto size = window_size(w);
        size.x *= scale;
        size.y = (size.y + reserved_area.topLeft.y + reserved_area.bottomRight.y + gap0 + gap1) * scale - gap0 - gap1 - reserved_area.topLeft.y - reserved_area.bottomRight.y;
        size = Vector2D(std::max(size.x, 1.0), std::max(size.y, 1.0));
        set_window_position(w, position);
        set_window_size(w, size);
        w->sendWindowSize();
    }

    void move_to_bottom(double x, const Box &max, const Vector2D &gap_x, double gap) {
        PHLWINDOW w = window.lock();
        if (!w) return;
        SBoxExtents reserved_area = w->getFullWindowReservedArea();
        const Vector2D topL = reserved_area.topLeft;
        set_window_position(w, Vector2D(x + topL.x + gap_x.x, max.y + max.h - get_geom_h() + topL.y + gap));
    }
    void move_to_top(double x, const Box &max, const Vector2D &gap_x, double gap) {
        PHLWINDOW w = window.lock();
        if (!w) return;
        SBoxExtents reserved_area = w->getFullWindowReservedArea();
        const Vector2D topL = reserved_area.topLeft;
        set_window_position(w, Vector2D(x + topL.x + gap_x.x, max.y + topL.y + gap));
    }
    void move_to_center(double x, const Box &max, const Vector2D &gap_x, double gap0, double gap1) {
        PHLWINDOW w = window.lock();
        if (!w) return;
        SBoxExtents reserved_area = w->getFullWindowReservedArea();
        Vector2D topL = reserved_area.topLeft, botR = reserved_area.bottomRight;
        set_window_position(w, Vector2D(x + topL.x + gap_x.x, max.y + 0.5 * (max.h - (botR.y - topL.y + gap1 - gap0 + window_size(w).y))));
    }
    void move_to_pos(double x, double y, const Vector2D &gap_x, double gap) {
        PHLWINDOW w = window.lock();
        if (!w) return;
        SBoxExtents reserved_area = w->getFullWindowReservedArea();
        const Vector2D topL = reserved_area.topLeft;
        set_window_position(w, Vector2D(x + topL.x + gap_x.x, y + gap + topL.y));
    }

    void scroll(double delta_y) {
        PHLWINDOW w = window.lock();
        if (!w) return;
        auto position = window_position(w);
        position.y += delta_y;
        set_window_position(w, position, true);
    }

    void update_window(double w, const Vector2D &gap_x, double gap0, double gap1, bool animate, bool is_active);
    bool can_resize_width(double geomw, double maxw, const Vector2D &gap_x, double gap, double deltax) {
        PHLWINDOW w = window.lock();
        if (!w) return false;
        // First, check if resize is possible or it would leave any window
        // with an invalid size.
        SBoxExtents reserved_area = w->getFullWindowReservedArea();
        Vector2D topL = reserved_area.topLeft, botR = reserved_area.bottomRight;
        // Width check
        auto rwidth = geomw + deltax - topL.x - botR.x - gap_x.x - gap_x.y;
        // Now we check for a size smaller than the maximum possible gap, so
        // we never get in trouble when a window gets expelled from a column
        // with gaps_out, gaps_in, to a column with gaps_in on both sides.
        auto mwidth = geomw + deltax - topL.x - botR.x - 2.0 * std::max(std::max(gap_x.x, gap_x.y), gap);
        if (mwidth <= 0.0 || rwidth > maxw)
            return false;

        return true;
    }
    bool can_resize_height(double maxh, bool active, double gap0, double gap1, double deltay) {
        PHLWINDOW w = window.lock();
        if (!w) return false;
        SBoxExtents reserved_area = w->getFullWindowReservedArea();
        const Vector2D topL = reserved_area.topLeft, botR = reserved_area.bottomRight;
        auto wh = get_geom_h() - gap0 - gap1 - topL.y - botR.y;
        if (active)
            wh += deltay;
        if (wh <= 0.0 || wh + gap0 + gap1 + topL.y + botR.y > maxh)
            return false;
        return true;
    }

    Config::CGradientValueData get_border_color() const;

    void selection_toggle() {
        selected = !selected;
    }

    void selection_set() {
        selected = true;
    }

    void selection_reset() {
        if (selected)
            selection_toggle();
    }

    bool is_selected() const { return selected; }

    // Grid mode support
    uint32_t get_grid_row() const { return grid_row; }
    void set_grid_row(uint32_t row) { grid_row = row; }

    void move_to_workspace(PHLWORKSPACE workspace) {
        PHLWINDOW w = window.lock();
        if (!w || !workspace) return;
        w->moveToWorkspace(workspace);
        w->m_monitor = workspace->m_monitor;
    }

    void pin(bool pin) {
        PHLWINDOW w = window.lock();
        if (!w || !w->m_ruleApplicator) return;
        if (pin) {
            w->m_ruleApplicator->m_tagKeeper.applyTag("+scroller:pinned");
        } else {
            w->m_ruleApplicator->m_tagKeeper.applyTag("-scroller:pinned");
        }
        w->updateDecorationValues();
    }

    void set_clip_when_inactive(bool clip) {
        clip_when_inactive = clip;
    }

    bool get_clip_when_inactive() const {
        return clip_when_inactive;
    }

    // Store the actual full size when clipping is active
    void store_full_size(double w, double h) {
        full_width = w;
        full_height = h;
    }

    double get_full_width() const { return full_width; }
    double get_full_height() const { return full_height; }

private:
    struct Memory {
        double pos_y = 0.0;
        double box_h = 0.0;
        Vector2D vPosition{};
        Vector2D vSize{};
    };

    void push_geom(Memory &mem) {
        PHLWINDOW w = window.lock();
        if (!w) return;
        mem.box_h = box_h;
        mem.pos_y = window_position(w).y;
        mem.vPosition = window_position(w);
        mem.vSize = window_size(w);
    }
    void pop_geom(const Memory &mem) {
        PHLWINDOW w = window.lock();
        if (!w) return;
        box_h = mem.box_h;
        set_window_position(w, mem.vPosition, true);
        set_window_size(w, mem.vSize, true);
        w->sendWindowSize();
    }

    PHLWINDOWREF window;
    StandardSize height = StandardSize::One;
    // This keeps track of the window width and recovers it when it is alone
    // in a column. When it is in a column with more windows, the active window
    // has this value synced with the column width. So this value changes when
    // the window is active and resized by its parent column resize.
    StandardSize width = StandardSize::OneHalf;
    // Windows store their `resizeActiveWindow` width, so it can be recovered
    // when their mode changes to StandardSize::FREE. This is necessary because
    // their window->m_size changes.
    double geom_w = 0.0;
    double box_h = 0.0;
    Memory mem_ov, mem_fs;   // memory to store old height and win y when in overview/fullscreen modes
    bool selected = false;
    SelectionBorders *decoration = nullptr;
    uint32_t grid_row = 0;  // Grid mode: which row this window belongs to

    // Clipping support: keep window at full size when inactive
    bool clip_when_inactive = false;
    double full_width = 0.0;
    double full_height = 0.0;
};

#endif // SCROLLER_WINDOW_H
