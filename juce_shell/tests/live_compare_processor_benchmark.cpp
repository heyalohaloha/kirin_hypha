#include "../src/PluginProcessor.h"
#include "ValidationStorageSandbox.h"
#include "LiveBlindLoopFixture.h"
#include "reference_rt_probe.h"
#include "ProcessorHeapProbe.h"
#include "BenchmarkPhaseBarrier.h"
#include "BenchmarkSampleCount.h"
#include "BenchmarkRealtimePolicy.h"
#include "ExactPcmOracle.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#if JUCE_MAC
 #include <dlfcn.h>
 #include <pthread/qos.h>
 #include <time.h>
#endif

#if JUCE_MAC
void initialiseBlindProductHostApplication();
#endif

// Non-shipping, paced full-processor timing. Includes Rust ingress, real pair/ring and normal
// message/measure/IO service. Not a DAW dropout test. macOS additionally observes the linked
// Rust System/C allocator through a separate test dylib; Windows still reports C++ coverage
// only. Locks/I/O and VM/custom allocator calls remain separate audit gates.
namespace
{
using Processor = KirinHyphaProcessorBase;
using Steady = std::chrono::steady_clock;
using namespace hypha::live_compare;
const auto measuredBlocks = static_cast<std::size_t> (hypha::test::benchmarkSampleCount());
constexpr int warmBlocks = 128;

void require (bool ok, const char* message)
{
    if (! ok) { std::cerr << "Full processor benchmark: " << message << '\n'; std::exit (EXIT_FAILURE); }
}

std::uint64_t threadCpuNanos()
{
   #if JUCE_MAC
    timespec time {};
    require (clock_gettime (CLOCK_THREAD_CPUTIME_ID, &time) == 0, "thread CPU clock is available");
    return static_cast<std::uint64_t> (time.tv_sec) * 1'000'000'000 + static_cast<std::uint64_t> (time.tv_nsec);
   #else
    return 0;
   #endif
}

void configureBenchmarkThread (bool audioQos)
{
   #if JUCE_MAC
    qos_class_t before = QOS_CLASS_UNSPECIFIED, after = QOS_CLASS_UNSPECIFIED;
    int beforePriority = 0, afterPriority = 0;
    require (pthread_get_qos_class_np (pthread_self(), &before, &beforePriority) == 0,
             "benchmark thread QoS is observable");
    if (audioQos)
        require (pthread_set_qos_class_self_np (QOS_CLASS_USER_INTERACTIVE, 0) == 0,
                 "benchmark-only audio QoS request succeeds");
    require (pthread_get_qos_class_np (pthread_self(), &after, &afterPriority) == 0,
             "benchmark thread QoS is verified after configuration");
    require (! audioQos || (after == QOS_CLASS_USER_INTERACTIVE && afterPriority == 0),
             "explicit benchmark QoS is actually active");
    std::cout << "benchmark_audio_qos=" << int (audioQos)
              << " qos_before=" << before << " relative_before=" << beforePriority
              << " qos_after=" << after << " relative_after=" << afterPriority << '\n';
   #else
    require (! audioQos, "explicit audio QoS is a macOS-only fixture option");
   #endif
}

struct Host final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setTimeInSamples (position);
        info.setTimeInSeconds (static_cast<double> (position) / 48000);
        info.setKirinAuxiliaryClockSource (1); // VST3 continuous, explicitly valid
        info.setKirinAuxiliaryClockSamples (position);
        loop.decorate (info, position);
        return info;
    }
    std::int64_t position = 0;
    bool playing = false;
    LiveBlindLoopFixture loop;
};

