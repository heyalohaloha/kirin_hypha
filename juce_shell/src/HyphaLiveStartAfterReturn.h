#pragma once
#include "live_compare/LiveCompareAuthority.h"

// INV-LC14（2026-10-31）：POST を下げているあいだに押した LISTEN は断らずに先に RETURN し、戻り終えたら
// 同じ押しで LISTEN を始める。押しは戻りが済むまで待たせる（待ちの上限と、上限での理由は editor が持つ）。
namespace hypha::live_compare_ui
{
enum class StartAfterReturn : std::uint8_t { refuse, returnFirst, waitForReturn, start, drop };

// 押したときの入口の答えから：RETURN が先なら、戻してから始める。ほかの断りは今までどおり理由を言う。
constexpr StartAfterReturn onStartPressed (live_compare::StartResult result) noexcept
{
    return result == live_compare::StartResult::returnRequired ? StartAfterReturn::returnFirst
                                                                : StartAfterReturn::refuse;
}

// LISTEN の入口（MENU・足元）を出すか：始められる時と、RETURN してから始められる時（音の側の開始の守りは変えない）。
constexpr bool entryOffered (live_compare::StartResult admission) noexcept
{
    return admission == live_compare::StartResult::started || admission == live_compare::StartResult::returnRequired;
}

// 待たせた押しの今の入口の答えから：戻りの途中は待ち、戻り終えたら始める（ほかの断りは始める側が言う）。
constexpr StartAfterReturn whileReturning (live_compare::StartResult admission) noexcept
{
    return admission == live_compare::StartResult::returnRequired
            || admission == live_compare::StartResult::returnPending
        ? StartAfterReturn::waitForReturn : StartAfterReturn::start;
}

// The same, read with the Reference's own output states. The role this press's RETURN still fades out
// (output without a chosen role) is not a new audition and the press waits; a role the user chose again
// or pressed and waiting, VERSION BLIND or PRE/POST Blind drops the press.
constexpr StartAfterReturn whileReturning (live_compare::StartResult admission, output_owner::States reference) noexcept
{
    using output_owner::State;
    constexpr auto chosen = output_owner::bit (State::auditionHeld) | output_owner::bit (State::versionBlind)
                          | output_owner::bit (State::localBlind);
    return (reference & chosen) != 0 ? StartAfterReturn::drop : whileReturning (admission);
}
}
