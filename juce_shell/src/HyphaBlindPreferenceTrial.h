#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace hypha::blind_preference
{
// One round is the legacy no-question listen, not a preference vote.
inline int normaliseRounds (int value) noexcept { return value == 5 || value == 10 ? value : 1; }
// Non-RT, message-thread owned only. No audio/gain, RNG, storage or JUCE dependency.
// A means PRE (or candidate A), B means POST (or candidate B). Never expose the
// assignment/history through an unrevealed screen. Context invalidation must reset.
struct Answer
{
    int source = 0;
    bool choseA = false;
    std::uint32_t epoch = 0;
};
struct Probability
{
    bool available = false; // no probability line for fewer than two answers
    unsigned numerator = 0, denominator = 1; // exact two-sided fair-binomial p
    double fraction() const noexcept { return static_cast<double> (numerator) / denominator; }
    bool lessThanOneInHundred() const noexcept
    { return available && numerator * 100u < denominator; }
    unsigned roundedPerHundred() const noexcept
    { return (numerator * 100u + denominator / 2u) / denominator; }
};
// The larger of the two counts is selected AFTER listening: use both tails.
inline Probability probability (unsigned a, unsigned b) noexcept
{
    const auto n = a + b;
    if (a > 10 || b > 10 || n < 2 || n > 10) return {};
    const unsigned denominator = 1u << n;
    if (a == b) return {true, denominator, denominator};
    const auto k = a > b ? a : b;
    unsigned combinations = 1, tail = 0;
    for (unsigned i = 0; i <= n; ++i) {
        if (i >= k) tail += combinations;
        if (i < n) combinations = combinations * (n - i) / (i + 1);
    }
    const unsigned both = tail * 2;
    return {true, both < denominator ? both : denominator, denominator};
}
struct Summary
{
    unsigned a = 0, b = 0;
    Probability chance;
    unsigned total() const noexcept { return a + b; }
    bool tied() const noexcept { return total() > 0 && a == b; }
};
enum class Phase { off, listening, awaitingAssignment, awaitingReveal, revealed, randomnessFailed };
class Trial final
{
public:
    // Called only after the first independently random assignment was published.
    bool begin (int rounds, std::uint64_t generation, std::uint32_t epoch) noexcept
    {
        reset();
        if ((rounds != 5 && rounds != 10) || generation == 0) return false;
        target = static_cast<unsigned> (rounds); context = generation;
        assignmentEpoch = epoch; currentPhase = Phase::listening;
        return true;
    }
    void reset() noexcept
    {
        records = {}; count = target = heard = 0; context = 0; assignmentEpoch = 0;
        currentPhase = Phase::off;
    }
    // END, restored state, pair/format/owner changes must call reset even if their
    // external generation happens to be unchanged. This helper rejects changed IDs.
    bool invalidateIfGenerationChanged (std::uint64_t generation) noexcept
    {
        if (currentPhase == Phase::off || generation == context) return false;
        reset(); return true;
    }
    // Feed only existing verified played receipts. A requested source, crossfade,
    // or substitute POST is NOT a receipt. Old epochs cannot satisfy the new round.
    bool observePlayed (std::uint64_t generation, std::uint32_t epoch, int playedMask) noexcept
    {
        if (currentPhase != Phase::listening || generation != context || epoch != assignmentEpoch
            || playedMask < 0 || playedMask > 3) return false;
        heard |= static_cast<unsigned> (playedMask); return true;
    }
    bool canAnswer() const noexcept { return currentPhase == Phase::listening && heard == 3; }
    // selectedA comes ONLY from successful BlindSession::acceptPreference. The
    // model never asks for the unrevealed assignment or determines audio authority.
    bool answer (int source, bool selectedA) noexcept
    {
        if (!canAnswer() || (source != 1 && source != 2) || count >= target) return false;
        records[count++] = { source, selectedA, assignmentEpoch };
        heard = 0;
        currentPhase = count == target ? Phase::awaitingReveal : Phase::awaitingAssignment;
        return true;
    }
    // No alternating/balanced schedule here. Caller obtains fresh independent OS
    // randomness and starts the existing BlindSession, preserving selected source
    // and approved gains; supply its NEW epoch only after that succeeds.
    bool armNext (std::uint64_t generation, std::uint32_t epoch) noexcept
    {
        if (currentPhase != Phase::awaitingAssignment || generation != context) return false;
        for (unsigned i = 0; i < count; ++i) if (records[i].epoch == epoch) return false;
        assignmentEpoch = epoch; heard = 0;
        currentPhase = Phase::listening; return true;
    }
    void randomnessFailed() noexcept
    {
        if (currentPhase == Phase::awaitingAssignment) {
            heard = 0; currentPhase = Phase::randomnessFailed;
        }
    }
    bool canReveal() const noexcept
    { return count > 0 && currentPhase != Phase::off && currentPhase != Phase::revealed; }
    bool reveal() noexcept
    {
        if (!canReveal()) return false;
        heard = 0; currentPhase = Phase::revealed; return true;
    }
    Phase phase() const noexcept { return currentPhase; }
    unsigned answered() const noexcept { return count; }
    unsigned planned() const noexcept { return target; }
    unsigned roundNumber() const noexcept { return count < target ? count + 1 : target; }
    unsigned playedMask() const noexcept { return heard; }
    std::uint64_t generation() const noexcept { return context; }
    std::uint32_t epoch() const noexcept { return assignmentEpoch; }
    const Answer* answerAt (unsigned index) const noexcept { return index < count ? &records[index] : nullptr; }
    Summary summary() const noexcept
    {
        Summary result;
        for (unsigned i = 0; i < count; ++i) (records[i].choseA ? result.a : result.b)++;
        result.chance = probability (result.a, result.b); return result;
    }
private:
    std::array<Answer, 10> records {};
    unsigned count = 0, target = 0, heard = 0;
    std::uint64_t context = 0;
    std::uint32_t assignmentEpoch = 0;
    Phase currentPhase = Phase::off;
};
}
