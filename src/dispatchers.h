#ifndef SCROLLER_DISPATCHERS_H
#define SCROLLER_DISPATCHERS_H

#include <hyprland/src/SharedDefs.hpp>

#include <string>

namespace dispatchers {
    [[nodiscard]] bool addDispatchers();
    SDispatchResult dispatch_layout_message(std::string message);
    SDispatchResult dispatch_movefocus(std::string arg);
    SDispatchResult dispatch_movewindow(std::string arg);
}

#endif // SCROLLER_DISPATCHERS_H
