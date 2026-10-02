#pragma once

template <typename Wait>
void verifyReferenceVisualSourceEvidence (const juce::File& root, const juce::String& versionId,
    const juce::File& file, const juce::String& hash, Wait wait)
{
    ref::RuntimeV2Controller controller (root);
    controller.configure ({ "abc-visual-only", {}, 42, true }, 44100, 2);
    require (wait (controller, [&] (const auto& state) {
        return std::any_of (state.versions.begin(), state.versions.end(),
            [&] (const auto& option) { return option.id == versionId; });
    }), "selection waits for the exact published option, not an earlier library receipt");
    require (controller.selectLibraryVersion (versionId), "select prepared visual-only source");
    require (wait (controller, [&] (const auto& state) {
        return state.presetId + "/" + state.checkId + "/" + state.candidateId == versionId
            && state.sampleRateApprovalRequired;
    }), "rate conversion remains an explicit audition permission");
    const auto state = controller.snapshot();
    const auto display = controller.visualBinding();
    require (state.measurementAvailable && state.detailedMeasurement && !state.bSelected
        && display.source && display.overview && !display.aligned
        && display.sourceCueStartSample == 0 && display.sourceCueEndSample == 384000
        && display.hostRate == 44100 && !controller.selectB (-14, -2)
        && juce::SHA256 (file).toHexString() == hash,
        "pre-SRC visuals retain the exact 48k source Cue without resampling, playback or source mutation");
    require (controller.approveSampleRateConversion(), "explicitly approve the audition copy");
    require (wait (controller, [] (const auto& next) {
        return next.state == ref::RuntimeState::ready && !next.sampleRateApprovalRequired;
    }), "approved copy becomes ready");
    const auto converted = controller.visualBinding();
    require (converted.source && converted.hostRate == 44100
        && converted.sourceCueStartSample == display.sourceCueStartSample
        && converted.sourceCueEndSample == display.sourceCueEndSample
        && !controller.snapshot().bSelected && juce::SHA256 (file).toHexString() == hash,
        "SRC approval keeps the same full source-range visuals and never starts audio");
    controller.disconnect();
    require (wait (controller, [] (const auto& next) { return next.state == ref::RuntimeState::disconnected; })
        && !controller.visualBinding().source, "disconnect clears prepared visual evidence");
}
