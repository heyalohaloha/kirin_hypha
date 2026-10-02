#include "../src/PluginProcessor.h"
#include "ValidationStorageSandbox.h"
#include "LiveTimingFixtureAccess.h"
#include "reference_rt_probe.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
static void require (bool ok, const char* why)
{ if (! ok) { std::cerr << "named recovery product: " << why << '\n'; std::exit (EXIT_FAILURE); } }
#include "LocalBlindSignalFixture.h"
#if JUCE_MAC
void initialiseBlindProductHostApplication();
#endif
namespace
{
using Processor = KirinHyphaProcessorBase;
using namespace hypha::live_compare;
using Steady = std::chrono::steady_clock;
constexpr int frames = 512, delay = 4096, length = 24000;
struct Clock final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info; info.setIsPlaying (playing);
        const auto project = (position + shift) % length;
        info.setTimeInSamples (project); info.setTimeInSeconds (project / 48000.0);
        info.setIsLooping (true); info.setBpm (120); info.setPpqPosition (project / 24000.0);
        info.setLoopPoints (LoopPoints { 0, 1 });
        info.setKirinAuxiliaryClockSource (1);
        if (! missing) info.setKirinAuxiliaryClockSamples (position);
        info.setKirinPresentationLatencySource (1);
        info.setKirinOutputPresentationLatencySamples (presentation);
        return info;
    }
    std::int64_t position = 0, shift = 0;
    bool playing = false, missing = false;
    int presentation = 0;
};
class Recovery final : private juce::Timer
{
public:
    Recovery (std::vector<float> input, std::string scenario) : signal (std::move (input)), mode (std::move (scenario))
    {
        preClock.presentation = delay;
        for (auto role : { Processor::Role::Pre, Processor::Role::Post })
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
            auto instance = std::make_unique<Processor> (role);
            auto layout = instance->getBusesLayout();
            layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
            layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
            require (instance->setBusesLayout (layout), "actual processors negotiate stereo");
            instance->setMeterContextPreference (hypha::meter_context::MeterContext::twoMix, false);
            instance->setPlayHead (role == Processor::Role::Pre ? &preClock : &postClock);
            instance->prepareToPlay (48000, frames);
            require (LiveTimingFixtureAccess::configureStudioProClock (*instance), "exact existing qualified clock policy");
            (role == Processor::Role::Pre ? pre : post) = std::move (instance);
        }
        if (mode == "pair-set")
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
            alternate = std::make_unique<Processor> (Processor::Role::Pre);
            require (alternate->setBusesLayout (pre->getBusesLayout()), "alternate actual PRE negotiates stereo");
            alternate->setPlayHead (&preClock); alternate->prepareToPlay (48000, frames);
            require (LiveTimingFixtureAccess::configureStudioProClock (*alternate), "alternate has the same exact clock policy");
        }
        audio = std::thread ([this] { processAudio(); }); startTimer (20);
    }
    ~Recovery() override
    { stopTimer(); running.store (false); audio.join(); pre->releaseResources(); post->releaseResources(); if (alternate) alternate->releaseResources(); }
    bool passed = false;
