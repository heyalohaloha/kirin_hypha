#pragma once

// 2026-10-06：段階の表（INV-S48）。
//  - 全部の段階 × 全部の役（B・C・V）で、状態の行は同じ種類になり、準備中・できないの文は「理由 / 直し方」を 1 つずつ
//    持つ（日英とも）。待ちの上限は自動で進む段階にだけ効き、利用者の操作を待つ段階はどれだけ待っても上限にならない。
//  - 300% の B・C・V の並びは 300% だけ（200% は今までの選択欄と見出し）。
//  - 切れた状態の文は、どの大きさでも指せば全文を読める（300% は足元の説明の行、ほかは吹き出し）。
#include "ReferenceADirectionTest.h"
#include "ReferenceGuideContractTest.h"
#include "../src/HyphaReferencePreparationWatch.h"
#include "../src/HyphaReferenceStages.h"
#include "../src/reference_audition/ReferenceWholeSongSpectrum.h"

namespace hypha::tests
{
inline reference_ui::State stageState (int slot, reference_ui::SourceStep step)
{
    using namespace reference_guide_contract;
    auto state = named ("ready");
    state.separateComparisons = state.libraryReceived = state.osOnline = state.aAvailable = state.auditionBuffered = true;
    state.osAccess = os_access::State::ready;
    state.comparisonSlot = slot;
    state.songSets = { { "set", "Set   1 / 1" } };
    state.versionStep = state.checkStep = state.referenceStep = step;
    return state;
}

inline void verifyReferenceStageTable()
{
    using namespace reference_guide_contract;
    using reference_ui::SourceStep;
    using reference_ui::StatusKind;
    using reference_ui::WaitBudget;
    for (int index = 0; index < reference_ui::stageCount; ++index)
    {
        const auto step = static_cast<SourceStep> (index);
        const auto& stage = reference_ui::stageOf (step);
        const auto name = juce::String (stage.status);
        require ((step == SourceStep::ready) == (stage.kind == StatusKind::ready) && reference_ui::kindOf (step) == stage.kind,
                 "only the ready stage reads as audible: " + name);
        require ((stage.budget == WaitBudget::none) == juce::String (stage.overdue).isEmpty()
                     && (stage.budget == WaitBudget::none || stage.kind == StatusKind::waiting),
                 "only a stage that settles by itself has a wait limit, and says what to do past it: " + name);
        for (const int slot : { 1, 2, 3 })
        {
            const auto line = reference_ui::referenceStatusLine (stageState (slot, step));
            const auto letter = juce::String (reference_ui::roleLetter (slot));
            require (line.kind == stage.kind, "every role reads the same kind for a stage: " + letter + " " + line.text);
            require (! line.text.contains ("%1"), "the role is put in: " + line.text);
            for (const auto language : { i18n::Language::english, i18n::Language::japanese })
            {
                juce::StringArray parts;
                parts.addTokens (i18n::translate (line.text, language), "/", {});
                parts.trim();
                parts.removeEmptyStrings();
                require (parts.size() == 2,
                         "a stage says one reason and one way forward: " + letter + " " + i18n::translate (line.text, language));
            }
            // 上限：自動で進む段階だけ（Kirin OS の応答 5 秒・準備 10 秒・位置合わせは再生 30 秒）。利用者の操作を待つ段階は
            // 10 分待っても上限にならない。
            reference_ui::PreparationWatch watch;
            juce::String overdue;
            for (double now = 0.0; now <= 600.0 && overdue.isEmpty(); now += 0.5)
                overdue = watch.observe (slot, step, false, true, true, now);
            require (overdue == juce::String (stage.overdue), "the wait limit belongs to the stage: " + letter + " " + name);
        }
    }

    // 300% の B・C・V の並びは 300% だけ。200% は選択欄の段と見出し（2026-10-06：200% の B に 300% の欄が詰まり、
    // C・V の見出しが消えていた）。
    for (const auto& [width, height] : { std::pair { 600, 400 }, std::pair { 900, 600 } })
        for (const int slot : { 1, 2, 3 })
        {
            auto state = stageState (slot, SourceStep::ready);
            state.songs = { { "e1/e1/song-1", "Song 1" } };
            state.songId = "e1/e1/song-1";
            state.versions = { { "v1", "Mix v7" } };
            state.versionId = "v1";
            state.checks = { { "chk-low/cand-1", "Low End  /  Song 1" } };
            state.checkId = "chk-low/cand-1";
            reference_ui::Component panel;
            panel.setVisible (true);
            panel.setPresentationContext (presentation::forEditor (width, height));
            panel.setSize (width - 12, height * 47 / 60);
            panel.setState (state);
            const bool large = width == 900;
            const auto texts = a_direction::drawnTexts (panel);
            auto* tabs = panel.findChildWithID ("reference-check-tabs");
            auto* list = panel.findChildWithID ("reference-song-list");
            require (tabs != nullptr && list != nullptr, "the page parts exist");
            require (tabs->isVisible() == (large && slot != 3) && list->isVisible() == (large && slot == 3)
                         && texts.contains ("REFERENCE") == ! large,
                     juce::String ("the 300% page layout is used at 300% only (") + juce::String (width) + ", "
                         + reference_ui::roleLetter (slot) + ")");
        }

    // 2026-10-06：C の Cue の値がまだ無いときの代わり（曲全体のスペクトル）は、同じ測定の区切りが 100 ms の曲だけ。
    // 長い曲（区切り 200 ms 以上）は図に出さず、どちらのときも状態の行が理由と直し方を言う。
    {
        using reference_audition::WholeSongSpectrum;
        reference_audition::RuntimeDetailedMeasurement measurement;
        measurement.audio = { 48'000, 2, 48'000 * 180 };
        measurement.spectrum = reference_audition::RuntimeMeasurementSpectrum { { 100.0, 1'000.0 }, { -40'000, -42'000 },
                                                                                 { -30'000, -32'000 }, { -20'000, -22'000 } };
        measurement.loudness = reference_audition::RuntimeMeasurementTimeline { 4'800, {} };
        require (reference_audition::wholeSongSpectrum (measurement) == WholeSongSpectrum::comparable
                     && reference_audition::wholeSongWindow (measurement) != nullptr
                     && reference_audition::wholeSongWindow (measurement)->medianDb[1] == -32.0f,
                 "a song measured every 100 ms stands in for its Cue with the same definition as A");
        measurement.loudness = reference_audition::RuntimeMeasurementTimeline { 9'600, {} };
        require (reference_audition::wholeSongSpectrum (measurement) == WholeSongSpectrum::differentDefinition
                     && reference_audition::wholeSongWindow (measurement) == nullptr,
                 "a longer song measured every 200 ms does not stand in for its Cue");
        measurement.loudness.reset();
        measurement.waveform = reference_audition::RuntimeMeasurementWaveform { 4'800, {}, {} };
        require (reference_audition::wholeSongSpectrum (measurement) == WholeSongSpectrum::comparable,
                 "without a loudness timeline the waveform bins give the same measurement's hop");
        measurement.spectrum.reset();
        require (reference_audition::wholeSongSpectrum (measurement) == WholeSongSpectrum::missing, "no spectrum, nothing stands in");
        for (const auto& [substitute, expected] : { std::pair { reference_ui::CueSubstitute::wholeSong, "C COMPARED OVER THE WHOLE SONG" },
                                                    std::pair { reference_ui::CueSubstitute::noSpectrum, "C SPECTRUM WAITS FOR ITS CUE VALUES" } })
            for (const auto step : { SourceStep::ready, SourceStep::playDaw })
            {
                auto state = stageState (2, step);
                state.cueSubstitute = substitute;
                const auto line = reference_ui::referenceStatusLine (state);
                require (line.text == juce::String (expected) + " / MEASURE ITS CUE IN KIRIN OS" && line.kind == reference_ui::kindOf (step),
                         "C says what stands in for its Cue, why and how to fix it: " + line.text);
                state.comparisonSlot = 1;
                require (! reference_ui::referenceStatusLine (state).text.contains ("CUE"), "only C's page says it");
            }
    }

    // 切れた状態の文は、指せば全文を読める。300% 未満は吹き出し、300% は足元の説明の行（statusLineHelp）。
    for (const auto& size : observatory::sizePresets)
    {
        auto state = stageState (3, SourceStep::ready);
        state.bSelected = true;
        state.audibleComparisonSlot = 3;
        state.tracking = reference_audition::TrackingState::stoppedCeiling;
        state.peakShortfallDb = 0.4;
        state.heldAttenuationDb = -3.2;
        state.appliedGainDb = 1.0;
        reference_ui::Component panel;
        panel.setVisible (true);
        panel.setPresentationContext (presentation::forEditor (size.width, size.height));
        panel.setSize (size.width - 12, size.height * 47 / 60);
        panel.setState (state);
        auto& strip = panel.footerStatusStrip();
        // どの大きさでも、行に入りきらない幅に置いて確かめる（全文はおよそ 600 px）。
        strip.setBounds (strip.getX(), strip.getY(), 160, strip.getHeight() > 0 ? strip.getHeight() : 18);
        strip.resized();
        const auto layout = panel.statusTextLayout (strip.getLocalBounds());
        require (layout.cut, "the long status is cut at " + juce::String (size.width));
        require (strip.getTooltip() == layout.line.text && panel.statusLineWhole() == layout.line.text,
                 "pointing at a cut status reads it whole at " + juce::String (size.width) + ": " + strip.getTooltip());
        require (panel.statusLineHelp (layout.primary.getCentre()) == layout.line.text,
                 "the 300% footer line reads the cut status whole at " + juce::String (size.width));
        strip.setBounds (strip.getX(), strip.getY(), 2'000, strip.getHeight());
        strip.resized();
        require (strip.getTooltip().isEmpty(), "a status shown whole has no tooltip at " + juce::String (size.width));
    }
}
}
