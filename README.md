# Hyprlane

![Hyprlane demo](./assets/demo.mp4)

Hyprlane is a Hyprland tiled-layout plugin inspired by PaperWM. It arranges
managed windows in horizontal columns, stacks windows vertically inside each
column, and scrolls the workspace as focus moves.

Nexitask Company maintains Hyprlane. This README documents the plugin
for Hyprland 0.56.2.

## How Hyprlane works

Hyprlane replaces Hyprland's active layout algorithm. Its runtime structure
is:

```text
Hyprland plugin API
  -> ScrollerAlgorithm
    -> ScrollerLayout
      -> Row
        -> Column
          -> Window
```

A `Row` represents one managed workspace. The row contains a horizontal list of
`Column` objects, and each column contains a vertical list of tiled windows.
The row's viewport is the monitor work area after Hyprland's reserved areas,
gaps, and any configured workspace padding. Columns and windows can extend past
that viewport. Focus and movement update the geometry so the active item comes
into view.

### Placement modes

Row mode is the default. A new tiled window normally starts a new column. Column
mode inserts a new tiled window below the active window in the active column.
The mode belongs to the workspace row, so different workspaces can use different
modes.

Mode modifiers are also stored per row. They control the insertion position
(`after`, `before`, `end`, or `beginning`), whether the new window receives
focus (`focus` or `nofocus`), whether automatic placement is used (`manual` or
`auto:<number>`), and whether the active column or window is centered. The
`auto:<number>` parameter is clamped to at least 1. Updating only one modifier
leaves the other modifiers unchanged.

A window rule can supply placement modifiers for one matching new window. That
rule does not change the row's persistent mode or modifiers.

### Geometry and sizing

Standard column widths and window heights are fractions of the available
monitor work area. The built-in size names are:

| Name | Fraction |
| --- | ---: |
| `oneeighth` | 1/8 |
| `onesixth` | 1/6 |
| `onefourth` | 1/4 |
| `onethird` | 1/3 |
| `threeeighths` | 3/8 |
| `onehalf` | 1/2 |
| `fiveeighths` | 5/8 |
| `twothirds` | 2/3 |
| `threequarters` | 3/4 |
| `fivesixths` | 5/6 |
| `seveneighths` | 7/8 |
| `one` | 1 |

The configured width and height lists define the order used by cycling
commands. Direct size commands accept either a zero-based index in the relevant
list or a size name. Hyprland's normal interactive resize remains available,
and a manually resized item uses free geometry until another operation changes
it.

Fitting can target the active item, visible items, all items, the active item to
the end, or the beginning to the active item. Width fitting always operates on
columns. Height fitting always operates on windows in the active column.

### Views and focus

Overview temporarily scales the complete workspace bounding box to fit the
monitor. Depending on configuration, it also scales window contents and renders
layer surfaces while overview is active. Grid mode presents one window from
each applicable column on each grid row. Focus layout is a Niri-style view that
emphasizes the active window and can keep its column centered.

Marks name window targets globally. Trails hold anonymous trailmarks for
sequential navigation. Selection marks windows, and selection-based moves can
place selected windows or columns beside the active column or on another
workspace.

Pinned columns stay fixed while the rest of the row adapts around them. A window
with the clipping tag keeps its larger client geometry while Hyprlane exposes
only the visible slice when the window is inactive. Hyprland gaps, borders,
decorations, fullscreen states, special workspaces, and layer surfaces are
handled through the layout callbacks.

## Feature set

- Scrolling horizontal rows with vertically stacked windows.
- Row and column placement modes, per-workspace mode modifiers, and automatic
  insertion.
- Fractional column widths and window heights, cycling, direct sizing, manual
  resizing, and fitting.
- Focus movement with optional wrapping, no-center movement, edge focus timing,
  and optional workspace changes at vertical edges.
