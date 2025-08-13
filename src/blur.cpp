/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2011 Philipp Knechtges <philipp-dev@knechtges.com>
    SPDX-FileCopyrightText: 2018 Alex Nemeth <alex.nemeth329@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "blur.h"
// KConfigSkeleton
#include "blurconfig.h"

#include "core/pixelgrid.h"
#include "core/rendertarget.h"
#include "core/renderviewport.h"
#include "effect/effecthandler.h"
#include "opengl/glutils.h"
#include "opengl/glplatform.h"
#include "scene/decorationitem.h"
#include "scene/surfaceitem.h"
#include "scene/windowitem.h"
#include "utils.h"
#include "utils/xcbutils.h"
#include "wayland/blur.h"
#include "wayland/display.h"
#include "wayland/surface.h"

#include <QGuiApplication>
#include <QImage>
#include <QMatrix4x4>
#include <QScreen>
#include <QTime>
#include <QTimer>
#include <QWindow>
#include <cmath> // for ceil()
#include <cstdlib>

#include <KConfigGroup>
#include <KSharedConfig>

#include <KDecoration3/Decoration>

#include <utility>

Q_LOGGING_CATEGORY(KWIN_BLUR, "kwin_better_blur", QtWarningMsg)

static void ensureResources()
{
    // Must initialize resources manually because the effect is a static lib.
    Q_INIT_RESOURCE(blur);
}

