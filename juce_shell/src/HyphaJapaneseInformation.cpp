#include "HyphaJapaneseCatalog.h"

// The received Kirin OS Guide as Hypha shows it on the guide rail and in its details menu. The
// Guide's own labels and names come from Kirin OS and pass through; Hypha's states around them
// are translated, fact by fact. The information menu's operating system note is here too.
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
    { "Timeline outside guide range", u8"再生位置がGuideの範囲外です" },
    { "Guide retained", u8"Guideを保持" },
    { "MASKING  NEXT %1", u8"MASKING  次 %1" },
    { "INSPECT  NEXT %1", u8"INSPECT  次 %1" },
    { "NEXT %1", u8"次 %1" },
    { "MASKING  END", u8"MASKING  終了" },
    { "INSPECT  END", u8"INSPECT  終了" },
    { "HELD", u8"HOLD中" },
    { "This host reports macOS %1 for compatibility",
      u8"このホストは互換のため、macOSのバージョンを%1と伝えています" },
    { "This host does not report the macOS version", u8"このホストはmacOSのバージョンを伝えていません" },
};
}

Section informationSection() noexcept
{
    return { "information", entries, sizeof (entries) / sizeof (entries[0]) };
}
}
