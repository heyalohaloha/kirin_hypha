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
    { "Play A for the length of the Cue, then MATCH again.", u8"Cueの長さだけAを再生してから、もう一度MATCHを押してください。" },
    { "MATCH exceeds the safe level. The current gain is kept.", u8"MATCHが安全上限を超えます。今のgainを保ちます。" },
};
}

Section referenceAbcvSection() noexcept
{
    return { "reference abcv", entries, sizeof (entries) / sizeof (entries[0]) };
}
}
