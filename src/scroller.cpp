#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/ViewState.hpp>
#include <hyprland/src/desktop/rule/Engine.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRule.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/devices/Keyboard.hpp>
#include <hyprland/src/managers/EventManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/pointer/PointerManager.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/helpers/math/Direction.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/managers/fullscreen/handler/FullscreenHandler.hpp>

#include "scroller.h"
#include "common.h"
#include "config.h"
#include "dispatchers.h"
#include "functions.h"
#include "row.h"
#include "column.h"
#include "overview.h"

#include <algorithm>
#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <thread>
#include <chrono>
#include <mutex>


extern HANDLE PHANDLE;
extern std::unique_ptr<ScrollerLayout> g_ScrollerLayout;
extern std::unique_ptr<Overview> overviews;

std::function<SDispatchResult(std::string)> orig_moveFocusTo;
std::function<SDispatchResult(std::string)> orig_moveActiveTo;

static std::chrono::steady_clock::time_point lastMouseTime;
static std::mutex mouseFocusMutex;
// Track if we should suppress focus layout for subsequent recalculations (global, accessible from row.cpp)
bool suppressFocusLayout = false;
// Track if focus should move without centering (for movefocus_nocenter dispatcher)
std::chrono::steady_clock::time_point lastNoCenterTime;
static std::chrono::steady_clock::time_point lastKeyboardFocusTime;

// An explicit window move is a keyboard/dispatcher action. Mark it as such so
// the focus change it triggers (force_focus_to_window warps the cursor and
// simulates mouse movement) is not mistaken for mouse-driven focus, which
// would set suppressFocusLayout and skip the relayout. Without this, rapid
// moves reorder the column list silently and the layout "teleports" later.
static void mark_explicit_move_action()
{
    {
        std::lock_guard<std::mutex> lock(mouseFocusMutex);
        lastKeyboardFocusTime = std::chrono::steady_clock::now();
    }
    suppressFocusLayout = false;
}

class Marks {
public:
    Marks() = default;
    ~Marks() = default;
    void reset() {
        marks.clear();
        post_mark_event(nullptr);
    }
    // Add a mark with name for window, overwriting any existing one with that name
    void add(PHLWINDOW window, const std::string &name) {
        const auto mark = marks.find(name);
        if (mark != marks.end()) {
            mark->second = window;
            post_mark_event(window);
            return;
        }
        marks[name] = window;
        post_mark_event(window);
    }
    void del(const std::string &name) {
        const auto mark = marks.find(name);
        if (mark != marks.end()) {
            if (Desktop::focusState()->window() == mark->second.lock())
                post_mark_event(nullptr);
            marks.erase(mark);
        }
    }
    // Remove window from list of marks (used when a window gets deleted)
    void remove(PHLWINDOW window) {
        for(auto it = marks.begin(); it != marks.end();) {
            if (it->second.lock() == window)
                it = marks.erase(it);
            else
                it++;
        }
    }
    // If the mark exists, returns that window, otherwise it returns null
    PHLWINDOW visit(const std::string &name) {
        const auto mark = marks.find(name);
        if (mark != marks.end()) {
            return mark->second.lock();
        }
        return nullptr;
    }

    void post_mark_event(PHLWINDOW window) {
        for(auto it = marks.begin(); it != marks.end(); it++) {
            if (it->second.lock() == window) {
                g_pEventManager->postEvent(SHyprIPCEvent{"scroller", std::format("mark, 1, {}", it->first)});
                return;
            }
        }
        g_pEventManager->postEvent(SHyprIPCEvent{"scroller", "mark, 0, "});
    }

private:
    std::unordered_map<std::string, PHLWINDOWREF> marks;
};

static Marks marks;

class Trail {
public:
    Trail(int number) : number(number), active(nullptr) {}
    ~Trail() = default;

protected:
    void toggle(const PHLWINDOW window) {
        auto win = marks.first();
        while (win != nullptr) {
            auto next = win->next();
            if (win->data() == window) {
                if (active == win)
                    active = win != marks.last() ? win->next() : win->prev();
                marks.erase(win);
                return;
            }
            win = next;
        }
        if (active == nullptr) {
            marks.push_back(window);
            active = marks.first();
        } else {
            marks.insert_after(active, window);
            active = active->next();
        }
    }
    void remove_window(PHLWINDOW window) {
        auto win = marks.first();
        while (win != nullptr) {
            auto next = win->next();
            if (win->data() == window) {
                if (active == win)
                    active = win != marks.last() ? win->next() : win->prev();
                marks.erase(win);
                return;
            }
            win = next;
        }
    }
    void next() {
        if (active == nullptr)
            return;
        active = active == marks.last() ? marks.first() : active->next();
    }
    void prev() {
        if (active == nullptr)
            return;
        active = active == marks.first() ? marks.last() : active->prev();
    }
    void clear() {
        marks.clear();
        active = nullptr;
    }
    bool is_marked(PHLWINDOW window) const {
        for (auto win = marks.first(); win != nullptr; win = win->next()) {
            if (win->data() == window)
                return true;
        }
        return false;
    }
    void toselection() const {
        for (auto win = marks.first(); win != nullptr; win = win->next()) {
            g_ScrollerLayout->selection_set(win->data());
        }
        // Re-render windows to show decorations
        for (auto monitor : State::monitorState()->monitors()) {
            g_pHyprRenderer->damageMonitor(monitor);
        }
    }

private:
    friend class Trails;

    int number;
    ListNode<const PHLWINDOWREF> *active;
    List<const PHLWINDOWREF> marks;
};

class Trails {
public:
    Trails() : counter(0), active(nullptr) {
        //trail_new();
    }
    ~Trails() {
        active = nullptr;
        trails.clear();
        post_trailmark_event(nullptr);
        post_trail_event();
    }
    void remove_window(PHLWINDOW window) {
        for (auto trail = trails.first(); trail != nullptr; trail = trail->next()) {
            trail->data()->remove_window(window);
        }
        post_trail_event();
    }

    size_t get_active_size() const {
        return active ? active->data()->marks.size() : 0;
    }
    int get_active_number() const {
        return active ? active->data()->number : -1;
    }
    bool get_active_marked(PHLWINDOW window) const {
        return active ? active->data()->is_marked(window) : false;
    }
    PHLWINDOW get_active() const {
        if (active == nullptr) {
            return nullptr;
        } else {
            const auto& mark = active->data();
            if (mark->active != nullptr) {
                return mark->active->data().lock();
            } else {
                return nullptr;
            }
        }
    }
    void trail_new() {
        trails.push_back(std::make_unique<Trail>(counter++));
        active = trails.last();
        post_trail_event();
    }
    void trail_next() {
        active = active == trails.last() ? trails.first() : active->next();
        post_trail_event();
    }
    void trail_prev() {
        active = active == trails.first() ? trails.last() : active->prev();
        post_trail_event();
    }
    void trail_delete() {
        if (active == nullptr)
            return;
        auto act = active == trails.first() ? active->next() : active->prev();
        trails.erase(active);
        active = act;
        post_trail_event();
    }
    void trail_clear() {
        if (active == nullptr)
            return;
        active->data()->clear();
        post_trail_event();
    }

    void trail_toselection() {
        if (active == nullptr)
            return;
        active->data()->toselection();
    }

    void trailmark_toggle(PHLWINDOW window) {
        if (active == nullptr) {
            trail_new();
        }
        active->data()->toggle(window);
        post_trailmark_event(window);
        post_trail_event();
    }
    void trailmark_next() {
        if (active == nullptr)
            return;
        active->data()->next();
    }
    void trailmark_prev() {
        if (active == nullptr)
            return;
        active->data()->prev();
    }

    void post_trail_event() {
        g_pEventManager->postEvent(SHyprIPCEvent{"scroller", std::format("trail, {}, {}", get_active_number(), get_active_size())});
    }
    void post_trailmark_event(PHLWINDOW window) {
        bool marked = false;
        if (active != nullptr && active->data()->is_marked(window))
            marked = true;
        g_pEventManager->postEvent(SHyprIPCEvent{"scroller", std::format("trailmark, {}", marked ? 1 : 0)});
    }

private:
    int counter;
    ListNode<std::unique_ptr<Trail>> *active;
    List<std::unique_ptr<Trail>> trails;
};

static std::unique_ptr<Trails> trails;

// ScrollerLayout
ScrollerLayout::ScrollerLayout() = default;
ScrollerLayout::~ScrollerLayout() = default;

Row *ScrollerLayout::getRowForWorkspace(WORKSPACEID workspace) {
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        if (row->data()->get_workspace() == workspace)
            return row->data().get();
    }
    return nullptr;
}

Row *ScrollerLayout::getRowForWindow(PHLWINDOW window) {
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        if (row->data()->has_window(window))
            return row->data().get();
    }
    return nullptr;
}

void ScrollerLayout::enforceClipZOrder(Row *row) {
    if (!row)
        return;

    const auto activeWindow = row->get_active_window();
    if (!activeWindow)
        return;

    std::vector<PHLWINDOWREF> windows;
    row->get_windows(windows);

    bool hasInactiveClippedWindow = false;
    for (const auto& windowRef : windows) {
        const auto window = windowRef.lock();
        if (!window || window == activeWindow || !window->m_ruleApplicator)
            continue;
        if (!window->m_ruleApplicator->m_tagKeeper.isTagged("scroller:clip_when_inactive"))
            continue;

        Desktop::windowState()->raise(window);
        hasInactiveClippedWindow = true;
    }

    if (!hasInactiveClippedWindow)
        return;

    // Clipped windows retain their full geometry and overlap their neighbor.
    // Keep the row's logical active window above them even when this row loses
    // monitor focus and Hyprland adjusts the global window stack.
    Desktop::windowState()->raise(activeWindow);
    if (const auto monitor = activeWindow->m_monitor.lock())
        g_pHyprRenderer->damageMonitor(monitor);
}

