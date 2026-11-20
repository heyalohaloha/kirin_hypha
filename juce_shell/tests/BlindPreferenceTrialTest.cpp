#include "HyphaBlindPreferenceTrial.h"
#include <iostream>
#include <stdexcept>
#include <limits>
namespace bp = hypha::blind_preference;
namespace
{
void require (bool ok, const char* reason) { if (!ok) throw std::runtime_error (reason); }
void statistics()
{
    require(bp::normaliseRounds(0)==1 && bp::normaliseRounds(1)==1 && bp::normaliseRounds(5)==5
        && bp::normaliseRounds(10)==10 && bp::normaliseRounds(9)==1,"round setting defaults to legacy one listen");
    struct Case { unsigned n, k, numerator, rounded; bool belowOne; };
    const Case cases[] {
        {5,5,2,6,false}, {5,4,12,38,false}, {5,3,32,100,false},
        {10,10,2,0,true}, {10,9,22,2,false}, {10,8,112,11,false},
        {10,7,352,34,false}, {10,6,772,75,false}, {10,5,1024,100,false}
    };
    for (const auto& c : cases) {
        for (bool swapped : {false, true}) {
            const auto p = bp::probability (swapped ? c.n-c.k : c.k, swapped ? c.k : c.n-c.k);
            require (p.available && p.numerator == c.numerator && p.denominator == (1u << c.n), "exact binomial table");
            require (p.roundedPerHundred() == c.rounded && p.lessThanOneInHundred() == c.belowOne, "specified display rounding");
        }
    }
    require (!bp::probability (0,0).available && !bp::probability (1,0).available, "omit probability before two answers");
    require (!bp::probability (11,0).available && !bp::probability (6,5).available, "reject unsupported counts");
    for (unsigned n = 2; n <= 10; ++n) for (unsigned a = 0; a <= n; ++a) {
        // Independently enumerate every fair binary outcome, counting both tails.
        const auto k = a > n-a ? a : n-a;
        unsigned extreme = 0;
        for (unsigned bits = 0; bits < (1u << n); ++bits) {
            unsigned ones = 0;
            for (unsigned j = 0; j < n; ++j) ones += (bits >> j) & 1u;
            if (ones >= k || n-ones >= k) ++extreme;
        }
        const auto p = bp::probability (a,n-a);
        require (p.available && p.numerator == extreme && p.denominator == (1u << n), "exhaustive exact tails including ties");
    }
}
void rounds (int target)
{
    bp::Trial trial;
    require (trial.begin (target, 42, 100), "begin supported round count");
    // Includes repeated mappings: independent input must not be forced to alternate.
    const bool mapping[] {true,true,false,true,false,false,false,true,true,false};
    unsigned expectedA = 0;
    for (int i = 0; i < target; ++i) {
        require (trial.phase() == bp::Phase::listening && trial.roundNumber() == unsigned(i+1), "round counter");
        require (!trial.canAnswer() && !trial.answer (1, true), "cannot answer unheard round");
        require (!trial.observePlayed (41, 100+i, 3) && !trial.observePlayed (42, 99+i, 3), "reject stale generation and epoch");
        require (trial.observePlayed (42,100+i,1) && !trial.canAnswer(), "one actual side insufficient");
        require (!trial.observePlayed (42,100+i,4) && !trial.observePlayed (42,100+i,-1), "reject malformed receipt");
        require (trial.observePlayed (42,100+i,2) && trial.canAnswer(), "both actual sides enable answer");
        require (!trial.answer (0, true) && !trial.answer (3, true) && trial.canAnswer(), "invalid answer does not consume round");
        const int source = i % 3 == 0 ? 2 : 1;
        expectedA += (source == 1) == mapping[i];
        require (trial.answer (source, (source == 1) == mapping[i]) && !trial.answer (source, (source == 1) == mapping[i]), "one vote only");
        const auto* recorded = trial.answerAt (i);
        require (recorded && recorded->source == source && recorded->choseA == ((source == 1) == mapping[i])
                 && recorded->epoch == unsigned(100+i), "record exact external mapping and choice");
        require (trial.playedMask() == 0 && trial.canReveal(), "reset played evidence and allow early reveal");
        if (i+1 < target) {
            require (!trial.armNext (41,101+i) && !trial.armNext (42,100), "reject foreign or reused assignment");
            require (trial.armNext (42,101+i), "accept independently supplied next assignment");
        }
    }
    require (trial.phase() == bp::Phase::awaitingReveal && !trial.armNext (42,999), "completion waits for explicit reveal");
    const auto summary = trial.summary();
    require (summary.a == expectedA && summary.b == unsigned(target)-expectedA, "count preferred identities not anonymous button");
    require (trial.answerAt(target) == nullptr && trial.answerAt(99) == nullptr, "history bounds");
    require (trial.reveal() && !trial.reveal() && !trial.canAnswer(), "explicit reveal once");
    trial.reset();
    require (trial.phase() == bp::Phase::off && trial.summary().total() == 0 && !trial.answerAt(0)
             && trial.planned() == 0 && trial.playedMask() == 0, "END or restore clears all in-memory history");
}
void interruption()
{
    bp::Trial trial;
    for (int invalid : {0,1,4,6,11,-1}) require (!trial.begin(invalid,1,1), "off or unsupported rounds do not start");
    require (!trial.begin(5,0,1), "no unknown context");
    require (trial.begin(5,7,1) && !trial.reveal(), "cannot reveal before first answer");
    trial.observePlayed(7,1,3); require(trial.answer(1,false), "first preference");
    require(trial.armNext(7,2), "second assignment"); trial.observePlayed(7,2,1);
    require(trial.reveal() && trial.answered()==1 && trial.summary().b==1
            && !trial.summary().chance.available, "partial reveal excludes unanswered current round");
    require(!trial.armNext(7,3), "revealed trial cannot resume");
    trial.begin(10,8,1); trial.observePlayed(8,1,3); trial.answer(2,false); trial.randomnessFailed();
    require(trial.phase()==bp::Phase::randomnessFailed && trial.answered()==1 && !trial.canAnswer()
            && !trial.armNext(8,2), "randomness failure never fabricates or resumes a mapping");
    require(trial.reveal() && trial.summary().b==1, "completed answers remain available after failed next draw");
    require(!trial.invalidateIfGenerationChanged(8) && trial.answered()==1, "same context is stable");
    require(trial.invalidateIfGenerationChanged(9) && trial.answered()==0 && !trial.canReveal(), "changed context clears history");
    trial.begin(5,10,std::numeric_limits<std::uint32_t>::max());
    trial.observePlayed(10,std::numeric_limits<std::uint32_t>::max(),3); trial.answer(1,false);
    require(trial.armNext(10,0), "epoch wrap accepted only as a new round epoch");
    require(!trial.observePlayed(10,std::numeric_limits<std::uint32_t>::max(),3), "wrapped round rejects old evidence");
    trial.reset();
    require(!trial.observePlayed(10,0,3) && !trial.answer(1,false), "ended model cannot receive late evidence");
}
}
int main()
{
    try { statistics(); rounds(5); rounds(10); interruption(); }
    catch (const std::exception& e) { std::cerr << "Blind preference FAIL: " << e.what() << '\n'; return 1; }
    std::cout << "Blind preference PASS: statistics, 5 rounds, 10 rounds, interruption (4 groups)\n";
}
