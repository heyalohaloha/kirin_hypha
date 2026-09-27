#include "HyphaJapaneseCatalog.h"

// Capture A in REF: the capture strip's state and action, its failures and what was kept. The
// detail is several lines joined by newlines and " / ", translated line by line and part by part.
namespace hypha::i18n::catalog
{
namespace
{
const Entry entries[] = {
    { "PLAY", u8"再生して" },
    { "CAPTURING", u8"取り込み中" },
    { "FINISH A", u8"Aを終える" },
    { "RESTORING", u8"復元中" },
    { "WAIT", u8"待機中" },
    { "CAPTURE A", u8"Aを取り込む" },
    { "CANCELLING", u8"中止処理中" },
    { "CAPTURED", u8"取り込み済" },
    { "PARTIAL", u8"一部のみ" },
    { "A DIFFERS", u8"Aが違います" },
    { "LAST: A DIFFERS", u8"前回：Aが違います" },
    { "RETRY FAILED", u8"再試行失敗" },
    { "Partial capture", u8"一部だけ取り込みました" },
    { "Capture unavailable", u8"取り込みを使えません" },
    { "Capture interrupted", u8"取り込みが中断しました" },
    { "Capture limit reached", u8"取り込みの上限に達しました" },
    { "Capture not saved", u8"取り込みを保存できませんでした" },
    { "Saved capture unavailable", u8"保存した取り込みを使えません" },
    { "previous kept", u8"前の取り込みを保持" },
    { "previous capture kept", u8"前の取り込みを保持" },
    { "capture again", u8"もう一度取り込んでください" },
    { "finish other capture or Blind", u8"ほかの取り込みかBlindを終えてください" },
    { "Interrupted", u8"中断しました" },
    { "Input unavailable", u8"入力を使えません" },
    { "Input interrupted", u8"入力が途切れました" },
    { "Audio input unavailable", u8"音声の入力を使えません" },
    // "Capture could not be saved" alone is the measurement image (HyphaJapaneseMenus.cpp).
    { "Capture could not be saved / previous capture kept",
      u8"取り込みを保存できませんでした / 前の取り込みを保持" },
    { "%1 s captured. Live A audio is unchanged.", u8"%1 s 取り込みました。今のAの音は変わりません。" },
    { "Capture original DAW input", u8"DAWの元の入力を取り込みます" },
    { "Stop this capture. Live A audio is unchanged.", u8"この取り込みを止めます。今のAの音は変わりません。" },
    { "Capture A, then play from the beginning. Stop the DAW to keep the captured range.",
      u8"Aを取り込みます。最初から再生し、DAWを止めるとその範囲を残します。" },
    { "Discard this capture and keep the previous one", u8"この取り込みを捨てて、前の取り込みを残します" },
    { "Displayed A: captured or live. Audio A always remains live.",
      u8"表示するA：取り込んだものか、今の音か。音のAは常に今の音です。" },
};
}

Section referenceCaptureSection() noexcept
{
    return { "reference capture", entries, sizeof (entries) / sizeof (entries[0]) };
}
}
