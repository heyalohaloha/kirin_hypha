#pragma once
#include "TestTemporaryDirectory.h"
#include "TestPathBudget.h"
#include "reference_runtime_test_support.h"

namespace
{
void verifyCaptureServiceClockContract()
{
    const auto root = hypha::tests::temporaryParent().getNonexistentChildFile ("capture-service-clock", {}, false);
    require (root.createDirectory(), "capture service clock fixture exists");
    const ref::RuntimeIdentity identity { "service-clock-runtime", workId, 42 };
    constexpr std::int64_t issued = 1'000'000;
    const juce::String recording = "55555555-5555-4555-8555-555555555555";
    ref::RuntimeABindingRepository repository (root);
    require (writeJson (repository.bindingFile (identity.runtimeInstanceId),
        makeRuntimeABinding (identity, recording, issued, issued + 9'000)), "a valid fixed-time authority is published");
    const auto binding = repository.load (identity, issued);
    require (binding.has_value(), "the service test uses an actually parsed valid binding");
    {
        ref::RuntimeACapture capture (root);
        std::int64_t position = 0;
        juce::AudioBuffer<float> input (2, 8'000);
        for (int channel = 0; channel < 2; ++channel)
            for (int frame = 0; frame < input.getNumSamples(); ++frame) input.setSample (channel, frame, 0.125f);
        const auto observeCapture = [&] (std::int64_t now) {
            capture.service (binding, 8'000, 2, now);
            for (int block = 0; block < 4; ++block)
            {
                capture.observe (input, position, true, true, true);
                position += input.getNumSamples();
            }
            capture.service (binding, 8'000, 2, now);
        };
        observeCapture (issued);
        const auto first = capture.currentAudio();
        require (first && first->frameCount == 32'000 && capture.captureFile (identity.runtimeInstanceId).existsAsFile(),
                 "normal authority captures and publishes the actual queued PCM");
        capture.service (binding, 8'000, 2, issued - 1);
        require (! capture.currentAudio() && ! capture.currentReceipt()
            && ! capture.captureFile (identity.runtimeInstanceId).exists(),
            "a backwards service clock rejects the cached future-issued authority and clears its PCM and receipt");
        observeCapture (issued + 1);
        require (capture.currentAudio() && capture.currentAudio() != first,
                 "normal time recovers using new actual PCM after rejected authority");
        capture.service (binding, 8'000, 2, issued + 9'001);
        require (! capture.currentAudio() && ! capture.currentReceipt(), "expired authority clears captured PCM without extending its lease");
        observeCapture (issued - 1);
        require (! capture.currentAudio() && ! capture.currentReceipt(), "future authority cannot arm or consume a new observation");
        capture.service (binding, 8'000, 2, issued + 9'000);
        require (hypha::tests::reference_control::captureEnabledFor (capture, {}), "the exact inclusive authority expiry remains accepted");
        capture.service (std::nullopt, 8'000, 2, issued);
        require (! hypha::tests::reference_control::captureEnabledFor (capture, {}), "missing authority remains fail closed");
    }
    require (hypha::tests::removeFixtureDirectory (root), "capture service fixtures are removed after destruction");
    std::cout << "A capture service clock: current, backwards, future, expired, boundary and real PCM recovery PASS\n";
}
}
