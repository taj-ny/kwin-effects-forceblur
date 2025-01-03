/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2011 Philipp Knechtges <philipp-dev@knechtges.com>
    SPDX-FileCopyrightText: 2018 Alex Nemeth <alex.nemeth329@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "blur.h"
// KConfigSkeleton
#include "config.h"

#include "core/pixelgrid.h"
#include "core/rendertarget.h"
#include "core/renderviewport.h"
#include "effect/effecthandler.h"
#include "internalwindow.h"
#include "opengl/glutils.h"
#include "opengl/glplatform.h"
#include "utils.h"
#include "utils/xcbutils.h"
#include "wayland/blur.h"
#include "wayland/display.h"
#include "wayland/surface.h"
#include "kwin/window.h"

#ifdef KWIN_6_2_OR_GREATER
#include "scene/decorationitem.h"
#include "scene/surfaceitem.h"
#include "scene/windowitem.h"
#endif

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

#ifdef KDECORATION3
#include <KDecoration3/Decoration>
#else
#include <KDecoration2/Decoration>
#endif

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
    m_windowRules = std::make_unique<BetterBlur::WindowRuleList>();
    BetterBlur::Config::instance(effects->config());
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
        m_texture.scaleLocation = m_texture.shader->uniformLocation("scale");
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
    Q_UNUSED(flags)

    BetterBlur::Config::self()->read();
    m_windowRules->load();
    m_colorMatrix = colorMatrix(BetterBlur::Config::brightness(), BetterBlur::Config::saturation(), BetterBlur::Config::contrast());

    m_noiseStrength = BetterBlur::Config::noiseStrength();
    m_staticBlurImage = QImage(BetterBlur::Config::fakeBlurImage());
    m_staticBlurTextures.clear();

    for (const auto &[_, w] : m_windows) {
        w->updateProperties();
        w->updateBlurRegion();
    }

    // Update all windows for the blur to take effect
    effects->addRepaintFull();
}

void BlurEffect::slotWindowAdded(EffectWindow *w)
{
    m_windows[w] = std::make_unique<BetterBlur::Window>(w, m_windowRules.get(), &net_wm_blur_region);

    windowFrameGeometryChangedConnections[w] = connect(w, &EffectWindow::windowFrameGeometryChanged, this, [this,w]() {
        if (w && w->isDesktop() && !effects->waylandDisplay()) {
            m_staticBlurTextures.clear();
        }
    });

    if (auto internal = w->internalWindow()) {
        internal->installEventFilter(this);
    }
}

void BlurEffect::slotWindowDeleted(EffectWindow *w)
{
    if (auto it = m_windows.find(w); it != m_windows.end()) {
        effects->makeOpenGLContextCurrent();
        m_windows.erase(it);
    }
    if (auto it = windowFrameGeometryChangedConnections.find(w); it != windowFrameGeometryChangedConnections.end()) {
        disconnect(*it);
        windowFrameGeometryChangedConnections.erase(it);
    }

    m_windows.erase(w);
}

void BlurEffect::slotScreenAdded(KWin::Output *screen)
{
    screenChangedConnections[screen] = connect(screen, &Output::changed, this, [this, screen]() {
        if (m_staticBlurTextures.empty()) {
            return;
        }

        m_staticBlurTextures.erase(screen);
        effects->addRepaintFull();
    });
}