/*
    Called when a window is created (mapped)
    The layout HAS TO set the goal pos and size (anim mgr will use it)
    If !animationinprogress, then the anim mgr will not apply an anim.
*/
void ScrollerLayout::onWindowCreatedTiling(PHLWINDOW window)
{
    if (!window)
        return;

    // Guard against duplicate adds
    if (getRowForWindow(window) != nullptr)
        return;

    WORKSPACEID wid = window->workspaceID();
    if (wid == WORKSPACE_INVALID)
        return;

    // Get the correct monitor for this window
    // Try workspace first (more reliable), then fall back to window's monitor
    PHLMONITOR targetMonitor = nullptr;
    auto workspace = window->m_workspace;
    if (workspace) {
        targetMonitor = workspace->m_monitor.lock();
    }
    if (!targetMonitor) {
        targetMonitor = window->m_monitor.lock();
    }
    if (!targetMonitor) {
        targetMonitor = Desktop::focusState()->monitor();
    }

    // Final safety check
    if (!targetMonitor) {
        Log::logger->log(Log::ERR, "[SCROLLER] onWindowCreatedTiling: no valid monitor found");
        return;
    }

    auto s = getRowForWorkspace(wid);
    if (s == nullptr) {
        auto row = std::make_unique<Row>(wid, targetMonitor);
        s = row.get();
        rows.push_back(std::move(row));
    }

    // Ensure the Row has correct geometry for the target monitor
    s->update_sizes(targetMonitor);

    // Update window data
    window->updateWindowData();

    // Note: Hyprland 0.53+ uses tag-based window rules for plugin features
    // Users should use: windowrule = tag +scroller:clip_when_inactive, class:...
    // Tags are already applied by Hyprland before onWindowCreatedTiling is called

    s->add_active_window(window);

    // Ensure window is visible and has correct geometry
    window->setHidden(false);

    // CRITICAL: Temporarily clear suppressFocusLayout for window creation
    // This flag gets set during mouse-triggered focus (like tab drag) and blocks geometry calculation
    // Without this, cross-monitor window creation (e.g. Firefox tab drag) leaves windows invisible
    bool savedSuppress = suppressFocusLayout;
    suppressFocusLayout = false;

    // Force the row to recalculate geometry for this window
    s->recalculate_row_geometry();

    // Also call recalculateMonitor for complete update
    recalculateMonitor(window->monitorID());

    // Restore the suppress flag
    suppressFocusLayout = savedSuppress;

    // Damage the monitor to ensure rendering
    if (auto monitor = window->m_monitor.lock()) {
        g_pHyprRenderer->damageMonitor(monitor);
    }
}

/*
    Called when a window is removed (unmapped) (m_isMapped still true), and
    then again when the window is destroyed.
    Some XWayland windows only call it once, at destroy, but those
    windows are not in the layout and are not floating either. For example Qt
    tooltips with a XWayland backend. What are they?
    Remove the window from the layout and re-focus in one call, so we can
    ignore windows that don't belong to the layout. However, if the window is
    removed because it became floating, we don't want to change focus to a
    tiled window, just remove it from the layout and let it keep focus.
*/
void ScrollerLayout::onWindowRemovedTiling(PHLWINDOW window)
{
    if (window && (window == clipHoverWindow.lock() || window == clipHoverMainWindow.lock()))
        endClipHover(false);

    auto s = getRowForWindow(window);
    if (s == nullptr)
        return;

    marks.remove(window);
    if (trails)
        trails->remove_window(window);

    // CRITICAL: Temporarily clear suppressFocusLayout for window removal
    // This ensures the remaining window(s) get properly laid out and centered
    // Same issue as window creation - mouse-triggered closes leave this flag true
    bool savedSuppress = suppressFocusLayout;
    suppressFocusLayout = false;

    if (!s->remove_window(window)) {
        // It was the last one, remove the row
        for (auto row = rows.first(); row != nullptr; row = row->next()) {
            if (row->data().get() == s) {
                rows.erase(row);
                break;
            }
        }
    }

    // Restore the suppress flag
    suppressFocusLayout = savedSuppress;

    // In v0.54, the layout manager handles focus transitions after removal
    // via getNextCandidate(). Do NOT call force_focus_to_window() here —
    // it crashes because warpCursor() is invalid during the removal flow.
}

/*
    Called when a floating window is removed (unmapped)
*/
void ScrollerLayout::onWindowRemovedFloating(PHLWINDOW window)
{
    if (g_scrollerConfig.avoid_focus_on_float_close()) {
        return;
    }
    if (window && window->m_isX11 && g_scrollerConfig.avoid_focus_on_xwayland_float_close()) {
        // Avoid automatic focus switch when an XWayland floating window is removed,
        // to prevent input method losing focus
        return;
    }
    const auto monitor = Desktop::focusState()->monitor();
    if (!monitor)
        return;

    WORKSPACEID workspace_id = monitor->activeSpecialWorkspaceID();
    if (!workspace_id) {
        workspace_id = monitor->activeWorkspaceID();
    }
    auto s = getRowForWorkspace(workspace_id);
    if (s != nullptr)
        Desktop::focusState()->fullWindowFocus(s->get_active_window(), Desktop::FOCUS_REASON_OTHER);
}

/*
    Internal: called when window focus changes
*/
void ScrollerLayout::onWindowFocusChange(PHLWINDOW window, Desktop::eFocusReason reason)
{
    Log::logger->log(Log::DEBUG, "[SCROLLER] onWindowFocusChange called, window = {}", (void*)window.get());

    auto s = getRowForWindow(window);
    const auto rowActive = s ? s->get_active_window() : nullptr;
    const bool hasClipTag = window && window->m_ruleApplicator &&
        window->m_ruleApplicator->m_tagKeeper.isTagged("scroller:clip_when_inactive");
    const auto hoverWindow = clipHoverWindow.lock();

    // Hover (FFM) on a clipped, non-active window must not take over at all:
    // no layout change, no active-column change. The window stays clipped and
    // only a real click (FOCUS_REASON_CLICK) commits the takeover below.
    // Hover focus is deliberately transient. Keep the row's logical active
    // window unchanged so leaving the hovered clipped window can restore it.
    if (hoverWindow) {
        Log::logger->log(Log::DEBUG, "[CLIP] Hover focus on clipped window ignored");
        return;
    }
    if (window) {
        const auto previousWindow = lastFocusedWindow.lock();
        lastFocusedWindow = window;

        if (previousWindow && previousWindow != window && previousWindow->monitorID() != window->monitorID()) {
            Log::logger->log(Log::DEBUG, "[CLIP] Focus moved to another monitor; restoring previous row z-order");
            enforceClipZOrder(getRowForWindow(previousWindow));
        }
    }

    if (window == nullptr) { // no window has focus
        Log::logger->log(Log::DEBUG, "[SCROLLER] window is nullptr, returning");
        return;
    }

    if (s == nullptr) {
        Log::logger->log(Log::DEBUG, "[SCROLLER] getRowForWindow returned nullptr (window not managed by scroller), returning");
        return;
    }

    Log::logger->log(Log::DEBUG, "[SCROLLER] Found row for window, checking mouse/keyboard triggers");

    // Check if this is a mouse-triggered focus change (within 200ms of mouse movement)
    const bool mouse_focus_disabled = g_scrollerConfig.focus_layout_mouse_disable();

    bool isMouseTriggered = false;
    bool isKeyboardTriggered = false;
    {
        std::lock_guard<std::mutex> lock(mouseFocusMutex);
        auto currentTime = std::chrono::steady_clock::now();

        // First check if a keyboard focus dispatcher was called recently (within 100ms)
        // This handles cursor warping - keyboard focus + cursor warp can look like mouse movement
        auto timeSinceKeyboard = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastKeyboardFocusTime);
        if (timeSinceKeyboard.count() < 100) {
            isKeyboardTriggered = true;
        }

        // Then check mouse movement timing
        auto timeSinceMouse = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastMouseTime);
        if (timeSinceMouse.count() < 200 && !isKeyboardTriggered) {
            isMouseTriggered = true;
        }
    }

    // A click is the explicit commit gesture: it outranks the movement-timestamp
    // heuristic, otherwise with focus_layout_mouse_disable=1 the clicked window
    // only LOOKS active (z-order) while the row's active column stays put and
    // any later hover "reverts" to it.
    const bool commitClick = reason == Desktop::FOCUS_REASON_CLICK;
    bool apply_focus_layout = commitClick || !(mouse_focus_disabled && isMouseTriggered);

    // Check if nocenter focus was recently triggered
    auto timeSinceNoCenter = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - lastNoCenterTime);
    if (!commitClick && timeSinceNoCenter.count() < 200) {
        apply_focus_layout = false;
    }

    Log::logger->log(Log::DEBUG, "[SCROLLER] isMouseTriggered={}, isKeyboardTriggered={}, apply_focus_layout={}",
              isMouseTriggered, isKeyboardTriggered, apply_focus_layout);

    // CLIP FIX: Hover (FFM) on a clipped, non-active window must not take
    // over. Hyprland has already focused/raised it by the time this callback
    // runs, so bounce focus back to the row-active window and repair z-order.
    // A real click arrives as FOCUS_REASON_CLICK and commits the takeover via
    // the normal path below.
    PHLWINDOW activeWindow = s->get_active_window();
    if (reason == Desktop::FOCUS_REASON_FFM && hasClipTag && activeWindow && activeWindow != window) {
        // Only bounce when the pointer sits in the active window's exposed
        // (non-overlapped) region. After a click-takeover the old active
        // window is clipped on the right and its retained geometry covers
        // most of the new center; hovering that covered region must stay
        // inert so the overtaken window remains fully interactive. Switching
        // back requires reaching the old window's exposed right slice.
        const Vector2D mousePos = g_pInputManager->getMouseCoordsInternal();
        const CBox activeBox = {window_position(activeWindow), window_size(activeWindow)};
        CBox exposedBox = activeBox;
        std::vector<PHLWINDOWREF> rowWindows;
        s->get_windows(rowWindows);
        for (const auto &winRef : rowWindows) {
            const auto colWindow = winRef.lock();
            if (!colWindow || colWindow == activeWindow || !colWindow->m_ruleApplicator ||
                !colWindow->m_ruleApplicator->m_tagKeeper.isTagged("scroller:clip_when_inactive"))
                continue;
            const CBox colBox = {window_position(colWindow), window_size(colWindow)};
            // A clipped LEFT neighbor hides the left part of the active box.
            if (colBox.x < activeBox.x &&
                colBox.y <= mousePos.y && mousePos.y <= colBox.y + colBox.h)
                exposedBox.x = std::max(exposedBox.x, colBox.x + colBox.w);
        }

        if (!exposedBox.containsPoint(mousePos)) {
            enforceClipZOrder(s);

            // Re-focus the active window
            Desktop::focusState()->fullWindowFocus(activeWindow, Desktop::FOCUS_REASON_OTHER);
        }
        return;
    }

    // Update the global flag for subsequent recalculations
    suppressFocusLayout = !apply_focus_layout;

    // If this was detected as mouse-triggered, update lastMouseTime so rapid mouse movements
    // chain together properly (otherwise fast mouse movement can trigger keyboard focus)
    if (isMouseTriggered && mouse_focus_disabled) {
        std::lock_guard<std::mutex> lock(mouseFocusMutex);
        lastMouseTime = std::chrono::steady_clock::now();
    }

    Log::logger->log(Log::DEBUG, "[SCROLLER] Calling s->focus_window() with apply_focus_layout={}", apply_focus_layout);
    s->focus_window(window, apply_focus_layout);
    // After a committed takeover (click/keyboard/dispatcher) the previous
    // active window becomes a clipped neighbor that may retain stale stack
    // precedence from when it was on top. Reassert the z-order contract.
    if (s->get_active_window() == window)
        enforceClipZOrder(s);
    Log::logger->log(Log::DEBUG, "[SCROLLER] Returned from s->focus_window()");
}

