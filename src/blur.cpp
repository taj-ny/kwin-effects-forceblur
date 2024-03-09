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
#include "opengl/glplatform.h"
#include "utils/xcbutils.h"
#include "wayland/blur.h"
#include "wayland/display.h"
#include "wayland/surface.h"

#include <QGuiApplication>
#include <QMatrix4x4>
#include <QScreen>
#include <QTime>
#include <QTimer>
#include <QWindow>
#include <cmath> // for ceil()
#include <cstdlib>

#include <KConfigGroup>
#include <KSharedConfig>

#include <KDecoration2/Decoration>

Q_LOGGING_CATEGORY(KWIN_BLUR, "kwin_effect_forceblur", QtWarningMsg)

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
                                                                                QStringLiteral(":/effects/blur/shaders/vertex.vert"),
                                                                                QStringLiteral(":/effects/blur/shaders/downsample.frag"));
    if (!m_downsamplePass.shader) {
        qCWarning(KWIN_BLUR) << "Failed to load downsampling pass shader";
        return;
    } else {
        m_downsamplePass.mvpMatrixLocation = m_downsamplePass.shader->uniformLocation("modelViewProjectionMatrix");
        m_downsamplePass.offsetLocation = m_downsamplePass.shader->uniformLocation("offset");
        m_downsamplePass.halfpixelLocation = m_downsamplePass.shader->uniformLocation("halfpixel");
    }

    m_upsamplePass.shader = ShaderManager::instance()->generateShaderFromFile(ShaderTrait::MapTexture,
                                                                              QStringLiteral(":/effects/blur/shaders/vertex.vert"),
                                                                              QStringLiteral(":/effects/blur/shaders/upsample.frag"));
    if (!m_upsamplePass.shader) {
        qCWarning(KWIN_BLUR) << "Failed to load upsampling pass shader";
        return;
    } else {
        m_upsamplePass.mvpMatrixLocation = m_upsamplePass.shader->uniformLocation("modelViewProjectionMatrix");
        m_upsamplePass.offsetLocation = m_upsamplePass.shader->uniformLocation("offset");
        m_upsamplePass.halfpixelLocation = m_upsamplePass.shader->uniformLocation("halfpixel");
    }

    m_noisePass.shader = ShaderManager::instance()->generateShaderFromFile(ShaderTrait::MapTexture,
                                                                           QStringLiteral(":/effects/blur/shaders/vertex.vert"),
                                                                           QStringLiteral(":/effects/blur/shaders/noise.frag"));
    if (!m_noisePass.shader) {
        qCWarning(KWIN_BLUR) << "Failed to load noise pass shader";
        return;
    } else {
        m_noisePass.mvpMatrixLocation = m_noisePass.shader->uniformLocation("modelViewProjectionMatrix");
        m_noisePass.noiseTextureSizeLocation = m_noisePass.shader->uniformLocation("noiseTextureSize");
        m_noisePass.texStartPosLocation = m_noisePass.shader->uniformLocation("texStartPos");
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
    BlurConfig::self()->read();

    int blurStrength = BlurConfig::blurStrength() - 1;
    m_iterationCount = blurStrengthValues[blurStrength].iteration;
    m_offset = blurStrengthValues[blurStrength].offset;
    m_expandSize = blurOffsets[m_iterationCount - 1].expandSize;
    m_noiseStrength = BlurConfig::noiseStrength();
    m_blurMatching = BlurConfig::blurMatching();
    m_blurNonMatching = BlurConfig::blurNonMatching();
    m_blurDecorations = BlurConfig::blurDecorations();
    m_windowClasses = BlurConfig::windowClasses().split("\n");
    m_transparentBlur = BlurConfig::transparentBlur();
    m_topCornerRadius = BlurConfig::topCornerRadius();
    m_bottomCornerRadius = BlurConfig::bottomCornerRadius();

    for (EffectWindow *w : effects->stackingOrder()) {
        updateBlurRegion(w);
    }

    // Update all windows for the blur to take effect
    effects->addRepaintFull();
}

void BlurEffect::updateBlurRegion(EffectWindow *w)
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

    if (shouldForceBlur(w)) {
        content = w->expandedGeometry().toRect().translated(-w->x(), -w->y());
        if (m_blurDecorations && w->decoration()) {
            const QMargins borders = w->decoration()->borders();
            frame = w->contentsRect().adjusted(0, 0, borders.left() + borders.right(), borders.top() + borders.bottom()).toRect();
        }
    }

    if (content.has_value() || frame.has_value()) {
        BlurEffectData &data = m_windows[w];
        data.content = content;
        data.frame = frame;
    } else {
        if (auto it = m_windows.find(w); it != m_windows.end()) {
            effects->makeOpenGLContextCurrent();
            m_windows.erase(it);
        }
    }
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
        if (w) {
            updateBlurRegion(w);
        }
    });

    if (auto internal = w->internalWindow()) {
        internal->installEventFilter(this);
    }

    connect(w, &EffectWindow::windowDecorationChanged, this, &BlurEffect::setupDecorationConnections);
    setupDecorationConnections(w);

    updateBlurRegion(w);
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
}