struct Measurements
{
    std::vector<double> pre, post, pair, cpuPair;
    unsigned heapOperations = 0, deadlineOverruns = 0;
    std::uint64_t windowBegin = 0, windowEnd = 0;
    ProcessorHeapCounts heap;
    void prepare()
    {
        pre.reserve (measuredBlocks); post.reserve (measuredBlocks); pair.reserve (measuredBlocks);
        cpuPair.reserve (measuredBlocks);
    }
    void print (int frames, const char* mode, bool splitCpu)
    {
        const auto percentile = [] (auto& values, std::size_t numerator)
        {
            std::sort (values.begin(), values.end());
            return values[values.size() * numerator / 100];
        };
        require (pair.size() == measuredBlocks, "every measured callback is present");
        std::cout << "frames=" << frames << " mode=" << mode << " blocks=" << pair.size()
                  << " pre_median_us=" << percentile (pre, 50) << " pre_p99_us=" << percentile (pre, 99)
                  << " post_median_us=" << percentile (post, 50) << " post_p99_us=" << percentile (post, 99)
                  << " pair_median_us=" << percentile (pair, 50) << " pair_p99_us=" << percentile (pair, 99)
                  << " window_begin_ns=" << windowBegin << " window_end_ns=" << windowEnd
                  << " deadline_overruns=" << deadlineOverruns << " cpp_heap_operations=" << heapOperations
                  << " system_heap_covered=" << int (initialiseProcessorHeapProbe())
                  << " system_allocations=" << heap.allocations << " system_frees=" << heap.frees;
        if (splitCpu) std::cout << " cpu_pair_median_us=" << percentile (cpuPair, 50)
                                << " cpu_pair_p99_us=" << percentile (cpuPair, 99);
        std::cout << '\n';
        require (heapOperations == 0, "C++ callback new/delete must stay zero");
        require (heap.allocations == 0 && heap.frees == 0, "observed System allocator calls must stay zero");
        require (deadlineOverruns == 0, "full processor pair must finish within the audio deadline");
    }
};

class Benchmark final : private juce::Timer
{
public:
    explicit Benchmark (int framesIn, bool splitCpuIn, bool audioQosIn)
        : frames (framesIn), splitCpu (splitCpuIn), audioQos (audioQosIn)
    {
        for (auto& result : measurements) result.prepare();
        for (auto role : { Processor::Role::Pre, Processor::Role::Post })
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
            auto instance = std::make_unique<Processor> (role);
            auto layout = instance->getBusesLayout();
            layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
            layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
            require (instance->setBusesLayout (layout), "stereo host layout");
            instance->setMeterContextPreference (hypha::meter_context::MeterContext::twoMix, false);
            instance->setPlayHead (&host);
            instance->prepareToPlay (48000, frames);
            require (instance->getLatencySamples() == 0, "normal path reports zero latency");
            (role == Processor::Role::Pre ? pre : post) = std::move (instance);
        }
        audio = std::thread ([this] { process(); });
        startTimer (20);
    }
    ~Benchmark() override { stopTimer(); running.store (false); audio.join(); }
    bool passed = false;