namespace KWin
{

static const QByteArray s_blurAtomName = QByteArrayLiteral("_KDE_NET_WM_BLUR_BEHIND_REGION");

BlurManagerInterface *BlurEffect::s_blurManager = nullptr;
QTimer *BlurEffect::s_blurManagerRemoveTimer = nullptr;

BlurEffect::BlurEffect()
{
    BlurConfig::instance(effects->config());
    ensureResources();

    m_downsamplePass.shader = ShaderManager::instance()->generateShaderFromFile(ShaderTrait::MapTexture,
                                                                                QStringLiteral(":/effects/forceblur/shaders/vertex.vert"),
                                                                                QStringLiteral(":/effects/forceblur/shaders/downsample.frag"));
    if (!m_downsamplePass.shader) {
        qCWarning(KWIN_BLUR) << "Failed to load downsampling pass shader";
        return;
    } else {
        m_downsamplePass.mvpMatrixLocation = m_downsamplePass.shader->uniformLocation("modelViewProjectionMatrix");
        m_downsamplePass.offsetLocation = m_downsamplePass.shader->uniformLocation("offset");
        m_downsamplePass.halfpixelLocation = m_downsamplePass.shader->uniformLocation("halfpixel");
        m_downsamplePass.transformColorsLocation = m_downsamplePass.shader->uniformLocation("transformColors");
        m_downsamplePass.colorMatrixLocation = m_downsamplePass.shader->uniformLocation("colorMatrix");
    }

    m_upsamplePass.shader = ShaderManager::instance()->generateShaderFromFile(ShaderTrait::MapTexture,
                                                                              QStringLiteral(":/effects/forceblur/shaders/vertex.vert"),
                                                                              QStringLiteral(":/effects/forceblur/shaders/upsample.frag"));
    if (!m_upsamplePass.shader) {
        qCWarning(KWIN_BLUR) << "Failed to load upsampling pass shader";
        return;
    } else {
        m_upsamplePass.mvpMatrixLocation = m_upsamplePass.shader->uniformLocation("modelViewProjectionMatrix");
        m_upsamplePass.offsetLocation = m_upsamplePass.shader->uniformLocation("offset");
        m_upsamplePass.halfpixelLocation = m_upsamplePass.shader->uniformLocation("halfpixel");
        m_upsamplePass.textureLocation = m_upsamplePass.shader->uniformLocation("texUnit");
        m_upsamplePass.noiseLocation = m_upsamplePass.shader->uniformLocation("noise");
        m_upsamplePass.noiseTextureLocation = m_upsamplePass.shader->uniformLocation("noiseTexture");
        m_upsamplePass.noiseTextureSizeLocation = m_upsamplePass.shader->uniformLocation("noiseTextureSize");
        m_upsamplePass.topCornerRadiusLocation = m_upsamplePass.shader->uniformLocation("topCornerRadius");
        m_upsamplePass.bottomCornerRadiusLocation = m_upsamplePass.shader->uniformLocation("bottomCornerRadius");
        m_upsamplePass.antialiasingLocation = m_upsamplePass.shader->uniformLocation("antialiasing");
        m_upsamplePass.blurSizeLocation = m_upsamplePass.shader->uniformLocation("blurSize");
        m_upsamplePass.opacityLocation = m_upsamplePass.shader->uniformLocation("opacity");
        m_upsamplePass.edgeSizePixelsLocation = m_upsamplePass.shader->uniformLocation("edgeSizePixels");
        m_upsamplePass.refractionStrengthLocation = m_upsamplePass.shader->uniformLocation("refractionStrength");
        m_upsamplePass.refractionNormalPowLocation = m_upsamplePass.shader->uniformLocation("refractionNormalPow");
        m_upsamplePass.refractionRGBFringingLocation = m_upsamplePass.shader->uniformLocation("refractionRGBFringing");
        m_upsamplePass.refractionTextureRepeatModeLocation = m_upsamplePass.shader->uniformLocation("refractionTextureRepeatMode");
    }

    m_texture.shader = ShaderManager::instance()->generateShaderFromFile(ShaderTrait::MapTexture,
                                                                         QStringLiteral(":/effects/forceblur/shaders/vertex.vert"),
                                                                         QStringLiteral(":/effects/forceblur/shaders/texture.frag"));
    if (!m_texture.shader) {
        qCWarning(KWIN_BLUR) << "Failed to load texture pass shader";
        return;
    } else {
        m_texture.mvpMatrixLocation = m_texture.shader->uniformLocation("modelViewProjectionMatrix");
        m_texture.textureSizeLocation = m_texture.shader->uniformLocation("textureSize");
        m_texture.texStartPosLocation = m_texture.shader->uniformLocation("texStartPos");
        m_texture.blurSizeLocation = m_texture.shader->uniformLocation("blurSize");
        m_texture.topCornerRadiusLocation = m_texture.shader->uniformLocation("topCornerRadius");
        m_texture.bottomCornerRadiusLocation = m_texture.shader->uniformLocation("bottomCornerRadius");
        m_texture.antialiasingLocation = m_texture.shader->uniformLocation("antialiasing");
        m_texture.opacityLocation = m_texture.shader->uniformLocation("opacity");
    }

    initBlurStrengthValues();
    reconfigure(ReconfigureAll);

    if (effects->xcbConnection()) {
        net_wm_blur_region = effects->announceSupportProperty(s_blurAtomName, this);
    }

    if (effects->waylandDisplay()) {
        if (!s_blurManagerRemoveTimer) {
            s_blurManagerRemoveTimer = new QTimer(QCoreApplication::instance());
            s_blurManagerRemoveTimer->setSingleShot(true);
            s_blurManagerRemoveTimer->callOnTimeout([]() {
                s_blurManager->remove();
                s_blurManager = nullptr;
            });
        }
        s_blurManagerRemoveTimer->stop();
        if (!s_blurManager) {
            s_blurManager = new BlurManagerInterface(effects->waylandDisplay(), s_blurManagerRemoveTimer);
        }
    }

    connect(effects, &EffectsHandler::windowAdded, this, &BlurEffect::slotWindowAdded);
    connect(effects, &EffectsHandler::windowDeleted, this, &BlurEffect::slotWindowDeleted);
    connect(effects, &EffectsHandler::screenAdded, this, &BlurEffect::slotScreenAdded);
    connect(effects, &EffectsHandler::screenRemoved, this, &BlurEffect::slotScreenRemoved);
    connect(effects, &EffectsHandler::propertyNotify, this, &BlurEffect::slotPropertyNotify);
    connect(effects, &EffectsHandler::xcbConnectionChanged, this, [this]() {
        net_wm_blur_region = effects->announceSupportProperty(s_blurAtomName, this);
    });

    // Fetch the blur regions for all windows
    const auto stackingOrder = effects->stackingOrder();
    for (EffectWindow *window : stackingOrder) {
        slotWindowAdded(window);
    }
    for (const auto &screen : effects->screens()) {
        slotScreenAdded(screen);
    }

    m_valid = true;
}

BlurEffect::~BlurEffect()
{
    // When compositing is restarted, avoid removing the manager immediately.
    if (s_blurManager) {
        s_blurManagerRemoveTimer->start(1000);
    }
}

void BlurEffect::initBlurStrengthValues()
{
    // This function creates an array of blur strength values that are evenly distributed

    // The range of the slider on the blur settings UI
    int numOfBlurSteps = 15;
    int remainingSteps = numOfBlurSteps;

    /*
     * Explanation for these numbers:
     *
     * The texture blur amount depends on the downsampling iterations and the offset value.
     * By changing the offset we can alter the blur amount without relying on further downsampling.
     * But there is a minimum and maximum value of offset per downsample iteration before we
     * get artifacts.
     *
     * The minOffset variable is the minimum offset value for an iteration before we
     * get blocky artifacts because of the downsampling.
     *
     * The maxOffset value is the maximum offset value for an iteration before we
     * get diagonal line artifacts because of the nature of the dual kawase blur algorithm.
     *
     * The expandSize value is the minimum value for an iteration before we reach the end
     * of a texture in the shader and sample outside of the area that was copied into the
     * texture from the screen.
     */

    // {minOffset, maxOffset, expandSize}
    blurOffsets.append({1.0, 2.0, 10}); // Down sample size / 2
    blurOffsets.append({2.0, 3.0, 20}); // Down sample size / 4
    blurOffsets.append({2.0, 5.0, 50}); // Down sample size / 8
    blurOffsets.append({3.0, 8.0, 150}); // Down sample size / 16
    // blurOffsets.append({5.0, 10.0, 400}); // Down sample size / 32
    // blurOffsets.append({7.0, ?.0});       // Down sample size / 64

    float offsetSum = 0;

    for (int i = 0; i < blurOffsets.size(); i++) {
        offsetSum += blurOffsets[i].maxOffset - blurOffsets[i].minOffset;
    }

    for (int i = 0; i < blurOffsets.size(); i++) {
        int iterationNumber = std::ceil((blurOffsets[i].maxOffset - blurOffsets[i].minOffset) / offsetSum * numOfBlurSteps);
        remainingSteps -= iterationNumber;

        if (remainingSteps < 0) {
            iterationNumber += remainingSteps;
        }

        float offsetDifference = blurOffsets[i].maxOffset - blurOffsets[i].minOffset;

        for (int j = 1; j <= iterationNumber; j++) {
            // {iteration, offset}
            blurStrengthValues.append({i + 1, blurOffsets[i].minOffset + (offsetDifference / iterationNumber) * j});
        }
    }
}

void BlurEffect::reconfigure(ReconfigureFlags flags)
{
    m_settings.read();

    m_iterationCount = blurStrengthValues[m_settings.general.blurStrength].iteration;
    m_offset = blurStrengthValues[m_settings.general.blurStrength].offset;
    m_expandSize = blurOffsets[m_iterationCount - 1].expandSize;
    m_staticBlurTextures.clear();
    m_colorMatrix = colorMatrix(m_settings.general.brightness, m_settings.general.saturation, m_settings.general.contrast);

    for (EffectWindow *w : effects->stackingOrder()) {
        updateBlurRegion(w);
    }

    // Update all windows for the blur to take effect
    effects->addRepaintFull();
}

void BlurEffect::updateBlurRegion(EffectWindow *w, bool geometryChanged)
{
    std::optional<QRegion> content;
    std::optional<QRegion> frame;

    if (net_wm_blur_region != XCB_ATOM_NONE) {
        const QByteArray value = w->readProperty(net_wm_blur_region, XCB_ATOM_CARDINAL, 32);
        QRegion region;
        if (value.size() > 0 && !(value.size() % (4 * sizeof(uint32_t)))) {
            const uint32_t *cardinals = reinterpret_cast<const uint32_t *>(value.constData());
            for (unsigned int i = 0; i < value.size() / sizeof(uint32_t);) {
                int x = cardinals[i++];
                int y = cardinals[i++];
                int w = cardinals[i++];
                int h = cardinals[i++];
                region += Xcb::fromXNative(QRect(x, y, w, h)).toRect();
            }
        }
        if (!value.isNull()) {
            content = region;
        }
    }

    SurfaceInterface *surf = w->surface();

    if (surf && surf->blur()) {
        content = surf->blur()->region();
    }

    if (auto internal = w->internalWindow()) {
        const auto property = internal->property("kwin_blur");
        if (property.isValid()) {
            content = property.value<QRegion>();
        }
    }

    if (w->decorationHasAlpha() && decorationSupportsBlurBehind(w)) {
        frame = decorationBlurRegion(w);
    }

    // Don't override blur region for menus that already have one. The window geometry could include shadows.
    if (shouldForceBlur(w) && !((isMenu(w) || w->isTooltip()) && (content.has_value() || geometryChanged))) {
        // On X11, EffectWindow::contentsRect() includes GTK's client-side shadows, while on Wayland, it doesn't.
        // The content region is translated by EffectWindow::contentsRect() in BlurEffect::blurRegion, causing the
        // blur region to be off on X11. The frame region is not translated, so it is used instead.
        const auto isX11WithCSD = w->isX11Client() && w->frameGeometry() != w->bufferGeometry();
        if (!isX11WithCSD) {
            content = w->contentsRect().translated(-w->contentsRect().topLeft()).toRect();
        }
        if (isX11WithCSD || (m_settings.forceBlur.blurDecorations && w->decoration())) {
            frame = w->frameGeometry().translated(-w->x(), -w->y()).toRect();
        }
    }

    if (content.has_value() || frame.has_value()) {
        BlurEffectData &data = m_windows[w];
        data.content = content;
        data.frame = frame;
        data.windowEffect = ItemEffect(w->windowItem());
    } else if (!geometryChanged) { // Blur may disappear if this method is called when window geometry changes
        if (auto it = m_windows.find(w); it != m_windows.end()) {
            effects->makeOpenGLContextCurrent();
            m_windows.erase(it);
        }
    }
}

bool BlurEffect::hasStaticBlur(EffectWindow *w)
{
    if (!m_settings.staticBlur.enable) {
        return false;
    }

    if (m_settings.staticBlur.disableWhenWindowBehind) {
        if (auto it = m_windows.find(w); it != m_windows.end()) {
            return !it->second.hasWindowBehind;
        }
    }

    return true;
}

void BlurEffect::slotWindowAdded(EffectWindow *w)
{
    SurfaceInterface *surf = w->surface();

    if (surf) {
        windowBlurChangedConnections[w] = connect(surf, &SurfaceInterface::blurChanged, this, [this, w]() {
            if (w) {
                updateBlurRegion(w);
            }
        });
    }

    windowFrameGeometryChangedConnections[w] = connect(w, &EffectWindow::windowFrameGeometryChanged, this, [this,w]() {
        if (!w) {
            return;
        }

        if (w->isDesktop() && !effects->waylandDisplay()) {
            m_staticBlurTextures.erase(nullptr);
            return;
        }

        updateBlurRegion(w, true);
    });

    if (auto internal = w->internalWindow()) {
        internal->installEventFilter(this);
    }

    connect(w, &EffectWindow::windowDecorationChanged, this, &BlurEffect::setupDecorationConnections);
    setupDecorationConnections(w);

    updateBlurRegion(w);

    m_allWindows.push_back(w);
}

void BlurEffect::slotWindowDeleted(EffectWindow *w)
{
    if (auto it = m_windows.find(w); it != m_windows.end()) {
        effects->makeOpenGLContextCurrent();
        m_windows.erase(it);
    }
    if (auto it = windowBlurChangedConnections.find(w); it != windowBlurChangedConnections.end()) {
        disconnect(*it);
        windowBlurChangedConnections.erase(it);
    }
    if (auto it = windowFrameGeometryChangedConnections.find(w); it != windowFrameGeometryChangedConnections.end()) {
        disconnect(*it);
        windowFrameGeometryChangedConnections.erase(it);
    }
    if (auto it = std::find(m_allWindows.begin(), m_allWindows.end(), w); it != m_allWindows.end()) {
        m_allWindows.erase(it);
    }

    if (m_blurWhenTransformed.contains(w)) {
        m_blurWhenTransformed.removeOne(w);
    }
}

void BlurEffect::slotScreenAdded(KWin::Output *screen)
{
    screenChangedConnections[screen] = connect(screen, &Output::changed, this, [this, screen]() {
        if (!m_settings.staticBlur.enable) {
            return;
        }

        m_staticBlurTextures.erase(screen);
        effects->addRepaintFull();
    });
}

void BlurEffect::slotScreenRemoved(KWin::Output *screen)
{
    for (auto &[window, data] : m_windows) {
        if (auto it = data.render.find(screen); it != data.render.end()) {
            effects->makeOpenGLContextCurrent();
            data.render.erase(it);
        }
    }

    if (auto it = screenChangedConnections.find(screen); it != screenChangedConnections.end()) {
        disconnect(*it);
        screenChangedConnections.erase(it);
    }
}

void BlurEffect::slotPropertyNotify(EffectWindow *w, long atom)
{
    if (w && atom == net_wm_blur_region && net_wm_blur_region != XCB_ATOM_NONE) {
        updateBlurRegion(w);
    }
}

void BlurEffect::setupDecorationConnections(EffectWindow *w)
{
    if (!w->decoration()) {
        return;
    }

    connect(w->decoration(), &KDecoration3::Decoration::blurRegionChanged, this, [this, w]() {
        updateBlurRegion(w);
    });
}

bool BlurEffect::eventFilter(QObject *watched, QEvent *event)
{
    auto internal = qobject_cast<QWindow *>(watched);
    if (internal && event->type() == QEvent::DynamicPropertyChange) {
        QDynamicPropertyChangeEvent *pe = static_cast<QDynamicPropertyChangeEvent *>(event);
        if (pe->propertyName() == "kwin_blur") {
            if (auto w = effects->findWindow(internal)) {
                updateBlurRegion(w);
            }
        }
    }
    return false;
}

bool BlurEffect::enabledByDefault()
{
    return false;
}

bool BlurEffect::supported()
{
    return effects->openglContext() && (effects->openglContext()->supportsBlits() || effects->waylandDisplay());
}

bool BlurEffect::decorationSupportsBlurBehind(const EffectWindow *w) const
{
    return w->decoration() && !w->decoration()->blurRegion().isNull();
}

QRegion BlurEffect::decorationBlurRegion(const EffectWindow *w) const
{
    if (!decorationSupportsBlurBehind(w)) {
        return QRegion();
    }

    QRect decorationRect = w->decoration()->rect().toAlignedRect();
    QRegion decorationRegion = QRegion(decorationRect) - w->contentsRect().toRect();
    //! we return only blurred regions that belong to decoration region
    return decorationRegion.intersected(w->decoration()->blurRegion());
}

QRegion BlurEffect::blurRegion(EffectWindow *w) const
{
    QRegion region;

    if (auto it = m_windows.find(w); it != m_windows.end()) {
        const std::optional<QRegion> &content = it->second.content;
        const std::optional<QRegion> &frame = it->second.frame;
        if (content.has_value()) {
            if (content->isEmpty()) {
                // An empty region means that the blur effect should be enabled
                // for the whole window.
                region = w->rect().toRect();
            } else {
                if (frame.has_value()) {
                    region = frame.value();
                }
                region += content->translated(w->contentsRect().topLeft().toPoint()) & w->contentsRect().toRect();
            }
        } else if (frame.has_value()) {
            region = frame.value();
        }
    }

    return region;
}

void BlurEffect::prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime)
{
    m_paintedArea = QRegion();
    m_currentBlur = QRegion();
    m_currentScreen = effects->waylandDisplay() ? data.screen : nullptr;

    effects->prePaintScreen(data, presentTime);
}

