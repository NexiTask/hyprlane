#ifndef SCROLLER_COMMON_H
#define SCROLLER_COMMON_H

#include <hyprutils/math/Vector2D.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>

using Hyprutils::Math::Vector2D;
using Fullscreen::eFullscreenMode;

inline PHLWORKSPACE workspace_by_id(WORKSPACEID id) {
    return State::workspaceState()->query().id(id).run();
}

inline PHLMONITOR monitor_by_id(MONITORID id) {
    return State::monitorState()->query().id(id).run();
}

inline PHLMONITOR monitor_at(const Vector2D& point) {
    return State::monitorState()->query().vec(point).run();
}

inline PHLMONITOR monitor_in_direction(Math::eDirection direction) {
    const auto monitor = Desktop::focusState()->monitor();
    return monitor ? State::monitorState()->query().inDirection(direction).relativeTo(monitor).run() : nullptr;
}

inline Vector2D window_position(const PHLWINDOW& window) {
    return window ? window->position(Desktop::View::IGeometric::GEOMETRIC_GOAL) : Vector2D{};
}

inline Vector2D window_size(const PHLWINDOW& window) {
    return window ? window->size(Desktop::View::IGeometric::GEOMETRIC_GOAL) : Vector2D{};
}

inline void set_window_position(const PHLWINDOW& window, const Vector2D& position, bool warp = false) {
    if (!window)
        return;
    window->move(position);
    if (warp)
        window->positionAnimation()->warp(false);
}

inline void set_window_size(const PHLWINDOW& window, const Vector2D& size, bool warp = false) {
    if (!window)
        return;
    window->resize(size);
    if (warp)
        window->sizeAnimation()->warp(false);
}

struct Box {
    Box() : x(0), y(0), w(0), h(0) {}
    Box(double x_, double y_, double w_, double h_)
        : x(x_), y(y_), w(w_), h(h_) {}
    Box(Vector2D pos, Vector2D size)
        : x(pos.x), y(pos.y), w(size.x), h(size.y) {}
    Box(const Box &box)
        : x(box.x), y(box.y), w(box.w), h(box.h) {}
    Box& operator=(const Box&) = default;

    void set_size(double w_, double h_) {
        w = w_;
        h = h_;
    }
    void set_pos(double x_, double y_) {
        x = x_;
        y = y_;
    }
    bool operator==(const Box&) const = default;
    bool contains_point(const Vector2D &vec) const {
        if (vec.x >= x && vec.x < x + w && vec.y >= y && vec.y < y + h)
            return true;
        return false;
    }

    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
};

#endif // SCROLLER_COMMON_H
