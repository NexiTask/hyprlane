#include <hyprland/src/Compositor.hpp>
#include <hyprutils/string/VarList.hpp>
#include <hyprland/src/includes.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include <array>
#include <optional>
#include <string_view>

#include "enums.h"
#include "dispatchers.h"
#include "scroller.h"


using Hyprutils::String::CVarList;

extern HANDLE PHANDLE;
extern std::unique_ptr<ScrollerLayout> g_ScrollerLayout;
extern std::function<SDispatchResult(std::string)> orig_moveActiveTo;
extern bool suppressFocusLayout;


namespace dispatchers {
    SDispatchResult dispatch_cyclesize(std::string);
    SDispatchResult dispatch_recalculate(std::string);
    SDispatchResult dispatch_cyclewidth(std::string);
    SDispatchResult dispatch_cycleheight(std::string);
    SDispatchResult dispatch_setsize(std::string);
    SDispatchResult dispatch_setwidth(std::string);
    SDispatchResult dispatch_setheight(std::string);
    SDispatchResult dispatch_movefocus_nocenter(std::string);
    SDispatchResult dispatch_alignwindow(std::string);
    SDispatchResult dispatch_admitwindow(std::string);
    SDispatchResult dispatch_expelwindow(std::string);
    SDispatchResult dispatch_setmode(std::string);
    SDispatchResult dispatch_setmodemodifier(std::string);
    SDispatchResult dispatch_fitsize(std::string);
    SDispatchResult dispatch_fitwidth(std::string);
    SDispatchResult dispatch_fitheight(std::string);
    SDispatchResult dispatch_toggleoverview(std::string);
    SDispatchResult dispatch_marksadd(std::string);
    SDispatchResult dispatch_marksdelete(std::string);
    SDispatchResult dispatch_marksvisit(std::string);
    SDispatchResult dispatch_marksreset(std::string);
    SDispatchResult dispatch_pin(std::string);
    SDispatchResult dispatch_selectiontoggle(std::string);
    SDispatchResult dispatch_selectionreset(std::string);
    SDispatchResult dispatch_selectionworkspace(std::string);
    SDispatchResult dispatch_selectionmove(std::string);
    SDispatchResult dispatch_trailnew(std::string);
    SDispatchResult dispatch_trailnext(std::string);
    SDispatchResult dispatch_trailprev(std::string);
    SDispatchResult dispatch_traildelete(std::string);
    SDispatchResult dispatch_trailclear(std::string);
    SDispatchResult dispatch_trailtoselection(std::string);
    SDispatchResult dispatch_trailmarktoggle(std::string);
    SDispatchResult dispatch_trailmarknext(std::string);
    SDispatchResult dispatch_trailmarkprev(std::string);
    SDispatchResult dispatch_jump(std::string);
    SDispatchResult dispatch_gridmode(std::string);
    namespace {
        using Dispatcher = SDispatchResult (*)(std::string);

        struct Action {
            std::string_view name;
            Dispatcher       handler;
        };

        constexpr std::array<Action, 39> ACTIONS = {{
            { "cyclesize", dispatch_cyclesize },
            { "recalculate", dispatch_recalculate },
            { "cyclewidth", dispatch_cyclewidth },
            { "cycleheight", dispatch_cycleheight },
            { "setsize", dispatch_setsize },
            { "setwidth", dispatch_setwidth },
            { "setheight", dispatch_setheight },
            { "movefocus", dispatch_movefocus },
            { "movefocus_nocenter", dispatch_movefocus_nocenter },
            { "movewindow", dispatch_movewindow },
            { "alignwindow", dispatch_alignwindow },
            { "admitwindow", dispatch_admitwindow },
            { "expelwindow", dispatch_expelwindow },
            { "setmode", dispatch_setmode },
            { "setmodemodifier", dispatch_setmodemodifier },
            { "fitsize", dispatch_fitsize },
            { "fitwidth", dispatch_fitwidth },
            { "fitheight", dispatch_fitheight },
            { "toggleoverview", dispatch_toggleoverview },
            { "marksadd", dispatch_marksadd },
            { "marksdelete", dispatch_marksdelete },
            { "marksvisit", dispatch_marksvisit },
            { "marksreset", dispatch_marksreset },
            { "pin", dispatch_pin },
            { "selectiontoggle", dispatch_selectiontoggle },
            { "selectionreset", dispatch_selectionreset },
            { "selectionworkspace", dispatch_selectionworkspace },
            { "selectionmove", dispatch_selectionmove },
            { "trailnew", dispatch_trailnew },
            { "trailnext", dispatch_trailnext },
            { "trailprevious", dispatch_trailprev },
            { "traildelete", dispatch_traildelete },
            { "trailclear", dispatch_trailclear },
            { "trailtoselection", dispatch_trailtoselection },
            { "trailmarktoggle", dispatch_trailmarktoggle },
            { "trailmarknext", dispatch_trailmarknext },
            { "trailmarkprevious", dispatch_trailmarkprev },
            { "jump", dispatch_jump },
            { "gridmode", dispatch_gridmode },
        }};

