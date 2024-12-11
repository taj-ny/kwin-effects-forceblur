#pragma once

#include <QImage>
#include <QStringList>

namespace KWin
{

enum class FakeBlurImageSource
{
    Custom,
    DesktopWallpaper
};

enum class WindowClassMatchingMode
{
    Blacklist,
    Whitelist
};


struct GeneralSettings
{
    int blurStrength;
    int noiseStrength;
    bool windowOpacityAffectsBlur;
};

struct ForceBlurSettings
{
    QStringList windowClasses;
    WindowClassMatchingMode windowClassMatchingMode;
    bool blurDecorations;
    bool blurMenus;
    bool blurDocks;
    bool markWindowAsTranslucent;
};

struct RoundedCornersSettings
{
    float windowTopRadius;
    float windowBottomRadius;
    float menuRadius;
    float dockRadius;
    float antialiasing;
    bool roundMaximized;
};

struct FakeBlurSettings
{
    bool enable;
    bool disableWhenWindowBehind;
    FakeBlurImageSource imageSource;
    QImage customImage;
    bool blurCustomImage;
};

class BlurSettings
{
public:
    GeneralSettings general{};
    ForceBlurSettings forceBlur{};
    RoundedCornersSettings roundedCorners{};
    FakeBlurSettings fakeBlur{};

    void read();
};

}
