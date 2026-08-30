#ifndef SCROLLER_DECORATIONS_H
#define SCROLLER_DECORATIONS_H

#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/render/Texture.hpp>

class Window;

class SelectionBorders : public IHyprWindowDecoration {
  public:
    explicit SelectionBorders(Window *);
    ~SelectionBorders() override;

    SDecorationPositioningInfo getPositioningInfo() override;
    void                       onPositioningReply(const SDecorationPositioningReply& reply) override;
    void                       draw(PHLMONITOR, float const& a) override;
    eDecorationType            getDecorationType() override;
    void                       updateWindow(PHLWINDOW) override;
    void                       damageEntire() override;
    eDecorationLayer           getDecorationLayer() override;
    uint64_t                   getDecorationFlags() override;
    std::string                getDisplayName() override;

  private:
    const Window *window;

    SBoxExtents  m_seExtents;
    PHLWINDOWREF m_pWindow;
    CBox         m_bAssignedGeometry = {0};
    int          m_iLastBorderSize = -1;
    CBox         assignedBoxGlobal();
    bool         doesntWantBorders();
};

class JumpDecoration : public IHyprWindowDecoration {
  public:
    JumpDecoration(PHLWINDOW, const std::string &label);
    ~JumpDecoration() override;

    SDecorationPositioningInfo getPositioningInfo() override;
    void                       onPositioningReply(const SDecorationPositioningReply& reply) override;
    void                       draw(PHLMONITOR, float const& a) override;
    eDecorationType            getDecorationType() override;
    void                       updateWindow(PHLWINDOW) override;
    void                       damageEntire() override;
    eDecorationLayer           getDecorationLayer() override;
    uint64_t                   getDecorationFlags() override;
    std::string                getDisplayName() override;

  private:
    PHLWINDOWREF m_pWindow;
    CBox m_bAssignedGeometry = { 0 };
    CBox assignedBoxGlobal();

    std::string m_sLabel;
    SP<Render::ITexture> m_pTexture;
};

#endif  // SCROLLER_DECORATIONS_H
