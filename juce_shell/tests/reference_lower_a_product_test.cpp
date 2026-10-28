// R-12・INV-S47：A を承認して下げる道を、POST の processor で通して確かめる。試験用の HOME に Kirin OS の
// ライセンスの印と Reference のライブラリを置く（利用者の Kirin OS の場所には触れない）。
// 確かめること：下げているあいだ LISTEN・LIVE BLIND・PRE/POST Blind・VERSION BLIND は RETURN を待つ。
// 測定は下げる前の A。オフライン書き出しと bypass のブロックには掛けない。RETURN は B を止めてから上げ、戻ると A は
// bit 同一。
#include "reference_runtime_v2_analysis_test_support.h"
#include "../src/PluginProcessor.h"
#include "reference_whole_song_fixture.h"
#include "reference_library_manifest_fixture.h"
#include "ReferenceOneSongLibrary.h"
#include "ReferenceValidationStorageSandbox.h"
#include "LocalBlindSignalFixture.h"
#include "../src/AtomicFile.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <type_traits>
#include "reference_rt_probe.h"
#include "TestProcessorStorage.h"
#if JUCE_MAC
void initialiseBlindProductHostApplication();
#endif

namespace
{
using Processor = KirinHyphaProcessorBase;
using Steady = std::chrono::steady_clock;
using hypha::output_owner::Activity;
using hypha::output_owner::Reason;

struct Clock final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing.load());
        info.setTimeInSamples (position.load());
        info.setTimeInSeconds (static_cast<double> (position.load()) / 48000.0);
        return info;
    }
    std::atomic<std::int64_t> position { 0 };
    std::atomic<bool> playing { false };
};

// 静かで peak の大きい B の曲：A（−6 dBFS の正弦）に合わせると上限を超え、A を下げる承認が要る。
void writeLibrary (const juce::File& root) { OneSongLibrary song; song.frequencyHz = 1500; writeOneSongLibrary (root, song); }

template <typename Sample>
double referenceTonePower (const Sample* audio, int frames)
{
    double power = 0, sine = 0, cosine = 0;
    for (int f = 0; f < frames; ++f)
    {
        const double out = audio[f], angle = 6.283185307179586 * 1500.0 * f / 48000.0;
        power += out * out; sine += out * std::sin (angle); cosine += out * std::cos (angle);
    }
    return power > 1.0e-8 ? 2 * (sine * sine + cosine * cosine) / (frames * power) : 0;
}

