#pragma once

// The Reference page can show VERSION BLIND alongside the live comparison rail.
inline void verifyReferenceLiveBlindFooter (hypha::observatory::View& view, juce::Button& blind)
{
    using namespace hypha;
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (const auto preset : observatory::sizePresets)
        {
            view.setSize (preset.width, preset.height);
            observatory::LiveCompareFooter state;
            state.active = state.matched = state.blindAvailable = true;
            view.setLiveCompareFooter (state);
            view.setDomain (observatory::Domain::reference);
            if (blind.getButtonText() != "LIVE BLIND")
            {
                std::cerr << "Reference footer must identify LIVE BLIND\n";
                std::exit (EXIT_FAILURE);
            }
            if (preset.width >= 600 && ! blind.isVisible())
            {
                std::cerr << "Reference LIVE BLIND must remain reachable at large sizes\n";
                std::exit (EXIT_FAILURE);
            }
            if (blind.isVisible() && ! readable (view, blind))
            {
                std::cerr << "Reference LIVE BLIND label must fit\n";
                std::exit (EXIT_FAILURE);
            }
            view.setDomain (observatory::Domain::level);
            if (blind.getButtonText() != "BLIND")
            {
                std::cerr << "Measurement pages retain BLIND\n";
                std::exit (EXIT_FAILURE);
            }
        }
    }
    view.setLiveCompareFooter ({});
}