/*
    Return tiled status
*/
bool ScrollerLayout::isWindowTiled(PHLWINDOW window)
{
    return getRowForWindow(window) != nullptr;
}

/*
    Called when the monitor requires a layout recalculation
    this usually means reserved area changes
*/
void ScrollerLayout::recalculateMonitor(const MONITORID &monitor_id)
{
    const auto PMONITOR = monitor_by_id(monitor_id);
    if (!PMONITOR)
        return;

    // Proactively hide admitted windows on all non-active workspaces before rendering
    WORKSPACEID activeWID = PMONITOR->activeWorkspaceID();
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        if (row->data()->get_workspace() != activeWID) {
            // This row is on a non-active workspace - force hide its admitted windows now
            row->data()->recalculate_row_geometry();
        }
    }

    g_pHyprRenderer->damageMonitor(PMONITOR);

    WORKSPACEID specialID = PMONITOR->activeSpecialWorkspaceID();
    if (specialID) {
        auto sw = getRowForWorkspace(specialID);
        if (sw == nullptr) {
            return;
        }
        const Box oldmax = sw->get_max();
        const bool force = sw->update_sizes(PMONITOR);
        auto PWORKSPACESPECIAL = PMONITOR->m_activeSpecialWorkspace;
        if (Fullscreen::controller()->hasFullscreen(PWORKSPACESPECIAL)) {
            sw->set_fullscreen_mode_windows(Fullscreen::controller()->getFullscreenModes(PWORKSPACESPECIAL).internal);
        } else {
            sw->update_windows(oldmax, force);
        }
    }

    auto PWORKSPACE = PMONITOR->m_activeWorkspace;
    if (!PWORKSPACE)
        return;

    auto s = getRowForWorkspace(PWORKSPACE->m_id);
    if (s == nullptr)
        return;

    const Box oldmax = s->get_max();
    const bool force = s->update_sizes(PMONITOR);
    if (Fullscreen::controller()->hasFullscreen(PWORKSPACE)) {
        s->set_fullscreen_mode_windows(Fullscreen::controller()->getFullscreenModes(PWORKSPACE).internal);
    } else {
        s->update_windows(oldmax, force);
        // If sizes didn't change (force=false), still recalculate to update admitted window visibility and focus layout
        if (!force) {
            s->recalculate_row_geometry();
        }
    }
}

void ScrollerLayout::recalculateWorkspace(WORKSPACEID wid)
{
    auto s = getRowForWorkspace(wid);
    if (s == nullptr)
        return;

    auto workspace = workspace_by_id(wid);
    if (!workspace)
        return;

    auto monitor = workspace->m_monitor.lock();
    if (!monitor)
        return;

    const Box oldmax = s->get_max();
    const bool force = s->update_sizes(monitor);
    s->update_windows(oldmax, force);
    if (!force) {
        s->recalculate_row_geometry();
    }
}

/*
    Called when the compositor requests a window
    to be recalculated, e.g. when pseudo is toggled or reserved area changes.
*/
void ScrollerLayout::recalculateWindow(PHLWINDOW window)
{
    // It can get called after windows are already being destroyed (decorations update)
    if (!enabled)
        return;

    auto s = getRowForWindow(window);
    if (s == nullptr)
        return;

    // Must run update_sizes first so max box reflects current reservedArea/gaps
    // (fixes padding when notifications or layer surfaces change reserved area)
    if (auto mon = window->m_monitor.lock()) {
        const Box oldmax = s->get_max();
        const bool force = s->update_sizes(mon);
        s->update_windows(oldmax, force);
    }
    // Respect the suppress flag set by mouse-triggered focus changes
    s->recalculate_row_geometry(!suppressFocusLayout);
}

/*
    Called when a user requests a resize of the current window by a vec
    Vector2D holds pixel values
    Optional pWindow for a specific window
*/
void ScrollerLayout::resizeActiveWindow(const Vector2D &delta,
                                        Layout::eRectCorner /* corner */, PHLWINDOW window)
{
    const auto PWINDOW = window ? window : Desktop::focusState()->window();
    if (!PWINDOW)
        return;
    auto s = getRowForWindow(PWINDOW);
    if (s == nullptr) {
        // Window is not tiled
        const auto size = window_size(PWINDOW) + delta;
        ::set_window_size(PWINDOW, Vector2D(std::max(size.x, 20.0), std::max(size.y, 20.0)));
        PWINDOW->sendWindowSize();
        PWINDOW->updateWindowDecos();
        return;
    }

    s->resize_active_window(delta);
}

/*
   Called when a window / the user requests to toggle the fullscreen state of a
   window. The layout sets all the fullscreen flags. It can either accept or
   ignore.
*/
void ScrollerLayout::fullscreenRequestForWindow(PHLWINDOW window,
                                                const eFullscreenMode CURRENT_EFFECTIVE_MODE,
                                                const eFullscreenMode EFFECTIVE_MODE)
{
    auto s = getRowForWindow(window);

    if (s == nullptr) {
        // Floating window fullscreen is handled by the layout manager
        return;
    } else {
        if (EFFECTIVE_MODE == CURRENT_EFFECTIVE_MODE)
            return;
        s->set_fullscreen_mode(window, CURRENT_EFFECTIVE_MODE, EFFECTIVE_MODE);
    }
    Desktop::windowState()->raise(window);
}

void ScrollerLayout::switchWindows(PHLWINDOW, PHLWINDOW)
{
}

/*
    Called when the user requests a window move in a direction.
    The layout is free to ignore.
*/
void ScrollerLayout::moveWindowTo(PHLWINDOW window, const std::string &direction, bool /* silent */)
{
    if (!window || direction.empty())
        return;
    mark_explicit_move_action();
    auto s = getRowForWindow(window);
    if (s == nullptr) {
        return;
    } else if (!(s->is_active(window))) {
        // cannot move non active window?
        return;
    }

    switch (direction.at(0)) {
        case 'l': s->move_active_column(Direction::Left); break;
        case 'r': s->move_active_column(Direction::Right); break;
        case 'u': s->move_active_column(Direction::Up); break;
        case 'd': s->move_active_column(Direction::Down); break;
        default: break;
    }

    // "silent" requires to keep focus in the neighborhood of the moved window
    // before it moved. I ignore it for now.
}


/*
    Called when something wants the current layout's name
*/
std::string ScrollerLayout::getLayoutName()
{
    return "scroller";
}

/*
    Called for getting the next candidate for a focus
*/
PHLWINDOW ScrollerLayout::getNextWindowCandidate(PHLWINDOW/* old_window */)
{
    // This is called when a windows in unmapped. This means the window
    // has also been removed from the layout. In that case, returning the
    // new active window is the correct thing.
    // We would like to be able to retain the full screen mode for old_window's
    // workspace if it was a different one than the current one (background
    // window unmapped), but old_window has had its fsmode removed in
    // Hyprland's /src/events/Windows.cpp
    // void Events::listener_unmapWindow(void* owner, void* data);
    // so it is impossible to know the old state short of storing it ourselves
    // in Row, because WORKSPACE has also lost it. Storing it in Row is hard
    // to keep synchronized. So for now, unmapping a window from a workspace
    // different than the active one, loses full screen state.
    const auto monitor = Desktop::focusState()->monitor();
    if (!monitor)
        return nullptr;

    WORKSPACEID workspace_id = monitor->activeSpecialWorkspaceID();
    if (!workspace_id) {
        workspace_id = monitor->activeWorkspaceID();
    }
    auto s = getRowForWorkspace(workspace_id);
    if (s == nullptr)
        return nullptr;
    else
        return s->get_active_window();
}


