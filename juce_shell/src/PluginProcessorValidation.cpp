#include "PluginProcessor.h"
#if JUCE_DEBUG
#include <juce_cryptography/juce_cryptography.h>

juce::StringArray KirinHyphaProcessorBase::localValidationFacts() const
{
    juce::StringArray lines;
    const auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    if (manager == nullptr || ! manager->isThisTheMessageThread()) return lines;
    const auto host = localBlindHostFacts();
    const auto number = [] (auto value) { return juce::String (static_cast<juce::int64> (value)); };
    const auto fingerprint = [] (const std::u16string& value)
    {
        // Diagnostic prefix only. Never use this truncated fingerprint as identity authority.
        if (value.empty()) return juce::String ("absent");
        std::vector<std::uint8_t> bytes (value.size() * 2);
        for (std::size_t i = 0; i < value.size(); ++i)
        {
            bytes[i * 2] = static_cast<std::uint8_t> (value[i]);
            bytes[i * 2 + 1] = static_cast<std::uint8_t> (value[i] >> 8u);
        }
        return juce::SHA256 (bytes.data(), bytes.size()).toHexString().substring (0, 12);
    };
    const char* issues[] { "observed", "unavailable", "malformed", "inactive document", "changed during read" };
    lines.add ("Host identity: " + juce::String (issues[static_cast<int> (host.issue)]));
    lines.add ("Document hash: " + fingerprint (host.document));
    lines.add ("Channel hash: " + fingerprint (host.channel));
    lines.add ("Host revision: " + number (host.revision));
    hypha::local_blind::HostClockProbeSnapshot clock;
    if (hostClockProbe.read (clock))
    {
        lines.add ("Last callback: " + number (clock.callback) + " / " + (clock.playing ? "playing" : "stopped"));
        lines.add (number (clock.channels) + " ch / " + juce::String (clock.rate, 0)
                   + " Hz / nominal block " + number (clock.frames));
        lines.add ("Position: " + (clock.hasPosition ? number (clock.position) : "absent")
                   + " / source " + number (clock.source));
        const auto latency = [&] (bool present, std::uint32_t value)
        { return ! present ? juce::String ("not reported") : value == 0 ? juce::String ("0 (ambiguous)") : number (value); };
        lines.add ("Input presentation: " + latency (clock.hasInputLatency, clock.inputLatency));
        lines.add ("Output presentation: " + latency (clock.hasOutputLatency, clock.outputLatency));
    }
    else lines.add ("Last callback: unavailable / concurrent read");
    if (isPostRole())
    {
        lines.add ("Requested PSB: " + number (psbAnalysisRequested.load())
            + " / perceptual: " + number (perceptualAnalysisRequested.load())
            + " / absolute: " + number (absoluteAnalysisRequested.load()));
        KirinPsbView psb {};
        if (pollPsb (psb))
        {
            lines.add ("PSB status: " + number (psb.status) + " / data: " + number (psb.has_data)
                       + " / delta: " + number (psb.is_delta));
            lines.add ("PSB epoch: " + number (psb.state_epoch_samples)
                       + " / end: " + number (psb.presentation_end_samples));
        }
    }
    lines.add ("Observations only; Blind admission not qualified");
    return lines;
}
#endif