- Overview, grid mode, focus layout, and quick-jump labels.
- Named marks, pinned columns, selection-based moves, trails, and trailmarks.
- Touchpad scrolling, overview toggling, and workspace switching.
- Per-monitor defaults, workspace-specific padding, inactive-window clipping,
  and clipping-order enforcement.
- Hyprland fullscreen, special-workspace, gap, border, decoration, and layer
  integration.
- IPC events for overview state, mode state, marks, trails, and trailmarks.

## Compatibility and requirements

Hyprlane targets Hyprland 0.56.2. Build it with a C++ compiler, CMake, the
Hyprland 0.56.2 development headers, the matching Hyprland utility headers, and
Lua 5.5 development headers (`lua.pc` and `lua.h`).
The repository Makefile provides the normal build and installation commands.

> Hyprland plugins have an exact build/runtime ABI. Rebuild Hyprlane against
the installed Hyprland headers after every Hyprland upgrade. Hyprlane is
verified against Hyprland 0.56.2 at commit
`efb50993780079460b0cbed1363e2166a2de1d9f`. The build and runtime must use
matching headers and ABI; other Hyprland versions are unsupported until
verified.

The plugin must be loaded by Hyprland before `general:layout` is set to
`scroller`. Use the plugin-loading setup already used by your Hyprland
installation. This README does not prescribe a separate loader command.

## Installation

Run these commands from the repository root:

```sh
make
make install
```

`make` creates a release build. `make install` places the plugin at
`$XDG_CONFIG_HOME/hypr/plugins/hyprlane.so`, or at
`$HOME/.config/hypr/plugins/hyprlane.so` when `XDG_CONFIG_HOME` is unset.
Set `PLUGIN_DIR` when you use another plugin directory:

```sh
PLUGIN_DIR="$HOME/.config/hypr/plugins" make install
```

Use the plugin-loading setup already used by your Hyprland installation. After
the plugin is available to Hyprland, set the layout and bindings as shown below.
Do not run manual runtime load or unload operations. For a live replacement
after source changes, use `make reload` from this repository.
It is the only live replacement workflow documented here.

## Quick start and starter shortcuts

The following is a starter fragment, not a complete Hyprland configuration.
Every shortcut in it is user-defined. Hyprlane does not install these
bindings as defaults. The fragment assumes that the plugin is already available
to the running Hyprland installation.

```lua
hl.config({
  general = { layout = "scroller" },
  plugin = { scroller = {
    column_default_width = "onehalf",
    window_default_height = "one",
  }},
})

local mainMod = "SUPER"
local function scroller(action, arg)
  return function() hl.plugin.scroller.dispatch(action, arg) end
end

hl.bind(mainMod .. " + Return", hl.dsp.exec_cmd("kitty"))
hl.bind(mainMod .. " + Q", hl.dsp.window.close())
hl.bind(mainMod .. " + H", hl.dsp.focus({ direction = "left" }))
hl.bind(mainMod .. " + L", hl.dsp.focus({ direction = "right" }))
hl.bind(mainMod .. " + K", hl.dsp.focus({ direction = "up" }))
hl.bind(mainMod .. " + J", hl.dsp.focus({ direction = "down" }))

hl.bind(mainMod .. " + 1", scroller("setmode", "row"))
hl.bind(mainMod .. " + 2", scroller("setmode", "column"))
hl.bind(mainMod .. " + equal", scroller("cyclewidth", "next"))
hl.bind(mainMod .. " + SHIFT + equal", scroller("cycleheight", "next"))
hl.bind(mainMod .. " + Tab", scroller("toggleoverview"))
hl.bind(mainMod .. " + G", scroller("gridmode"))
hl.bind(mainMod .. " + slash", scroller("jump"))
```

Hyprland's normal directional focus and window-movement dispatchers use the
active layout, so these bindings work with the scroller layout. Use the
remaining dispatchers for marks, selections, trails, and more specialized
workflows.

