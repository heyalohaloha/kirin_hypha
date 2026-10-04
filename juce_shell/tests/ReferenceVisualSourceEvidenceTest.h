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
    // 試聴コピーのサンプルレート変換は自動。見た目は元の 48k の Cue の範囲のまま、音は出さない。
    require (wait (controller, [&] (const auto& state) {
        return state.presetId + "/" + state.checkId + "/" + state.candidateId == versionId
            && state.state == ref::RuntimeState::ready && !state.sampleRateApprovalRequired;
    }), "the audition copy is converted without asking");
    const auto state = controller.snapshot();
    const auto converted = controller.visualBinding();
    require (state.measurementAvailable && state.detailedMeasurement && !state.bSelected
        && converted.source && converted.overview && !converted.aligned && converted.hostRate == 44100
        && converted.sourceCueStartSample == 0 && converted.sourceCueEndSample == 384000
        && juce::SHA256 (file).toHexString() == hash,
        "converted visuals keep the exact 48k source Cue, never start audio and never change the source");
    controller.disconnect();
    require (wait (controller, [] (const auto& next) { return next.state == ref::RuntimeState::disconnected; })
        && !controller.visualBinding().source, "disconnect clears prepared visual evidence");
}
