#ifndef SCROLLER_SCROLLER_H
#define SCROLLER_SCROLLER_H

#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/layout/algorithm/TiledAlgorithm.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>

#include "list.h"
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/devices/IPointer.hpp>

#include <memory>

#include "enums.h"
#include "common.h"

class Row;

class ScrollerLayout {
public:
    ScrollerLayout();
    ~ScrollerLayout();

    void onEnable();
    void onDisable();

    void onWindowCreatedTiling(PHLWINDOW);
    bool isWindowTiled(PHLWINDOW);
    void onWindowRemovedTiling(PHLWINDOW);
    void onWindowRemovedFloating(PHLWINDOW);
    void recalculateMonitor(const MONITORID &monitor_id);
    void recalculateWorkspace(WORKSPACEID wid);
    void recalculateWindow(PHLWINDOW);
    void resizeActiveWindow(const Vector2D &delta, Layout::eRectCorner corner,
                                    PHLWINDOW pWindow = nullptr);
    void fullscreenRequestForWindow(PHLWINDOW pWindow,
            const eFullscreenMode CURRENT_EFFECTIVE_MODE,
            const eFullscreenMode EFFECTIVE_MODE);
    void switchWindows(PHLWINDOW, PHLWINDOW);
    void moveWindowTo(PHLWINDOW, const std::string &direction, bool silent = false);
    std::string getLayoutName();
    PHLWINDOW getNextWindowCandidate(PHLWINDOW);
    void onWindowFocusChange(PHLWINDOW, Desktop::eFocusReason);
    Vector2D predictSizeForNewWindowTiled();

    // New Dispatchers
    void cycle_window_size(WORKSPACEID workspace, int step);
    void cycle_window_width(WORKSPACEID workspace, int step);
    void cycle_window_height(WORKSPACEID workspace, int step);
    void set_window_size(WORKSPACEID workspace, const std::string &arg);
    void set_window_width(WORKSPACEID workspace, const std::string &arg);
    void set_window_height(WORKSPACEID workspace, const std::string &arg);
    void move_focus(WORKSPACEID workspace, Direction);
    void move_focus_nocenter(WORKSPACEID workspace, Direction);
    void move_window(WORKSPACEID workspace, Direction, bool);
    void align_window(WORKSPACEID workspace, Direction);
    void admit_window(WORKSPACEID workspace, AdmitExpelDirection direction);
    void expel_window(WORKSPACEID workspace, AdmitExpelDirection direction);
    void set_mode(WORKSPACEID workspace, Mode);
    void set_mode_modifier(WORKSPACEID workspace, const ModeModifier &);
    void fit_size(WORKSPACEID workspace, FitSize);
    void fit_width(WORKSPACEID workspace, FitSize);
    void fit_height(WORKSPACEID workspace, FitSize);
    void toggle_overview(WORKSPACEID workspace);
    void toggle_grid_mode(WORKSPACEID workspace);

    void marks_add(const std::string &name);
    void marks_delete(const std::string &name);
    void marks_visit(const std::string &name);
    void marks_reset();

    void pin(WORKSPACEID workspace);

    void selection_toggle(WORKSPACEID workspace);
    void selection_set(PHLWINDOWREF window);
    void selection_reset();
    void selection_workspace(WORKSPACEID workspace);
    void selection_move(WORKSPACEID workspace, Direction direction = Direction::End);

    void trail_new();
    void trail_next();
    void trail_prev();
    void trail_delete();
    void trail_clear();
    void trail_toselection();
    void trailmark_toggle();
    void trailmark_next();
    void trailmark_prev();

    void jump();

    void post_event(WORKSPACEID workspace, const std::string &event);

    void swipe_begin(IPointer::SSwipeBeginEvent);
    void swipe_update(Event::SCallbackInfo& info, IPointer::SSwipeUpdateEvent);
    void swipe_end(Event::SCallbackInfo& info, IPointer::SSwipeEndEvent);

    void mouse_move(Event::SCallbackInfo& info, const Vector2D &mousePos);

    bool is_enabled() const { return enabled; }

    // Used by ScrollerAlgorithm::getNextCandidate
    PHLWINDOW getActiveWindow(WORKSPACEID workspace);
    PHLWINDOW getClipHoverWindow() const { return clipHoverWindow.lock(); }
    bool clipHoverOwnsPoint(const Vector2D &point);

private:
    Row *getRowForWorkspace(WORKSPACEID workspace);
    Row *getRowForWindow(PHLWINDOW window);
    void cancelJump(bool restoreOverview);
    void enforceClipZOrder(Row *row);
    void endClipHover(bool restoreMain);
    bool updateClipHover(Row *row, const Vector2D &mousePos, bool pointerInHiddenRegion);

    List<std::unique_ptr<Row>> rows;
    PHLWINDOWREF lastFocusedWindow;
    PHLWINDOWREF clipHoverWindow;
    PHLWINDOWREF clipHoverMainWindow;

    bool enabled = false;
    Vector2D gesture_delta{};
    bool swipe_active = false;
    bool swipe_triggered = false;
    Direction swipe_direction = Direction::Begin;
    bool jumping = false;
};

class ScrollerAlgorithm : public Layout::ITiledAlgorithm {
public:
    ScrollerAlgorithm();
    ~ScrollerAlgorithm() override;

    WP<Fullscreen::IFullscreenHandler> getFSHandler() override;

    void newTarget(SP<Layout::ITarget> target) override;
    void movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D> focalPoint = std::nullopt) override;
    void removeTarget(SP<Layout::ITarget> target) override;
    void resizeTarget(const Vector2D& delta, SP<Layout::ITarget> target, Layout::eRectCorner corner = Layout::CORNER_NONE) override;
    void recalculate(Layout::eRecalculateReason reason = Layout::RECALCULATE_REASON_UNKNOWN) override;
    void swapTargets(SP<Layout::ITarget> a, SP<Layout::ITarget> b) override;
    void moveTargetInDirection(SP<Layout::ITarget> t, Math::eDirection dir, bool silent) override;
    Config::ErrorResult layoutMsg(const std::string_view& sv) override;
    std::optional<Vector2D> predictSizeForNewTarget() override;
    SP<Layout::ITarget> getNextCandidate(SP<Layout::ITarget> old) override;

    // Sync CWindow geometry to ITarget boxes after our layout calculations.
    // v0.54 reads geometry from ITarget, not directly from CWindow.
    void syncTargetGeometry();

private:
    WORKSPACEID getWorkspaceID();
    UP<Fullscreen::IFullscreenHandler> m_fullscreenHandler;
};

#endif  // SCROLLER_SCROLLER_H