void BlurEffect::prePaintWindow(EffectWindow *w, WindowPrePaintData &data, std::chrono::milliseconds presentTime)
{
    // this effect relies on prePaintWindow being called in the bottom to top order

    // in case this window has regions to be blurred
    const QRegion blurArea = blurRegion(w).translated(w->pos().toPoint());

    bool staticBlur = hasStaticBlur(w) && m_staticBlurTextures.contains(m_currentScreen) && !blurArea.isEmpty();
    if (staticBlur) {
        if (!m_settings.general.windowOpacityAffectsBlur) {
            data.opaque += blurArea;
        }

        int topCornerRadius;
        int bottomCornerRadius;
        if (isMenu(w)) {
            topCornerRadius = bottomCornerRadius = std::ceil(m_settings.roundedCorners.menuRadius);
        } else if (w->isDock()) {
            topCornerRadius = bottomCornerRadius = std::ceil(m_settings.roundedCorners.dockRadius);
        } else {
            topCornerRadius = std::ceil(m_settings.roundedCorners.windowTopRadius);
            bottomCornerRadius = std::ceil(m_settings.roundedCorners.windowBottomRadius);
        }

        if (!w->isDock() || (w->isDock() && isDockFloating(w, blurArea))) {
            const QRect blurRect = blurArea.boundingRect();
            data.opaque -= QRect(blurRect.x(), blurRect.y(), topCornerRadius, topCornerRadius);
            data.opaque -= QRect(blurRect.x() + blurRect.width() - topCornerRadius, blurRect.y(), topCornerRadius, topCornerRadius);
            data.opaque -= QRect(blurRect.x(), blurRect.y() + blurRect.height() - bottomCornerRadius, bottomCornerRadius, bottomCornerRadius);
            data.opaque -= QRect(blurRect.x() + blurRect.width() - bottomCornerRadius, blurRect.y() + blurRect.height() - bottomCornerRadius, bottomCornerRadius, bottomCornerRadius);
            data.mask |= Effect::PAINT_WINDOW_TRANSLUCENT;
        }
    }

    if (m_settings.staticBlur.enable) {
        if (m_settings.staticBlur.disableWhenWindowBehind) {
            if (auto it = m_windows.find(w); it != m_windows.end()) {
                const bool hadWindowBehind = it->second.hasWindowBehind;
                it->second.hasWindowBehind = false;
                for (EffectWindow *other : m_allWindows) {
                    if (w->window()->stackingOrder() <= other->window()->stackingOrder()
                        || other->isDesktop()
                        || !other->isOnCurrentDesktop()
                        || !other->isOnCurrentActivity()
                        || other->window()->resourceClass() == "xwaylandvideobridge"
                        || other->isMinimized()) {
                        continue;
                    }

                    if (w->frameGeometry().intersects(other->frameGeometry())) {
                        it->second.hasWindowBehind = true;
                        break;
                    }
                }

                if (hadWindowBehind != it->second.hasWindowBehind) {
                    data.paint += blurArea;
                    data.opaque -= blurArea;
                }
            }
        }

        if (m_settings.staticBlur.imageSource == StaticBlurImageSource::DesktopWallpaper && w->isDesktop() && w->frameGeometry() == data.paint.boundingRect()) {
            m_staticBlurTextures.erase(m_currentScreen);
        }
    }

    effects->prePaintWindow(w, data, presentTime);

    if (!staticBlur) {
        const QRegion oldOpaque = data.opaque;
        if (data.opaque.intersects(m_currentBlur)) {
            // to blur an area partially we have to shrink the opaque area of a window
            QRegion newOpaque;
            for (const QRect &rect : data.opaque) {
                newOpaque += rect.adjusted(m_expandSize, m_expandSize, -m_expandSize, -m_expandSize);
            }
            data.opaque = newOpaque;

            // we don't have to blur a region we don't see
            m_currentBlur -= newOpaque;
        }

        // if we have to paint a non-opaque part of this window that hasWindowBehind with the
        // currently blurred region we have to redraw the whole region
        if ((data.paint - oldOpaque).intersects(m_currentBlur)) {
            data.paint += m_currentBlur;
        }

        // if this window or a window underneath the blurred area is painted again we have to
        // blur everything
        if (m_paintedArea.intersects(blurArea) || data.paint.intersects(blurArea)) {
            data.paint += blurArea;
            // we have to check again whether we do not damage a blurred area
            // of a window
            if (blurArea.intersects(m_currentBlur)) {
                data.paint += m_currentBlur;
            }
        }

        m_currentBlur += blurArea;
        if (!blurArea.isEmpty()) {
            data.mask |= Effect::PAINT_WINDOW_TRANSLUCENT;
        }
    }

    m_paintedArea -= data.opaque;
    m_paintedArea += data.paint;
}

