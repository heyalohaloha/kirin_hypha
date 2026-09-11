#include "../src/local_blind/LocalBlindProductSession.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace hypha::local_blind;
static void require (bool ok, const char* message)
{ if (! ok) { std::cerr << message << '\n'; std::abort(); } }

// A deliberately narrow reader for the checked-in S-1 fixture, not a product audio decoder.
// Require the actual 48 kHz, stereo, extensible IEEE-float PCM instead of generating a substitute.
// DAWs may append valid RIFF metadata after the data chunk, so the container's total byte count is
// not part of the qualified audio fixture.
static std::vector<float> readFixture (const char* path)
{
    std::ifstream input (path, std::ios::binary);
    require (input.good(), "S-1 fixture is required");
    const std::vector<unsigned char> bytes ((std::istreambuf_iterator<char> (input)), {});
    require (bytes.size() >= 68, "S-1 RIFF header is truncated");
    const auto u32 = [&bytes] (std::size_t i)
    { return std::uint32_t (bytes[i]) | (std::uint32_t (bytes[i + 1]) << 8)
          | (std::uint32_t (bytes[i + 2]) << 16) | (std::uint32_t (bytes[i + 3]) << 24); };
    require (std::memcmp (bytes.data(), "RIFF", 4) == 0 && u32 (4) + 8 == bytes.size()
        && std::memcmp (bytes.data() + 8, "WAVEfmt ", 8) == 0 && u32 (16) == 40
        && bytes[20] == 0xfe && bytes[21] == 0xff && bytes[22] == 2 && bytes[23] == 0
        && u32 (24) == 48000 && bytes[32] == 8 && bytes[34] == 32 && u32 (40) == 3 && u32 (44) == 3
        && std::memcmp (bytes.data() + 60, "data", 4) == 0 && u32 (64) == 3840000
        && bytes.size() >= 68 + u32 (64),
        "S-1 must remain native stereo IEEE float");
    std::vector<float> mono (48000 * 4);
    for (std::size_t f = 0; f < mono.size(); ++f)
    {
        const auto left = u32 (68 + f * 8), right = u32 (72 + f * 8);
        require (left == right, "S-1 stereo channels must match for mono/stereo fixtures");
        std::memcpy (&mono[f], &left, sizeof (float));
        require (std::isfinite (mono[f]), "fixture must be finite");
    }
    return mono;
}

static std::unique_ptr<ExactRangeCapture> capture (const std::vector<float>& pcm, int channels,
                                                  std::int64_t start, std::uint64_t generation = 3)
{
    auto result = std::make_unique<ExactRangeCapture> (
        CaptureRange { generation, 48000, channels, start, static_cast<std::int64_t> (pcm.size()) },
        pcm.size() * channels * sizeof (float));
    for (std::size_t offset = 0; offset < pcm.size(); offset += 257)
    {
        const float* pointers[] = { pcm.data() + offset, pcm.data() + offset };
        result->push (pointers, channels, static_cast<int> (std::min<std::size_t> (257, pcm.size() - offset)),
                      start + static_cast<std::int64_t> (offset), generation, true, 48000);
    }
    return result; // Producer has exited before non-RT preparation starts.
}

static TrialFormat format (int channels, std::size_t frames)
{ return { { 1, 2, 3, 4 }, 48000, channels, 8192, static_cast<std::int64_t> (frames), 48000, false }; }

