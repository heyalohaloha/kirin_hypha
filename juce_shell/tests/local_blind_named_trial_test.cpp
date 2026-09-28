#include "../src/local_blind/LocalBlindTrial.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

// INV-LC17: the named A/B hears the frozen range by name before Blind. PRE is stimulus 1 and POST
// is 2 at the prepared gain; a replay from before the range plays it again, a seek or a stop never
// fails it, and nothing counts as heard. START BLIND then begins a new anonymous trial of the same
// range: heard, answer and reveal are empty, the hidden assignment is the one drawn at
// preparation, and the first Blind pass waits for the range start.
namespace
{
using namespace hypha::local_blind;

void require (bool ok, const char* message)
{
    if (! ok) { std::cerr << "Named A/B: " << message << '\n'; std::abort(); }
}

// The range is [-64, 192): four 64-frame blocks. POST is 0.25 and PRE -0.125, so PRE at its
// fixed +6 dB (x2) plays -0.25. No loop is reported, so no exact wrap starts a pass.
TrialBlock at (std::int64_t position, bool playing = true)
{
    return { { 1, 2, 3, 4 }, 48000, position, true, playing, true, false, false, 0, 0 };
}

std::unique_ptr<LocalBlindTrial> trial (bool firstIsPre, TrialGain gain = { 2.0f, 1.0f, false })
{
    const TrialFormat format { { 1, 2, 3, 4 }, 48000, 2, -64, 256, 256, true };
    return std::make_unique<LocalBlindTrial> (format, gain, std::vector<float> (512, 0.25f),
                                              std::vector<float> (512, -0.125f), firstIsPre, 512 * 8);
}

struct Buffer
{
    std::array<float, 64> left {}, right {};
    std::array<float*, 2> pointers { left.data(), right.data() };
    // Renders one block of live input 0.8 and returns what the first frame became.
    float play (LocalBlindTrial& t, TrialBlock block)
    {
        left.fill (0.8f);
        right.fill (0.8f);
        t.render (pointers.data(), 2, 64, block);
        return left[10];
    }
};

void playsByNameAndFreely()
{
    auto t = trial (false);
    Buffer b;
    require (! t->startBlind(), "Blind starts from the named A/B only");
    require (t->startNamed() && t->view().named && ! t->startNamed(), "the named A/B starts once from ready");
    require (b.play (*t, at (-128)) == 0.8f, "before the range the live signal plays");
    require (b.play (*t, at (-64)) == -0.25f, "stimulus 1 is PRE at its fixed gain");
    require (t->view().activeStimulus == 1 && t->view().named, "the view names what plays");
    require (t->select (2) && b.play (*t, at (0)) == 0.25f, "POST can be chosen during the pass");
    require (t->select (1) && b.play (*t, at (64)) == -0.25f, "and PRE again");
    b.play (*t, at (128));
    const auto done = t->view();
    require (done.passComplete && ! done.heardOneComplete && ! done.heardTwoComplete && ! done.canAnswer,
             "nothing counts as heard and nothing can be answered");
    require (! t->answer (TrialAnswer::one) && ! t->reveal(), "the named A/B has no answer");
    require (b.play (*t, at (192)) == 0.8f, "after the range the live signal plays");
    require (b.play (*t, at (-128)) == 0.8f && b.play (*t, at (-64)) == -0.25f,
             "a replay from before the range plays the chosen source again");
    require (b.play (*t, at (64)) == 0.8f && t->view().failure == TrialFailure::none,
             "a seek waits for the range start without failing");
    require (b.play (*t, at (-64)) == -0.25f, "the range plays again from its start");
    require (b.play (*t, at (0, false)) == 0.8f && t->view().failure == TrialFailure::none,
             "a stop forgets the pass without failing");
    require (b.play (*t, at (-64)) == -0.25f, "and playing from the start plays it again");
}

void blindAfterNamedIsANewTrial()
{
    for (const bool firstIsPre : { false, true })
    {
        auto t = trial (firstIsPre);
        Buffer b;
        const float one = firstIsPre ? -0.25f : 0.25f, two = firstIsPre ? 0.25f : -0.25f;
        require (t->startNamed(), "named A/B");
        b.play (*t, at (-64));
        require (t->select (2), "POST by name");
        b.play (*t, at (0));
        require (t->startBlind() && ! t->view().named && ! t->startBlind(), "Blind starts once from the named A/B");
        require (b.play (*t, at (64)) == 0.8f, "the first Blind pass waits for the range start");
        require (! t->view().heardOneComplete && t->view().answer == TrialAnswer::none, "heard and answer start empty");
        for (std::int64_t p = -64; p < 192; p += 64)
            require (b.play (*t, at (p)) == one, "Source 1 plays the hidden assignment drawn at preparation");
        require (t->view().heardOneComplete && ! t->view().canAnswer, "one complete pass is counted");
        for (std::int64_t p = -64; p < 192; p += 64)
            require (b.play (*t, at (p)) == two, "Source 2 is armed and plays on the replay");
        require (t->view().canAnswer && t->answer (TrialAnswer::noPreference) && t->reveal()
                     && t->view().revealedOneSide == (firstIsPre ? 1 : 0),
                 "both passes allow the answer, and the reveal names the preparation's assignment");
    }
}

void approvedAttenuationCarriesIntoBlind()
{
    auto t = trial (true, { 2.0f, 0.5f, true });
    Buffer b;
    require (t->view().lowerPostApprovalRequired && ! t->startNamed (false), "lowering POST needs approval");
    require (t->startNamed (true) && b.play (*t, at (-64)) == -0.125f, "approved, PRE plays at its own level");
    require (t->select (2) && b.play (*t, at (0)) == 0.125f, "and POST lowered by the approved amount");
    require (t->startBlind(), "Blind keeps the approval");
    require (b.play (*t, at (64)) == 0.4f, "while Blind waits, the live signal stays lowered");
    require (b.play (*t, at (-64)) == -0.125f, "Blind plays PRE at its own level");
    for (std::int64_t p = 0; p < 192; p += 64)
        b.play (*t, at (p));
    require (b.play (*t, at (192)) == 0.4f && t->view().failure == TrialFailure::none,
             "after the pass the live signal stays lowered");
}
}

void verifyNamedAb()
{
    playsByNameAndFreely();
    blindAfterNamedIsANewTrial();
    approvedAttenuationCarriesIntoBlind();
    std::cout << "local Blind named A/B: all checks passed\n";
}