The Lua API takes the dispatcher action suffix without the `scroller:` prefix.
For example, `hl.plugin.scroller.dispatch("setwidth", "onehalf")` invokes the
same operation as the legacy `scroller:setwidth` dispatcher. The optional
argument may be omitted. The exact legacy `scroller:*` names remain available
for non-Lua configurations; they are listed in the compatibility table below.

## Feature reference

### Focus and movement

Focus follows the row and column structure. Horizontal focus moves between
columns. Vertical focus moves within a column. The focus-wrap setting controls
whether movement wraps at layout edges. A separate no-center focus operation
moves focus without applying the normal centering step.

Window movement can move the active column or, with its no-mode variant, the
active window. The active column can also be aligned to the left, right, or
center of the viewport. In column mode, vertical alignment applies to the
active window. A middle alignment centers the active window and column where
that operation is applicable.

Admitting a window takes one window from a neighboring column and inserts it in
the active column. Expelling a window creates a neighboring column for one
window from the active column. These operations preserve the row's order and
respect fullscreen and clipping constraints.

### Sizing and fitting

The width of a column and the height of a window are independent. Cycling uses
the configured lists and can wrap or stop at the nearest end according to the
configuration. Direct setters accept a list index or one of the standard size
names. The mode-sensitive operations change the dimension associated with the
current row or column mode. Width-only and height-only operations always affect
their named dimension.

Fitting redistributes free geometry over a selected scope. The active scope
contains the active item. The visible scope contains the columns currently in
the viewport. The all scope covers the full row. The beginning and end scopes
cover the range between the relevant edge and the active item. Fitting is
ignored while overview is active or when the active item is fullscreen.

### Overview, grid mode, and focus layout

Overview calculates the full workspace bounding box and scales it into the
monitor work area. It temporarily exits fullscreen for the active window and
restores that state when overview ends. Window contents and layer surfaces can
be included according to their configuration switches.

Grid mode changes the presentation so rows expose one window from each
applicable column. It is useful when several columns contain vertical stacks.

Focus layout changes the geometry around the active window in the style of
Niri's focus layout. It can be disabled globally, disabled for selected
workspaces, and configured to center the active column. Mouse-triggered focus
changes can also be ignored in this layout.

### Marks, jump, and pinned columns

A mark associates a name with the active window. Visiting a mark moves focus to
that window and brings its workspace and column into view. Deleting one mark or
resetting all marks removes those targets.

Jump mode places short labels over windows so a key sequence can focus one
window directly. The label font falls back to Hyprland's configured font when no
plugin font is set.

Pinning fixes one column in the viewport. When a pinned column exists, new
windows added to it inherit the pinned state. Pinning is per workspace row and
is also reflected by the automatic `scroller:pinned` tag.

### Selection and trails

Selection toggles the active window, or selects all windows in the active
workspace. Selected windows receive the configured selection border. A selection
can move left, right, to the beginning, or to the end of the active row, and it
can move to another workspace. The operation moves the selected windows and
rebuilds their column placement.

A trail is an ordered set of locations used for sequential navigation. Trail
commands create, move through, delete, clear, or populate a trail from the
current selection. A trailmark toggles the active window as an anonymous trail
target, and the next and previous trailmark commands visit those targets.

### Clipping and workspace integration

Apply the `scroller:clip_when_inactive` tag to windows that should retain their
full client geometry while inactive windows show only a visible slice. The
clipping order option keeps clipped inactive windows to the right of normal
windows when enabled. Hyprland's pointer hit testing is integrated so clipped
areas can resolve to the intended window or remain suppressed when appropriate.

Rows recompute their available work area when monitor geometry, reserved areas,
gaps, workspace rules, layer surfaces, or padding change. Fullscreen and special
workspaces use the same layout callbacks as ordinary workspaces.

## Dispatcher reference

The following compatibility table lists the 39 legacy V2 dispatchers registered
by Hyprlane. Arguments are Hyprland dispatcher arguments unless noted otherwise.
For new Hyprland 0.56.2 Lua configurations, call the same actions through
`hl.plugin.scroller.dispatch(action, arg?)` as described above.

