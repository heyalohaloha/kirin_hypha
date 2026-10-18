#pragma once

// 2026-10-18：Kirin OS が準備した音源を Hypha が使えないとき、理由ごとに「理由 / 直し方」を言い、理由のコードを案内に
// 残す。知らない理由を「Kirin OS で準備し直す」と推測して言わない（Kirin OS では準備済みで直す手段が無かった）。
// 表示の正しさだけを確かめる。mono の音源を stereo の track で鳴らせないこと自体は変えない（別の判断）。
#include "ReferenceGuideContractTest.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaReferenceNotices.h"
#include "../src/HyphaReferenceRuntimeView.h"
#include "../src/HyphaReferenceStages.h"
#include "../src/HyphaReferenceSongChoice.h"
#include "../src/HyphaReferenceStatusModel.h"
#include "../src/HyphaTextStyle.h"

namespace hypha::tests
{
inline void verifyReferenceRejectionReasons()
{
    using namespace reference_guide_contract;
    using reference_ui::SourceStep;
    namespace view = reference_ui::runtime_view;
    const auto japanese = [] (const juce::String& english) { return i18n::translate (english, i18n::Language::japanese); };
    // 再生の準備で断られた理由（ReferenceAudioPages.cpp）と、音源の記録を受け取れなかった理由。
    require (view::rejectedStep ("channel_layout_unsupported") == SourceStep::channelsDiffer,
             "a source whose channels differ from the track is named as such");
    require (view::rejectedStep ("reference_source_receipt_rejected") == SourceStep::sourceRecordMismatch,
             "a source record that does not match its file is named as such");
    require (view::rejectedStep ("reference_source_contract_rejected") == SourceStep::sourceRecordUnsupported,
             "a source record Hypha cannot read is named as such");
    require (view::rejectedStep ("an_unknown_reason") == SourceStep::attention, "an unknown reason stays unknown");
    for (const auto* code : { "reference_preset_rejected", "reference_work_invalid", "reference_global_preset_catalog_rejected" })
        require (view::rejectedStep (code) == SourceStep::publicationInvalid
                     && view::rejectedStatus (code) == reference_ui::stageOf (SourceStep::publicationInvalid).status,
                 "a publication Kirin OS can publish again says so in the status and in the guide");
    for (const auto step : { SourceStep::channelsDiffer, SourceStep::sourceRecordMismatch,
                             SourceStep::sourceRecordUnsupported, SourceStep::publicationInvalid, SourceStep::attention })
    {
        const juce::String line (reference_ui::stageOf (step).status);
        require (! line.contains ("PREPARE AGAIN") || step == SourceStep::sourceRecordMismatch,
                 "only a record Kirin OS can write again asks to prepare it again: " + line);
        require (japanese (line) != line && japanese (reference_ui::stepText (step)) != reference_ui::stepText (step),
                 "the reason reads in Japanese: " + line);
    }
    require (! juce::String (reference_ui::stageOf (SourceStep::attention).status).contains ("PREPARE AGAIN"),
             "an unknown reason does not guess that preparing again helps");
    // コードは案内（C を押した・指したときの説明）に残る。英語でも日本語でもコードはそのまま。
    for (const auto* code : { "channel_layout_unsupported", "an_unknown_reason" })
    {
        auto state = named ("ready");
        state.separateComparisons = state.libraryReceived = state.osOnline = state.aAvailable = true;
        state.osAccess = os_access::State::ready;
        state.comparisonSlot = 2;
        state.checkReady = state.bSelected = false;  // 断られた C は鳴らせない
        state.checkStep = view::rejectedStep (code);
        state.checkRejectionCode = code;
        const auto guide = reference_ui::guide (state);
        require (guide.detail.contains (code), juce::String ("the guide keeps the reason code: ") + guide.detail);
        require (japanese (guide.detail).contains (code) && japanese (guide.detail) != guide.detail,
                 juce::String ("the Japanese guide keeps the code as it is: ") + japanese (guide.detail));
        const auto notice = reference_ui::notice::roleUnavailable (2, state.checkStep, reference_audition::MatchFailure::none, code);
        require (notice.startsWith ("C: ") && (state.checkStep != SourceStep::attention || notice.contains (code)),
                 "pressing C says the reason it cannot play, an unknown one with its code: " + notice);
        require (japanese (notice) != notice && (state.checkStep != SourceStep::attention || japanese (notice).contains (code)),
                 "the reason reads in Japanese and keeps the code: " + japanese (notice));
        // 状態の行は、C のページでも案内を出さない B のページでも、文の無い理由のコードを見せる。
        for (const int slot : { 2, 3 })
        {
            auto page = state;
            page.comparisonSlot = slot;
            page.referenceStep = view::rejectedStep (code);
            page.referenceRejectionCode = code;
            const auto line = reference_ui::referenceStatusLine (page, false).text;
            const bool unknown = view::rejectedStep (code) == SourceStep::attention;
            require (! unknown || (line.contains (code) && ! line.contains ("SEE THE REASON CODE")),
                     "the status line of the C and B pages shows an unknown reason's code: " + line);
            require (! unknown || (japanese (line).contains (code) && ! japanese (line).contains ("REASON")),
                     "the Japanese status line keeps the code and reads the rest in Japanese: " + japanese (line));
        }
    }
    // 全体として断られた（Reference が使えない）ときの状態の行も、知らない理由を推測せず、コードを見せる（コードは
    // Kirin OS 側と照らす識別子なので日本語の画面でもそのまま）。
    require (! view::rejectedStatus ("channel_layout_unsupported").contains ("PREPARE AGAIN")
                 && ! view::rejectedStatus ("an_unknown_reason").contains ("PREPARE AGAIN")
                 && view::rejectedStatus ("an_unknown_reason").contains ("REASON CODE an_unknown_reason"),
             "the whole-reference status names the reason and shows the code");
}

// 2026-10-18：音源が見つからない B の曲。選択欄は「PREPARING」と言わず「NOT FOUND」と言い（日本語の画面では状態の語だけ
// 訳し、曲名は訳さない）、状態の行は一覧と同じく見つからないことと Kirin OS での直し方を言う。
inline void verifyReferenceSongChoice()
{
    using namespace reference_guide_contract;
    using namespace reference_ui;
    const auto japanese = [] (const juce::String& english) { return i18n::translate (english, i18n::Language::japanese); };
    require (songChoiceLabel ("reference 2", true, true) == "reference 2   NOT FOUND", "a missing source is not called preparing");
    require (songChoiceLabel ("reference 2", true, false) == "reference 2   PREPARING" && songChoiceLabel ("Tone", false, false) == "Tone",
             "a song still being prepared says so; a ready song is its name");
    require (songChoiceName ("reference 2   NOT FOUND") == "reference 2" && songChoiceName ("reference 2   PREPARING") == "reference 2"
                 && songChoiceName ("Tone") == "Tone", "the list shows the name alone");
    require (japanese ("reference 2   NOT FOUND") == juce::String::fromUTF8 (u8"reference 2   見つからない")
                 && japanese ("Tone   PREPARING") == juce::String::fromUTF8 (u8"Tone   準備中"),
             "the state word reads in Japanese and the song title stays as written");
    // A name the user wrote that ends in an upper-case word Hypha also uses is not a state word.
    require (japanese ("Demo mix   WAITING") == "Demo mix   WAITING", "only Hypha's state words after a name are translated");
    require (japanese ("Tone") == "Tone" && japanese ("Set 2   1 / 3") == "Set 2   1 / 3", "nothing else changes");
    reference_audition::RuntimeSongPreparation preparation;
    preparation.state = "ready";
    markSourceMissing (preparation);
    require (preparationFailed (preparation) && preparationWord (preparation) == "NOT FOUND"
                 && preparationLine (preparation).contains ("RETRY IN KIRIN OS"),
             "the status line says the file is missing and how to retry it in Kirin OS");
}

// 2026-10-18：Kirin OS で Hypha に出さなくした CHECK セットは、選び直すまで選んだまま（C の音を勝手に替えない）だが、
// 出していないことを添えて言う。B セットは今までどおり 1 位へ戻る。
inline void verifyUnsentCheckSet()
{
    using namespace reference_guide_contract;
    reference_ui::State sets;
    const juce::String recording ("00000000-0000-4000-8000-000000000101");  // Kirin OS の組み込みの CHECK セット
    sets.presets = { { "basic", "Basic 5" }, { recording, {} } };
    sets.presets[1].label = juce::String::fromUTF8 (u8"Recording · tone and performance");
    sets.presetId = recording;
    reference_ui::runtime_view::rankCheckSets (sets, { { "basic", "r1", 1 } });
    require (sets.presets.size() == 2 && sets.presets[0].label == "Basic 5   1 / 1" && sets.presets[1].id == recording
                 && sets.presets[1].label.endsWith ("   NOT SENT TO HYPHA"),
             "the chosen CHECK set stays chosen and says Kirin OS no longer sends it");
    reference_ui::prepareDisplayNames (sets, i18n::Language::japanese);
    require (i18n::translate (sets.presets[1].label, i18n::Language::japanese)
                 == juce::String::fromUTF8 (u8"録音｜音色・演奏   Hyphaに出していない"),
             "in Japanese the set reads as Kirin OS names it and says it is not sent");
}

// 2026-10-18：B の一覧で長い曲名を切るときは名前を切り、版（「 · v5」）は残す（同じ曲の版を見分けられる）。
inline void verifyLongSongTitleKeepsVersion()
{
    using namespace reference_guide_contract;
    const juce::Font font (14.0f);
    const auto five = juce::String::fromUTF8 (u8"Long Reference Title 日本語 さらに長い名前 · v5");
    const auto two = juce::String::fromUTF8 (u8"Long Reference Title 日本語 さらに長い名前 · v2");
    for (const auto width : { 90.0f, 140.0f, 200.0f })
    {
        const auto shownFive = text_style::ellipsizedKeepingVersion (five, font, width);
        const auto shownTwo = text_style::ellipsizedKeepingVersion (two, font, width);
        require (shownFive.endsWith (juce::String::fromUTF8 (u8" · v5")) && shownTwo.endsWith (juce::String::fromUTF8 (u8" · v2"))
                     && shownFive != shownTwo && font.getStringWidthFloat (shownFive) <= width,
                 "a long title keeps its version and fits: " + shownFive);
    }
    require (text_style::ellipsizedKeepingVersion ("Song 1", font, 200.0f) == "Song 1", "a title that fits stays whole");
    const auto plain = juce::String::repeatedString ("Long title ", 8);
    require (text_style::ellipsizedKeepingVersion (plain, font, 120.0f) == text_style::ellipsizedText (plain, font, 120.0f),
             "a title without a version is cut as before");
}
}
