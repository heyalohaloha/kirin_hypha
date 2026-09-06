#pragma once

#include <memory>

namespace hypha::attack_ui_test
{
inline bool verifyDetailLifecycle (const KirinAttackEventBatch& events,
                                   const KirinAttackWaveformBatch& waveform,
                                   const KirinAttackDetailBatch& details,
                                   const KirinAttackPairEventBatch& pairs,
                                   const KirinAttackStats& stats)
{
    auto component = std::make_unique<AttackComponent>();
    auto missing = std::make_unique<KirinAttackDetailBatch>();
    auto changed = std::make_unique<KirinAttackDetailBatch> (details);
    for (std::uint32_t i = 0; i < changed->count; ++i)
    {
        changed->details[i].attack_rms_dbfs += 3.0f;
        changed->details[i].sharpness_acum += 0.5f;
    }
    const auto paint = [&]
    {
        juce::Image image (juce::Image::ARGB, component->getWidth(), component->getHeight(), true);
        juce::Graphics graphics (image);
        component->paintEntireComponent (graphics, true);
        return image;
    };
    const auto snapshot = [&] (const KirinAttackDetailBatch& post)
    {
        component->setSnapshot (events, waveform, post, waveform, details, pairs,
                                288'000, 48'000, 7, stats);
    };
    // These are shell body sizes, including the 100% editor with its Guide rail visible.
    for (const auto size : { juce::Point<int> { 292, 52 }, { 365, 94 }, { 434, 124 },
                             { 580, 228 }, { 872, 386 } })
    {
        component->setSize (size.x, size.y);
        snapshot (details);
        const auto original = paint();
        snapshot (*changed);
        const auto updated = paint();
        if (specimenDifferences (original, updated) < 5) return false;
    }
    component->setSize (580, 228);
    snapshot (details);
    const auto complete = paint();
    snapshot (*missing);
    if (specimenDifferences (complete, paint()) < 10) return false;
    snapshot (details); // Late detail at an unchanged raw endpoint must appear.
    if (specimenDifferences (complete, paint()) != 0) return false;
    component->presentationTick (false);
    auto inactive = paint();
    for (int y = attack_ui::headerHeight; y < inactive.getHeight(); ++y)
        for (int x = 0; x < inactive.getWidth(); ++x)
            if (inactive.getPixelAt (x, y) != juce::Colours::black) return false;
    component->keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
    if (specimenDifferences (inactive, paint()) < 10) return false; // Explicit lock survives silence.
    component->presentationTick (true);
    component->keyPressed (juce::KeyPress (juce::KeyPress::endKey));
    snapshot (*missing);
    const auto awaiting = paint();
    for (std::uint32_t i = 0; i < changed->count; ++i) changed->details[i].generation = 6;
    snapshot (*changed);
    return specimenDifferences (awaiting, paint()) == 0; // Reused sample is not reused identity.
}
}
