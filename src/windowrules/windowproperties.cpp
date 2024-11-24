#include "windowproperties.h"

namespace BetterBlur
{

void WindowProperties::setWindowOpacityAffectsBlur(const bool &windowOpacityAffectsBlur)
{
    m_windowOpacityAffectsBlur = windowOpacityAffectsBlur;
};

void WindowProperties::setBlurContent(const bool &blurContent)
{
    m_blurContent = blurContent;
}

void WindowProperties::setBlurDecorations(const bool &blurDecorations)
{
    m_blurDecorations = blurDecorations;
}

void WindowProperties::setForceTransparency(const bool &forceTransparency)
{
    m_forceTransparency = forceTransparency;
}

void WindowProperties::setBottomCornerRadius(const float &radius)
{
    m_bottomCornerRadius = radius;
}

void WindowProperties::setTopCornerRadius(const float &radius)
{
    m_topCornerRadius = radius;
}

void WindowProperties::setCornerAntialiasing(const float &antialiasing)
{
    m_cornerAntialiasing = antialiasing;
}

void WindowProperties::setStaticBlur(const bool &staticBlur)
{
    m_staticBlur = staticBlur;
}

}