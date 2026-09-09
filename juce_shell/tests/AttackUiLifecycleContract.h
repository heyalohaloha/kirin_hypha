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
        if (specimenDifferences (original, updated) < 5)
            { std::cerr << "detail lifecycle: unchanged size " << size.x << 'x' << size.y << '\n'; return false; }
    }
    component->setSize (580, 228);
    snapshot (details);
    const auto complete = paint();
    snapshot (*missing);
    if (specimenDifferences (complete, paint()) < 10)
        { std::cerr << "detail lifecycle: missing detail\n"; return false; }
    snapshot (details); // Late detail at an unchanged raw endpoint must appear.
    if (specimenDifferences (complete, paint()) != 0)
        { std::cerr << "detail lifecycle: late detail mismatch\n"; return false; }
    component->presentationTick (false);
    const auto inactive = paint();
    const auto retainedHeight = attack_ui::metricsHeight (inactive.getHeight());
    const juce::Rectangle<int> retainedSpecimen {
        0, inactive.getHeight() - retainedHeight, inactive.getWidth(), retainedHeight };
    if (specimenDifferences (complete, inactive, retainedSpecimen) != 0
        || specimenDifferences (complete, inactive) == 0)
        { std::cerr << "detail lifecycle: hold changed specimen\n"; return false; }
    component->keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
    if (specimenDifferences (inactive, paint()) < 10)
        { std::cerr << "detail lifecycle: lock did not change\n"; return false; }
    component->presentationTick (true);
    component->keyPressed (juce::KeyPress (juce::KeyPress::endKey));
    snapshot (*missing);
    const auto awaiting = paint();
    for (std::uint32_t i = 0; i < changed->count; ++i) changed->details[i].generation = 6;
    snapshot (*changed);
    const auto stable = specimenDifferences (awaiting, paint()) == 0;
    if (! stable) std::cerr << "detail lifecycle: stale generation changed frame\n";
    return stable; // Reused sample is not reused identity.
}
}