### Navigation and movement

| Dispatcher | Argument | Behavior | Example |
| --- | --- | --- | --- |
| `scroller:movefocus` | `<direction>` | Move focus through the row and column structure, bringing the destination into view. | `l` |
| `scroller:movefocus_nocenter` | `<direction>` | Move focus without applying the normal centering step. | `r` |
| `scroller:movewindow` | `<direction> [nomode]` | Move the active column by default. With `nomode`, move the active window instead. Unrecognized directions are passed to Hyprland's original window-movement dispatcher. | `r nomode` |
| `scroller:alignwindow` | `<direction>` | Align the active column or active window. Left and right align the column, up and down align the window, center centers the applicable item, and middle centers both where possible. | `center` |
| `scroller:admitwindow` | `[left\|right]` | Move one window from the neighboring column into the active column. The default is left; only `r` or `right` selects the right side. | `right` |
| `scroller:expelwindow` | `[left\|right]` | Move one window from the active column into a new neighboring column. The default is right; only `l` or `left` selects the left side. | `left` |

### Modes and views

| Dispatcher | Argument | Behavior | Example |
| --- | --- | --- | --- |
| `scroller:setmode` | `row\|column\|toggle` | Set row mode, set column mode, or toggle between them. `r`, `c`, `col`, `t`, and the long forms are accepted. Empty or unrecognized input selects row mode. | `column` |
| `scroller:setmodemodifier` | `<modifier list>` | Merge placement, focus, automatic-placement, and centering modifiers into the current workspace row. Unspecified fields keep their current values. | `after nofocus` |
| `scroller:gridmode` | none | Toggle grid mode for the active workspace row. | none |
| `scroller:toggleoverview` | none | Toggle overview for the active workspace row. | none |

### Sizing and fitting

| Dispatcher | Argument | Behavior | Example |
| --- | --- | --- | --- |
| `scroller:cyclesize` | `[next\|prev]` | Cycle the mode-sensitive size. Row mode changes column width; column mode changes active-window height. | `next` |
| `scroller:cyclewidth` | `[next\|prev]` | Cycle the active column width. | `prev` |
| `scroller:cycleheight` | `[next\|prev]` | Cycle the active-window height. | `next` |
| `scroller:setsize` | `<index\|size>` | Set the mode-sensitive size from a zero-based configured-list index or a standard size name. | `onehalf` |
| `scroller:setwidth` | `<index\|size>` | Set the active column width from a zero-based configured-list index or a standard size name. | `twothirds` |
| `scroller:setheight` | `<index\|size>` | Set the active-window height from a zero-based configured-list index or a standard size name. | `onehalf` |
| `scroller:fitsize` | `<scope>` | Fit the dimension selected by the current mode. | `visible` |
| `scroller:fitwidth` | `<scope>` | Fit row columns regardless of the current mode. | `all` |
| `scroller:fitheight` | `<scope>` | Fit windows in the active column regardless of the current mode. | `active` |

The size-cycle argument accepts `+1`, `1`, or `next` for the next entry, and
`-1`, `prev`, or `previous` for the previous entry. Invalid or omitted cycle
arguments do nothing. The fit scope accepts `active`, `visible`, `all`, `toend`,
`tobeg`, or `tobeginning`.

### Maintenance

| Dispatcher | Argument | Behavior | Example |
| --- | --- | --- | --- |
| `scroller:recalculate` | `[warp]` | Recalculate the focused monitor after geometry changes. The optional `warp` argument also warps tiled-window animations to their new geometry. | `warp` |

### Marks, pinning, and jump

