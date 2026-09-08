#include "PluginProcessor.h"
#include "HyphaClockSourceContract.h"
#include <limits>

static_assert (std::atomic<bool>::is_always_lock_free
               && std::atomic<std::uint64_t>::is_always_lock_free,
               "Audio Thread host-state notifications must remain lock-free");

hypha::HostProcessClock KirinHyphaProcessorBase::readHostProcessClock() const
{
    bool playing = false;
    bool recording = false;
    bool hasPosition = false;
    uint8_t clockSource = KIRIN_HYPHA_CLOCK_UNKNOWN;
    int64_t positionSamples = 0;
    bool hasClockEnd = false;
    int64_t clockStartSamples = 0;
    int64_t clockEndSamples = 0;
    uint8_t presentationSource = KIRIN_HYPHA_PRESENTATION_SOURCE_UNKNOWN;
    bool inputPresentationValid = false;
    uint32_t inputPresentationSamples = 0;
    bool outputPresentationValid = false;
    uint32_t outputPresentationSamples = 0;
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            recording = pos->getIsRecording();
            if (const auto timeSamples = pos->getTimeInSamples())
            {
                hasPosition = true;
                positionSamples = *timeSamples;
                clockSource = KIRIN_HYPHA_CLOCK_PROJECT_TIMELINE;
               #if KIRIN_HYPHA_AU_CLOCK_PROVENANCE
                // Processor.cpp is shared by the AU and VST3 products. JucePlugin_Build_AU is
                // therefore true in this translation unit even for a VST3 instance and cannot
                // identify the active wrapper. Read the AU-only provenance marker only for an
                // actual Audio Unit v2 instance; VST3 always keeps the host project timeline.
                if (wrapperType == juce::AudioProcessor::wrapperType_AudioUnit
                    && hypha::clock_source_contract::audioUnitV2UsesRenderTimeline (
                        pos->getKirinAuUsesHostTransportTimeline()))
                    clockSource = KIRIN_HYPHA_CLOCK_AUDIO_RENDER_TIMELINE;
               #endif
            }
           #if KIRIN_HYPHA_PRESENTATION_CLOCK
            const auto wrapperSource = pos->getKirinPresentationLatencySource();
            if (wrapperSource == KIRIN_HYPHA_PRESENTATION_SOURCE_VST3
                || wrapperSource == KIRIN_HYPHA_PRESENTATION_SOURCE_AUDIO_UNIT_V2)
                presentationSource = (uint8_t) wrapperSource;
            const auto readPresentationLatency = [] (const auto& value, bool& valid, uint32_t& samples)
            {
                if (value.hasValue() && *value >= 0
                    && *value <= (int64_t) std::numeric_limits<uint32_t>::max())
                {
                    valid = true;
                    samples = (uint32_t) *value;
                }
            };
            readPresentationLatency (pos->getKirinInputPresentationLatencySamples(),
                                     inputPresentationValid, inputPresentationSamples);
            readPresentationLatency (pos->getKirinOutputPresentationLatencySamples(),
                                     outputPresentationValid, outputPresentationSamples);
           #endif
        }
    lastHostRecording.store (recording, std::memory_order_release);
    hostProcessHeartbeat.fetch_add (1u, std::memory_order_release);
    // JUCE exposes loop points in PPQ, not the exact exported WAV sample range. Do not
    // promote those values to wav_clock_native; render span remains a lower-trust fallback
    // until a host-supplied native sample range exists.
    const hypha::HostProcessClock clock { playing, hasPosition, clockSource, positionSamples, hasClockEnd,
             clockStartSamples, clockEndSamples, presentationSource,
             inputPresentationValid, inputPresentationSamples,
             outputPresentationValid, outputPresentationSamples };
    // Release PRE never issues a capture request. Keep its Audio Thread free of an otherwise
    // unused snapshot write while retaining both-role clock diagnostics in Debug validation.
   #if JUCE_DEBUG
    const bool publishClockProbe = true;
   #else
    const bool publishClockProbe = role == Role::Post;
   #endif
    if (publishClockProbe)
        hostClockProbe.publish (clock, preparedSampleRate,
            static_cast<std::uint32_t> (getBlockSize()),
            static_cast<std::uint32_t> (getTotalNumInputChannels()));
    return clock;
}
