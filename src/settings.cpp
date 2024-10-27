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

    fakeBlur.enable = BlurConfig::fakeBlur();
    fakeBlur.disableWhenWindowBehind = BlurConfig::fakeBlurDisableWhenWindowBehind();
    fakeBlur.customImage = QImage(BlurConfig::fakeBlurImage());
    if (BlurConfig::fakeBlurImageSourceDesktopWallpaper()) {
        fakeBlur.imageSource = FakeBlurImageSource::DesktopWallpaper;
    } else {
        fakeBlur.imageSource = FakeBlurImageSource::Custom;
    }
    fakeBlur.blurCustomImage = BlurConfig::fakeBlurCustomImageBlur();
}

}