static CHyprSignalListener workspaceHookCallback;
static CHyprSignalListener focusedMonHookCallback;
static CHyprSignalListener monitorLayoutChangedHookCallback;
static CHyprSignalListener activeWindowHookCallback;
static CHyprSignalListener fullscreenHookCallback;
static CHyprSignalListener layerOpenedHookCallback;
static CHyprSignalListener layerClosedHookCallback;
static CHyprSignalListener swipeBeginHookCallback;
static CHyprSignalListener swipeUpdateHookCallback;
static CHyprSignalListener swipeEndHookCallback;
static CHyprSignalListener mouseMoveHookCallback;

// Hook Hyprland's window hit tester to prioritize the row-active window over
// the overlapping full geometry of clipped neighbors.
static CFunctionHook* g_pWindowAtHook = nullptr;
typedef PHLWINDOW (*origWindowAt)(void*, const Vector2D&, uint16_t, PHLWINDOW);

static PHLWINDOW hookWindowAt(void* thisptr, const Vector2D& pos, uint16_t properties, PHLWINDOW pIgnoreWindow) {
    // Call the original function first
    PHLWINDOW result = ((origWindowAt)g_pWindowAtHook->m_original)(thisptr, pos, properties, pIgnoreWindow);

    // If no window found or scroller layout not active, return original result
    if (!result || !g_ScrollerLayout || !g_ScrollerLayout->is_enabled()) {
        return result;
    }

    // During a hover session the hovered window stays the hit target across
    // its full geometry, but a click on it commits the takeover, so plain
    // hover hit-testing must not redirect clicks away either.
    auto hoverWindow = g_ScrollerLayout->getClipHoverWindow();
    if (hoverWindow && !result->m_isFloating && g_ScrollerLayout->clipHoverOwnsPoint(pos)) {
        return hoverWindow;
    }

    // Check if the found window has clip_when_inactive tag - if not, return as-is
    if (!result->m_ruleApplicator || !result->m_ruleApplicator->m_tagKeeper.isTagged("scroller:clip_when_inactive")) {
        return result;
    }

    // Only redirect hits for genuine tiled clipped neighbors managed by the scroller.
    // VSCode-based editors (Kiro, Cursor, ...) apply clip_when_inactive by window class,
    // so their popup/modal dialogs inherit the tag too. Such a dialog is floating and is
    // rendered on top of (and inside) its parent's box, so the containsPoint() check below
    // would wrongly steal every click away from it, making the dialog unselectable.
    // A real clipped neighbor is an admitted window living in a column, hence isWindowTiled().
    if (result->m_isFloating || !g_ScrollerLayout->isWindowTiled(result)) {
        return result;
    }

    // Once a clipped window has transient hover focus, its full geometry is
    // intentionally interactive until the pointer leaves it.
    if (result == g_ScrollerLayout->getClipHoverWindow()) {
        return result;
    }

    // Use the row's logical active window rather than the globally focused
    // window. When focus is on another monitor, the global window belongs to
    // another workspace and cannot resolve overlap on this row. Returning the
    // row-active window here prevents the clipped neighbor from receiving a
    // transient focus/click before the focus callback repairs the z-order.
    PHLWINDOW activeWindow = g_ScrollerLayout->getActiveWindow(result->workspaceID());
    if (!activeWindow || activeWindow == result) {
        return result;
    }

    if (activeWindow->m_isFloating || !activeWindow->m_isMapped || activeWindow->isHidden()) {
        return result;
    }

    CBox activeBox = {window_position(activeWindow), window_size(activeWindow)};

    if (activeBox.containsPoint(pos)) {
        return activeWindow;
    }

    return result;
}

void ScrollerLayout::onEnable() {
    if (enabled)
        return;

    swipe_active = false;
    swipe_triggered = false;
    gesture_delta = {};
    swipe_direction = Direction::Begin;

    // Managers must exist before listeners are connected because a focus event
    // can be delivered as soon as the subscriptions become active.
    overviews = std::make_unique<Overview>();
    trails = std::make_unique<Trails>();
    marks.reset();

    lastFocusedWindow = Desktop::focusState()->window();

    // Hijack Hyprland's default dispatchers
    orig_moveFocusTo = g_pKeybindManager->m_dispatchers["movefocus"];
    orig_moveActiveTo = g_pKeybindManager->m_dispatchers["movewindow"];
    g_pKeybindManager->m_dispatchers["movefocus"] = this_moveFocusTo;
    g_pKeybindManager->m_dispatchers["movewindow"] = this_moveActiveTo;

    // Register event listeners
    workspaceHookCallback = Event::bus()->m_events.workspace.active.listen([this](PHLWORKSPACE ws) {
        endClipHover(false);
        if (!ws)
            return;
        post_event(ws->m_id, "mode");
        post_event(ws->m_id, "overview");
        if (auto mon = ws->m_monitor.lock())
            g_layoutManager->recalculateMonitor(mon);
    });
    focusedMonHookCallback = Event::bus()->m_events.monitor.focused.listen([this](PHLMONITOR monitor) {
        endClipHover(false);
        if (!monitor)
            return;
        post_event(monitor->activeWorkspaceID(), "mode");
        post_event(monitor->activeWorkspaceID(), "overview");
        g_layoutManager->recalculateMonitor(monitor);
    });
    monitorLayoutChangedHookCallback = Event::bus()->m_events.monitor.layoutChanged.listen([]() {
        // Monitor scale/mode changes are structural, not mouse-focus changes.
        // Never let a stale hover-suppression flag block their geometry update.
        suppressFocusLayout = false;
        for (const auto& monitor : State::monitorState()->monitors()) {
            if (monitor) {
                const auto reservedArea = monitor->m_reservedArea;
                g_layoutManager->invalidateMonitorGeometries(monitor);
                monitor->m_reservedArea = reservedArea;
                for (const auto& workspace : State::workspaceState()->workspaces()) {
                    if (workspace && workspace->monitorID() == monitor->m_id && workspace->m_space)
                        workspace->m_space->recheckWorkArea();
                }
                g_layoutManager->recalculateMonitor(monitor);
            }
        }
    });
    activeWindowHookCallback = Event::bus()->m_events.window.active.listen([this](PHLWINDOW window, Desktop::eFocusReason reason) {
        if (trails)
            trails->post_trailmark_event(window);
        marks.post_mark_event(window);
        onWindowFocusChange(window, reason);
        if (window) {
            if (auto mon = window->m_monitor.lock())
                g_layoutManager->recalculateMonitor(mon);
        }
    });
    // Recalculate when layer surfaces (notifications, panels) open/close - reserved area changes
    layerOpenedHookCallback = Event::bus()->m_events.layer.opened.listen([](PHLLS layer) {
        if (layer) {
            if (auto mon = layer->m_monitor.lock()) {
                suppressFocusLayout = false;
                g_layoutManager->recalculateMonitor(mon);
            }
        }
    });
    layerClosedHookCallback = Event::bus()->m_events.layer.closed.listen([](PHLLS layer) {
        if (layer) {
            if (auto mon = layer->m_monitor.lock()) {
                suppressFocusLayout = false;
                g_layoutManager->recalculateMonitor(mon);
            }
        }
    });
    // Hyprland 0.56 fullscreen goes through IFullscreenHandler, not
    // ScrollerLayout::fullscreenRequestForWindow. After exit, a leftover
    // mouse-focus suppress flag (focus_layout_mouse_disable) makes
    // recalculate_row_geometry a no-op and the window stays monitor-sized.
    fullscreenHookCallback = Event::bus()->m_events.window.fullscreen.listen([](PHLWINDOW window) {
        if (!g_ScrollerLayout || !g_ScrollerLayout->is_enabled() || !window)
            return;
        if (Fullscreen::controller()->getFullscreenModes(window).internal != eFullscreenMode::FSMODE_NONE)
            return;
        dispatchers::dispatch_layout_message("recalculate warp");
    });

    swipeBeginHookCallback = Event::bus()->m_events.gesture.swipe.begin.listen([this](IPointer::SSwipeBeginEvent swipe_event, Event::SCallbackInfo& /* info */) {
        swipe_begin(swipe_event);
    });

    swipeUpdateHookCallback = Event::bus()->m_events.gesture.swipe.update.listen([this](IPointer::SSwipeUpdateEvent swipe_event, Event::SCallbackInfo& info) {
        swipe_update(info, swipe_event);
    });

    swipeEndHookCallback = Event::bus()->m_events.gesture.swipe.end.listen([this](IPointer::SSwipeEndEvent swipe_event, Event::SCallbackInfo& info) {
        swipe_end(info, swipe_event);
    });

    mouseMoveHookCallback = Event::bus()->m_events.input.mouse.move.listen([this](Vector2D mousePos, Event::SCallbackInfo& info) {
        mouse_move(info, mousePos);
    });

    // Hyprland 0.56 routes pointer hit-testing through
    // Desktop::CViewHitTester::windowAt.
    void* windowAtAddress = nullptr;
    for (const auto& function : HyprlandAPI::findFunctionsByName(PHANDLE, "windowAt")) {
        if (function.demangled.find("CViewHitTester::windowAt") != std::string::npos) {
            windowAtAddress = function.address;
            break;
        }
    }
    if (windowAtAddress) {
        g_pWindowAtHook = HyprlandAPI::createFunctionHook(PHANDLE, windowAtAddress, (void*)&hookWindowAt);
        if (g_pWindowAtHook && !g_pWindowAtHook->hook()) {
            Log::logger->log(Log::ERR, "[hyprlane] Failed to enable CViewHitTester::windowAt hook");
            HyprlandAPI::removeFunctionHook(PHANDLE, g_pWindowAtHook);
            g_pWindowAtHook = nullptr;
        } else if (!g_pWindowAtHook) {
            Log::logger->log(Log::ERR, "[hyprlane] Failed to create CViewHitTester::windowAt hook");
        }
    } else {
        Log::logger->log(Log::ERR, "[hyprlane] CViewHitTester::windowAt not found; clipped-window mouse routing is disabled");
    }

    enabled = true;
    // In v0.54, the layout manager calls newTarget() for each window when
    // the workspace switches to this algorithm. Do NOT manually add windows
    // here — that causes duplicates that lead to stale weak pointers.
}

