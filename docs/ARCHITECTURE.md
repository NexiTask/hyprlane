# Architecture

Hyprlane adapts Hyprland's tiled-algorithm API to a workspace-oriented
scrolling model. This document describes the current Hyprlane tree, not every
historical version of the source it started from.

## Runtime model

```text
Hyprland plugin API
        |
        v
ScrollerAlgorithm (one adapter per Hyprland layout space)
        |
        v
ScrollerLayout (plugin-wide coordinator)
        |
        +-- Row (one per managed workspace)
              |
              +-- Column (horizontal ordering)
                    |
                    +-- Window (vertical ordering and geometry)
```

`ScrollerLayout` owns rows with `std::unique_ptr`. A `Row` owns its columns and
a `Column` owns its window wrappers. Column/window ownership is currently
explicit because admit, expel, and cross-workspace selection operations
transfer those objects between containers. The custom `List` owns list nodes,
never the raw pointer payloads used by those transfer paths.

Hyprland windows, workspaces, and monitors are retained through Hyprland's
strong/weak handle types. Every callback boundary must revalidate a weak handle
before dereferencing it because unmap, workspace removal, and plugin teardown
can invalidate state between events.

## Module boundaries

- `main.cpp`: ABI check, configuration/dispatcher/algorithm registration, and
  plugin teardown.
- `config.*`: the complete typed V2 configuration registry. Add new plugin
  configuration here rather than distributing string lookups across modules.
- `dispatchers.*`: validates dispatcher context and translates user arguments
  into domain operations. It exposes both the legacy V2 `scroller:*` names and
  the Hyprland 0.56.2 Lua entry point `hl.plugin.scroller.dispatch(action,
  arg?)`; both paths use the same exact-action registry.
- `scroller.*`: Hyprland event integration, layout coordination, focus and
  gesture routing, marks/trails, and the `ITiledAlgorithm` adapter.
- `row.*`: workspace-level column ordering and layout policies.
- `column.*`: vertical window ordering and column geometry.
- `window.*`: the smallest geometry/decorations wrapper around a Hyprland
  window handle.
- `overview.*`: version-sensitive internal rendering hooks. Hook activation is
  atomic: a partial failure is rolled back before returning.
- `workspace_config.*`: compositor-independent parsing/cache for workspace
  filters and CSS-style padding.
- `window_rule_effects.*`: registers the dynamic Lua/legacy window-rule effects
  for mode modifiers, column widths, and window heights, and reads their
  per-window applied values.
- `sizes.*` and `enums.*`: domain parsing and configured size/mode policy.
- `list.h`: move-aware intrusive node container used where stable node
  addresses are part of the layout state.

## Invariants

1. One managed window belongs to exactly one `Row`, one `Column`, and one
   `Window` wrapper.
2. Empty columns and rows are removed immediately after transfer/removal.
3. A row's `active` pointer is null only while the row is empty or in a bounded
   transfer operation.
4. Row construction receives its monitor explicitly; it must not mutate global
   monitor focus to obtain geometry.
5. Overview and hit-test hooks are removed before their managers are destroyed.
6. The plugin binary must be built against the exact running Hyprland ABI and
   Lua 5.5 development interface.
7. Direct plugin load/unload is not a supported development workflow. `make
   reload` first switches to `master`, installs the exact built artifact, then
   loads and switches back to `scroller`.

## Configuration changes

Add a typed value and accessor in `config.*`, include it in
`ScrollerConfig::register_values`, document it in [docs/reference.md](reference.md), and consume the
accessor from domain code. Integer toggles and non-negative values should carry
V2 constraints. Do not reintroduce legacy `getConfigValue` calls.

## Validation gates

```sh
make test
make release
make check
```

`make test` compiles the plugin and unit tests with project warnings promoted
to errors. Unit tests cover compositor-independent containers and parsers.
The runtime target is Hyprland 0.56.2 at ABI commit
`efb50993780079460b0cbed1363e2166a2de1d9f`; rebuild after every compositor
upgrade.
`make release` proves the optimized plugin links against the installed
Hyprland dependencies. Before handing off runtime-affecting changes, use `make
reload`, confirm the plugin is listed by `hyprctl plugins list`, and exercise
focus, resize, overview, cross-workspace movement, and clipped-window input on
the live compositor.

## Version-sensitive areas

`overview.cpp` and the clipped-window `CViewHitTester::windowAt` hook use
Hyprland internals because no stable plugin API provides the required behavior.
On a Hyprland upgrade, verify symbol discovery, signatures, hook rollback, and
unload behavior before treating a successful compile as compatibility.