void BlurEffect::slotScreenRemoved(KWin::Output *screen)
{
    for (auto &[window, data] : m_windows) {
        if (auto it = data.render.find(screen); it != data.render.end()) {
            effects->makeOpenGLContextCurrent();
            data.render.erase(it);
        }
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

    connect(w->decoration(), &KDecoration2::Decoration::blurRegionChanged, this, [this, w]() {
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
    GLPlatform *gl = GLPlatform::instance();

    if (gl->isIntel() && gl->chipClass() < SandyBridge) {
        return false;
    }
    if (gl->isPanfrost() && gl->chipClass() <= MaliT8XX) {
        return false;
    }
    // The blur effect works, but is painfully slow (FPS < 5) on Mali and VideoCore
    if (gl->isLima() || gl->isVideoCore4() || gl->isVideoCore3D()) {
        return false;
    }
    if (gl->isSoftwareEmulation()) {
        return false;
    }

    return true;
}

bool BlurEffect::supported()
{
    return effects->isOpenGLCompositing() && GLFramebuffer::supported() && GLFramebuffer::blitSupported();
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

    QRegion decorationRegion = QRegion(w->decoration()->rect()) - w->decorationInnerRect().toRect();
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
                region += content->translated(w->contentsRect().topLeft().toPoint()) & w->decorationInnerRect().toRect();
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

    effects->prePaintWindow(w, data, presentTime);

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

    // if we have to paint a non-opaque part of this window that intersects with the
    // currently blurred region we have to redraw the whole region
    if ((data.paint - oldOpaque).intersects(m_currentBlur)) {
        data.paint += m_currentBlur;
    }

    // in case this window has regions to be blurred
    const QRegion blurArea = blurRegion(w).boundingRect().translated(w->pos().toPoint());

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

    m_paintedArea -= data.opaque;
    m_paintedArea += data.paint;
}

bool BlurEffect::shouldBlur(const EffectWindow *w, int mask, const WindowPaintData &data) const
{
    if (effects->activeFullScreenEffect() && !w->data(WindowForceBlurRole).toBool()) {
        return false;
    }

    if (w->isDesktop()) {
        return false;
    }

    bool scaled = !qFuzzyCompare(data.xScale(), 1.0) && !qFuzzyCompare(data.yScale(), 1.0);
    bool translated = data.xTranslation() || data.yTranslation();

    if ((scaled || (translated || (mask & PAINT_WINDOW_TRANSFORMED))) && !w->data(WindowForceBlurRole).toBool()) {
        return false;
    }

    return true;
}

bool BlurEffect::shouldForceBlur(const EffectWindow *w) const
{
    bool matches = m_windowClasses.contains(w->window()->resourceName())
        || m_windowClasses.contains(w->window()->resourceClass());
    return (matches && m_blurMatching) || (!matches && m_blurNonMatching);
}

void BlurEffect::drawWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const QRegion &region, WindowPaintData &data)
{
    blur(renderTarget, viewport, w, mask, region, data);

    // Draw the window over the blurred area
    effects->drawWindow(renderTarget, viewport, w, mask, region, data);
}

GLTexture *BlurEffect::ensureNoiseTexture()
{
    if (m_noiseStrength == 0) {
        return nullptr;
    }

    const qreal scale = std::max(1.0, QGuiApplication::primaryScreen()->logicalDotsPerInch() / 96.0);
    if (!m_noisePass.noiseTexture || m_noisePass.noiseTextureScale != scale || m_noisePass.noiseTextureStength != m_noiseStrength) {
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

        m_noisePass.noiseTexture = GLTexture::upload(noiseImage);
        if (!m_noisePass.noiseTexture) {
            return nullptr;
        }
        m_noisePass.noiseTexture->setFilter(GL_NEAREST);
        m_noisePass.noiseTexture->setWrapMode(GL_REPEAT);
        m_noisePass.noiseTextureScale = scale;
        m_noisePass.noiseTextureStength = m_noiseStrength;
    }

    return m_noisePass.noiseTexture.get();
}

void BlurEffect::blur(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const QRegion &region, WindowPaintData &data)
{
    auto it = m_windows.find(w);
    if (it == m_windows.end()) {
        return;
    }

    BlurEffectData &blurInfo = it->second;
    BlurRenderData &renderInfo = blurInfo.render[m_currentScreen];
    if (!shouldBlur(w, mask, data)) {
        return;
    }

    // Compute the effective blur shape. Note that if the window is transformed, so will be the blur shape.
    QRegion blurShape = blurRegion(w).translated(w->pos().toPoint());
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
    const auto opacity = m_transparentBlur
        ? w->opacity() * data.opacity()
        : data.opacity();

    // Get the effective shape that will be actually blurred. It's possible that all of it will be clipped.
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

    // Maybe reallocate offscreen render targets. Keep in mind that the first one contains
    // original background behind the window, it's not blurred.
    GLenum textureFormat = GL_RGBA8;
    if (renderTarget.texture()) {
        textureFormat = renderTarget.texture()->internalFormat();
    }

    if (renderInfo.framebuffers.size() != (m_iterationCount + 1) || renderInfo.textures[0]->size() != backgroundRect.size() || renderInfo.textures[0]->internalFormat() != textureFormat) {
        renderInfo.framebuffers.clear();
        renderInfo.textures.clear();

        for (size_t i = 0; i <= m_iterationCount; ++i) {
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
    const QRegion dirtyRegion = region & backgroundRect;
    for (const QRect &dirtyRect : dirtyRegion) {
        renderInfo.framebuffers[0]->blitFromRenderTarget(renderTarget, viewport, dirtyRect, dirtyRect.translated(-backgroundRect.topLeft()));
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

    // The downsample pass of the dual Kawase algorithm: the background will be scaled down 50% every iteration.
    {
        ShaderManager::instance()->pushShader(m_downsamplePass.shader.get());

        QMatrix4x4 projectionMatrix;
        projectionMatrix.ortho(QRectF(0.0, 0.0, backgroundRect.width(), backgroundRect.height()));

        m_downsamplePass.shader->setUniform(m_downsamplePass.mvpMatrixLocation, projectionMatrix);
        m_downsamplePass.shader->setUniform(m_downsamplePass.offsetLocation, float(m_offset));

        for (size_t i = 1; i < renderInfo.framebuffers.size(); ++i) {
            const auto &read = renderInfo.framebuffers[i - 1];
            const auto &draw = renderInfo.framebuffers[i];

            const QVector2D halfpixel(0.5 / read->colorAttachment()->width(),
                                      0.5 / read->colorAttachment()->height());
            m_downsamplePass.shader->setUniform(m_downsamplePass.halfpixelLocation, halfpixel);

            read->colorAttachment()->bind();

            GLFramebuffer::pushFramebuffer(draw.get());
            vbo->draw(GL_TRIANGLES, 0, 6);
        }

        ShaderManager::instance()->popShader();
    }

    // The upsample pass of the dual Kawase algorithm: the background will be scaled up 200% every iteration.
    {
        ShaderManager::instance()->pushShader(m_upsamplePass.shader.get());

        QMatrix4x4 projectionMatrix;
        projectionMatrix.ortho(QRectF(0.0, 0.0, backgroundRect.width(), backgroundRect.height()));

        m_upsamplePass.shader->setUniform(m_upsamplePass.mvpMatrixLocation, projectionMatrix);
        m_upsamplePass.shader->setUniform(m_upsamplePass.offsetLocation, float(m_offset));

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

        projectionMatrix = data.projectionMatrix();
        projectionMatrix.translate(deviceBackgroundRect.x(), deviceBackgroundRect.y());
        m_upsamplePass.shader->setUniform(m_upsamplePass.mvpMatrixLocation, projectionMatrix);

        const QVector2D halfpixel(0.5 / read->colorAttachment()->width(),
                                  0.5 / read->colorAttachment()->height());
        m_upsamplePass.shader->setUniform(m_upsamplePass.halfpixelLocation, halfpixel);

        read->colorAttachment()->bind();

        // Modulate the blurred texture with the window opacity if the window isn't opaque
        if (opacity < 1.0) {
            glEnable(GL_BLEND);
            float o = 1.0f - (opacity);
            o = 1.0f - o * o;
            glBlendColor(0, 0, 0, o);
            glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
        }

        vbo->draw(GL_TRIANGLES, 6, vertexCount);

        if (opacity < 1.0) {
            glDisable(GL_BLEND);
        }

        ShaderManager::instance()->popShader();
    }

    if (m_noiseStrength > 0) {
        // Apply an additive noise onto the blurred image. The noise is useful to mask banding
        // artifacts, which often happens due to the smooth color transitions in the blurred image.

        glEnable(GL_BLEND);
        if (opacity < 1.0) {
            glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE);
        } else {
            glBlendFunc(GL_ONE, GL_ONE);
        }

        if (GLTexture *noiseTexture = ensureNoiseTexture()) {
            ShaderManager::instance()->pushShader(m_noisePass.shader.get());

            QMatrix4x4 projectionMatrix = data.projectionMatrix();
            projectionMatrix.translate(deviceBackgroundRect.x(), deviceBackgroundRect.y());

            m_noisePass.shader->setUniform(m_noisePass.mvpMatrixLocation, projectionMatrix);
            m_noisePass.shader->setUniform(m_noisePass.noiseTextureSizeLocation, QVector2D(noiseTexture->width(), noiseTexture->height()));
            m_noisePass.shader->setUniform(m_noisePass.texStartPosLocation, QVector2D(deviceBackgroundRect.topLeft()));

            noiseTexture->bind();

            vbo->draw(GL_TRIANGLES, 6, vertexCount);

            ShaderManager::instance()->popShader();
        }

        glDisable(GL_BLEND);
    }

    vbo->unbindArrays();
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
