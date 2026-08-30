#include "window_rule_effects.h"

#include <array>

#include <hyprland/src/desktop/rule/windowRule/WindowRuleApplicator.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleEffectContainer.hpp>
#include <hyprland/src/desktop/view/Window.hpp>

namespace {

using EffectId = Desktop::Rule::CWindowRuleEffectContainer::storageType;

constexpr std::array<const char*, 3> EFFECT_NAMES = {
    "plugin:scroller:modemodifier",
    "plugin:scroller:columnwidth",
    "plugin:scroller:windowheight",
};

std::array<std::optional<EffectId>, EFFECT_NAMES.size()> effect_ids;

std::optional<std::string> value_for(PHLWINDOW window, const std::optional<EffectId>& id)
{
    if (!window || !window->m_ruleApplicator || !id)
        return std::nullopt;

    const auto& props = window->m_ruleApplicator->m_otherProps.props;
    const auto it = props.find(*id);
    if (it == props.end() || !it->second)
        return std::nullopt;

    return it->second->effect;
}

} // namespace

namespace window_rule_effects {

bool register_effects()
{
    auto effects = Desktop::Rule::windowEffects();
    if (!effects)
        return false;

    // registerEffect returns an existing ID on collision, so preflight every
    // name before mutating the shared container.
    for (const auto name : EFFECT_NAMES) {
        if (effects->get(name).has_value())
            return false;
    }

    std::array<std::optional<EffectId>, EFFECT_NAMES.size()> registered;
    for (size_t i = 0; i < EFFECT_NAMES.size(); ++i) {
        const auto id = effects->registerEffect(std::string{EFFECT_NAMES[i]});
        if (!effects->isEffectDynamic(id) || effects->get(id) != EFFECT_NAMES[i]) {
            for (size_t j = i; j > 0; --j)
                effects->unregisterEffect(*registered[j - 1]);
            return false;
        }
        registered[i] = id;
    }

    effect_ids = registered;
    return true;
}

void unregister_effects()
{
    auto effects = Desktop::Rule::windowEffects();
    if (effects) {
        for (size_t i = effect_ids.size(); i > 0; --i) {
            if (effect_ids[i - 1])
                effects->unregisterEffect(*effect_ids[i - 1]);
        }
    }
    effect_ids = {};
}

std::optional<std::string> modemodifier(PHLWINDOW window)
{
    return value_for(window, effect_ids[0]);
}

std::optional<std::string> columnwidth(PHLWINDOW window)
{
    return value_for(window, effect_ids[1]);
}

std::optional<std::string> windowheight(PHLWINDOW window)
{
    return value_for(window, effect_ids[2]);
}

} // namespace window_rule_effects