        const Action* find_action(std::string_view name) {
            const auto action = std::ranges::find(ACTIONS, name, &Action::name);
            return action == ACTIONS.end() ? nullptr : &*action;
        }

        int lua_dispatch(lua_State* state) {
            const auto count = lua_gettop(state);
            if (count < 1 || count > 2)
                return luaL_error(state, "hl.plugin.scroller.dispatch expects 1 or 2 arguments");
            if (lua_type(state, 1) != LUA_TSTRING)
                return luaL_error(state, "hl.plugin.scroller.dispatch action must be a string");
            if (count == 2 && lua_type(state, 2) != LUA_TSTRING && lua_type(state, 2) != LUA_TNIL)
                return luaL_error(state, "hl.plugin.scroller.dispatch argument must be a string or nil");

            const auto* action = find_action(lua_tostring(state, 1));
            if (!action)
                return luaL_error(state, "hl.plugin.scroller.dispatch unknown action '%s'", lua_tostring(state, 1));

            const auto argument = count == 2 && lua_type(state, 2) == LUA_TSTRING ? lua_tostring(state, 2) : "";
            const auto result   = action->handler(argument);
            if (!result.success)
                return luaL_error(state, "%s", result.error.empty() ? "scroller dispatcher failed" : result.error.c_str());

            return 0;
        }
    }

    std::optional<Direction> parse_move_arg(std::string arg) {
        if (arg == "l" || arg == "left")
            return Direction::Left;
        else if (arg == "r" || arg == "right")
            return Direction::Right;
        else if (arg == "u" || arg == "up")
            return Direction::Up;
        else if (arg == "d" || arg == "dn" || arg == "down")
            return Direction::Down;
        else if (arg == "b" || arg == "begin" || arg == "beginning")
            return Direction::Begin;
        else if (arg == "e" || arg == "end")
            return Direction::End;
        else if (arg == "c" || arg == "center" || arg == "centre")
            return Direction::Center;
        else if (arg == "m" || arg == "middle")
            return Direction::Middle;
        else
            return std::nullopt;
    }

    WORKSPACEID workspace_for_action() {
        if (!g_ScrollerLayout || !g_ScrollerLayout->is_enabled())
            return WORKSPACE_INVALID;

        auto monitor = Desktop::focusState()->monitor();
        if (!monitor) {
            return WORKSPACE_INVALID;
        }

        WORKSPACEID workspace_id;
        if (monitor->activeSpecialWorkspaceID()) {
            workspace_id = monitor->activeSpecialWorkspaceID();
        } else {
            workspace_id = monitor->activeWorkspaceID();
        }
        if (workspace_id == WORKSPACE_INVALID)
            return WORKSPACE_INVALID;
        auto workspace = workspace_by_id(workspace_id);
        if (workspace == nullptr)
            return WORKSPACE_INVALID;

        // Clear suppressFocusLayout for explicit dispatcher actions.
        // This flag may be left true from a mouse focus event (mouse_disable),
        // which would cause recalculate_row_geometry() to bail out and skip
        // layout updates for user-triggered actions like admitwindow/expelwindow.
        suppressFocusLayout = false;

        return workspace_id;
    }

    int parse_step_arg(const std::string &arg) {
        int step = 0;
        if (arg == "+1" || arg == "1" || arg == "next") {
            step = 1;
        } else if (arg == "-1" || arg == "prev" || arg == "previous") {
            step = -1;
        }
        return step;
    }

