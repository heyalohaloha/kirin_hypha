// 出力の持ち主の表（OutputOwnership.h）を、決まりの元（R-12・README・INV-S33・INV-S47・INV-LC4・INV-LC14）から
// 状態ごとに書いた期待と突き合わせる。活動・状態の種類を足すと、ここの列を書き足すまで通らない。
#include "../src/OutputOwnership.h"
#include "../src/live_compare/LiveCompareAuthority.h"
#include "../src/HyphaLiveStartAfterReturn.h"
#include "../src/HyphaKeepPreWait.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace hypha::output_owner;

namespace
{
int failures = 0;
void require (bool condition, const std::string& what)
{
    if (condition) return;
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
}

static_assert (activityCount == 7 && stateCount == 16,
               "a new activity or state needs its column below (and the rule in OutputOwnership.h)");

// 1 文字が 1 つの活動（並び：LISTEN, LIVE BLIND, PRE/POST Blind, VERSION BLIND, A を下げる承認, B・C・V, Keep）。
// a 許す、t 取って代わる。大文字は断る理由：L 形式、R 確かめ直し、F live の戻り、H RETURN が先、C 今の live 比較、
// B Blind、K Keep・Record、U Reference の戻り、P REF の B・C・V。
struct Column { State state; const char* cells; const char* why; };
constexpr Column columns[] {
    { State::layout,             "LLLLLLL", "INV-S33: every comparison and Keep are mono / stereo only" },
    { State::offline,            "aaaaaaa", "R-12: in an offline block the audio thread keeps A / POST; no start is refused" },
    { State::bypass,             "aaaaaaa", "R-12: in a bypassed block the audio thread keeps A / POST; no start is refused" },
    { State::liveRestoring,      "RRRRRRa", "after a restore nothing takes POST until LISTEN is checked again; Keep is not an audition" },
    { State::liveFinishing,      "FFFFFFa", "INV-LC14: no other audition while POST returns after END" },
    { State::liveSessionLowered, "HaHHCHa", "INV-LC14: a session's own approved MATCH carries only into LIVE BLIND" },
    { State::liveHeld,           "HHHHHHa", "INV-LC14: held attenuation blocks auditions; Keep measures before the lowering" },
    { State::liveBlind,          "CCCCCCB", "one comparison at a time; Keep waits for Blind" },
    { State::localBlind,         "BBBBBBB", "one Blind for a POST and its project" },
    { State::versionBlind,       "BBBBBBB", "one Blind for a POST and its project, until V has returned its output" },
    { State::recordKeep,         "aKKKaKa", "Keep blocks every audition; LISTEN and the approval measure before them" },
    { State::referenceLowered,   "HHHHaaa", "INV-S47: no LISTEN or Blind while A is lowered; B, C, V, a deeper approval and Keep go on" },
    { State::referenceReturning, "UUUUaaa", "LISTEN and Blind wait for the RETURN rise; roles wait for A inside the controller" },
    { State::liveSession,        "aattCta", "INV-LC4: an audition or Blind takes the output; LISTEN keeps POST, so A is not lowered" },
    { State::audition,           "aPPtaaP", "VERSION BLIND takes over B and C; other Blinds and Keep wait for A in REF" },
    { State::auditionHeld,       "ttttaaa", "INV-S47: LISTEN and Blind forget the held resume" },
};

Rule expected (char cell)
{
    switch (cell)
    {
        case 'a': return {};
        case 't': return { Verdict::takeOver, Reason::none };
        case 'L': return { Verdict::refuse, Reason::layout };
        case 'R': return { Verdict::refuse, Reason::restoring };
        case 'F': return { Verdict::refuse, Reason::liveReturning };
        case 'H': return { Verdict::refuse, Reason::returnFirst };
        case 'C': return { Verdict::refuse, Reason::liveComparison };
        case 'B': return { Verdict::refuse, Reason::blindRunning };
        case 'K': return { Verdict::refuse, Reason::recordRunning };
        case 'U': return { Verdict::refuse, Reason::referenceReturning };
        case 'P': return { Verdict::refuse, Reason::auditionRunning };
        default: break;
    }
    return { Verdict::refuse, Reason::none };  // 読めない文字は合わない
}

void everyCellIsStated()
{
    bool stated[stateCount] {};
    for (const auto& column : columns)
    {
        const auto index = static_cast<int> (column.state);
        require (! stated[index], std::string ("a state is stated twice: ") + column.why);
        stated[index] = true;
        require (std::string (column.cells).size() == activityCount, std::string ("a column names every activity: ") + column.why);
        for (int a = 0; a < activityCount; ++a)
        {
            const auto want = expected (column.cells[a]);
            const auto got = rule (static_cast<Activity> (a), column.state);
            require (got.verdict == want.verdict && got.reason == want.reason,
                     "activity " + std::to_string (a) + " in state " + std::to_string (index) + ": " + column.why);
        }
    }
    for (int s = 0; s < stateCount; ++s) require (stated[s], "state " + std::to_string (s) + " has no stated column");
}

void decisionsCombineStates()
{
    require (decide (Activity::audition, 0).verdict == Verdict::allow && decide (Activity::audition, 0).yielding == 0,
             "nothing running allows every start");
    // A を承認して下げたまま B を聴いている：VERSION BLIND は始めない（POST を二重に下げない）。
    const auto lowered = decide (Activity::versionBlind, bit (State::referenceLowered) | bit (State::audition));
    require (lowered.refused() && lowered.reason == Reason::returnFirst && lowered.cause == State::referenceLowered,
             "VERSION BLIND waits for RETURN while A is lowered, even with B playing");
    // 断る状態が複数あれば、並びの先の理由を言う。断る状態は、取って代わる状態より強い。
    const auto first = decide (Activity::liveCompare, bit (State::liveRestoring) | bit (State::referenceLowered));
    require (first.refused() && first.reason == Reason::restoring, "the earlier state gives the reason");
    const auto keep = decide (Activity::localBlind, bit (State::liveSession) | bit (State::recordKeep));
    require (keep.refused() && keep.reason == Reason::recordRunning && keep.yielding == 0, "a refusal wins over a takeover");
    // 取って代わる相手は、まとめて返す。
    const auto over = decide (Activity::versionBlind, bit (State::liveSession) | bit (State::audition) | bit (State::auditionHeld));
    require (over.verdict == Verdict::takeOver
                 && over.yielding == (bit (State::liveSession) | bit (State::audition) | bit (State::auditionHeld)),
             "VERSION BLIND takes over LISTEN, the sounding role and the held resume together");
    // live 比較の事実から状態へ。
    require (liveStates ({ true, false, false, false, 0.5f, 0.5f }) == (bit (State::liveSessionLowered) | bit (State::liveSession)),
             "a lowered active session");
    require (liveStates ({ false, false, false, false, 1.0f, 0.5f }) == bit (State::liveHeld), "a lowering held after the session");
    require (! decide (Activity::recordKeep, liveStates ({ false, false, false, false, 0.5f, 0.5f })).refused(),
             "held named attenuation without Blind permits canonical Keep");
    const auto endedBlind = decide (Activity::recordKeep, liveStates ({ false, false, false, true, 0.5f, 0.5f }));
    require (endedBlind.refused() && endedBlind.reason == Reason::blindRunning,
             "a retained Blind scope blocks Keep until its fade releases it");
    require (liveStates ({ false, true, true, true, 1.0f, 1.0f })
                 == (bit (State::liveRestoring) | bit (State::liveFinishing) | bit (State::liveBlind)),
             "restoring, finishing and LIVE BLIND");
    // live 比較の入口の答えも同じ表から（LIVE BLIND は自分のセッションの下げを持ち込める）。
    using hypha::live_compare::StartResult;
    using hypha::live_compare::entryAdmission;
    require (entryAdmission (true, true, false, false, false, 0.5f, 0.5f) == StartResult::started
                 && entryAdmission (false, true, false, false, false, 0.5f, 0.5f) == StartResult::returnRequired,
             "only LIVE BLIND carries its session's attenuation");
    // INV-LC14（2026-10-31）：下げているあいだに押した LISTEN は断らずに RETURN し、戻り終えたら同じ押しで始める。
    using hypha::live_compare_ui::StartAfterReturn;
    using hypha::live_compare_ui::onStartPressed;
    using hypha::live_compare_ui::whileReturning;
    require (onStartPressed (StartResult::returnRequired) == StartAfterReturn::returnFirst,
             "LISTEN pressed while POST is lowered returns POST first instead of refusing");
    require (whileReturning (StartResult::returnRequired) == StartAfterReturn::waitForReturn
                 && whileReturning (StartResult::returnPending) == StartAfterReturn::waitForReturn
                 && whileReturning (StartResult::started) == StartAfterReturn::start,
             "the press waits while POST returns and starts LISTEN once it is back");
    // The role this press's own RETURN still fades out is not a new audition: the press waits. A role the
    // user chose again (or pressed and waiting), VERSION BLIND or PRE/POST Blind drops the press.
    {
        using hypha::output_owner::bit;
        using hypha::output_owner::State;
        require (whileReturning (StartResult::returnRequired, bit (State::audition) | bit (State::referenceLowered))
                         == StartAfterReturn::waitForReturn
                     && whileReturning (StartResult::returnPending, bit (State::audition) | bit (State::referenceReturning))
                         == StartAfterReturn::waitForReturn
                     && whileReturning (StartResult::started, 0) == StartAfterReturn::start,
                 "the fade of this press's own RETURN keeps the LISTEN press waiting");
        require (whileReturning (StartResult::started, bit (State::auditionHeld)) == StartAfterReturn::drop
                     && whileReturning (StartResult::returnRequired, bit (State::audition) | bit (State::auditionHeld))
                         == StartAfterReturn::drop
                     && whileReturning (StartResult::comparisonBusy, bit (State::versionBlind)) == StartAfterReturn::drop
                     && whileReturning (StartResult::comparisonBusy, bit (State::localBlind)) == StartAfterReturn::drop,
                 "a new audition by the user drops the waiting LISTEN press");
    }
    require (onStartPressed (StartResult::comparisonBusy) == StartAfterReturn::refuse
                 && onStartPressed (StartResult::noPair) == StartAfterReturn::refuse,
             "other refusals still say why");
    // Keep（2026-10-31）：組んだ PRE がまだ応答しないだけなら、押した Keep を待たせ、上限まで応答しない時だけ選び直しを言う。
    using hypha::keep_ui::KeepWait;
    using hypha::keep_ui::keepWait;
    require (keepWait (false, hypha::keep_ui::preUnavailableNotice, 0.0) == KeepWait::wait
                 && keepWait (false, hypha::keep_ui::preUnavailableNotice, hypha::keep_ui::preWaitSeconds - 0.1) == KeepWait::wait,
             "Keep waits for a paired PRE that has not answered yet");
    require (keepWait (false, hypha::keep_ui::preUnavailableNotice, hypha::keep_ui::preWaitSeconds) == KeepWait::notify,
             "a PRE that never answers asks to choose PRE again");
    require (keepWait (true, {}, 0.0) == KeepWait::done && keepWait (false, "Keep storage is full. Free disk space.", 0.0) == KeepWait::notify
                 && keepWait (false, "Choose a PRE before Keep.", 0.0) == KeepWait::notify,
             "a started Keep and every other refusal answer at once");
}
}

int main()
{
    everyCellIsStated();
    decisionsCombineStates();
    if (failures != 0)
    {
        std::cerr << failures << " output ownership checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Output ownership: " << activityCount << " activities x " << stateCount << " states PASS\n";
    return EXIT_SUCCESS;
}
