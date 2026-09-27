#include "HyphaJapaneseCatalog.h"

// The received Kirin OS Guide as Hypha shows it on the guide rail and in its details menu. The
// Guide's own labels and names come from Kirin OS and pass through; Hypha's states around them
// are translated, fact by fact.
namespace hypha::i18n::catalog
{
namespace
{
const Entry entries[] = {
    { "MASKING  NO TIMED ITEMS", u8"MASKING  時刻指定なし" },
    { "Measured guide retained", u8"計測済みのGuideを保持" },
    { "MASKING  RECEIVED", u8"MASKING  受信済み" },
    { "INSPECT  RECEIVED", u8"INSPECT  受信済み" },
    { "Legacy guide", u8"旧形式のGuide" },
    { "No timed items", u8"時刻指定なし" },
    { "Retained", u8"保持" },
    { "Timeline outside guide range", u8"時間軸がGuideの範囲外です" },
    { "Guide retained", u8"Guideを保持" },
    { "MASKING  NEXT %1", u8"MASKING  次 %1" },
    { "INSPECT  NEXT %1", u8"INSPECT  次 %1" },
    { "NEXT %1", u8"次 %1" },
    { "MASKING  END", u8"MASKING  終了" },
    { "INSPECT  END", u8"INSPECT  終了" },
    { "HELD", u8"保持" },
};
}

Section informationSection() noexcept
{
    return { "information", entries, sizeof (entries) / sizeof (entries[0]) };
}
}
