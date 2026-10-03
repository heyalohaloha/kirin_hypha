#include "HyphaJapaneseCatalog.h"

// REF の A／B／C／V（ABCV）で足した文。C の画面（H12）の曲・MATCH・同じ区間の見比べと、MATCH を
// やり直せなかったときの理由。CHECK SET・LOW 20-250 などの見出しと単位は英語のまま。
namespace hypha::i18n::catalog
{
namespace
{
const Entry entries[] = {
    { "NO SOURCE IN KIRIN OS", u8"Kirin OSに音源なし" },
    { "LEVEL NOT MATCHED", u8"音量未調整" },
    { "C Song", u8"Cの曲" },
    { "Choose the song C plays for this Check. A stays the current DAW input.",
      u8"このCheckでCが鳴らす曲を選びます。AはDAWの今の入力のままです。" },
    { "Match C to A again", u8"CをAにもう一度合わせる" },
    { "Match C to the latest A over the Cue's length again, and keep it fixed.",
      u8"Cueと同じ長さの直近のAにCをもう一度合わせ、固定します。" },
    { "Play C matched to the latest A over the Cue's length.", u8"Cueと同じ長さの直近のAに合わせてCを鳴らします。" },
    { "C is not playing. Press C to play it matched.", u8"Cは鳴っていません。Cを押すと合わせて鳴らします。" },
    { "This Check plays at its original level.", u8"このCheckは元の音量で鳴らします。" },
    { "Play A for the Cue length (up to 30 s), then MATCH again.", u8"Cueの長さ（最長30秒）だけAを再生してから、もう一度MATCHを押してください。" },
    { "This Check matches True Peak when C starts. Press A, then C.", u8"このCheckはCを鳴らすときにTrue Peakで合わせます。Aを押してからCを押してください。" },
    { "MATCH exceeds the safe level. The current gain is kept.", u8"MATCHが安全上限を超えます。今のgainを保ちます。" },
    { "PLAY A WITH V ALIGNED", u8"Vを位置合わせしてAを再生" },
    // H6：待ちが上限を超えたときの理由と直し方（「V: %1」の %1 にそのまま入る）。
    { "A LEVEL NOT MEASURED IN 10 S OF PLAY / PLAY A LONGER, THEN SELECT AGAIN",
      u8"再生10秒でAを測れません / Aを長めに再生して選び直す" },
    { "KIRIN OS IS NOT RESPONDING / OPEN KIRIN OS", u8"Kirin OSが応答しません / Kirin OSを開く" },
    { "SOURCE NOT VERIFIED IN 10 S / CHECK THE SOURCE IN KIRIN OS", u8"10秒で音源を確認できません / Kirin OSで確認" },
    { "AUDIO NOT LOADED IN 10 S / PLAY FROM ANOTHER POSITION", u8"10秒で読み込めません / 別の位置から再生" },
    { "NOT PREPARED IN 10 S / OPEN THE SOURCE IN KIRIN OS", u8"10秒で準備できません / Kirin OSで開く" },
    { "NO MATCH IN 30 S OF PLAY / CHOOSE THE VERSION AGAIN", u8"再生30秒で位置が合いません / Versionを選び直す" },
    { "V is measured over the same section as A while they are aligned",
      u8"位置合わせしているあいだ、VをAと同じ区間で測ります" },
    { "A LEVEL NOT MEASURED IN 35 S OF PLAY / PLAY A LONGER, THEN SELECT AGAIN",
      u8"再生35秒でAを測れません / Aを長めに再生して選び直す" },
    // 追従を MATCH から ±6 dB で止めたとき（live 比較の AUTO と同じ幅）。
    { "%1 FOLLOW STOPPED 6 DB FROM MATCH", u8"%1の追従はMATCHから6 dBで停止" },
    { "Level follow stopped 6 dB from the MATCH. The current gain is kept.",
      u8"音量の追従はMATCHから6 dBで停止。今のgainを保ちます。" },
    // Kirin OS のセット（sets.json）を読めなかったとき。
    { "B SET NOT READ / UPDATE KIRIN OS AND HYPHA", u8"Bセットを読めません / Kirin OSとHyphaを更新" },
    { "Some Kirin OS sets were not read. Update Kirin OS and Hypha.",
      u8"Kirin OSのセットの一部を読めませんでした。Kirin OSとHyphaを更新してください。" },
    // V のタブ（その Check の表示に合わせる）。
    { "V compares spectrum and balance. This Check is shown on C.",
      u8"Vはスペクトルとバランスを比べます。このCheckはCの画面で見ます。" },
    // C の画面の MATCH の横と Cue の凡例。
    { "MATCHED / C %1 dB / FIXED", u8"MATCH済み / C %1 dB / 固定" },
    { "ON PLAY / C %1 dB", u8"鳴らすとき / C %1 dB" },
    { "A WAITING", u8"A待ち" },
    { "A LAST %1 S", u8"A直近%1秒" },
    { "A %1 / %2 S", u8"A %1 / %2秒" },
    // K13b：Kirin OS の準備の状態（B の一覧の短い語と、状態の行の「理由 / 直し方」）。
    { "NOT FOUND", u8"見つからない" },
    { "CHECKING", u8"確認中" },
    { "%1 AHEAD", u8"前に%1曲" },
    { "KIRIN OS CANNOT FIND THE FILE / IT RETRIES ONCE SOON", u8"Kirin OSがファイルを確かめられません / まもなく再試行" },
    { "KIRIN OS CANNOT FIND THE FILE / RETRY IN KIRIN OS", u8"Kirin OSがファイルを確かめられません / Kirin OSで再試行" },
    { "KIRIN OS IS CHECKING THE FILE", u8"Kirin OSがファイルを確かめています" },
    { "KIRIN OS IS MEASURING THE SONG", u8"Kirin OSが曲を測っています" },
    { "KIRIN OS WAITS FOR ANOTHER MEASUREMENT", u8"Kirin OSはほかの測定を待っています" },
    { "KIRIN OS PREPARES THIS SONG NEXT", u8"Kirin OSが次にこの曲を準備します" },
    { "KIRIN OS PREPARES 1 SONG FIRST", u8"Kirin OSが先に1曲を準備しています" },
    { "KIRIN OS PREPARES %1 SONGS FIRST", u8"Kirin OSが先に%1曲を準備しています" },
    // H15：Kirin OS の中の「Hypha ではこう見える」（描画の道具）。
    { "PREVIEW / DAW INPUT NOT AVAILABLE", u8"プレビュー / DAWの入力はありません" },
    { "KIRIN OS PREVIEW", u8"Kirin OSのプレビュー" },
    // 2026-10-03（R-12）：上限超えの MATCH は、承認して A を下げて合わせる。
    { "%1 NEEDS A %2 DB LOWER", u8"%1はAを%2 dB下げると合う" },
    { "LOWER A TO MATCH", u8"Aを下げて合わせる" },
    { "LOWER A %1 DB & PLAY %2", u8"Aを%1 dB下げて%2を鳴らす" },
    { "A LOWERED %1 DB", u8"Aを%1 dB下げ中" },
    { "%1 needs A %2 dB lower to match. Press LOWER A.", u8"%1はAを%2 dB下げると合います。「Aを下げて…」を押してください。" },
    { "A was not lowered. Press the role again.", u8"Aは下げていません。もう一度押してください。" },
};
}

Section referenceAbcvSection() noexcept
{
    return { "reference abcv", entries, sizeof (entries) / sizeof (entries[0]) };
}
}
