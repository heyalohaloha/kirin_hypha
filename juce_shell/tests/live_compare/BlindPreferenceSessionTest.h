#pragma once
#include "../../src/live_compare/LiveBlindSession.h"
#include <stdexcept>

inline void runBlindPreferenceSessionTests()
{
    using namespace hypha::live_compare;
    const auto check = [] (bool value, const char* why) { if (!value) throw std::runtime_error (why); };
    const auto hearBoth = [] (BlindSession& session) {
        session.select (1); session.observe (session.command(), true);
        session.select (2); session.observe (session.command(), true);
    };
    for (int rounds : {5, 10})
    {
        BlindSession session; session.start (true);
        int randomCalls = 0;
        for (int round = 0; round < rounds; ++round)
        {
            const auto epoch = session.command().epoch();
            check (!session.acceptPreference (1, epoch), "unheard sources cannot be answered");
            check (session.view().played == 0, "each round starts without receipts");
            session.observe (session.command(), false);
            check (!session.acceptPreference (1, epoch), "fallback is not hearing both sources");
            hearBoth (session);
            check (!session.acceptPreference (0, epoch) && !session.acceptPreference (3, epoch), "invalid source rejected");
            check (!session.acceptPreference (1, epoch + 1), "stale UI epoch rejected");
            const auto answer = session.acceptPreference (1, epoch);
            check (answer.has_value() && *answer == session.command().firstPre(), "accepted choice maps to PRE/A only internally");
            check (!session.acceptPreference (2, epoch), "one answer per round");
            if (round + 1 < rounds)
            {
                const auto old = session.command();
                check (session.nextPreferenceWith (epoch, [&] { ++randomCalls; return randomCalls % 3 == 0; })
                    == BlindSession::NextPreference::started, "next round takes independent random callback");
                check (session.command().epoch() != epoch && session.command().stimulus() == old.stimulus(),
                    "round changes epoch but preserves anonymous source number");
                session.observe (old, true);
                check (session.view().played == 0 && session.view().audible == 0, "old round receipt cannot count");
            }
        }
        check (randomCalls == rounds - 1, "one fresh draw per transition, no forced alternation");
        check (!session.view().revealed && session.revealPreference (session.command().epoch()), "final answer does not auto reveal");
        check (!session.revealPreference (session.command().epoch()), "reveal cannot repeat");
    }
    BlindSession partial; partial.start (false);
    check (!partial.revealPreference (partial.command().epoch()), "UI cannot forge completed answer count");
    hearBoth (partial);
    auto epoch = partial.command().epoch();
    check (partial.acceptPreference (2, epoch).has_value(), "first completed round");
    check (partial.nextPreferenceWith (epoch, [] { return true; }) == BlindSession::NextPreference::started, "arm unanswered second round");
    check (!partial.reveal(), "legacy OFF reveal still requires both current sources");
    check (partial.revealPreference (partial.command().epoch()), "preference reveal permits stopping after completed round");
    partial.reset(); partial.start (true);
    check (!partial.revealPreference (partial.command().epoch()), "reset cannot revive past answers");
    hearBoth (partial); epoch = partial.command().epoch();
    check (partial.acceptPreference (1, epoch).has_value(), "answer before entropy failure");
    check (partial.nextPreferenceWith (epoch, [] () -> bool { throw std::runtime_error ("entropy unavailable"); })
        == BlindSession::NextPreference::randomUnavailable, "entropy failure is distinct, no fallback");
    check (!partial.command().active() && !partial.revealPreference (epoch), "entropy failure ends trial and its receipts");
    partial.start (true); hearBoth (partial); epoch = partial.command().epoch();
    check (partial.acceptPreference (1, epoch).has_value(), "answer before invalidation");
    partial.invalidate (partial.command(), RecoveryReason::preUnavailable);
    check (partial.nextPreferenceWith (epoch, [] { return false; }) == BlindSession::NextPreference::notReady
        && !partial.revealPreference (epoch), "authority invalidation cannot advance or reveal old answer");
    partial.end(); partial.start (false);
    check (!partial.revealPreference (partial.command().epoch()), "END discards answer proof");
}
