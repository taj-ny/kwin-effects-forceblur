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

#include <KDecoration2/Decoration>

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

    m_texturePass.shader = ShaderManager::instance()->generateShaderFromFile(ShaderTrait::MapTexture,
                                                                             QStringLiteral(":/effects/blur/shaders/vertex.vert"),
                                                                             QStringLiteral(":/effects/blur/shaders/texture.frag"));
    if (!m_texturePass.shader) {
        qCWarning(KWIN_BLUR) << "Failed to load texture pass shader";
        return;
    } else {
        m_texturePass.mvpMatrixLocation = m_texturePass.shader->uniformLocation("modelViewProjectionMatrix");
        m_texturePass.textureSizeLocation = m_texturePass.shader->uniformLocation("textureSize");
        m_texturePass.texStartPosLocation = m_texturePass.shader->uniformLocation("texStartPos");
        m_texturePass.regionSizeLocation = m_texturePass.shader->uniformLocation("regionSize");
    }

    m_roundedCorners.shader = ShaderManager::instance()->generateShaderFromFile(ShaderTrait::MapTexture,
                                                                                QStringLiteral(":/effects/blur/shaders/vertex.vert"),
                                                                                QStringLiteral(":/effects/blur/shaders/roundedcorners.frag"));
    if (!m_roundedCorners.shader) {
        qCWarning(KWIN_BLUR) << "Failed to load rounded corners shader";
        return;
    } else {
        m_roundedCorners.roundTopLeftCornerLocation = m_roundedCorners.shader->uniformLocation("roundTopLeftCorner");
        m_roundedCorners.roundTopRightCornerLocation = m_roundedCorners.shader->uniformLocation("roundTopRightCorner");
        m_roundedCorners.roundBottomLeftCornerLocation = m_roundedCorners.shader->uniformLocation("roundBottomLeftCorner");
        m_roundedCorners.roundBottomRightCornerLocation = m_roundedCorners.shader->uniformLocation("roundBottomRightCorner");

        m_roundedCorners.topCornerRadiusLocation = m_roundedCorners.shader->uniformLocation("topCornerRadius");
        m_roundedCorners.bottomCornerRadiusLocation = m_roundedCorners.shader->uniformLocation("bottomCornerRadius");

        m_roundedCorners.antialiasingLocation = m_roundedCorners.shader->uniformLocation("antialiasing");

        m_roundedCorners.regionSizeLocation = m_roundedCorners.shader->uniformLocation("regionSize");

        m_roundedCorners.beforeBlurTextureLocation = m_roundedCorners.shader->uniformLocation("beforeBlurTexture");
        m_roundedCorners.afterBlurTextureLocation = m_roundedCorners.shader->uniformLocation("afterBlurTexture");

        m_roundedCorners.mvpMatrixLocation = m_roundedCorners.shader->uniformLocation("modelViewProjectionMatrix");
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
    m_windowTopCornerRadius = BlurConfig::topCornerRadius();
    m_windowBottomCornerRadius = BlurConfig::bottomCornerRadius();
    m_menuCornerRadius = BlurConfig::menuCornerRadius();
    m_dockCornerRadius = BlurConfig::dockCornerRadius();
    m_roundedCornersAntialiasing = BlurConfig::roundedCornersAntialiasing();
    m_roundCornersOfMaximizedWindows = BlurConfig::roundCornersOfMaximizedWindows();
    m_blurMenus = BlurConfig::blurMenus();
    m_blurDocks = BlurConfig::blurDocks();
    m_paintAsTranslucent = BlurConfig::paintAsTranslucent();
    m_fakeBlur = BlurConfig::fakeBlur();
    m_fakeBlurImage = BlurConfig::fakeBlurImage();

    // Antialiasing does take up a bit of space, so the corner radius will be reduced by the offset in order to leave some space.
    m_cornerRadiusOffset = m_roundedCornersAntialiasing == 0 ? 0 : std::round(m_roundedCornersAntialiasing) + 2;

    QImage fakeBlurImage(m_fakeBlurImage);
    m_hasValidFakeBlurTexture = !fakeBlurImage.isNull();
    if (m_hasValidFakeBlurTexture) {
        m_texturePass.texture = GLTexture::upload(fakeBlurImage);
    }

    m_corners.clear();

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
            frame = w->frameGeometry().toRect().translated(-w->x(), -w->y());
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

void BlurEffect::generateRoundedCornerMasks(int radius, QRegion &left, QRegion &right, bool top) const
{
    // This method uses OpenGL to draw circles, since the ones drawn by Qt are terrible.

    left = QRegion();
    right = QRegion();

    if (radius == 0) {
        return;
    }

    float size = radius * 2;
    auto cornersTexture = GLTexture::allocate(GL_RGB8, QSize(size, size));
    auto cornersFramebuffer = std::make_unique<GLFramebuffer>(cornersTexture.get());

    GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setAttribLayout(std::span(GLVertexBuffer::GLVertex2DLayout), sizeof(GLVertex2D));
    if (auto result = vbo->map<GLVertex2D>(6)) {
        auto map = *result;

        size_t vboIndex = 0;

        const float x0 = 0;
        const float y0 = 0;
        const float x1 = size;
        const float y1 = size;

        const float u0 = x0 / size;
        const float v0 = 1.0f - y0 / size;
        const float u1 = x1 / size;
        const float v1 = 1.0f - y1 / size;

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

        vbo->unmap();
    } else {
        qCWarning(KWIN_BLUR) << "Failed to map vertex buffer";
        return;
    }

    vbo->bindArrays();

    QMatrix4x4 projectionMatrix;
    projectionMatrix.ortho(QRect(0, 0, size, size));

    ShaderManager::instance()->pushShader(m_roundedCorners.shader.get());
    m_roundedCorners.shader->setUniform(m_roundedCorners.roundTopLeftCornerLocation, false);
    m_roundedCorners.shader->setUniform(m_roundedCorners.roundTopRightCornerLocation, false);
    m_roundedCorners.shader->setUniform(m_roundedCorners.roundBottomLeftCornerLocation, true);
    m_roundedCorners.shader->setUniform(m_roundedCorners.roundBottomRightCornerLocation, true);
    m_roundedCorners.shader->setUniform(m_roundedCorners.topCornerRadiusLocation, static_cast<float>(0));
    m_roundedCorners.shader->setUniform(m_roundedCorners.bottomCornerRadiusLocation, radius);
    m_roundedCorners.shader->setUniform(m_roundedCorners.antialiasingLocation, static_cast<float>(0));
    m_roundedCorners.shader->setUniform(m_roundedCorners.regionSizeLocation, QVector2D(size, size));
    m_roundedCorners.shader->setUniform(m_roundedCorners.mvpMatrixLocation, projectionMatrix);

    GLFramebuffer::pushFramebuffer(cornersFramebuffer.get());
    vbo->draw(GL_TRIANGLES, 0, 6);
    GLFramebuffer::popFramebuffer();
    ShaderManager::instance()->popShader();
    vbo->unbindArrays();

    QImage img = cornersTexture->toImage().mirrored().copy(0, 0, radius, radius);
    if (!top) {
        img.mirror();
    }

    left = QRegion(QBitmap::fromImage(img.createMaskFromColor(QColor(Qt::black).rgb(), Qt::MaskOutColor), Qt::DiffuseAlphaDither));
    right = QRegion(QBitmap::fromImage(img.mirrored(true, false).createMaskFromColor(QColor(Qt::black).rgb(), Qt::MaskOutColor), Qt::DiffuseAlphaDither));;
}

std::array<QRegion, 4> BlurEffect::roundedCorners(int topCornerRadius, int bottomCornerRadius, qreal scale)
{
    const auto key = std::make_tuple(topCornerRadius, bottomCornerRadius, scale);
    if (m_corners.contains(key)) {
        return m_corners[key];
    }

    std::array<QRegion, 4> corners;
    generateRoundedCornerMasks(topCornerRadius, corners[0], corners[1], true);
    generateRoundedCornerMasks(bottomCornerRadius, corners[2], corners[3], false);
    return m_corners[key] = corners;
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

    windowExpandedGeometryChangedConnections[w] = connect(w, &EffectWindow::windowExpandedGeometryChanged, this, [this,w]() {
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
    if (auto it = windowExpandedGeometryChangedConnections.find(w); it != windowExpandedGeometryChangedConnections.end()) {
        disconnect(*it);
        windowExpandedGeometryChangedConnections.erase(it);
    }

    if (m_blurWhenTransformed.contains(w)) {
        m_blurWhenTransformed.removeOne(w);
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
    return false;
}

bool BlurEffect::supported()
{
#ifdef KWIN_6_0
    return effects->isOpenGLCompositing() && GLFramebuffer::supported() && GLFramebuffer::blitSupported();
#else
    return effects->openglContext() && effects->openglContext()->supportsBlits();
#endif
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

    QRegion decorationRegion = QRegion(w->decoration()->rect()) - w->contentsRect().toRect();
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

QRegion BlurEffect::transformedBlurRegion(QRegion blurRegion, const WindowPaintData &data) const
{
    if (data.xScale() != 1 || data.yScale() != 1) {
        QPoint pt = blurRegion.boundingRect().topLeft();
        QRegion scaledShape;
        for (const QRect &r : blurRegion) {
            const QPointF topLeft(pt.x() + (r.x() - pt.x()) * data.xScale() + data.xTranslation(),
                                  pt.y() + (r.y() - pt.y()) * data.yScale() + data.yTranslation());
            const QPoint bottomRight(std::floor(topLeft.x() + r.width() * data.xScale()) - 1,
                                     std::floor(topLeft.y() + r.height() * data.yScale()) - 1);
            scaledShape += QRect(QPoint(std::floor(topLeft.x()), std::floor(topLeft.y())), bottomRight);
        }
        blurRegion = scaledShape;
    } else if (data.xTranslation() || data.yTranslation()) {
        blurRegion.translate(std::round(data.xTranslation()), std::round(data.yTranslation()));
    }

    return blurRegion;
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

    bool hasFakeBlur = m_fakeBlur && m_hasValidFakeBlurTexture && !blurArea.isEmpty();
    if (hasFakeBlur) {
        data.opaque += blurArea;

        int topCornerRadius;
        int bottomCornerRadius;
        if (isMenu(w)) {
            topCornerRadius = bottomCornerRadius = m_menuCornerRadius;
        } else if (w->isDock()) {
            topCornerRadius = bottomCornerRadius = m_dockCornerRadius;
        } else {
            topCornerRadius = m_windowTopCornerRadius;
            bottomCornerRadius = m_windowBottomCornerRadius;
        }

        if (!w->isDock() || (w->isDock() && isDockFloating(w, blurArea))) {
            const QRect blurRect = blurArea.boundingRect();
            data.opaque -= QRect(blurRect.x(), blurRect.y(), topCornerRadius, topCornerRadius);
            data.opaque -= QRect(blurRect.x() + blurRect.width() - topCornerRadius, blurRect.y(), topCornerRadius, topCornerRadius);
            data.opaque -= QRect(blurRect.x(), blurRect.y() + blurRect.height() - bottomCornerRadius, bottomCornerRadius, bottomCornerRadius);
            data.opaque -= QRect(blurRect.x() + blurRect.width() - bottomCornerRadius, blurRect.y() + blurRect.height() - bottomCornerRadius, bottomCornerRadius, bottomCornerRadius);
        }
    }

    if (shouldForceBlur(w) && m_paintAsTranslucent) {
        data.setTranslucent();
    }

    effects->prePaintWindow(w, data, presentTime);

    if (!hasFakeBlur) {
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
}

bool BlurEffect::shouldBlur(const EffectWindow *w, int mask, const WindowPaintData &data)
{
    const bool hasForceBlurRole = w->data(WindowForceBlurRole).toBool();

    if (effects->activeFullScreenEffect() && !hasForceBlurRole) {
        return false;
    }

    if (w->isDesktop()) {
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
    if (w->isDesktop() || (!m_blurDocks && w->isDock()) || (!m_blurMenus && isMenu(w))) {
        return false;
    }

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

    int topCornerRadius;
    int bottomCornerRadius;
    if (isMenu(w)) {
        topCornerRadius = bottomCornerRadius = m_menuCornerRadius;
    } else if (w->isDock()) {
        topCornerRadius = bottomCornerRadius = m_dockCornerRadius;
    } else {
        topCornerRadius = m_windowTopCornerRadius;
        bottomCornerRadius = m_windowBottomCornerRadius;
    }
    topCornerRadius = std::max(0, (int)std::round(topCornerRadius * viewport.scale()) - m_cornerRadiusOffset);
    bottomCornerRadius = std::max(0, (int)std::round(bottomCornerRadius * viewport.scale()) - m_cornerRadiusOffset);

    bool hasRoundedCorners = topCornerRadius || bottomCornerRadius;

    const QRegion rawBlurRegion = blurRegion(w);
    const QRegion blurShape = transformedBlurRegion(rawBlurRegion.translated(w->pos().toPoint()), data);
    const QRect backgroundRect = blurShape.boundingRect();

    // The blur shape has to be moved to 0,0 before being scaled, otherwise the size may end up being off by 1 pixel.
    QRegion scaledBlurShape = scaledRegion(blurShape.translated(-backgroundRect.topLeft()), viewport.scale());
    const QRect deviceBackgroundRect = snapToPixelGrid(scaledRect(backgroundRect, viewport.scale()));

    bool roundTopLeftCorner = false;
    bool roundTopRightCorner = false;
    bool roundBottomLeftCorner = false;
    bool roundBottomRightCorner = false;
    const bool isMaximized = effects->clientArea(MaximizeArea, effects->activeScreen(), effects->currentDesktop()) == w->frameGeometry();
    if (hasRoundedCorners && ((!w->isFullScreen() && !isMaximized) || m_roundCornersOfMaximizedWindows)) {
        if (w->isDock()) {
            if (isDockFloating(w, rawBlurRegion)) {
                roundTopLeftCorner = roundTopRightCorner = topCornerRadius;
                roundBottomLeftCorner = roundBottomRightCorner = bottomCornerRadius;
            }
        } else {
            // Ensure the blur region corners touch the window corners before rounding them.
            if (topCornerRadius && (!w->decoration() || (w->decoration() && m_blurDecorations))) {
                roundTopLeftCorner = rawBlurRegion.intersects(QRect(0, 0, topCornerRadius, topCornerRadius));
                roundTopRightCorner = rawBlurRegion.intersects(QRect(w->width() - topCornerRadius, 0, topCornerRadius, topCornerRadius));
            }
            if (bottomCornerRadius) {
                roundBottomLeftCorner = rawBlurRegion.intersects(QRect(0, w->height() - bottomCornerRadius, bottomCornerRadius, bottomCornerRadius));
                roundBottomRightCorner = rawBlurRegion.intersects(QRect(w->width() - bottomCornerRadius, w->height() - bottomCornerRadius, bottomCornerRadius, bottomCornerRadius));
            }
            hasRoundedCorners = roundTopLeftCorner || roundTopRightCorner || roundBottomLeftCorner || roundBottomRightCorner;
        }

        const auto corners = roundedCorners(topCornerRadius, bottomCornerRadius, viewport.scale());
        const QRect blurRect = scaledBlurShape.boundingRect();

        if (roundTopLeftCorner) {
            scaledBlurShape -= corners[0];
        }
        if (roundTopRightCorner) {
            scaledBlurShape -= corners[1].translated(blurRect.width() - topCornerRadius, 0);
        }

        if (roundBottomLeftCorner) {
            scaledBlurShape -= corners[2].translated(0, blurRect.height() - bottomCornerRadius);
        }
        if (roundBottomRightCorner) {
            scaledBlurShape -= corners[3].translated(blurRect.width() - bottomCornerRadius, blurRect.height() - bottomCornerRadius);
        }
    }

    const auto opacity = m_transparentBlur
        ? w->opacity() * data.opacity()
        : data.opacity();

    // Get the effective shape that will be actually blurred. It's possible that all of it will be clipped.
    QRegion effectiveShape;
    if (region != infiniteRegion()) {
        for (const QRect &clipRect : region) {
            const QRect deviceClipRect = snapToPixelGrid(scaledRect(clipRect, viewport.scale()))
                    .translated(-deviceBackgroundRect.topLeft());
            for (const QRect &shapeRect : scaledBlurShape) {
                const QRect deviceShapeRect = shapeRect;
                if (const QRect intersected = deviceClipRect.intersected(deviceShapeRect); !intersected.isEmpty()) {
                    effectiveShape += intersected;
                }
            }
        }
    } else {
        for (const QRect &rect : scaledBlurShape) {
            effectiveShape += rect;
        }
    }
    if (!effectiveShape.rectCount()) {
        return;
    }

    const bool hasAntialiasedRoundedCorners = hasRoundedCorners && m_roundedCornersAntialiasing > 0;

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
    // This framebuffer is left unchanged, so we can use that for rounding corners.
    const QRegion dirtyRegion = region & backgroundRect;
    for (const QRect &dirtyRect : dirtyRegion) {
        renderInfo.framebuffers[0]->blitFromRenderTarget(renderTarget, viewport, dirtyRect, dirtyRect.translated(-backgroundRect.topLeft()));
    }

    // Upload the geometry: the first 6 vertices are used when downsampling and upsampling offscreen,
    // the remaining vertices are used when rendering on the screen.
    GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setAttribLayout(std::span(GLVertexBuffer::GLVertex2DLayout), sizeof(GLVertex2D));

    const int vertexCount = effectiveShape.rectCount() * 6;
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

    /*
     * The anti-aliasing implementation is actually really bad, but that's the best I can do for now. Suprisingly,
     * there are no performance issues.
     *
     * Anti-aliasing is done by a shader that paints rounded rectangles. It's a modified version of
     * https://www.shadertoy.com/view/ldfSDj.
     * The shader requires two textures: the blur region before being blurred and after being blurred.
     * The first texture can simply be taken from renderInfo.textures[0], as it's left unchanged.
     * The second texture is more tricky. We could just blit, but that's slow. A faster solution is to create a virtual
     * framebuffer with a texture attached to it and paint the blur in that framebuffer instead of the screen.
     *
     * Since only a fragment of the window may be painted, the shader allows to toggle rounding for each corner.
    */

    if (hasAntialiasedRoundedCorners && (!renderInfo.blurTexture || renderInfo.blurTexture->size() != backgroundRect.size())) {
        renderInfo.blurTexture = GLTexture::allocate(textureFormat, backgroundRect.size());
        renderInfo.blurTexture->setFilter(GL_LINEAR);
        renderInfo.blurTexture->setWrapMode(GL_CLAMP_TO_EDGE);
        renderInfo.blurFramebuffer = std::make_unique<GLFramebuffer>(renderInfo.blurTexture.get());
    }

    if (m_fakeBlur && m_hasValidFakeBlurTexture) {
        ShaderManager::instance()->pushShader(m_texturePass.shader.get());

        QMatrix4x4 projectionMatrix;
        if (hasAntialiasedRoundedCorners) {
            projectionMatrix.ortho(QRectF(0.0, 0.0, backgroundRect.width(), backgroundRect.height()));
            GLFramebuffer::pushFramebuffer(renderInfo.blurFramebuffer.get());
        } else {
            projectionMatrix = viewport.projectionMatrix();
            projectionMatrix.translate(deviceBackgroundRect.x(), deviceBackgroundRect.y());
        }

        m_texturePass.shader->setUniform(m_texturePass.mvpMatrixLocation, projectionMatrix);
        m_texturePass.shader->setUniform(m_texturePass.textureSizeLocation, QVector2D(m_texturePass.texture.get()->width(), m_texturePass.texture.get()->height()));
        m_texturePass.shader->setUniform(m_texturePass.texStartPosLocation, QVector2D(backgroundRect.x(), backgroundRect.y()));
        m_texturePass.shader->setUniform(m_texturePass.regionSizeLocation, QVector2D(backgroundRect.width(), backgroundRect.height()));

        m_texturePass.texture.get()->bind();

        if (!hasAntialiasedRoundedCorners) {
            glEnable(GL_BLEND);
            float o = 1.0f - (opacity);
            o = 1.0f - o * o;
            glBlendColor(0, 0, 0, o);
            glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
        }

        vbo->draw(GL_TRIANGLES, hasAntialiasedRoundedCorners ? 0 : 6, hasAntialiasedRoundedCorners ? 6 : vertexCount);

        if (!hasAntialiasedRoundedCorners) {
            glDisable(GL_BLEND);
        }

        ShaderManager::instance()->popShader();
        if (hasAntialiasedRoundedCorners) {
            GLFramebuffer::popFramebuffer();
        }
    } else {
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

        if (hasAntialiasedRoundedCorners) {
            GLFramebuffer::pushFramebuffer(renderInfo.blurFramebuffer.get());
            projectionMatrix = QMatrix4x4();
            projectionMatrix.ortho(QRectF(0.0, 0.0, backgroundRect.width(), backgroundRect.height()));
        } else {
            projectionMatrix = viewport.projectionMatrix();
            projectionMatrix.translate(deviceBackgroundRect.x(), deviceBackgroundRect.y());
        }
        m_upsamplePass.shader->setUniform(m_upsamplePass.mvpMatrixLocation, projectionMatrix);

        const QVector2D halfpixel(0.5 / read->colorAttachment()->width(),
                                  0.5 / read->colorAttachment()->height());
        m_upsamplePass.shader->setUniform(m_upsamplePass.halfpixelLocation, halfpixel);

        read->colorAttachment()->bind();

        // Modulate the blurred texture with the window opacity if the window isn't opaque
        if (!hasAntialiasedRoundedCorners && opacity < 1.0) {
            glEnable(GL_BLEND);
            float o = 1.0f - (opacity);
            o = 1.0f - o * o;
            glBlendColor(0, 0, 0, o);
            glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
        }

        vbo->draw(GL_TRIANGLES, hasAntialiasedRoundedCorners ? 0 : 6, hasAntialiasedRoundedCorners ? 6 : vertexCount);

        if (!hasAntialiasedRoundedCorners && opacity < 1.0) {
            glDisable(GL_BLEND);
        }

        ShaderManager::instance()->popShader();

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

                QMatrix4x4 projectionMatrix;
                if (hasAntialiasedRoundedCorners) {
                    projectionMatrix.ortho(QRectF(0.0, 0.0, backgroundRect.width(), backgroundRect.height()));
                } else {
                    projectionMatrix = viewport.projectionMatrix();
                    projectionMatrix.translate(deviceBackgroundRect.x(), deviceBackgroundRect.y());
                }

                m_noisePass.shader->setUniform(m_noisePass.mvpMatrixLocation, projectionMatrix);
                m_noisePass.shader->setUniform(m_noisePass.noiseTextureSizeLocation, QVector2D(noiseTexture->width(), noiseTexture->height()));
                m_noisePass.shader->setUniform(m_noisePass.texStartPosLocation, QVector2D(deviceBackgroundRect.topLeft()));

                noiseTexture->bind();

                vbo->draw(GL_TRIANGLES, hasAntialiasedRoundedCorners ? 0 : 6, hasAntialiasedRoundedCorners ? 6 : vertexCount);

                ShaderManager::instance()->popShader();
            }

            glDisable(GL_BLEND);
        }

        if (hasAntialiasedRoundedCorners) {
            GLFramebuffer::popFramebuffer();
        }
    }

    if (hasAntialiasedRoundedCorners) {
        QMatrix4x4 projectionMatrix = viewport.projectionMatrix();
        projectionMatrix.translate(deviceBackgroundRect.x(), deviceBackgroundRect.y());

        // The Y axis is flipped in OpenGL.
        // TODO Rename the uniforms
        ShaderManager::instance()->pushShader(m_roundedCorners.shader.get());
        m_roundedCorners.shader->setUniform(m_roundedCorners.roundTopLeftCornerLocation, roundBottomLeftCorner);
        m_roundedCorners.shader->setUniform(m_roundedCorners.roundTopRightCornerLocation, roundBottomRightCorner);
        m_roundedCorners.shader->setUniform(m_roundedCorners.roundBottomLeftCornerLocation, roundTopLeftCorner);
        m_roundedCorners.shader->setUniform(m_roundedCorners.roundBottomRightCornerLocation, roundTopRightCorner);
        m_roundedCorners.shader->setUniform(m_roundedCorners.topCornerRadiusLocation, bottomCornerRadius + m_cornerRadiusOffset);
        m_roundedCorners.shader->setUniform(m_roundedCorners.bottomCornerRadiusLocation, topCornerRadius + m_cornerRadiusOffset);
        m_roundedCorners.shader->setUniform(m_roundedCorners.antialiasingLocation, m_roundedCornersAntialiasing);
        m_roundedCorners.shader->setUniform(m_roundedCorners.regionSizeLocation, QVector2D(deviceBackgroundRect.width(), deviceBackgroundRect.height()));
        m_roundedCorners.shader->setUniform(m_roundedCorners.mvpMatrixLocation, projectionMatrix);

        glUniform1i(m_roundedCorners.beforeBlurTextureLocation, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, renderInfo.textures[0]->texture());

        glUniform1i(m_roundedCorners.afterBlurTextureLocation, 1);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, renderInfo.blurTexture->texture());

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (opacity < 1.0f) {
            float o = 1.0f - (opacity);
            o = 1.0f - o * o;
            glBlendColor(0, 0, 0, o);
            glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
        }

        vbo->draw(GL_TRIANGLES, 6, vertexCount);

        glDisable(GL_BLEND);
        glActiveTexture(GL_TEXTURE0);
        renderInfo.textures[0]->unbind();
        renderInfo.blurTexture->unbind();
        ShaderManager::instance()->popShader();
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
