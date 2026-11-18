#pragma once
#include "ReferenceBalanceAlignmentFixture.h"
#include "../src/HyphaReferenceBalanceGain.h"
#include "../src/HyphaReferenceChartReading.h"
namespace hypha::tests::balance_alignment
{
inline void verify()
{
    using namespace reference_ui;
    auto waiting = fixture (false);
    const auto items = overlayItems (waiting);
    require (balanceReferenceLevel (waiting) == -7.5 && items[0].drawGainDb == 0, "selected line stays at its original height");
    for (size_t i = 0; i < 4; ++i)
    {
        require (std::abs (items[i].drawGainDb - (-7.5 - waiting.songFacts[i].lufsI)) < 1e-9, "each song aligns to selected loudness");
        require (!std::isfinite (items[i].gainDb) && !std::isfinite (songPreviewGainDb (waiting, waiting.songFacts[i], i == 0)),
                 "drawing alignment never invents MATCH/playback gain while A is absent");
        const auto reading = drawnCurve ("", juce::Colours::white, *items[i].centers, *items[i].medians,
                                         items[i].drawGainDb, 20.0, 20000.0);
        const auto raw = drawnCurve ("", juce::Colours::white, *items[i].centers, *items[i].medians, 0.0, 20.0, 20000.0);
        require (!reading.level.empty() && std::abs (reading.level.front() - raw.level.front() - items[i].drawGainDb) < 1e-4,
                 "hover uses the same shifted curve");
    }
    // Exclude the target from this identical-shape four-song distribution.
    auto songs = items; songs.pop_back();
    const auto songShape = overlayShape (songs, true);
    for (size_t b = 0; b < songShape.low.size(); ++b)
        require (std::abs (songShape.high[b] - songShape.low[b]) < 1e-4, "loudness-only differences contribute zero band width");
    require (std::string (balanceLevelText (waiting, false)) == "MATCHED TO EACH OTHER", "waiting aligned header");
    require (std::abs (items.back().drawGainDb - (-7.5 - waiting.targets.collection.items[0].integrated)) < 1e-9
        && !std::isfinite (items.back().gainDb), "target draw alignment leaves target MATCH missing");
    auto fallback = fixture (false); fallback.songId.clear();
    require (std::abs (balanceReferenceLevel (fallback) + 16.7) < 1e-9, "even-count fallback takes two middle corrected levels");
    fallback = fixture (false); fallback.songFacts[0].lufsI = std::numeric_limits<double>::quiet_NaN();
    require (std::abs (balanceReferenceLevel (fallback) + 17.8) < 1e-9, "selected missing loudness uses other valid songs");
    fallback = fixture (false); fallback.songFacts[0].loudnessOffsetLu = 3.0;
    require (balanceReferenceLevel (fallback) == -4.5 && balanceSongGainDb (fallback, fallback.songFacts[0], true) == 0,
             "reference anchor includes the existing channel loudness correction");
    auto ready = fixture (true); const auto shifted = overlayItems (ready);
    for (size_t i = 0; i < items.size(); ++i)
        require (std::abs (shifted[i].drawGainDb - items[i].drawGainDb - (-18.0 + 7.5)) < 1e-9,
                 "ordinary Cue matching translates all songs/targets equally when A arrives");
    auto shiftedSongs = shifted; shiftedSongs.pop_back();
    const auto readyShape = overlayShape (shiftedSongs, true);
    for (size_t b = 0; b < songShape.low.size(); ++b)
        require (std::abs ((readyShape.high[b] - readyShape.low[b]) - (songShape.high[b] - songShape.low[b])) < 1e-4,
                 "A arrival preserves the aligned distribution shape");
    for (size_t i = 0; i < 4; ++i)
        require (shifted[i].gainDb == songPreviewGainDb (ready, ready.songFacts[i], i == 0), "A-ready MATCH calculation is unchanged");
    auto offer = ready; offer.action.kind = ActionKind::lowerAAndPlay; offer.action.offer.slot = 3; offer.action.offer.db = -4.0;
    require (songPreviewGainDb (offer, offer.songFacts[0], true) == 4.0
        && overlayItems (offer)[0].gainDb == 4.0 && overlayItems (offer)[0].drawGainDb == 4.0,
        "lower-A approval keeps its fixed playback and drawing gain");
    auto following = ready; following.followA = true; following.wholeSongLoudness = -11.0;
    const auto followingItems = overlayItems (following);
    require (followingItems[0].drawGainDb == -7.0
        && followingItems[0].drawGainDb == songPreviewGainDb (following, following.songFacts[0], true)
        && !std::isfinite (followingItems[1].drawGainDb), "A-ready FOLLOW keeps whole-song gain and other-song missing state");
    auto playing = ready; playing.bSelected = true; playing.audibleComparisonSlot = 3; playing.appliedGainDb = -2.25;
    require (overlayItems (playing)[0].drawGainDb == -2.25 && overlayItems (playing)[0].gainDb == -2.25
        && overlayItems (playing)[1].drawGainDb == shifted[1].drawGainDb,
        "A-ready B playback preserves applied gain only for the selected song");
    require (std::string (balanceLevelText (ready, false)) == "20 Hz - 20 kHz", "A-matched header remains unchanged");
    waiting.followA = true;
    require (overlayItems (waiting)[1].drawGainDb == items[1].drawGainDb, "FOLLOW A waiting uses same reference level");
    waiting.followA = false; waiting.songFacts[1].lufsI = std::numeric_limits<double>::quiet_NaN();
    const auto missing = overlayItems (waiting);
    require (!std::isfinite (missing[1].drawGainDb) && missing[0].drawGainDb == 0, "only missing-loudness song remains unshifted");
    require (overlayShape (missing, true).unmatched && std::string (balanceLevelText (waiting, true)) == "LEVEL NOT MATCHED", "missing item keeps warning");
    waiting.songId.clear(); require (std::abs (balanceReferenceLevel (waiting) + 17.8) < 1e-9, "fallback uses valid-song median");
    int count = 0;
    for (const auto language : {i18n::Language::english, i18n::Language::japanese})
    {
        const i18n::ScopedLanguage scoped (language);
        for (const int scale : {3, 6, 9}) for (int status = 0; status < 3; ++status) for (int mode = 0; mode < 4; ++mode)
        {
            auto state = fixture (status == 0); state.overlay.all = mode != 0;
            using Drawing = reference_audition::BalanceOverlay::Drawing;
            state.overlay.drawing = mode < 2 ? Drawing::lines : mode == 2 ? Drawing::shape : Drawing::allShape;
            if (status == 2) state.songFacts[1].lufsI = std::numeric_limits<double>::quiet_NaN();
            reference_ui::Component panel; const auto context = presentation::forEditor (300 * scale, 200 * scale);
            reference_text_repairs::sizePanel (panel, 300 * scale, 200 * scale); panel.setState (state);
            reference_text_repairs::requireWhole (a_direction::drawnTexts (panel), balanceLevelText (state, status == 2));
            require (labelFont (context, typography::TextRole::legend, typography::Composition::visualization).getHeight() >= 12.0f,
                     "status typography is at least 12 pixels");
            ++count;
        }
    }
    require (count == 72, "all languages/sizes/statuses/drawing modes actually ran");
    std::cout << "BALANCE_ALIGNMENT_CASES=" << count << std::endl;
}
}