void ScrollerLayout::onDisable() {
    if (!enabled)
        return;

    enabled = false;
    // Jump mode temporarily enables overview on participating workspaces.
    // Restore that state while the rows and hook infrastructure still exist.
    cancelJump(true);

    // Restore Hyprland's default dispatchers
    g_pKeybindManager->m_dispatchers["movefocus"] = orig_moveFocusTo;
    g_pKeybindManager->m_dispatchers["movewindow"] = orig_moveActiveTo;

    // Unregister event listeners
    workspaceHookCallback.reset();
    focusedMonHookCallback.reset();
    monitorLayoutChangedHookCallback.reset();
    activeWindowHookCallback.reset();
    fullscreenHookCallback.reset();
    layerOpenedHookCallback.reset();
    layerClosedHookCallback.reset();
    swipeBeginHookCallback.reset();
    swipeUpdateHookCallback.reset();
    swipeEndHookCallback.reset();
    mouseMoveHookCallback.reset();
    endClipHover(false);
    swipe_active = false;
    swipe_triggered = false;
    gesture_delta = {};
    swipe_direction = Direction::Begin;

    // Remove window hit-test hook
    if (g_pWindowAtHook != nullptr) {
        HyprlandAPI::removeFunctionHook(PHANDLE, g_pWindowAtHook);
        g_pWindowAtHook = nullptr;
    }

    overviews.reset();
    rows.clear();
    marks.reset();
    trails.reset();
}

/*
    Called to predict the size of a newly opened window to send it a configure.
    Return 0,0 if unpredictable
*/
Vector2D ScrollerLayout::predictSizeForNewWindowTiled() {
    const auto monitor = Desktop::focusState()->monitor();
    if (!monitor)
        return {};

    WORKSPACEID workspace_id = monitor->activeWorkspaceID();
    auto s = getRowForWorkspace(workspace_id);
    if (s == nullptr) {
        Vector2D size = monitor->m_size;
        size.x *= 0.5;
        return size;
    }

    return s->predict_window_size();
}

void ScrollerLayout::cycle_window_size(WORKSPACEID workspace, int step)
{
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    s->resize_active_column(step);
}

void ScrollerLayout::cycle_window_width(WORKSPACEID workspace, int step)
{
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    Mode mode = s->get_mode();
    s->set_mode(Mode::Row, true);
    s->resize_active_column(step);
    s->set_mode(mode, true);
}

void ScrollerLayout::cycle_window_height(WORKSPACEID workspace, int step)
{
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    Mode mode = s->get_mode();
    s->set_mode(Mode::Column, true);
    s->resize_active_column(step);
    s->set_mode(mode, true);
}

void ScrollerLayout::set_window_size(WORKSPACEID workspace, const std::string &arg)
{
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    s->size_active_column(arg);
}

void ScrollerLayout::set_window_width(WORKSPACEID workspace, const std::string &arg)
{
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    Mode mode = s->get_mode();
    s->set_mode(Mode::Row, true);
    s->size_active_column(arg);
    s->set_mode(mode, true);
}

void ScrollerLayout::set_window_height(WORKSPACEID workspace, const std::string &arg)
{
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    Mode mode = s->get_mode();
    s->set_mode(Mode::Column, true);
    s->size_active_column(arg);
    s->set_mode(mode, true);
}

void ScrollerLayout::move_focus(WORKSPACEID workspace, Direction direction)
{
    // Mark this as keyboard-triggered focus (to override cursor warping detection)
    {
        std::lock_guard<std::mutex> lock(mouseFocusMutex);
        lastKeyboardFocusTime = std::chrono::steady_clock::now();
    }

    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        // if workspace is empty, use the deault movefocus, which now
        // is "move to another monitor" (pass the direction)
        switch (direction) {
            case Direction::Left:
                orig_moveFocusTo("l");
                break;
            case Direction::Right:
                orig_moveFocusTo("r");
                break;
            case Direction::Up:
                {
                    if (g_scrollerConfig.movefocus_changes_workspace() && monitor_in_direction(Math::DIRECTION_UP) == nullptr) {
                        g_pKeybindManager->m_dispatchers["workspace"]("m-1");
                    } else {
                        orig_moveFocusTo("u");
                    }
                }
                break;
            case Direction::Down:
                {
                    if (g_scrollerConfig.movefocus_changes_workspace() && monitor_in_direction(Math::DIRECTION_DOWN) == nullptr) {
                        g_pKeybindManager->m_dispatchers["workspace"]("m+1");
                    } else {
                        orig_moveFocusTo("d");
                    }
                }
                break;
            default:
                break;
        }
        return;
    }

    auto from = s->get_active_window();
    update_relative_cursor_coords(from);

    // When focus layout is enabled, disable wrapping regardless of the focus_wrap setting
    const bool allow_wrap = !g_scrollerConfig.focus_layout_enable() && g_scrollerConfig.focus_wrap();
    if (s->move_focus(direction, allow_wrap)) {
        // Changed workspace
        const WORKSPACEID workspace_id = get_workspace_id();
        s = getRowForWorkspace(workspace_id);
        if (s != nullptr) {
            s->recalculate_row_geometry();
        }
    }
    PHLWINDOW to = s != nullptr ? s->get_active_window() : nullptr;
    switch_to_window(from, to);
}

void ScrollerLayout::move_focus_nocenter(WORKSPACEID workspace, Direction direction)
{
    // Set nocenter timestamp
    lastNoCenterTime = std::chrono::steady_clock::now();

    // Mark this as keyboard-triggered focus (to override cursor warping detection)
    {
        std::lock_guard<std::mutex> lock(mouseFocusMutex);
        lastKeyboardFocusTime = std::chrono::steady_clock::now();
    }

    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        // if workspace is empty, use the default movefocus
        switch (direction) {
            case Direction::Left:
                orig_moveFocusTo("l");
                break;
            case Direction::Right:
                orig_moveFocusTo("r");
                break;
            case Direction::Up:
                orig_moveFocusTo("u");
                break;
            case Direction::Down:
                orig_moveFocusTo("d");
                break;
            default:
                break;
        }
        return;
    }

    auto from = s->get_active_window();
    update_relative_cursor_coords(from);

    // Use move_focus_nocenter which doesn't recalculate geometry
    const bool allow_wrap = g_scrollerConfig.focus_wrap();
    if (s->move_focus_nocenter(direction, allow_wrap)) {
        // Changed workspace - in this case we do need to recalculate
        const WORKSPACEID workspace_id = get_workspace_id();
        s = getRowForWorkspace(workspace_id);
        if (s != nullptr) {
            s->recalculate_row_geometry();
        }
    }
    PHLWINDOW to = s != nullptr ? s->get_active_window() : nullptr;

    if (to != nullptr && to != from) {
        Desktop::focusState()->fullWindowFocus(to, Desktop::FOCUS_REASON_OTHER);
    }
}

void ScrollerLayout::move_window(WORKSPACEID workspace, Direction direction, bool nomode) {
    // Reset nocenter timestamp so movewindow can apply focus layout
    lastNoCenterTime = std::chrono::steady_clock::time_point{};
    mark_explicit_move_action();

    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    if (nomode)
        s->move_active_window(direction);
    else
        s->move_active_column(direction);
}

void ScrollerLayout::align_window(WORKSPACEID workspace, Direction direction) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    s->align_column(direction);
}

void ScrollerLayout::admit_window(WORKSPACEID workspace, AdmitExpelDirection direction) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->admit_window(direction);
}

void ScrollerLayout::expel_window(WORKSPACEID workspace, AdmitExpelDirection direction) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->expel_window(direction);
}

void ScrollerLayout::set_mode(WORKSPACEID workspace, Mode mode) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->set_mode(mode);
}

void ScrollerLayout::set_mode_modifier(WORKSPACEID workspace, const ModeModifier &modifier) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->set_mode_modifier(modifier);
}

void ScrollerLayout::fit_size(WORKSPACEID workspace, FitSize fitsize) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->fit_size(fitsize);
}

void ScrollerLayout::fit_width(WORKSPACEID workspace, FitSize fitsize) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    Mode mode = s->get_mode();
    s->set_mode(Mode::Row, true);
    s->fit_size(fitsize);
    s->set_mode(mode, true);
}

void ScrollerLayout::fit_height(WORKSPACEID workspace, FitSize fitsize) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    Mode mode = s->get_mode();
    s->set_mode(Mode::Column, true);
    s->fit_size(fitsize);
    s->set_mode(mode, true);
}

void ScrollerLayout::toggle_overview(WORKSPACEID workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->toggle_overview();
}

void ScrollerLayout::toggle_grid_mode(WORKSPACEID workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }
    s->toggle_grid_mode();
}

PHLWINDOW ScrollerLayout::getActiveWindow(WORKSPACEID workspace) {
    const Row *s = getRowForWorkspace(workspace);
    if (s == nullptr)
        return nullptr;

    return s->get_active_window();
}

void ScrollerLayout::marks_add(const std::string &name) {
    PHLWINDOW window = getActiveWindow(get_workspace_id());
    if (window != nullptr)
        marks.add(window, name);
}

void ScrollerLayout::marks_delete(const std::string &name) {
    marks.del(name);
}

void ScrollerLayout::marks_visit(const std::string &name) {
    PHLWINDOW from = getActiveWindow(get_workspace_id());
    update_relative_cursor_coords(from);
    PHLWINDOW to = marks.visit(name);
    if (to != nullptr) {
        switch_to_window(from, to);
    }
}

void ScrollerLayout::marks_reset() {
    marks.reset();
}

// Trails and Trailmarks
void ScrollerLayout::trail_new() {
    trails->trail_new();
}

void ScrollerLayout::trail_next() {
    trails->trail_next();
}

void ScrollerLayout::trail_prev() {
    trails->trail_prev();
}

void ScrollerLayout::trail_delete() {
    trails->trail_delete();
}

