#pragma once

// Included in the test namespace. Nominal long LOOP playback supplies the measured VST3
// continuous clock, independent of folded project time. The no-clock stall scenario keeps
// plugin-frame timing and must refuse PRE after a gap; neither mode changes shipping policy.
struct Clock final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        if (playing)
        {
            info.setTimeInSamples (position);
            info.setTimeInSeconds (static_cast<double> (position) / 48000.0);
            if (certifiedContent)
            {
                info.setKirinAuxiliaryClockSource (1);
                info.setKirinAuxiliaryClockSamples (position);
                info.setKirinPresentationLatencySource (1);
                info.setKirinOutputPresentationLatencySamples (0);
            }
            (sharedLoop != nullptr ? *sharedLoop : loop).decorate (info, position);
        }
        return info;
    }
    std::int64_t position = 0;
    bool playing = false, certifiedContent = false;
    LiveBlindLoopFixture loop;
    const LiveBlindLoopFixture* sharedLoop = nullptr;
};
