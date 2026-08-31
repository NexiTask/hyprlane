# Install

Hyprlane targets Hyprland 0.56.2. The plugin must be loaded before
`general:layout` is set to `scroller`.

## Hyprpm

```sh
hyprpm add https://github.com/NexiTask/hyprlane
hyprpm enable hyprlane
hyprpm reload
```

If Hyprpm reports outdated headers, run `hyprpm update`, then repeat the
installation. Hyprpm may clone the matching Hyprland source and prepare its
headers during first setup or after a Hyprland upgrade. Hyprland plugins
require an exact build and runtime ABI, so Hyprpm cannot safely skip this
step.

If the matching development headers are already installed, use the manual
commands below to avoid Hyprpm's separate header cache.

## Manual build

Build with a C++ compiler, CMake, the Hyprland 0.56.2 development headers,
the matching Hyprland utility headers, and Lua 5.5 development headers
(`lua.pc` and `lua.h`). From the repository root:

```sh
make
make install
```

`make install` places the plugin at
`$XDG_CONFIG_HOME/hypr/plugins/hyprlane.so`, or at
`$HOME/.config/hypr/plugins/hyprlane.so` when `XDG_CONFIG_HOME` is unset.
Set `PLUGIN_DIR` when you use another plugin directory:

```sh
PLUGIN_DIR="$HOME/.config/hypr/plugins" make install
```

Use the plugin-loading setup already used by your Hyprland installation.
After the plugin is available, set the layout as shown in the
[README](../README.md).

## Compatibility

Hyprland plugins have an exact build/runtime ABI. Rebuild Hyprlane against
the installed Hyprland headers after every Hyprland upgrade. Hyprlane is
verified against Hyprland 0.56.2 at commit
`efb50993780079460b0cbed1363e2166a2de1d9f`. Other Hyprland versions are
unsupported until verified.

A daily GitHub Actions check compares the latest stable Hyprland release tag
with `.github/hyprland-compatibility.json`. When the tag changes, it opens
or reopens a compatibility issue. That check does not update the supported
version. Tagging or releasing Hyprlane remains a manual gate.

## Makefile

| Command | Purpose |
| --- | --- |
| `make` | Configure and build the release target. |
| `make debug` | Configure and build the debug target with tests enabled. |
| `make release` | Configure and build the release target. |
| `make test` | Build the debug target and run its CTest tests. |
| `make check` | Run tests, then build the release target. |
| `make install` | Install the release plugin into the configured plugin directory. |
| `make clean` | Remove build output and generated symlinks. |
| `make reload` | Replace the plugin in a running Hyprland session. |

`make reload` is the only live replacement workflow. It requires a running
Hyprland instance and restores the scroller layout after replacement.

See [architecture](ARCHITECTURE.md) for module boundaries and validation.
