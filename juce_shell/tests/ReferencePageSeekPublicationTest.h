#pragma once
#include "../src/reference_audition/ReferenceObservationTestHook.h"

namespace hypha::tests::page_seek
{
inline reference_control::Comparison* comparison = nullptr;
inline const void* expectedRuntime = nullptr;
inline bool entered = false;
inline void interrupt (const void* runtime)
{
    if (runtime != expectedRuntime) return;
    reference_audition::referenceObservationTestHook.store (nullptr);
    entered = true;
    comparison->servicePendingAudition (-22, -6, true, 100.1);
}
}

inline void testReferencePageSeekPublication (
    hypha::tests::reference_control::Comparison& c, hypha::tests::reference_control::Runtime& runtime,
    ref::AudioPages& pages, const juce::AudioBuffer<float>& source, int cursor, bool switching)
{
    namespace control = hypha::tests::reference_control;
    namespace seek = hypha::tests::page_seek;
    if (!switching)
    {
        c.selectA();
        require (c.requestAudition (1, -22, -6), "arm a real page wait before seeking");
    }
    c.servicePendingAudition (-22, -6, true, 100.0);
    const auto before = control::pageIntentFactsFor (c, {});
    require (before.active != 0 && c.snapshot().pendingAudition.waiting(), "seek starts with the page intent armed");
    require (control::mappedPositionFor (runtime, 0, {}) < 0, "the old content offset maps the seek outside V");
    seek::comparison = &c; seek::expectedRuntime = &runtime; seek::entered = false;
    ref::referenceObservationTestHook.store (seek::interrupt);
    c.observeTransport (0, true, true); // The hook services pending before A observes this seek.
    ref::referenceObservationTestHook.store (nullptr);
    require (seek::entered, "service interrupts the actual position publication");
    const auto middle = control::pageIntentFactsFor (c, {});
    std::cout << "PAGE_SEEK switching=" << switching << " before=" << before.active << " middle=" << middle.active
              << " stage=" << int (c.snapshot().pendingAudition.stage)
              << " reason=" << c.snapshot().pendingAudition.reasonCode << std::endl;
    require (middle.active == before.active && middle.switchingSlot == before.switchingSlot
                 && c.snapshot().pendingAudition.waiting(), "an unfinished A observation cannot consume the page intent");
    const auto activation = runtime.startB (-22, -6, runtime.normalSelectionTicket(),
                                            c.snapshot().versionSelection->playbackIdentity);
    std::cout << "PAGE_SEEK_ACTIVATE switching=" << switching << " result=" << int (activation) << std::endl;
    require (activation == ref::RuntimeV2Controller::StartResult::preparing,
             "activation cannot refuse an unfinished transport and A observation");
    juce::AudioBuffer<float> input (2, 256);
    for (int frame = 0; frame < source.getNumSamples() - 256 && !c.snapshot().bSelected; frame += 256)
    {
        for (int ch = 0; ch < 2; ++ch) input.copyFrom (ch, 0, source, ch, frame, 256);
        beginReferenceRtProbe();
        if (frame != 0) c.observeTransport (frame, true, true);
        c.observeAInput (input, frame, true, true, true);
        require (endReferenceRtProbe() == 0, "seek observation allocates nothing on the callback");
        control::service (c);
        const auto mapped = control::mappedPositionFor (runtime, frame, {});
        if (mapped >= 0) { pages.request (mapped); control::servicePages (pages, {}); }
        c.servicePendingAudition (-22, -6, true, 101.0 + frame / 48000.0);
        if (!c.snapshot().bSelected)
            require (control::pageIntentFactsFor (c, {}).active == before.active, "reconfirmation keeps the same explicit intent");
    }
    const auto recovered = c.snapshot();
    std::cout << "PAGE_SEEK_RECOVER switching=" << switching << " selected=" << recovered.bSelected
              << " mappedZero=" << control::mappedPositionFor (runtime, 0, {})
              << " stage=" << int (recovered.pendingAudition.stage) << " pages=" << int (recovered.versionSelection->auditionPages)
              << " eligible=" << recovered.versionSelection->blindEligible << std::endl;
    require (c.snapshot().bSelected, "fresh input and pages resume the same V without another press");
    require (control::pageIntentFactsFor (c, {}).active == 0, "only successful activation consumes the held intent");
    c.selectA(); seek::comparison = nullptr; seek::expectedRuntime = nullptr;
    (void) cursor;
}
