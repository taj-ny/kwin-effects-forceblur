#include "windowrulecondition.h"

#ifdef CONFIG_KWIN
#include "blur.h"
#include "window.h"
#endif

namespace BetterBlur
{

#ifdef CONFIG_KWIN
bool WindowRuleCondition::isSatisfied(const Window *w) const
{
    return isHasWindowBehindSubConditionSatisfied(w)
        && isWindowClassSubConditionSatisfied(w)
        && isWindowStateSubConditionSatisfied(w)
        && isWindowTypeSubConditionSatisfied(w);
}

bool WindowRuleCondition::isHasWindowBehindSubConditionSatisfied(const Window *w) const
{
    if (!m_hasWindowBehind.has_value()) {
        return true;
    }

    return w->hasWindowBehind() == *m_hasWindowBehind;
}

bool WindowRuleCondition::isWindowClassSubConditionSatisfied(const Window *w) const
{
    if (!m_windowClass) {
        return true;
    }

    return ((*m_windowClass).match(w->window()->window()->resourceClass()).hasMatch()
            || (*m_windowClass).match(w->window()->window()->resourceName()).hasMatch())
            == !m_negateWindowClasses;
}

bool WindowRuleCondition::isWindowStateSubConditionSatisfied(const Window *w) const
{
    if (m_windowStates == WindowState::All) {
        return true;
    }

    const auto effectWindow = w->window();
    const bool satisfied =
        ((m_windowStates & WindowState::Fullscreen) && effectWindow->isFullScreen())
        || ((m_windowStates & WindowState::Maximized) && w->isMaximized());
    return m_negateWindowStates == !satisfied;
}

bool WindowRuleCondition::isWindowTypeSubConditionSatisfied(const Window *w) const
{
    if (m_windowTypes == WindowType::All)
        return true;

    const auto effectWindow = w->window();
    const bool satisfied =
        ((m_windowTypes & WindowType::Normal) && effectWindow->isNormalWindow())
        || ((m_windowTypes & WindowType::Dock) && effectWindow->isDock())
        || ((m_windowTypes & WindowType::Toolbar) && effectWindow->isToolbar())
        || ((m_windowTypes & WindowType::Menu)
            && (effectWindow->isMenu() || effectWindow->isDropdownMenu() || effectWindow->isPopupMenu() || effectWindow->isPopupWindow()))
        || ((m_windowTypes & WindowType::Dialog) && effectWindow->isDialog())
        || ((m_windowTypes & WindowType::Utility) && effectWindow->isUtility())
        || ((m_windowTypes & WindowType::Tooltip) && effectWindow->isTooltip());
    return m_negateWindowTypes == !satisfied;
}
#endif

void WindowRuleCondition::setNegateWindowClasses(const bool &negate)
{
    m_negateWindowClasses = negate;
}

void WindowRuleCondition::setNegateWindowStates(const bool &negate)
{
    m_negateWindowStates = negate;
}

void WindowRuleCondition::setNegateWindowTypes(const bool &negate)
{
    m_negateWindowTypes = negate;
}

void WindowRuleCondition::setHasWindowBehind(const bool &hasWindowBehind)
{
    m_hasWindowBehind = hasWindowBehind;
}

void WindowRuleCondition::setWindowClass(const QRegularExpression &windowClass)
{
    m_windowClass = windowClass;
}

void WindowRuleCondition::setWindowStates(const WindowStates &windowStates)
{
    m_windowStates = windowStates;
}

void WindowRuleCondition::setWindowTypes(const WindowTypes &windowTypes)
{
    m_windowTypes = windowTypes;
}

}