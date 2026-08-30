#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>

#include "config.h"
#include "overview.h"

#include <array>
#include <memory>

extern HANDLE PHANDLE;

using Monitor::CMonitor;

inline CFunctionHook* g_pVisibleOnMonitorHook = nullptr;
inline CFunctionHook* g_pRenderLayerHook = nullptr;
inline CFunctionHook* g_pLogicalBoxHook = nullptr;
inline CFunctionHook* g_pRenderSoftwareCursorsForHook = nullptr;
inline CFunctionHook* g_pGetMonitorFromVectorHook = nullptr;
inline CFunctionHook* g_pClosestValidHook = nullptr;
inline CFunctionHook* g_pRenderMonitorHook = nullptr;
inline CFunctionHook* g_pGetCursorPosForMonitorHook = nullptr;

std::unique_ptr<Overview> overviews;

typedef bool (*origVisibleOnMonitor)(void *thisptr, PHLMONITOR monitor);
typedef void (*origRenderLayer)(void *thisptr, PHLLS pLayer, PHLMONITOR pMonitor, const Time::steady_tp&, bool popups, bool lockscreen);
typedef CBox (*origLogicalBox)(CMonitor *thisptr);
typedef void (*origRenderSoftwareCursorsFor)(void *thisptr, PHLMONITOR pMonitor, const Time::steady_tp& now, CRegion& damage, std::optional<Vector2D> overridePos, bool forceRender);
typedef Vector2D (*origClosestValid)(void *thisptr, const Vector2D &pos);
typedef PHLMONITOR (*origGetMonitorFromVector)(void *thisptr, const Vector2D& point);
typedef void (*origRenderMonitor)(Render::IHyprRenderer *thisptr, PHLMONITOR pMonitor, bool commit);
typedef Vector2D (*origGetCursorPosForMonitor)(void *thisptr, PHLMONITOR pMonitor);

class OverviewPassElement : public IPassElement {
public:
    struct OverviewModifData {
        std::optional<Render::SRenderModifData> renderModif;
    };

    explicit OverviewPassElement(const OverviewModifData &data) : data(data) {}
    ~OverviewPassElement() override = default;

    std::vector<UP<IPassElement>> draw() override {
        if (data.renderModif.has_value())
            g_pHyprRenderer->m_renderData.renderModif = *data.renderModif;
        return {};
    }
    bool needsLiveBlur() override { return false; }
    bool needsPrecomputeBlur() override { return false; }
    bool undiscardable() override { return true; }

    const char* passName() override {
        return "OverviewPassElement";
    }
    ePassElementType type() override {
        return EK_CUSTOM;
    }

private:
    OverviewModifData data;
};

// Needed to show windows that are outside of the viewport
static bool hookVisibleOnMonitor(void *thisptr, PHLMONITOR monitor) {
    Desktop::View::CWindow *window = static_cast<Desktop::View::CWindow *>(thisptr);
    if (overviews && window && overviews->overview_enabled(window->workspaceID())) {
        return true;
    }
    return ((origVisibleOnMonitor)(g_pVisibleOnMonitorHook->m_original))(thisptr, monitor);
}

// Needed to undo the monitor scale to render layers at the original scale
static void hookRenderLayer(void *thisptr, PHLLS layer, PHLMONITOR monitor, const Time::steady_tp& time, bool popups, bool lockscreen) {
    if (!overviews || !monitor) {
        ((origRenderLayer)(g_pRenderLayerHook->m_original))(thisptr, layer, monitor, time, popups, lockscreen);
        return;
    }
    WORKSPACEID workspace = monitor->activeSpecialWorkspaceID();
    if (!workspace)
        workspace = monitor->activeWorkspaceID();
    auto &data = overviews->data_for(workspace);
    if (data.overview) {
        if (!g_scrollerConfig.overview_render_layers())
            return;
        Vector2D monitor_size = monitor->m_size;
        monitor->m_size = monitor->m_size * data.scale_i;
        Render::SRenderModifData modif_data;
        modif_data.modifs.push_back({Render::SRenderModifData::eRenderModifType::RMOD_TYPE_SCALE, data.scale_i});
        modif_data.enabled = true;
        g_pHyprRenderer->m_renderPass.add(makeUnique<OverviewPassElement>(OverviewPassElement::OverviewModifData(modif_data)));
        g_pHyprRenderer->damageMonitor(monitor);
        ((origRenderLayer)(g_pRenderLayerHook->m_original))(thisptr, layer, monitor, time, popups, lockscreen);
        g_pHyprRenderer->m_renderPass.add(makeUnique<OverviewPassElement>(OverviewPassElement::OverviewModifData(Render::SRenderModifData())));
        monitor->m_size = monitor_size;
        return;
    }
    ((origRenderLayer)(g_pRenderLayerHook->m_original))(thisptr, layer, monitor, time, popups, lockscreen);
}