private:
    template <typename Predicate> void awaitAudio (Predicate&& predicate)
    {
        const auto deadline = Steady::now() + std::chrono::milliseconds (350);
        while (! predicate())
        { require (Steady::now() < deadline, "bounded callback handoff"); std::this_thread::sleep_for (std::chrono::milliseconds (1)); }
    }
    void fault()
    {
        pause.store (true); awaitAudio ([this] { return paused.load(); });
        oldPlan = planMatch (post->measureLiveCompare(), 0.0);
        if (mode == "stop" || mode == "blind-stop" || (mode == "failure-named" && restarted && transportRound == 0)) playing.store (false);
        else if (mode == "seek" || oddMode() || mode == "preparing-seek" || (mode == "failure-named" && restarted)) seek.store (true);
        else if (mode == "clock" || mode == "clock-blind" || mode == "preparing-clock" || mode == "failure-named") clockHole.store (true);
        else if (mode == "compensation" || mode == "preparing-dc") post->kirinHostDelayCompensationStateChanged (false);
        else if (mode == "preparing-content") post->holdLiveCompareForContentJump (3000);
        else if (mode == "restore" || mode == "restore-blind")
        { juce::MemoryBlock saved; post->getStateInformation (saved); post->setStateInformation (saved.getData(), static_cast<int> (saved.getSize())); }
        else if (mode == "end") post->finishLiveCompare();
        else if (mode == "pair-clear" || mode == "pair-set")
        {
            if (mode == "pair-clear") post->clearPairCandidate();
            else require (post->setPairCandidate (alternate->instanceId(), {}), "real alternate PRE selected");
            juce::AudioBuffer<float> first (2, frames); juce::MidiBuffer midi;
            const auto held = post->liveCompareStatus().postActual;
            for (int c = 0; c < 2; ++c) for (int f = 0; f < frames; ++f) first.setSample (c, f, (f + c + 1) / 32768.0f);
            beginReferenceRtProbe(); post->processBlock (first, midi); heap.fetch_add (endReferenceRtProbe());
            for (int c = 0; c < 2; ++c) for (int f = 0; f < frames; ++f)
                require (juce::exactlyEqual (first.getSample (c, f), (f + c + 1) / 32768.0f * held), "first RT block before service is exact held POST after pair mutation");
            require (! post->applyLiveCompareMatch (oldPlan, MatchChoice::basis)
                && ! post->selectLiveBlind (1) && ! post->revealLiveBlind(), "pair mutation immediately rejects old approvals/trials");
        }
        faultBlocks = blocks.load();
        if (oddMode()) oddStart.store (faultBlocks + (mode == "seek-odd" ? 0 : 1));
        pause.store (false); checkpoint = Steady::now();
    }
    void timerCallback() override
    {
        const auto now = Steady::now();
        require (now - began < std::chrono::seconds (45), "scenario deadline");
        require (pcmErrors.load() == 0 && heap.load() == 0, "all eligible PCM exact and RT heap zero");
        post->serviceLiveCompare();
        if (mode == "blind-stop" || mode == "clock-blind" || mode.rfind ("preparing-", 0) == 0
            || mode == "reuse" || mode == "pending-end" || mode == "restore-blind" || mode == "failure-named") post->serviceLiveBlind();
        const auto status = post->liveCompareStatus();
        switch (stage)
        {
            case 0:
            {
                if (pre->instanceId().isEmpty()) break;
                if (! preview) preview = post->createPairPreview();
                if (! demanded)
                { if (! hypha::pair_preview::request (preview)) break; demanded = true; checkpoint = now; }
                KirinPairPreviewValue value {};
                if (! kirin_hypha_pair_preview_poll (preview.get(), &value) || ! value.complete
                    || (! value.has_single && mode != "pair-set"))
                { if (now - checkpoint > std::chrono::milliseconds (1050) && hypha::pair_preview::request (preview)) checkpoint = now; break; }
                require (post->setPairCandidate (pre->instanceId(), {}), "explicit actual PRE pair"); preview.reset(); stage = 1; break;
            }
            case 1:
                if (post->pairStatus() != KIRIN_PAIR_STATUS_PAIRED) break;
                playing.store (true);
                if (mode.rfind ("preparing-", 0) == 0 || mode == "failure-named")
                {
                    require (post->beginLiveBlind() == StartResult::started, "direct Blind begins before its first timing proof");
                    if (mode == "preparing-stop") { playing.store (false); faultBlocks = blocks.load(); stage = 11; }
                    else stage = 10;
                    break;
                }
                require (post->startLiveCompare() == StartResult::started, "named initial LOOP session starts");
                post->selectLiveComparePre (true); stage = 2; break;
            case 2:
            {
                const auto measured = post->measureLiveCompare();
                if (! measured.ok()) break;
                require (post->applyLiveCompareMatch (planMatch (measured, 0.0), MatchChoice::basis), "one real measured MATCH");
                stage = 3; break;
            }
            case 3:
                if (! status.matchReady || ! status.preAudible) break;
                fixedGain = status.gain; fixedPost = status.postTarget;
                if (mode == "blind-stop" || mode == "reuse")
                { require (post->beginLiveBlind() == StartResult::started, "same MATCH enters a fresh Blind admission"); checkpoint = now; stage = 7; break; }
                if (mode == "pending-end" && ! restarted)
                {
                    pause.store (true); awaitAudio ([this] { return paused.load(); });
                    require (post->beginLiveBlind() == StartResult::started, "fresh timing request can be pending before its first callback");
                    const auto pending = LiveTimingFixtureAccess::timingAdmission (*post);
                    require (pending.request != pending.receipt, "the new trial has no old RT admission receipt");
                    post->finishLiveCompare();
                    const auto canceled = LiveTimingFixtureAccess::timingAdmission (*post);
                    require (canceled.request != pending.request && canceled.authority == UINT64_MAX,
                             "END cancels the pending timing request immediately, before any callback");
                    pause.store (false); stage = 9; break;
                }
                fault(); stage = 4; break;
            case 4:
                if (blocks.load() < faultBlocks + 4) break;
                require (! post->applyLiveCompareMatch (oldPlan, MatchChoice::basis), "pre-fault measurement plan cannot apply after loss");
                if (mode == "stop" || mode == "blind-stop" || (mode == "failure-named" && restarted && transportRound == 0))
                { require (! status.matched, "stop revokes current MATCH proof"); playing.store (true); }
                if (mode == "clock" || mode == "clock-blind")
                {
                    require (status.active && status.interrupted && ! status.preSelected && status.matchHeld,
                             "unexplained clock hole seals PRE while retaining fixed gain");
                    require (status.reason == RecoveryReason::clockMissing, "PRE clock loss remains the exact cause even with valid POST flags");
                    checkpoint = now; stage = 8; break;
                }
                if (mode == "compensation")
                {
                    require (status.preSelected && ! status.preAudible && status.compensationOff,
                             "known DC OFF holds PRE selection but outputs POST");
                    post->kirinHostDelayCompensationStateChanged (true);
                }
                checkpoint = now; stage = 5; break;
            case 5:
                if (mode == "stop" || mode == "seek" || oddMode() || mode == "clock" || mode == "compensation"
                    || (mode == "failure-named" && restarted))
                {
                    if (! status.matchReady || ! status.preAudible) break;
                    require (juce::exactlyEqual (status.gain, fixedGain) && juce::exactlyEqual (status.postTarget, fixedPost), "fresh proof restores the exact approved MATCH without remeasurement");
                    checkpoint = now; stage = 6; break;
                }
                if (mode == "clock-blind")
                {
                    if (post->liveBlindStatus().stage != BlindStage::active) break;
                    require (status.matchReady && juce::exactlyEqual (status.gain, fixedGain) && juce::exactlyEqual (status.postTarget, fixedPost),
                             "one new explicit BLIND admission proves timing and keeps only the same approved tuple");
                    require (post->liveBlindStatus().trial.epoch != oldBlindEpoch, "fresh admission owns a new CSPRNG trial epoch");
                    post->finishLiveCompare(); stage = 9; break;
                }
                if (now - checkpoint < std::chrono::milliseconds (500)) break;
                require (! status.active && ! status.preAudible && ! status.matched, "END/restore/Blind stop never renews a session");
                require (! post->revealLiveBlind() && ! post->selectLiveBlind (1), "old Blind receipts and controls remain revoked");
                if (mode.rfind ("preparing-", 0) == 0 || mode == "failure-named")
                    require (post->liveBlindStatus().stage == BlindStage::invalidated
                        && post->liveBlindStatus().reason == preparationLoss(),
                        "established preparation retains its exact first cause and ends, not an endless preparing screen");
                if (mode == "restore-blind")
                {
                    require (post->beginLiveBlind() == StartResult::started, "restore permits only a new explicit trial with fresh authority/timing");
                    restarted = true; stage = 12; break;
                }
                post->finishLiveCompare(); stage = 9; break;
            case 6:
                if (now - checkpoint < std::chrono::seconds (1)) break;
                require (status.matchReady && juce::exactlyEqual (status.gain, fixedGain), "renewed fixed MATCH stays ready through further wraps");
                if (mode == "proof-odd") require (oddProofSeen.load(), "fresh timing adoption actually overlapped an odd writer");
                if (mode == "failure-named" && transportRound == 0)
                { ++transportRound; fault(); stage = 4; break; }
                post->finishLiveCompare(); stage = 9; break;
            case 7:
            {
                if (post->liveBlindStatus().stage != BlindStage::active) break;
                if (mode == "reuse")
                {
                    require (now - checkpoint < std::chrono::seconds (1), "same MATCH enters Blind on fresh timing without a new observation window");
                    require (status.matchReady && juce::exactlyEqual (status.gain, fixedGain) && juce::exactlyEqual (status.postTarget, fixedPost),
                             "first current coherent RT receipt reuses the exact approved tuple");
                    post->finishLiveCompare(); stage = 9; break;
                }
                const auto command = LiveTimingFixtureAccess::command (*post);
                if (! status.preAudible) { require (post->selectLiveBlind (command.firstPre() ? 1 : 2), "fixture selects anonymous PRE"); break; }
                fault(); stage = 4; break;
            }
            case 8:
                if (now - checkpoint < std::chrono::milliseconds (500)) break;
                require (! status.preAudible && ! status.matched && status.matchHeld, "new clocks alone cannot revive unexplained loss");
                if (mode == "clock-blind")
                {
                    oldBlindEpoch = post->liveBlindStatus().trial.epoch;
                    require (post->beginLiveBlind() == StartResult::started, "one explicit new BLIND starts fresh timing after unknown interruption");
                }
                else post->selectLiveComparePre (true);
                stage = 5; break;
            case 9:
                if (status.finishing) break;
                require (! status.active && status.postActual == 1.0f, "actual END receipt returns ordinary unity POST");
                if (! restarted && (mode == "pending-end" || mode == "failure-named"))
                {
                    restarted = true;
                    if (mode == "pending-end")
                    { require (post->beginLiveBlind() == StartResult::started, "new explicit trial after canceled request begins cleanly"); stage = 12; }
                    else
                    { require (post->startLiveCompare() == StartResult::started, "new named LISTEN ignores only closed Blind failure history"); post->selectLiveComparePre (true); stage = 2; }
                    break;
                }
                post->selectLiveComparePre (true);
                require (! post->liveCompareStatus().preSelected, "stale PRE cannot revive END");
                std::cout << "Named recovery product: PASS " << mode << " fullFrames=" << verifiedFrames.load() << " heap=" << heap.load() << '\n';
                passed = true; stopTimer(); juce::MessageManager::getInstance()->stopDispatchLoop(); break;
            case 10:
                require (post->liveBlindStatus().stage == BlindStage::preparing, "causal fault occurs before MATCH or anonymous trial");
                if (status.verdict != Verdict::accepted) break;
                require (! status.matched, "initial timing is established but MATCH observation is not yet complete");
                fault(); stage = 4; break;
            case 11:
                if (blocks.load() < faultBlocks + 4) break;
                require (post->liveBlindStatus().stage == BlindStage::preparing && status.active,
                         "startup STOP with no established timing remains a recoverable wait");
                playing.store (true); stage = 12; break;
            case 12:
                if (post->liveBlindStatus().stage != BlindStage::active) break;
                require (status.matchReady, "startup wait later enters a newly verified trial without another click");
                require (LiveTimingFixtureAccess::timingAdmission (*post).request == LiveTimingFixtureAccess::timingAdmission (*post).receipt,
                         "new trial owns the actual fresh timing request receipt");
                post->finishLiveCompare(); stage = 9; break;
        }
    }
    float source (std::int64_t frame, int channel) const
    {
        if (frame < 0) return 0;
        const auto token = static_cast<std::uint32_t> (frame + 1) * 0x9e3779b1u;
        return signal[static_cast<std::size_t> (frame) % signal.size()] * 0.1f
            + static_cast<float> (((token >> (channel == 0 ? 0 : 16)) & 0xffffu)) / 1048576.0f;
    }
    bool oddMode() const { return mode == "seek-odd" || mode == "pending-odd" || mode == "proof-odd"; }
    RecoveryReason preparationLoss() const
    {
        if (mode == "preparing-dc") return RecoveryReason::compensationOff;
        if (mode == "preparing-content") return RecoveryReason::contentChanged;
        if (mode == "preparing-seek") return RecoveryReason::positionChanged;
        return RecoveryReason::clockMissing;
    }
    void processAudio()
    {
        juce::AudioBuffer<float> buffer (2, frames); juce::MidiBuffer midi;
        std::array<std::array<float, delay>, 2> physical {}; std::size_t head = 0;
        std::int64_t emitted = 0, shift = 0;
        std::optional<GainUpdate> oddWriter;
        auto next = Steady::now();
        while (running.load())
        {
            if (pause.load())
            { paused.store (true); std::this_thread::sleep_for (std::chrono::milliseconds (1)); next = Steady::now(); continue; }
            paused.store (false);
            if (seek.exchange (false)) shift += 1234;
            preClock.shift = postClock.shift = shift;
            preClock.playing = postClock.playing = playing.load();
            preClock.position = emitted; postClock.position = emitted - delay;
            preClock.missing = clockHole.exchange (false);
            for (int c = 0; c < 2; ++c) for (int f = 0; f < frames; ++f) buffer.setSample (c, f, source (emitted + f, c));
            beginReferenceRtProbe();
            if (alternate) alternate->processBlock (buffer, midi);
            pre->processBlock (buffer, midi); heap.fetch_add (endReferenceRtProbe());
            for (int f = 0; f < frames; ++f)
            {
                for (int c = 0; c < 2; ++c)
                { auto& delayed = physical[static_cast<std::size_t> (c)][head]; const auto input = buffer.getSample (c, f); buffer.setSample (c, f, delayed * 0.5f); delayed = input; }
                head = (head + 1) % delay;
            }
            const auto before = LiveTimingFixtureAccess::audioView (*post);
            const auto at = blocks.load();
            if (at == oddStart.load())
            { oddWriter.emplace (LiveTimingFixtureAccess::gainRevision (*post)); require (static_cast<bool> (*oddWriter), "fixture opens one bounded odd writer"); }
            beginReferenceRtProbe(); post->processBlock (buffer, midi); heap.fetch_add (endReferenceRtProbe());
            const auto after = LiveTimingFixtureAccess::audioView (*post);
            if (oddWriter)
            {
                require (! after.preAudible && ! after.matchReady, "odd approval holds POST actual and publishes no receipt");
                if (mode == "proof-odd" && after.verdict == Verdict::accepted) oddProofSeen.store (true);
                if ((mode != "proof-odd" && at >= oddStart.load() + 2) || oddProofSeen.load()) oddWriter.reset();
            }
            const bool exactPre = before.preAudible && after.preAudible && before.matchReady && after.matchReady
                && juce::exactlyEqual (before.gain, after.gain) && after.verdict == Verdict::accepted;
            const bool exactPost = ! before.preAudible && ! after.preAudible && juce::exactlyEqual (before.postActual, after.postActual);
            if (exactPre || exactPost)
            {
                for (int c = 0; c < 2; ++c) for (int f = 0; f < frames; ++f)
                {
                    const auto expected = source (emitted - delay + f, c) * (exactPre ? before.gain : 0.5f * before.postActual);
                    if (! juce::exactlyEqual (buffer.getSample (c, f), expected)) pcmErrors.fetch_add (1);
                }
                verifiedFrames.fetch_add (frames * 2);
            }
            emitted += frames; blocks.fetch_add (1);
            next += std::chrono::nanoseconds (static_cast<long long> (frames) * 1'000'000'000 / 48000);
            std::this_thread::sleep_until (next);
        }
    }
    std::vector<float> signal; std::string mode;
    Clock preClock, postClock; std::unique_ptr<Processor> pre, post, alternate;
    std::thread audio; hypha::pair_preview::Ticket preview; MatchPlan oldPlan;
    std::atomic<bool> running { true }, playing { false }, pause { false }, paused { false }, seek { false }, clockHole { false };
    std::atomic<int> blocks { 0 }, pcmErrors { 0 }; std::atomic<unsigned int> heap { 0 };
    std::atomic<int> oddStart { -1 }; std::atomic<bool> oddProofSeen { false };
    std::atomic<std::uint64_t> verifiedFrames { 0 };
    Steady::time_point began = Steady::now(), checkpoint;
    bool demanded = false, restarted = false; int stage = 0, faultBlocks = 0, transportRound = 0;
    float fixedGain = 1.0f, fixedPost = 1.0f;
    std::uint32_t oldBlindEpoch = 0;
};
}
int main (int argc, char** argv)
{
    require (argc == 3, "reentry-product S-1.wav stop|seek|clock|restore|end|blind-stop|compensation");
    auto signal = readFixture (argv[1]); ValidationStorageSandbox sandbox;
   #if JUCE_MAC
    initialiseBlindProductHostApplication();
   #endif
    juce::ScopedJuceInitialiser_GUI init;
    Recovery contract (std::move (signal), argv[2]); juce::MessageManager::getInstance()->runDispatchLoop();
    return contract.passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