bool BlurEffect::shouldBlur(const EffectWindow *w, int mask, const WindowPaintData &data)
{
    const bool hasForceBlurRole = w->data(WindowForceBlurRole).toBool();
    if ((effects->activeFullScreenEffect() && !hasForceBlurRole) || w->isDesktop()) {
        return false;
    }

    bool scaled = !qFuzzyCompare(data.xScale(), 1.0) && !qFuzzyCompare(data.yScale(), 1.0);
    bool translated = data.xTranslation() || data.yTranslation();
    if (!(scaled || (translated || (mask & PAINT_WINDOW_TRANSFORMED)))) {
        if (m_blurWhenTransformed.contains(w)) {
            m_blurWhenTransformed.removeOne(w);
        }

        return true;
    }

    // The force blur role may be removed while the window is still transformed, causing the blur to disappear for
    // a short time. To avoid that, we allow the window to be blurred until it's not transformed anymore.
    if (m_blurWhenTransformed.contains(w)) {
        return true;
    } else if (hasForceBlurRole) {
        m_blurWhenTransformed.append(w);
    }

    return hasForceBlurRole;
}

bool BlurEffect::shouldForceBlur(const EffectWindow *w) const
{
    const auto windowClass = w->window()->resourceClass();
    const auto layer = w->window()->layer();
    if (w->isDesktop()
        || (!m_settings.forceBlur.blurDocks && w->isDock())
        || (!m_settings.forceBlur.blurMenus && isMenu(w))
        || windowClass == "xwaylandvideobridge"
        || ((windowClass == "spectacle" || windowClass == "org.kde.spectacle")
            && (layer == Layer::OverlayLayer || layer == Layer::ActiveLayer))) {
        return false;
    }

    bool matches = m_settings.forceBlur.windowClasses.contains(w->window()->resourceName())
        || m_settings.forceBlur.windowClasses.contains(w->window()->resourceClass());
    return (matches && m_settings.forceBlur.windowClassMatchingMode == WindowClassMatchingMode::Whitelist)
        || (!matches && m_settings.forceBlur.windowClassMatchingMode == WindowClassMatchingMode::Blacklist);
}

void BlurEffect::drawWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const QRegion &region, WindowPaintData &data)
{
    auto it = m_windows.find(w);
    if (it != m_windows.end()) {
        BlurEffectData &blurInfo = it->second;
        BlurRenderData &renderInfo = blurInfo.render[m_currentScreen];
        if (shouldBlur(w, mask, data)) {
            blur(renderInfo, renderTarget, viewport, w, mask, region, data);
        }
    }

    // Draw the window over the blurred area
    effects->drawWindow(renderTarget, viewport, w, mask, region, data);
}