// Needed to scale the range of the cursor in overview mode to cover the whole area.
static CBox hookLogicalBox(CMonitor *thisptr) {
    if (!overviews || !thisptr)
        return ((origLogicalBox)(g_pLogicalBoxHook->m_original))(thisptr);
    WORKSPACEID workspace = thisptr->activeSpecialWorkspaceID();
    if (!workspace)
        workspace = thisptr->activeWorkspaceID();
    auto &data = overviews->data_for(workspace);
    if (data.overview)
        return {thisptr->m_position, thisptr->m_size * data.scale_i};
    return ((origLogicalBox)(g_pLogicalBoxHook->m_original))(thisptr);
}

// Needed to render the software cursor only on the correct monitors.
static void hookRenderSoftwareCursorsFor(void *thisptr, PHLMONITOR monitor, const Time::steady_tp& now, CRegion& damage, std::optional<Vector2D> overridePos, bool forceRender) {
    // Should render the cursor for all the extent of the workspace, and only on
    // overview workspaces when there is one active, and it is in the current monitor.
    PHLMONITOR last = Desktop::focusState()->monitor();

    if (!overviews || !last || !monitor) {
        ((origRenderSoftwareCursorsFor)(g_pRenderSoftwareCursorsForHook->m_original))(thisptr, monitor, now, damage, overridePos, forceRender);
        return;
    }

    if (monitor == last) {
        // Render cursor
        WORKSPACEID workspace = monitor->activeSpecialWorkspaceID();
        if (!workspace)
            workspace = monitor->activeWorkspaceID();
        auto &data = overviews->data_for(workspace);
        Vector2D monitor_size = monitor->m_size;
        monitor->m_size = monitor->m_size * data.scale_i;
        ((origRenderSoftwareCursorsFor)(g_pRenderSoftwareCursorsForHook->m_original))(thisptr, monitor, now, damage, overridePos, forceRender);
        monitor->m_size = monitor_size;
    }
}

// Needed to fake an overview monitor's desktop contains all its windows
// instead of some of them being in the other monitor.
static Vector2D hookClosestValid(void *thisptr, const Vector2D& pos) {
    PHLMONITOR last = Desktop::focusState()->monitor();
    if (!overviews || !last)
        return ((origClosestValid)(g_pClosestValidHook->m_original))(thisptr, pos);
    WORKSPACEID workspace = last->activeSpecialWorkspaceID();
    if (!workspace)
        workspace = last->activeWorkspaceID();
    bool overview_enabled = overviews->overview_enabled(workspace);
    if (overview_enabled) {
        CBox bounds = last->logicalBox();
        Vector2D ret = pos;
        if (ret.x < bounds.x) ret.x = bounds.x;
        if (ret.x > bounds.x + bounds.w) ret.x = bounds.x + bounds.w;
        if (ret.y < bounds.y) ret.y = bounds.y;
        if (ret.y > bounds.y + bounds.h) ret.y = bounds.y + bounds.h;
        return ret;
    }
    return ((origClosestValid)(g_pClosestValidHook->m_original))(thisptr, pos);
}

