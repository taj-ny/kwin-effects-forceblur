#pragma once

#include "windowrule.h"

#include <QList>
#include <QObject>

namespace BetterBlur
{

class WindowRule;

class WindowRuleList
{
public:
    void load();

    std::vector<std::unique_ptr<WindowRule>> &rules()
    {
        return m_windowRules;
    }
    const std::vector<std::unique_ptr<WindowRule>> &rules() const
    {
        return m_windowRules;
    }
private:
    void loadSimple();

    std::vector<std::unique_ptr<WindowRule>> m_windowRules;
};

}