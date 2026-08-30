#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>

#include "config.h"
#include "decorations.h"
#include "window.h"

#include <pango/pangocairo.h>

// SelectionBorders
SelectionBorders::SelectionBorders(Window *window) : IHyprWindowDecoration(window->get_window()), window(window) {
    m_pWindow = window->get_window();
}

SelectionBorders::~SelectionBorders() = default;

SDecorationPositioningInfo SelectionBorders::getPositioningInfo() {
    // Avoid duplicating the border, we will draw over it
    m_seExtents = {{}, {}};

    SDecorationPositioningInfo info;
    info.priority       = 10000;
    info.policy         = DECORATION_POSITION_STICKY;
    info.desiredExtents = m_seExtents;
    info.reserved       = true;
    info.edges          = DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT | DECORATION_EDGE_TOP;

    return info;
}

void SelectionBorders::onPositioningReply(const SDecorationPositioningReply& reply) {
    m_bAssignedGeometry = reply.assignedGeometry;
}

CBox SelectionBorders::assignedBoxGlobal() {
    CBox box = m_bAssignedGeometry;
    const auto pWindow = m_pWindow.lock();
    if (!pWindow)
        return box;
    box.translate(g_pDecorationPositioner->getEdgeDefinedPoint(DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT | DECORATION_EDGE_TOP, pWindow));

    const auto PWORKSPACE = pWindow->m_workspace;

    if (!PWORKSPACE)
        return box;

    const auto WORKSPACEOFFSET = !pWindow->m_pinned ? PWORKSPACE->m_renderOffset->value() : Vector2D();
    return box.translate(WORKSPACEOFFSET);
}

void SelectionBorders::draw(PHLMONITOR pMonitor, float const& a) {
    const auto pWindow = m_pWindow.lock();
    if (!pWindow || !pMonitor || doesntWantBorders())
        return;

    if (m_bAssignedGeometry.width < m_seExtents.topLeft.x + 1 || m_bAssignedGeometry.height < m_seExtents.topLeft.y + 1)
        return;

    CBox windowBox = assignedBoxGlobal().translate(-pMonitor->m_position + pWindow->m_floatingOffset).expand(-pWindow->getRealBorderSize()).scale(pMonitor->m_scale).round();

    if (windowBox.width < 1 || windowBox.height < 1)
        return;

    auto       grad     = window->get_border_color();
    const bool ANIMATED = pWindow->m_borderFadeAnimationProgress->isBeingAnimated();

    if (pWindow->m_borderAngleAnimationProgress->enabled()) {
        grad.m_angle += pWindow->m_borderAngleAnimationProgress->value() * M_PI * 2;
        grad.m_angle = normalizeAngleRad(grad.m_angle);
    }

    int        borderSize = pWindow->getRealBorderSize();
    const auto ROUNDING   = pWindow->rounding() * pMonitor->m_scale;
    const auto ROUNDINGPOWER = pWindow->roundingPower();

    CBorderPassElement::SBorderData data;
    data.box           = windowBox;
    data.grad1         = grad;
    data.round         = ROUNDING;
    data.roundingPower = ROUNDINGPOWER;
    data.a             = a;
    data.borderSize    = borderSize;

    if (ANIMATED) {
        data.hasGrad2 = true;
        data.grad1    = pWindow->m_realBorderColorPrevious;
        data.grad2    = grad;
        data.lerp     = pWindow->m_borderFadeAnimationProgress->value();
    }

    g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(data));
}

eDecorationType SelectionBorders::getDecorationType() {
    return DECORATION_BORDER;
}

void SelectionBorders::updateWindow(PHLWINDOW) {
    const auto pWindow = m_pWindow.lock();
    if (!pWindow)
        return;
    auto borderSize = pWindow->getRealBorderSize();

    if (borderSize == m_iLastBorderSize)
        return;

    if (borderSize <= 0 && m_iLastBorderSize <= 0)
        return;

    m_iLastBorderSize = borderSize;

    g_pDecorationPositioner->repositionDeco(this);
}