| Dispatcher | Argument | Behavior | Example |
| --- | --- | --- | --- |
| `scroller:marksadd` | `<name>` | Associate the raw name string with the active window. | `mail` |
| `scroller:marksdelete` | `<name>` | Delete the named mark. | `mail` |
| `scroller:marksvisit` | `<name>` | Visit the named mark and focus its window. | `mail` |
| `scroller:marksreset` | none | Delete all marks. | none |
| `scroller:pin` | none | Pin the active column when no column is pinned. If a pinned column already exists, release it. | none |
| `scroller:jump` | none | Enter jump mode and show labels for available windows. | none |

### Selection

| Dispatcher | Argument | Behavior | Example |
| --- | --- | --- | --- |
| `scroller:selectiontoggle` | none | Toggle selection on the active window. | none |
| `scroller:selectionreset` | none | Clear selection across all scroller rows. | none |
| `scroller:selectionworkspace` | none | Select every window in the active workspace. | none |
| `scroller:selectionmove` | `<direction>` | Move selected windows or columns to the active workspace and place them left, right, at the beginning, or at the end of its row. Valid placement directions are left, right, beginning, and end; other input defaults to right. | `end` |

### Trails and trailmarks

| Dispatcher | Argument | Behavior | Example |
| --- | --- | --- | --- |
| `scroller:trailnew` | none | Create a new trail. | none |
| `scroller:trailnext` | none | Visit the next location in the active trail. | none |
| `scroller:trailprevious` | none | Visit the previous location in the active trail. | none |
| `scroller:traildelete` | none | Delete the active trail. | none |
| `scroller:trailclear` | none | Clear locations from the active trail. | none |
| `scroller:trailtoselection` | none | Add the current selection to the active trail. | none |
| `scroller:trailmarktoggle` | none | Toggle a trailmark on the active window. | none |
| `scroller:trailmarknext` | none | Visit the next trailmark. | none |
| `scroller:trailmarkprevious` | none | Visit the previous trailmark. | none |

Direction aliases are `l` or `left`, `r` or `right`, `u` or `up`, `d`, `dn`, or
`down`, `b`, `begin`, or `beginning`, `e` or `end`, `c`, `center`, or `centre`,
and `m` or `middle`. The movement dispatchers ignore unrecognized directions
unless their own behavior specifies a fallback.

The mode-modifier list accepts these tokens:

- Position: `after`, `before`, `end`, `beginning`, or `beg`.
- Focus: `focus` or `nofocus`.
- Automatic placement: `manual` or `auto:<integer>`.
- Centering: `center_column`, `nocenter_column`, `center_window`, or
  `nocenter_window`.

## Configuration reference

The following are the 38 registered configuration options. Integer-backed
switches accept `0` or `1`. Non-negative integer settings reject values below
zero. String lists use spaces between entries unless their syntax is described
below.

Configure these values through the Lua table, using the option names without
the `plugin:scroller:` prefix:

```lua
hl.config({
  plugin = { scroller = {
    column_default_width = "onehalf",
    window_default_height = "one",
    column_widths = "onethird onehalf twothirds one",
  }},
})
```

### Layout and sizing

| Option | Type | Default | Description | Accepted values or format |
| --- | --- | --- | --- | --- |
| `plugin:scroller:column_default_width` | String | `onehalf` | Default width for new columns. | One standard size name. |
| `plugin:scroller:window_default_height` | String | `one` | Default height for new windows. | One standard size name. |
| `plugin:scroller:column_widths` | String | `onethird onehalf twothirds one` | Ordered standard widths used by column cycling and indexed width changes. | Space-separated standard size names. |
| `plugin:scroller:window_heights` | String | `onethird onehalf twothirds one` | Ordered standard heights used by window cycling and indexed height changes. | Space-separated standard size names. |
| `plugin:scroller:monitor_options` | String | empty | Per-monitor modes, defaults, and size lists. | Parenthesized monitor entries with semicolon-separated key/value pairs. |
| `plugin:scroller:workspace_padding` | String | empty | Workspace-specific top, right, bottom, and left padding. | `workspace:vertical:horizontal` or `workspace:top:right:bottom:left`. |
| `plugin:scroller:cyclesize_wrap` | Int | `1` | Wrap size cycling at either end of the configured list. | `0` or `1`. |
| `plugin:scroller:cyclesize_closest` | Int | `1` | Start cycling from the closest configured size when the current geometry is free-sized. | `0` or `1`. |

