#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/debug/log/Logger.hpp>

#include "config.h"
#include "window.h"

extern HANDLE PHANDLE;
extern ScrollerSizes scroller_sizes;

Window::Window(PHLWINDOW window, double maxy, double box_h, StandardSize width)
    : window(window), width(width)
{
    if (!window)
        return;

    StandardSize h = scroller_sizes.get_window_default_height(window);
    auto position = window_position(window);
    position.y = maxy;
    set_window_position(window, position);
    update_height(h, box_h);
    auto deco = makeUnique<SelectionBorders>(this);
    auto* const decoration_ptr = deco.get();
    if (HyprlandAPI::addWindowDecoration(PHANDLE, window, std::move(deco)))
        decoration = decoration_ptr;

    // Check if this window should be clipped when inactive (instead of resized)
    clip_when_inactive = window->m_ruleApplicator &&
        window->m_ruleApplicator->m_tagKeeper.isTagged("scroller:clip_when_inactive");
    Log::logger->log(Log::DEBUG, "[CLIP] Window created: class={}, clip_when_inactive={}",
              window->m_class.c_str(), clip_when_inactive);
    // NOTE: Don't store full_width here! It will be stored when window first becomes active
}

void Window::update_height(StandardSize h, double max)
{
    height = h;
    switch (height) {
    case StandardSize::One:
        box_h = max;
        break;
    case StandardSize::SevenEighths:
        box_h = 7.0 * max / 8.0;
        break;
    case StandardSize::FiveSixths:
        box_h = 5.0 * max / 6.0;
        break;
    case StandardSize::ThreeQuarters:
        box_h = 3.0 * max / 4.0;
        break;
    case StandardSize::TwoThirds:
        box_h = 2.0 * max / 3.0;
        break;
    case StandardSize::FiveEighths:
        box_h = 5.0 * max / 8.0;
        break;
    case StandardSize::OneHalf:
        box_h = 0.5 * max;
        break;
    case StandardSize::ThreeEighths:
        box_h = 3.0 * max / 8.0;
        break;
    case StandardSize::OneThird:
        box_h = max / 3.0;
        break;
    case StandardSize::OneFourth:
        box_h = max / 4.0;
        break;
    case StandardSize::OneSixth:
        box_h = max / 6.0;
        break;
    case StandardSize::OneEighth:
        box_h = max / 8.0;
        break;
    default:
        break;
    }
}

void Window::update_window(double w, const Vector2D &gap_x, double gap0, double gap1, bool animate, bool is_active)
{
    PHLWINDOW win = window.lock();
    if (!win) return;

    auto reserved = win->getFullWindowReservedArea();

    // Calculate target width
    double target_width = std::max(w - reserved.topLeft.x - reserved.bottomRight.x - gap_x.x - gap_x.y, 1.0);
    double target_height = std::max(get_geom_h() - reserved.topLeft.y - reserved.bottomRight.y - gap0 - gap1, 1.0);

    // CLIP MODE: Only activate if:
    // 1. Window is tagged for clipping
    // 2. We have a stored full size
    // 3. Target width is significantly smaller than full width (not just rounding error)
    // 4. Focus layout is NOT enabled (clipping conflicts with focus_layout positioning)
    // 5. Window is not the active one (the active window must always be at layout size)
    const bool focus_layout_active = g_scrollerConfig.focus_layout_enable();

    const double CLIP_THRESHOLD = 50.0; // Only clip if difference > 50px
    bool should_clip = clip_when_inactive &&
                      !is_active &&
                      full_width > 0 &&
                      (full_width - target_width) > CLIP_THRESHOLD &&
                      !focus_layout_active;  // Disable clipping when focus_layout is managing sizes

    // DEBUG: Log clipping decisions
    if (clip_when_inactive) {
        Log::logger->log(Log::DEBUG, "[CLIP] Window: clip_flag={}, is_active={}, full_width={:.0f}, target_width={:.0f}, diff={:.0f}, should_clip={}",
                  clip_when_inactive, is_active, full_width, target_width, (full_width - target_width), should_clip);
    }

    if (should_clip) {
        // CLIP MODE: Keep window at full size but position to show only a viewport
        // This will cause overlap but preserve client content size

        // Keep window at full size so client doesn't resize
        set_window_size(win, Vector2D(full_width, full_height));

        // Shift position left to align rightmost edge with where the small window would end
        // This way only the rightmost target_width portion is visible
        auto position = window_position(win);
        double original_x = position.x;
        position.x -= full_width - target_width;

        set_window_position(win, position, !animate);
        // DON'T call sendWindowSize() - client keeps rendering at full_width

        Log::logger->log(Log::DEBUG, "[CLIP] Kept window at {:.0f}x{:.0f}, shifted position from {:.0f} to {:.0f}",
                  full_width, full_height, original_x, position.x);
        return;
    }

    // NORMAL MODE: Resize the window (either not clipping, or at full size)
    set_window_size(win, Vector2D(target_width, target_height));
    if (!animate)
        win->positionAnimation()->warp(false);
    win->sendWindowSize();  // Notify client of size change
}

Config::CGradientValueData Window::get_border_color() const
{
    const CHyprColor selected_col = g_scrollerConfig.selection_border();

    if (selected) return selected_col;

    PHLWINDOW w = window.lock();
    if (!w) return selected_col;

    return w->m_realBorderColor;
}