// Needed to select the correct monitor for a cursor when two can contain it.
static PHLMONITOR hookGetMonitorFromVector(void *thisptr, const Vector2D& point) {
    if (!overviews || State::monitorState()->monitors().empty())
        return ((origGetMonitorFromVector)(g_pGetMonitorFromVectorHook->m_original))(thisptr, point);
    // First, see if the current monitor contains the point
    PHLMONITOR last = Desktop::focusState()->monitor();
    PHLMONITOR mon;
    for (auto const& m : State::monitorState()->monitors()) {
        WORKSPACEID workspace = m->activeSpecialWorkspaceID();
        if (!workspace)
            workspace = m->activeWorkspaceID();
        auto &data = overviews->data_for(workspace);
        Vector2D m_size = data.overview ? m->m_size * data.scale_i : m->m_size;
        // If the monitor contains the point
        if (CBox{m->m_position, m_size}.containsPoint(point)) {
            // Priority for last monitor
            if (m == last) {
                return last;
            }
            // Priority for monitor running overview
            if (data.overview) {
                mon = m;
            } else if (!mon) {
                mon = m;
            }
        }
    }
    if (mon)
        return mon;

    float      bestDistance = 0.f;
    PHLMONITOR pBestMon;

    for (auto const& m : State::monitorState()->monitors()) {
        float dist = vecToRectDistanceSquared(point, m->m_position, m->m_position + m->m_size);

        if (dist < bestDistance || !pBestMon) {
            bestDistance = dist;
            pBestMon     = m;
        }
    }

    return pBestMon ? pBestMon : ((origGetMonitorFromVector)(g_pGetMonitorFromVectorHook->m_original))(thisptr, point);
}

static void hookRenderMonitor(Render::IHyprRenderer *thisptr, PHLMONITOR monitor, bool commit) {
    if (!overviews || !monitor) {
        ((origRenderMonitor)(g_pRenderMonitorHook->m_original))(thisptr, monitor, commit);
        return;
    }
    WORKSPACEID workspace = monitor->activeSpecialWorkspaceID();
    if (!workspace)
        workspace = monitor->activeWorkspaceID();
    auto &data = overviews->data_for(workspace);
    float scale = monitor->m_scale;
    if (data.overview)
        monitor->m_scale *= data.scale;
    ((origRenderMonitor)(g_pRenderMonitorHook->m_original))(thisptr, monitor, commit);
    monitor->m_scale = scale;
}

// Needed to render the HW cursor at the right position
static Vector2D hookGetCursorPosForMonitor(void *thisptr, PHLMONITOR monitor) {
    if (!overviews || !monitor)
        return ((origGetCursorPosForMonitor)(g_pGetCursorPosForMonitorHook->m_original))(thisptr, monitor);
    if (Desktop::focusState()->monitor() != monitor)
        return { 0.0, monitor->m_size.y };

    WORKSPACEID workspace = monitor->activeSpecialWorkspaceID();
    if (!workspace)
        workspace = monitor->activeWorkspaceID();
    auto &data = overviews->data_for(workspace);
    auto monitor_scale = monitor->m_scale;
    if (data.overview)
        monitor->m_scale *= data.scale;
    Vector2D pos = ((origGetCursorPosForMonitor)(g_pGetCursorPosForMonitorHook->m_original))(thisptr, monitor);
    monitor->m_scale = monitor_scale;
    return pos;
}



// Find a function by name, filtering by class prefix in the demangled signature.
// This avoids hooking the wrong overload when multiple classes have the same method name.
static void* findFunctionByClass(const std::string& name, const std::string& classPrefix) {
    auto FNS = HyprlandAPI::findFunctionsByName(PHANDLE, name);
    for (auto& fn : FNS) {
        if (fn.demangled.find(classPrefix + "::" + name) != std::string::npos)
            return fn.address;
    }
    return nullptr;
}

static std::array<CFunctionHook**, 8> overviewHookSlots() {
    return {
        &g_pVisibleOnMonitorHook,
        &g_pRenderLayerHook,
        &g_pLogicalBoxHook,
        &g_pRenderSoftwareCursorsForHook,
        &g_pClosestValidHook,
        &g_pGetMonitorFromVectorHook,
        &g_pRenderMonitorHook,
        &g_pGetCursorPosForMonitorHook,
    };
}