void ScrollerLayout::trail_clear() {
    trails->trail_clear();
}

void ScrollerLayout::trail_toselection() {
    trails->trail_toselection();
}

void ScrollerLayout::trailmark_toggle() {
    PHLWINDOW window = getActiveWindow(get_workspace_id());
    if (window != nullptr)
        trails->trailmark_toggle(window);
}

void ScrollerLayout::trailmark_next() {
    trails->trailmark_next();
    PHLWINDOW from = getActiveWindow(get_workspace_id());
    update_relative_cursor_coords(from);
    PHLWINDOW to = trails->get_active();
    if (to != nullptr) {
        switch_to_window(from, to);
    }
}

void ScrollerLayout::trailmark_prev() {
    trails->trailmark_prev();
    PHLWINDOW from = getActiveWindow(get_workspace_id());
    update_relative_cursor_coords(from);
    PHLWINDOW to = trails->get_active();
    if (to != nullptr) {
        switch_to_window(from, to);
    }
}

void ScrollerLayout::pin(WORKSPACEID workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    s->pin();
}

void ScrollerLayout::selection_toggle(WORKSPACEID workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    s->selection_toggle();

    // Re-render that monitor to remove decorations
    if (const auto monitor = Desktop::focusState()->monitor())
        g_pHyprRenderer->damageMonitor(monitor);
}

void ScrollerLayout::selection_set(PHLWINDOWREF window) {
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        row->data()->selection_set(window);
    }
}

void ScrollerLayout::selection_reset() {
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        row->data()->selection_reset();
    }
    // Re-render windows to remove decorations
    for (auto monitor : State::monitorState()->monitors()) {
        g_pHyprRenderer->damageMonitor(monitor);
    }
}

void ScrollerLayout::selection_workspace(WORKSPACEID workspace) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    s->selection_all();

    // Re-render that monitor to render decorations
    if (const auto monitor = Desktop::focusState()->monitor())
        g_pHyprRenderer->damageMonitor(monitor);
}

// Move all selected columns/windows to workspace, and locate them in direction wrt
// the active column. Valid directiona are left, right, beginning, end, other
// defaults to right.
void ScrollerLayout::selection_move(WORKSPACEID workspace, Direction direction) {
    // Before doing anything complicated, first checkt if there is any selection active
    bool selection = false;
    for (auto row = rows.first(); row != nullptr; row = row->next()) {
        if (row->data()->selection_exists()) {
            selection = true;
            break;
        }
    }
    if (!selection)
        return;

    const auto destination = workspace_by_id(workspace);
    if (!destination)
        return;

    auto s = getRowForWorkspace(workspace);
    bool overview_on = false;
    if (s == nullptr) {
        const auto monitor = destination->m_monitor.lock();
        if (!monitor)
            return;
        auto row = std::make_unique<Row>(workspace, monitor);
        s = row.get();
        rows.push_back(std::move(row));
    } else {
        overview_on = s->is_overview();
        if (overview_on)
            s->toggle_overview();
    }
    // First modify ScrollerLayout internal structures and then call
    // CWindow::moveToWorkspace(PHLWORKSPACE pWorkspace)
    // for each window, so Hyprland is aware of the changes.
    List<Column *> columns;
    auto row = rows.first();
    while (row != nullptr) {
        auto next = row->next();
        if (row->data()->size() > 0) {
            row->data()->selection_get(s, columns);
        }
        row = next;
    }

    s->selection_move(columns, direction);

    // Now delete those rows that may have become empty,
    // and recalculate the rest
    row = rows.first();
    while (row != nullptr) {
        auto next = row->next();
        if (row->data()->size() == 0) {
            rows.erase(row);
        } else {
            bool overview = row->data()->is_overview();
            if (overview)
                row->data()->toggle_overview();
            Desktop::focusState()->fullWindowFocus(row->data()->get_active_window(), Desktop::FOCUS_REASON_OTHER);
            row->data()->recalculate_row_geometry();
            if (overview)
                row->data()->toggle_overview();
        }
        row = next;
    }

    Desktop::focusState()->fullWindowFocus(s->get_active_window(), Desktop::FOCUS_REASON_OTHER);
    // Reset selection
    selection_reset();

    if (overview_on)
        s->toggle_overview();
}

struct JumpData {
    struct WorkspaceState {
        WORKSPACEID workspace = WORKSPACE_INVALID;
        bool overview = false;
    };
    struct DecorationState {
        PHLWINDOWREF window;
        JumpDecoration* decoration = nullptr;
    };
    PHLWINDOWREF from_window;
    PHLMONITORREF from_monitor;
    std::vector<WorkspaceState> workspaces;
    std::vector<PHLWINDOWREF> windows;
    std::vector<DecorationState> decorations;
    std::string keys;
    size_t keys_pressed = 0;
    size_t nkeys = 0;
    size_t window_number = 0;
    CHyprSignalListener keyPressHookCallback;
};

static std::unique_ptr<JumpData> jump_data;

static std::string generate_label(size_t index, const std::string &keys, size_t nkeys)
{
    const size_t ksize = keys.size();
    std::string label;
    for (size_t n = 0, divisor = index; n < nkeys; ++n) {
        const size_t remainder = divisor % ksize;
        label.insert(0, &keys[remainder], 1);
        divisor /= ksize;
    }
    return label;
}

void ScrollerLayout::cancelJump(bool restoreOverview) {
    if (!jump_data) {
        jumping = false;
        return;
    }

    jump_data->keyPressHookCallback.reset();
    for (const auto& state : jump_data->decorations) {
        if (const auto window = state.window.lock(); window && state.decoration)
            window->removeWindowDeco(state.decoration);
    }

    if (restoreOverview) {
        for (const auto& state : jump_data->workspaces) {
            if (!state.overview) {
                if (auto row = getRowForWorkspace(state.workspace); row && row->is_overview())
                    row->toggle_overview();
            }
        }
    }

    jump_data.reset();
    jumping = false;
}

void ScrollerLayout::jump() {
    if (jumping)
        return;

    jumping = true;
    jump_data = std::make_unique<JumpData>();

    for (auto monitor : State::monitorState()->monitors()) {
        WORKSPACEID workspace_id = monitor->activeSpecialWorkspaceID();
        if (!workspace_id) {
            workspace_id = monitor->activeWorkspaceID();
        }
        auto s = getRowForWorkspace(workspace_id);
        if (s == nullptr)
            continue;

        jump_data->workspaces.push_back({workspace_id, s->is_overview()});
    }
    if (jump_data->workspaces.size() == 0) {
        cancelJump(false);
        return;
    }

    for (const auto& state : jump_data->workspaces) {
        if (const auto row = getRowForWorkspace(state.workspace))
            row->get_windows(jump_data->windows);
    }
    std::erase_if(jump_data->windows, [](const PHLWINDOWREF& window) { return !window.lock(); });
    if (jump_data->windows.size() == 0) {
        cancelJump(false);
        return;
    }

    jump_data->keys = g_scrollerConfig.jump_labels_keys();
    jump_data->from_window = Desktop::focusState()->window();
    jump_data->from_monitor = Desktop::focusState()->monitor();

    if (jump_data->keys.empty() || (jump_data->keys.size() == 1 && jump_data->windows.size() > 1)) {
        cancelJump(false);
        return;
    }
    if (jump_data->windows.size() == 1)
        jump_data->nkeys = 1;
    else
        jump_data->nkeys = static_cast<size_t>(std::ceil(
            std::log(static_cast<double>(jump_data->windows.size())) /
            std::log(static_cast<double>(jump_data->keys.size()))));

    // Set overview mode for those workspaces that are not
    for (const auto& state : jump_data->workspaces) {
        if (!state.overview) {
            if (auto row = getRowForWorkspace(state.workspace))
                row->toggle_overview();
        }
    }

    // Set decorations (in overview mode)
    size_t index = 0;
    for (const auto& window_ref : jump_data->windows) {
        const auto window = window_ref.lock();
        const std::string label = generate_label(index++, jump_data->keys, jump_data->nkeys);
        auto deco = makeUnique<JumpDecoration>(window, label);
        auto* const decoration = deco.get();
        if (HyprlandAPI::addWindowDecoration(PHANDLE, window, std::move(deco)))
            jump_data->decorations.push_back({window, decoration});
    }

    jump_data->keys_pressed = 0;
    jump_data->window_number = 0;

    jump_data->keyPressHookCallback = Event::bus()->m_events.input.keyboard.key.listen([this](IKeyboard::SKeyEvent event, Event::SCallbackInfo& info) {
        // Find a keyboard with valid xkb state
        SP<IKeyboard> keyboard;
        for (auto& kb : g_pInputManager->m_keyboards) {
            if (kb && kb->m_xkbState) {
                keyboard = kb;
                break;
            }
        }
        if (!keyboard)
            return;

        const auto KEYCODE = event.keycode + 8; // Because to xkbcommon it's +8 from libinput
        const xkb_keysym_t keysym = xkb_state_key_get_one_sym(keyboard->m_xkbState, KEYCODE);

        if (event.state != WL_KEYBOARD_KEY_STATE_PRESSED)
            return;

        // Check if key is valid, otherwise exit
        bool valid = false;
        for (size_t i = 0; i < jump_data->keys.size(); ++i) {
            std::string keyname(1, jump_data->keys[i]);
            xkb_keysym_t key = xkb_keysym_from_name(keyname.c_str(), XKB_KEYSYM_NO_FLAGS);
            if (key && key == keysym) {
                jump_data->window_number = jump_data->window_number * jump_data->keys.size() + i;
                valid = true;
                break;
            }
        }
        bool focus = false;
        if (valid) {
            jump_data->keys_pressed++;
            if (jump_data->keys_pressed == jump_data->nkeys) {
                if (jump_data->window_number < jump_data->windows.size())
                    focus = true;
            } else {
                info.cancelled = true;
                return;
            }
        }

        const auto from_window = jump_data->from_window.lock();
        const auto from_monitor = jump_data->from_monitor.lock();
        const auto target_window = focus ? jump_data->windows[jump_data->window_number].lock() : nullptr;
        cancelJump(true);

        if (focus && target_window) {
            // Mark this as a keyboard-triggered focus change so onWindowFocusChange
            // doesn't treat it as mouse-triggered (the jump key is handled by a
            // custom key listener, not a dispatcher, so lastKeyboardFocusTime
            // wouldn't be updated otherwise)
            lastKeyboardFocusTime = std::chrono::steady_clock::now();
            suppressFocusLayout = false;
            update_relative_cursor_coords(from_window);
            switch_to_window(from_window, target_window);
        } else {
            if (from_window)
                from_window->warpCursor();
            else if (from_monitor) {
                Pointer::mgr()->warpTo(from_monitor->middle());
                Desktop::focusState()->rawMonitorFocus(from_monitor);
            }
        }
        info.cancelled = true;
    });
}

