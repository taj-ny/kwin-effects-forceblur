#pragma once

#ifdef CONFIG_KWIN
#include "blurwindow.h"
#endif

#include <vector>

#include <QRegularExpression>

namespace BetterBlur
{

#ifdef CONFIG_KWIN
class Window;
#endif

enum class WindowClassMatchingMode
{
    Exact,
    Substring,
    Regex
};

enum class WindowState : uint32_t
{
    Maximized = 1u << 0,
    Fullscreen = 1u << 1,
    All = 0U - 1,
};
Q_DECLARE_FLAGS(WindowStates, WindowState)
Q_DECLARE_OPERATORS_FOR_FLAGS(WindowStates)

enum class WindowType : uint32_t
{
    Normal = 1u << 0,
    Dock = 1u << 1,
    Toolbar = 1u << 2,
    Menu = 1u << 3,
    Dialog = 1u << 4,
    Utility = 1u << 5,
    Tooltip = 1u << 6,
    All = 0U - 1,
};
Q_DECLARE_FLAGS(WindowTypes, WindowType)

class WindowRuleCondition
{
    Q_GADGET

public:
#ifdef CONFIG_KWIN
    bool isSatisfied(const Window *w) const;
#endif

    void setNegateWindowClasses(const bool &negate);
    void setNegateWindowStates(const bool &negate);
    void setNegateWindowTypes(const bool &negate);

    void setHasWindowBehind(const bool &hasWindowBehind);
    void setInternal(const bool &internal);
    void setWindowClass(const QRegularExpression &windowClass);
    void setWindowStates(const WindowStates &windowStates);
    void setWindowTypes(const WindowTypes &windowTypes);

    std::optional<QRegularExpression> &windowClass()
    {
        return m_windowClass;
    }

    const WindowStates &windowStates() const
    {
        return m_windowStates;
    }

    const WindowTypes &windowTypes() const
    {
        return m_windowTypes;
    }
private:
#ifdef CONFIG_KWIN
    bool isHasWindowBehindSubConditionSatisfied(const Window *w) const;
    bool isInternalSubConditionSatisfied(const Window *w) const;
    bool isWindowClassSubConditionSatisfied(const Window *w) const;
    bool isWindowStateSubConditionSatisfied(const Window *w) const;
    bool isWindowTypeSubConditionSatisfied(const Window *w) const;
#endif
    std::optional<bool> m_hasWindowBehind;
    std::optional<bool> m_internal;

    std::optional<QRegularExpression> m_windowClass;
    bool m_negateWindowClasses = false;

    /**
     * Ignored if set to WindowState::All.
     */
    WindowStates m_windowStates = WindowState::All;
    bool m_negateWindowStates = false;

    /**
     * Ignored if set to WindowType::All.
     */
    WindowTypes m_windowTypes = WindowType::All;
    bool m_negateWindowTypes = false;
};

}
