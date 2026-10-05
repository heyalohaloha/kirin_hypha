#pragma once

#include <cstdint>

// POST の出力（と A の音量）を取る活動を、今の状態で始めてよいか。ボタンの出し分け・processor・controller の
// どの入口も、この表だけで決める（入口ごとに決まりを書くと、入口が増えるたびに漏れる）。
// 結果は 3 通り：許す／断る（理由つき。押した利用者に言う：R-28）／取って代わる（相手が譲る）。
// 決まりの元：AGENTS.md の R-12、README の Reference、INV-S33・INV-S47・INV-S48・INV-LC4・INV-LC14。
namespace hypha::output_owner
{
enum class Activity : std::uint8_t
{
    liveCompare,   // LISTEN（live PRE/POST 比較）を始める
    liveBlind,     // LIVE BLIND（live 比較のセッションを使う Blind）
    localBlind,    // PRE/POST Blind（区間の取り込み。live 比較の PIN も）
    versionBlind,  // REF の VERSION BLIND
    lowerA,        // 上限を超えた MATCH で、A を下げて合わせる承認
    audition,      // B・C・V を鳴らす（押した・待たせた・戻す・替えた選択のどれも）
    recordKeep,    // Keep・Record
};
inline constexpr int activityCount = 7;

// 今の状態。同時にいくつあってもよい。並びは断る理由の順（先の状態の理由を言う）。
enum class State : std::uint8_t
{
    layout,              // mono・stereo でない（exact 5.1 など。INV-S33）
    offline,             // オフライン書き出し（その block では Audio Thread が A・POST を保つ）
    bypass,              // host の bypass（同上）
    liveRestoring,       // DAW の状態の読み込みの後、live 比較を確かめ直している
    liveFinishing,       // live 比較の END の後、POST を戻している途中（INV-LC14 の復帰待ち）
    liveSessionLowered,  // live 比較のセッションが、承認した量だけ POST を下げている
    liveHeld,            // セッションの後も、live 比較が POST を下げて保っている（RETURN まで）
    liveBlind,           // LIVE BLIND の途中
    localBlind,          // PRE/POST Blind の途中（Blind の枠を持つ）
    versionBlind,        // VERSION BLIND の途中（V が出力を返し終えるまで）
    recordKeep,          // Keep・Record の途中
    referenceLowered,    // Reference が承認して A を下げている（RETURN まで）
    referenceReturning,  // RETURN で A を上げている途中（0.5 秒）
    liveSession,         // live 比較のセッションがある
    audition,            // B・C・V が出力を持っている
    auditionHeld,        // B・C・V の選択を保っている（停止・シークの後に戻す控え、押した後の待ち）
};
inline constexpr int stateCount = 16;

enum class Verdict : std::uint8_t { allow, refuse, takeOver };

enum class Reason : std::uint8_t
{
    none,
    layout,              // mono・stereo だけ
    restoring,           // live 比較を確かめ直している
    liveReturning,       // live 比較の END の後、通常の音量へ戻る途中
    returnFirst,         // 先に RETURN（下げたまま、別の比較で POST を二重に下げない）
    liveComparison,      // 先に今の live 比較を終える
    blindRunning,        // 先に Blind を終える
    recordRunning,       // 先に Keep・Record を終える
    referenceReturning,  // A を通常の音量へ戻している途中
    auditionRunning,     // 先に REF で A を押す（B・C・V を止める）
};

struct Rule
{
    Verdict verdict = Verdict::allow;
    Reason reason = Reason::none;
};

using States = std::uint32_t;
constexpr States bit (State state) noexcept { return States (1) << static_cast<int> (state); }
constexpr bool has (States states, State state) noexcept { return (states & bit (state)) != 0; }

struct Decision
{
    Verdict verdict = Verdict::allow;
    Reason reason = Reason::none;
    State cause = State::layout;  // 断ったときの状態（理由の元）
    States yielding = 0;          // 取って代わられる状態（相手が譲る）
    constexpr bool refused() const noexcept { return verdict == Verdict::refuse; }
};

namespace detail
{
constexpr Rule A {};
constexpr Rule T { Verdict::takeOver, Reason::none };
constexpr Rule refuse (Reason reason) noexcept { return { Verdict::refuse, reason }; }
constexpr Rule L = refuse (Reason::layout), R = refuse (Reason::restoring), F = refuse (Reason::liveReturning),
               H = refuse (Reason::returnFirst), C = refuse (Reason::liveComparison), B = refuse (Reason::blindRunning),
               K = refuse (Reason::recordRunning), U = refuse (Reason::referenceReturning),
               P = refuse (Reason::auditionRunning);

// 行：Activity、列：State（上の並び）。
//                                layout off byp  rest fin  sLow held lBl  loc  ver  rec  rLow rRet sess aud  held
constexpr Rule table[activityCount][stateCount] {
    /* liveCompare  */ { L,     A,  A,   R,   F,   H,   H,   C,   B,   B,   A,   H,   U,   A,   A,   T },
    /* liveBlind    */ { L,     A,  A,   R,   F,   A,   H,   C,   B,   B,   K,   H,   U,   A,   P,   T },
    /* localBlind   */ { L,     A,  A,   R,   F,   H,   H,   C,   B,   B,   K,   H,   U,   T,   P,   T },
    /* versionBlind */ { L,     A,  A,   R,   F,   H,   H,   C,   B,   B,   K,   H,   U,   T,   T,   T },
    /* lowerA       */ { L,     A,  A,   R,   F,   C,   H,   C,   B,   B,   A,   A,   A,   C,   A,   A },
    /* audition     */ { L,     A,  A,   R,   F,   H,   H,   C,   B,   B,   K,   A,   A,   T,   A,   A },
    /* recordKeep   */ { L,     A,  A,   A,   A,   A,   A,   B,   B,   B,   A,   A,   A,   A,   P,   A },
};
}

constexpr Rule rule (Activity activity, State state) noexcept
{
    return detail::table[static_cast<int> (activity)][static_cast<int> (state)];
}

// 断る状態があれば、並びの最初の理由で断る。無ければ、取って代わる状態をまとめて返す。
constexpr Decision decide (Activity activity, States states) noexcept
{
    Decision decision;
    for (int index = 0; index < stateCount; ++index)
    {
        const auto state = static_cast<State> (index);
        if (! has (states, state)) continue;
        const auto r = rule (activity, state);
        if (r.verdict == Verdict::refuse) return { Verdict::refuse, r.reason, state, 0 };
        if (r.verdict == Verdict::takeOver) { decision.verdict = Verdict::takeOver; decision.yielding |= bit (state); }
    }
    return decision;
}

// live 比較の事実から状態へ（processor と、live 比較の入口の試験が使う）。
struct LiveFacts
{
    bool active = false, restoring = false, finishing = false, blindOwned = false;
    float postActual = 1.0f, postTarget = 1.0f;
};
constexpr States liveStates (const LiveFacts& facts) noexcept
{
    const bool lowered = facts.postActual != 1.0f || facts.postTarget != 1.0f;
    States states = 0;
    if (facts.restoring) states |= bit (State::liveRestoring);
    if (facts.finishing) states |= bit (State::liveFinishing);
    if (lowered) states |= bit (facts.active ? State::liveSessionLowered : State::liveHeld);
    if (facts.blindOwned) states |= bit (State::liveBlind);
    if (facts.active) states |= bit (State::liveSession);
    return states;
}
}