### Focus and navigation

| Option | Type | Default | Description | Accepted values or format |
| --- | --- | --- | --- | --- |
| `plugin:scroller:focus_wrap` | Int | `1` | Wrap focus at row and column edges. | `0` or `1`. |
| `plugin:scroller:movefocus_changes_workspace` | Int | `0` | Change workspace when vertical focus or scrolling reaches an edge without a monitor in that direction. | `0` or `1`. |
| `plugin:scroller:focus_edge_ms` | Int | `400` | Milliseconds before pointer focus at a layout edge activates. | Non-negative integer. |
| `plugin:scroller:center_row_if_space_available` | Int | `0` | Center the row when all columns fit in the available width. | `0` or `1`. |
| `plugin:scroller:center_active_window` | Int | `0` | Center the active window vertically when the row mode allows it. | `0` or `1`. |
| `plugin:scroller:center_active_column` | Int | `0` | Center the active column horizontally when the row mode allows it. | `0` or `1`. |
| `plugin:scroller:avoid_focus_on_float_close` | Int | `0` | Do not restore tiled focus when a floating window closes. | `0` or `1`. |
| `plugin:scroller:avoid_focus_on_xwayland_float_close` | Int | `0` | Do not restore tiled focus when an XWayland floating window closes. | `0` or `1`. |

### Overview and jump labels

| Option | Type | Default | Description | Accepted values or format |
| --- | --- | --- | --- | --- |
| `plugin:scroller:overview_scale_content` | Int | `1` | Scale window contents while overview is active. | `0` or `1`. |
| `plugin:scroller:overview_render_layers` | Int | `1` | Render layer surfaces while overview is active. | `0` or `1`. |
| `plugin:scroller:jump_labels_font` | String | empty | Font family for jump labels. An empty value uses Hyprland's `misc:font_family`. | Font-family string, or empty. |
| `plugin:scroller:jump_labels_scale` | Float | `0.5` | Jump-label size relative to the labeled window. Rendering clamps this value to `0.1` through `1.0`. | Floating-point value. |
| `plugin:scroller:jump_labels_color` | Int | `0x80159e30` | Jump-label text color. | Hyprland color integer. |
| `plugin:scroller:jump_labels_keys` | String | `1234` | Characters used to generate jump-label sequences. | String of label characters. |

### Touchpad gestures

| Option | Type | Default | Description | Accepted values or format |
| --- | --- | --- | --- | --- |
| `plugin:scroller:gesture_sensitivity` | Float | `1.0` | Multiplier applied to swipe deltas. | Floating-point value. |
| `plugin:scroller:gesture_overview_enable` | Int | `1` | Enable overview gestures. | `0` or `1`. |
| `plugin:scroller:gesture_overview_distance` | Int | `5` | Accumulated vertical distance required to toggle overview. | Non-negative integer. |
| `plugin:scroller:gesture_overview_fingers` | Int | `4` | Finger count for overview gestures. | Non-negative integer. |
| `plugin:scroller:gesture_scroll_enable` | Int | `1` | Enable layout-scrolling gestures. | `0` or `1`. |
| `plugin:scroller:gesture_scroll_fingers` | Int | `3` | Finger count for layout-scrolling gestures. | Non-negative integer. |
| `plugin:scroller:gesture_workspace_switch_enable` | Int | `1` | Enable workspace-switching gestures. | `0` or `1`. |
| `plugin:scroller:gesture_workspace_switch_distance` | Int | `5` | Accumulated horizontal distance required to switch workspace. | Non-negative integer. |
| `plugin:scroller:gesture_workspace_switch_fingers` | Int | `4` | Finger count for workspace-switching gestures. | Non-negative integer. |
| `plugin:scroller:gesture_workspace_switch_prefix` | String | empty | Prefix prepended to the workspace dispatcher argument used by a horizontal workspace gesture. | String prefix, or empty. |

