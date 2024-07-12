/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2018 Alex Nemeth <alex.nemeth329@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "effect/effect.h"
#include "opengl/glutils.h"
#include "settings.h"
#include "window.h"

#include <QList>

#include <unordered_map>

namespace KWin
{

class BlurManagerInterface;

struct BlurRenderData
{
    /// Temporary render targets needed for the Dual Kawase algorithm, the first texture
    /// contains not blurred background behind the window, it's cached.
    std::vector<std::unique_ptr<GLTexture>> textures;
    std::vector<std::unique_ptr<GLFramebuffer>> framebuffers;
};

struct BlurEffectData
{
    /// The region that should be blurred behind the window
    std::optional<QRegion> content;

    /// The region that should be blurred behind the frame
    std::optional<QRegion> frame;

    /// The render data per screen. Screens can have different color spaces.
    std::unordered_map<Output *, BlurRenderData> render;

    bool hasWindowBehind;
};

class BlurEffect : public KWin::Effect
{
    Q_OBJECT

public:
    BlurEffect();
    ~BlurEffect() override;

    static bool supported();
    static bool enabledByDefault();

    void reconfigure(ReconfigureFlags flags) override;
    void prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime) override;
    void prePaintWindow(EffectWindow *w, WindowPrePaintData &data, std::chrono::milliseconds presentTime) override;
    void drawWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const QRegion &region, WindowPaintData &data) override;

    bool provides(Feature feature) override;
    bool isActive() const override;

    int requestedEffectChainPosition() const override
    {
        return 20;
    }

    bool eventFilter(QObject *watched, QEvent *event) override;

    bool blocksDirectScanout() const override;

public Q_SLOTS:
    void slotWindowAdded(KWin::EffectWindow *w);
    void slotWindowDeleted(KWin::EffectWindow *w);
    void slotScreenRemoved(KWin::Output *screen);
    void slotPropertyNotify(KWin::EffectWindow *w, long atom);
    void setupDecorationConnections(EffectWindow *w);

private:
    void initBlurStrengthValues();
    QRegion blurRegion(EffectWindow *w) const;
    QRegion decorationBlurRegion(const EffectWindow *w) const;
    bool decorationSupportsBlurBehind(const EffectWindow *w) const;
    bool shouldBlur(const EffectWindow *w, int mask, const WindowPaintData &data);
    bool shouldForceBlur(const EffectWindow *w) const;
    void updateBlurRegion(EffectWindow *w, bool geometryChanged = false);
    bool hasFakeBlur(EffectWindow *w);

    /*
     * @param w The pointer to the window being blurred, nullptr if an image is being blurred.
     */
    void blur(BlurRenderData &renderInfo, const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const QRegion &region, WindowPaintData &data);
    GLTexture *blur(const QImage &image);

    GLTexture *ensureFakeBlurTexture(const Output *output);
    GLTexture *ensureNoiseTexture();
private:
    struct
    {
        std::unique_ptr<GLShader> shader;
        int mvpMatrixLocation;
        int offsetLocation;
        int halfpixelLocation;
    } m_downsamplePass;

    struct
    {
        std::unique_ptr<GLShader> shader;
        int mvpMatrixLocation;
        int offsetLocation;
        int halfpixelLocation;
        int textureLocation;

        int noiseLocation;
        int noiseTextureLocation;
        int noiseTextureSizeLocation;

        int topCornerRadiusLocation;
        int bottomCornerRadiusLocation;
        int antialiasingLocation;
        int blurSizeLocation;
        int opacityLocation;


    } m_upsamplePass;

    struct
    {
        std::unique_ptr<GLShader> shader;
        int mvpMatrixLocation;
        int textureSizeLocation;
        int texStartPosLocation;
        int scaleLocation;

        int topCornerRadiusLocation;
        int bottomCornerRadiusLocation;
        int antialiasingLocation;
        int blurSizeLocation;
        int opacityLocation;
    } m_texture;

    bool m_valid = false;
    long net_wm_blur_region = 0;
    QRegion m_paintedArea; // keeps track of all painted areas (from bottom to top)
    QRegion m_currentBlur; // keeps track of the currently blured area of the windows(from bottom to top)
    Output *m_currentScreen = nullptr;

    size_t m_iterationCount; // number of times the texture will be downsized to half size
    int m_offset;
    int m_expandSize;

    std::unique_ptr<GLTexture> noiseTexture;
    qreal noiseTextureScale = 1.0;
    int noiseTextureStength = 0;

    BlurSettings m_settings;

    struct OffsetStruct
    {
        float minOffset;
        float maxOffset;
        int expandSize;
    };

    QList<OffsetStruct> blurOffsets;

    struct BlurValuesStruct
    {
        int iteration;
        float offset;
    };

    QList<BlurValuesStruct> blurStrengthValues;

    QHash<const Output*, GLTexture*> m_fakeBlurTextures;

    // Windows to blur even when transformed.
    QList<const EffectWindow*> m_blurWhenTransformed;

    QMap<EffectWindow *, QMetaObject::Connection> windowBlurChangedConnections;
    QMap<EffectWindow *, QMetaObject::Connection> windowExpandedGeometryChangedConnections;
    std::unordered_map<EffectWindow *, BlurEffectData> m_windows;

    static BlurManagerInterface *s_blurManager;
    static QTimer *s_blurManagerRemoveTimer;
};

inline bool BlurEffect::provides(Effect::Feature feature)
{
    if (feature == Blur) {
        return true;
    }
    return KWin::Effect::provides(feature);
}

} // namespace KWin