static void matchedCopies (const std::vector<float>& postPcm)
{
    for (int channels : { 1, 2 })
        for (float ratio : { 0.5f, 1.0f, 2.0f })
        {
            auto prePcm = postPcm;
            for (auto& sample : prePcm) sample *= ratio;
            auto post = capture (postPcm, channels, 8192), pre = capture (prePcm, channels, 0);
            const auto f = format (channels, postPcm.size());
            const auto budget = postPcm.size() * channels * 8;
            int randomCalls = 0;
            auto candidate = prepareLocalBlindCandidate (
                *post, *pre, f, 0, budget, GainMatchPolicy::alignedActiveBlocksV1,
                [&] { ++randomCalls; return true; });
            require (candidate.trial && candidate.failure == PreparationFailure::none, "native pair prepares");
            require (candidate.gainPolicy == GainMatchPolicy::alignedActiveBlocksV1
                         && candidate.matchedAnalysisUnits >= 27 && randomCalls == 1,
                     "continuous policy and one random assignment");
            require (std::abs (candidate.fixedPreGainDb + 20 * std::log10 (ratio)) < 0.002,
                     "fixed match must agree within 0.002 dB");
            require (candidate.trial->pcmBytes() == budget, "bounded pair PCM size");
            require (candidate.trial->view().phase == TrialPhase::ready, "preparation never starts playback");
            require (candidate.trial->start(), "no unnecessary lower-level approval");
            std::vector<float> left (257), right (257);
            float* pointers[] = { left.data(), right.data() };
            const TrialBlock block { f.epochs, 48000, f.start, true, true, true, false };
            require (candidate.trial->render (pointers, channels, 257, block) == TrialOutput::copy, "prepared PCM renders");
            double maxError = 0;
            for (std::size_t i = 0; i < left.size(); ++i)
                maxError = std::max (maxError, std::abs (double (left[i]) - postPcm[i]));
            require (maxError < 0.00004, "integer gain precision is within expected 1 milli-dB tolerance");
            if (ratio == 1) require (std::memcmp (left.data(), postPcm.data(), left.size() * sizeof (float)) == 0,
                                      "same-PCM control remains bit identical");
            std::cout << "channels=" << channels << " ratio=" << ratio << " match_db=" << candidate.fixedPreGainDb
                      << " blocks=" << candidate.matchedAnalysisUnits << " pcm_bytes=" << budget
                      << " max_error=" << maxError << '\n';
        }
}

static void unavailableAndApproval (const std::vector<float>& source)
{
    auto post = capture (source, 1, 8192), pre = capture (source, 1, 0);
    auto f = format (1, source.size());
    const auto budget = source.size() * 8;
    auto candidate = prepareLocalBlindCandidate (
        *post, *pre, f, 0, budget - 1, GainMatchPolicy::alignedActiveBlocksV1, [] { return false; });
    require (! candidate.trial && candidate.failure == PreparationFailure::capacity, "budget refusal");
    candidate = prepareLocalBlindCandidate (
        *post, *pre, f, 1, budget, GainMatchPolicy::alignedActiveBlocksV1, [] { return false; });
    require (! candidate.trial && candidate.failure == PreparationFailure::rangeMismatch, "unproven shifted range refused");
    ++f.epochs.capture;
    candidate = prepareLocalBlindCandidate (
        *post, *pre, f, 0, budget, GainMatchPolicy::alignedActiveBlocksV1, [] { return false; });
    require (! candidate.trial && candidate.failure == PreparationFailure::rangeMismatch, "generation mismatch refused");
    --f.epochs.capture;
    candidate = prepareLocalBlindCandidate (
        *post, *pre, f, 0, budget, GainMatchPolicy::alignedActiveBlocksV1,
        [] () -> bool { throw std::runtime_error ("rng"); });
    require (! candidate.trial && candidate.failure == PreparationFailure::preparationFailed, "no deterministic RNG fallback");
    candidate = prepareLocalBlindCandidate (
        *post, *pre, f, 0, budget, GainMatchPolicy::alignedActiveBlocksV1,
        [&] { pre->cancel(); return false; });
    require (! candidate.trial && candidate.failure == PreparationFailure::incompleteCapture,
             "cancellation during preparation cannot publish a candidate");
    candidate = prepareLocalBlindCandidate (
        *post, *pre, f, 0, budget, GainMatchPolicy::alignedActiveBlocksV1, [] { return false; });
    require (! candidate.trial && candidate.failure == PreparationFailure::incompleteCapture, "cancelled capture is unavailable");
    for (int variant : { 0, 1, 2 })
    {
        auto pcm = source;
        if (variant == 0) pcm.resize (48000); // short track
        if (variant == 1) std::fill (pcm.begin(), pcm.end(), 0); // genuine silence
        if (variant == 2) // sparse events without 27 contiguous active blocks
            for (std::size_t i = 0; i < pcm.size(); ++i) if (i % 48000 >= 1200) pcm[i] = 0;
        post = capture (pcm, 1, 8192); pre = capture (pcm, 1, 0);
        candidate = prepareLocalBlindCandidate (
            *post, *pre, format (1, pcm.size()), 0, budget,
            GainMatchPolicy::alignedActiveBlocksV1, [] { return false; });
        require (! candidate.trial && candidate.failure == PreparationFailure::gainUnavailable, "unqualified match cannot audition raw");
    }
    auto transient = source;
    for (auto& sample : transient) sample *= 0.5f;
    transient[20000] = 0.95f;
    post = capture (source, 1, 8192); pre = capture (transient, 1, 0);
    candidate = prepareLocalBlindCandidate (
        *post, *pre, f, 0, budget, GainMatchPolicy::alignedActiveBlocksV1, [] { return false; });
    require (candidate.trial && candidate.lowerPostGainDb < -6 && candidate.trial->view().lowerPostApprovalRequired,
             "crest-limited match proposes explicit lower POST");
    require (! candidate.trial->start() && candidate.trial->start (true), "lower POST requires approval");
}

