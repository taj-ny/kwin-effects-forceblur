#include "settings.h"
#include "blurconfig.h"

namespace KWin
{

void BlurSettings::read()
{
    BlurConfig::self()->read();

    general.blurStrength = BlurConfig::blurStrength() - 1;
    general.noiseStrength = BlurConfig::noiseStrength();
    general.windowOpacityAffectsBlur = BlurConfig::transparentBlur();
    general.brightness = BlurConfig::brightness();
    general.saturation = BlurConfig::saturation();
    general.contrast = BlurConfig::contrast();

    forceBlur.windowClasses.clear();
    const auto blank = QStringLiteral("blank");
    for (const auto &line : BlurConfig::windowClasses().split("\n", Qt::SkipEmptyParts)) {
        QString unescaped = "";
        bool consumed = false;
        for (qsizetype i = 0; i < line.size(); i++) {
            const auto character = line[i];
            if (character == QChar('$') && !consumed) {
                consumed = true;
                continue;
            }
            if (consumed) {
                const qsizetype skips = blank.size();
                if (line.mid(i, skips) == blank) {
                    consumed = false;
                    i += skips - 1;
                    continue;
                }
            }
            consumed = false;
            unescaped += character;
        }
        if (consumed) {
            unescaped += QChar('$');
        }
        forceBlur.windowClasses << unescaped;
    }
    forceBlur.windowClassMatchingMode = BlurConfig::blurMatching() ? WindowClassMatchingMode::Whitelist : WindowClassMatchingMode::Blacklist;
    forceBlur.blurDecorations = BlurConfig::blurDecorations();
    forceBlur.blurMenus = BlurConfig::blurMenus();
    forceBlur.blurDocks = BlurConfig::blurDocks();

    roundedCorners.windowTopRadius = BlurConfig::topCornerRadius();
    roundedCorners.windowBottomRadius = BlurConfig::bottomCornerRadius();
    roundedCorners.menuRadius = BlurConfig::menuCornerRadius();
    roundedCorners.dockRadius = BlurConfig::dockCornerRadius();
    roundedCorners.antialiasing = BlurConfig::roundedCornersAntialiasing();
    roundedCorners.roundMaximized = BlurConfig::roundCornersOfMaximizedWindows();

    staticBlur.enable = BlurConfig::fakeBlur();
    staticBlur.disableWhenWindowBehind = BlurConfig::fakeBlurDisableWhenWindowBehind();
    staticBlur.customImage = QImage(BlurConfig::fakeBlurImage());
    if (BlurConfig::fakeBlurImageSourceDesktopWallpaper()) {
        staticBlur.imageSource = StaticBlurImageSource::DesktopWallpaper;
    } else {
        staticBlur.imageSource = StaticBlurImageSource::Custom;
    }
    staticBlur.blurCustomImage = BlurConfig::fakeBlurCustomImageBlur();

    refraction.edgeSizePixels = BlurConfig::refractionEdgeSize() * 10;

    {
        const double maxCorner = 200.0;
        const double steps = 30.0;
        const double stepSize = maxCorner / steps; // ≈6.6667
        const double raw = BlurConfig::refractionCornerRadius();
        const double snapped = std::round(raw / stepSize) * stepSize;
        refraction.refractionCornerRadiusPixels = snapped;
    }

    refraction.refractionStrength = BlurConfig::refractionStrength() / 30.0;
    refraction.refractionNormalPow = BlurConfig::refractionNormalPow() / 2.0;
    refraction.refractionRGBFringing = BlurConfig::refractionRGBFringing() / 30.0;
    refraction.refractionTextureRepeatMode = BlurConfig::refractionTextureRepeatMode();
    refraction.refractionMode = BlurConfig::refractionMode();
}

}