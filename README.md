# Hyprlane

[![Hyprlane demo](./assets/demo.webp)](./assets/demo.mp4)

Click the animated preview to open the full-quality MP4.

Hyprlane is a Hyprland tiled-layout plugin inspired by PaperWM. It arranges
managed windows in horizontal columns, stacks windows vertically inside each
column, and scrolls the workspace as focus moves. It targets Hyprland 0.56.2.

## Install

```sh
hyprpm add https://github.com/NexiTask/hyprlane
hyprpm enable hyprlane
hyprpm reload
```

Manual builds, ABI notes, and `make reload` are in
[docs/install.md](docs/install.md).

## Configure

Load the plugin, then set the layout. Hyprlane does not ship default
keybindings.

```lua
hl.config({
  general = { layout = "scroller" },
  plugin = { scroller = {
    column_default_width = "onehalf",
    window_default_height = "one",
  }},
})

local function scroller(action, arg)
  return function() hl.plugin.scroller.dispatch(action, arg) end
end

hl.bind("SUPER + 1", scroller("setmode", "row"))
hl.bind("SUPER + 2", scroller("setmode", "column"))
hl.bind("SUPER + equal", scroller("cyclewidth", "next"))
hl.bind("SUPER + Tab", scroller("toggleoverview"))
```

Hyprland's normal directional focus bindings work with this layout. Lua
calls use the dispatcher suffix without the `scroller:` prefix.

## Docs

- [Install and build](docs/install.md)
- [Tutorial](docs/tutorial.md)
- [Reference](docs/reference.md) — dispatchers, config, gestures, window rules, IPC
- [Architecture](docs/ARCHITECTURE.md)

## Credits

Hyprlane started from the MIT-licensed hyprscroller plugin. See
[NOTICE](NOTICE) and [LICENSE](LICENSE). Dawser created the original plugin;
Constantin Piber maintained an intermediate tree. Nexitask Company maintains
Hyprlane. The layout idea comes from [PaperWM](https://github.com/paperwm/PaperWM).
