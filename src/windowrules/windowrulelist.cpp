#include "windowrulelist.h"

#include "config.h"

#include <KConfig>

namespace BetterBlur
{

void WindowRuleList::load()
{
    m_windowRules.clear();

    if (Config::convertSimpleConfigToRules()) {
        loadSimple();
    }

    KConfig config("kwinbetterblurrc", KConfig::SimpleConfig);
    const auto windowRulesGroup = config.group("WindowRules");
    for (const auto &windowRuleGroupName : windowRulesGroup.groupList()) {
        const auto windowRuleGroup = windowRulesGroup.group(windowRuleGroupName);

        const auto priority = windowRuleGroup.readEntry("Priority", 0);
        auto rule = std::make_unique<WindowRule>(priority);
        auto ruleProperties = rule->properties();

        auto conditionsGroup = windowRuleGroup.group("Conditions");
        for (const auto &conditionGroupName : conditionsGroup.groupList()) {
            const auto conditionGroup = conditionsGroup.group(conditionGroupName);
            auto condition = std::make_unique<WindowRuleCondition>();

            for (const auto &subcondition : conditionGroup.readEntry("Negate", "").split(" ")) {
                if (subcondition == "WindowClass") {
                    condition->setNegateWindowClasses(true);
                } else if (subcondition == "WindowState") {
                    condition->setNegateWindowStates(true);
                } else if (subcondition == "WindowType") {
                    condition->setNegateWindowTypes(true);
                }
            }

            if (conditionGroup.hasKey("HasWindowBehind")) {
                condition->setHasWindowBehind(conditionGroup.readEntry("HasWindowBehind", false));
            }

            if (conditionGroup.hasKey("Internal")) {
                condition->setInternal(conditionGroup.readEntry("Internal", false));
            }

            if (conditionGroup.hasKey("WindowClass")) {
                condition->setWindowClass(QRegularExpression(conditionGroup.readEntry("WindowClass", "")));
            }

            if (conditionGroup.hasKey("WindowState")) {
                WindowStates states;
                for (const auto &stateStr : conditionGroup.readEntry("WindowState", "").split(" ")) {
                    if (stateStr == "Fullscreen") {
                        states |= WindowState::Fullscreen;
                    } else if (stateStr == "Maximized") {
                        states |= WindowState::Maximized;
                    }
                }
                condition->setWindowStates(states);
            }

            if (conditionGroup.hasKey("WindowType")) {
                WindowTypes types;
                for (const auto &typeStr : conditionGroup.readEntry("WindowType", "").split(" ")) {
                    if (typeStr == "Dialog") {
                        types |= WindowType::Dialog;
                    } else if (typeStr == "Dock") {
                        types |= WindowType::Dock;
                    } else if (typeStr == "Normal") {
                        types |= WindowType::Normal;
                    } else if (typeStr == "Menu") {
                        types |= WindowType::Menu;
                    } else if (typeStr == "Toolbar") {
                        types |= WindowType::Toolbar;
                    } else if (typeStr == "Tooltip") {
                        types |= WindowType::Tooltip;
                    } else if (typeStr == "Utility") {
                        types |= WindowType::Utility;
                    }
                }
                condition->setWindowTypes(types);
            }

            rule->conditions().push_back(std::move(condition));
        }

        auto propertiesGroup = windowRuleGroup.group("Properties");
        if (propertiesGroup.hasKey("BlurContent")) {
            ruleProperties->setBlurContent(propertiesGroup.readEntry("BlurContent", false));
        }
        if (propertiesGroup.hasKey("BlurDecorations")) {
            ruleProperties->setBlurDecorations(propertiesGroup.readEntry("BlurDecorations", false));
        }
        if (propertiesGroup.hasKey("BlurStrength")) {
            ruleProperties->setBlurStrength(propertiesGroup.readEntry("BlurStrength", 15));
        }
        if (propertiesGroup.hasKey("CornerRadius")) {
            float topCornerRadius = 0;
            float bottomCornerRadius = 0;

            auto cornerRadius = propertiesGroup.readEntry("CornerRadius", "");
            if (cornerRadius.contains(" ")) {
                topCornerRadius = cornerRadius.split(" ")[0].toFloat();
                bottomCornerRadius = cornerRadius.split(" ")[1].toFloat();
            } else {
                topCornerRadius = bottomCornerRadius = cornerRadius.toFloat();
            }

            if (topCornerRadius >= 0) {
                ruleProperties->setTopCornerRadius(topCornerRadius);
            }
            if (bottomCornerRadius >= 0) {
                ruleProperties->setBottomCornerRadius(bottomCornerRadius);
            }
        }
        if (propertiesGroup.hasKey("CornerAntialiasing")) {
            ruleProperties->setCornerAntialiasing(propertiesGroup.readEntry("CornerAntialiasing", 0.0));
        }
        if (propertiesGroup.hasKey("ForceTransparency")) {
            ruleProperties->setForceTransparency(propertiesGroup.readEntry("ForceTransparency", false));
        }
        if (propertiesGroup.hasKey("StaticBlur")) {
            ruleProperties->setStaticBlur(propertiesGroup.readEntry("StaticBlur", false));
        }
        if (propertiesGroup.hasKey("WindowOpacityAffectsBlur")) {
            ruleProperties->setWindowOpacityAffectsBlur(propertiesGroup.readEntry("WindowOpacityAffectsBlur", false));
        }

        if (m_windowRules.empty()) {
            m_windowRules.push_back(std::move(rule));
            continue;
        }
        auto it = std::lower_bound(m_windowRules.begin(), m_windowRules.end(), rule, [](const std::unique_ptr<WindowRule>& a, const std::unique_ptr<WindowRule>& b) {
            return a->priority() < b->priority();
        });
        m_windowRules.insert(it, std::move(rule));
    }
}

void WindowRuleList::loadSimple()
{
    m_windowRules.clear();

    KConfig kwinrc("kwinrc", KConfig::SimpleConfig);
    auto betterBlurV1Group = kwinrc.group("Effect-blurplus");

    auto blurStrengthRule = std::make_unique<WindowRule>(-1);
    blurStrengthRule->properties()->setBlurStrength(betterBlurV1Group.readEntry("BlurStrength", 15));
    m_windowRules.push_back(std::move(blurStrengthRule));

    // Force blur
    auto forceBlurRule = std::make_unique<WindowRule>(-1);
    forceBlurRule->properties()->setBlurContent(true);
    forceBlurRule->properties()->setBlurDecorations(betterBlurV1Group.readEntry("BlurDecorations", false));
    forceBlurRule->properties()->setForceTransparency(betterBlurV1Group.readEntry("PaintAsTranslucent", false));
    auto forceBlurRuleCondition = std::make_unique<WindowRuleCondition>();
    forceBlurRuleCondition->setNegateWindowClasses(!betterBlurV1Group.readEntry("BlurMatching", true));
    QStringList regexes;
    for (const auto &windowClass : betterBlurV1Group.readEntry("WindowClasses", "").split("\n")) {
        regexes << (windowClass.startsWith("$regex:")
            ? windowClass.mid(7)
            : "^" + QRegularExpression::escape(windowClass) + "$");
    }
    if (!regexes.empty()) {
        forceBlurRuleCondition->setWindowClass(QRegularExpression(regexes.join("|")));
    }
    WindowTypes types = static_cast<WindowTypes>(WindowType::Normal) | WindowType::Dialog;
    if (betterBlurV1Group.readEntry("BlurMenus", false)) {
        types |= WindowType::Menu;
    }
    if (betterBlurV1Group.readEntry("BlurDocks", false)) {
        types |= WindowType::Dock;
    }
    forceBlurRuleCondition->setWindowTypes(types);
    forceBlurRule->conditions().push_back(std::move(forceBlurRuleCondition));
    m_windowRules.push_back(std::move(forceBlurRule));

    auto cornerAntialiasingRule = std::make_unique<WindowRule>(-1);
    cornerAntialiasingRule->properties()->setCornerAntialiasing(betterBlurV1Group.readEntry("RoundedCornersAntialiasing", static_cast<float>(0.0)));
    m_windowRules.push_back(std::move(cornerAntialiasingRule));

    auto windowCornerRadiusRule = std::make_unique<WindowRule>(-1);
    windowCornerRadiusRule->properties()->setTopCornerRadius(betterBlurV1Group.readEntry("TopCornerRadius", static_cast<float>(0.0)));
    windowCornerRadiusRule->properties()->setBottomCornerRadius(betterBlurV1Group.readEntry("BottomCornerRadius", static_cast<float>(0.0)));
    if (!betterBlurV1Group.readEntry("RoundCornersOfMaximizedWindows", false)) {
        auto dontRoundMaximizedWindowsCondition = std::make_unique<WindowRuleCondition>();
        dontRoundMaximizedWindowsCondition->setWindowStates(WindowState::Fullscreen | WindowState::Maximized);
        dontRoundMaximizedWindowsCondition->setNegateWindowStates(true);
        windowCornerRadiusRule->conditions().push_back(std::move(dontRoundMaximizedWindowsCondition));
    }
    m_windowRules.push_back(std::move(windowCornerRadiusRule));

    // Menu corner radius
    auto roundedCornersRule3 = std::make_unique<WindowRule>(-1);
    auto radius = betterBlurV1Group.readEntry("MenuCornerRadius", static_cast<float>(0.0));
    roundedCornersRule3->properties()->setTopCornerRadius(radius);
    roundedCornersRule3->properties()->setBottomCornerRadius(radius);
    auto roundedCornersRule3Condition = std::make_unique<WindowRuleCondition>();
    roundedCornersRule3Condition->setWindowTypes(WindowType::Menu);
    roundedCornersRule3->conditions().push_back(std::move(roundedCornersRule3Condition));
    m_windowRules.push_back(std::move(roundedCornersRule3));

    // Dock corner radius
    auto roundedCornersRule4 = std::make_unique<WindowRule>(-1);
    radius = betterBlurV1Group.readEntry("DockCornerRadius", static_cast<float>(0.0));
    roundedCornersRule4->properties()->setTopCornerRadius(radius);
    roundedCornersRule4->properties()->setBottomCornerRadius(radius);
    auto roundedCornersRule4Condition = std::make_unique<WindowRuleCondition>();
    roundedCornersRule4Condition->setWindowTypes(WindowType::Dock);
    roundedCornersRule4->conditions().push_back(std::move(roundedCornersRule4Condition));
    m_windowRules.push_back(std::move(roundedCornersRule4));

    if (betterBlurV1Group.readEntry("FakeBlur", false)) {
        auto staticBlurRule = std::make_unique<WindowRule>(-1);
        staticBlurRule->properties()->setStaticBlur(true);
        if (betterBlurV1Group.readEntry("FakeBlurDisableWhenWindowBehind", true)) {
            auto condition = std::make_unique<WindowRuleCondition>();
            condition->setHasWindowBehind(false);
            staticBlurRule->conditions().push_back(std::move(condition));
        }
        m_windowRules.push_back(std::move(staticBlurRule));
    }

    if (betterBlurV1Group.readEntry("TransparentBlur", true)) {
        auto blurOpacityRule = std::make_unique<WindowRule>(-1);
        blurOpacityRule->properties()->setWindowOpacityAffectsBlur(true);
        m_windowRules.push_back(std::move(blurOpacityRule));
    }
}

}