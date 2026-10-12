#include "ContentAnalysisProductFixture.h"

namespace
{
using namespace content_analysis_product;
using namespace hypha::live_compare;

void warmup (Fixture& fixture, bool spectrum)
{
    fixture.until ([&] { return fixture.zeroDelta (spectrum); }, "aligned data after warmup");
}

void loseProof (Fixture& fixture, bool spectrum)
{
    fixture.postClock.available = false;
    if (fixture.reaper) fixture.postClock.positionAvailable = false;
    fixture.block(); fixture.noDelta (spectrum);
    fixture.postClock.available = fixture.postClock.positionAvailable = true;
}

void recoverProof (Fixture& fixture, bool spectrum)
{
    loseProof (fixture, spectrum);
    fixture.until ([&] { return fixture.zeroDelta (spectrum); }, "fresh data after proof recovery");
}

void unverifiedCycle (Fixture& fixture, bool spectrum)
{
    int localObservations = 0;
    for (int i = 0; i < 900; ++i)
    {
        fixture.block(); fixture.noDelta (spectrum); fixture.cycleGuidance();
        KirinSpectrumBatch batch {};
        if (spectrum && i >= 80 && fixture.post->pollSpectrumBatch (batch))
        {
            require (batch.latest.post_has_data != 0, "unverified paired AU Cycle keeps POST FREQ");
            ++localObservations;
        }
    }
    require (! spectrum || localObservations > 700, "POST FREQ is sustained during unverified Cycle");
}

void cycleListenRecovery (Fixture& fixture, bool spectrum, bool blind)
{
    auto& post = *fixture.post;
    require (post.startLiveCompare() == StartResult::started, "LISTEN starts while waiting for Cycle proof");
    post.selectLiveComparePre (true);
    for (int i = 0; i < 16; ++i)
    {
        fixture.block(); fixture.noDelta (spectrum); fixture.cycleGuidance();
        require (juce::String (hypha::live_compare_ui::namedRecovery (post.liveCompareStatus()))
            == "Turn Cycle off to compare (POST)", "LISTEN has the same callback's Cycle reason");
    }
    if (blind)
    {
        require (post.beginLiveBlind() == StartResult::started, "BLIND may prepare but not audition an unverified Cycle");
        for (int i = 0; i < 16; ++i)
        {
            fixture.block(); post.serviceLiveBlind(); fixture.noDelta (spectrum); fixture.cycleGuidance();
            const auto state = post.liveBlindStatus();
            require (state.stage == BlindStage::preparing && ! state.trial.active,
                     "initial Cycle wait never starts an anonymous trial");
            require (hypha::live_blind_ui::screenFor (state, true, 1.0f).instruction == "Turn Cycle off to compare",
                     "BLIND preparing has Cycle guidance");
        }
    }
    post.finishLiveCompare();
    fixture.until ([&] { return ! post.liveCompareStatus().finishing; }, "END completes during the Cycle wait");
    fixture.preClock.loop = fixture.postClock.loop = false;
    fixture.until ([&] { return fixture.zeroDelta (spectrum) && ! post.analysisPairingLoopBlocked(); }, "Cycle off restores delta");
    require (post.startLiveCompare() == StartResult::started, "linear LISTEN starts again");
    post.selectLiveComparePre (true); fixture.auditionAllowed = true;
    fixture.until ([&] { return post.liveCompareStatus().preAudible; }, "linear proof restores PRE audition");
    if (blind)
    {
        require (post.beginLiveBlind() == StartResult::started, "BLIND can begin again after Cycle off");
        // MATCH needs at least three seconds of actual input history, not three wall seconds.
        // Its worker still gets wall time via block pacing; no history or receipt is fabricated.
        fixture.until ([&] { post.serviceLiveBlind(); return post.liveBlindStatus().stage == BlindStage::active; },
                       "linear proof and MATCH restore BLIND");
    }
    post.finishLiveCompare();
    fixture.until ([&] { return ! post.liveCompareStatus().finishing; },
                   blind ? "END returns after recovered BLIND" : "END returns after recovered LISTEN");
    fixture.auditionAllowed = false;
}

void run (Fixture& fixture, const juce::String& phase, bool au, bool included)
{
    fixture.until ([&] { return fixture.post->setPairCandidate (fixture.pre->instanceId(), {}); }, "select PRE");
    for (bool spectrum : { true, false })
    {
        std::cerr << (spectrum ? "FREQ" : "SHARP/PSB") << " " << phase << std::endl;
        // Each CTest phase establishes its own prerequisites. Switching demand here still
        // exercises FREQ -> SHARP in one session on the real PRE/POST workers.
        fixture.preClock.loop = fixture.postClock.loop = false;
        hypha::analysis::Demand demand;
        demand.kind = spectrum ? hypha::analysis::Kind::spectrum : hypha::analysis::Kind::sharpDelta;
        require (fixture.post->setAnalysisDemand (fixture.owner, demand), "demand accepted");
        warmup (fixture, spectrum);
        if (phase == "linear") fixture.verifyAvailability (spectrum);
        else if (phase == "recovery")
        {
            if (au)
            {
                fixture.preClock.projectWitness = false;
                for (int i = 0; i < 24; ++i) { fixture.block(); fixture.noDelta (spectrum); }
                fixture.preClock.projectWitness = true;
                fixture.until ([&] { return fixture.zeroDelta (spectrum); }, "independent project witness recovers");
            }
            recoverProof (fixture, spectrum);
        }
        else
        {
            if (au)
            {
                // An initial AU Cycle must not borrow a previously linear occurrence proof.
                loseProof (fixture, spectrum);
            }
            fixture.preClock.loop = fixture.postClock.loop = true;
            if (phase == "cycle_listen" || phase == "cycle_blind")
                cycleListenRecovery (fixture, spectrum, phase == "cycle_blind");
            else if (au && ! included) unverifiedCycle (fixture, spectrum);
            else
            {
                warmup (fixture, spectrum);
                fixture.verifyAvailability (spectrum);
                recoverProof (fixture, spectrum);
            }
        }
    }
}
}

int main (int argc, char** argv)
try
{
    require (argc == 5, "explicit layout, latency scenario, host and phase are required");
    const juce::String layout (argv[1]), scenario (argv[2]), host (argv[3]), phase (argv[4]);
    require (layout == "mono" || layout == "stereo", "unknown layout");
    require (scenario == "missing" || scenario == "omitted" || scenario == "included" || scenario == "zero", "unknown latency scenario");
    require (host == "content" || host == "au" || host == "reaper", "unknown host");
    require (host != "reaper" || scenario == "missing", "REAPER has no presentation report");
    const bool cycle = phase == "cycle_listen" || phase == "cycle_blind";
    require (phase == "linear" || phase == "loop" || phase == "recovery" || cycle, "unknown phase");
    require (! cycle || (host == "au" && scenario != "included"), "Cycle guidance needs unverified AU timing");
    ReferenceValidationStorageSandbox sandbox;
    hypha::pre_display::Controller::placeUnderForTest (sandbox.directory());
    juce::ScopedJuceInitialiser_GUI initialise;
    Fixture fixture (layout == "mono" ? 1 : 2, scenario, host == "au", host == "reaper");
    run (fixture, phase, host == "au", scenario == "included");
    std::cout << "Content analysis product: " << layout << " " << scenario << " " << host << " " << phase << " PASS\n";
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
