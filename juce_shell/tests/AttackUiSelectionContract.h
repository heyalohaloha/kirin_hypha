#pragma once

#include "AttackUiLaneContract.h"

#include <iostream>

namespace hypha::attack_ui_test
{
// Values follow the hypha. While a locked hit is on the axis its values are shown; once time
// carries it out of the six seconds, no readout, one-row cell or loupe describes it any more.
inline bool verifyOffscreenLock()
{
    for (const auto& preset : observatory::sizePresets)
    {
        auto scene = presetScene (preset);
        auto fixture = laneFixture ({ 48'000, 240'000 });
        fixture.submit (*scene.component);
        scene.component->keyPressed (juce::KeyPress (juce::KeyPress::homeKey)); // LOCK 48'000
        const auto locked = renderAttack (*scene.component);
        const auto change = [&fixture] {
            auto& old = fixture.post->details[0];
            old.contrast_db += 3.0f; old.attack_rms_dbfs += 3.0f;
            old.crest_db += 3.0f; old.sharpness_acum += 0.25f;
            for (auto& point : old.shape) point *= 0.5f;
        };
        change();
        fixture.submit (*scene.component);
        if (differences (locked, renderAttack (*scene.component)) == 0)
        {
            std::cerr << "locked values not shown at " << preset.label << '\n';
            return false;
        }
        fixture.submit (*scene.component, 384'000); // the window is now 96'000..384'000
        scene.component->presentationTickAt (juce::Time::getMillisecondCounterHiRes() + 1'000.0);
        const auto scrolled = renderAttack (*scene.component);
        change();
        fixture.submit (*scene.component, 384'000);
        if (differences (scrolled, renderAttack (*scene.component)) != 0)
        {
            std::cerr << "off-screen lock still shows values at " << preset.label << '\n';
            return false;
        }
    }
    return true;
}

// HOLD is stated at every size. Bodies narrower than the header state (100% to 150%) carry
// LIVE / HOLD / LOCK in the axis row.
inline bool verifyHoldAtEverySize()
{
    for (const auto& preset : observatory::sizePresets)
    {
        auto scene = presetScene (preset);
        auto fixture = laneFixture ({ 96'000, 192'000, 240'000 });
        fixture.submit (*scene.component);
        scene.component->presentationTickAt (juce::Time::getMillisecondCounterHiRes() + 1'000.0);
        const auto live = renderAttack (*scene.component);
        scene.component->presentationTick (false);
        const auto held = renderAttack (*scene.component);
        scene.component->presentationTick (true);
        if (differences (live, held, rectangle (scene.layout.axis)) == 0)
        {
            std::cerr << "HOLD not stated at " << preset.label << '\n';
            return false;
        }
    }
    return true;
}
}