void ScrollerLayout::post_event(WORKSPACEID workspace, const std::string &event) {
    auto s = getRowForWorkspace(workspace);
    if (s == nullptr) {
        return;
    }

    s->post_event(event);
}

void ScrollerLayout::swipe_begin(IPointer::SSwipeBeginEvent /* swipe_event */) {
    endClipHover(false);
    swipe_active = false;
    swipe_triggered = false;
    gesture_delta = {};
    swipe_direction = Direction::Begin;
}

void ScrollerLayout::swipe_update(Event::SCallbackInfo &info, IPointer::SSwipeUpdateEvent swipe_event) {
    WORKSPACEID wid = get_workspace_id();
    if (wid == WORKSPACE_INVALID) {
        return;
    }

    auto s = getRowForWorkspace(wid);

    static const CConfigValue<Config::INTEGER> natural_scroll("input:touchpad:natural_scroll");
    static const CConfigValue<Config::INTEGER> workspace_swipe_invert("gestures:workspace_swipe_invert");
    const bool scroll_enabled = g_scrollerConfig.gesture_scroll_enable();
    const auto scroll_fingers = g_scrollerConfig.gesture_scroll_fingers();
    const bool overview_enabled = g_scrollerConfig.gesture_overview_enable();
    const auto overview_fingers = g_scrollerConfig.gesture_overview_fingers();
    const bool workspace_switch_enabled = g_scrollerConfig.gesture_workspace_switch_enable();
    const auto workspace_switch_fingers = g_scrollerConfig.gesture_workspace_switch_fingers();

    if (!(scroll_enabled && swipe_event.fingers == scroll_fingers) &&
        !(overview_enabled && swipe_event.fingers == overview_fingers) &&
        !(workspace_switch_enabled && swipe_event.fingers == workspace_switch_fingers)) {
        return;
    }

    info.cancelled = true;
    Vector2D delta = swipe_event.delta;
    const auto gesture_sensitivity = g_scrollerConfig.gesture_sensitivity();
    delta *= *natural_scroll ? gesture_sensitivity : -gesture_sensitivity;
    if (!swipe_active) {
        gesture_delta = {};
        swipe_active = true;
    }
    gesture_delta += delta;

    if (scroll_enabled && swipe_event.fingers == scroll_fingers) {
        if (s == nullptr)
            return;
        if (std::abs(gesture_delta.x) > std::abs(gesture_delta.y))
            swipe_direction = gesture_delta.x > 0 ? Direction::Right : Direction::Left;
        else
            swipe_direction = gesture_delta.y > 0 ? Direction::Down : Direction::Up;
        s->scroll_update(swipe_direction, delta);
    } else {
        // Undo natural
        const Vector2D delta = gesture_delta * (*natural_scroll ? -1.0 : 1.0);
        if (overview_enabled && swipe_event.fingers == overview_fingers) {
            if (swipe_triggered)
                return;
            if (delta.y <= -g_scrollerConfig.gesture_overview_distance()) {
                if (s == nullptr)
                    return;
                if (!s->is_overview()) {
                    s->toggle_overview();
                    swipe_triggered = true;
                }
            } else if (delta.y >= g_scrollerConfig.gesture_overview_distance()) {
                if (s == nullptr)
                    return;
                if (s->is_overview()) {
                    s->toggle_overview();
                    swipe_triggered = true;
                }
            }
        }
        if (workspace_switch_enabled && swipe_event.fingers == workspace_switch_fingers) {
            if (swipe_triggered)
                return;
            const auto workspace_switch_distance = g_scrollerConfig.gesture_workspace_switch_distance();
            const auto offset = g_scrollerConfig.gesture_workspace_switch_prefix();
            if (delta.x <= -workspace_switch_distance) {
                g_pKeybindManager->m_dispatchers["workspace"](*workspace_swipe_invert ? offset + "+1" : offset + "-1");
                swipe_triggered = true;
            } else if (delta.x >= workspace_switch_distance) {
                g_pKeybindManager->m_dispatchers["workspace"](*workspace_swipe_invert ? offset + "-1" : offset + "+1");
                swipe_triggered = true;
            }
        }
    }
}

void ScrollerLayout::swipe_end(Event::SCallbackInfo &info,
                               IPointer::SSwipeEndEvent /* swipe_event */) {
    WORKSPACEID wid = get_workspace_id();
    if (wid == WORKSPACE_INVALID) {
        swipe_active = false;
        swipe_triggered = false;
        gesture_delta = {};
        swipe_direction = Direction::Begin;
        return;
    }
    // Only if scrolling
    if (swipe_direction != Direction::Begin) {
        auto s = getRowForWorkspace(wid);
        if (s) {
            auto from = s->get_active_window();
            s->scroll_end(swipe_direction);
            auto to = s->get_active_window();

            if (from == to) {
                // scroll hit an edge and couldn't move
                if (g_scrollerConfig.movefocus_changes_workspace()) {
                    if (swipe_direction == Direction::Up) { // Swipe down gesture
                        PHLMONITOR monitor = monitor_in_direction(Math::DIRECTION_DOWN);
                        if (monitor == nullptr) {
                            g_pKeybindManager->m_dispatchers["workspace"]("m+1");
                        }
                    } else if (swipe_direction == Direction::Down) { // Swipe up gesture
                        PHLMONITOR monitor = monitor_in_direction(Math::DIRECTION_UP);
                        if (monitor == nullptr) {
                            g_pKeybindManager->m_dispatchers["workspace"]("m-1");
                        }
                    }
                }
            }
        }
    }

    info.cancelled = swipe_active;
    swipe_active = false;
    swipe_triggered = false;
    gesture_delta = {};
    swipe_direction = Direction::Begin;
}

bool ScrollerLayout::clipHoverOwnsPoint(const Vector2D &point) {
    const auto hoveredWindow = clipHoverWindow.lock();
    const auto mainWindow = clipHoverMainWindow.lock();
    if (!hoveredWindow || !mainWindow || !hoveredWindow->m_isMapped || hoveredWindow->isHidden() ||
        !mainWindow->m_isMapped || mainWindow->isHidden())
        return false;

    const CBox hoveredBox = {window_position(hoveredWindow), window_size(hoveredWindow)};
    const CBox mainBox = {window_position(mainWindow), window_size(mainWindow)};
    return hoveredBox.containsPoint(point) && !mainBox.containsPoint(point);
}

void ScrollerLayout::endClipHover(bool restoreMain) {
    const auto hoveredWindow = clipHoverWindow.lock();
    const auto mainWindow = clipHoverMainWindow.lock();
    if (!hoveredWindow && !mainWindow)
        return;

    const auto row = getRowForWindow(hoveredWindow);
    if (restoreMain && mainWindow && row) {
        Log::logger->log(Log::DEBUG, "[CLIP] Pointer left transient hover; restoring main window");
        row->focus_window(mainWindow, true);
        enforceClipZOrder(row);
        Desktop::focusState()->fullWindowFocus(mainWindow, Desktop::FOCUS_REASON_OTHER);
    } else if (!restoreMain) {
        enforceClipZOrder(row);
    }

    clipHoverWindow.reset();
    clipHoverMainWindow.reset();
}

bool ScrollerLayout::updateClipHover(Row * /*row*/, const Vector2D & /*mousePos*/, bool /*pointerInHiddenRegion*/) {
    if (clipHoverWindow.lock())
        endClipHover(false);

    // Takeovers are click-committed only. Never start a hover session on bare
    // pointer motion: the clipped window stays clipped until a real click.
    return false;
}

void ScrollerLayout::mouse_move(Event::SCallbackInfo& info, const Vector2D &mousePos) {
    static bool inside = false;
    auto PMONITOR = monitor_at(mousePos);
    if (!PMONITOR)
        return;

    WORKSPACEID workspace_id = PMONITOR->activeWorkspaceID();
    auto s = getRowForWorkspace(workspace_id);

    const bool pointerInHiddenRegion = s != nullptr && s->is_point_in_clipped_hidden_region(mousePos);
    const bool clipHoverActive = updateClipHover(s, mousePos, pointerInHiddenRegion);

    // FIX: Check if mouse is over a clipped window's hidden region
    // If so, cancel the focus event to prevent focus stealing
    // Only cancel if mouse is in the HIDDEN portion, not the visible portion
    if (pointerInHiddenRegion && !clipHoverActive) {
        Log::logger->log(Log::DEBUG, "[CLIP] Mouse over hidden region of clipped window, cancelling focus event");
        info.cancelled = true;
        return;
    }

    if (s != nullptr) {
        CBox monitorBox = {PMONITOR->m_position, PMONITOR->m_size};
        CBox cbox = PMONITOR->m_reservedArea.apply(monitorBox);
        Box box = {cbox.x, cbox.y, cbox.w, cbox.h};

        if (!s->get_max().contains_point(mousePos) && box.contains_point(mousePos)) {
            // We are in gaps_out territory
            static auto enteredTime = std::chrono::high_resolution_clock::now();
            auto eventTime = std::chrono::high_resolution_clock::now();
            if (!inside) {
                inside = true;
                enteredTime = eventTime;
                info.cancelled = true;
                return;
            } else {
                if (std::chrono::duration_cast<std::chrono::milliseconds>(eventTime - enteredTime).count() < g_scrollerConfig.focus_edge_ms()) {
                    info.cancelled = true;
                    return;
                }
            }
        }
    }
    inside = false;

    // Update last mouse movement time
    {
        std::lock_guard<std::mutex> lock(mouseFocusMutex);
        lastMouseTime = std::chrono::steady_clock::now();
    }
}

