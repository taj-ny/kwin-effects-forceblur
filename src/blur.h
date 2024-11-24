/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2018 Alex Nemeth <alex.nemeth329@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "windowrules/windowrulelist.h"

#include "effect/effect.h"
#include "opengl/glutils.h"
#include "blurwindow.h"

#include <QList>

#include <unordered_map>

namespace KWin
{

class BlurManagerInterface;

class BlurEffect : public KWin::Effect
{
    Q_OBJECT

public:
    BlurEffect();
    ~BlurEffect() override;

    static bool supported();
    static bool enabledByDefault();

    static const std::vector<EffectWindow *> allWindows();

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
    void slotScreenAdded(KWin::Output *screen);
    void slotScreenRemoved(KWin::Output *screen);
    void slotPropertyNotify(KWin::EffectWindow *w, long atom);

private:
    void initBlurStrengthValues();

    /*
     * @param w The pointer to the window being blurred, nullptr if an image is being blurred.
     */
    void blur(BetterBlur::Window *w, BetterBlur::BlurRenderData &renderInfo, const RenderTarget &renderTarget, const RenderViewport &viewport, int mask, const QRegion &region, WindowPaintData &data);
    void blur(GLTexture *texture);

    /**
     * @param output Can be nullptr.
     * @remark This method shall not be called outside of BlurEffect::blur.
     * @return The cached static blur texture. The texture will be created if it doesn't exist.
     */
    GLTexture *ensureStaticBlurTexture(const Output *output, const RenderTarget &renderTarget);
    GLTexture *ensureNoiseTexture();

    /**
     * @remark This method shall not be called outside of BlurEffect::blur.
     * @return A pointer to a texture containing the wallpaper of the specified desktop, or nullptr if an error
     * occurred. The texture will contain icons and widgets, if there are any.
     */
    GLTexture *wallpaper(EffectWindow *desktop, const qreal &scale, const GLenum &textureFormat);

    /**
     * Creates a static blur texture for the specified screen.
     * @remark This method shall not be called outside of BlurEffect::blur.
     * @return A pointer to the texture, or nullptr if an error occurred.
     */
    GLTexture *createStaticBlurTextureWayland(const Output *output, const RenderTarget &renderTarget, const GLenum &textureFormat);

    /**
     * Creates a composite static blur texture containing images for all screens.
     * @return A pointer to the texture, or nullptr if an error occurred.
     */
    GLTexture *createStaticBlurTextureX11(const GLenum &textureFormat);

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
    QRegion m_windowGeometriesSum;

    size_t m_iterationCount; // number of times the texture will be downsized to half size
    int m_offset;
    int m_expandSize;
    int m_noiseStrength;

    std::unique_ptr<GLTexture> noiseTexture;
    qreal noiseTextureScale = 1.0;
    int noiseTextureStength = 0;

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

    std::unordered_map<const Output*, std::unique_ptr<GLTexture>> m_staticBlurTextures;
    QImage m_staticBlurImage;

    QMap<EffectWindow *, QMetaObject::Connection> windowExpandedGeometryChangedConnections;
    QMap<Output *, QMetaObject::Connection> screenChangedConnections;
    std::unordered_map<EffectWindow *, std::unique_ptr<BetterBlur::Window>> m_windows;

    std::unique_ptr<BetterBlur::WindowRuleList> m_config;

    /**
     * Stores all currently open windows, even those that aren't blurred. Used for determining whether windows are
     * overlapping.
     *
     * Objects retrieved from effects->stackingOrder() and workspace()->stackingOrder() appear to be deleted when
     * BlurEffect::prePaintWindow is running, so that can't be used.
     */
    static std::vector<EffectWindow *> s_allWindows;

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
