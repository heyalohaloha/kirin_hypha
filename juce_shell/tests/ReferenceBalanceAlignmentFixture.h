#pragma once
#include "ReferenceOverlayTest.h"
#include "../src/HyphaReferenceBalance.h"
namespace hypha::tests::balance_alignment
{
using reference_guide_contract::require;
inline reference_ui::State fixture (bool aReady)
{
    auto state = reference_overlay::page (4, 1);
    const double loudness[] {-7.5, -15.6, -17.8, -22.0};
    state.visualTimeline.reset();
    state.followA = false; state.bSelected = false; state.audibleComparisonSlot = 0;
    state.aWindowLoudness = aReady ? -18.0 : std::numeric_limits<double>::quiet_NaN();
    if (!aReady) state.aKirin.reset();
    state.targets.collection.selected.clear();
    for (size_t i = 0; i < state.songFacts.size(); ++i)
    {
        auto& fact = state.songFacts[i]; fact.lufsI = loudness[i]; fact.loudnessOffsetLu = 0;
        for (size_t b = 0; b < fact.medianDb.size(); ++b)
            fact.medianDb[b] = static_cast<float> (-28.0 + loudness[i] + 2.0 * std::sin (b * 0.15));
    }
    state.overlay.lines.clear();
    for (const auto& item : reference_ui::overlayItems (state))
        state.overlay.lines.push_back ({item.id, state.overlay.freeColour()});
    return state;
}
// Uses only established renderer/component APIs, so this same fixture runner may
// be compiled on the parent revision to obtain genuine before images.
inline void exportImages (const juce::File& output)
{
    require (output.createDirectory().wasOk(), "create isolated alignment image output");
    int count = 0;
    for (const auto language : {i18n::Language::english, i18n::Language::japanese})
    {
        const i18n::ScopedLanguage scoped (language);
        for (const int scale : {3, 6, 9}) for (const bool aReady : {false, true}) for (int mode = 0; mode < 4; ++mode)
        {
            auto state = fixture (aReady); state.overlay.all = mode != 0;
            using Drawing = reference_audition::BalanceOverlay::Drawing;
            state.overlay.drawing = mode < 2 ? Drawing::lines : mode == 2 ? Drawing::shape : Drawing::allShape;
            reference_ui::Component panel;
            reference_text_repairs::sizePanel (panel, 300 * scale, 200 * scale); panel.setState (state);
            const auto name = juce::String (language == i18n::Language::japanese ? "ja-" : "en-")
                + juce::String (scale * 100) + (aReady ? "-a-" : "-waiting-") + juce::String (mode) + ".png";
            auto stream = reference_overlay::fresh (output.getChildFile (name));
            require (stream && juce::PNGImageFormat().writeImageToStream (panel.createComponentSnapshot (panel.getLocalBounds(), true, static_cast<float> (scale) / 3.0f), *stream),
                     "write actual Balance component image");
            ++count;
        }
    }
    require (count == 48, "alignment export cannot silently run zero cases");
    std::cout << "BALANCE_ALIGNMENT_IMAGES=" << count << std::endl;
}
}
