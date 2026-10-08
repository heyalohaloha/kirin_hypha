#include "HyphaJapaneseCatalog.h"
namespace hypha::i18n::catalog
{
Section drumSnapshotSection() noexcept
{
    static const Entry entries[] {
        { "Choice ", u8"候補" },
        { "Cohort ", u8"対象" },
        { "Cohort waiting", u8"対象打音待ち" },
        { "Full-band ", u8"全帯域 " },
        { "Past hit", u8"過去の一打" },
        { "Within RES", u8"判別内" },
        { "No worker", u8"計測応答なし" },
        { "Latest hit", u8"最新の一打" },
        { "Locked hit", u8"固定した一打" },
        { "No observations", u8"観測なし" },
        { "Single hit", u8"一打" },
        { "Set change: gap", u8"集合変更は断線" },
        { "Both silent", u8"両側無音" },
        { "Next hit", u8"次打まで" },
        { "No mapping", u8"対応不明" },
        { "Clock changed", u8"時計変更" },
        { "Source changed", u8"音源変更" },
        { "Worker unavailable", u8"計測停止" },
        { "Request expired", u8"取得期限切れ" },
        { "Waiting for worker", u8"計測待ち" },
        { "Waiting for publication", u8"公開待ち" },
        { "Update PRE", u8"PRE更新" },
        { "Long tail", u8"長い余韻" },
        { "Audio ended", u8"入力終了" },
        { "Max age ", u8"確定最大古さ" },
        { "Full-band 6s history", u8"全帯域6秒履歴" },
        { u8"Facts — frozen / ESC close / End LIVE", u8"根拠 — 固定 / ESC閉じる / End LIVE" },
        { " / presentation ", u8" / 表示版" },
        { "Whole interval ", u8"全N区間 " },
        { "Exact median ", u8"確定部分中央値 " },
        { "Raw interval ", u8"元区間 " },
        { " / Whole within", u8" / 全Nが範囲内" },
        { "Source generation ", u8"入力世代 " },
        { "Pair authority ", u8"比較の検証版 " },
        { "Mean in dB; identical participants for PRE and POST. Set changes break lines and fill.", u8"dB平均。PRE/POSTは同参加集合。集合変更では線と塗りを切る。" },
        { "Scroll / Up Down for further facts", u8"スクロール / 上下キーで続き" },
    };
    return { "DRUM snapshot", entries, sizeof (entries) / sizeof (entries[0]) };
}
}
