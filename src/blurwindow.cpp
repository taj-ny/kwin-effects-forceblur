#include "blurwindow.h"

#include "effect/effecthandler.h"
#include "effect/globals.h"
#include "effect/effectwindow.h"
#include "utils/xcbutils.h"
#include "wayland/blur.h"
#include "wayland/surface.h"
#include "window.h"

#ifdef KWIN_6_2_OR_GREATER
#include "scene/windowitem.h"
#endif

#ifdef KDECORATION3
#include <KDecoration3/Decoration>
#else
#include <KDecoration2/Decoration>
#endif

namespace BetterBlur
{

Window::Window(KWin::EffectWindow *w, const WindowRuleList *windowRules, long *net_wm_blur_region)
    : w(w),
      m_windowRules(windowRules),
      net_wm_blur_region(net_wm_blur_region)
{
    connect(w, &KWin::EffectWindow::windowDecorationChanged, this, [this]() {
        setupDecorationConnections();
    });
    windowFrameGeometryChangedConnection = connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this, &Window::slotWindowFrameGeometryChanged);
    setupDecorationConnections();

    if (auto surf = w->surface()) {
        windowBlurChangedConnection = connect(surf, &KWin::SurfaceInterface::blurChanged, [this]() {
            updateBlurRegion();
        });
    }

    updateProperties();
    updateBlurRegion();
}

Window::~Window()
{
    disconnect(windowBlurChangedConnection);
    disconnect(windowFrameGeometryChangedConnection);
}

bool Window::isMaximized() const
{
    return KWin::effects->clientArea(KWin::MaximizeArea, KWin::effects->activeScreen(), KWin::effects->currentDesktop()) == w->window()->frameGeometry();
}

bool Window::isMenu() const
{
    return w->isMenu() || w->isDropdownMenu() || w->isPopupMenu() || w->isPopupWindow();
}

void Window::updateBlurRegion(bool geometryChanged)
{
    std::optional<QRegion> content;
    std::optional<QRegion> frame;

    if (*net_wm_blur_region != XCB_ATOM_NONE) {
        const QByteArray value = w->readProperty(*net_wm_blur_region, XCB_ATOM_CARDINAL, 32);
        QRegion region;
        if (value.size() > 0 && !(value.size() % (4 * sizeof(uint32_t)))) {
            const uint32_t *cardinals = reinterpret_cast<const uint32_t *>(value.constData());
            for (unsigned int i = 0; i < value.size() / sizeof(uint32_t);) {
                int x = cardinals[i++];
                int y = cardinals[i++];
                int w = cardinals[i++];
                int h = cardinals[i++];
                region += KWin::Xcb::fromXNative(QRect(x, y, w, h)).toRect();
            }
        }
        if (!value.isNull()) {
            content = region;
        }
    }

    KWin::SurfaceInterface *surf = w->surface();

    if (surf && surf->blur()) {
        content = surf->blur()->region();
    }

    if (auto internal = w->internalWindow()) {
        const auto property = internal->property("kwin_blur");
        if (property.isValid()) {
            content = property.value<QRegion>();
        }
    }

    if (w->decorationHasAlpha() && decorationSupportsBlurBehind()) {
        frame = decorationBlurRegion();
    }

    // Don't override blur region for menus that already have one. The window geometry could include shadows.
    if (!((isMenu() || w->isTooltip()) && (content.has_value() || geometryChanged))) {
        const auto blurContent = m_properties->blurContent();
        const auto blurFrame = m_properties->blurDecorations();

        // On X11, EffectWindow::contentsRect() includes GTK's client-side shadows, while on Wayland, it doesn't.
        // The content region is translated by EffectWindow::contentsRect() in BlurEffect::blurRegion, causing the
        // blur region to be off on X11. The frame region is not translated, so it is used instead.
        const auto isX11WithCSD = w->isX11Client() && w->frameGeometry() != w->bufferGeometry();
        if (!isX11WithCSD && blurContent) {
            content = w->contentsRect().translated(-w->contentsRect().topLeft()).toRect();
        }
        if ((isX11WithCSD && (blurContent || blurFrame)) || (blurFrame && w->decoration())) {
            const auto frameRect = w->frameGeometry().translated(-w->x(), -w->y()).toRect();
            frame = isX11WithCSD
                ? frameRect
                : QRegion(frameRect) - w->contentsRect().toRect();
        }
    }

    if (content.has_value() || frame.has_value()) {
        m_contentBlurRegion = content;
        m_frameBlurRegion = frame;
#ifdef KWIN_6_2_OR_GREATER
        m_windowEffect = std::make_unique<KWin::ItemEffect>(w->windowItem());
#endif
    } else if (!geometryChanged) { // Blur may disappear if this method is called when window geometry changes
        m_contentBlurRegion = {};
        m_frameBlurRegion = {};
        render.clear();
#ifdef KWIN_6_2_OR_GREATER
        m_windowEffect.reset();
#endif
    }
}

