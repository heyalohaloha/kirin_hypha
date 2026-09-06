#include "../src/local_blind/LocalBlindPreparation.h"
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
// Require the actual 48 kHz, stereo, extensible IEEE-float file instead of generating a substitute.
static std::vector<float> readFixture (const char* path)
{
    std::ifstream input (path, std::ios::binary);
    require (input.good(), "S-1 fixture is required");
    const std::vector<unsigned char> bytes ((std::istreambuf_iterator<char> (input)), {});
    require (bytes.size() == 3840068, "S-1 file size changed; requalify fixture");
    const auto u32 = [&bytes] (std::size_t i)
    { return std::uint32_t (bytes[i]) | (std::uint32_t (bytes[i + 1]) << 8)
          | (std::uint32_t (bytes[i + 2]) << 16) | (std::uint32_t (bytes[i + 3]) << 24); };
    require (std::memcmp (bytes.data(), "RIFF", 4) == 0 && u32 (4) + 8 == bytes.size()
        && std::memcmp (bytes.data() + 8, "WAVEfmt ", 8) == 0 && u32 (16) == 40
        && bytes[20] == 0xfe && bytes[21] == 0xff && bytes[22] == 2 && bytes[23] == 0
        && u32 (24) == 48000 && bytes[32] == 8 && bytes[34] == 32 && u32 (40) == 3 && u32 (44) == 3
        && std::memcmp (bytes.data() + 60, "data", 4) == 0 && u32 (64) == 3840000,
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
            auto candidate = prepareLocalBlindCandidate (*post, *pre, f, 0, budget, [&] { ++randomCalls; return true; });
            require (candidate.trial && candidate.failure == PreparationFailure::none, "native pair prepares");
            require (candidate.matchedBlocks >= 27 && randomCalls == 1, "gain policy and one random assignment");
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
                      << " blocks=" << candidate.matchedBlocks << " pcm_bytes=" << budget
                      << " max_error=" << maxError << '\n';
        }
}

static void unavailableAndApproval (const std::vector<float>& source)
{
    auto post = capture (source, 1, 8192), pre = capture (source, 1, 0);
    auto f = format (1, source.size());
    const auto budget = source.size() * 8;
    auto candidate = prepareLocalBlindCandidate (*post, *pre, f, 0, budget - 1, [] { return false; });
    require (! candidate.trial && candidate.failure == PreparationFailure::capacity, "budget refusal");
    candidate = prepareLocalBlindCandidate (*post, *pre, f, 1, budget, [] { return false; });
    require (! candidate.trial && candidate.failure == PreparationFailure::rangeMismatch, "unproven shifted range refused");
    ++f.epochs.capture;
    candidate = prepareLocalBlindCandidate (*post, *pre, f, 0, budget, [] { return false; });
    require (! candidate.trial && candidate.failure == PreparationFailure::rangeMismatch, "generation mismatch refused");
    --f.epochs.capture;
    candidate = prepareLocalBlindCandidate (*post, *pre, f, 0, budget, [] () -> bool { throw std::runtime_error ("rng"); });
    require (! candidate.trial && candidate.failure == PreparationFailure::preparationFailed, "no deterministic RNG fallback");
    candidate = prepareLocalBlindCandidate (*post, *pre, f, 0, budget, [&] { pre->cancel(); return false; });
    require (! candidate.trial && candidate.failure == PreparationFailure::incompleteCapture,
             "cancellation during preparation cannot publish a candidate");
    candidate = prepareLocalBlindCandidate (*post, *pre, f, 0, budget, [] { return false; });
    require (! candidate.trial && candidate.failure == PreparationFailure::incompleteCapture, "cancelled capture is unavailable");
    for (int variant : { 0, 1, 2 })
    {
        auto pcm = source;
        if (variant == 0) pcm.resize (48000); // short track
        if (variant == 1) std::fill (pcm.begin(), pcm.end(), 0); // genuine silence
        if (variant == 2) // sparse events without 27 contiguous active blocks
            for (std::size_t i = 0; i < pcm.size(); ++i) if (i % 48000 >= 1200) pcm[i] = 0;
        post = capture (pcm, 1, 8192); pre = capture (pcm, 1, 0);
        candidate = prepareLocalBlindCandidate (*post, *pre, format (1, pcm.size()), 0, budget, [] { return false; });
        require (! candidate.trial && candidate.failure == PreparationFailure::gainUnavailable, "unqualified match cannot audition raw");
    }
    auto transient = source;
    for (auto& sample : transient) sample *= 0.5f;
    transient[20000] = 0.95f;
    post = capture (source, 1, 8192); pre = capture (transient, 1, 0);
    candidate = prepareLocalBlindCandidate (*post, *pre, f, 0, budget, [] { return false; });
    require (candidate.trial && candidate.lowerPostGainDb < -6 && candidate.trial->view().lowerPostApprovalRequired,
             "crest-limited match proposes explicit lower POST");
    require (! candidate.trial->start() && candidate.trial->start (true), "lower POST requires approval");
}

int main (int argc, char** argv)
{
    require (argc == 2, "pass the checked-in S-1 WAV path");
    const auto source = readFixture (argv[1]);
    matchedCopies (source);
    unavailableAndApproval (source);
    std::cout << "Local Blind preparation: PASS (real S-1, mono/stereo, fixed gain, same-PCM, fail-closed match, lower POST)\n";
}
