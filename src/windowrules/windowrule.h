#pragma once

#include "windowrulecondition.h"
#include "windowproperties.h"

#ifdef CONFIG_KWIN
#include "blurwindow.h"
#endif

#include <vector>

namespace BetterBlur
{

#ifdef CONFIG_KWIN
class Window;
#endif
class WindowRuleCondition;

class WindowRule
{
public:
    explicit WindowRule(const int &priority);

#ifdef CONFIG_KWIN
    bool matches(const Window *w) const;
#endif

    const int &priority() const
    {
        return m_priority;
    }

    std::vector<std::unique_ptr<WindowRuleCondition>> &conditions()
    {
        return m_conditions;
    }

    WindowProperties *properties()
    {
        return m_properties.get();
    }
private:
    const int m_priority;
    std::unique_ptr<WindowProperties> m_properties = std::make_unique<WindowProperties>();
    std::vector<std::unique_ptr<WindowRuleCondition>> m_conditions;
};

}