GLTexture *BlurEffect::ensureStaticBlurTexture(const Output *output, const RenderTarget &renderTarget)
{
    if (m_staticBlurTextures.contains(output)) {
        return m_staticBlurTextures[output].get();
    }

    if (effects->waylandDisplay() && !output) {
        return nullptr;
    }

    GLenum textureFormat = GL_RGBA8;
    if (renderTarget.texture()) {
        textureFormat = renderTarget.texture()->internalFormat();
    }
    GLTexture *texture = effects->waylandDisplay()
        ? createStaticBlurTextureWayland(output, renderTarget, textureFormat)
        : createStaticBlurTextureX11(textureFormat);
    if (!texture) {
        return nullptr;
    }

    return (m_staticBlurTextures[output] = std::unique_ptr<GLTexture>(texture)).get();
}

GLTexture *BlurEffect::ensureNoiseTexture()
{
    if (m_settings.general.noiseStrength == 0) {
        return nullptr;
    }

    const qreal scale = std::max(1.0, QGuiApplication::primaryScreen()->logicalDotsPerInch() / 96.0);
    if (!noiseTexture || noiseTextureScale != scale || noiseTextureStength != m_settings.general.noiseStrength) {
        // Init randomness based on time
        std::srand((uint)QTime::currentTime().msec());

        QImage noiseImage(QSize(256, 256), QImage::Format_Grayscale8);

        for (int y = 0; y < noiseImage.height(); y++) {
            uint8_t *noiseImageLine = (uint8_t *)noiseImage.scanLine(y);

            for (int x = 0; x < noiseImage.width(); x++) {
                noiseImageLine[x] = std::rand() % m_settings.general.noiseStrength;
            }
        }

        noiseImage = noiseImage.scaled(noiseImage.size() * scale);

        noiseTexture = GLTexture::upload(noiseImage);
        if (!noiseTexture) {
            return nullptr;
        }
        noiseTexture->setFilter(GL_NEAREST);
        noiseTexture->setWrapMode(GL_REPEAT);
        noiseTextureScale = scale;
        noiseTextureStength = m_settings.general.noiseStrength;
    }

    return noiseTexture.get();
}

