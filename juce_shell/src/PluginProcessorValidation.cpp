#include "PluginProcessor.h"
#if JUCE_DEBUG
#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <cmath>

bool KirinHyphaProcessorBase::startLocalBlindPdcValidation()
{
    if (! isPostRole())
    {
        localBlindPdcValidationIssue.store (2, std::memory_order_release);
        return false;
    }
    hypha::local_blind::ExactPairBinding pair;
    if (! localBlindPairBinding (pair))
    {
        localBlindPdcValidationIssue.store (3, std::memory_order_release);
        return false;
    }
    hypha::local_blind::HostClockProbeSnapshot clock;
    if (! hostClockProbe.read (clock) || ! std::isfinite (clock.rate)
        || clock.rate < 8'000.0 || clock.rate > 768'000.0)
    {
        localBlindPdcValidationIssue.store (4, std::memory_order_release);
        return false;
    }
    if (localBlindCapture.view().phase != hypha::local_blind::CaptureOwnerPhase::idle)
    {
        localBlindCapture.requestReset();
        localBlindPdcValidationIssue.store (5, std::memory_order_release);
        return false;
    }
    const auto serial = localBlindPdcValidationSerial.fetch_add (1, std::memory_order_acq_rel) + 1;
    const auto now = static_cast<std::uint64_t> (juce::Time::currentTimeMillis());
    const auto generation = (now << 16u) | (serial & 0xffffu);
    const auto frames = static_cast<std::int64_t> (std::llround (clock.rate)) * 4;
    hypha::local_blind::ExactCaptureRequest request;
    if (generation == 0 || ! issueLocalBlindCaptureRequest (generation, frames, request))
    {
        localBlindPdcValidationIssue.store (6, std::memory_order_release);
        return false;
    }
    localBlindPdcValidationIssue.store (1, std::memory_order_release);
    return true;
}

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
    if (! host.hasActiveIdentity())
        lines.add ("Unavailable at: " + juce::String (hypha::local_blind::hostContextReadStageName (host.failedAt)));
    lines.add ("Context provider API: "
               + juce::String (hypha::local_blind::hostContextProviderApiName (host.providerApi)));
    lines.add ("Document hash: " + fingerprint (host.document));
    lines.add ("Channel hash: " + fingerprint (host.channel));
    lines.add ("Host revision: " + number (host.revision));
    lines.add ("Host hooks: component " + number (host.componentHandlerSets)
               + " / application " + number (host.hostApplicationSets));
    lines.add ("Host queries: all " + number (host.editControllerQueries)
               + " / context " + number (host.handlerInterfaceQueries));
    lines.add ("Host context notifications: " + number (host.notifications));
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
        const char* validationIssues[] {
            "not requested", "scheduled", "POST only", "exact PRE pair unavailable",
            "live clock unavailable", "previous attempt reset; retry", "request rejected"
        };
        const char* capturePhases[] {
            "idle", "awaiting PRE", "capturing", "complete", "retired", "paired", "failed"
        };
        const char* captureFailures[] {
            "none", "invalid request", "expired", "PRE rejected", "pair changed",
            "arm rejected", "capture failed", "receipt rejected"
        };
        const char* laneFailures[] {
            "none", "discontinuity", "format", "generation", "non-realtime",
            "non-finite", "transport", "clock"
        };
        const auto issue = std::clamp (localBlindPdcValidationIssue.load (std::memory_order_acquire), 0, 6);
        const auto capture = localBlindCapture.view();
        const auto phase = std::clamp (static_cast<int> (capture.phase), 0, 6);
        const auto failure = std::clamp (static_cast<int> (capture.failure), 0, 7);
        const auto laneFailure = std::clamp (static_cast<int> (capture.captureFailure), 0, 7);
        lines.add ("PDC validation issue: " + juce::String (validationIssues[issue]));
        lines.add ("PDC capture: "
            + juce::String (capturePhases[phase]) + " / "
            + captureFailures[failure] + " / lane " + laneFailures[laneFailure]);
        lines.add ("PDC callbacks: all " + number (localBlindCapture.debugProcessCount())
            + " / owned " + number (localBlindCapture.debugOwnedCount())
            + " / last " + number (localBlindCapture.debugLastPosition()));
        const auto comparison = localBlindCapture.capturePairComparison();
        if (comparison.valid)
        {
            lines.add ("PDC exact zero: " + juce::String (comparison.exactAtZero ? "yes" : "no"));
            auto residual = juce::String ("not estimable");
            if (comparison.lagEstimated)
            {
                residual = number (comparison.bestLagFrames) + " samples";
                if (comparison.bestLagFrames > 0) residual += " (POST later)";
                if (comparison.bestLagFrames < 0) residual += " (POST earlier)";
            }
            lines.add ("PDC residual: " + residual);
            lines.add ("PDC correlation zero / best: "
                + juce::String (comparison.zeroLagCorrelation, 8) + " / "
                + juce::String (comparison.bestCorrelation, 8));
            lines.add ("PDC correlation margin: "
                + juce::String (comparison.correlationMargin, 8));
            lines.add ("PDC normalized zero RMS error: "
                + juce::String (comparison.normalizedZeroLagRmsError, 8));
            lines.add ("PDC range: " + number (comparison.start) + " + "
                + number (comparison.frames) + " / " + number (comparison.sampleRate) + " Hz / "
                + number (comparison.channels) + " ch");
        }
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
    else
    {
        const char* capturePhases[] {
            "idle", "awaiting PRE", "capturing", "complete", "retired", "paired", "failed"
        };
        const char* captureFailures[] {
            "none", "invalid request", "expired", "PRE rejected", "pair changed",
            "arm rejected", "capture failed", "receipt rejected"
        };
        const char* laneFailures[] {
            "none", "discontinuity", "format", "generation", "non-realtime",
            "non-finite", "transport", "clock"
        };
        const auto capture = localBlindCapture.view();
        const auto phase = std::clamp (static_cast<int> (capture.phase), 0, 6);
        const auto failure = std::clamp (static_cast<int> (capture.failure), 0, 7);
        const auto laneFailure = std::clamp (static_cast<int> (capture.captureFailure), 0, 7);
        lines.add ("PRE capture: " + juce::String (capturePhases[phase])
            + " / " + captureFailures[failure] + " / lane " + laneFailures[laneFailure]);
        lines.add ("PRE callbacks: all " + number (localBlindCapture.debugProcessCount())
            + " / owned " + number (localBlindCapture.debugOwnedCount())
            + " / last " + number (localBlindCapture.debugLastPosition()));
    }
    lines.add ("Observations only; Blind admission not qualified");
    return lines;
}
#endif
