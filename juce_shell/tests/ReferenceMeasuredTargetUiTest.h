#pragma once
#include "ReferenceAWindowTest.h"
#include "../src/HyphaReferenceTargetView.h"
#include "../src/HyphaReferenceCueSummary.h"
#include "../src/HyphaReferenceAComparison.h"
#include "../src/reference_audition/ReferenceTargetService.h"
#include "../src/reference_audition/ReferenceTargetPeakWindow.h"

namespace hypha::tests::measured_target_ui
{
inline void verifyExistingComparison()
{
    using reference_guide_contract::require;
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (const auto size : observatory::sizePresets)
        {
            reference_ui::Component panel;
            const auto viewport = observatory::displayViewport (size.width, size.height);
            observatory::View shell (observatory::Role::post);
            shell.setSize (viewport.width, viewport.height); shell.setDomain (observatory::Domain::reference);
            shell.setExternalAnalysisBodyActive (true);
            panel.setVisible (true); panel.setPresentationContext (presentation::forEditor (viewport.width, viewport.height));
            panel.setSize (shell.analysisBodyBounds().getWidth(), shell.analysisBodyBounds().getHeight());
            auto state = reference_a_window::cPage();
            state.blindLargeScreen = size.density == observatory::Density::inspection;
            state.presets = { { "preset", "Check set" } }; state.presetId = "preset";
            state.cues = { { "cue", "Cue" } }; state.cueId = "cue";
            reference_ui::applyMeasuredTarget (state, {}, true);
            panel.setState (state);
            auto* bar = panel.findChildWithID ("reference-targets");
            const auto normalBarHeight = bar->getHeight();
            for (const bool measuring : { true, false })
            {
                state.targets.measuring = measuring; state.targets.progress = 0.42;
                state.targets.issue = measuring ? "" : "FILE UNREADABLE";
                panel.setState (state);
                const auto log = a_direction::drawnTexts (panel);
                require (! panel.state().targetSelected, "existing C remains selected during import and failure");
                if (panel.shortPanel())
                {
                    require (bar->getHeight() == normalBarHeight, "import status never consumes compact comparison height");
                    reference_text_repairs::requireWhole (log, "LUFS-I");
                    reference_text_repairs::requireWhole (log, "MAX TP");
                    const auto line = panel.statusTextLayout (panel.footerStatusStrip().getLocalBounds());
                    require (! line.cut && line.line.text == (measuring ? "MEASURING 42%" : "FILE UNREADABLE"),
                             "the existing status row fits progress and failure whole in both languages");
                }
                const juce::String notice = measuring ? "MEASURING 42%" : "FILE UNREADABLE";
                if (panel.shortPanel()) reference_text_repairs::requireWhole (log, notice);
                else
                {
                    bool fits = false;
                    for (auto* child : bar->getChildren())
                        if (const auto* label = dynamic_cast<juce::Label*> (child); label && label->isVisible() && label->getText() == notice)
                            fits = text_style::shownWidth (label->getFont(), notice) <= label->getWidth() - 10;
                    require (fits, "the full target status label fits in both languages");
                }
                const auto out = juce::SystemStats::getEnvironmentVariable ("KIRIN_TARGET_PREVIEW", {});
                if (out.isNotEmpty() && size.width == 300)
                {
                    juce::Image image (juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
                    juce::Graphics g (image); panel.paintEntireComponent (g, true);
                    auto stream = juce::File (out).getChildFile ("existing-c-" + juce::String (measuring ? "progress" : "failure")
                        + (language == i18n::Language::japanese ? "-ja.png" : "-en.png")).createOutputStream();
                    require (stream && juce::PNGImageFormat().writeImageToStream (image, *stream), "existing C status preview");
                }
            }
            for (const int slot : { 1, 3 })
            {
                auto other = slot == 3 ? reference_a_window::bPage() : reference_a_window::cPage();
                other.comparisonSlot = slot; other.blindLargeScreen = state.blindLargeScreen;
                panel.setState (other);
                const auto original = panel.panelArea();
                reference_ui::applyMeasuredTarget (other, state.targets, true); panel.setState (other);
                require (! bar->isVisible() && panel.panelArea() == original,
                         "targets and notices preserve B and V's original comparison area");
            }
            for (const int slot : { 1, 2 })
                for (const bool online : { false, true })
                for (const bool measuring : { true, false })
                {
                    reference_ui::State fresh;
                    fresh.separateComparisons = true; fresh.comparisonSlot = slot; fresh.osOnline = online;
                    fresh.osAccess = os_access::State::ownedDisconnected;
                    reference_audition::TargetSnapshot importing;
                    importing.measuring = measuring; importing.progress = 0.42;
                    importing.issue = measuring ? "" : "FILE UNREADABLE";
                    reference_ui::applyMeasuredTarget (fresh, importing, true);
                    require (reference_ui::guide (fresh).shown, "the licensed initial screen still uses the empty-state guide");
                    panel.setState (fresh);
                    const auto texts = a_direction::drawnTexts (panel);
                    const auto shown = reference_ui::guide (fresh);
                    reference_text_repairs::requireWhole (texts, shown.heading);
                    require (panel.guideFit().heading && panel.guideFit().detail,
                             "the shipping paint retains the complete Kirin OS heading and reason");
                    if (slot == 2)
                    {
                        // JUCE draws an English ComboBox placeholder outside ShownTextLog.
                        auto* choice = dynamic_cast<juce::ComboBox*> (bar->findChildWithID ("reference-target-choice"));
                        require (choice && bar->isVisible() && choice->isVisible()
                            && choice->getTextWhenNothingSelected() == "DROP AUDIO", "the drop hint is visible alongside the guide");
                        const auto hint = choice->createComponentSnapshot (choice->getLocalBounds());
                        choice->setTextWhenNothingSelected ({});
                        const auto blank = choice->createComponentSnapshot (choice->getLocalBounds());
                        choice->setTextWhenNothingSelected ("DROP AUDIO");
                        bool painted = false;
                        for (int y = 0; y < hint.getHeight(); ++y)
                            for (int x = 0; x < hint.getWidth(); ++x)
                                painted = painted || hint.getPixelAt (x, y) != blank.getPixelAt (x, y);
                        require (painted && text_style::shownWidth (choice->getLookAndFeel().getComboBoxFont (*choice), "DROP AUDIO")
                            <= choice->getWidth() - 24, "the actual drop-hint pixels are painted without clipping");
                    }
                    const auto preview = juce::SystemStats::getEnvironmentVariable ("KIRIN_TARGET_PREVIEW", {});
                    if (preview.isNotEmpty() && ! online && ! measuring && (size.width == 300 || size.width == 900))
                    {
                        const auto image = panel.createComponentSnapshot (panel.getLocalBounds());
                        auto stream = juce::File (preview).getChildFile ("initial-" + juce::String (slot == 2 ? "c-" : "v-")
                            + juce::String (size.width) + (language == i18n::Language::japanese ? "-ja.png" : "-en.png")).createOutputStream();
                        require (stream && juce::PNGImageFormat().writeImageToStream (image, *stream), "initial guide preview");
                    }
                    if (panel.shortPanel() || slot == 1)
                        reference_text_repairs::requireWhole (texts, measuring ? "MEASURING 42%" : "FILE UNREADABLE");
                }
            for (const int slot : { 1, 2, 3 })
                for (const int operation : { 0, 1, 2, 3, 4 })
                {
                    auto audition = reference_a_window::cPage();
                    audition.comparisonSlot = audition.audibleComparisonSlot = slot;
                    audition.blindLargeScreen = state.blindLargeScreen;
                    audition.bSelected = operation < 2 || operation == 4;
                    audition.tracking = operation == 0 ? reference_audition::TrackingState::fixed : reference_audition::TrackingState::following;
                    if (operation == 4) { audition.peakShortfallDb = 12.3; audition.heldAttenuationDb = -3.0; }
                    if (operation == 2) audition.heldAttenuationDb = -3.0;
                    if (operation == 3) { audition.pendingAudition.slot = slot; audition.pendingAudition.stage = reference_audition::PendingAuditionView::Stage::play; }
                    audition.targetsEnabled = true; audition.targets.issue = "FILE UNREADABLE";
                    panel.setState (audition);
                    const auto layout = panel.statusTextLayout (panel.footerStatusStrip().getLocalBounds());
                    const auto expected = reference_ui::referenceStatusLine (audition);
                    const bool inStatus = panel.shortPanel() || slot != 2;
                    require (layout.line.kind == expected.kind && layout.line.text.contains (expected.text)
                        && (! inStatus || layout.line.text.contains ("FILE UNREADABLE")),
                             "a failed drop retains the current audition, MATCH, attenuation and pending status");
                    const auto notice = text_style::shownText ("FILE UNREADABLE");
                    if (inStatus)
                    {
                        bool painted = false;
                        for (const auto& text : a_direction::drawnTexts (panel)) painted = painted || text.contains (notice);
                        require (painted, "shipping paint shows the entire failed-drop reason during long audition status");
                        require (layout.text.startsWith (notice) && text_style::shownWidth (labelFont (presentation::forEditor (viewport.width, viewport.height),
                            typography::TextRole::readout, typography::Composition::information), layout.text)
                                <= layout.textArea.getWidth(), "the failure survives segment fitting without clipping");
                    }
                    else
                    {
                        bool fits = false;
                        for (auto* child : bar->getChildren())
                            if (const auto* label = dynamic_cast<juce::Label*> (child); label && label->isVisible() && label->getText() == "FILE UNREADABLE")
                                fits = text_style::shownWidth (label->getFont(), notice) <= label->getWidth() - 10;
                        require (fits, "the full C target bar shows the failed-drop reason without clipping");
                    }
                    require (panel.state().bSelected == audition.bSelected && panel.state().tracking == audition.tracking
                        && panel.state().audibleComparisonSlot == slot && std::abs (panel.state().heldAttenuationDb - audition.heldAttenuationDb) < 1.0e-9,
                             "the notice preserves playback choice, tracking and approved attenuation");
                    const auto preview = juce::SystemStats::getEnvironmentVariable ("KIRIN_TARGET_PREVIEW", {});
                    if (preview.isNotEmpty() && slot == 3 && operation == 4)
                    {
                        const auto image = panel.createComponentSnapshot (panel.getLocalBounds());
                        auto stream = juce::File (preview).getChildFile ("audition-failure-" + juce::String (size.width)
                            + (language == i18n::Language::japanese ? "-ja.png" : "-en.png")).createOutputStream();
                        require (stream && juce::PNGImageFormat().writeImageToStream (image, *stream), "audition failure preview");
                    }
                    audition.targets.issue.clear(); panel.setState (audition);
                    require (panel.statusTextLayout (panel.footerStatusStrip().getLocalBounds()).line.text == expected.text,
                             "the original audition status returns when the temporary notice expires");
                }
        }
    }
}
// A drop that does not fit says how many were not added and how to make room, whole on C's bar and as
// the first fact of the status line where the bar has no room, in both languages.
inline void verifyFullNotice()
{
    using reference_guide_contract::require;
    const auto notice = reference_audition::targetsFullIssue (5);
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        const auto shown = text_style::shownText (notice);
        require (language == i18n::Language::english ? shown == notice
                     : ! shown.contains ("NOT ADDED") && ! shown.contains ("REMOVE") && shown.contains ("5") && shown.contains ("16"),
                 "the full-collection notice reads in the screen's language: " + shown);
        for (const auto size : observatory::sizePresets)
        {
            reference_ui::Component panel;
            const auto viewport = observatory::displayViewport (size.width, size.height);
            observatory::View shell (observatory::Role::post);
            shell.setSize (viewport.width, viewport.height); shell.setDomain (observatory::Domain::reference);
            shell.setExternalAnalysisBodyActive (true);
            panel.setVisible (true); panel.setPresentationContext (presentation::forEditor (viewport.width, viewport.height));
            panel.setSize (shell.analysisBodyBounds().getWidth(), shell.analysisBodyBounds().getHeight());
            auto state = reference_a_window::cPage();
            state.blindLargeScreen = size.density == observatory::Density::inspection;
            reference_audition::TargetSnapshot targets; targets.issue = notice;
            reference_ui::applyMeasuredTarget (state, targets, true);
            panel.setState (state);
            auto* bar = panel.findChildWithID ("reference-targets");
            bool whole = false;
            if (! panel.shortPanel())
                for (auto* child : bar->getChildren())
                    if (const auto* label = dynamic_cast<juce::Label*> (child); label && label->isVisible() && label->getText() == notice)
                        whole = text_style::shownWidth (label->getFont(), shown) <= label->getWidth() - 10;
            if (panel.shortPanel())
            {
                const auto line = panel.statusTextLayout (panel.footerStatusStrip().getLocalBounds());
                whole = line.line.text == notice && line.text.startsWith (text_style::shownText (notice.upToFirstOccurrenceOf (" / ", false, false)));
            }
            require (whole, "the full-collection notice is shown at " + juce::String (size.width));
        }
    }
}
inline void verifySelection (const reference_audition::MeasuredTarget& target)
{
    using reference_guide_contract::require;
    reference_audition::TargetService service;
    auto other = target; other.id = "target-456"; other.name = "Other";
    service.restore ({ { target, other }, target.id });
    reference_ui::Component panel;
    auto state = reference_a_window::cPage();
    const auto refresh = [&] {
        reference_ui::applyMeasuredTarget (state, service.snapshot(), true); panel.setState (state);
    };
    refresh();
    auto* bar = panel.findChildWithID ("reference-targets");
    auto* select = dynamic_cast<reference_ui::SelectionControl*> (bar->findChildWithID ("reference-target-choice"));
    require (select != nullptr, "targets use the shipping synchronous selection control");
    int choices = 0;
    panel.onTargetRequest = [&] (const auto& request) {
        ++choices; service.select (request.id, request.restoreEpoch); refresh();
    };
    service.report ("FILE UNREADABLE"); refresh();
    select->choose (3);
    require (choices == 1 && service.snapshot().issue.isEmpty() && service.snapshot().collection.selected == other.id,
             "a target choice commits synchronously before the next display refresh");
    auto reply = select->menuReply();
    service.remove (target.id); refresh();
    reply (3);
    require (choices == 2 && service.snapshot().collection.selected == other.id,
             "an offered row keeps its stable target ID when indices change");
    reply = select->menuReply();
    service.restore ({ { target, other }, target.id }); refresh();
    reply (2);
    require (choices == 2 && service.snapshot().collection.selected == target.id,
             "an open menu is invalidated by restore even when its IDs still exist");
    reply = select->menuReply();
    service.restore ({ { other, target }, other.id });
    reply (3);
    require (choices == 3 && service.snapshot().collection.selected == other.id,
             "the service rejects an old menu's epoch before the UI has refreshed");
}
inline void verifyChannels (const juce::File& sandbox)
{
    using reference_guide_contract::require;
    namespace ref = reference_audition;
    for (const int sourceChannels : { 1, 2 })
    {
        const auto file = sandbox.getChildFile ("channels-" + juce::String (sourceChannels) + ".wav");
        juce::AudioBuffer<float> audio (sourceChannels, 144000);
        for (int n = 0; n < audio.getNumSamples(); ++n)
            for (int c = 0; c < sourceChannels; ++c)
                audio.setSample (c, n, 0.1f * static_cast<float> (std::sin (6.283185307179586 * n / 48.0)));
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (file.createOutputStream().release(), 48000, static_cast<unsigned int> (sourceChannels), 24, {}, 0));
        require (writer && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()), "channel comparison fixture");
        writer.reset();
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        require (reader && reader->read (&audio, 0, audio.getNumSamples(), 0, true, sourceChannels == 2), "channel fixture quantized PCM");
        const auto measured = ref::measureTargetFile (file, [] { return false; });
        require (measured.target.valid() && measured.target.channels == sourceChannels, "file measurement retains its channel count");
        ref::TargetCollection collection { { measured.target }, measured.target.id };
        juce::XmlElement xml ("KirinHyphaState"); collection.write (xml);
        for (const bool restore : { false, true })
        {
            if (restore) collection = ref::TargetCollection::read (xml);
            require (collection.items.size() == 1 && collection.items[0].channels == sourceChannels
                && std::abs (collection.items[0].integrated - measured.target.integrated) < 1.0e-12, "state keeps channels and the original Integrated");
            for (const int trackChannels : { 1, 2 })
            {
                ref::KirinSpectrumMeter spectrum;
                ref::TargetPeakWindow levels;
                require (spectrum.configure (48000, trackChannels, 30), "A channel spectrum");
                std::vector<float> pcm (static_cast<size_t> (4800 * trackChannels));
                for (int b = 0; b < 30; ++b)
                {
                    for (int n = 0; n < 4800; ++n)
                        for (int c = 0; c < trackChannels; ++c)
                            pcm[static_cast<size_t> (n * trackChannels + c)] = audio.getSample (0, b * 4800 + n);
                    spectrum.push (pcm.data(), 4800); levels.push (pcm.data(), 4800, 48000, trackChannels);
                }
                auto timeline = std::make_shared<ref::VisualTimeline>();
                timeline->observing = true; timeline->aKirin = spectrum.window (30); timeline->aTickChannels = trackChannels;
                timeline->aTargetLoudness = levels.levels (30); timeline->aTargetTruePeak = levels.window (30);
                auto state = reference_a_window::cPage(); state.visualTimeline = timeline; state.aKirin = timeline->aKirin;
                ref::TargetSnapshot targets; targets.collection = collection;
                reference_ui::applyMeasuredTarget (state, targets, true);
                reference_ui::Component panel; panel.setState (state);
                const auto gain = reference_ui::comparisonGainDb (panel.state());
                require (std::abs (gain) < 0.0001
                    && std::abs (state.adjustedBIntegratedLoudness - state.aIntegratedLoudness) < 0.0001,
                    "equal mono/stereo audio keeps C's display gain and Integrated difference at zero before and after restore");
                for (size_t band = 0; band < 64; ++band)
                    require (std::abs (state.cueKirin->medianDb[band] + gain - state.aKirin->medianDb[band]) < 0.0001,
                             "the same mono/stereo audio draws the same target curve");
                for (size_t band = 0; band < 4; ++band)
                    require (reference_ui::compareBand ({ state.aKirin->balanceDb[band], state.cueKirin->balanceDb[band] + gain }).text == "SAME",
                             "all four Balance bands compare equal across channel layouts");
            }
        }
        auto* item = xml.getChildByName ("MeasuredTargets")->getChildByName ("Target");
        for (const auto* invalid : { "0", "3", "1.5", "broken" })
        {
            item->setAttribute ("channels", invalid);
            require (ref::TargetCollection::read (xml).items.empty(), "invalid channel metadata is rejected");
        }
        item->removeAttribute ("channels");
        const auto legacy = ref::TargetCollection::read (xml);
        require (legacy.items.size() == 1 && legacy.items[0].channels == 2, "older numeric states retain their stereo comparison convention");
    }
}
inline void verify (const juce::File& sandbox)
{
    using reference_guide_contract::require;
    namespace ref = reference_audition;
    verifyChannels (sandbox);
    auto base = reference_a_window::cPage();
    ref::MeasuredTarget target;
    target.id = "target-123"; target.name = "Window"; target.spectrum = *base.cueKirin;
    target.duration = 30.0; target.integrated = -18.0; target.maximumTruePeak = -2.0;
    require (target.valid(), "numeric target fixture");
    verifyExistingComparison();
    verifyFullNotice();
    verifySelection (target);
    auto timeline = std::make_shared<ref::VisualTimeline>();
    auto spectrum = std::make_shared<ref::KirinSpectrumWindow> (*base.aKirin); spectrum->frames = 30;
    auto ticks = std::make_shared<std::vector<KirinReferenceVisualBin>> (30);
    for (auto& tick : *ticks)
    { tick.momentary_lufs = target.integrated; tick.true_peak = std::pow (10.0, target.maximumTruePeak / 20.0); }
    timeline->observing = true; timeline->aKirin = spectrum; timeline->aTicks = ticks;
    timeline->aTickChannels = 2;
    timeline->aTargetLoudness = { target.integrated, target.maximumTruePeak, 30, 27 };
    timeline->aTargetTruePeak = target.maximumTruePeak;
    base.visualTimeline = timeline; base.aKirin = spectrum;
    ref::TargetSnapshot targets; targets.collection.items = { target }; targets.collection.selected = target.id;
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (const auto size : observatory::sizePresets)
            for (const int phase : { 0, 1, 2 })
            {
                reference_ui::Component panel;
                const auto viewport = observatory::displayViewport (size.width, size.height);
                observatory::View shell (observatory::Role::post);
                shell.setSize (viewport.width, viewport.height); shell.setDomain (observatory::Domain::reference);
                shell.setExternalAnalysisBodyActive (true);
                panel.setVisible (true);
                panel.setPresentationContext (presentation::forEditor (viewport.width, viewport.height));
                panel.setSize (shell.analysisBodyBounds().getWidth(), shell.analysisBodyBounds().getHeight());
                auto state = base;
                state.blindLargeScreen = size.density == observatory::Density::inspection;
                state.aIntegratedLoudness = -6.0; state.aMaximumTruePeakDbtp = 0.0; // Earlier loud audio.
                state.libraryReceived = state.osOnline = state.bSelected = false;
                state.osAccess = os_access::State::ownedDisconnected;
                state.aWindowLoudness = std::numeric_limits<double>::quiet_NaN();
                state.aWindowNeededBlocks = 300;
                targets.measuring = phase == 1; targets.progress = 0.42;
                targets.issue = phase == 2 ? "FILE UNREADABLE" : "";
                reference_ui::applyMeasuredTarget (state, targets, true);
                panel.setState (state);
                require (panel.state().aWindowBlocks == 30 && panel.state().aWindowNeededBlocks == 30
                    && std::abs (reference_ui::comparisonGainDb (panel.state())) < 1.0e-9,
                    "a numeric target compares the real three-second A window without an old C MATCH wait");
                const auto line = reference_ui::referenceStatusLine (panel.state(), false);
                require (line.kind == reference_ui::StatusKind::ready && line.text == "DATA ONLY",
                         "a numeric C never asks for Kirin OS registration or preparation");
                auto* bar = panel.findChildWithID ("reference-targets");
                auto* make = dynamic_cast<juce::Button*> (bar->findChildWithID ("reference-make-target"));
                auto* badge = panel.findChildWithID ("reference-target-data");
                auto* c = panel.findChildWithID ("reference-c");
                require (bar && make && bar->isVisible() && make->isEnabled() && badge->isVisible() && ! c->isVisible(),
                         "licensed numeric target works without a Kirin OS library; C has a non-playable label");
                require (panel.getLocalBounds().contains (bar->getBounds()) && ! bar->getBounds().intersects (badge->getBounds()),
                         "target controls stay inside the panel and away from source controls");
                const text_style::ShownTextLog log;
                juce::Image image (juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
                juce::Graphics graphics (image); panel.paintEntireComponent (graphics, true);
                reference_text_repairs::requireWhole (log.texts(), phase == 1 ? "CANCEL" : "MAKE TARGET");
                require (std::abs (panel.state().aIntegratedLoudness - target.integrated) < 1.0e-9
                    && std::abs (panel.state().aMaximumTruePeakDbtp - target.maximumTruePeak) < 1.0e-9,
                         "numeric C metrics replace the earlier session's Integrated and maximum peak");
                if (! observatory::isFullDensity (size.density))
                {
                    int same = 0;
                    for (const auto& text : log.texts())
                        if (text == text_style::shownText ("SAME") || text == text_style::shownText ("A SAME AS C")) ++same;
                    require (same == 2, "both painted compact metric differences compare the same A window as the target: "
                             + log.texts().joinIntoString (" | "));
                }
                if (observatory::isFullDensity (size.density))
                    reference_text_repairs::requireWhole (log.texts(), "TARGET RANGE");
                const auto out = juce::SystemStats::getEnvironmentVariable ("KIRIN_TARGET_PREVIEW", {});
                if (out.isNotEmpty() && phase == 0)
                {
                    auto stream = juce::File (out).getChildFile ("target-" + juce::String (size.width)
                        + (language == i18n::Language::japanese ? "-ja.png" : "-en.png")).createOutputStream();
                    require (stream && juce::PNGImageFormat().writeImageToStream (image, *stream), "target preview exported");
                }
                int viewed = 0, played = 0, opened = 0;
                panel.onOpenLarge = [&] (int slot) { require (slot == 2, "DATA expands the C page"); ++opened; };
                panel.onSelectVisualSlot = [&] (int slot) { viewed = slot; };
                panel.onSelectC = [&] { ++played; };
                for (const int page : { 1, 3 })
                {
                    auto other = base; other.comparisonSlot = page; other.blindLargeScreen = state.blindLargeScreen;
                    reference_ui::applyMeasuredTarget (other, targets, true); panel.setState (other);
                    auto* data = dynamic_cast<juce::Button*> (badge);
                    require (data && data->isVisible() && ! c->isVisible(), "numeric C stays non-audible on B and V pages");
                    opened = 0; data->onClick();
                    require (opened == (state.blindLargeScreen ? 0 : 1), "DATA uses the existing expansion entrance below full size");
                    require (viewed == 2 && played == 0, "the data label opens C without taking an output");
                }
                panel.setState (state);
                std::optional<ref::TargetRequest> request;
                panel.onTargetRequest = [&] (const auto& next) { request = next; };
                make->onClick();
                require (request && request->action == (phase == 1 ? ref::TargetRequest::Action::cancel : ref::TargetRequest::Action::make),
                         "the visible button reaches the target action");
                const juce::StringArray files { "/fixture/audio.wav" };
                panel.filesDropped (files, 0, 0);
                require (request->action == ref::TargetRequest::Action::importFiles && request->files == files,
                         "the real REF file-drop entrance delivers the selected files");
                state.blindPhase = reference_ui::BlindPhase::active; panel.setState (state);
                require (! bar->isVisible() && ! badge->isVisible() && ! panel.isInterestedInFileDrag (files),
                         "Blind neither exposes targets nor admits drops");
                reference_ui::applyMeasuredTarget (state, targets, false); state.blindPhase = reference_ui::BlindPhase::unavailable;
                panel.setState (state);
                require (! bar->isVisible() && ! panel.isInterestedInFileDrag (files), "unlicensed Reference cannot create targets");
            }
    }
}
}