### Selection and focus layout

| Option | Type | Default | Description | Accepted values or format |
| --- | --- | --- | --- | --- |
| `plugin:scroller:col.selection_border` | Int | `0xff9e1515` | Border color for selected windows. | Hyprland color integer. |
| `plugin:scroller:focus_layout_enable` | Int | `0` | Enable the Niri-style focus layout. | `0` or `1`. |
| `plugin:scroller:focus_layout_mouse_disable` | Int | `0` | Ignore mouse-triggered focus changes in the focus layout. | `0` or `1`. |
| `plugin:scroller:focus_layout_disable_workspaces` | String | empty | Comma-separated workspace names or numeric IDs excluded from the focus layout. | Comma-separated names or numeric IDs, or empty. |
| `plugin:scroller:focus_layout_center_active` | Int | `1` | Keep the active focus-layout column centered. | `0` or `1`. |

### Clipping

| Option | Type | Default | Description | Accepted values or format |
| --- | --- | --- | --- | --- |
| `plugin:scroller:clip_window_order_enforce` | Int | `1` | Keep clipped inactive windows to the right of normal windows when enabled. | `0` or `1`. |

### Structured option syntax

`monitor_options` uses an outer parenthesized list of monitor entries. Each
entry has a monitor name, an equals sign, and an inner parenthesized list of
semicolon-separated key/value pairs. The supported keys are `mode`,
`column_default_width`, `window_default_height`, `column_widths`, and
`window_heights`.

```ini
monitor_options = (DP-2 = (mode = column; column_default_width = onehalf; window_default_height = one; column_widths = onehalf one; window_heights = onehalf one), HDMI-A-1 = (mode = row))
```

`workspace_padding` accepts either three values or five values after the
workspace selector:

```ini
workspace_padding = workspace-name:vertical:horizontal
workspace_padding = workspace-name:top:right:bottom:left
```

Workspace names are matched before numeric workspace IDs and may contain
colons, such as `special:social`. `focus_layout_disable_workspaces` is a
comma-separated list of names or IDs.

## Touchpad gestures

Hyprlane listens to Hyprland swipe events. A gesture is handled only when
its finger count matches an enabled feature, and handled events are canceled so
Hyprland does not process the same swipe a second time.

- The scroll gesture uses the configured scroll finger count. The dominant
  horizontal axis moves between columns; the dominant vertical axis moves
  within the active column. If scrolling reaches a vertical edge and workspace
  changes are enabled, the gesture can switch workspace when no monitor is
  available in that direction.
- The overview gesture uses the configured overview finger count. A vertical
  swipe past the configured distance toggles overview on or off.
- The workspace gesture uses the configured workspace-switch finger count. A
  horizontal swipe past the configured distance invokes Hyprland's workspace
  dispatcher. The configured prefix is placed before its relative workspace
  argument.

Gesture sensitivity scales the swipe delta. The implementation also honors
Hyprland's `input:touchpad:natural_scroll` setting when choosing direction and
applying that multiplier. Hyprland's workspace-swipe inversion setting affects
the direction used by workspace gestures.

## Window rules and tags

Hyprland 0.56.2 uses Lua window rules. Each rule needs a unique name, a match,
and any plugin effect as a bracketed property:

```lua
hl.window_rule({
  name = "kitty-column-mode",
  match = { class = "^(kitty)$" },
  ["plugin:scroller:modemodifier"] = "column before nofocus",
})
hl.window_rule({
  name = "firefox-column-width",
  match = { class = "^(firefox)$" },
  ["plugin:scroller:columnwidth"] = "twothirds",
})
hl.window_rule({
  name = "firefox-window-height",
  match = { class = "^(firefox)$" },
  ["plugin:scroller:windowheight"] = "onehalf",
})
```