void BlurEffect::blur(BlurRenderData &renderInfo, const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const QRegion &region, WindowPaintData &data)
{
    // Compute the effective blur shape. Note that if the window is transformed, so will be the blur shape.
    QRegion blurShape = w ? blurRegion(w).translated(w->pos().toPoint()) : region;
    if (data.xScale() != 1 || data.yScale() != 1) {
        QPoint pt = blurShape.boundingRect().topLeft();
        QRegion scaledShape;
        for (const QRect &r : blurShape) {
            const QPointF topLeft(pt.x() + (r.x() - pt.x()) * data.xScale() + data.xTranslation(),
                                  pt.y() + (r.y() - pt.y()) * data.yScale() + data.yTranslation());
            const QPoint bottomRight(std::floor(topLeft.x() + r.width() * data.xScale()) - 1,
                                     std::floor(topLeft.y() + r.height() * data.yScale()) - 1);
            scaledShape += QRect(QPoint(std::floor(topLeft.x()), std::floor(topLeft.y())), bottomRight);
        }
        blurShape = scaledShape;
    } else if (data.xTranslation() || data.yTranslation()) {
        blurShape.translate(std::round(data.xTranslation()), std::round(data.yTranslation()));
    }

    const QRect backgroundRect = blurShape.boundingRect();
    const QRect deviceBackgroundRect = snapToPixelGrid(scaledRect(backgroundRect, viewport.scale()));
    const auto opacity = w && m_settings.general.windowOpacityAffectsBlur
        ? w->opacity() * data.opacity()
        : data.opacity();

    QList<QRectF> effectiveShape;
    effectiveShape.reserve(blurShape.rectCount());
    if (region != infiniteRegion()) {
        for (const QRect &clipRect : region) {
            const QRectF deviceClipRect = snapToPixelGridF(scaledRect(clipRect, viewport.scale()))
                    .translated(-deviceBackgroundRect.topLeft());
            for (const QRect &shapeRect : blurShape) {
                const QRectF deviceShapeRect = snapToPixelGridF(scaledRect(shapeRect.translated(-backgroundRect.topLeft()), viewport.scale()));
                if (const QRectF intersected = deviceClipRect.intersected(deviceShapeRect); !intersected.isEmpty()) {
                    effectiveShape.append(intersected);
                }
            }
        }
    } else {
        for (const QRect &rect : blurShape) {
            effectiveShape.append(snapToPixelGridF(scaledRect(rect.translated(-backgroundRect.topLeft()), viewport.scale())));
        }
    }
    if (effectiveShape.isEmpty()) {
        return;
    }

    float topCornerRadius = 0;
    float bottomCornerRadius = 0;
    if (w && !(w->isDock() && !isDockFloating(w, blurShape))) {
        const bool isMaximized = effects->clientArea(MaximizeArea, effects->activeScreen(), effects->currentDesktop()) == w->frameGeometry();
        if (isMenu(w)) {
            topCornerRadius = bottomCornerRadius = m_settings.roundedCorners.menuRadius;
        } else if (w->isDock()) {
            topCornerRadius = bottomCornerRadius = m_settings.roundedCorners.dockRadius;
        } else if ((!w->isFullScreen() && !isMaximized) || m_settings.roundedCorners.roundMaximized) {
            if (!w->decoration() || (w->decoration() && m_settings.forceBlur.blurDecorations)) {
                topCornerRadius = m_settings.roundedCorners.windowTopRadius;
            }
            bottomCornerRadius = m_settings.roundedCorners.windowBottomRadius;
        }
        topCornerRadius = topCornerRadius * viewport.scale();
        bottomCornerRadius = bottomCornerRadius * viewport.scale();
    }

    // Maybe reallocate offscreen render targets. Keep in mind that the first one contains
    // original background behind the window, it's not blurred.
    GLenum textureFormat = GL_RGBA8;
    if (renderTarget.texture()) {
        textureFormat = renderTarget.texture()->internalFormat();
    }

    // Since the VBO is shared, the texture needs to be blurred before the geometry is uploaded, otherwise it will be
    // reset.
    GLTexture *staticBlurTexture = nullptr;
    if (w && hasStaticBlur(w)) {
        staticBlurTexture = ensureStaticBlurTexture(m_currentScreen, renderTarget);
        if (staticBlurTexture) {
            renderInfo.textures.clear();
            renderInfo.framebuffers.clear();
        }
    }

    if (!staticBlurTexture
        && (renderInfo.framebuffers.size() != (m_iterationCount + 1)
            || renderInfo.textures[0]->size() != deviceBackgroundRect.size()
            || renderInfo.textures[0]->internalFormat() != textureFormat)) {
        renderInfo.framebuffers.clear();
        renderInfo.textures.clear();
        glClearColor(0, 0, 0, 0);

        for (size_t i = 0; i <= m_iterationCount; ++i) {
            // For very small windows, the width and/or height of the last blur texture may be 0. Creation of
            // and/or usage of invalid textures to create framebuffers appears to cause performance issues.
            // https://github.com/taj-ny/kwin-effects-forceblur/issues/160
            const QSize textureSize(std::max(1, deviceBackgroundRect.width() / (1 << i)), std::max(1, deviceBackgroundRect.height() / (1 << i)));
            auto texture = GLTexture::allocate(textureFormat, textureSize);
            if (!texture) {
                qCWarning(KWIN_BLUR) << "Failed to allocate an offscreen texture";
                return;
            }
            texture->setFilter(GL_LINEAR);
            texture->setWrapMode(GL_CLAMP_TO_EDGE);

            auto framebuffer = std::make_unique<GLFramebuffer>(texture.get());
            if (!framebuffer->valid()) {
                qCWarning(KWIN_BLUR) << "Failed to create an offscreen framebuffer";
                return;
            }
            auto *context =
#ifdef BETTERBLUR_X11
                OpenGlContext
#else
                EglContext
#endif
                ::currentContext();
            context->pushFramebuffer(framebuffer.get());
            glClear(GL_COLOR_BUFFER_BIT);
            context->popFramebuffer();

            renderInfo.textures.push_back(std::move(texture));
            renderInfo.framebuffers.push_back(std::move(framebuffer));
        }
    }

    // Fetch the pixels behind the shape that is going to be blurred.
    if (!staticBlurTexture) {
        const QRegion dirtyRegion = region & backgroundRect;
        for (const QRect &dirtyRect : dirtyRegion) {
            const auto destination = snapToPixelGrid(scaledRect(dirtyRect, viewport.scale())).translated(-deviceBackgroundRect.topLeft());
            renderInfo.framebuffers[0]->blitFromRenderTarget(renderTarget, viewport, dirtyRect, destination);
        }
    }

    // Upload the geometry: the first 6 vertices are used when downsampling and upsampling offscreen,
    // the remaining vertices are used when rendering on the screen.
    GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setAttribLayout(std::span(GLVertexBuffer::GLVertex2DLayout), sizeof(GLVertex2D));

    const int vertexCount = effectiveShape.size() * 6;
    if (auto result = vbo->map<GLVertex2D>(6 + vertexCount)) {
        auto map = *result;

        size_t vboIndex = 0;

        // The geometry that will be blurred offscreen, in logical pixels.
        {
            const QRectF localRect = QRectF(0, 0, deviceBackgroundRect.width(), deviceBackgroundRect.height());

            const float x0 = localRect.left();
            const float y0 = localRect.top();
            const float x1 = localRect.right();
            const float y1 = localRect.bottom();

            const float u0 = x0 / deviceBackgroundRect.width();
            const float v0 = 1.0f - y0 / deviceBackgroundRect.height();
            const float u1 = x1 / deviceBackgroundRect.width();
            const float v1 = 1.0f - y1 / deviceBackgroundRect.height();

            // first triangle
            map[vboIndex++] = GLVertex2D{
                .position = QVector2D(x0, y0),
                .texcoord = QVector2D(u0, v0),
            };
            map[vboIndex++] = GLVertex2D{
                .position = QVector2D(x1, y1),
                .texcoord = QVector2D(u1, v1),
            };
            map[vboIndex++] = GLVertex2D{
                .position = QVector2D(x0, y1),
                .texcoord = QVector2D(u0, v1),
            };

            // second triangle
            map[vboIndex++] = GLVertex2D{
                .position = QVector2D(x0, y0),
                .texcoord = QVector2D(u0, v0),
            };
            map[vboIndex++] = GLVertex2D{
                .position = QVector2D(x1, y0),
                .texcoord = QVector2D(u1, v0),
            };
            map[vboIndex++] = GLVertex2D{
                .position = QVector2D(x1, y1),
                .texcoord = QVector2D(u1, v1),
            };
        }

        // The geometry that will be painted on screen, in device pixels.
        for (const QRectF &rect : effectiveShape) {
            const float x0 = rect.left();
            const float y0 = rect.top();
            const float x1 = rect.right();
            const float y1 = rect.bottom();

            const float u0 = x0 / deviceBackgroundRect.width();
            const float v0 = 1.0f - y0 / deviceBackgroundRect.height();
            const float u1 = x1 / deviceBackgroundRect.width();
            const float v1 = 1.0f - y1 / deviceBackgroundRect.height();

            // first triangle
            map[vboIndex++] = GLVertex2D{
                .position = QVector2D(x0, y0),
                .texcoord = QVector2D(u0, v0),
            };
            map[vboIndex++] = GLVertex2D{
                .position = QVector2D(x1, y1),
                .texcoord = QVector2D(u1, v1),
            };
            map[vboIndex++] = GLVertex2D{
                .position = QVector2D(x0, y1),
                .texcoord = QVector2D(u0, v1),
            };

            // second triangle
            map[vboIndex++] = GLVertex2D{
                .position = QVector2D(x0, y0),
                .texcoord = QVector2D(u0, v0),
            };
            map[vboIndex++] = GLVertex2D{
                .position = QVector2D(x1, y0),
                .texcoord = QVector2D(u1, v0),
            };
            map[vboIndex++] = GLVertex2D{
                .position = QVector2D(x1, y1),
                .texcoord = QVector2D(u1, v1),
            };
        }

        vbo->unmap();
    } else {
        qCWarning(KWIN_BLUR) << "Failed to map vertex buffer";
        return;
    }

    vbo->bindArrays();

    if (staticBlurTexture) {
        ShaderManager::instance()->pushShader(m_texture.shader.get());

        QMatrix4x4 projectionMatrix;
        projectionMatrix = viewport.projectionMatrix();
        projectionMatrix.translate(deviceBackgroundRect.x(), deviceBackgroundRect.y());

        QRectF screenGeometry;
        if (m_currentScreen) {
            screenGeometry = scaledRect(m_currentScreen->geometryF(), viewport.scale());
        }

        m_texture.shader->setUniform(m_texture.mvpMatrixLocation, projectionMatrix);
        m_texture.shader->setUniform(m_texture.textureSizeLocation, QVector2D(staticBlurTexture->size().width(), staticBlurTexture->size().height()));
        m_texture.shader->setUniform(m_texture.texStartPosLocation, QVector2D(deviceBackgroundRect.x() - screenGeometry.x(), deviceBackgroundRect.y() - screenGeometry.y()));
        m_texture.shader->setUniform(m_texture.blurSizeLocation, QVector2D(deviceBackgroundRect.width(), deviceBackgroundRect.height()));
        m_texture.shader->setUniform(m_texture.topCornerRadiusLocation, topCornerRadius);
        m_texture.shader->setUniform(m_texture.bottomCornerRadiusLocation, bottomCornerRadius);
        m_texture.shader->setUniform(m_texture.antialiasingLocation, m_settings.roundedCorners.antialiasing);
        m_texture.shader->setUniform(m_texture.opacityLocation, static_cast<float>(opacity));

        staticBlurTexture->bind();
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        vbo->draw(GL_TRIANGLES, 6, vertexCount);

        glDisable(GL_BLEND);
        ShaderManager::instance()->popShader();
    }
    else {
        // The downsample pass of the dual Kawase algorithm: the background will be scaled down 50% every iteration.
        {
            ShaderManager::instance()->pushShader(m_downsamplePass.shader.get());

            QMatrix4x4 projectionMatrix;
            projectionMatrix.ortho(QRectF(0.0, 0.0, deviceBackgroundRect.width(), deviceBackgroundRect.height()));

            m_downsamplePass.shader->setUniform(m_downsamplePass.mvpMatrixLocation, projectionMatrix);
            m_downsamplePass.shader->setUniform(m_downsamplePass.offsetLocation, float(m_offset));
            m_downsamplePass.shader->setUniform(m_downsamplePass.colorMatrixLocation, m_colorMatrix);
            m_downsamplePass.shader->setUniform(m_downsamplePass.transformColorsLocation, true);

            for (size_t i = 1; i < renderInfo.framebuffers.size(); ++i) {
                const auto &read = renderInfo.framebuffers[i - 1];
                const auto &draw = renderInfo.framebuffers[i];

                const QVector2D halfpixel(0.5 / read->colorAttachment()->width(),
                                          0.5 / read->colorAttachment()->height());
                m_downsamplePass.shader->setUniform(m_downsamplePass.halfpixelLocation, halfpixel);

                read->colorAttachment()->bind();

                GLFramebuffer::pushFramebuffer(draw.get());
                vbo->draw(GL_TRIANGLES, 0, 6);

                if (i == 1) {
                    m_downsamplePass.shader->setUniform(m_downsamplePass.transformColorsLocation, false);
                }
            }

            ShaderManager::instance()->popShader();
        }

        // The upsample pass of the dual Kawase algorithm: the background will be scaled up 200% every iteration.
        ShaderManager::instance()->pushShader(m_upsamplePass.shader.get());

        QMatrix4x4 projectionMatrix;
        projectionMatrix.ortho(QRectF(0.0, 0.0, deviceBackgroundRect.width(), deviceBackgroundRect.height()));

        m_upsamplePass.shader->setUniform(m_upsamplePass.topCornerRadiusLocation, static_cast<float>(0));
        m_upsamplePass.shader->setUniform(m_upsamplePass.bottomCornerRadiusLocation, static_cast<float>(0));
        m_upsamplePass.shader->setUniform(m_upsamplePass.mvpMatrixLocation, projectionMatrix);
        m_upsamplePass.shader->setUniform(m_upsamplePass.noiseLocation, false);
        m_upsamplePass.shader->setUniform(m_upsamplePass.offsetLocation, float(m_offset));

        // apply refraction ONLY on the last pass, otherwise this ends in weird stacking
        m_upsamplePass.shader->setUniform(m_upsamplePass.refractionStrengthLocation, static_cast<float>(0));

        for (size_t i = renderInfo.framebuffers.size() - 1; i > 1; --i) {
            GLFramebuffer::popFramebuffer();
            const auto &read = renderInfo.framebuffers[i];

            const QVector2D halfpixel(0.5 / read->colorAttachment()->width(),
                                      0.5 / read->colorAttachment()->height());
            m_upsamplePass.shader->setUniform(m_upsamplePass.halfpixelLocation, halfpixel);

            read->colorAttachment()->bind();

            vbo->draw(GL_TRIANGLES, 0, 6);
        }

        // The last upsampling pass is rendered on the screen, not in framebuffers[0].
        GLFramebuffer::popFramebuffer();
        const auto &read = renderInfo.framebuffers[1];

        if (m_settings.general.noiseStrength > 0) {
            if (auto *noiseTexture = ensureNoiseTexture()) {
                m_upsamplePass.shader->setUniform(m_upsamplePass.noiseLocation, true);
                m_upsamplePass.shader->setUniform(m_upsamplePass.noiseTextureSizeLocation, QVector2D(noiseTexture->width(), noiseTexture->height()));

                glUniform1i(m_upsamplePass.noiseTextureLocation, 1);
                glActiveTexture(GL_TEXTURE1);
                noiseTexture->bind();
            }
        }

        glUniform1i(m_upsamplePass.textureLocation, 0);
        glActiveTexture(GL_TEXTURE0);
        read->colorAttachment()->bind();

        m_upsamplePass.shader->setUniform(m_upsamplePass.topCornerRadiusLocation, topCornerRadius);
        m_upsamplePass.shader->setUniform(m_upsamplePass.bottomCornerRadiusLocation, bottomCornerRadius);
        m_upsamplePass.shader->setUniform(m_upsamplePass.antialiasingLocation, m_settings.roundedCorners.antialiasing);
        m_upsamplePass.shader->setUniform(m_upsamplePass.blurSizeLocation, QVector2D(deviceBackgroundRect.width(), deviceBackgroundRect.height()));
        m_upsamplePass.shader->setUniform(m_upsamplePass.opacityLocation, static_cast<float>(opacity));

        projectionMatrix = viewport.projectionMatrix();
        projectionMatrix.translate(deviceBackgroundRect.x(), deviceBackgroundRect.y());
        m_upsamplePass.shader->setUniform(m_upsamplePass.mvpMatrixLocation, projectionMatrix);

        const QVector2D halfpixel(0.5 / read->colorAttachment()->width(),
                                  0.5 / read->colorAttachment()->height());
        m_upsamplePass.shader->setUniform(m_upsamplePass.halfpixelLocation, halfpixel);

        if (w && m_settings.refraction.refractionStrength > 0) {
            m_upsamplePass.shader->setUniform(m_upsamplePass.edgeSizePixelsLocation,
                std::min(m_settings.refraction.edgeSizePixels, (float)std::min(deviceBackgroundRect.width() / 2, deviceBackgroundRect.height() / 2)));
            m_upsamplePass.shader->setUniform(m_upsamplePass.refractionStrengthLocation, m_settings.refraction.refractionStrength);
            m_upsamplePass.shader->setUniform(m_upsamplePass.refractionNormalPowLocation, m_settings.refraction.refractionNormalPow);
            m_upsamplePass.shader->setUniform(m_upsamplePass.refractionRGBFringingLocation, m_settings.refraction.refractionRGBFringing);
            m_upsamplePass.shader->setUniform(m_upsamplePass.refractionTextureRepeatModeLocation, m_settings.refraction.refractionTextureRepeatMode);
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        vbo->draw(GL_TRIANGLES, 6, vertexCount);

        glDisable(GL_BLEND);
        ShaderManager::instance()->popShader();
    }

    vbo->unbindArrays();
}

void BlurEffect::blur(GLTexture *texture)
{
    const QRect textureRect = QRect(0, 0, texture->width(), texture->height());
    auto blurredFramebuffer = std::make_unique<GLFramebuffer>(texture);

    BlurRenderData renderData;
    const RenderTarget renderTarget(blurredFramebuffer.get());
    const RenderViewport renderViewport(textureRect, 1.0, renderTarget);
    WindowPaintData data;

    GLFramebuffer::pushFramebuffer(blurredFramebuffer.get());
    blur(renderData, renderTarget, renderViewport, nullptr, 0, textureRect, data);
    GLFramebuffer::popFramebuffer();
}

GLTexture *BlurEffect::wallpaper(EffectWindow *desktop, const qreal &scale, const GLenum &textureFormat)
{
    const auto geometry = snapToPixelGrid(scaledRect(desktop->rect(), scale));

    auto texture = GLTexture::allocate(textureFormat, geometry.size());
    texture->setFilter(GL_LINEAR);
    texture->setWrapMode(GL_CLAMP_TO_EDGE);
    if (!texture) {
        return nullptr;
    }
    std::unique_ptr<GLFramebuffer> desktopFramebuffer = std::make_unique<GLFramebuffer>(texture.get());
    if (!desktopFramebuffer->valid()) {
        return nullptr;
    }

    const RenderTarget renderTarget(desktopFramebuffer.get());
    const RenderViewport renderViewport(desktop->frameGeometry(), scale, renderTarget);
    WindowPaintData data;

    GLFramebuffer::pushFramebuffer(desktopFramebuffer.get());

    effects->drawWindow(renderTarget, renderViewport, desktop, PAINT_WINDOW_TRANSFORMED | PAINT_WINDOW_TRANSLUCENT, infiniteRegion(), data);
    GLFramebuffer::popFramebuffer();
    return texture.release();
}

GLTexture *BlurEffect::createStaticBlurTextureWayland(const Output *output, const RenderTarget &renderTarget, const GLenum &textureFormat)
{
    EffectWindow *desktop = nullptr;
    for (EffectWindow *w : effects->stackingOrder()) {
        if (w && w->isDesktop() && (w->window()->output() == output)) {
            desktop = w;
            break;
        }
    }
    if (!desktop) {
        return nullptr;
    }

    std::unique_ptr<GLTexture> texture;
    if (m_settings.staticBlur.imageSource == StaticBlurImageSource::DesktopWallpaper) {
        texture.reset(wallpaper(desktop, output->scale(), textureFormat));
    } else if (m_settings.staticBlur.imageSource == StaticBlurImageSource::Custom) {
        texture = GLTexture::upload(m_settings.staticBlur.customImage.scaled(output->pixelSize(), Qt::AspectRatioMode::IgnoreAspectRatio, Qt::TransformationMode::SmoothTransformation));
    }
    if (!texture) {
        return nullptr;
    }

    // Transform image colorspace
    auto imageTransformedColorspaceTexture = GLTexture::allocate(textureFormat, texture->size());
    if (!imageTransformedColorspaceTexture) {
        return nullptr;
    }
    auto imageTransformedColorspaceFramebuffer = std::make_unique<GLFramebuffer>(imageTransformedColorspaceTexture.get());
    if (!imageTransformedColorspaceFramebuffer->valid()) {
        return nullptr;
    }

    auto *shader = ShaderManager::instance()->pushShader(ShaderTrait::MapTexture | ShaderTrait::TransformColorspace);
    shader->setColorspaceUniforms(ColorDescription::sRGB, renderTarget.colorDescription(), RenderingIntent::RelativeColorimetricWithBPC);
    QMatrix4x4 projectionMatrix;
    projectionMatrix.scale(1, -1);
    projectionMatrix.ortho(QRect(0, 0, texture->width(), texture->height()));
    shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, projectionMatrix);
    GLFramebuffer::pushFramebuffer(imageTransformedColorspaceFramebuffer.get());

    texture->render(texture->size());
    texture = std::move(imageTransformedColorspaceTexture);

    GLFramebuffer::popFramebuffer();
    ShaderManager::instance()->popShader();

    if (m_settings.staticBlur.blurCustomImage) {
        blur(texture.get());
    }

    return texture.release();
}

