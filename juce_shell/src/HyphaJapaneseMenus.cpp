#include "HyphaJapaneseCatalog.h"

// MENU and the other menus, the PRE connection and its help, and the NOTE dialog. The short
// notices that answer an action are in HyphaJapaneseNotices.cpp.
namespace hypha::i18n::catalog
{
namespace
{
const Entry entries[] = {
    { "A/B: finish Record / Keep first", u8"A/B：Record／Keepの終了後" },
    { "A/B: add PRE2 between A and B", u8"A/B：AとBの間にPRE2を追加" },
    { "One track. Hardware in A or B.", u8"同じトラック。実機はA・Bどちらにも。" },
    { "A different order changes the sound during A/B.", u8"配置が違うと、比較中の音も変わります。" },
    { "A/B could not start", u8"A/Bを開始できませんでした" },
    { "BLIND did not start; A/B continues", u8"BLINDを開始できず、A/Bを続けます" },
    { "A/B: track names differ", u8"A/B：トラック名が異なります" },
    { "Compare A and B on one track", u8"同じトラックのAとBを比較" },
    { "Listen to A", u8"Aを聴く" },
    { "Listen to B", u8"Bを聴く" },
    { "Match A to B", u8"Aの音量をBに合わせる" },
    { "Match A to B loudness over the latest four seconds", u8"直近4秒で、Aの音量をBに合わせます" },
    { "Press MATCH to match again or start AUTO (A follows B)", u8"MATCHを押すと、再測定またはAUTO（AがBに追従）を選べます" },
    { "AUTO: A follows B loudness within 0.5 dB, up to 6 dB from your MATCH, lower if the TP ceiling needs it. Press AUTO to match again or stop AUTO", u8"AUTO：AがBの音量に0.5 dB以内で追従します（MATCHから最大6 dB、TP上限のためさらに下げることも）。AUTOを押すと、再測定またはAUTOの停止を選べます" },
    { "MATCH stopped at the true-peak ceiling: A is still quieter than B. Press to measure again", u8"MATCHはTrue Peakの上限で止まりました。AはまだBより小さいままです。押すと測り直します" },
    { "Lower B to match levels?", u8"音量を合わせるためにBを下げますか？" },
    { "Lower B and begin Blind", u8"Bを下げてBlindを開始" },
    { "Chain audio was used briefly", u8"一部で通常の接続の音を使用" },
    { "A/B: enable PRE2; bypass is on", u8"A/B：PRE2のバイパスを解除" },
    { "A/B: enable PRE2 in the DAW", u8"A/B：DAWでPRE2を有効にしてください" },
    { "A/B: timing unavailable; check PRE1 and PRE2", u8"A/B：時刻を確認できません。PRE1・PRE2を確認" },
    { "END restores normal level; END restores the A to B chain", u8"終了すると通常の音量とA→Bの接続に戻ります" },
    { "END returns +%1 dB; END restores the A to B chain", u8"終了すると音量が%1 dB上がり、A→Bの接続に戻ります" },
    { "Lower %1 dB; END returns the same amount; END restores the A to B chain", u8"%1 dB下げます。終了時に同じ量だけ上がり、A→Bの接続に戻ります" },
    { "PRE1: %1 / PRE2: %2", u8"PRE1：%1／PRE2：%2" },
    { "MATCH: %1 s remaining", u8"MATCH：残り%1秒" },

    { "A needs %1; TP ceiling allows %2", u8"Aは%1必要、TP上限までは%2" },
    { "Lower B by %1; A stays at its level", u8"Bを%1下げる（Aは元の音量のまま）" },
    { "Raise A by %1 only (TP LIMIT)", u8"Aを%1だけ上げる（TP上限）" },
    { "MATCH: A %1", u8"MATCH：A %1" },
    { "MATCH: B %1", u8"MATCH：B %1" },
    { "TP limit: A %1, need %2", u8"TP上限：A %1（揃えるには%2）" },

    { "AUTO: follow B within 0.5 dB", u8"AUTO：Bとの音量差を0.5 dB以内に" },
    { "AUTO at TP ceiling: A %1 dB quieter", u8"AUTOはTP上限まで：Aが%1 dB小さい" },
    { "AUTO at 24 dB: A %1 dB quieter", u8"AUTOは24 dBまで：Aが%1 dB小さい" },
    { "AUTO at 24 dB: A %1 dB louder", u8"AUTOは24 dBまで：Aが%1 dB大きい" },
    { "AUTO 6 dB from MATCH: A %1 dB quieter", u8"AUTOはMATCHから6 dBまで：Aが%1 dB小さい" },
    { "AUTO 6 dB from MATCH: A %1 dB louder", u8"AUTOはMATCHから6 dBまで：Aが%1 dB大きい" },
    { "End A/B and restore the chain", u8"A/Bを終了して通常の接続へ戻す" },
    { "A/B Blind Compare", u8"A/BをBlindで比較" },

    { "Returning to normal, then A/B", u8"通常の音量へ戻してからA/Bを開始" },
    { "A/B: Record / Keep or another comparison is running", u8"A/B：Record／Keepまたは別の比較が実行中" },

    { "Align A automatically", u8"Aの位置を自動で合わせる" },
    { "A %1 ms aligned", u8"Aの位置補正：%1 ms適用済み" },
    { "Aligning A", u8"Aの位置を確認中" },
    { "Position unconfirmed; B plays", u8"位置未確認（B出力）" },
    { "Position unconfirmed; A plays", u8"位置未確認（A出力）" },
    { "A %1 ms early", u8"Aが%1 ms早い" },
    { "A %1 ms late", u8"Aが%1 ms遅い" },

    // MENU.
    { "Stop selected pair", u8"選択中のペアを止める" },
    { "All Stop: active POSTs", u8"All Stop：動作中のPOSTすべて" },
    { "Keep selected pair", u8"選択中のペアでKeep" },
    { "All Keep: Kirin OS required", u8"All Keep：Kirin OSが必要です" },
    { "All Keep: %1 ready POSTs", u8"All Keep：準備済みのPOST %1" },
    { "All Keep: %1 ready POST", u8"All Keep：準備済みのPOST %1" },
    { "Record / Keep: mono / stereo only", u8"Record／Keep：Mono／Stereoのみ" },
    { "Measurement", u8"計測" },
    { "Reset Meter Session", u8"Meter Sessionをリセット" },
    { "Add NOTE at current position", u8"今の位置にNOTEを追加" },
    { "Save measurement image", u8"計測画像を保存" },
    { "PRE / POST Blind / Open at 300% / %1", u8"PRE / POST Blind / 300%で開く / %1" },
    { "Display", u8"表示" },
    { "Show Hybrid VU while recording", u8"記録中はHybrid VUを表示" },
    { "Show selected view for this recording", u8"この記録では選んだ表示に戻す" },
    { "Show hover help", u8"ホバーで説明を表示" },
    { "Language", u8"言語" },
    { "Observation view", u8"表示する画面" },
    { "30 seconds", u8"30秒" },
    { "2 minutes", u8"2分" },
    { "10 minutes", u8"10分" },
    { "2 hours", u8"2時間" },
    { "24 hours", u8"24時間" },
    { "Editor size", u8"画面の大きさ" },
    { "Status", u8"状態" },

    // The PRE connection menu.
    { "PRE connection", u8"PREとの接続" },
    { "Stop playback to change connection", u8"接続を変えるには再生を止めてください" },
    { "Unpair", u8"外す" },
    { "No available PRE", u8"使えるPREがありません" },
    { "In use by another POST: %1", u8"別のPOSTが使用中：%1" },
    { "PRE: %1", u8"PRE：%1" },
    { "Click to connect", u8"押して接続" },
    { u8"▼ All PREs", u8"▼ 候補一覧" },
    { "NO PRE", u8"PREなし" },
    { "Choose PRE", u8"PREを選ぶ" },
    { "SELECT PRE", u8"PREを選択" },
    { "PAIR SELECT PRE", u8"PAIR PREを選択" },
    { "Locked during playback", u8"再生中は変更不可" },
    { "Pair menu", u8"ペアのメニュー" },
    { "Click to edit this PRE name.", u8"クリックしてこのPREの名前を編集します。" },
    { "Spectrum size", u8"Spectrumの大きさ" },
    { "Cycle POST Analysis between 100, 125, 150, and 200 percent",
      u8"POSTの解析の大きさを100、125、150、200%で切り替えます" },

    // The OS Guide details.
    { "Sources  %1", u8"音源  %1" },
    { "Source  %1", u8"音源  %1" },
    { "Channel  %1", u8"チャンネル  %1" },
    { "Frequency focus  %1", u8"確認する周波数範囲  %1" },  // Kirin OS で選んだ範囲（2026-10-18）
    { "Next frequency focus  %1", u8"次に確認する周波数範囲  %1" },  // 次の項目の範囲（始まる前）
    { "%1 to %2 Hz", u8"%1〜%2 Hz" },

    // Hypha information.
    { "Loaded v%1", u8"読み込んだ版：v%1" },
    { "Update information and downloads (English)", u8"更新情報とダウンロード（英語）" },
    { "Release notes", u8"リリースノート" },
    { "Downloads (English)", u8"ダウンロード（英語）" },
    { "Downloads (Japanese)", u8"ダウンロード（日本語）" },
    { "Copy official URL", u8"公式URLをコピー" },
    { "Updating PRE and POST together", u8"PREとPOSTをまとめて更新する" },
    { "Save work, close the DAW, then install both",
      u8"作業を保存してDAWを閉じ、両方をインストールします" },
    { "Restart / rescan; check both loaded versions",
      u8"再起動または再スキャンして、両方の読み込んだ版を確認します" },
    { "Could not open the browser", u8"ブラウザを開けませんでした" },

    // The chain timing in POST's information.
    { "PRE to POST chain", u8"PREからPOSTまでのチェーン" },
    { "Elapsed %1 ms typical / %2 ms peak", u8"経過時間  通常 %1 ms / Peak %2 ms" },
    { "Share of a %1 ms block: %2% typical / %3% peak",
      u8"ブロック長（%1 ms）に対する割合  通常 %2% / Peak %3%" },
    { "Counted %1% of blocks (%2)", u8"計測できたブロック %1%（%2）" },
    { "Elapsed time between PRE and POST, not CPU usage",
      u8"PREとPOSTの間の経過時間です。CPU使用率ではありません" },
    { "Waiting for audio callbacks", u8"音声の処理が始まるのを待っています" },
    { "Not measured: %1", u8"計測できません：%1" },
    { "PRE timing is not available", u8"PREの時刻を読めません" },
    { "playback is stopped", u8"再生が止まっています" },
    { "PRE is feeding PRE/POST LISTEN", u8"PREがPRE/POST LISTENへ音声を送っています" },
    { "PRE and POST run on different threads", u8"PREとPOSTが別のスレッドで動いています" },
    { "PRE and POST are not called once each per block",
      u8"PREとPOSTが1ブロックに1回ずつ呼ばれていません" },
    { "PRE and POST get different block lengths", u8"PREとPOSTのブロック長が違います" },
    { "PRE is not confirmed to run before POST", u8"PREがPOSTより先に動くことを確認できません" },
    { "Show in the footer", u8"下部に常時表示" },
    { "Align PRE automatically", u8"PREの位置を自動で合わせる" },
    { "Footer chain time changed for this session only",
      u8"下部のチェーン表示は、このセッションだけ変更しました" },

    // Saving and attaching a measurement image.
    { "Capture format", u8"画像の形式" },
    { "1200 x 630  Landscape", u8"1200 x 630  横長" },
    { "1080 x 1080  Square", u8"1080 x 1080  正方形" },
    { "1080 x 1350  Portrait", u8"1080 x 1350  縦長" },
    { "Connected Work", u8"接続中のWork" },
    { "Attach to Work - connect from Kirin OS", u8"Workに添付 - Kirin OSから接続してください" },
    { "Attach to Work - Kirin OS required", u8"Workに添付 - Kirin OSが必要です" },
    { "Attach to Work - %1", u8"Workに添付 - %1" },
    { "Privacy - private by default", u8"プライバシー - 初期は非公開" },
    { "Include OS Guide", u8"OS Guideを含める" },
    { "Include PRE name", u8"PREの名前を含める" },
    { "Include POST name", u8"POSTの名前を含める" },
    { "Include project name", u8"プロジェクト名を含める" },
                                                    { "Save Hypha capture", u8"Hyphaの画像を保存" },

    // The choice when MATCH would raise PRE above the true-peak ceiling.
    { "PRE needs %1; TP ceiling allows %2", u8"PREは%1必要、TP上限までは%2" },
    { "Lower POST by %1; PRE stays at its level", u8"POSTを%1下げる（PREは元の音量のまま）" },
    { "Raise PRE by %1 only (TP LIMIT)", u8"PREを%1だけ上げる（TP LIMIT）" },
    // MATCH once matched: again, or AUTO (INV-LC16).
    { "MATCH again", u8"もう一度MATCH" },
    { "AUTO: follow POST within 0.5 dB", u8"AUTO：POSTに0.5 dB以内で追従" },
    { "Stop AUTO", u8"AUTOを止める" },
    { "AUTO needs a MATCH without TP LIMIT", u8"AUTOはTP LIMITのないMATCHの後に使えます" },
    // The NOTE dialog.
    { "Attach a note to the current sample position.", u8"今の位置にメモを付けます。" },
    { "ADD", u8"追加" },
    { "CANCEL", u8"中止" },
};
}

Section menuSection() noexcept
{
    return { "menus", entries, sizeof (entries) / sizeof (entries[0]) };
}
}
