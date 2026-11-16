#pragma once
#include "ReferenceAWindowTest.h"
#include "../src/HyphaReferenceTargetView.h"
#include "../src/HyphaReferenceOverlay.h"
#include "../src/HyphaReferenceCueSummary.h"
#include "../src/HyphaReferenceAComparison.h"
#include "../src/reference_audition/ReferenceTargetService.h"
#include "../src/reference_audition/ReferenceTargetPeakWindow.h"

namespace hypha::tests::measured_target_ui
{
// A saved target is B data. C keeps its Check and never exposes target controls.
inline void verifyExistingComparison (const reference_audition::MeasuredTarget& target)
{
    using reference_guide_contract::require;
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (const auto size : observatory::sizePresets)
        {
            const auto viewport = observatory::displayViewport (size.width, size.height);
            observatory::View shell (observatory::Role::post);
            shell.setSize (viewport.width, viewport.height); shell.setDomain (observatory::Domain::reference);
            shell.setExternalAnalysisBodyActive (true);
            reference_ui::Component panel;
            panel.setVisible (true); panel.setPresentationContext (presentation::forEditor (viewport.width, viewport.height));
            panel.setSize (shell.analysisBodyBounds().getWidth(), shell.analysisBodyBounds().getHeight());
            auto c = reference_a_window::cPage();
            c.blindLargeScreen = size.density == observatory::Density::inspection;
            const auto oldCue = c.cueKirin;
            reference_audition::TargetSnapshot targets;
            targets.collection.items = { target }; targets.collection.selected = target.id;
            reference_ui::applyMeasuredTarget (c, targets, true);
            panel.setState (c);
            auto* bar = panel.findChildWithID ("reference-targets");
            require (bar && ! bar->isVisible() && ! panel.isInterestedInFileDrag ({ "/fixture/audio.wav" })
                         && ! panel.state().targetSelected && panel.state().cueKirin == oldCue,
                     "a saved target never replaces C's Check or exposes B's target controls");
            auto b = reference_a_window::bPage();
            b.blindLargeScreen = c.blindLargeScreen;
            reference_ui::applyMeasuredTarget (b, targets, true); panel.setState (b);
            require (bar->isVisible() && panel.getLocalBounds().contains (bar->getBounds()), "B owns the target bar at every size");
            b.targets.issue = reference_audition::targetsFullIssue (5); panel.setState (b);
            const auto notice = panel.statusTextLayout (panel.footerStatusStrip().getLocalBounds());
            require (notice.line.text.contains (b.targets.issue), "B reports the number of targets that did not fit");
            b.targets.issue.clear(); panel.setState (b);
            auto* details = dynamic_cast<juce::Button*> (bar->findChildWithID ("reference-target-details"));
            auto* make = dynamic_cast<juce::Button*> (bar->findChildWithID ("reference-make-target"));
            require (details && make && ! make->isVisible(), "target management begins closed");
            details->onClick();
            require (make->isVisible(), "B details exposes MAKE TARGET");
            details->onClick();
            if (c.blindLargeScreen)
            {
                auto* list = dynamic_cast<reference_ui::SongList*> (panel.findChildWithID ("reference-song-list"));
                require (list && list->isVisible() && ! list->rows().empty() && list->rows().back().target
                             && list->rows().back().selected, "B list retains the target saved by an older project");
            }
            juce::String explanation;
            panel.onExplain = [&] (const juce::String& text) { explanation = text; };
            panel.requestRole (3);
            require (explanation == reference_ui::targetSilentText, "pressing B on a target explains that it has no audio");
            const juce::StringArray files { "/fixture/audio.wav" };
            std::optional<reference_audition::TargetRequest> request;
            panel.onTargetRequest = [&] (const auto& next) { request = next; };
            panel.filesDropped (files, 0, 0);
            require (request && request->action == reference_audition::TargetRequest::Action::importFiles && request->files == files,
                     "B's real drop entrance delivers the files");
            b.blindPhase = reference_ui::BlindPhase::active; panel.setState (b);
            require (! bar->isVisible() && ! panel.isInterestedInFileDrag (files), "Blind hides target management and rejects drops");
            reference_ui::applyMeasuredTarget (b, targets, false); b.blindPhase = reference_ui::BlindPhase::unavailable; panel.setState (b);
            require (! bar->isVisible() && ! panel.isInterestedInFileDrag (files), "unlicensed Reference rejects target management");
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
    auto state = reference_a_window::bPage();
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
                auto state = reference_a_window::bPage(); state.visualTimeline = timeline; state.aKirin = timeline->aKirin;
                state.aWindowLoudness = timeline->aTargetLoudness.lufs;
                const auto gain = reference_ui::targetOverlayGainDb (state, collection.items.front());
                require (std::abs (gain) < 0.0001, "equal mono/stereo audio has zero B target gain before and after restore");
                for (size_t band = 0; band < 64; ++band)
                    require (std::abs (collection.items.front().spectrum.medianDb[band] + gain - state.aKirin->medianDb[band]) < 0.0001,
                             "the same mono/stereo audio draws the same target curve");
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
    ref::TargetCollection old { { target }, target.id };
    juce::XmlElement project ("KirinHyphaState"); old.write (project);
    const auto restored = ref::TargetCollection::read (project);
    require (restored.items.size() == 1 && restored.selected == target.id
        && restored.items.front().spectrum.medianDb == target.spectrum.medianDb,
        "the previous project's selected target survives its real XML restore");
    verifyExistingComparison (restored.items.front());
    verifySelection (target);
}
}