private:
    bool collect (int mode)
    {
        if (! barrier.ready (mode)) return false;
        done.store (false); requested.store (mode);
        return true;
    }
    void timerCallback() override
    {
        const auto limit = std::max (85.0, 20.0 + 3.0 * static_cast<double> (measuredBlocks + warmBlocks) * frames / 48000.0);
        require (Steady::now() - began < std::chrono::duration<double> (limit), "fixture timed out");
        if (stage == 5 || stage == 8)
        {
            // Status includes message-thread trial state. Never read it from the audio thread;
            // every measured frame below independently verifies PRE, gain and zero substitution.
            const auto status = post->liveCompareStatus();
            require (status.preAudible && status.active && status.matched && ! status.preWaiting
                && status.verdict == Verdict::accepted, "only proven stable output is measured");
            if (stage == 8) require (post->liveBlindStatus().stage == BlindStage::active,
                                    "measured anonymous trial remains active");
        }
        switch (stage)
        {
            case 0:
                if (pre->instanceId().isEmpty()) break;
                if (! preview) preview = post->createPairPreview();
                if (! demanded || Steady::now() - requestedAt >= std::chrono::milliseconds (1050))
                    if (hypha::pair_preview::request (preview))
                    { demanded = true; requestedAt = Steady::now(); }
                {
                    KirinPairPreviewValue value {};
                    if (! kirin_hypha_pair_preview_poll (preview.get(), &value)
                        || ! value.complete || ! value.has_single) break;
                }
                require (post->setPairCandidate (pre->instanceId(), {}), "discovered PRE selected");
                preview.reset();
                stage = 1;
                break;
            case 1:
                if (post->pairStatus() != KIRIN_PAIR_STATUS_PAIRED) break;
                play.store (true);
                if (! collect (0)) break;
                stage = 2;
                break;
            case 2:
                if (! done.load()) break;
                require (post->startLiveCompare() == StartResult::started, "real named session starts");
                stage = 3;
                break;
            case 3:
            {
                const auto measured = post->measureLiveCompare();
                if (! measured.ok()) break;
                require (std::abs (measured.measuredDb + 6.0206) < 0.002, "real MATCH measures minus-six dB at its millidB precision");
                require (post->applyLiveCompareMatch (planMatch (measured, 0.0), MatchChoice::basis), "measured MATCH accepted");
                matchedGain = post->liveCompareStatus().gain;
                const auto expectedGain = static_cast<float> (std::pow (10.0, measured.measuredDb / 20.0));
                require (std::memcmp (&matchedGain, &expectedGain, sizeof (float)) == 0,
                         "approved gain exactly matches the measured millidB value");
                post->selectLiveComparePre (true);
                host.loop.requested.store (true);
                stage = 4;
                break;
            }
            case 4:
                if (! post->liveCompareStatus().preAudible || host.loop.laps.load() < 2) break;
                if (! collect (1)) break;
                stage = 5;
                break;
            case 5:
                if (! done.load()) break;
                require (post->beginLiveBlind() == StartResult::started, "same MATCH enters Blind");
                stage = 6;
                break;
            case 6:
                post->serviceLiveBlind();
                if (post->liveBlindStatus().stage != BlindStage::active) break;
                require (post->selectLiveBlind (1), "anonymous source selected");
                sourceSelectedAt = Steady::now();
                stage = 7;
                break;
            case 7:
                if (Steady::now() - sourceSelectedAt < std::chrono::milliseconds (100)) break;
                if (! post->liveCompareStatus().preAudible)
                {
                    require (! triedOtherSource && post->selectLiveBlind (2), "anonymous PRE remains available");
                    triedOtherSource = true;
                    sourceSelectedAt = Steady::now();
                    break;
                }
                // Diagnostic oracle, not identity-bearing UI: baseline/candidate always time
                // the actual PRE renderer, never different random source mappings.
                if (! collect (2)) break;
                stage = 8;
                break;
            case 8:
                if (! done.load()) break;
                post->finishLiveCompare();
                for (int i = 0; i < 3; ++i)
                    measurements[static_cast<std::size_t> (i)].print (frames, i == 0 ? "A" : i == 1 ? "PRE_LOOP" : "BLIND_PRE_LOOP", splitCpu);
                passed = true;
                stopTimer();
                juce::MessageManager::getInstance()->stopDispatchLoop();
                break;
            default: require (false, "unexpected stage");
        }
    }

    static float sample (int channel, std::int64_t position)
    {
        const auto token = static_cast<std::uint32_t> (position + 1) * 0x9e3779b1u;
        return static_cast<float> (((token >> (channel == 0 ? 0 : 16)) & 0xffffu) + 1) / 524288.0f;
    }
    void process()
    {
        // Fixture scheduling, before callbacks/probes. Changes this test thread only, not a
        // product thread, DAW, device or global scheduler. Default remains separately auditable.
        configureBenchmarkThread (audioQos);
        hypha::test::BenchmarkRealtimePolicy realtimePolicy;
        realtimePolicy.configure (frames);
        juce::AudioBuffer<float> buffer (2, frames);
        juce::MidiBuffer midi;
        // Initialise the diagnostic counter's own C++ TLS before callback instrumentation.
        // Otherwise macOS allocates its TLV block while the System probe is active, falsely
        // attributing this test-only setup to the first product callback. No product warmup.
        beginReferenceRtProbe();
        endReferenceRtProbe();
        if (splitCpu) threadCpuNanos(); // initialise only the diagnostic clock, not the product
        auto next = Steady::now();
        int mode = -1, observed = 0;
        while (running.load())
        {
            const auto command = requested.exchange (-1);
            if (command >= 0) { mode = command; observed = 0; }
            host.playing = play.load();
            host.loop.advance (host.position);
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < frames; ++f) buffer.setSample (c, f, sample (c, host.position + f));
            ProcessorHeapCounts heapCounts;
            require (beginProcessorHeapProbe (&heapCounts), "PRE heap probe attaches successfully");
            beginReferenceRtProbe();
            const auto preCpuStart = splitCpu ? threadCpuNanos() : 0;
            const auto preStart = Steady::now();
            pre->processBlock (buffer, midi);
            const auto preEnd = Steady::now();
            const auto preCpuEnd = splitCpu ? threadCpuNanos() : 0;
            unsigned heap = endReferenceRtProbe();
            require (endProcessorHeapProbe(), "PRE heap probe detaches successfully");
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < frames; ++f)
                {
                    const auto expected = sample (c, host.position + f);
                    const auto actual = buffer.getSample (c, f);
                    require (std::memcmp (&actual, &expected, sizeof (float)) == 0, "PRE remains bit identical");
                    buffer.setSample (c, f, -0.5f * actual);
                }
            require (beginProcessorHeapProbe (&heapCounts), "POST heap probe attaches successfully");
            beginReferenceRtProbe();
            const auto postCpuStart = splitCpu ? threadCpuNanos() : 0;
            const auto postStart = Steady::now();
            post->processBlock (buffer, midi);
            const auto postEnd = Steady::now();
            const auto postCpuEnd = splitCpu ? threadCpuNanos() : 0;
            heap += endReferenceRtProbe();
            require (endProcessorHeapProbe(), "POST heap probe detaches successfully");
            if (heap != 0 || heapCounts.allocations != 0 || heapCounts.frees != 0)
            {
                std::cerr << "position=" << host.position << " mode=" << mode << " observed=" << observed
                          << " cpp=" << heap << " system_alloc=" << heapCounts.allocations << " system_free=" << heapCounts.frees << '\n';
               #if JUCE_MAC
                for (const auto* address : { heapCounts.firstAllocation, heapCounts.firstFree })
                {
                    Dl_info symbol {};
                    if (address != nullptr && dladdr (address, &symbol) != 0)
                        std::cerr << "heap caller=" << address << " image_base=" << symbol.dli_fbase
                                  << " image=" << symbol.dli_fname << " symbol=" << (symbol.dli_sname != nullptr ? symbol.dli_sname : "unknown") << '\n';
                }
               #endif
            }
            require (heap == 0 && heapCounts.allocations == 0 && heapCounts.frees == 0,
                     "every callback, including startup and transitions, is heap-free");
            if (mode >= 0 && ++observed > warmBlocks)
            {
                const bool preOutput = mode > 0; // expectation, never inferred from product output/status
                for (int c = 0; c < 2; ++c)
                    for (int f = 0; f < frames; ++f)
                    {
                        const auto input = sample (c, host.position + f);
                        const auto expected = preOutput ? input * matchedGain : input * -0.5f;
                        const auto actual = buffer.getSample (c, f);
                        if (mode == 0) require (std::memcmp (&actual, &expected, sizeof (float)) == 0, "normal POST remains bit identical");
                        else require (hypha::test::exactScaledPcm (actual, input, matchedGain), "every compared frame has the exact source and gain");
                    }
                const auto preUs = std::chrono::duration<double, std::micro> (preEnd - preStart).count();
                const auto postUs = std::chrono::duration<double, std::micro> (postEnd - postStart).count();
                auto& result = measurements[static_cast<std::size_t> (mode)];
                if (result.pair.empty())
                {
                    realtimePolicy.verify();
                    result.windowBegin = static_cast<std::uint64_t> (std::chrono::duration_cast<std::chrono::nanoseconds> (preStart.time_since_epoch()).count());
                }
                result.windowEnd = static_cast<std::uint64_t> (std::chrono::duration_cast<std::chrono::nanoseconds> (postEnd.time_since_epoch()).count());
                result.pre.push_back (preUs); result.post.push_back (postUs); result.pair.push_back (preUs + postUs);
                if (splitCpu) result.cpuPair.push_back (static_cast<double> (preCpuEnd - preCpuStart + postCpuEnd - postCpuStart) / 1000);
                result.heapOperations += heap;
                result.heap.allocations += heapCounts.allocations; result.heap.frees += heapCounts.frees;
                result.deadlineOverruns += preUs + postUs >= static_cast<double> (frames) * 1e6 / 48000;
                if (result.pair.size() == measuredBlocks)
                {
                    realtimePolicy.verify();
                    mode = -1; done.store (true);
                }
            }
            if (host.playing) host.position += frames;
            next += std::chrono::nanoseconds (static_cast<long long> (frames) * 1'000'000'000 / 48000);
            std::this_thread::sleep_until (next);
        }
    }

    int frames, stage = 0;
    bool splitCpu = false, audioQos = false;
    Host host;
    std::unique_ptr<Processor> pre, post;
    hypha::pair_preview::Ticket preview;
    std::thread audio;
    std::atomic<bool> running { true }, play { false }, done { false };
    std::atomic<int> requested { -1 };
    std::array<Measurements, 3> measurements;
    hypha::test::BenchmarkPhaseBarrier barrier;
    Steady::time_point began = Steady::now(), requestedAt, sourceSelectedAt;
    bool demanded = false, triedOtherSource = false;
    float matchedGain = 1.0f; // published before collect's release store
};
}

