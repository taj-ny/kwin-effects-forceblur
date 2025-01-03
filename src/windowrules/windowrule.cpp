#include "windowrule.h"

namespace BetterBlur
{

WindowRule::WindowRule(const int &priority)
    : m_priority(priority)
{
}

#ifdef CONFIG_KWIN
bool WindowRule::matches(const Window *w) const
{
    return m_conditions.empty()
        || std::find_if(m_conditions.cbegin(), m_conditions.cend(), [w](const std::unique_ptr<WindowRuleCondition> &condition) {
            return condition->isSatisfied(w);
        }) != m_conditions.cend();
}
#endif

}