#pragma once

// 2026-10-06：日本語の画面に英語が残らない。状態の行・知らせ・案内・説明の行の文を、画面と同じ関数で状態の組み合わせ
// から全部作って日本語にし、英字の語に分けて、許可の一覧に無い語が残れば落とす（「訳した文が元と違う」を見るだけでは、
// 「B：KIRIN OS PREPARES 2 SONGS FIRST」のように中身が英語のまま通っていた）。許可の一覧は語の単位：役の文字・単位・
// 製品の名前・英語のまま残すボタンの語など（文の単位の scripts/screen_text_kept.tsv とは別）。
#include "ReferenceGuideContractTest.h"
#include "../src/HyphaReferenceHelpText.h"
#include "../src/HyphaReferenceNotices.h"
#include "../src/HyphaReferencePendingUI.h"
#include "../src/HyphaReferencePreparationWatch.h"
#include "../src/HyphaReferenceRuntimeStatus.h"
#include "../src/HyphaReferenceStatusModel.h"

#include <set>

namespace hypha::tests
{
namespace screen_text_japanese
{
// 日本語の画面に英字のまま残してよい語。
inline const std::set<juce::String>& keptWords()
{
    static const std::set<juce::String> words {
        // 役と経路
        "A", "B", "C", "V", "REF", "PRE", "POST",
        // 製品と外の名前
        "Kirin", "OS", "Hypha", "DAW", "Blauert",
        // 単位と略語
        "dB", "dBTP", "dBFS", "LU", "LUFS", "I", "M", "S", "Hz", "kHz", "k", "s", "pt", "p", "TP", "RMS", "Mid", "Side",
        // Kirin OS の画面と同じ名前
        "Reference", "Version", "Check", "Cue", "Preset",
        // 英語のまま残すボタン・欄・機能の語（簡単な英語は訳さない）
        "MATCH", "BLIND", "Blind", "Gain", "Match", "gain", "WHOLE", "CHECK",
    };
    return words;
}

// 英字の続き（A〜Z・a〜z）を語として取り出す。
inline juce::StringArray englishWords (const juce::String& text)
{
    juce::StringArray words;
    juce::String word;
    for (int index = 0; index <= text.length(); ++index)
    {
        const auto character = index < text.length() ? text[index] : juce::juce_wchar (0);
        if ((character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z'))
        {
            word += juce::String::charToString (character);
            continue;
        }
        if (word.isNotEmpty()) words.add (word);
        word.clear();
    }
    return words;
}

// 画面の状態の文と REF のボタンの文：runtime の状態を 1 つずつ変えて、画面と同じ関数で作る。
inline void addRuntimeStatuses (std::set<juce::String>& texts)
{
    using reference_audition::BlindPhase;
    using reference_audition::RuntimeState;
    reference_ui::State facts;
    facts.osAccess = os_access::State::ready;
    facts.auditionBuffered = facts.aAvailable = true;
    const auto add = [&texts] (const reference_ui::RuntimeStatus& status)
    {
        texts.insert (status.status);
        if (status.actionText.isNotEmpty()) texts.insert (status.actionText);
    };
    reference_audition::Snapshot base;
    base.state = RuntimeState::ready;
    base.comparisonSlot = 2;
    for (const auto phase : { BlindPhase::starting, BlindPhase::active, BlindPhase::revealed, BlindPhase::invalidated })
        for (const bool playing : { true, false })
            for (const int stimulus : { 0, 1 })
                for (const double attenuation : { 0.0, 2.5 })
                {
                    auto runtime = base;
                    runtime.blindPhase = phase; runtime.transportPlaying = playing;
                    runtime.activeBlindStimulus = stimulus; runtime.blindRequiredAAttenuationDb = attenuation;
                    add (reference_ui::runtimeStatus (runtime, facts));
                }
    for (const int slot : { 1, 2, 3 })
    {
        auto runtime = base;
        runtime.bSelected = true; runtime.audibleComparisonSlot = slot; runtime.comparisonSlot = slot;
        add (reference_ui::runtimeStatus (runtime, facts));
        for (const bool buffered : { true, false })
            for (const bool outside : { true, false })
                for (const bool aAvailable : { true, false })
                {
                    auto ready = base;
                    ready.comparisonSlot = slot; ready.auditionOutsideCue = outside;
                    auto readyFacts = facts;
                    readyFacts.auditionBuffered = buffered; readyFacts.aAvailable = aAvailable;
                    add (reference_ui::runtimeStatus (ready, readyFacts));
                }
    }
    for (const auto access : { os_access::State::unowned, os_access::State::ownedDisconnected })
    {
        auto accessFacts = facts;
        accessFacts.osAccess = access;
        add (reference_ui::runtimeStatus (base, accessFacts));
    }
    for (const auto state : { RuntimeState::disconnected, RuntimeState::waiting, RuntimeState::verifying, RuntimeState::rejected })
        for (const auto* code : { "", "reference_selection_unavailable", "reference_version_unselected",
                                  "reference_alignment_waiting_for_content", "reference_alignment_no_match",
                                  "reference_alignment_ambiguous", "reference_candidates_empty", "reference_checks_empty",
                                  "reference_source_unavailable", "source_changed", "source_open_failed", "reference_source_changed",
                                  "reference_source_audio_mismatch", "another_code" })
        {
            auto runtime = base;
            runtime.state = state; runtime.rejectionCode = code;
            runtime.libraryReceived = true; runtime.presetId = "preset";
            add (reference_ui::runtimeStatus (runtime, facts));
        }
    const juce::StringArray preparation { "pending", "prepared", "timed_out", "preset_setup_required", "source_unavailable",
                                          "measurement_required", "request_stale", "storage_unavailable", "publication_failed",
                                          "work_unavailable", "request_invalid", "another_status" };
    for (const auto& status : preparation)
    {
        auto preset = base;
        preset.presetSelectionStatus = status;
        add (reference_ui::runtimeStatus (preset, facts));
        auto candidate = base;
        candidate.candidatePreparationStatus = status;
        add (reference_ui::runtimeStatus (candidate, facts));
    }
    for (const auto* status : { "pending", "opened", "exact_opened", "safe_fallback_opened", "rejected", "timed_out" })
    {
        auto runtime = base;
        runtime.recoveryStatus = status;
        add (reference_ui::runtimeStatus (runtime, facts));
    }
    for (const bool large : { true, false })
    {
        auto blindFacts = facts;
        blindFacts.blindLowerAApprovalRequired = true; blindFacts.blindRequiredAAttenuationDb = 3.5; blindFacts.blindLargeScreen = large;
        add (reference_ui::runtimeStatus (base, blindFacts));
    }
}

// 状態の行：役・段階・Kirin OS の準備・押した役の待ち・聴いている役の合わせ方・下げた A の組み合わせ。
inline void addStatusLines (std::set<juce::String>& texts, const std::set<juce::String>& statuses)
{
    using reference_ui::SourceStep;
    using Stage = reference_audition::PendingAuditionView::Stage;
    using Tracking = reference_audition::TrackingState;
    reference_ui::State base;
    base.separateComparisons = base.libraryReceived = base.osOnline = base.aAvailable = base.auditionBuffered = true;
    base.osAccess = os_access::State::ready;
    base.readiness = reference_ui::Readiness::ready;
    base.songSets = { { "set", "Set   1 / 1" } };
    const auto add = [&texts] (const reference_ui::State& state)
    {
        for (const bool footer : { false, true }) texts.insert (reference_ui::referenceStatusLine (state, footer).text);
    };
    std::vector<SourceStep> steps;  // 段階の表の全部（足した段階もここに入る）
    for (int index = 0; index <= static_cast<int> (SourceStep::attention); ++index) steps.push_back (static_cast<SourceStep> (index));
    const std::vector<reference_audition::RuntimeSongPreparation> preparations {
        { "pending", "queued", {}, {}, "working", 0 }, { "pending", "queued", {}, {}, "working", 1 },
        { "pending", "queued", {}, {}, "working", 2 }, { "pending", "resolving", {}, {}, "working", 0 },
        { "pending", "measuring", {}, {}, "working", 0 }, { "pending", "queued", {}, {}, "waiting", 0 },
        { "pending", {}, "source_unavailable", "manual", "idle", 0 } };
    for (const auto& status : statuses)
        for (const int slot : { 1, 2, 3 })
            for (const double held : { 0.0, -2.5 })
            {
                auto state = base;
                state.status = status; state.comparisonSlot = slot; state.heldAttenuationDb = held;
                add (state);
            }
    for (const int slot : { 1, 2, 3 })
    {
        for (const auto step : steps)
        {
            auto state = base;
            state.status = "READY / A REMAINS LIVE";
            state.comparisonSlot = slot;
            state.versionStep = state.checkStep = state.referenceStep = step;
            for (const bool online : { true, false }) { state.osOnline = online; add (state); }
            for (const auto& preparation : preparations) { state.rolePreparation = preparation; add (state); }
            state.rolePreparation = {};
            for (const bool measuringA : { false, true })
                for (const bool online : { true, false })
                {
                    reference_ui::PreparationWatch watch;
                    watch.observe (slot, step, measuringA, online, true, 0.0);
                    double now = 0.0;
                    juce::String overdue;
                    while (now < 60.0 && overdue.isEmpty()) overdue = watch.observe (slot, step, measuringA, online, true, now += 0.5);
                    state.preparationOverdue = overdue;
                    if (overdue.isNotEmpty()) texts.insert (overdue);
                    add (state);
                }
            state.preparationOverdue.clear();
            for (const auto stage : { Stage::play, Stage::checking, Stage::level, Stage::sourceChanged, Stage::safetyChanged,
                                      Stage::startFailed, Stage::sourceLevelUnavailable, Stage::ceilingExceeded })
            {
                state.pendingAudition = { slot, stage };
                add (state);
            }
            state.pendingAudition = {};
        }
        auto playing = base;
        playing.bSelected = true; playing.audibleComparisonSlot = playing.comparisonSlot = slot;
        playing.appliedGainDb = 1.5;
        for (const auto tracking : { Tracking::following, Tracking::stoppedCeiling, Tracking::stoppedRange, Tracking::fixed })
            for (const bool original : { false, true })
                for (const double shortfall : { 0.0, 0.3 })
                    for (const double held : { 0.0, -2.5 })
                    {
                        playing.tracking = tracking; playing.originalAudition = original;
                        playing.peakShortfallDb = shortfall; playing.heldAttenuationDb = held; playing.gainLimited = shortfall > 0.0;
                        add (playing);
                        texts.insert (reference_ui::gainReadout (playing));
                    }
        auto offered = base;
        offered.comparisonSlot = slot;
        offered.status = reference_ui::notice::lowerAOfferStatus (slot, -2.5);
        texts.insert (reference_ui::notice::lowerAOfferAction (slot, -2.5));
        offered.action = { reference_ui::ActionKind::lowerAAndPlay, { slot, -2.5, "id", 1, 1 } };
        add (offered);
    }
    auto noSet = base;
    noSet.comparisonSlot = 3;
    noSet.songSets.clear();
    for (const auto* issue : { "", "unreadable" }) { noSet.songSetsIssue = issue; add (noSet); }
}

// 案内（REF の始め方）と、まだ鳴らせない役を押したときの理由・知らせ。
inline void addGuidesAndNotices (std::set<juce::String>& texts)
{
    using reference_ui::SourceStep;
    using Failure = reference_audition::MatchFailure;
    for (int step = 0; step <= static_cast<int> (SourceStep::attention); ++step)
    {
        const auto source = static_cast<SourceStep> (step);
        texts.insert (reference_ui::stepText (source));
        for (const int slot : { 1, 2, 3 })
            for (const auto failure : { Failure::none, Failure::liveLevelUnavailable, Failure::sourceLevelUnavailable, Failure::ceilingExceeded })
                texts.insert (reference_ui::notice::roleUnavailable (slot, source, failure));
        for (const bool separate : { true, false })
            for (const bool online : { true, false })
            {
                reference_ui::State state;
                state.separateComparisons = separate; state.osOnline = online; state.libraryReceived = online;
                state.osAccess = online ? os_access::State::ready : os_access::State::ownedDisconnected;
                state.versionStep = state.checkStep = state.referenceStep = source;
                const auto guide = reference_ui::guide (state);
                texts.insert (guide.heading);
                texts.insert (guide.detail);
                texts.insert (reference_ui::unavailableText (state, true));
                texts.insert (reference_ui::unavailableText (state, false));
            }
    }
    for (const int slot : { 1, 2, 3 }) texts.insert (reference_ui::notice::lowerANeeded (slot, -2.5));
    texts.insert (reference_ui::notice::lowerAAgain (-3.5));
    for (const bool ceiling : { true, false }) texts.insert (reference_ui::notice::trackingStopped (ceiling));
    for (const int slot : { 1, 2 })
        for (const bool playable : { true, false }) texts.insert (reference_ui::notice::pageOpened (slot, playable));
    using Skipped = reference_audition::RuntimeSkippedItem;  // 名前は Kirin OS の中身（訳さない）なので、英字の無い名前で試す
    for (const bool unreadable : { true, false })
        for (const std::vector<Skipped>& items : { std::vector<Skipped> { { "01", unreadable } },
                                                   std::vector<Skipped> { { "01", unreadable }, { "02", unreadable } },
                                                   std::vector<Skipped> { { {}, unreadable } },
                                                   std::vector<Skipped> { { {}, unreadable }, { {}, unreadable } } })
            texts.insert (reference_ui::notice::librarySkipped (items));
    for (const auto* help : reference_ui::help_text::all) texts.insert (help);
}
}

inline void verifyReferenceJapaneseScreenText()
{
    using namespace reference_guide_contract;
    using namespace screen_text_japanese;
    std::set<juce::String> statuses, texts;
    addRuntimeStatuses (statuses);
    texts.insert (statuses.begin(), statuses.end());
    addStatusLines (texts, statuses);
    addGuidesAndNotices (texts);
    texts.erase (juce::String());
    require (texts.size() > 300, "the screen texts are built from every state (" + juce::String ((int) texts.size()) + ")");
    juce::String leftovers;
    int count = 0;
    for (const auto& english : texts)
    {
        const auto japanese = i18n::translate (english, i18n::Language::japanese);
        juce::StringArray left;
        for (const auto& word : englishWords (japanese))
            if (keptWords().count (word) == 0) left.addIfNotAlreadyThere (word);
        if (left.isEmpty()) continue;
        ++count;
        leftovers << "\n  [" << left.joinIntoString (" ") << "] " << english << "  ->  " << japanese;
    }
    require (count == 0, "no English is left on the Japanese screen (" + juce::String (count) + " texts):" + leftovers);
}
}