GLTexture *BlurEffect::createStaticBlurTextureX11(const GLenum &textureFormat)
{
    std::vector<EffectWindow *> desktops;
    QRegion desktopGeometries;
    for (auto *w : effects->stackingOrder()) {
        if (!w || !w->isDesktop())
            continue;

        desktops.push_back(w);
        desktopGeometries += w->frameGeometry().toRect();
    }

    auto compositeTexture = GLTexture::allocate(textureFormat, desktopGeometries.boundingRect().size());
    if (!compositeTexture) {
        return nullptr;
    }
    auto compositeTextureFramebuffer = std::make_unique<GLFramebuffer>(compositeTexture.get());
    if (!compositeTextureFramebuffer->valid()) {
        return nullptr;
    }

    GLFramebuffer::pushFramebuffer(compositeTextureFramebuffer.get());
    ShaderBinder binder(ShaderTrait::MapTexture);
    for (auto *desktop : desktops) {
        const auto geometry = desktop->frameGeometry();

        std::unique_ptr<GLTexture> texture;
        if (m_settings.staticBlur.imageSource == StaticBlurImageSource::DesktopWallpaper) {
            texture.reset(wallpaper(desktop, 1, textureFormat));
        } else if (m_settings.staticBlur.imageSource == StaticBlurImageSource::Custom) {
            texture = GLTexture::upload(m_settings.staticBlur.customImage.scaled(geometry.width(), geometry.height(), Qt::AspectRatioMode::IgnoreAspectRatio, Qt::TransformationMode::SmoothTransformation));
        }
        if (!texture) {
            return nullptr;
        }

        if (m_settings.staticBlur.blurCustomImage) {
            blur(texture.get());
        }

        QMatrix4x4 projectionMatrix;
        projectionMatrix.scale(1, -1);
        projectionMatrix.ortho(QRectF(QPointF(), compositeTexture->size()));
        projectionMatrix.translate(geometry.x(), geometry.y());
        binder.shader()->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, projectionMatrix);

        texture->render(geometry.toRect(), desktop->size());
    }
    GLFramebuffer::popFramebuffer();

    return compositeTexture.release();
}

