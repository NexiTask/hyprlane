#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "config.h"
#include "dispatchers.h"
#include "scroller.h"
#include "window_rule_effects.h"

HANDLE PHANDLE = nullptr;
std::unique_ptr<ScrollerLayout> g_ScrollerLayout;

#if defined(__clang__)
// Hyprland's public plugin ABI intentionally exports C-linkage entry points
// whose signatures contain C++ standard-library types.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
#endif

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const auto fail_initialization = [](const std::string& message) -> void {
        window_rule_effects::unregister_effects();
        HyprlandAPI::addNotification(
            PHANDLE,
            "[hyprlane] Failure in initialization: " + message,
            CHyprColor{1.0F, 0.2F, 0.2F, 1.0F},
            5000);
        throw std::runtime_error("[hyprlane] " + message);
    };

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENT_HASH) {
        fail_initialization("version mismatch between build headers and the running Hyprland");
    }

    if (!g_scrollerConfig.register_values(PHANDLE))
        fail_initialization("configuration registration failed");

    if (!window_rule_effects::register_effects())
        fail_initialization("window-rule effect registration failed");

    if (!HyprlandAPI::reloadConfig())
        fail_initialization("configuration reload could not be queued");

    g_ScrollerLayout = std::make_unique<ScrollerLayout>();
    if (!dispatchers::addDispatchers()) {
        g_ScrollerLayout.reset();
        fail_initialization("dispatcher registration failed");
    }

    // Register tiled algo AFTER config values are registered and reloaded,
    // because registration triggers updateWorkspaceLayouts which creates
    // Row objects that read config values via ScrollerSizes::update().
    if (!HyprlandAPI::addTiledAlgo(PHANDLE, "scroller",
        &typeid(ScrollerAlgorithm),
        []() -> UP<Layout::ITiledAlgorithm> {
            return makeUnique<ScrollerAlgorithm>();
        }
    )) {
        g_ScrollerLayout.reset();
        fail_initialization("layout algorithm registration failed");
    }

    // Enable the layout after config is registered
    g_ScrollerLayout->onEnable();

    return {"hyprlane", "scrolling window layout",
        "Nexitask Company (current maintainer); Dawser and Constantin Piber (historical authors)", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    if (g_pHyprRenderer)
        g_pHyprRenderer->m_renderPass.removeAllOfType("OverviewPassElement");

    if (g_ScrollerLayout)
        g_ScrollerLayout->onDisable();

    if (PHANDLE)
        HyprlandAPI::removeAlgo(PHANDLE, "scroller");

    if (g_ScrollerLayout)
        g_ScrollerLayout.reset();

    window_rule_effects::unregister_effects();

    PHANDLE = nullptr;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