Rule names are identities, not effect names: two differently named rules may
reuse the same effect for different matches.

`plugin:scroller:modemodifier` accepts `row`, `col`, or `column`, the placement
tokens `after`, `before`, `end`, `beg`, or `beginning`, and the focus tokens
`focus` or `nofocus`. It applies to the matching new window without changing
the row's persistent mode or modifier state.

`plugin:scroller:columnwidth` overrides the matching window's column width.
`plugin:scroller:windowheight` overrides the matching new window's height. Both
use the standard size names listed in the geometry section.

Hyprlane applies `scroller:pinned` automatically to windows in a pinned
column. The user can apply `scroller:clip_when_inactive` through a tag rule:

```lua
hl.window_rule({
  name = "kitty-clipping",
  match = { class = "^(kitty)$" },
  tag = "+scroller:clip_when_inactive",
})
hl.window_rule({
  name = "pinned-border",
  match = { tag = "scroller:pinned" },
  border_color = "rgb(eeee00) rgb(00eeee)",
})
```

The clipping tag is user-controlled. The pinned tag is maintained by the
plugin and should be treated as read-only state.

## IPC events

Hyprlane posts the public events below through Hyprland's `scroller` event
name. The payload is shown after the event name.

| Payload | Meaning |
| --- | --- |
| `overview, 0\|1` | Overview is off or on. |
| `mode, <row\|column>, <position>, <focus>, <manual\|auto>:<number>, <center-column>, <center-window>` | Current row mode and its placement, focus, automatic-placement, and centering state. |
| `mark, 0,` or `mark, 1, <mark-name>` | A mark was cleared or set to the given name. |
| `trail, <number>, <size>` | The identified trail now has the reported size. |
| `trailmark, 0\|1` | The active window is not marked or is marked as a trailmark. |

## Tutorial link and scope

Read [`TUTORIAL.md`](TUTORIAL.md) for longer examples. It covers advanced
submaps, task workflows, and practical combinations of the features described
here. This README keeps the public feature and interface reference in one
place, while the tutorial explains how to turn those pieces into a working
configuration.

## Development commands and architecture link

The Makefile provides these commands:

| Command | Purpose |
| --- | --- |
| `make` | Configure and build the release target. |
| `make debug` | Configure and build the debug target with tests enabled. |
| `make release` | Configure and build the release target. |
| `make test` | Build the debug target and run its CTest tests. |
| `make check` | Run tests, then build the release target. |
| `make install` | Install the release plugin into the configured plugin directory. |
| `make clean` | Remove build output and generated symlinks. |
| `make reload` | Replace the plugin in a running Hyprland session using the repository's build and installation workflow. |

Use `make reload` as the only live replacement workflow. The target requires a
running Hyprland instance and restores the scroller layout after replacement.
Do not replace that workflow with ad-hoc runtime operations.

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for module boundaries,
layout invariants, callback flow, and validation commands.

## Credits and lineage

Hyprlane started from the MIT-licensed hyprscroller plugin. See [NOTICE](NOTICE)
for the source repositories.

Dawser created the original plugin
([dawsers/hyprscroller](https://github.com/dawsers/hyprscroller)).
Constantin Piber maintained an intermediate tree
([cpiber/hyprscroller](https://github.com/cpiber/hyprscroller)); this checkout
was copied from that tree. Those repositories are historical sources only.

The project follows the scrolling-workspace idea established by
[PaperWM](https://github.com/paperwm/PaperWM), adapted to Hyprland's plugin and
layout interfaces. Nexitask Company maintains Hyprlane for the Hyprland 0.56.2
target.

## MIT license

Hyprlane is distributed under the [MIT License](LICENSE). The original
hyprscroller copyright notice is preserved there, with a later Nexitask
Company copyright for Hyprlane changes.
