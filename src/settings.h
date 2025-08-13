#pragma once

#include <QImage>
#include <QStringList>

namespace KWin
{

enum class StaticBlurImageSource
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
    float brightness;
    float saturation;
    float contrast;
};

struct ForceBlurSettings
{
    QStringList windowClasses;
    WindowClassMatchingMode windowClassMatchingMode;
    bool blurDecorations;
    bool blurMenus;
    bool blurDocks;
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

struct StaticBlurSettings
{
    bool enable;
    bool disableWhenWindowBehind;
    StaticBlurImageSource imageSource;
    QImage customImage;
    bool blurCustomImage;
};

struct RefractionSettings
{
    float edgeSizePixels;
    float refractionStrength;
    float refractionNormalPow;
    float refractionRGBFringing;
    int refractionTextureRepeatMode;
};

class BlurSettings
{
public:
    GeneralSettings general{};
    ForceBlurSettings forceBlur{};
    RoundedCornersSettings roundedCorners{};
    StaticBlurSettings staticBlur{};
    RefractionSettings refraction{};

    void read();
};

}