    SDispatchResult dispatch_recalculate(std::string arg) {
        if (!g_ScrollerLayout || !g_ScrollerLayout->is_enabled())
            return { .success = false, .error = "scroller:recalculate: layout is not active" };

        const auto monitor = Desktop::focusState()->monitor();
        if (!monitor)
            return { .success = false, .error = "scroller:recalculate: no focused monitor" };

        // Opening and clicking the display panel can leave mouse-driven focus
        // suppression enabled. A scale reflow is an explicit user action and
        // must update geometry even when that transient mouse flag is set.
        suppressFocusLayout = false;

        const auto reservedArea = monitor->m_reservedArea;
        g_layoutManager->invalidateMonitorGeometries(monitor);
        monitor->m_reservedArea = reservedArea;
        for (const auto& workspace : State::workspaceState()->workspaces()) {
            if (workspace && workspace->monitorID() == monitor->m_id && workspace->m_space)
                workspace->m_space->recheckWorkArea();
        }
        g_layoutManager->recalculateMonitor(monitor);

        if (arg == "warp") {
            for (const auto& window : Desktop::windowState()->windows()) {
                if (!window || !window->m_isMapped || window->monitorID() != monitor->m_id ||
                    !g_ScrollerLayout->isWindowTiled(window))
                    continue;

                window->positionAnimation()->warp(false);
                window->sizeAnimation()->warp(false);
            }
        }
        return {};
    }

    SDispatchResult dispatch_cyclesize(std::string arg) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:cyclesize: invalid workspace" };

        int step = parse_step_arg(arg);
        if (step != 0)
            g_ScrollerLayout->cycle_window_size(workspace, step);

