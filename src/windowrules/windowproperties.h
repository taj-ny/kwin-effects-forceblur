#pragma once

#include <QObject>

#include <optional>

namespace BetterBlur
{

class WindowProperties
{
    Q_GADGET

    Q_PROPERTY(bool blurContent READ blurContent WRITE setBlurContent)
    Q_PROPERTY(bool blurDecorations READ blurDecorations WRITE setBlurDecorations)
    Q_PROPERTY(bool forceTransparency READ forceTransparency WRITE setForceTransparency)

    Q_PROPERTY(float bottomCornerRadius READ bottomCornerRadius WRITE setBottomCornerRadius)
    Q_PROPERTY(float topCornerRadius READ topCornerRadius WRITE setTopCornerRadius)
    Q_PROPERTY(float cornerAntialiasing READ cornerAntialiasing WRITE setCornerAntialiasing)
public:
    bool windowOpacityAffectsBlur() const
    {
        return m_windowOpacityAffectsBlur.value_or(false);
    }
    void setWindowOpacityAffectsBlur(const bool &windowOpacityAffectsBlur);

    bool blurContent() const
    {
        return m_blurContent.value_or(false);
    };
    void setBlurContent(const bool &blurContent);

    bool blurDecorations() const
    {
        return m_blurDecorations.value_or(false);
    };
    void setBlurDecorations(const bool &blurDecorations);

    bool forceTransparency() const
    {
        return m_forceTransparency.value_or(false);
    };
    void setForceTransparency(const bool &forceTransparency);

    float bottomCornerRadius() const
    {
        return m_bottomCornerRadius.value_or(0);
    };
    void setBottomCornerRadius(const float &radius);

    float topCornerRadius() const
    {
        return m_topCornerRadius.value_or(0);
    };
    void setTopCornerRadius(const float &radius);

    float cornerAntialiasing() const
    {
        return m_cornerAntialiasing.value_or(0);
    };
    void setCornerAntialiasing(const float &antialiasing);

    bool staticBlur() const
    {
        return m_staticBlur.value_or(false);
    }
    void setStaticBlur(const bool &staticBlur);

private:
    std::optional<bool> m_windowOpacityAffectsBlur;

    std::optional<bool> m_blurContent;
    std::optional<bool> m_blurDecorations;
    std::optional<bool> m_forceTransparency;

    std::optional<float> m_bottomCornerRadius;
    std::optional<float> m_topCornerRadius;
    std::optional<float> m_cornerAntialiasing;

    std::optional<bool> m_staticBlur;

    friend class Window;
};

}