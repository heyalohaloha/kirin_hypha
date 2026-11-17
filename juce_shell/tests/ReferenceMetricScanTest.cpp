#include "reference_audition/ReferenceMetricScan.h"
#include <iostream>
#include <stdexcept>
using namespace hypha::reference_audition::metrics;
void require (bool value, const char* message) { if (!value) throw std::runtime_error (message); }
bool same (double a, double b) { return a == b || (std::isnan (a) && std::isnan (b)); }
void pcm (std::uint64_t position, float* values, std::size_t count, std::uint32_t rate, std::uint32_t channels)
{
    for (std::size_t i = 0; i < count; ++i)
        values[i] = static_cast<float> (std::sin (static_cast<double> (position + i / channels) * 0.13))
            * (i % channels == 0 ? 0.2f : -0.11f) * (position < rate / 10 ? 0.1f : 1.0f);
}
int main()
{
    try
    {
        std::atomic<bool> cancel {false};
        for (const auto rate : {44100u, 48000u})
            for (const auto channels : {1u, 2u})
            {
                const auto aperture = rate / 10;
                std::uint64_t calls = 0;
                auto result = scan (rate, channels, 3 * aperture + 17, 3,
                    [&] (auto position, auto* samples, auto count) {
                        require (position == calls++ * aperture, "continuous full apertures only");
                        pcm (position, samples, count, rate, channels); return ReadState::rendered;
                    }, cancel);
                require (result.state == ScanState::complete && result.frames.size() == 3 && calls == 3, "complete scan");
                require (result.sourceFrames == 3 * aperture + 17, "partial EOF is not padded");
                auto* direct = kirin_reference_metrics_create (rate, channels);
                require (direct != nullptr, "direct handle");
                std::vector<KirinReferenceMetricFrame> expected;
                std::vector<float> input (aperture * channels);
                KirinReferenceMetricFrame output[8] {};
                for (std::uint64_t slot = 0; slot < 3; ++slot)
                {
                    pcm (slot * aperture, input.data(), input.size(), rate, channels);
                    std::size_t count = 0;
                    require (kirin_reference_metrics_push (direct, input.data(), input.size(), output, 8, &count), "direct push");
                    expected.insert (expected.end(), output, output + count);
                }
                std::size_t count = 0;
                require (kirin_reference_metrics_finish (direct, output, 8, &count), "direct EOF");
                expected.insert (expected.end(), output, output + count);
                kirin_reference_metrics_drop (direct);
                require (expected.size() == result.frames.size(), "same EOF observations");
                for (std::size_t i = 0; i < expected.size(); ++i)
                {
                    const auto& a = expected[i]; const auto& b = result.frames[i];
                    require (a.end_sample == b.end_sample && same(a.crest_db,b.crest_db)
                        && same(a.psr_db,b.psr_db) && same(a.sharpness_acum,b.sharpness_acum), "exact native parity");
                }
            }
        unsigned calls = 0;
        const auto silence = [&] (auto, auto* samples, auto count) {
            ++calls; std::fill(samples, samples + count, 0.0f); return ReadState::rendered;
        };
        require (scan (44105, 1, 4410, 1, silence, cancel).state == ScanState::invalidFormat, "unsupported format");
        require (scan (48000, 1, 9600, 1, silence, cancel).state == ScanState::capacityExceeded, "bounded storage");
        require (calls == 0, "reject before read");
        cancel = true;
        require (scan (48000, 1, 4800, 1, silence, cancel).state == ScanState::cancelled && calls == 0, "cancel before work");
        cancel = false;
        const auto cancelled = scan (48000, 1, 9600, 2, [&] (auto, auto*, auto) {
            cancel = true; return ReadState::rendered;
        }, cancel);
        require (cancelled.state == ScanState::cancelled && cancelled.frames.empty(), "cancel before engine/publish");
        cancel = false;
        for (const auto state : {ReadState::waiting, ReadState::failed})
        {
            calls = 0;
            const auto failed = scan (48000, 1, 9600, 2, [&] (auto, auto* samples, auto count) {
                std::fill(samples, samples + count, 0.0f); return ++calls == 1 ? ReadState::rendered : state;
            }, cancel);
            require (failed.frames.empty() && calls == 2, "no partial publication or waiting spin");
            require (failed.state == (state == ReadState::waiting ? ScanState::waiting : ScanState::readFailed), "read state retained");
        }
        calls = 0;
        const auto lateCancel = scan (48000, 1, 9600, 2, [&] (auto, auto* samples, auto count) {
            std::fill(samples, samples + count, 0.0f);
            if (++calls == 2) cancel = true;
            return ReadState::rendered;
        }, cancel);
        require (lateCancel.state == ScanState::cancelled && lateCancel.frames.empty() && calls == 2, "cancel discards previous apertures");
        cancel = false;
        for (const auto partial : {true, false})
        {
            const auto invalid = scan (48000, 1, 4800, 1, [partial] (auto, auto* samples, auto count) {
                std::fill(samples, samples + count - (partial ? 1 : 0), 0.0f);
                if (!partial) samples[0] = std::numeric_limits<float>::infinity();
                return ReadState::rendered;
            }, cancel);
            require (invalid.state == ScanState::analysisFailed && invalid.frames.empty(), "incomplete or infinite PCM is not measured");
        }
        const auto shortSource = scan (48000, 1, 4799, 1, silence, cancel);
        require (shortSource.state == ScanState::complete && shortSource.frames.empty(), "short file has no observations, not zero");
        const auto thrown = scan (48000, 1, 4800, 1, [] (auto, auto*, auto) -> ReadState {
            throw std::runtime_error("reader failed");
        }, cancel);
        require (thrown.state == ScanState::readFailed && thrown.frames.empty(), "reader exception stays unavailable");
        const auto unwritten = scan (48000, 1, 4800, 1, [] (auto, auto*, auto) { return ReadState::rendered; }, cancel);
        require (unwritten.state == ScanState::analysisFailed && unwritten.frames.empty(), "never reuse unfilled PCM");
        std::cout << "Reference metric scan contracts passed\n";
        return 0;
    }
    catch (const std::exception& failure) { std::cerr << failure.what() << '\n'; return 1; }
}