template <typename Sample>
class LowerAProduct final : private juce::Timer
{
public:
    explicit LowerAProduct (std::vector<float> input) : signal (std::move (input))
    {
        require (signal.size() >= frames && referenceTonePower (signal.data(), frames) < 0.01,
                 "the independent tone oracle rejects the known A fixture");
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
        post = std::make_unique<Processor> (Processor::Role::Post);
        auto layout = post->getBusesLayout();
        layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
        layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
        require (post->setBusesLayout (layout), "stereo layout");
        post->setProcessingPrecision (std::is_same_v<Sample, double> ? juce::AudioProcessor::doublePrecision
                                                                 : juce::AudioProcessor::singlePrecision);
        post->setPlayHead (&clock);
        hypha::tests::prepareFixtureProcessor (*post, 48000, frames);
        require (post->getLatencySamples() == 0, "ordinary A starts with zero reported latency");
        post->refreshLicenseForUserAction();
        require (post->licenseIsOs(), "the sandbox identity grants the Kirin OS license");
        clock.playing.store (true);
        audio = std::thread ([this] { processAudio(); });
        startTimer (20);
    }
    ~LowerAProduct() override { stopTimer(); running.store (false); audio.join(); }
    bool passed = false;

private:
    Sample input (std::int64_t position) const
    {
        const auto value = signal[static_cast<std::size_t> (position) % signal.size()];
        if constexpr (std::is_same_v<Sample, double>) return std::nextafter (static_cast<double> (value), 1.0);
        return value;
    }
    void timerCallback() override
    {
        const auto progress = blocks.load();
        if (progress != lastProgress) { lastProgress = progress; lastProgressAt = Steady::now(); }
        require (Steady::now() - lastProgressAt < std::chrono::minutes (5), "audio worker stopped making progress");
        require (progress < 48000 * 300 / frames, "scenario exceeds its input-content budget");
        const auto state = post->referenceAuditionSnapshot();
        switch (stage.load())
        {
            case 0:  // B の曲が準備でき、A の 4 秒が測れてから B を押す
                if (! state.referenceReady || blocks.load() < 48000 * 4 / frames) break;
                require (! post->selectReferenceRef(), "a B MATCH over the ceiling does not play B");
                stage = 1;
                break;
            case 1:
            {
                const auto& b = state.referenceSelection ? *state.referenceSelection : state;
                if (b.matchFailure != hypha::reference_audition::MatchFailure::ceilingExceeded) break;
                needed = b.neededAttenuationDb;
                require (needed < -10.0, "the refusal names how far A must be lowered");
                KirinMeterSession meter {};
                require (post->pollMeterSession (meter) && std::isfinite (meter.lufs_m), "A is measured before the approval");
                beforeLufs = meter.lufs_m;
                require (post->approveReferenceLowerA ({ 3, needed, b.playbackIdentity, b.selectionGeneration, b.matchFailureSerial })
                             == hypha::reference_audition::LowerAApproval::lowered,
                         "the approval lowers A");
                stage = 2;
                break;
            }
            case 2:  // 下げ終わって B が鳴る
                if (state.audibleComparisonSlot != 3) break;
                stageBlocks = blocks.load();
                stage = 3;
                break;
            case 3:
            {
                if (blocks.load() < stageBlocks + 48000 / frames) break;  // 1 秒（測定の窓が下げた後になる）
                require (std::abs (post->referenceHeldAttenuationDb() - needed) < 0.05, "A stays lowered by the approved amount");
                using StartResult = hypha::live_compare::StartResult;
                require (post->startLiveCompare() == StartResult::returnRequired
                             && post->beginLiveBlind() == StartResult::returnRequired,
                         "LISTEN and LIVE BLIND wait for RETURN while A is lowered");
                require (post->localBlindCaptureAvailability() == hypha::local_blind::CaptureAdmission::referenceLowered,
                         "PRE / POST Blind waits for RETURN while A is lowered");
                const auto blind = post->outputDecision (Activity::versionBlind);
                require (blind.refused() && blind.reason == Reason::returnFirst, "VERSION BLIND waits for RETURN while A is lowered");
                KirinMeterSession meter {};
                require (post->pollMeterSession (meter) && std::abs (meter.lufs_m - beforeLufs) < 1.0,
                         "the POST measurement keeps reading A before the lowering");
                require (heardB.load(), "the actual output contains the independent Reference tone");
                require (loweredPeak.load() < 0.2f * peak, "the output is lowered while the approval holds");
                mode.store (1);  // オフライン書き出し
                stageBlocks = blocks.load();
                stage = 4;
                break;
            }
            case 4:
            case 5:
                if (blocks.load() < stageBlocks + 12) break;
                require (verified.load() >= 8 && mismatches.load() == 0,
                         stage == 4 ? "an offline block is never lowered and never plays B"
                                    : "a bypassed block is never lowered and never plays B");
                verified.store (0);
                mode.store (stage == 4 ? 2 : 0);
                stageBlocks = blocks.load();
                ++stage;
                break;
            case 6:  // 戻った後は、また下げる（ramp で）。RETURN で B を止めてから上げる。
                if (blocks.load() < stageBlocks + 12) break;
                require (risePeak.load() <= peak + 1.0e-6f, "after offline and bypass, A is lowered again without a jump");
                post->returnReferenceLevelToNormal();
                require (post->referenceHeldAttenuationDb() > -1.0e-9, "RETURN gives up the held amount");
                risePeak.store (0.0f);
                mode.store (3);
                stageBlocks = blocks.load();
                stage = 7;
                break;
            case 7:
                if (blocks.load() < stageBlocks + 48000 / frames) break;  // 0.5 秒の上げ＋余裕
                require (risePeak.load() <= peak + 1.0e-6f, "RETURN never raises the output above A");
                verified.store (0); mismatches.store (0);
                mode.store (4);
                stageBlocks = blocks.load();
                stage = 8;
                break;
            case 8:
                if (blocks.load() < stageBlocks + 12) break;
                require (verified.load() >= 8 && mismatches.load() == 0, "after RETURN, A is bit identical again");
                require (post->startLiveCompare() != hypha::live_compare::StartResult::returnRequired
                             && ! post->outputDecision (Activity::versionBlind).refused(),
                         "after RETURN, LISTEN and Blind may start");
                require (post->getLatencySamples() == 0, "Reference, offline, bypass and RETURN add no latency");
                std::cout << "Reference lower A product: PASS lowered " << needed << " dB\n";
                passed = true;
                stopTimer();
                juce::MessageManager::getInstance()->stopDispatchLoop();
                break;
            default: break;
        }
    }
    void processAudio()
    {
        juce::AudioBuffer<Sample> buffer (2, frames);
        juce::MidiBuffer midi;
        auto* bypass = post->getBypassParameter();
        auto next = Steady::now();
        int current = 0, settle = 0;
        while (running.load())
        {
            const int wanted = mode.load();
            if (wanted != current)
            {
                post->setNonRealtime (wanted == 1);
                if (bypass != nullptr) bypass->setValue (wanted == 2 ? 1.0f : 0.0f);
                current = wanted;
                settle = 1;  // 切り替えのブロックは数えない
            }
            const auto start = clock.position.load();
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < frames; ++f) buffer.setSample (c, f, input (start + f));
            beginReferenceRtProbe(); post->processBlock (buffer, midi);
            require (endReferenceRtProbe() == 0, "Reference callback has no C++ heap operations");
            float loudest = 0.0f;
            int differences = 0;
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < frames; ++f)
                {
                    loudest = std::max (loudest, static_cast<float> (std::abs (buffer.getSample (c, f))));
                    const auto expected = input (start + f);
                    const auto out = buffer.getSample (c, f);
                    if (std::memcmp (&out, &expected, sizeof (Sample)) != 0)
                    {
                        ++differences;
                        const Sample widened = static_cast<Sample> (static_cast<float> (out));
                        require (std::memcmp (&out, &widened, sizeof (Sample)) == 0, "changed Reference and fade samples are widened float PCM");
                    }

                }
            if (current == 0 && stage == 3 && referenceTonePower (buffer.getReadPointer (0), frames) > 0.99) heardB.store (true);
            if (current == 0 && stage == 3) loweredPeak.store (std::max (loweredPeak.load(), loudest));
            if ((current == 0 && stage == 6) || current == 3) risePeak.store (std::max (risePeak.load(), loudest));
            if ((current == 1 || current == 2 || current == 4) && settle == 0)
            {
                verified.fetch_add (1);
                if (differences != 0) mismatches.fetch_add (1);
            }
            settle = std::max (0, settle - 1);
            clock.position.fetch_add (frames);
            blocks.fetch_add (1);
            next += std::chrono::nanoseconds (static_cast<long long> (frames) * 1'000'000'000 / 48000);
            if (next > Steady::now()) std::this_thread::sleep_until (next);
        }
    }

    static constexpr int frames = 480;
    static constexpr float peak = 0.5011872f;  // S-1：−6 dBFS
    std::vector<float> signal;
    Clock clock;
    std::unique_ptr<Processor> post;
    std::thread audio;
    std::atomic<bool> running { true }, heardB { false };
    std::atomic<int> mode { 0 }, verified { 0 }, mismatches { 0 };  // 0 通常、1 オフライン、2 bypass、3 RETURN、4 確かめ
    std::atomic<std::int64_t> blocks { 0 };
    std::atomic<float> loweredPeak { 0.0f }, risePeak { 0.0f };
    std::int64_t stageBlocks = 0;
    std::atomic<int> stage { 0 };
    double needed = 0.0, beforeLufs = 0.0;
    std::int64_t lastProgress = 0;
    Steady::time_point lastProgressAt = Steady::now();
};
}

