#pragma once
#include "../src/HyphaReferenceOverlay.h"
#include "../src/HyphaReferenceTargetView.h"

// A synthetic old-format project: check its saved target and C choice using the real XML and UI code.
inline bool verifyPreviewRound2 (hypha::reference_ui::State state)
{
    using namespace hypha;
    using namespace reference_audition;
    MeasuredTarget target;
    target.id = "target-1234"; target.name = "Example target";
    target.duration = 12.0; target.integrated = -14.0; target.maximumTruePeak = -1.0;
    target.setCenters();
    target.spectrum.p10Db.assign (64, -33.0f);
    target.spectrum.medianDb.assign (64, -30.0f);
    target.spectrum.p90Db.assign (64, -27.0f);
    target.spectrum.balanceDb = { -20.0, -22.0, -28.0, -35.0 };
    if (! target.valid()) return false;
    TargetCollection old;
    old.items.push_back (target); old.selected = target.id;
    juce::XmlElement xml ("Project"); old.write (xml);
    const auto restored = TargetCollection::read (xml);
    if (restored.items.size() != 1 || restored.selected != target.id
        || restored.items[0].spectrum.medianDb != target.spectrum.medianDb) return false;
    TargetCollection full;
    for (int i = 0; i < 17; ++i)
    {
        auto item = target; item.id = "target-" + juce::String::toHexString (i + 1);
        full.items.push_back (std::move (item));
    }
    juce::XmlElement fullXml ("Project"); full.write (fullXml);
    if (TargetCollection::read (fullXml).items.size() != TargetCollection::capacity) return false;
    state.comparisonSlot = 2; state.checkId = "check-1/song-1";
    const auto previousCue = state.cueKirin;
    TargetSnapshot snapshot; snapshot.collection = restored;
    reference_ui::applyMeasuredTarget (state, snapshot, true);
    if (state.targetSelected || state.checkId != "check-1/song-1" || state.cueKirin != previousCue) return false;
    state.comparisonSlot = 3; state.songs.clear(); state.songFacts.clear(); state.overlay = {};
    const auto items = reference_ui::overlayItems (state);
    if (items.size() != 1 || ! items[0].target || ! items[0].drawable()) return false;
    VisualPreferences preferences;
    BalanceOverlay overlay; overlay.used = overlay.all = true; overlay.lines.push_back ({ "song-1", 0 });
    preferences.setOverlay (overlay);
    preferences.showNewTargets ({ target.id });
    preferences.showNewTargets ({ target.id });
    const auto saved = preferences.overlay();
    if (saved.lines.size() != 2 || ! saved.find (target.id) || saved.find ("song-1")->colour != 0) return false;
    state.overlay = saved;
    if (reference_ui::overlayShape (reference_ui::overlayItems (state)).count != 1) return false;
    reference_ui::Component panel;
    panel.setPresentationContext (presentation::forOutput (900, 600, presentation::OutputTarget::referencePreview));
    panel.setSize (900, 500); panel.setState (state);
    auto* list = dynamic_cast<reference_ui::SongList*> (panel.findChildWithID ("reference-song-list"));
    if (list == nullptr || ! list->isVisible() || list->rows().size() != 1 || ! list->rows()[0].selected) return false;
    juce::String explanation;
    panel.onExplain = [&explanation] (const juce::String& text) { explanation = text; };
    panel.requestRole (3);
    if (explanation != reference_ui::targetSilentText) return false;
    state.targets.collection.selected.clear(); state.targets.measuring = true;
    panel.setState (state);
    if (list->rows().size() != 2 || list->rows()[1].preparation != "MEASURING") return false;
    juce::String chosen;
    panel.onTargetRequest = [&chosen] (const TargetRequest& request) { if (request.action == TargetRequest::Action::select) chosen = request.id; };
    list->onChoose (target.id);
    return chosen == target.id;
}
