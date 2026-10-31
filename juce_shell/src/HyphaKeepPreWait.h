#pragma once
#include <string_view>

// Keep（2026-10-31）：組んだ PRE がまだ動いていない（project を開いた直後・PRE の準備中）だけで「PREが使えません。
// 選び直してください。」と言わない。押した Keep を待たせて PRE が応答したら始め、待っても応答しない時だけ選び直しを言う。
namespace hypha::keep_ui
{
inline constexpr std::string_view preUnavailableNotice = "PRE unavailable. Choose PRE again.";
inline constexpr double preWaitSeconds = 8.0, preRetrySeconds = 0.5;
enum class KeepWait { done, wait, notify };

// 押した（または待たせた）Keep の答え：始まった、PRE を待つ、知らせる（Rust の短い文か、待ちの上限の選び直し）。
constexpr KeepWait keepWait (bool started, std::string_view notice, double waitedSeconds) noexcept
{
    if (started) return KeepWait::done;
    return notice == preUnavailableNotice && waitedSeconds < preWaitSeconds ? KeepWait::wait : KeepWait::notify;
}
}
