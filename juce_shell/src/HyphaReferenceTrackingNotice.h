#pragma once

#include <cstdint>
#include <utility>

namespace hypha::reference_ui
{
// 追従が上限・±6 dB で止まった知らせ（R-28）を、同じ役の同じ MATCH の試みのあいだ一度だけ出す。停止・シーク・
// ループで自動に戻っても試みは同じなので出し直さない。押し直した（試みが変わった）・別の役なら、また一度出す。
struct TrackingStopNotice
{
    std::pair<int, std::uint64_t> key { 0, 0 };
    bool shown = false;

    // sounding：役が鳴っている。stopped：その役の追従が止まっている。出すときだけ true。
    bool update (bool sounding, int slot, std::uint64_t matchAttempt, bool stopped) noexcept
    {
        if (sounding && std::make_pair (slot, matchAttempt) != key)
        {
            key = { slot, matchAttempt };
            shown = false;
        }
        const bool show = sounding && stopped && ! shown;
        shown = shown || show;
        return show;
    }
};
}