void BlurEffect::slotScreenRemoved(KWin::Output *screen)
{
    for (auto &[_, w] : m_windows) {
        if (auto it = w->render.find(screen); it != w->render.end()) {
            effects->makeOpenGLContextCurrent();
            w->render.erase(it);
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
        m_windows[w]->updateBlurRegion();
    }
}

bool BlurEffect::eventFilter(QObject *watched, QEvent *event)
{
    auto internal = qobject_cast<QWindow *>(watched);
    if (internal && event->type() == QEvent::DynamicPropertyChange) {
        QDynamicPropertyChangeEvent *pe = static_cast<QDynamicPropertyChangeEvent *>(event);
        if (pe->propertyName() == "kwin_blur") {
            if (auto w = effects->findWindow(internal)) {
                m_windows[w]->updateBlurRegion();
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
#ifdef KWIN_6_1_OR_GREATER
    return effects->openglContext() && (effects->openglContext()->supportsBlits() || effects->waylandDisplay());
#else
    return effects->isOpenGLCompositing() && GLFramebuffer::supported() && GLFramebuffer::blitSupported();
#endif
}

void BlurEffect::prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime)
{
    m_paintedArea = {};
    m_currentBlur = {};
    m_windowGeometriesSum = {};
    m_currentScreen = effects->waylandDisplay() ? data.screen : nullptr;

    effects->prePaintScreen(data, presentTime);
}

void BlurEffect::prePaintWindow(EffectWindow *w, WindowPrePaintData &data, std::chrono::milliseconds presentTime)
{
    // this effect relies on prePaintWindow being called in the bottom to top order

    const auto &blurWindow = m_windows[w];

    if (!w->isDesktop()) {
        const auto hadWindowBehind = blurWindow->hasWindowBehind();
        blurWindow->setHasWindowBehind(m_windowGeometriesSum.intersects(w->frameGeometry().toRect()));
        if (hadWindowBehind != blurWindow->hasWindowBehind()) {
            data.paint += w->expandedGeometry().toRect();
        }
    }

    const QRegion blurArea = blurWindow->blurRegion().translated(w->pos().toPoint());

    bool staticBlur = blurWindow->properties()->staticBlur()
        && !blurArea.isEmpty()
        && m_staticBlurTextures.contains(m_currentScreen)
        && m_staticBlurTextures[m_currentScreen].contains(blurWindow->properties()->blurStrength());
    if (staticBlur) {
        if (!blurWindow->properties()->windowOpacityAffectsBlur()) {
            data.opaque += blurArea;
        }

        const int topCornerRadius = std::ceil(blurWindow->properties()->topCornerRadius());
        const int bottomCornerRadius = std::ceil(blurWindow->properties()->bottomCornerRadius());
        if (!w->isDock() || (w->isDock() && isDockFloating(w, blurArea))) {
            const QRect blurRect = blurArea.boundingRect();
            data.opaque -= QRect(blurRect.x(), blurRect.y(), topCornerRadius, topCornerRadius);
            data.opaque -= QRect(blurRect.x() + blurRect.width() - topCornerRadius, blurRect.y(), topCornerRadius, topCornerRadius);
            data.opaque -= QRect(blurRect.x(), blurRect.y() + blurRect.height() - bottomCornerRadius, bottomCornerRadius, bottomCornerRadius);
            data.opaque -= QRect(blurRect.x() + blurRect.width() - bottomCornerRadius, blurRect.y() + blurRect.height() - bottomCornerRadius, bottomCornerRadius, bottomCornerRadius);
        }
    }

    if (BetterBlur::Config::fakeBlurImageSourceDesktopWallpaper()
        && !m_staticBlurTextures.empty()
        && w->isDesktop()
        && w->frameGeometry() == data.paint.boundingRect()) {
        m_staticBlurTextures.erase(m_currentScreen);
    }

    if (!staticBlur && blurWindow->properties()->forceTransparency()) {
        data.setTranslucent();
    }

    effects->prePaintWindow(w, data, presentTime);

    if (!staticBlur) {
        const QRegion oldOpaque = data.opaque;
        if (data.opaque.intersects(m_currentBlur)) {
            // to blur an area partially we have to shrink the opaque area of a window
            QRegion newOpaque;
            auto iterationCount = blurStrengthValues[blurWindow->properties()->blurStrength() - 1].iteration;
            auto expandSize = blurOffsets[iterationCount - 1].expandSize;
            for (const QRect &rect : data.opaque) {
                newOpaque += rect.adjusted(expandSize, expandSize, -expandSize, -expandSize);
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
    }

    m_paintedArea -= data.opaque;
    m_paintedArea += data.paint;
    if (!w->isDesktop() && w->window()->resourceClass() != "xwaylandvideobridge") {
        m_windowGeometriesSum += w->frameGeometry().toRect();
    }
}

void BlurEffect::drawWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const QRegion &region, WindowPaintData &data)
{
    auto blurWindow = m_windows[w].get();
    if (blurWindow->shouldBlur(mask, data)) {
        blur(blurWindow, blurWindow->properties()->blurStrength() - 1, blurWindow->render[m_currentScreen], renderTarget, viewport, mask, region, data);
    }

    // Draw the window over the blurred area
    effects->drawWindow(renderTarget, viewport, w, mask, region, data);
}

GLTexture *BlurEffect::ensureStaticBlurTexture(const Output *output, const int &strength, const RenderTarget &renderTarget)
{
    if (m_staticBlurTextures.contains(output) && m_staticBlurTextures[output].contains(strength)) {
        return m_staticBlurTextures[output][strength].get();
    }

    if (effects->waylandDisplay() && !output) {
        return nullptr;
    }

    GLenum textureFormat = GL_RGBA8;
    if (renderTarget.texture()) {
        textureFormat = renderTarget.texture()->internalFormat();
    }
    GLTexture *texture = effects->waylandDisplay()
        ? createStaticBlurTextureWayland(output, strength, renderTarget, textureFormat)
        : createStaticBlurTextureX11(strength, textureFormat);
    if (!texture) {
        return nullptr;
    }

    return (m_staticBlurTextures[output][strength] = std::unique_ptr<GLTexture>(texture)).get();
}

GLTexture *BlurEffect::ensureNoiseTexture()
{
    if (m_noiseStrength == 0) {
        return nullptr;
    }

    const qreal scale = std::max(1.0, QGuiApplication::primaryScreen()->logicalDotsPerInch() / 96.0);
    if (!noiseTexture || noiseTextureScale != scale || noiseTextureStength != m_noiseStrength) {
        // Init randomness based on time
        std::srand((uint)QTime::currentTime().msec());

        QImage noiseImage(QSize(256, 256), QImage::Format_Grayscale8);

        for (int y = 0; y < noiseImage.height(); y++) {
            uint8_t *noiseImageLine = (uint8_t *)noiseImage.scanLine(y);

            for (int x = 0; x < noiseImage.width(); x++) {
                noiseImageLine[x] = std::rand() % m_noiseStrength;
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
        noiseTextureStength = m_noiseStrength;
    }

    return noiseTexture.get();
}

void BlurEffect::blur(BetterBlur::Window *w, const int &strength, BetterBlur::BlurRenderData &renderInfo, const RenderTarget &renderTarget, const RenderViewport &viewport, int mask, const QRegion &region, WindowPaintData &data)
{
    Q_UNUSED(mask)

    // Compute the effective blur shape. Note that if the window is transformed, so will be the blur shape.
    QRegion blurShape = w ? w->blurRegion().translated(w->window()->pos().toPoint()) : region;
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
    const auto opacity = w && w->properties()->windowOpacityAffectsBlur()
        ? w->window()->opacity() * data.opacity()
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
    if (w && !(w->window()->isDock() && !isDockFloating(w->window(), blurShape))) {
        if (!w->window()->decoration() || (w->window()->decoration() && w->properties()->blurDecorations())) {
            topCornerRadius = w->properties()->topCornerRadius() * viewport.scale();
        }
        bottomCornerRadius = w->properties()->bottomCornerRadius() * viewport.scale();
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
    if (w && w->properties()->staticBlur()) {
        staticBlurTexture = ensureStaticBlurTexture(m_currentScreen, strength, renderTarget);
        if (staticBlurTexture) {
            renderInfo.textures.clear();
            renderInfo.framebuffers.clear();
        }
    }

    size_t iterationCount = blurStrengthValues[strength].iteration;
    auto offset = blurStrengthValues[strength].offset;

    if (!staticBlurTexture
        && (renderInfo.framebuffers.size() != (iterationCount + 1)
            || renderInfo.textures[0]->size() != backgroundRect.size()
            || renderInfo.textures[0]->internalFormat() != textureFormat)) {
        renderInfo.framebuffers.clear();
        renderInfo.textures.clear();

        for (size_t i = 0; i <= iterationCount; ++i) {
            auto texture = GLTexture::allocate(textureFormat, backgroundRect.size() / (1 << i));
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
            renderInfo.textures.push_back(std::move(texture));
            renderInfo.framebuffers.push_back(std::move(framebuffer));
        }
    }

    // Fetch the pixels behind the shape that is going to be blurred.
    if (!staticBlurTexture) {
        const QRegion dirtyRegion = region & backgroundRect;
        for (const QRect &dirtyRect: dirtyRegion) {
            renderInfo.framebuffers[0]->blitFromRenderTarget(renderTarget, viewport, dirtyRect, dirtyRect.translated(-backgroundRect.topLeft()));
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
            const QRectF localRect = QRectF(0, 0, backgroundRect.width(), backgroundRect.height());

            const float x0 = localRect.left();
            const float y0 = localRect.top();
            const float x1 = localRect.right();
            const float y1 = localRect.bottom();

            const float u0 = x0 / backgroundRect.width();
            const float v0 = 1.0f - y0 / backgroundRect.height();
            const float u1 = x1 / backgroundRect.width();
            const float v1 = 1.0f - y1 / backgroundRect.height();

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
            screenGeometry = m_currentScreen->geometryF();
        }

        m_texture.shader->setUniform(m_texture.mvpMatrixLocation, projectionMatrix);
        m_texture.shader->setUniform(m_texture.textureSizeLocation, QVector2D(staticBlurTexture->size().width(), staticBlurTexture->size().height()));
        m_texture.shader->setUniform(m_texture.texStartPosLocation, QVector2D(backgroundRect.x() - screenGeometry.x(), backgroundRect.y() - screenGeometry.y()));
        m_texture.shader->setUniform(m_texture.blurSizeLocation, QVector2D(backgroundRect.width(), backgroundRect.height()));
        m_texture.shader->setUniform(m_texture.scaleLocation, (float)viewport.scale());
        m_texture.shader->setUniform(m_texture.topCornerRadiusLocation, topCornerRadius);
        m_texture.shader->setUniform(m_texture.bottomCornerRadiusLocation, bottomCornerRadius);
        m_texture.shader->setUniform(m_texture.antialiasingLocation, w->properties()->cornerAntialiasing());
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
            projectionMatrix.ortho(QRectF(0.0, 0.0, backgroundRect.width(), backgroundRect.height()));

            m_downsamplePass.shader->setUniform(m_downsamplePass.mvpMatrixLocation, projectionMatrix);
            m_downsamplePass.shader->setUniform(m_downsamplePass.offsetLocation, offset);
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
        projectionMatrix.ortho(QRectF(0.0, 0.0, backgroundRect.width(), backgroundRect.height()));

        m_upsamplePass.shader->setUniform(m_upsamplePass.topCornerRadiusLocation, static_cast<float>(0));
        m_upsamplePass.shader->setUniform(m_upsamplePass.bottomCornerRadiusLocation, static_cast<float>(0));
        m_upsamplePass.shader->setUniform(m_upsamplePass.mvpMatrixLocation, projectionMatrix);
        m_upsamplePass.shader->setUniform(m_upsamplePass.noiseLocation, false);
        m_upsamplePass.shader->setUniform(m_upsamplePass.offsetLocation, offset);

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

        if (m_noiseStrength > 0) {
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
        m_upsamplePass.shader->setUniform(m_upsamplePass.antialiasingLocation, static_cast<float>(w ? w->properties()->cornerAntialiasing() : 0.0));
        m_upsamplePass.shader->setUniform(m_upsamplePass.blurSizeLocation, QVector2D(backgroundRect.width(), backgroundRect.height()));
        m_upsamplePass.shader->setUniform(m_upsamplePass.opacityLocation, static_cast<float>(opacity));

        projectionMatrix = viewport.projectionMatrix();
        projectionMatrix.translate(deviceBackgroundRect.x(), deviceBackgroundRect.y());
        m_upsamplePass.shader->setUniform(m_upsamplePass.mvpMatrixLocation, projectionMatrix);

        const QVector2D halfpixel(0.5 / read->colorAttachment()->width(),
                                  0.5 / read->colorAttachment()->height());
        m_upsamplePass.shader->setUniform(m_upsamplePass.halfpixelLocation, halfpixel);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        vbo->draw(GL_TRIANGLES, 6, vertexCount);

        glDisable(GL_BLEND);
        ShaderManager::instance()->popShader();
    }

    vbo->unbindArrays();
}

void BlurEffect::blur(GLTexture *texture, const int &strength)
{
    const QRect textureRect = QRect(0, 0, texture->width(), texture->height());
    auto blurredFramebuffer = std::make_unique<GLFramebuffer>(texture);

    BetterBlur::BlurRenderData renderInfo;
    const RenderTarget renderTarget(blurredFramebuffer.get());
    const RenderViewport renderViewport(textureRect, 1.0, renderTarget);
    WindowPaintData data;

    GLFramebuffer::pushFramebuffer(blurredFramebuffer.get());
    blur(nullptr, strength, renderInfo, renderTarget, renderViewport, 0, textureRect, data);
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

#ifndef KWIN_6_1_OR_GREATER
    QMatrix4x4 projectionMatrix;
    projectionMatrix.ortho(geometry);
    data.setProjectionMatrix(projectionMatrix);
#endif

    GLFramebuffer::pushFramebuffer(desktopFramebuffer.get());

    effects->drawWindow(renderTarget, renderViewport, desktop, PAINT_WINDOW_TRANSFORMED | PAINT_WINDOW_TRANSLUCENT, infiniteRegion(), data);
    GLFramebuffer::popFramebuffer();
    return texture.release();
}

GLTexture *BlurEffect::createStaticBlurTextureWayland(const Output *output, const int &strength, const RenderTarget &renderTarget, const GLenum &textureFormat)
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
    if (BetterBlur::Config::fakeBlurImageSourceDesktopWallpaper()) {
        texture.reset(wallpaper(desktop, output->scale(), textureFormat));
    } else if (BetterBlur::Config::fakeBlurImageSourceCustom()) {
        texture = GLTexture::upload(m_staticBlurImage.scaled(output->pixelSize(), Qt::AspectRatioMode::IgnoreAspectRatio, Qt::TransformationMode::SmoothTransformation));
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
    shader->setColorspaceUniforms(
        ColorDescription::sRGB, renderTarget.colorDescription()
#ifdef KWIN_6_2_OR_GREATER
        , RenderingIntent::RelativeColorimetricWithBPC
#endif
    );
    QMatrix4x4 projectionMatrix;
    projectionMatrix.scale(1, -1);
    projectionMatrix.ortho(QRect(0, 0, texture->width(), texture->height()));
    shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, projectionMatrix);
    GLFramebuffer::pushFramebuffer(imageTransformedColorspaceFramebuffer.get());

    texture->render(texture->size());
    texture = std::move(imageTransformedColorspaceTexture);

    GLFramebuffer::popFramebuffer();
    ShaderManager::instance()->popShader();

    if (BetterBlur::Config::fakeBlurCustomImageBlur()) {
        blur(texture.get(), strength);
    }

    return texture.release();
}

GLTexture *BlurEffect::createStaticBlurTextureX11(const int &strength, const GLenum &textureFormat)
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
        if (BetterBlur::Config::fakeBlurImageSourceDesktopWallpaper()) {
            texture.reset(wallpaper(desktop, 1, textureFormat));
        } else if (BetterBlur::Config::fakeBlurImageSourceCustom()) {
            texture = GLTexture::upload(m_staticBlurImage.scaled(geometry.width(), geometry.height(), Qt::AspectRatioMode::IgnoreAspectRatio, Qt::TransformationMode::SmoothTransformation));
        }
        if (!texture) {
            return nullptr;
        }

        if (BetterBlur::Config::fakeBlurCustomImageBlur()) {
            blur(texture.get(), strength);
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
