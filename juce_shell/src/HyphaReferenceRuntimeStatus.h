#pragma once

#include "HyphaReferenceComponent.h"

// 2026-10-06：Reference の状態の文と REF のアクション（ボタンの文と押したときの意図）を、Kirin OS と runtime の状態から
// 決める。editor の中の分岐から外した純粋な関数で、試験が全部の組み合わせの文を作れる（日本語の画面に英語が残ら
// ないかを、作った文で確かめる）。
namespace hypha::reference_ui
{
struct RuntimeStatus
{
    juce::String status;
    ActionIntent action;     // 押したときの意図（ボタンの文と一緒に決める）
    juce::String actionText; // ボタンの文（空ならボタンを出さない）
    bool request = false;    // Kirin OS へ頼んだこと（Preset・曲の準備、Kirin OS で開く、Blind の承認）の途中の文
};

// `state` からは、先に決めた Kirin OS とのつながり・A・Blind の承認の項目だけを読む（osAccess・auditionBuffered・
// aAvailable・blindLowerAApprovalRequired・blindRequiredAAttenuationDb・blindLargeScreen）。
RuntimeStatus runtimeStatus (const reference_audition::Snapshot& runtime, const State& state);
}
