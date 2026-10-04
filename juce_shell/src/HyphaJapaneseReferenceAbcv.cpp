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
    { "MATCHED / C %1 dB", u8"MATCH済み / C %1 dB" },
    { "ON PLAY / C %1 dB", u8"鳴らすとき / C %1 dB" },
    { "A WAITING", u8"A待ち" },
    { "A LAST %1 S", u8"A直近%1秒" },
    { "A %1 / %2 S", u8"A %1 / %2秒" },
    // 2026-10-04：図の凡例の比べる側（Daisuke「A直近10秒 / Bサビ」）。B は鳴らす部分の時刻を添える。
    { "B CHORUS %1", u8"Bサビ %1" },
    { "B LOUDEST 30 S %1", u8"B最も大きい30秒 %1" },
    { "B CUE %1", u8"BのCue %1" },
    { "B CHORUS", u8"Bサビ" },
    { "B LOUDEST 30 S", u8"B最も大きい30秒" },
    { "B CUE", u8"BのCue" },
    { "B WHOLE", u8"B曲全体" },
    { "C CHORUS", u8"Cサビ" },
    { "C LOUDEST 30 S", u8"C最も大きい30秒" },
    { "C CUE", u8"CのCue" },
    { "C WHOLE", u8"C曲全体" },
    { "B SET RANGE", u8"Bセットの範囲" },  // B セットの曲の p10〜p90 の帯
    // Blauert の帯（HyphaReferenceBlauertZones.h）：1 kHz 付近と 300-400 Hz・3-4 kHz の差の、比べる側 − A。
    { u8"1k vs 300-400·3-4k %1-A %2 dB", u8"1kと300-400·3-4kの差 %1-A %2 dB" },
    { "SAME SECTION %1 S", u8"同じ区間 %1秒" },
    // 範囲の帯（HyphaReferenceRangeStrips.cpp）の項目名と、V の時間の線の見出し（「CREST (TP/RMS) / OVER TIME」）。
    { "CREST (TP/RMS)", u8"クレスト (TP/RMS)" },
    { "LOUDNESS MOVEMENT (LUFS-S)", u8"音量の動き (LUFS-S)" },
    { "ONSET (RISE PER HOP)", u8"立ち上がり (区間ごと)" },
    { "OVER TIME", u8"時間の動き" },
    // Kirin OS の標準の名前（HyphaReferenceDisplayText.h が英語の名前にそろえる）。日本語の画面では Kirin OS の日本語名で出す。
    { u8"Mastering · tone, level, and dynamics", u8"Mastering｜音色・音量・ダイナミクス" },
    { u8"MIX · balance, position, and space", u8"MIX｜バランス・定位・空間" },
    { u8"Recording · tone, performance, and capture", u8"録音｜音色・演奏・収録状態" },
    { u8"Album context", u8"Album 全体との関係" },
    { u8"Arrangement · roles, density, and development", u8"編曲｜役割・密度・展開" },
    { u8"Energy curve", u8"曲中のエネルギー変化" },
    { u8"Performance dynamics", u8"演奏のダイナミクス" },
    { u8"Frequency space", u8"周波数帯域の使い方" },
    { u8"Hook placement", u8"聴かせどころの位置" },
    { u8"Low-volume balance", u8"小音量時のバランス" },
    { u8"Section difference", u8"セクション間の違い" },
    { u8"All stages · 5 essential checks", u8"全工程｜基本5項目" },
    { u8"All stages · 3 essential checks", u8"全工程｜基本3項目" },
    { u8"Pitch and timing", u8"音程とタイミング" },
    { u8"Genre context", u8"ジャンルとの関係" },
    { u8"Delivery context", u8"納品条件との関係" },
    { u8"Loudest 30 s", u8"最も大きい30秒" },
    { u8"Instrumentation and roles", u8"楽器編成と役割" },
    { u8"Kick and bass", u8"キックとベース" },
    { u8"Tonal balance", u8"音色のバランス" },
    { u8"Stereo and phase", u8"ステレオと位相" },
    { u8"Separation and bleed", u8"分離とかぶり" },
    { u8"Phase and microphones", u8"位相とマイク" },
    { u8"Low-end consistency", u8"低域の安定性" },
    { u8"Lead position", u8"主役の位置" },
    { u8"Production stage", u8"制作段階" },
    { u8"Chorus candidate", u8"サビ候補" },
    { u8"High end", u8"高域" },
    { "%1 DB UNDER A (PEAK LIMIT)", u8"Aより%1 dB小さい（ピーク上限）" },
    // 耳で聴き比べる Check（V と C の画面。Kirin OS の「この項目は耳で聴き比べます」と同じ言い方）。
    { "Compared by listening. Hypha shows no result it has not measured. Press A and %1 to switch at the same level.",
      u8"この項目は耳で聴き比べます。Hyphaは測っていない結果を出しません。Aと%1を押して、同じ音量で切り替えてください。" },  // V の Check のタブの凡例（A と V の色は線の見本で示す）
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
    { "A lowered %1 dB to match: A got louder after the offer.", u8"合わせるためにAを%1 dB下げました（承認の後にAが大きくなりました）。" },
    { "PRE / POST LISTEN is using POST. End it or press RETURN, then press the role again.",
      u8"PRE / POST LISTENがPOSTを使っています。終了するかRETURNを押してから、もう一度押してください。" },
};
}

Section referenceAbcvSection() noexcept
{
    return { "reference abcv", entries, sizeof (entries) / sizeof (entries[0]) };
}
}
