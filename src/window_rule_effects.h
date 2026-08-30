#pragma once

#include <optional>
#include <string>

#include <hyprland/src/desktop/DesktopTypes.hpp>

namespace window_rule_effects {

bool register_effects();
void unregister_effects();

std::optional<std::string> modemodifier(PHLWINDOW window);
std::optional<std::string> columnwidth(PHLWINDOW window);
std::optional<std::string> windowheight(PHLWINDOW window);

} // namespace window_rule_effects