int main (int argc, char** argv)
{
    require (argc == 2 || (argc == 3 && juce::String (argv[2]) == "--double-precision"), "usage: reference lower A product S-1.wav");
    auto signal = readFixture (argv[1]);
    ReferenceValidationStorageSandbox sandbox;
    // macOS JUCE resolves the home without HOME; keep PRE display files out of the real Kirin OS.
    hypha::pre_display::Controller::placeUnderForTest (sandbox.directory());
    const auto& box = sandbox.directory();
    // Rust の保存先（identity・plugin_data）は環境変数 HOME に従う。試験用の HOME の中にライセンスの印を置く。
    const auto* homeValue = std::getenv ("HOME");
    require (homeValue != nullptr, "the sandbox sets HOME");
    const auto identity = juce::File (juce::String::fromUTF8 (homeValue))
                              .getChildFile ("Library/Application Support/Kirin OS/identity.json");
    // Reference の置き場所は JUCE が決め、macOS では HOME に従わない。試験用のフォルダへ向ける。
    const auto reference = box.getChildFile ("reference-v2");
    ref::RuntimeV2Repository::setTransportRootForTesting (reference);
    // 書く前に、書き先がどれも試験用のフォルダの中だと確かめる（本物の Kirin OS の場所には書かない）。
    require (box.isDirectory() && identity.isAChildOf (box) && reference.isAChildOf (box)
                 && ref::RuntimeV2Repository::transportRoot() == reference,
             "every write stays inside the sandbox");
    std::cout << "Reference lower A product: sandbox " << box.getFullPathName() << '\n';
    require (identity.getParentDirectory().createDirectory().wasOk() && hypha::storage::writeText (identity, "{\"license\":\"os\"}"),
             "sandbox Kirin OS identity");
    writeLibrary (reference);
   #if JUCE_MAC
    initialiseBlindProductHostApplication();
   #endif
    juce::ScopedJuceInitialiser_GUI init;
    const auto run = [&] (auto sample) {
        LowerAProduct<decltype (sample)> contract (std::move (signal));
        juce::MessageManager::getInstance()->runDispatchLoop();
        return contract.passed ? EXIT_SUCCESS : EXIT_FAILURE;
    };
    return argc == 3 ? run (double {}) : run (float {});
}
