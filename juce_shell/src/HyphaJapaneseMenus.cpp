#include "HyphaJapaneseCatalog.h"

// MENU and the other menus, the PRE connection and its help, and the NOTE dialog. The short
// notices that answer an action are in HyphaJapaneseNotices.cpp.
namespace hypha::i18n::catalog
{
namespace
{
const Entry entries[] = {
    // MENU.
    { "Stop selected pair", u8"選択中のペアを止める" },
    { "All Stop: active POSTs", u8"All Stop：動作中のPOSTすべて" },
    { "Keep selected pair", u8"選択中のペアでKeep" },
    { "All Keep: Kirin OS required", u8"All Keep：Kirin OSが必要です" },
    { "All Keep: %1 ready POSTs", u8"All Keep：準備済みのPOST %1" },
    { "All Keep: %1 ready POST", u8"All Keep：準備済みのPOST %1" },
    { "Record / Keep: mono / stereo only", u8"Record／Keep：モノラル／ステレオのみ" },
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
    { "Meter context", u8"計測の対象" },
    { "2MIX / Mix or master bus / continuous sections",
      u8"2MIX / ミックスまたはマスターのバス / 途切れない音" },
    { "TRACK / STEM / individual or group bus / sparse events",
      u8"TRACK / STEM / 個別またはグループのバス / まばらな音" },
    { "Observation view", u8"表示する観測" },
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
    { "Use POST only", u8"POSTだけで使う" },
    { "No available PRE", u8"使えるPREがありません" },
    { "In use by another POST: %1", u8"別のPOSTが使用中：%1" },
    { "Use PRE: %1", u8"このPREを使う：%1" },
    { "SELECT PRE", u8"PREを選択" },
    { "PAIR SELECT PRE", u8"PAIR PREを選択" },
    { "Pair selection is locked during playback", u8"再生中はペアを変えられません" },
    { "Click to choose one exact PRE.", u8"クリックして、PREをひとつ選びます。" },
    { "Pair menu", u8"ペアのメニュー" },
    { "Choose one exact PRE connection", u8"接続するPREをひとつ選びます" },
    { "Click to edit this PRE name.", u8"クリックしてこのPREの名前を編集します。" },
    { "Connect this PRE: %1. The arrow opens all candidates.",
      u8"このPREに接続します：%1。矢印を押すと候補をすべて表示します。" },
    { "Spectrum size", u8"スペクトラムの大きさ" },
    { "Cycle POST Analysis between 100, 125, 150, and 200 percent",
      u8"POSTの解析の大きさを100、125、150、200%で切り替えます" },

    // The OS Guide details.
    { "Sources  %1", u8"ソース  %1" },
    { "Source  %1", u8"ソース  %1" },
    { "Channel  %1", u8"チャンネル  %1" },
    { "%1 to %2 Hz", u8"%1〜%2 Hz" },

    // Hypha information.
    { "Loaded v%1", u8"読み込んだ版：v%1" },
    { "Official release identity not verified", u8"公式リリースであることを確認していません" },
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
    { "Lower POST by %1; PRE stays at its level", u8"POSTを%1下げる（PREは原音量のまま）" },
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
