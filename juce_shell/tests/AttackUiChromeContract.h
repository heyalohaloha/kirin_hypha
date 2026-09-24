#pragma once

#include "AttackUiLaneContract.h"

#include <array>
#include <iostream>

namespace hypha::attack_ui_test
{
struct ChromeState
{
    int width = 580;
    int height = 248;
    presentation::Context context = presentation::forEditor (600, 400);
    bool overlay = false;
    bool paired = true;
    bool running = true;
    float dpi = 1.0f;
};

inline void applyChromeState (AttackComponent& component, const ChromeState& state)
{
    component.setPresentationContext (state.context);
    component.setSize (state.width, state.height);
    component.setOverlayMode (state.overlay);
    auto fixture = laneFixture ({ 96'000, 192'000, 240'000 });
    if (! state.paired)
    {
        fixture.pairs->status = KIRIN_SPECTRUM_NO_PAIR;
        fixture.pairs->count = 0;
    }
    fixture.stats.worker_running = state.running ? 1 : 0;
    fixture.submit (component);
}

// The cached structure follows every input it depends on. One component walks through size,
// density, VIEW, pairing, data validity and device scale; after each step both its first frame
// and its cached second frame must equal a fresh component's frame in that state.
inline bool verifyChromeCache()
{
    const auto base = ChromeState {};
    auto size = base;          size.width = 600; size.height = 260;
    auto density = size;       density.context = presentation::forEditor (450, 300);
    auto inspection = density; inspection.width = 872; inspection.height = 412;
                               inspection.context = presentation::forEditor (900, 600);
    auto overlay = inspection; overlay.overlay = true;
    auto unpaired = overlay;   unpaired.paired = false;
    auto dormant = unpaired;   dormant.running = false;
    auto awake = dormant;      awake.running = true;
    auto retina = awake;       retina.dpi = 2.0f;
    auto back = retina;        back.width = 580; back.height = 248;
                               back.context = presentation::forEditor (600, 400);
    const std::array states { base, size, density, inspection, overlay, unpaired, dormant, awake,
                              retina, back };
    auto moving = std::make_unique<AttackComponent>();
    for (std::size_t step = 0; step < states.size(); ++step)
    {
        const auto& state = states[step];
        applyChromeState (*moving, state);
        const auto first = renderAttack (*moving, state.dpi);
        const auto second = renderAttack (*moving, state.dpi);
        auto fresh = std::make_unique<AttackComponent>();
        applyChromeState (*fresh, state);
        const auto reference = renderAttack (*fresh, state.dpi);
        if (differences (first, reference) != 0 || differences (second, reference) != 0
            || moving->cachedChromeBytes() == 0)
        {
            std::cerr << "chrome cache stale at step " << step << ": first "
                      << differences (first, reference) << ", second "
                      << differences (second, reference) << '\n';
            return false;
        }
    }
    // Leaving the page, hiding the view and hiding the editor each release the image.
    moving->clearSnapshot();
    if (moving->cachedChromeBytes() != 0)
        return false;
    renderAttack (*moving);
    if (moving->cachedChromeBytes() == 0)
        return false;
    moving->setVisible (true);
    moving->setVisible (false);
    if (moving->cachedChromeBytes() != 0)
        return false;
    renderAttack (*moving);
    if (moving->cachedChromeBytes() == 0)
        return false;
    moving->releaseCachedChrome();
    return moving->cachedChromeBytes() == 0;
}
}