static void exactTrackEvents (const std::vector<float>& source)
{
    std::vector<float> shortPost (source.size(), 0.0f);
    std::copy_n (source.begin(), 48000, shortPost.begin());
    auto shortPre = shortPost;
    for (auto& sample : shortPre) sample *= 0.5f;
    const auto budget = source.size() * 8;
    auto post = capture (shortPost, 1, 8192), pre = capture (shortPre, 1, 0);
    auto candidate = prepareLocalBlindCandidate (
        *post, *pre, format (1, source.size()), 0, budget,
        GainMatchPolicy::exactTrackEventEnergyV1, [] { return false; });
    require (candidate.trial && candidate.gainPolicy == GainMatchPolicy::exactTrackEventEnergyV1
                 && candidate.matchedAnalysisUnits == 50
                 && std::abs (candidate.fixedPreGainDb - 6.021) < 0.002,
             "one-second TRACK event uses the separate exact-range policy");

    std::vector<float> sparsePost (source.size(), 0.0f);
    for (std::size_t event : { 2'000u, 51'000u, 100'000u, 149'000u })
        std::copy_n (source.begin(), 1'440, sparsePost.begin() + event);
    auto sparsePre = sparsePost;
    for (auto& sample : sparsePre) sample *= 2.0f;
    post = capture (sparsePost, 1, 8192); pre = capture (sparsePre, 1, 0);
    candidate = prepareLocalBlindCandidate (
        *post, *pre, format (1, source.size()), 0, budget,
        GainMatchPolicy::exactTrackEventEnergyV1, [] { return false; });
    require (candidate.trial && candidate.matchedAnalysisUnits >= 3
                 && std::abs (candidate.fixedPreGainDb + 6.021) < 0.002,
             "sparse TRACK events match without loop padding");

    std::vector<float> silence (source.size(), 0.0f);
    post = capture (silence, 1, 8192); pre = capture (silence, 1, 0);
    candidate = prepareLocalBlindCandidate (
        *post, *pre, format (1, source.size()), 0, budget,
        GainMatchPolicy::exactTrackEventEnergyV1, [] { return false; });
    require (! candidate.trial && candidate.failure == PreparationFailure::gainUnavailable,
             "silence remains unavailable under TRACK policy");

    auto shortCapture = shortPost;
    shortCapture.resize (48000);
    post = capture (shortCapture, 1, 8192); pre = capture (shortCapture, 1, 0);
    candidate = prepareLocalBlindCandidate (
        *post, *pre, format (1, shortCapture.size()), 0, shortCapture.size() * 8,
        GainMatchPolicy::exactTrackEventEnergyV1, [] { return false; });
    require (! candidate.trial && candidate.failure == PreparationFailure::gainUnavailable,
             "TRACK policy requires one exact four-second capture instead of padding");
}

static ExactCaptureRequest productRequest (int channels, std::int64_t frames)
{
    return { "12345678-1234-1234-1234-123456789abc", { 2, "project", "pre" },
             3, 4, 1, 0, 48000, channels, 8192, frames, 1'000'000 };
}

static void productLifecycle (const std::vector<float>& source)
{
    int releaseCalls = 0;
    std::uint64_t releasedEpoch = 0;
    LocalBlindProductSession session ([&] (std::uint64_t epoch)
    {
        ++releaseCalls;
        releasedEpoch = epoch;
        return true;
    });
    const auto request = productRequest (1, static_cast<std::int64_t> (source.size()));
    auto post = capture (source, 1, request.nativeStart, request.captureGeneration);
    auto pre = capture (source, 1, request.nativeStart, request.captureGeneration);
    require (session.beginCapture (
                 11, request.captureGeneration, GainMatchPolicy::alignedActiveBlocksV1),
             "product admission binds capture generation and gain policy");
    require (session.needsService(), "capture admission keeps its failure observer alive");
    require (session.acceptCapturedPair (request, *post, *pre, [] { return false; }),
             "sealed exact pair is consumed once");
    require (session.view().phase == ProductSessionPhase::ready
                 && session.view().frames == request.frames
                 && session.view().gainPolicy == GainMatchPolicy::alignedActiveBlocksV1
                 && session.view().matchedAnalysisUnits >= 27
                 && releaseCalls == 0,
             "preparation publishes without starting or releasing scope");
    require (session.start(), "product trial starts explicitly");

    std::vector<float> output (257, 0.0f);
    float* pointers[] = { output.data() };
    const auto renderLap = [&] (bool loopWrap)
    {
        std::int64_t position = request.nativeStart;
        while (position < request.nativeStart + request.frames)
        {
            const auto count = static_cast<int> (std::min<std::int64_t> (
                static_cast<std::int64_t> (output.size()), request.nativeStart + request.frames - position));
            TrialBlock block { {}, request.sampleRate, position, true, true, true, false,
                               loopWrap && position == request.nativeStart,
                               request.nativeStart, request.nativeStart + request.frames };
            require (session.render (pointers, 1, count, block), "published trial owns product callback");
            position += count;
        }
    };
    renderLap (false);
    require (! session.answer (TrialAnswer::one), "one full hidden side is insufficient");
    require (session.select (2), "second hidden side can be requested");
    renderLap (true);
    require (session.answer (TrialAnswer::cannotDistinguish) && session.reveal(),
             "two complete native passes permit answer and reveal");
    auto differentPair = request.pair;
    ++differentPair.generation;
    session.validatePair (&differentPair);
    session.service();
    require (releaseCalls == 0 && session.view().phase == ProductSessionPhase::returnPending
                 && session.view().failure == ProductSessionFailure::pairChanged,
             "pair change stops output but cannot release admission before normal audio receipt");
    session.requestNormalReturn();
    TrialBlock normal { {}, request.sampleRate, 0, true, true, true, false };
    std::fill (output.begin(), output.end(), 0.75f);
    require (session.render (pointers, 1, static_cast<int> (output.size()), normal),
             "normal-return callback is owned until receipt");
    session.service();
    require (releaseCalls == 1 && releasedEpoch == 11
                 && session.view().phase == ProductSessionPhase::returned
                 && ! session.hasPublishedRealtime(),
             "only normal output receipt retires PCM and releases exact scope");

    auto trackRequest = productRequest (1, static_cast<std::int64_t> (source.size()));
    trackRequest.captureGeneration = 9;
    std::vector<float> shortPost (source.size(), 0.0f);
    std::copy_n (source.begin(), 48'000, shortPost.begin());
    auto shortPre = shortPost;
    for (auto& sample : shortPre) sample *= 0.5f;
    post = capture (shortPost, 1, trackRequest.nativeStart, trackRequest.captureGeneration);
    pre = capture (shortPre, 1, trackRequest.nativeStart, trackRequest.captureGeneration);
    require (session.beginCapture (12, trackRequest.captureGeneration,
                                   GainMatchPolicy::exactTrackEventEnergyV1),
             "returned session can admit a fresh capture with a new frozen policy");
    require (session.acceptCapturedPair (trackRequest, *post, *pre, [] { return false; })
                 && session.view().phase == ProductSessionPhase::ready
                 && session.view().gainPolicy == GainMatchPolicy::exactTrackEventEnergyV1
                 && session.view().matchedAnalysisUnits == 50,
             "product session uses the frozen TRACK policy for short audio");
    session.invalidate();
    session.requestNormalReturn();
    require (session.render (pointers, 1, static_cast<int> (output.size()), normal),
             "published TRACK trial returns through one normal callback");
    session.service();
    require (releaseCalls == 2 && releasedEpoch == 12
                 && session.view().phase == ProductSessionPhase::returned,
             "TRACK policy keeps the same audio-confirmed return contract");

    require (session.beginCapture (13, 10, GainMatchPolicy::alignedActiveBlocksV1),
             "returned session can admit another capture");
    session.invalidate();
    session.service();
    require (releaseCalls == 3 && releasedEpoch == 13
                 && session.view().phase == ProductSessionPhase::failed,
             "asynchronous capture failure releases without waiting for an audio receipt");
}

int main (int argc, char** argv)
{
    require (argc == 2, "pass the checked-in S-1 WAV path");
    const auto source = readFixture (argv[1]);
    matchedCopies (source);
    unavailableAndApproval (source);
    exactTrackEvents (source);
    productLifecycle (source);
    std::cout << "Local Blind preparation: PASS (real S-1, product capture-to-return, mono/stereo, fixed gain, same-PCM, fail-closed match, lower POST)\n";
}