        return {};
    }

    SDispatchResult dispatch_cyclewidth(std::string arg) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:cyclewidth: invalid workspace" };

        int step = parse_step_arg(arg);
        if (step != 0)
            g_ScrollerLayout->cycle_window_width(workspace, step);

        return {};
    }

    SDispatchResult dispatch_cycleheight(std::string arg) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:cycleheight: invalid workspace" };

        int step = parse_step_arg(arg);
        if (step != 0)
            g_ScrollerLayout->cycle_window_height(workspace, step);

        return {};
    }

    SDispatchResult dispatch_setsize(std::string arg) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:setsize: invalid workspace" };

        g_ScrollerLayout->set_window_size(workspace, arg);

        return {};
    }

    SDispatchResult dispatch_setwidth(std::string arg) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:setwidth: invalid workspace" };

        g_ScrollerLayout->set_window_width(workspace, arg);

        return {};
    }

    SDispatchResult dispatch_setheight(std::string arg) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:setheight: invalid workspace" };

        g_ScrollerLayout->set_window_height(workspace, arg);

        return {};
    }

    SDispatchResult dispatch_movefocus(std::string arg) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:movefocus: invalid workspace" };

        auto args = CVarList(arg);
        if (auto direction = parse_move_arg(args[0]))
            g_ScrollerLayout->move_focus(workspace, *direction);

        return {};
    }

    SDispatchResult dispatch_movefocus_nocenter(std::string arg) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:movefocus_nocenter: invalid workspace" };

        auto args = CVarList(arg);
        if (auto direction = parse_move_arg(args[0]))
            g_ScrollerLayout->move_focus_nocenter(workspace, *direction);

        return {};
    }

    SDispatchResult dispatch_movewindow(std::string arg) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:movewindow: invalid workspace" };

        auto args = CVarList(arg);
        if (auto direction = parse_move_arg(args[0])) {
            bool nomode = args.contains("nomode");
            g_ScrollerLayout->move_window(workspace, *direction, nomode);
        } else
            orig_moveActiveTo(arg);

        return {};
    }

    SDispatchResult dispatch_alignwindow(std::string arg) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:alignwindow: invalid workspace" };

        auto args = CVarList(arg);
        if (auto direction = parse_move_arg(args[0]))
            g_ScrollerLayout->align_window(workspace, *direction);

        return {};
    }

    SDispatchResult dispatch_admitwindow(std::string arg) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:admitwindow: invalid workspace" };

        AdmitExpelDirection direction;
        if (arg == "r" || arg == "right") {
            direction = AdmitExpelDirection::Right;
        } else {
            // Default is left, in case there is no arg
            direction = AdmitExpelDirection::Left;
        }
        g_ScrollerLayout->admit_window(workspace, direction);

        return {};
    }

    SDispatchResult dispatch_expelwindow(std::string arg) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:expelwindow: invalid workspace" };

        AdmitExpelDirection direction;
        if (arg == "l" || arg == "left") {
            direction = AdmitExpelDirection::Left;
        } else {
            // Default is right, in case there is no arg
            direction = AdmitExpelDirection::Right;
        }
        g_ScrollerLayout->expel_window(workspace, direction);

        return {};
    }
    SDispatchResult dispatch_setmode(std::string arg) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:setmode: invalid workspace" };

        Mode mode = Mode::Row;
        if (arg == "r" || arg == "row") {
            mode = Mode::Row;
        } else if (arg == "c" || arg == "col" || arg == "column") {
            mode = Mode::Column;
        } else if (arg == "t" || arg == "toggle") {
            mode = Mode::Toggle;
        }
        g_ScrollerLayout->set_mode(workspace, mode);

        return {};
    }
    SDispatchResult dispatch_setmodemodifier(std::string arg) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:setmodemodifier: invalid workspace" };

        ModeModifier modifier(arg);
        g_ScrollerLayout->set_mode_modifier(workspace, modifier);

        return {};
    }
    std::optional<FitSize> parse_fit_size(std::string arg) {
        if (arg == "active")
            return FitSize::Active;
        else if (arg == "visible")
            return FitSize::Visible;
        else if (arg == "all")
            return FitSize::All;
        else if (arg == "toend")
            return FitSize::ToEnd;
        else if (arg == "tobeg" || arg == "tobeginning")
            return FitSize::ToBeg;
        else
            return {};
    }
    SDispatchResult dispatch_fitsize(std::string arg) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:fitsize: invalid workspace" };

        auto args = CVarList(arg);
        if (auto fitsize = parse_fit_size(args[0])) {
            g_ScrollerLayout->fit_size(workspace, *fitsize);
        }

        return {};
    }
    SDispatchResult dispatch_fitwidth(std::string arg) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:fitwidth: invalid workspace" };

        auto args = CVarList(arg);
        if (auto fitsize = parse_fit_size(args[0])) {
            g_ScrollerLayout->fit_width(workspace, *fitsize);
        }

        return {};
    }
    SDispatchResult dispatch_fitheight(std::string arg) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:fitheight: invalid workspace" };

        auto args = CVarList(arg);
        if (auto fitsize = parse_fit_size(args[0])) {
            g_ScrollerLayout->fit_height(workspace, *fitsize);
        }

        return {};
    }
    SDispatchResult dispatch_toggleoverview(std::string) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:toggleoverview: invalid workspace" };

        g_ScrollerLayout->toggle_overview(workspace);

        return {};
    }
    SDispatchResult dispatch_marksadd(std::string arg) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:marksadd: invalid workspace" };

        g_ScrollerLayout->marks_add(arg);

        return {};
    }
    SDispatchResult dispatch_marksdelete(std::string arg) {
        if (!g_ScrollerLayout || !g_ScrollerLayout->is_enabled())
            return { .success = false, .error = "scroller:marksdelete: called while not running hyprlane" };

        g_ScrollerLayout->marks_delete(arg);

        return {};
    }
    SDispatchResult dispatch_marksvisit(std::string arg) {
        if (!g_ScrollerLayout || !g_ScrollerLayout->is_enabled())
            return { .success = false, .error = "scroller:marksvisit: called while not running hyprlane" };

        g_ScrollerLayout->marks_visit(arg);

        return {};
    }
    SDispatchResult dispatch_marksreset(std::string) {
        if (!g_ScrollerLayout || !g_ScrollerLayout->is_enabled())
            return { .success = false, .error = "scroller:marksreset: called while not running hyprlane" };

        g_ScrollerLayout->marks_reset();

        return {};
    }
    SDispatchResult dispatch_pin(std::string) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:pin: invalid workspace" };

        g_ScrollerLayout->pin(workspace);

        return {};
    }
    SDispatchResult dispatch_selectiontoggle(std::string) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:selectiontoggle: invalid workspace" };

        g_ScrollerLayout->selection_toggle(workspace);

        return {};
    }
    SDispatchResult dispatch_selectionreset(std::string) {
        if (!g_ScrollerLayout || !g_ScrollerLayout->is_enabled())
            return { .success = false, .error = "scroller:selectionreset: called while not running hyprlane" };

        g_ScrollerLayout->selection_reset();

        return {};
    }
    SDispatchResult dispatch_selectionworkspace(std::string) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:selectionworkspace: invalid workspace" };

        g_ScrollerLayout->selection_workspace(workspace);

        return {};
    }
    SDispatchResult dispatch_selectionmove(std::string arg) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:selectionmove: invalid workspace" };

        auto args = CVarList(arg);
        if (auto direction = parse_move_arg(args[0]))
            g_ScrollerLayout->selection_move(workspace, *direction);

        return {};
    }
    SDispatchResult dispatch_trailnew(std::string) {
        if (!g_ScrollerLayout || !g_ScrollerLayout->is_enabled())
            return { .success = false, .error = "scroller:trailnew: called while not running hyprlane" };

        g_ScrollerLayout->trail_new();

        return {};
    }
    SDispatchResult dispatch_trailnext(std::string) {
        if (!g_ScrollerLayout || !g_ScrollerLayout->is_enabled())
            return { .success = false, .error = "scroller:trailnext: called while not running hyprlane" };

        g_ScrollerLayout->trail_next();

        return {};
    }
    SDispatchResult dispatch_trailprev(std::string) {
        if (!g_ScrollerLayout || !g_ScrollerLayout->is_enabled())
            return { .success = false, .error = "scroller:trailprevious: called while not running hyprlane" };

        g_ScrollerLayout->trail_prev();

        return {};
    }
    SDispatchResult dispatch_traildelete(std::string) {
        if (!g_ScrollerLayout || !g_ScrollerLayout->is_enabled())
            return { .success = false, .error = "scroller:traildelete: called while not running hyprlane" };

        g_ScrollerLayout->trail_delete();

        return {};
    }
    SDispatchResult dispatch_trailclear(std::string) {
        if (!g_ScrollerLayout || !g_ScrollerLayout->is_enabled())
            return { .success = false, .error = "scroller:trailclear: called while not running hyprlane" };

        g_ScrollerLayout->trail_clear();

        return {};
    }
    SDispatchResult dispatch_trailtoselection(std::string) {
        if (!g_ScrollerLayout || !g_ScrollerLayout->is_enabled())
            return { .success = false, .error = "scroller:trailtoselection: called while not running hyprlane" };

        g_ScrollerLayout->trail_toselection();

        return {};
    }
    SDispatchResult dispatch_trailmarktoggle(std::string) {
        if (!g_ScrollerLayout || !g_ScrollerLayout->is_enabled())
            return { .success = false, .error = "scroller:trailmarktoggle: called while not running hyprlane" };

        g_ScrollerLayout->trailmark_toggle();

        return {};
    }
    SDispatchResult dispatch_trailmarknext(std::string) {
        if (!g_ScrollerLayout || !g_ScrollerLayout->is_enabled())
            return { .success = false, .error = "scroller:trailmarknext: called while not running hyprlane" };

        g_ScrollerLayout->trailmark_next();

        return {};
    }
    SDispatchResult dispatch_trailmarkprev(std::string) {
        if (!g_ScrollerLayout || !g_ScrollerLayout->is_enabled())
            return { .success = false, .error = "scroller:trailmarkprevious: called while not running hyprlane" };

        g_ScrollerLayout->trailmark_prev();

        return {};
    }
    SDispatchResult dispatch_jump(std::string) {
        if (!g_ScrollerLayout || !g_ScrollerLayout->is_enabled())
            return { .success = false, .error = "scroller:jump: called while not running hyprlane" };

        g_ScrollerLayout->jump();

        return {};
    }

    SDispatchResult dispatch_gridmode(std::string) {
        auto workspace = workspace_for_action();
        if (workspace == -1)
            return { .success = false, .error = "scroller:gridmode: invalid workspace" };

        g_ScrollerLayout->toggle_grid_mode(workspace);

        return {};
    }

    SDispatchResult dispatch_layout_message(std::string message) {
        const auto separator = message.find(' ');
        const auto command   = message.substr(0, separator);
        const auto arg       = separator == std::string::npos ? std::string{} : message.substr(separator + 1);

        if (command == "movefocus")
            return dispatch_movefocus(arg);
        if (command == "recalculate")
            return dispatch_recalculate(arg);
        if (command == "movefocus_nocenter")
            return dispatch_movefocus_nocenter(arg);
        if (command == "admitwindow")
            return dispatch_admitwindow(arg);
        if (command == "expelwindow")
            return dispatch_expelwindow(arg);
        if (command == "pin")
            return dispatch_pin(arg);

        return { .success = false, .error = "scroller layout message: unknown command " + command };
    }

    bool addDispatchers() {
        bool success = true;
        for (const auto& action : ACTIONS)
            success = HyprlandAPI::addDispatcherV2(PHANDLE, "scroller:" + std::string(action.name), action.handler) && success;

        if (Config::mgr()->type() == Config::CONFIG_LUA)
            success = HyprlandAPI::addLuaFunction(PHANDLE, "scroller", "dispatch", lua_dispatch) && success;

        return success;
    }
}
