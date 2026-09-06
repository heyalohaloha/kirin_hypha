#pragma once

#include <string_view>

namespace hypha::update_information
{
enum class Action : int { none = 0, downloadsEnglish = 21, downloadsJapanese,
                         changes, copyEnglish, copyJapanese, copyChanges };
enum class Outcome { ignored, blocked, opened, openFailed, copied };

constexpr std::string_view url (Action action) noexcept
{
    switch (action)
    {
        case Action::downloadsEnglish:
        case Action::copyEnglish: return "https://kirinmastering.com/hypha";
        case Action::downloadsJapanese:
        case Action::copyJapanese: return "https://kirinmastering.com/ja/hypha";
        case Action::changes:
        case Action::copyChanges: return "https://github.com/heyalohaloha/kirin_hypha/releases";
        case Action::none: return {};
    }
    return {};
}

constexpr bool copies (Action action) noexcept
{
    return action == Action::copyEnglish || action == Action::copyJapanese
        || action == Action::copyChanges;
}

constexpr Action copyAction (Action action) noexcept
{
    return action == Action::downloadsEnglish ? Action::copyEnglish
         : action == Action::downloadsJapanese ? Action::copyJapanese
         : action == Action::changes ? Action::copyChanges : Action::none;
}

// Recheck at dispatch, not just when the menu was constructed. No input can supply a URL.
template <typename Launch, typename Copy>
Outcome dispatch (Action action, bool blindBusy, Launch launch, Copy copy)
{
    const auto destination = url (action);
    if (destination.empty()) return Outcome::ignored;
    if (blindBusy) return Outcome::blocked;
    if (copies (action)) { copy (destination); return Outcome::copied; }
    return launch (destination) ? Outcome::opened : Outcome::openFailed;
}
}
