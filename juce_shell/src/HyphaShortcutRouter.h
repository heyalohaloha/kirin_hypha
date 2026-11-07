#pragma once

#include <algorithm>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaShortcutBindings.h"

// From a key press to the one thing it means now. No processor and no component here: the editor says
// what is running (Context) and does the command (PluginEditorShortcuts.cpp).
namespace hypha::shortcuts
{
struct Context
{
    bool post = false, keysListOpen = false, blind = false, listen = false;
    bool reference = false, time = false;
    // Set both from the editor when A/B lands; until then its keys belong to the host.
    bool candidateCompareAvailable = false, candidateCompareActive = false;

    bool operator== (const Context& other) const noexcept
    {
        return post == other.post && keysListOpen == other.keysListOpen && blind == other.blind
            && listen == other.listen && reference == other.reference && time == other.time
            && candidateCompareAvailable == other.candidateCompareAvailable
            && candidateCompareActive == other.candidateCompareActive;
    }
    bool operator!= (const Context& other) const noexcept { return ! (*this == other); }
};

struct Press
{
    int key = 0;  // 0: never a shortcut (Cmd, Ctrl or Alt held)
    bool shift = false;
};

// macOS names Shift + the top row by what the layout prints, not by the digit: `!` is Shift+1 on
// every layout, and Shift+2 is `@` in US and `"` in JIS. Windows already gives the digit.
inline int digitUnderShift (int code) noexcept
{
    switch (code)
    {
        case '!':                           return '1';
        case '@': case '"':                 return '2';
        case '#': case 0xa3: case 0xa7:     return '3';  // £ and § on UK and German layouts
        case '$':                           return '4';
        case '%':                           return '5';
        default:                            return code;
    }
}

inline Press normalise (const juce::KeyPress& key) noexcept
{
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown()) return {};
    if (key.isKeyCode (juce::KeyPress::escapeKey)) return { escapeCode, mods.isShiftDown() };
    auto code = key.getKeyCode();
    if (code >= 'a' && code <= 'z') code += 'A' - 'a';
    return { mods.isShiftDown() ? digitUnderShift (code) : code, mods.isShiftDown() };
}

constexpr bool scopeActive (Scope scope, const Context& context) noexcept
{
    if (context.keysListOpen) return scope == Scope::keysList || scope == Scope::always;
    if (context.blind) return scope == Scope::blind || scope == Scope::always;
    switch (scope)
    {
        case Scope::candidateCompare: return context.candidateCompareAvailable && context.candidateCompareActive;
        case Scope::candidateEntry:   return context.candidateCompareAvailable;
        case Scope::listen:           return context.listen;
        case Scope::reference:        return context.reference;
        case Scope::time:             return context.time;
        case Scope::everywhere:
        case Scope::always:           return true;
        case Scope::keysList:
        case Scope::blind:
        case Scope::focus:            break;
    }
    return false;
}

inline constexpr Scope scopeOrder[] { Scope::keysList, Scope::blind, Scope::candidateCompare, Scope::listen,
                                      Scope::reference, Scope::time, Scope::candidateEntry,
                                      Scope::everywhere, Scope::always };

struct Resolution
{
    bool assigned = false;              // false: the key goes back to the host
    const Binding* binding = nullptr;   // the command, when it can run now
    const char* refusal = "";           // why not, in a few words (English; empty says nothing)
};

// A/B's rows exist before A/B does: until then they are as if unwritten.
constexpr bool usable (const Binding& binding, const Context& context) noexcept
{
    return context.candidateCompareAvailable
        || (binding.scope != Scope::candidateCompare && binding.scope != Scope::candidateEntry);
}

constexpr const char* refusalFor (const Binding& binding, const Context& context) noexcept
{
    if (context.keysListOpen) return "";
    if (context.blind) return "End Blind Compare first";
    if (binding.scope == Scope::blind) return "BLIND is not active";
    if (binding.postOnly && ! context.post) return "Available in POST";
    switch (binding.scope)
    {
        case Scope::listen:   return "LISTEN is not active";
        case Scope::reference: return "Available in REF";
        case Scope::time:     return "Available in TIME";
        default:              return "";
    }
}

inline Resolution resolve (Press press, const Context& context) noexcept
{
    const auto matches = [press] (const Binding& binding)
    { return binding.key == press.key && binding.shift == press.shift && binding.scope != Scope::focus; };
    for (auto scope : scopeOrder)
        if (scopeActive (scope, context))
            for (const auto& binding : table::bindings)
                if (binding.scope == scope && matches (binding) && usable (binding, context))
                {
                    if (binding.postOnly && ! context.post)
                        return { true, nullptr, "Available in POST" };
                    return { true, &binding };
                }
    // Assigned, but not now: it says why and goes no further. Esc belongs to the host unless the list
    // is open.
    for (const auto& binding : table::bindings)
        if (matches (binding) && usable (binding, context) && binding.scope != Scope::keysList)
            return { true, nullptr, refusalFor (binding, context) };
    return {};
}

// A held key repeats. One press is one action until that key is up, whatever the modifiers do on the
// way (Q held, then Shift, is still that Q). `raw` is the code the press arrived with: macOS tracks
// Shift+1 as `!`, and as `1` once Shift is up.
class HeldKeys
{
public:
    // True for a new press, false for a repeat of a held key.
    bool press (int key, int raw)
    {
        if (std::any_of (held.begin(), held.end(), [key] (const Held& entry) { return entry.key == key; }))
            return false;
        held.push_back ({ key, raw });
        return true;
    }
    void released()
    {
        held.erase (std::remove_if (held.begin(), held.end(), [] (const Held& entry)
        {
            return ! juce::KeyPress::isKeyCurrentlyDown (entry.key)
                && ! juce::KeyPress::isKeyCurrentlyDown (entry.raw);
        }), held.end());
    }
    void clear() noexcept { held.clear(); }
    bool empty() const noexcept { return held.empty(); }

private:
    struct Held { int key, raw; };
    std::vector<Held> held;
};
}