QRegion Window::blurRegion() const
{
    QRegion region;

    if (m_contentBlurRegion.has_value()) {
        if (m_contentBlurRegion->isEmpty()) {
            // An empty region means that the blur effect should be enabled
            // for the whole window.
            region = w->rect().toRect();
        } else {
            if (m_frameBlurRegion.has_value()) {
                region = m_frameBlurRegion.value();
            }
            region += m_contentBlurRegion->translated(w->contentsRect().topLeft().toPoint()) & w->contentsRect().toRect();
        }
    } else if (m_frameBlurRegion.has_value()) {
        region = m_frameBlurRegion.value();
    }

    return region;
}

bool Window::shouldBlur(int mask, const KWin::WindowPaintData &data)
{
    const bool hasForceBlurRole = w->data(KWin::WindowForceBlurRole).toBool();
    if ((KWin::effects->activeFullScreenEffect() && !hasForceBlurRole) || w->isDesktop()) {
        return false;
    }

    bool scaled = !qFuzzyCompare(data.xScale(), 1.0) && !qFuzzyCompare(data.yScale(), 1.0);
    bool translated = data.xTranslation() || data.yTranslation();
    if (!(scaled || (translated || (mask & KWin::Effect::PAINT_WINDOW_TRANSFORMED)))) {
        m_blurWhenTransformed = false;
        return true;
    }

    // The force blur role may be removed while the window is still transformed, causing the blur to disappear for
    // a short time. To avoid that, we allow the window to be blurred until it's not transformed anymore.
    if (m_blurWhenTransformed) {
        return true;
    } else if (hasForceBlurRole) {
        m_blurWhenTransformed = true;
    }

    return hasForceBlurRole;
}

QRegion Window::decorationBlurRegion() const
{
    if (!decorationSupportsBlurBehind()) {
        return {};
    }

    QRect decorationRect = w->decoration()->rect()
#ifdef KDECORATION3
        .toAlignedRect()
#endif
        ;
    QRegion decorationRegion = QRegion(decorationRect) - w->contentsRect().toRect();
    //! we return only blurred regions that belong to decoration region
    return decorationRegion.intersected(w->decoration()->blurRegion());
}

bool Window::decorationSupportsBlurBehind() const
{
    return w->decoration() && !w->decoration()->blurRegion().isNull();
}

void Window::slotWindowFrameGeometryChanged(KWin::EffectWindow *w)
{
    if (w->size() != m_size) {
        updateProperties();
    }

    updateBlurRegion(true);
    m_size = w->size();
}

void Window::setupDecorationConnections()
{
    if (!w->decoration()) {
        return;
    }

    connect(w->decoration(),
#ifdef KDECORATION3
        &KDecoration3::Decoration::blurRegionChanged
#else
        &KDecoration2::Decoration::blurRegionChanged
#endif
    , this, [this]() {
        updateBlurRegion(w);
    });
}

void Window::updateProperties()
{
    // TODO Rule evaluation needs to be optimized. Sometimes it's not necessary to evaluate a rule.

    auto staticBlur = false;
    uint8_t blurStrength = 0;
    if (m_properties) {
        staticBlur = m_properties->staticBlur();
        blurStrength = m_properties->blurStrength();
    }

    m_properties = std::make_unique<WindowProperties>();
    for (const auto &rule : m_windowRules->rules()) {
        if (!rule->matches(this)) {
            continue;
        }

        auto ruleProperties = rule->properties();
        if (ruleProperties->m_blurContent) {
            m_properties->setBlurContent(*ruleProperties->m_blurContent);
        }
        if (ruleProperties->m_blurStrength) {
            m_properties->setBlurStrength(*ruleProperties->m_blurStrength);
        }
        if (ruleProperties->m_blurDecorations) {
            m_properties->setBlurDecorations(*ruleProperties->m_blurDecorations);
        }
        if (ruleProperties->m_forceTransparency) {
            m_properties->setForceTransparency(*ruleProperties->m_forceTransparency);
        }
        if (ruleProperties->m_bottomCornerRadius) {
            m_properties->setBottomCornerRadius(*ruleProperties->m_bottomCornerRadius);
        }
        if (ruleProperties->m_topCornerRadius) {
            m_properties->setTopCornerRadius(*ruleProperties->m_topCornerRadius);
        }
        if (ruleProperties->m_cornerAntialiasing) {
            m_properties->setCornerAntialiasing(*ruleProperties->m_cornerAntialiasing);
        }
        if (ruleProperties->m_staticBlur) {
            m_properties->setStaticBlur(*ruleProperties->m_staticBlur);
        }
        if (ruleProperties->m_windowOpacityAffectsBlur) {
            m_properties->setWindowOpacityAffectsBlur(*ruleProperties->m_windowOpacityAffectsBlur);
        }
    }

    if (staticBlur != m_properties->staticBlur() || blurStrength != m_properties->blurStrength()) {
        w->addRepaintFull();
    }
}

void Window::setHasWindowBehind(const bool &hasWindowBehind)
{
    const auto old = m_hasWindowBehind;
    m_hasWindowBehind = hasWindowBehind;
    if (old != m_hasWindowBehind) {
        updateProperties();
    }
}

}

#include "moc_blurwindow.cpp"