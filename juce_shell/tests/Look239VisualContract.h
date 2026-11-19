#pragma once
#include "VisualReadabilityContract.h"
#include "ListenTextLayoutTest.h"
#include "FooterNoticeContractChecks.h"
#include "../src/HyphaComparisonNotices.h"
#include "../src/HyphaFooterNoticeFont.h"

namespace hypha::tests::look239
{
inline void save (const juce::Image& image, const juce::String& name)
{
    const auto root = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_LOOK239_DIR", {});
    if (root.isEmpty()) return; // Evidence paths are opt-in; ordinary CTest still checks every condition.
    const juce::File directory (root);
    visual_readability::require (directory.createDirectory().wasOk(), "create look239 directory");
    auto stream = directory.getChildFile (name + ".png").createOutputStream();
    visual_readability::require (stream && stream->setPosition (0) && stream->truncate().wasOk()
        && juce::PNGImageFormat().writeImageToStream (image, *stream), "write look239 pixels");
}
inline void verify()
{
    int images = 0, failures = 0;
    // Only the explicitly requested pre-change capture skips new wording. CI checks it by default.
    const bool candidate = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_LOOK239_BASELINE", {}) != "1";
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (const int width : { 300, 450, 900 })
        for (const float scale : { 1.0f, 2.0f })
        {
            const auto suffix = juce::String (language == i18n::Language::english ? "en-" : "ja-")
                + juce::String (width) + "-" + juce::String (int (scale * 100));
            const auto context = presentation::forEditor (width, width * 2 / 3);
            SpectrumComponent frequency;
            frequency.setPresentationContext (context); frequency.setAbsoluteObservation (true);
            frequency.setSignalActive (true);
            observatory::View freqShell (observatory::Role::post); freqShell.setSize (width, width * 2 / 3);
            freqShell.setObservatoryFrame (visual_readability::warmingFrame(), true);
            freqShell.setDomain (observatory::Domain::frequency);
            const auto freqBounds = freqShell.analysisBodyBounds();
            frequency.setSize (freqBounds.getWidth(), freqBounds.getHeight());
            auto spectrum = spectrumPerformanceFixture();
            for (size_t band = 0; band < KIRIN_SPECTRUM_BAND_COUNT; ++band)
                spectrum.post_dbfs[band] = band % 9 == 0 ? -3.0f : -80.0f;
            frequency.setSnapshot (spectrum); frequency.presentationTick();
            visual_readability::require (frequency.displayedPostForTest (144) > -10, "F12 real peak accepted");
            {
                text_style::ShownTextLog frequencyText;
                const auto body = frequency.createComponentSnapshot (frequency.getLocalBounds(), true, scale);
                const bool hasNotes = frequencyText.texts().joinIntoString (" ").contains ("256 BANDS");
                std::cout << "F12 " << suffix << " notesVisible=" << hasNotes << " axisFont="
                          << typography::resolve (context, typography::TextRole::axis, typography::Composition::visualization).fontHeight << '\n';
                auto full = freqShell.createComponentSnapshot (freqShell.getLocalBounds(), true, scale);
                juce::Graphics g (full);
                g.drawImageAt (body, juce::roundToInt (freqBounds.getX() * scale), juce::roundToInt (freqBounds.getY() * scale));
                save (full, "F12-" + suffix); ++images;
            }

            observatory::View level (observatory::Role::post);
            level.setSize (width, width * 2 / 3);
            auto frame = visual_readability::warmingFrame(); frame.lra_elapsed_seconds = 20;
            level.setObservatoryFrame (frame, true);
            std::vector<KirinMeterHistoryEntry> history (40);
            for (size_t i = 0; i < history.size(); ++i)
            {
                auto& entry = history[i]; entry.generation = 7; entry.run_id = 1;
                entry.last_observed_frames = (i + 1) * 4'800;
                entry.lufs_m = { -21.5, -21.5, -21.5 }; entry.true_peak = { -20, -20, -20 };
            }
            level.setHistory (history);
            {
                save (level.createComponentSnapshot (level.getLocalBounds(), true, scale), "F13-" + suffix); ++images;
                bool lraVisible = false;
                for (int y = 0; y < level.getHeight() && !lraVisible; y += 8)
                    for (int x = 0; x < level.getWidth(); x += 8)
                        if (level.metricHelpAt ({ x, y }).contains ("(LRA)")) { lraVisible = true; break; }
                if (width == 900) visual_readability::require (lraVisible, "F13 full-size LRA must be visible");
                // LRA uses per-glyph tabular drawing, not ShownTextLog. Its actual
                // bounds/brightness are checked by lra_state_bounds below, and
                // candidate wording is separately tied to its source and catalogue.
                std::cout << "F13 " << suffix << " lraVisible=" << lraVisible << " rasterScale=" << scale << '\n';
            }

            const juce::String notice (comparison_ui::beforeRealtimeBounce);
            observatory::View view (observatory::Role::post); view.setSize (width, width * 2 / 3);
            view.setObservatoryFrame (frame, true);
            observatory::LiveCompareFooter footer; footer.entryEnabled = true;
            footer.active = footer.preSelected = true; footer.recoveryHelp = notice.toRawUTF8();
            view.setLiveCompareFooter (footer); view.setFeedback (notice);
            visual_readability::require (view.getLocalBounds().contains (view.statusStripBounds()), "F14 strip fits product view");
            for (const auto* id : { "observatory-live-pre", "observatory-live-post", "observatory-live-end", "observatory-menu" })
                if (const auto* action = view.findChildWithID (id); action && action->isVisible())
                    visual_readability::require (!action->getBounds().intersects (view.statusStripBounds()), "F14 clears visible actions");
            if (!view.statusStripFolded())
                visual_readability::require (!view.analysisBodyBounds().intersects (view.statusStripBounds()), "F14 clears nonfolded body");
            FeedbackStrip strip; strip.setBounds (view.statusStripBounds());
            strip.setPresentationContext (view.presentationContext()); strip.setFeedback (notice);
            auto& painted = view.statusStripFolded() ? static_cast<juce::Component&> (strip) : view.feedbackDetailsAnchor();
            text_style::ShownTextLog actual;
            const auto band = painted.createComponentSnapshot (painted.getLocalBounds(), true, scale);
            const auto room = painted.getLocalBounds().reduced (view.statusStripFolded() ? 6 : 3, view.statusStripFolded() ? 0 : 1);
            const auto font = footer_notice_font::fontForText (view.presentationContext(), notice, room.getWidth());
            const auto shown = text_style::shownText (notice);
            const bool full = footer_notice::completePaint (actual, shown, painted.getLocalBounds())
                && font.getHeight() >= 12.0f
                && text_style::shownWrappedHeight (notice, font, room.getWidth()) <= room.getHeight() + 0.5f;
            std::cout << "F14 " << suffix << " full=" << full << " font=" << font.getHeight()
                      << " room=" << room.toString() << " text=" << shown << '\n';
            failures += !full;
            save (band, "F14-band-" + suffix);
            auto composite = view.createComponentSnapshot (view.getLocalBounds(), true, scale);
            if (view.statusStripFolded())
            {
                juce::Graphics g (composite);
                g.drawImageAt (band, juce::roundToInt (view.statusStripBounds().getX() * scale),
                                    juce::roundToInt (view.statusStripBounds().getY() * scale));
            }
            save (composite, "F14-" + suffix); ++images;
            if (candidate)
            {
                const auto expected = language == i18n::Language::japanese
                    ? juce::String::fromUTF8 (u8"書き出す前は END を押し、音量が戻るのを待ってください")
                    : juce::String ("Before a realtime bounce, press END and wait for the level to return");
                visual_readability::require (shown == expected, "F14 exact requested full sentence");
                visual_readability::require (i18n::tr ("Preparing 20 s") == (language == i18n::Language::japanese
                    ? juce::String::fromUTF8 (u8"準備中 20 秒") : juce::String ("Preparing 20 s")), "F13 preparing units");
            }
        }
    }
    std::cout << "LOOK239 images=" << images << " noticeFailures=" << failures << '\n';
    visual_readability::require (images == 36 && failures == 0, "look239 all image conditions fit");
    visual_readability::verify(); // Preserved ridge, history and all-five-preset LRA oracles.
}
}
