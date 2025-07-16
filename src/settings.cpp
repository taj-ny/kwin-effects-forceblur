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

    forceBlur.windowClasses = BlurConfig::windowClasses().split("\n");
    forceBlur.windowClassMatchingMode = BlurConfig::blurMatching() ? WindowClassMatchingMode::Whitelist : WindowClassMatchingMode::Blacklist;
    forceBlur.blurDecorations = BlurConfig::blurDecorations();
    forceBlur.blurMenus = BlurConfig::blurMenus();
    forceBlur.blurDocks = BlurConfig::blurDocks();
    forceBlur.markWindowAsTranslucent = BlurConfig::paintAsTranslucent();

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

    refraction.edgeSizePixels = BlurConfig::refractionEdgeSize() * 3;
    refraction.refractionStrength = BlurConfig::refractionStrength() / 20.0;
    refraction.refractionNormalPow = BlurConfig::refractionNormalPow() / 2.0;
}

}