#pragma once

#include "windowrules/windowproperties.h"
#include "windowrules/windowrulelist.h"

#include "effect/effect.h"
#include "effect/effectwindow.h"
#include "opengl/glutils.h"

#ifdef KWIN_6_2_OR_GREATER
#include "scene/item.h"
#endif

namespace BetterBlur
{

class WindowRuleList;

struct BlurRenderData
{
    /// Temporary render targets needed for the Dual Kawase algorithm, the first texture
    /// contains not blurred background behind the window, it's cached.
    std::vector<std::unique_ptr<KWin::GLTexture>> textures;
    std::vector<std::unique_ptr<KWin::GLFramebuffer>> framebuffers;
};

class Window : public QObject
{
    Q_OBJECT
public:
    Window(KWin::EffectWindow *w, const WindowRuleList *windowRules, long net_wm_blur_region);
    ~Window() override;

    bool isMaximized() const;
    bool isMenu() const;

    void updateBlurRegion(bool geometryChanged = false);
    QRegion blurRegion() const;
    bool shouldBlur(int mask, const KWin::WindowPaintData &data);

    const WindowProperties *properties() const
    {
        return m_properties.get();
    }
    void updateProperties();

    KWin::EffectWindow *window() const
    {
        return w;
    }

    bool hasWindowBehind() const
    {
        return m_hasWindowBehind;
    }
    void setHasWindowBehind(const bool &hasWindowBehind);

    /// The render data per screen. Screens can have different color spaces.
    std::unordered_map<KWin::Output *, BlurRenderData> render;

public Q_SLOTS:
    void slotWindowExpandedGeometryChanged(KWin::EffectWindow *w);
//    void slotDecorationBlurRegionChanged();

private:
    void setupDecorationConnections();

    QRegion decorationBlurRegion() const;
    bool decorationSupportsBlurBehind() const;

    QMetaObject::Connection windowBlurChangedConnection;
    QMetaObject::Connection windowExpandedGeometryChangedConnection;

    std::optional<QRegion> m_contentBlurRegion;
    std::optional<QRegion> m_frameBlurRegion;
    bool m_blurWhenTransformed = false;

#ifdef KWIN_6_2_OR_GREATER
    std::unique_ptr<KWin::ItemEffect> m_windowEffect;
#endif

    QSizeF m_size;
    bool m_hasWindowBehind = false;
    std::unique_ptr<WindowProperties> m_properties;

    KWin::EffectWindow *w;
    const WindowRuleList *m_windowRules;
    long net_wm_blur_region = 0;
};

}