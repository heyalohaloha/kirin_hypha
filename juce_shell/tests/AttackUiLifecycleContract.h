#pragma once

#include "AttackUiImageHelpers.h"
#include "../src/HyphaObservatoryContract.h"

#include <iostream>
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
    const auto snapshot = [&] (const KirinAttackDetailBatch& post)
    {
        component->setSnapshot (events, waveform, post, waveform, details, pairs,
                                288'000, 48'000, 7, stats);
    };
    // Every editor body shows a changed selected-hit value, including the 100% one-row body.
    for (const auto& preset : observatory::sizePresets)
    {
        const auto shell = observatory::shellLayout (observatory::Role::post, preset,
                                                     observatory::GuidePresence::absent);
        component->setPresentationContext (presentation::forEditor (preset.width, preset.height));
        component->setSize (shell.body.width,
                            shell.body.height - observatory::timeNavigationHeight (preset.density));
        snapshot (details);
        const auto original = renderAttack (*component);
        snapshot (*changed);
        if (differences (original, renderAttack (*component)) < 5)
            { std::cerr << "detail lifecycle: unchanged " << preset.label << '\n'; return false; }
    }
    const auto context = presentation::forEditor (600, 400);
    component->setPresentationContext (context);
    component->setSize (580, 248);
    const auto layout = attack_ui::layoutFor (580, 248, context);
    snapshot (details);
    const auto complete = renderAttack (*component);
    snapshot (*missing);
    if (differences (complete, renderAttack (*component)) < 10)
        { std::cerr << "detail lifecycle: missing detail\n"; return false; }
    snapshot (details); // Late detail at an unchanged raw endpoint must appear.
    if (differences (complete, renderAttack (*component)) != 0)
        { std::cerr << "detail lifecycle: late detail mismatch\n"; return false; }
    component->presentationTick (false);
    const auto inactive = renderAttack (*component);
    if (differences (complete, inactive, lanesArea (layout)) != 0
        || differences (complete, inactive) == 0)
        { std::cerr << "detail lifecycle: hold changed lanes\n"; return false; }
    component->keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
    if (differences (inactive, renderAttack (*component)) < 10)
        { std::cerr << "detail lifecycle: lock did not change\n"; return false; }
    component->presentationTick (true);
    component->keyPressed (juce::KeyPress (juce::KeyPress::endKey));
    snapshot (*missing);
    const auto awaiting = renderAttack (*component);
    for (std::uint32_t i = 0; i < changed->count; ++i) changed->details[i].generation = 6;
    snapshot (*changed);
    const auto stable = differences (awaiting, renderAttack (*component)) == 0;
    if (! stable) std::cerr << "detail lifecycle: stale generation changed frame\n";
    return stable; // Reused sample is not reused identity.
}
}
