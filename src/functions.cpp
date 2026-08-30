#include "functions.h"
#include "dispatchers.h"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>

#include <utility>

SDispatchResult this_moveFocusTo(std::string args)
{
    return dispatchers::dispatch_movefocus(std::move(args));
}


SDispatchResult this_moveActiveTo(std::string args)
{
    return dispatchers::dispatch_movewindow(std::move(args));
}


eFullscreenMode window_fullscreen_state(PHLWINDOW window)
{
    if (!window)
        return eFullscreenMode::FSMODE_NONE;
    return Fullscreen::controller()->getFullscreenModes(window).internal;
}

void toggle_window_fullscreen_internal(PHLWINDOW window, eFullscreenMode mode)
{
    if (!window)
        return;
    if (window_fullscreen_state(window) != eFullscreenMode::FSMODE_NONE) {
        Fullscreen::controller()->setFullscreenMode(window, Fullscreen::FSMODE_NONE);
    } else {
        Fullscreen::controller()->setFullscreenMode(window, mode);
    }
}

WORKSPACEID get_workspace_id()
{
    const auto monitor = Desktop::focusState()->monitor();
    if (!monitor)
        return WORKSPACE_INVALID;

    WORKSPACEID workspace_id;
    if (monitor->activeSpecialWorkspaceID()) {
        workspace_id = monitor->activeSpecialWorkspaceID();
    } else {
        workspace_id = monitor->activeWorkspaceID();
    }
    if (workspace_id == WORKSPACE_INVALID)
        return WORKSPACE_INVALID;
    if (workspace_by_id(workspace_id) == nullptr)
        return WORKSPACE_INVALID;

    return workspace_id;
}

void update_relative_cursor_coords(PHLWINDOW window)
{
    if (window != nullptr)
        window->m_relativeCursorCoordsOnLastWarp = g_pInputManager->getMouseCoordsInternal() - window_position(window);
}

void force_focus_to_window(PHLWINDOW window)
{
    if (!window)
        return;
    g_pInputManager->unconstrainMouse();
    Desktop::focusState()->fullWindowFocus(window, Desktop::FOCUS_REASON_OTHER);
    window->warpCursor();

    g_pInputManager->m_forcedFocus = window;
    g_pInputManager->simulateMouseMovement();
    g_pInputManager->m_forcedFocus.reset();
}

void switch_to_window(PHLWINDOW from, PHLWINDOW to)
{
    if (to == nullptr)
        return;

    auto fwid = from != nullptr? from->workspaceID() : WORKSPACE_INVALID;
    auto twid = to->workspaceID();
    bool change_workspace = fwid != twid;
    if (from != to) {
        const PHLWORKSPACE workspace = to->m_workspace;
        const eFullscreenMode mode = workspace ? Fullscreen::controller()->getFullscreenModes(workspace).internal
                                               : eFullscreenMode::FSMODE_NONE;
        if (mode != eFullscreenMode::FSMODE_NONE) {
            if (change_workspace) {
                auto fwindow = workspace->getLastFocusedWindow(); 
                toggle_window_fullscreen_internal(fwindow, eFullscreenMode::FSMODE_NONE);
            } else {
                toggle_window_fullscreen_internal(from, eFullscreenMode::FSMODE_NONE);
            }
        }
        if (change_workspace) {
            // This is to override overview trying to stay in an overview workspace
            if (const auto monitor = to->m_monitor.lock())
                Desktop::focusState()->rawMonitorFocus(monitor);
        }
        force_focus_to_window(to);
        if (mode != eFullscreenMode::FSMODE_NONE) {
            toggle_window_fullscreen_internal(to, mode);
        }
    } else {
        // from and to are the same, it can happen when we want to recover
        // focus after changing to another monitor where focus was lost
        // due to a window exiting in the background
        force_focus_to_window(to);
    }
    return;
}
