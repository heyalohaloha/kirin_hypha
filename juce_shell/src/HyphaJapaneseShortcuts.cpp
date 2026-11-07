#include "HyphaJapaneseCatalog.h"

namespace hypha::i18n::catalog
{
namespace
{
const Entry entries[] = {
    { "Press Return to choose a PRE", u8"Returnを押してPREを選びます" },
    { "Press Return to edit the PRE name", u8"Returnを押してPREの名前を編集します" },
    { "LISTEN is not active", u8"LISTENが有効ではありません" },
    { "MATCH first", u8"先にMATCHしてください" },
    { "Reset Meter Session?", u8"Meter Sessionをリセットしますか" },
    { "Available in POST", u8"POSTで使用できます" },
    { "Close KEYS", u8"KEYSを閉じます" },
    { "Available in REF", u8"REFで使えます" },
    { "Available in TIME", u8"TIMEで使えます" },
    { "Available in C", u8"Cで使えます" },
    { "Available in V", u8"Vで使えます" },
    { "Available in C and V", u8"CとVで使えます" },
    { "Available at 300%", u8"300%で使えます" },
    { "Available from 125%", u8"125%以上で使えます" },
    { "Not available on this page", u8"この画面では使えません" },
    { "Nothing to choose", u8"選べる項目がありません" },
    { "First in the list", u8"一覧の先頭です" },
    { "Last in the list", u8"一覧の末尾です" },
    { "LISTEN is on", u8"LISTEN中です" },
    { "A/B is on", u8"A/B中です" },
    { "BLIND is not active", u8"BLINDは動いていません" },
    { "POST is at its normal level", u8"POSTは通常の音量です" },
    { "Hear both sources first", u8"両方のSOURCEを聴いた後に使えます" },
    { "Choose a Version in V", u8"VでVersionを選ぶと使えます" },
};
}

Section shortcutSection() noexcept
{
    return { "shortcuts", entries, sizeof (entries) / sizeof (entries[0]) };
}
}