#define DO_HOOK_CLASS(name_capital, name, className) do { \
    auto addr = findFunctionByClass(#name, className); \
    if (addr) { \
        g_p ## name_capital ## Hook = HyprlandAPI::createFunctionHook(PHANDLE, addr, reinterpret_cast<const void*>(hook ## name_capital)); \
        if (g_p ## name_capital ## Hook == nullptr) { \
            Log::logger->log(Log::WARN, "[hyprlane] Overview: Hook of " className "::" #name " failed, function found but hook not successful"); \
            return; \
        } \
    } else { \
        Log::logger->log(Log::WARN, "[hyprlane] Overview: Hook of " className "::" #name " failed, function not found"); \
        return; \
    } \
} while (0)


Overview::Overview() : initialized(false)
{
    DO_HOOK_CLASS(VisibleOnMonitor, visibleOnMonitor, "CWindow");
    DO_HOOK_CLASS(RenderLayer, renderLayer, "IHyprRenderer");
    DO_HOOK_CLASS(LogicalBox, logicalBox, "CMonitor");
    DO_HOOK_CLASS(RenderSoftwareCursorsFor, renderSoftwareCursorsFor, "CPointerManager");
    DO_HOOK_CLASS(GetMonitorFromVector, getMonitorFromVector, "CCompositor");
    DO_HOOK_CLASS(ClosestValid, closestValid, "CPointerManager");
    DO_HOOK_CLASS(RenderMonitor, renderMonitor, "IHyprRenderer");
    DO_HOOK_CLASS(GetCursorPosForMonitor, getCursorPosForMonitor, "CPointerManager");

    initialized = true;
}

Overview::~Overview()
{
    disable_hooks();

    for (auto slot : overviewHookSlots()) {
        if (*slot)
            HyprlandAPI::removeFunctionHook(PHANDLE, *slot);
        *slot = nullptr;
    }

    initialized = false;
}

bool Overview::enable(WORKSPACEID workspace)
{
    if (!initialized)
        return false;
    if (!overview_enabled()) {
        if (!enable_hooks())
            return false;
    }
    auto &data = data_for(workspace);
    data.overview = true;
    return true;
}

void Overview::disable(WORKSPACEID workspace)
{
    if (!initialized)
        return;
    for (auto &w : _workspaceData) {
        if (w.workspace == workspace) {
            w.overview = false;
            w.scale = 1.0f;
            w.scale_i = 1.0f;
        }
    }
    if (!overview_enabled()) {
        disable_hooks();
    }
}

bool Overview::overview_enabled(WORKSPACEID workspace) const
{
    if (!initialized)
        return false;
    for (auto &w : _workspaceData) {
        if (w.workspace == workspace)
            return w.overview;
    }
    return false;
}

void Overview::set_scale(WORKSPACEID workspace, float scale)
{
    if (scale <= 0.0F)
        scale = 1.0F;
    auto &data = data_for(workspace);
    data.scale = scale;
    data.scale_i = 1.0F / scale;
}

Overview::OverviewData& Overview::data_for(WORKSPACEID workspace)
{
    for (auto &w : _workspaceData) {
        if (w.workspace == workspace)
            return w;
    }
    _workspaceData.push_back({.workspace = workspace});
    return _workspaceData.back();
}

bool Overview::overview_enabled() const
{
    for (auto &workspace : _workspaceData) {
        if (workspace.overview)
            return true;
    }
    return false;
}

bool Overview::enable_hooks()
{
    if (!initialized)
        return false;

    std::array<CFunctionHook*, 8> enabled_hooks{};
    std::size_t enabled_count = 0;
    for (auto slot : overviewHookSlots()) {
        if (!*slot || !(*slot)->hook()) {
            while (enabled_count > 0)
                enabled_hooks[--enabled_count]->unhook();
            return false;
        }
        enabled_hooks[enabled_count++] = *slot;
    }
    return true;
}

void Overview::disable_hooks()
{
    if (!initialized)
        return;

    const auto slots = overviewHookSlots();
    for (auto slot = slots.rbegin(); slot != slots.rend(); ++slot) {
        if (**slot)
            (**slot)->unhook();
    }
}