// ============================================================
// ScrollerAlgorithm - ITiledAlgorithm adapter
// ============================================================

namespace {
class ScrollerFullscreenHandler : public Fullscreen::IFullscreenHandler {
public:
    explicit ScrollerFullscreenHandler(Layout::IModeAlgorithm* algorithm)
        : IFullscreenHandler(algorithm) {}

    Fullscreen::eFullscreenRequestResult requestFullscreen(
        const Fullscreen::SFullscreenRequest& request) override {
        const auto window = request.target ? request.target->window() : nullptr;
        const bool exiting = request.mode == Fullscreen::FSMODE_NONE;

        // Save tiled geometry before Hyprland's default handler resizes.
        if (!exiting && g_ScrollerLayout && g_ScrollerLayout->is_enabled() && window)
            g_ScrollerLayout->fullscreenRequestForWindow(
                window, request.currentMode, request.mode);

        IFullscreenHandler::requestFullscreen(request);

        // Restore scroller geometry after the default handler's exit path.
        if (exiting && g_ScrollerLayout && g_ScrollerLayout->is_enabled() && window) {
            g_ScrollerLayout->fullscreenRequestForWindow(
                window, request.currentMode, request.mode);
            dispatchers::dispatch_layout_message("recalculate warp");
        }

        return Fullscreen::FULLSCREEN_REQUEST_LAYOUT_HANDLED;
    }

    Fullscreen::eFullscreenHandler getFullscreenHandlerName() const override {
        return Fullscreen::FULLSCREEN_HANDLER_LAYOUT;
    }
};
}

ScrollerAlgorithm::ScrollerAlgorithm()
    : m_fullscreenHandler(makeUnique<ScrollerFullscreenHandler>(this)) {}

ScrollerAlgorithm::~ScrollerAlgorithm() = default;

WP<Fullscreen::IFullscreenHandler> ScrollerAlgorithm::getFSHandler() {
    return m_fullscreenHandler;
}

WORKSPACEID ScrollerAlgorithm::getWorkspaceID() {
    auto parent = m_parent.lock();
    if (!parent)
        return WORKSPACE_INVALID;
    auto space = parent->space();
    if (!space)
        return WORKSPACE_INVALID;
    auto ws = space->workspace();
    if (!ws)
        return WORKSPACE_INVALID;
    return ws->m_id;
}

void ScrollerAlgorithm::syncTargetGeometry() {
    // Don't sync during overview mode — the overview rendering hooks
    // manage display independently via monitor scale modification.
    // Syncing would write overview-scaled positions to m_box which
    // can interfere with the layout manager.
    WORKSPACEID wid = getWorkspaceID();
    if (wid != WORKSPACE_INVALID && overviews && overviews->overview_enabled(wid))
        return;

    auto parent = m_parent.lock();
    if (!parent)
        return;
    auto space = parent->space();
    if (!space)
        return;
    for (auto& wt : space->targets()) {
        auto t = wt.lock();
        if (!t || t->floating())
            continue;
        auto w = t->window();
        if (!w)
            continue;
        // Set both logicalBox and visualBox to our calculated positions.
        // This prevents CWindowTarget::updatePos() from re-applying gaps_in
        // when recalc() is called (e.g., from decoration updates, window mapping).
        // updatePos() skips gap calculation if visualBox is non-empty.
        //
        // We use m_position/m_size (tile position with gaps applied by the plugin)
        // for both boxes. updatePos() will still apply window reserved area on top,
        // but we've already accounted for that in update_window(), so we need to
        // use m_realPosition/m_realSize which is the FINAL position after all adjustments.
        Layout::STargetBox tbox;
        tbox.logicalBox = CBox(window_position(w), window_size(w));
        tbox.visualBox = tbox.logicalBox;
        t->Layout::ITarget::setPositionGlobal(tbox);
    }
}

void ScrollerAlgorithm::newTarget(SP<Layout::ITarget> target) {
    if (!g_ScrollerLayout || !target)
        return;
    auto window = target->window();
    if (!window)
        return;
    g_ScrollerLayout->onWindowCreatedTiling(window);
    // Use the base ITarget::setPositionGlobal to update m_box only.
    // The scroller already applies gaps_in and reserved area to m_position/m_size
    // in update_window(). CWindowTarget::updatePos() would double-apply them,
    // causing extra padding on newly created windows.
    target->Layout::ITarget::setPositionGlobal(CBox(window_position(window), window_size(window)));
    // Also sync all other targets in case the new window shifted them.
    syncTargetGeometry();
    // Second refresh via Hyprland pipeline (fixes padding when window loads)
    if (auto mon = window->m_monitor.lock())
        g_layoutManager->recalculateMonitor(mon);
}

void ScrollerAlgorithm::movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D> /* focalPoint */) {
    if (!g_ScrollerLayout || !target)
        return;
    auto window = target->window();
    if (!window)
        return;
    // Re-add the window if it was removed (e.g. after a mouse drag that
    // temporarily floated the window). onWindowCreatedTiling guards against
    // duplicate adds, so this is safe to call unconditionally.
    g_ScrollerLayout->onWindowCreatedTiling(window);
    target->Layout::ITarget::setPositionGlobal(CBox(window_position(window), window_size(window)));
    syncTargetGeometry();
    // Second refresh via Hyprland pipeline (fixes padding when window moves)
    if (auto mon = window->m_monitor.lock())
        g_layoutManager->recalculateMonitor(mon);
}

void ScrollerAlgorithm::removeTarget(SP<Layout::ITarget> target) {
    if (!g_ScrollerLayout || !target)
        return;
    auto window = target->window();
    if (!window)
        return;
    g_ScrollerLayout->onWindowRemovedTiling(window);
    syncTargetGeometry();
}

void ScrollerAlgorithm::resizeTarget(const Vector2D& delta, SP<Layout::ITarget> target, Layout::eRectCorner corner) {
    if (!g_ScrollerLayout || !target)
        return;
    auto window = target->window();
    if (!window)
        return;
    g_ScrollerLayout->resizeActiveWindow(delta, corner, window);
    syncTargetGeometry();
}

void ScrollerAlgorithm::recalculate(Layout::eRecalculateReason reason) {
    if (!g_ScrollerLayout)
        return;
    if (reason == Layout::RECALCULATE_REASON_TOGGLE_LAYOUT_HANDLED_FULLSCREEN ||
        reason == Layout::RECALCULATE_REASON_TOGGLE_DEFAULT_HANDLED_FULLSCREEN)
        suppressFocusLayout = false;
    WORKSPACEID wid = getWorkspaceID();
    if (wid == WORKSPACE_INVALID)
        return;
    g_ScrollerLayout->recalculateWorkspace(wid);
    syncTargetGeometry();
}

void ScrollerAlgorithm::swapTargets(SP<Layout::ITarget> a, SP<Layout::ITarget> b) {
    if (!g_ScrollerLayout || !a || !b)
        return;
    auto wa = a->window();
    auto wb = b->window();
    if (!wa || !wb)
        return;
    g_ScrollerLayout->switchWindows(wa, wb);
    syncTargetGeometry();
}

void ScrollerAlgorithm::moveTargetInDirection(SP<Layout::ITarget> t, Math::eDirection dir, bool silent) {
    if (!g_ScrollerLayout || !t)
        return;
    auto window = t->window();
    if (!window)
        return;
    std::string direction;
    switch (dir) {
        case Math::DIRECTION_LEFT:  direction = "l"; break;
        case Math::DIRECTION_RIGHT: direction = "r"; break;
        case Math::DIRECTION_UP:    direction = "u"; break;
        case Math::DIRECTION_DOWN:  direction = "d"; break;
        default: return;
    }
    g_ScrollerLayout->moveWindowTo(window, direction, silent);
    syncTargetGeometry();
}

Config::ErrorResult ScrollerAlgorithm::layoutMsg(const std::string_view& sv) {
    const auto result = dispatchers::dispatch_layout_message(std::string{sv});
    if (!result.success)
        return Config::configError(result.error, Config::eConfigErrorLevel::ERROR, Config::eConfigErrorCode::INVALID_ARGUMENT);

    return {};
}

std::optional<Vector2D> ScrollerAlgorithm::predictSizeForNewTarget() {
    if (!g_ScrollerLayout)
        return std::nullopt;
    auto size = g_ScrollerLayout->predictSizeForNewWindowTiled();
    if (size.x == 0 && size.y == 0)
        return std::nullopt;
    return size;
}

SP<Layout::ITarget> ScrollerAlgorithm::getNextCandidate(SP<Layout::ITarget> /* old */) {
    if (!g_ScrollerLayout)
        return nullptr;

    // Use this algorithm's workspace, not the focused monitor's workspace,
    // because getNextCandidate is called per-algorithm-instance (per workspace).
    WORKSPACEID wid = getWorkspaceID();
    if (wid == WORKSPACE_INVALID)
        return nullptr;

    PHLWINDOW candidate = g_ScrollerLayout->getActiveWindow(wid);
    if (!candidate)
        return nullptr;

    auto parent = m_parent.lock();
    if (!parent)
        return nullptr;
    auto space = parent->space();
    if (!space)
        return nullptr;

    for (auto& wt : space->targets()) {
        auto t = wt.lock();
        if (t && t->window() == candidate)
            return t;
    }
    return nullptr;
}
