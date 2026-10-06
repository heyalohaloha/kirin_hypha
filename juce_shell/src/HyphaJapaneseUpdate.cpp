#include "HyphaJapaneseCatalog.h"

namespace hypha::i18n::catalog
{
namespace
{
const Entry entries[] = {
    { "Update check", u8"更新確認" },
    { "Automatic update checks (at most once per 24 hours)", u8"自動更新確認（24時間に最大1回）" },
    { "Check for updates now", u8"今すぐ更新を確認" },
    { "No official version has been checked", u8"公式の最新版は未確認です" },
    { "Checking for updates...", u8"更新を確認中…" },
    { "This Hypha version is up to date", u8"このHyphaは最新版です" },
    { "Latest official version: v%1", u8"公式の最新版：v%1" },
    { "New official version available: v%1", u8"公式の新版があります：v%1" },
    { "Development build; official version: v%1", u8"開発版です。公式の版：v%1" },
    { "Update check failed; use the official downloads page", u8"更新確認に失敗。公式のダウンロードページを確認してください" },
    { "Update check failed", u8"更新確認に失敗しました" },
    { "Official version information is unavailable", u8"公式の最新版の情報を取得できません" },
    { "Official version unavailable", u8"公式の版の情報を取得できません" },
    { "Previously announced version is no longer offered", u8"以前に案内した版の配布は停止されています" },
    { "Update offer withdrawn", u8"更新の配布は停止されています" },
    { "Development build", u8"開発版です" },
    { "Last update check: %1", u8"前回の更新確認：%1" },
    { "Next permitted check: %1", u8"次に確認できる日時：%1" },
    { "Update requests are limited to once per 24 hours", u8"更新通信は24時間に最大1回です" },
    { "Update check is already in progress", u8"更新確認はすでに進行中です" },
    { "Update check requested", u8"更新確認を要求しました" },
    { "Update check could not be requested", u8"更新確認を要求できませんでした" },
    { "Automatic update checks enabled", u8"自動更新確認をONにしました" },
    { "Automatic update checks disabled", u8"自動更新確認をOFFにしました" },
    { "Changing update check preference...", u8"更新確認の設定を変更中…" },
    { "Update check preference could not be saved", u8"更新確認の設定を保存できませんでした" },
    { "Update check setting changed in another Hypha window", u8"別のHypha操作で設定が変わりました" },
    { "Hypha v%1 is available", u8"Hypha v%1があります" },
    { "Hypha v%1 available; open PRE / POST for downloads", u8"Hypha v%1があります。PRE／POSTから取得" },
};
}

Section updateSection() noexcept
{
    return { "update", entries, sizeof (entries) / sizeof (entries[0]) };
}
}