void SelectionBorders::damageEntire() {
    const auto pWindow = m_pWindow.lock();
    if (!pWindow || !validMapped(pWindow))
        return;

    auto       surfaceBox   = pWindow->getWindowMainSurfaceBox();
    const auto ROUNDING     = pWindow->rounding();
    const auto ROUNDINGSIZE = ROUNDING - M_SQRT1_2 * ROUNDING + 2;
    const auto BORDERSIZE   = pWindow->getRealBorderSize() + 1;

    const auto PWINDOWWORKSPACE = pWindow->m_workspace;
    if (PWINDOWWORKSPACE && PWINDOWWORKSPACE->m_renderOffset->isBeingAnimated() && !pWindow->m_pinned)
        surfaceBox.translate(PWINDOWWORKSPACE->m_renderOffset->value());
    surfaceBox.translate(pWindow->m_floatingOffset);

    CBox surfaceBoxExpandedBorder = surfaceBox;
    surfaceBoxExpandedBorder.expand(BORDERSIZE);
    CBox surfaceBoxShrunkRounding = surfaceBox;
    surfaceBoxShrunkRounding.expand(-ROUNDINGSIZE);

    CRegion borderRegion(surfaceBoxExpandedBorder);
    borderRegion.subtract(surfaceBoxShrunkRounding);

    for (auto const& m : State::monitorState()->monitors()) {
        if (!g_pHyprRenderer->shouldRenderWindow(pWindow, m)) {
            const CRegion monitorRegion({m->m_position, m->m_size});
            borderRegion.subtract(monitorRegion);
        }
    }

    g_pHyprRenderer->damageRegion(borderRegion);
}

eDecorationLayer SelectionBorders::getDecorationLayer() {
    return DECORATION_LAYER_OVER;
}

uint64_t SelectionBorders::getDecorationFlags() {
    return !doesntWantBorders() ? DECORATION_PART_OF_MAIN_WINDOW : 0;
}

std::string SelectionBorders::getDisplayName() {
    return "Border";
}

bool SelectionBorders::doesntWantBorders() {
    const auto pWindow = m_pWindow.lock();
    return !pWindow || pWindow->m_X11DoesntWantBorders || pWindow->getRealBorderSize() == 0;
}


// JumpDecoration
JumpDecoration::JumpDecoration(PHLWINDOW window, const std::string &label)
    : IHyprWindowDecoration(window), m_pWindow(window), m_sLabel(label) {}

JumpDecoration::~JumpDecoration() = default;

SDecorationPositioningInfo JumpDecoration::getPositioningInfo() {
    SDecorationPositioningInfo info;
    info.policy = DECORATION_POSITION_STICKY;
    info.edges = DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT | DECORATION_EDGE_TOP;
    return info;
}

void JumpDecoration::onPositioningReply(const SDecorationPositioningReply& reply) {
    m_bAssignedGeometry = reply.assignedGeometry;
}

CBox JumpDecoration::assignedBoxGlobal() {
    CBox box = m_bAssignedGeometry;
    const auto pWindow = m_pWindow.lock();
    if (!pWindow)
        return box;
    box.translate(g_pDecorationPositioner->getEdgeDefinedPoint(DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT | DECORATION_EDGE_TOP, pWindow));
    if (box.w > box.h) {
        box.x += 0.5 * (box.w - box.h);
        box.w = box.h;
    } else {
        box.y += 0.5 * (box.h - box.w);
        box.h = box.w;
    }

    const auto configured_scale = g_scrollerConfig.jump_labels_scale();
    const double scale = configured_scale < 0.1F ? 0.1 : configured_scale > 1.0F ? 1.0 : configured_scale;
    box.scaleFromCenter(scale);

    const auto PWORKSPACE = pWindow->m_workspace;

    if (!PWORKSPACE)
        return box;

    const auto WORKSPACEOFFSET = PWORKSPACE->m_renderOffset->value();
    return box.translate(WORKSPACEOFFSET);
}

void JumpDecoration::draw(PHLMONITOR pMonitor, float const&) {
    if (!m_pWindow.lock() || !pMonitor)
        return;
    CBox windowBox = assignedBoxGlobal().translate(-pMonitor->m_position).scale(pMonitor->m_scale).round();

    if (windowBox.width < 1 || windowBox.height < 1)
        return;
    
    if (m_pTexture.get() == nullptr) {
        static auto  FALLBACKFONT = CConfigValue<std::string>("misc:font_family");
        const CHyprColor color = CHyprColor(g_scrollerConfig.jump_labels_color());
        std::string font_family = g_scrollerConfig.jump_labels_font();
        if (font_family == "")
            font_family = *FALLBACKFONT;

        if (m_sLabel.empty()) {
            return;  // Can't draw empty label
        }
        auto TEXTFONTSIZE = windowBox.width / m_sLabel.size();
        m_pTexture = g_pHyprRenderer->renderText(m_sLabel, color, TEXTFONTSIZE, false, font_family, windowBox.width);
    }

    CTexPassElement::SRenderData data;
    data.tex = m_pTexture;
    data.box = windowBox;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(data));
}

eDecorationType JumpDecoration::getDecorationType() {
    return DECORATION_CUSTOM;
}

void JumpDecoration::updateWindow(PHLWINDOW) {
}

void JumpDecoration::damageEntire() {
}

eDecorationLayer JumpDecoration::getDecorationLayer() {
    return DECORATION_LAYER_OVER;
}

uint64_t JumpDecoration::getDecorationFlags() {
    return DECORATION_PART_OF_MAIN_WINDOW;
}

std::string JumpDecoration::getDisplayName() {
    return "Overview";
}