int main (int argc, char** argv)
{
    require (hypha::test::exactPcmControls(), "runtime exact PCM oracle controls");
    require (argc >= 2 && argc <= 4,
             "usage: full processor benchmark 64|128|256|512 [--cpu-split] [--audio-qos]");
    bool splitCpu = false, audioQos = false;
    for (int i = 2; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--cpu-split") == 0 && ! splitCpu) splitCpu = true;
        else if (std::strcmp (argv[i], "--audio-qos") == 0 && ! audioQos) audioQos = true;
        else require (false, "unknown or duplicated fixture option");
    }
    const int frames = std::atoi (argv[1]);
    require (frames == 64 || frames == 128 || frames == 256 || frames == 512, "specified buffer size is in the gate");
   #if ! JUCE_MAC
    require (argc == 2, "thread CPU split is a macOS-only diagnostic");
   #endif
    ValidationStorageSandbox sandbox;
   #if JUCE_MAC
    initialiseBlindProductHostApplication();
   #endif
    juce::ScopedJuceInitialiser_GUI init;
   #if JUCE_MAC
    require (initialiseProcessorHeapProbe(), "macOS allocator hook initialises before audio");
    const uint8_t roles[] { KIRIN_CHANNEL_ROLE_LEFT, KIRIN_CHANNEL_ROLE_RIGHT };
    ProcessorHeapCounts positive;
    require (beginProcessorHeapProbe (&positive), "positive-control heap probe attaches successfully");
    auto* engine = kirin_hypha_create (48000, roles, 2);
    kirin_hypha_destroy (engine);
    require (endProcessorHeapProbe(), "positive-control heap probe detaches successfully");
    require (engine != nullptr && positive.allocations > 0 && positive.frees > 0,
             "positive control must observe real Rust FFI allocation and destruction");
    std::cout << "Rust System positive control allocations=" << positive.allocations << " frees=" << positive.frees << '\n';
   #endif
    Benchmark benchmark (frames, splitCpu, audioQos);
    juce::MessageManager::getInstance()->runDispatchLoop();
    return benchmark.passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