QMatrix4x4 BlurEffect::colorMatrix(const float &brightness, const float &saturation, const float &contrast) const
{
    QMatrix4x4 saturationMatrix;
    if (saturation != 1.0) {
        const qreal r = (1.0 - saturation) * .2126;
        const qreal g = (1.0 - saturation) * .7152;
        const qreal b = (1.0 - saturation) * .0722;

        saturationMatrix = QMatrix4x4(r + saturation, r, r, 0.0,
                                      g, g + saturation, g, 0.0,
                                      b, b, b + saturation, 0.0,
                                      0, 0, 0, 1.0);
    }

    QMatrix4x4 brightnessMatrix;
    if (brightness != 1.0) {
        brightnessMatrix.scale(brightness, brightness, brightness);
    }

    QMatrix4x4 contrastMatrix;
    if (contrast != 1.0) {
        const float transl = (1.0 - contrast) / 2.0;

        contrastMatrix = QMatrix4x4(contrast, 0, 0, 0.0,
                                    0, contrast, 0, 0.0,
                                    0, 0, contrast, 0.0,
                                    transl, transl, transl, 1.0);
    }

    return contrastMatrix * saturationMatrix * brightnessMatrix;
}

bool BlurEffect::isActive() const
{
    return m_valid && !effects->isScreenLocked();
}

bool BlurEffect::blocksDirectScanout() const
{
    return false;
}

} // namespace KWin

#include "moc_blur.cpp"
