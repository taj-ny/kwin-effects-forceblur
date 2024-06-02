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
    QRegion blurRegion(EffectWindow *w) const;
    QRegion decorationBlurRegion(const EffectWindow *w) const;
    QRegion transformedBlurRegion(QRegion blurRegion, const WindowPaintData &data) const;
    bool decorationSupportsBlurBehind(const EffectWindow *w) const;
    bool shouldBlur(const EffectWindow *w, int mask, const WindowPaintData &data) const;
    bool shouldForceBlur(const EffectWindow *w) const;
    void updateBlurRegion(EffectWindow *w);
    void blur(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const QRegion &region, WindowPaintData &data);
    GLTexture *ensureNoiseTexture();

    /*
     * @param regionScale How much to scale the blur region. This parameter should be 1 if the region is rounded.
     * @returns The area to be blurred. It's possible that all of it will be clipped.
     */
    QList<QRectF> effectiveBlurShape(const QRect &backgroundRect, const QRect &deviceBackgroundRect, const QRegion &paintRegion, const QRegion &blurRegion, qreal screenScale, qreal regionScale) const;

    /*
     * @returns An array containing rounded corner masks for the given screen scale. If no masks exist for the given screen scale,
     * they will be generated.
     */
    std::array<QRegion, 4> roundedCorners(qreal scale);

    /*
     * Generates rounded corner masks for the left and right corner for the given radius.
     * @param top Whether TODO
     */
    void generateRoundedCornerMasks(int radius, QRegion &left, QRegion &right, bool top) const;

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
        int regionSizeLocation;

        std::unique_ptr<GLTexture> texture;
    } m_texturePass;

    struct
    {
        std::unique_ptr<GLShader> shader;

        int roundTopLeftCornerLocation;
        int roundTopRightCornerLocation;
        int roundBottomLeftCornerLocation;
        int roundBottomRightCornerLocation;

        int topCornerRadiusLocation;
        int bottomCornerRadiusLocation;

        int antialiasingLocation;

        int regionSizeLocation;

        int beforeBlurTextureLocation;
        int afterBlurTextureLocation;

        int mvpMatrixLocation;
    } m_roundedCorners;

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
    float m_roundedCornersAntialiasing;
    bool m_roundCornersOfMaximizedWindows;
    bool m_blurMenus;
    bool m_blurDocks;
    bool m_paintAsTranslucent;
    bool m_fakeBlur;
    QString m_fakeBlurImage;

    bool m_hasValidFakeBlurTexture;

    int m_cornerRadiusOffset;

    // Corner masks where the key is the screen scale and the value is an array of the masks
    // (top left, top right, bottom left, bottom right). Used for rounding the blur region.
    std::unordered_map<qreal, std::array<QRegion, 4>> m_corners;

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
