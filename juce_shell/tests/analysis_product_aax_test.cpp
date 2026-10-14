#include "AaxAnalysisProductFixture.h"

using namespace aax_analysis_product;

namespace
{
void certified (Fixture& fixture)
{
    // The measured (certified) host build. LIVE and the absolute PSB live on the project timeline
    // on every AAX build: an engine clock gap does not touch them.
    fixture.demand (hypha::analysis::Kind::liveAbsolute);
    fixture.preparingSoon (false);
    fixture.until ([&] { return fixture.absoluteActive(); }, "LIVE and absolute PSB use the project timeline without output latency");
    fixture.postClock.available = false;
    fixture.throughout (4, [&] { return fixture.absoluteActive(); }, "LIVE keeps the project timeline through an engine clock gap");
    fixture.postClock.available = true;
    fixture.until ([&] { return fixture.post->setPairCandidate (fixture.pre->instanceId(), {}); }, "exact PRE is selected");
    fixture.demand (hypha::analysis::Kind::sharpDelta);
    fixture.until ([&] { return fixture.sharpActive(); }, "certified SHARP and delta PSB must use the proven nonzero K");
    fixture.postClock.available = false;
    fixture.block(); fixture.preparing (true);
    fixture.postClock.available = true;
    fixture.until ([&] { return fixture.sharpActive(); }, "fresh proof reacquires analysis");
    fixture.preClock.project += 4096; fixture.postClock.project += 4096;
    fixture.sharpReproves ("seek renews the proof before SHARP returns");
    fixture.post->kirinHostDelayCompensationStateChanged (false);
    fixture.block(); fixture.preparing (true);
    fixture.post->kirinHostDelayCompensationStateChanged (true);
    fixture.until ([&] { return fixture.sharpActive(); }, "compensation ON renews the pair proof");
    fixture.block (true); fixture.preparing (true);
    fixture.until ([&] { return fixture.sharpActive(); }, "other output owner leaving renews the pair proof");
}

void uncertified (Fixture& fixture)
{
    // An AAX host that is not Pro Tools: no certificate, no documented bound. SHARP and the delta
    // PSB use the same linear PRE/POST proof as LISTEN; they are never refused as missing output
    // latency.
    fixture.certified (false);
    fixture.sharpReproves ("an AAX host that is not Pro Tools proves SHARP through LISTEN's linear K");
    require (! fixture.sharpRefused(), "an AAX host that is not Pro Tools is not a missing-latency host");
    require (! fixture.post->analysisPairingLoopBlocked(), "linear playback asks for no loop change");
    fixture.loopGuidance (false);
    fixture.preClock.project += 4096; fixture.postClock.project += 4096;
    fixture.sharpReproves ("not Pro Tools: seek renews the proof before SHARP returns");
    fixture.post->kirinHostDelayCompensationStateChanged (false);
    fixture.block(); fixture.preparing (true);
    fixture.post->kirinHostDelayCompensationStateChanged (true);
    fixture.until ([&] { return fixture.sharpActive(); }, "not Pro Tools: compensation ON renews the pair proof");
    fixture.playing (false);
    fixture.block(); fixture.preparing (true);
    fixture.playing (true);
    fixture.until ([&] { return fixture.sharpActive(); }, "not Pro Tools: play after stop renews the pair proof");
    fixture.postClock.available = false;
    fixture.block(); fixture.preparing (true);
    fixture.postClock.available = true;
    fixture.until ([&] { return fixture.sharpActive(); }, "not Pro Tools: an engine clock gap revokes SHARP until proven again");

    // The host's loop-playback setting with no selection names no loop: playback is linear.
    fixture.loopPlayback (true);
    fixture.throughout (40, [&] { return fixture.sharpActive() && ! fixture.post->analysisPairingLoopBlocked(); },
                        "loop playback without a range is linear playback");
    // A looped range needs Pro Tools' documented bound; here only the guidance appears while
    // POST waits, even though the host really wraps both sides.
    fixture.loopRange (2.0);
    fixture.block(); fixture.preparing (true);
    fixture.throughout (120, [&] { return ! fixture.sharpActive() && fixture.analysisPreparing (true)
                                       && fixture.post->analysisPairingLoopBlocked(); },
                        "an AAX host that is not Pro Tools looping a range waits with loop guidance and no difference");
    fixture.loopGuidance (true);
    fixture.demand (hypha::analysis::Kind::liveAbsolute);
    fixture.until ([&] { return fixture.absoluteActive(); }, "LIVE and absolute PSB ignore the loop setting");
    KirinPsbView psb {};
    require (fixture.post->pollPsb (psb) && psb.is_delta == 0, "project PSB contains only POST facts");
    fixture.demand (hypha::analysis::Kind::sharpDelta);
    fixture.loopPlayback (false);
    fixture.until ([&] { return fixture.sharpActive() && ! fixture.post->analysisPairingLoopBlocked(); },
                   "loop playback off restores the linear proof and clears the guidance");
    fixture.loopGuidance (false);
}

void loop (Fixture& fixture)
{
    // A regular Pro Tools build (certified by the version floor, not the measured one): the
    // linear proof as before, and in a looped range the bounded initial-loop K, so SHARP, the
    // delta PSB and LISTEN all proceed while the host wraps both sides. No loop guidance appears.
    fixture.certified (true, "26.4.1.179");
    fixture.sharpReproves ("a regular Pro Tools build proves SHARP through the linear K");
    // Pro Tools wraps the transport once per cycle for the whole chain, so POST's compensated
    // position lies before the loop start for K samples after every wrap. Two beats are 187
    // callbacks: 400 callbacks cross at least two wraps.
    fixture.compensatedPositionWrapsWithEngine = true;
    fixture.loopRange (2.0);
    fixture.until ([&] { return fixture.sharpActive(); },
                   "a regular Pro Tools build proves SHARP and the delta PSB through a looped range with K");
    fixture.throughout (400, [&] { return fixture.sharpActive() && ! fixture.post->analysisPairingLoopBlocked(); },
                        "SHARP stays proven and exact across the loop wraps without asking for loop playback off");
    fixture.loopGuidance (false);
    require (fixture.post->startLiveCompare() == hypha::live_compare::StartResult::started,
             "LISTEN starts on a regular Pro Tools build during loop playback");
    fixture.post->selectLiveComparePre (true);
    fixture.until ([&] { const auto status = fixture.post->liveCompareStatus();
                         return status.active && status.preSelected && status.preAudible
                             && status.verdict == hypha::live_compare::Verdict::accepted; },
                   "LISTEN plays PRE through a looped range on a regular Pro Tools build");
    fixture.throughout (400, [&] { const auto status = fixture.post->liveCompareStatus();
                                   return status.preAudible && ! status.contentHeld && status.loopBlock == hypha::live_compare::LoopPairingBlock::none; },
                        "PRE stays audible across the loop wraps without a hold or loop guidance");
    fixture.post->stopLiveCompare();
    fixture.loopPlayback (false);
    fixture.until ([&] { return fixture.sharpActive(); }, "a regular Pro Tools build returns to the linear proof");
}

void short_loops (Fixture& fixture)
{
    fixture.compensatedPositionWrapsWithEngine = true;
    // A loop no longer than the compensation bound (0.6 beats are 14400 samples, below 16383):
    // a K already proven carries through it exactly, but once lost it cannot be proven again
    // there, so SHARP waits with no difference rather than pairing a wrong lap.
    fixture.loopRange (0.6);
    fixture.until ([&] { return fixture.sharpActive(); }, "a known K is corroborated by the first wrap of a loop no longer than the bound");
    fixture.throughout (200, [&] { return fixture.sharpActive(); },
                        "a known K carries exactly through a loop no longer than the bound");
    fixture.postClock.available = false;
    fixture.block(); fixture.preparing (true);
    fixture.postClock.available = true;
    fixture.throughout (200, [&] { return ! fixture.sharpActive() && fixture.analysisPreparing (true); },
                        "a loop no longer than the compensation bound proves no new K: no difference, no wrong lap");
    fixture.loopPlayback (false);
    fixture.until ([&] { return fixture.sharpActive(); }, "linear playback after a short loop proves SHARP again");
    // A loop just longer than the bound (0.71 beats are 17040 samples) proves a new K with K near
    // a quarter of the loop, and pairs the right lap across its frequent wraps.
    fixture.loopRange (0.71);
    fixture.postClock.available = false;
    fixture.block(); fixture.preparing (true);
    fixture.postClock.available = true;
    fixture.until ([&] { return fixture.sharpActive(); }, "a loop just longer than the bound proves a new K with K");
    fixture.throughout (300, [&] { return fixture.sharpActive(); }, "the short loop stays exact across its wraps");
    fixture.loopPlayback (false);
    fixture.until ([&] { return fixture.sharpActive(); }, "linear playback after the short loop proves SHARP again");
}

void per_insert (Fixture& fixture)
{
    // A host that wraps each insert's compensated position at the loop end proves the same way.
    fixture.compensatedPositionWrapsWithEngine = false;
    fixture.postClock.project = fixture.preClock.project - delay;
    fixture.loopRange (2.0);
    fixture.until ([&] { return fixture.sharpActive(); }, "a per-insert wrap proves SHARP through a looped range with K");
    fixture.throughout (400, [&] { return fixture.sharpActive(); }, "a per-insert wrap stays exact across the loop wraps");
    fixture.loopPlayback (false);
    fixture.until ([&] { return fixture.sharpActive(); }, "linear playback after a per-insert wrap proves SHARP again");
}

void measured_loop (Fixture& fixture)
{
    // The measured build in the same looped range proves the same way.
    fixture.certified (true);
    fixture.compensatedPositionWrapsWithEngine = true;
    fixture.loopRange (2.0);
    fixture.until ([&] { return fixture.sharpActive(); }, "the measured build proves SHARP through a looped range");
    fixture.throughout (400, [&] { return fixture.sharpActive() && ! fixture.post->analysisPairingLoopBlocked(); },
                        "a certified host never asks to turn loop playback off");
    fixture.loopGuidance (false);
}

}
int main (int argc, char** argv)
try
{
    juce::ScopedJuceInitialiser_GUI initialise;
    require (std::getenv ("KIRIN_HYPHA_TEST_STORAGE_ROOT") != nullptr, "isolated fixture storage is required");
    require (argc == 3, "channel layout and scenario must be explicit");
    const juce::String layout (argv[1]), scenario (argv[2]);
    require (layout == "mono" || layout == "stereo", "supported channel layout");
    void (*run) (Fixture&) = nullptr;
    if (scenario == "certified") run = certified;
    if (scenario == "uncertified") run = uncertified;
    if (scenario == "loop") run = loop;
    if (scenario == "short_loops") run = short_loops;
    if (scenario == "per_insert") run = per_insert;
    if (scenario == "measured_loop") run = measured_loop;
    require (run != nullptr, "supported isolated scenario");
    Fixture fixture (layout == "mono" ? 1 : 2);
    if (scenario == "loop") fixture.certified (false);
    if (scenario == "short_loops" || scenario == "per_insert" || scenario == "measured_loop")
        fixture.certified (true, "26.4.1.179");
    if (scenario != "certified")
    {
        // Each scenario begins with a real selected PRE and a proven nonzero K. Later changes
        // still revoke and reacquire that proof, exactly as in the uninterrupted scenario.
        fixture.until ([&] { return fixture.post->setPairCandidate (fixture.pre->instanceId(), {}); },
                       "exact PRE is selected");
        fixture.demand (hypha::analysis::Kind::sharpDelta);
        fixture.until ([&] { return fixture.sharpActive(); }, "initial nonzero K is proven");
    }
    run (fixture);
    std::cout << "AAX analysis product: PASS (" << layout << ", " << scenario << ")\n";
}
catch (const std::exception& error)
{
    std::cerr << "AAX analysis: " << error.what() << '\n';
    return 1;
}
