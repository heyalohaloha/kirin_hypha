#pragma once
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSpectrumGeometry.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
// An opt-in desktop diagnostic, in its own process: CI's synthetic display need not emit
// physical VBlank events. The ordinary contract also exercises all motion with an injected clock.
inline void verifyFreqVBlankInteractive()
{
    juce::Component window;
    SpectrumComponent view;
    window.setSize (600, 400); window.addAndMakeVisible (view); view.setBounds (12, 70, 576, 280);
    window.addToDesktop (juce::ComponentPeer::windowIsTemporary); window.setVisible (true);
    view.setAbsoluteObservation (true); view.setSignalActive (true);
    KirinSpectrumView source {};
    source.status = KIRIN_SPECTRUM_ACTIVE; source.post_has_data = 1; source.channels = 2;
    source.sample_rate = 48000; source.aperture_samples = 4096; source.fft_size = 8192;
    source.approximate_below_hz = 35.15625f; source.min_hz = 10; source.max_hz = 22000;
    source.presentation_end_samples = 48000;
    for (auto& value : source.post_dbfs) value = -60;
    view.setSnapshot (source);
    const auto started = view.curveUpdateTimeForTest();
    int frames = 0;
    bool spectrumMoved = false, psbMoved = false, hiddenStopped = false;
    juce::VBlankAttachment witness (&window, [&]
    {
        ++frames;
        if (frames == 1)
        {
            source.presentation_end_samples += 1600;
            for (auto& value : source.post_dbfs) value = -20;
            view.queueSnapshot (source);
        }
        if (frames == 5)
        {
            spectrumMoved = view.curveUpdateTimeForTest() > started
                && view.displayedPostForTest (128) > -60 && view.displayedPostForTest (128) < -20;
            const auto bounds = view.getLocalBounds().toFloat();
            const auto point = spectrum_geometry::subviewBoundsFor (spectrum_geometry::plotBoundsFor (bounds),
                                spectrum_geometry::visualScaleFor (bounds)).getCentre();
            view.mouseDown ({ juce::Desktop::getInstance().getMainMouseSource(), point, {}, 0, 0, 0, 0, 0,
                              &view, &view, {}, point, {}, 0, false });
            KirinPsbView psb {}; psb.status = KIRIN_SPECTRUM_ACTIVE; psb.has_data = 1;
            psb.channels = 2; psb.sample_rate = 48000; psb.aperture_samples = 4800; psb.presentation_end_samples = 4800;
            for (auto& value : psb.shares) value = 0.05;
            view.setPsbSnapshot (psb);
            psb.presentation_end_samples += 4800; psb.shares[0] = 0.1; psb.shares[1] = 0;
            view.setPsbSnapshot (psb);
        }
        if (frames == 9)
        {
            psbMoved = view.displayedPsbForTest (0) > 0.05 && view.displayedPsbForTest (0) < 0.1;
            window.setVisible (false); hiddenStopped = ! view.frameDriverActiveForTest();
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }
    });
    juce::Timer::callAfterDelay (2000, [] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
    juce::MessageManager::getInstance()->runDispatchLoop();
    std::cout << "FREQ native VBlank: frames=" << frames << " spectrum=" << spectrumMoved
              << " psb=" << psbMoved << " hidden=" << hiddenStopped << '\n';
    if (! spectrumMoved || ! psbMoved || ! hiddenStopped) std::exit (1);
}
inline void verifyFreqFrameDeliveryIfRequested()
{
    if (std::getenv ("KIRIN_HYPHA_FREQ_VBLANK") == nullptr) return;
    juce::ChildProcess child;
    const auto executable = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    const bool started = child.start (juce::StringArray { executable.getFullPathName(), "--freq-vblank-only" });
    const bool finished = started && child.waitForProcessToFinish (5000);
    if (! finished) child.kill();
    std::cout << child.readAllProcessOutput();
    if (! finished || child.getExitCode() != 0) { std::cerr << "FREQ native frame delivery failed\n"; std::exit (1); }
}
}
