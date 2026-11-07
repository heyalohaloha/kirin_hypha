#pragma once

#include <vector>

#include "HyphaShortcutRouter.h"

// The marks and the KEYS list, built from the table: "LISTEN Q", "REVEAL Shift+E", "SONG A / D".
namespace hypha::shortcuts
{
inline juce::String keyName (int key)
{
    switch (key)
    {
        case escapeCode: return "Esc";
        case tabCode:    return "Tab";
        case returnCode: return "Return";
        default:         return juce::String::charToString (static_cast<juce::juce_wchar> (key));
    }
}

inline juce::String keyText (const Binding& binding)
{
    return (binding.shift ? "Shift+" : "") + keyName (binding.key);
}

inline juce::String pairText (const Binding& first, const Binding& second)
{
    return keyText (first) + " / " + keyName (second.key);
}

inline juce::String markFor (const Binding& binding)
{
    if (binding.pairsWithNext) return binding.label + juce::String (" ") + pairText (binding, *(&binding + 1));
    return binding.label + juce::String (" ") + keyText (binding);
}

inline const Binding* bindingFor (Command command) noexcept
{
    for (const auto& binding : table::bindings)
        if (binding.command == command) return &binding;
    return nullptr;
}

// The mark of the control at or above `component`; empty when it has no key.
inline juce::String controlMark (const juce::Component& component)
{
    for (auto* owner = &component; owner != nullptr; owner = owner->getParentComponent())
    {
        if (owner->getProperties()[table::candidateKeysProperty])
            for (const auto& control : table::candidateControls)
                if (owner->getComponentID() == control.id)
                    return markFor (*bindingFor (control.command));
        for (const auto& control : table::controls)
            if (owner->getComponentID() == control.id)
                if (const auto* binding = bindingFor (control.command))
                    return control.last != nullptr ? keyText (*binding) + " - " + keyName (bindingFor (*control.last)->key)
                                                   : markFor (*binding);
    }
    return {};
}

// A hover help with its key: the key first, so a help too tall for a small window never cuts it off.
inline juce::String withMark (const juce::String& help, const juce::String& mark)
{
    if (mark.isEmpty()) return help;
    return help.isEmpty() ? mark : mark + "\n" + help;
}

struct ListRow
{
    juce::String label, keys, where;
    bool usable = true;
    bool operator== (const ListRow& other) const noexcept
    {
        return label == other.label && keys == other.keys && where == other.where && usable == other.usable;
    }
};

inline const char* scopeWord (Scope scope) noexcept
{
    switch (scope)
    {
        case Scope::blind:            return "BLIND";
        case Scope::listen:           return "LISTEN";
        case Scope::reference:        return "REF";
        case Scope::time:             return "TIME";
        case Scope::candidateCompare:
        case Scope::candidateEntry:   return "A/B";
        case Scope::keysList:         return "KEYS";
        default:                      return "ALL";
    }
}

// What works now first, then the rest. During a Blind only its anonymous controls.
inline std::vector<ListRow> listRows (const Context& context)
{
    auto shown = context;
    shown.keysListOpen = false;  // the list says what the window offers behind it
    std::vector<ListRow> now, later;
    for (auto binding = std::begin (table::bindings); binding != std::end (table::bindings); ++binding)
    {
        if (! usable (*binding, context)) continue;
        const bool active = binding->scope == Scope::focus || binding->scope == Scope::keysList
            || (scopeActive (binding->scope, shown) && (context.post || ! binding->postOnly));
        ListRow row { binding->label,
                      binding->pairsWithNext ? pairText (*binding, *(binding + 1)) : keyText (*binding),
                      scopeWord (binding->scope), active };
        if (binding->pairsWithNext) ++binding;
        if (active) now.push_back (std::move (row));
        else if (! context.blind) later.push_back (std::move (row));
    }
    now.insert (now.end(), later.begin(), later.end());
    return now;
}
}
