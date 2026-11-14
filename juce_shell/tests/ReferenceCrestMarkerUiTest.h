#pragma once

namespace hypha::tests
{
template <typename Panel, typename State, typename Measurement, typename Difference>
void verifyCrestMarkerUi (Panel& panel, State& state, Measurement& measurement, Difference differentPixels)
{
    using reference_guide_contract::require;
    state.viewBindings = { "dynamics" };
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage shown (language);
        measurement->dynamics->samplePeakCrest = false;
        panel.setState (state);
        const auto oldImage = panel.createComponentSnapshot (panel.getLocalBounds());
        measurement->dynamics->samplePeakCrest = true;
        panel.setState (state);
        require (differentPixels (oldImage, panel.createComponentSnapshot (panel.getLocalBounds())) > 0,
                 "the old crest marker is drawn in both languages");
    }
    require (i18n::translate ("CREST (OLD)", i18n::Language::japanese) == juce::String::fromUTF8 (u8"CREST（旧式）"),
             "the old crest marker has the requested Japanese text");
    measurement->dynamics->samplePeakCrest = false;
}
}
