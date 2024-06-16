/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2018 Alex Nemeth <alex.nemeth329@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "effect/effect.h"
#include "opengl/glutils.h"
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
    QRegion blurRegion(EffectWindow *w, bool noRoundedCorners = false) const;
    QRegion decorationBlurRegion(const EffectWindow *w) const;
    bool decorationSupportsBlurBehind(const EffectWindow *w) const;
    bool shouldBlur(const EffectWindow *w, int mask, const WindowPaintData &data);
    bool shouldForceBlur(const EffectWindow *w) const;
    void updateBlurRegion(EffectWindow *w);
    void updateCornerRegions();
    void blur(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const QRegion &region, WindowPaintData &data);
    GLTexture *ensureNoiseTexture();
    bool hasFakeBlur(EffectWindow *w) const;

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
    } m_upsamplePass;

    struct
    {
        std::unique_ptr<GLShader> shader;
        int mvpMatrixLocation;
        int noiseTextureSizeLocation;
        int texStartPosLocation;

        std::unique_ptr<GLTexture> noiseTexture;
        qreal noiseTextureScale = 1.0;
        int noiseTextureStength = 0;
    } m_noisePass;

    struct
    {
        std::unique_ptr<GLShader> shader;
        int mvpMatrixLocation;
        int textureSizeLocation;
        int texStartPosLocation;

        std::unique_ptr<GLTexture> texture;
    } m_texturePass;

    bool m_valid = false;
    long net_wm_blur_region = 0;
    QRegion m_paintedArea; // keeps track of all painted areas (from bottom to top)
    QRegion m_currentBlur; // keeps track of the currently blured area of the windows(from bottom to top)
    Output *m_currentScreen = nullptr;

    size_t m_iterationCount; // number of times the texture will be downsized to half size
    int m_offset;
    int m_expandSize;
    int m_noiseStrength;
    QStringList m_windowClasses;
    bool m_blurMatching;
    bool m_blurNonMatching;
    bool m_blurDecorations;
    bool m_transparentBlur;
    int m_topCornerRadius;
    int m_bottomCornerRadius;
    bool m_roundCornersOfMaximizedWindows;
    bool m_blurMenus;
    bool m_blurDocks;
    bool m_paintAsTranslucent;
    bool m_fakeBlur;
    QString m_fakeBlurImage;

    bool m_hasValidFakeBlurTexture;

    // Regions to subtract from the blurred region
    QRegion m_topLeftCorner;
    QRegion m_topRightCorner;
    QRegion m_bottomLeftCorner;
    QRegion m_bottomRightCorner;

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
