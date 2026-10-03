#pragma once

// H12: C（CHECK）の画面（300%）。CHECK SET・Check のタブ（CHECK セットの順）・曲・Cue・MATCH、
// 見比べ（Cue 対 A の同じ長さの直近、同じ音量）、4 帯域の要約、Cue の時間軸。200% は今までの選択欄。
#include "ReferenceGuideContractTest.h"
#include "../src/HyphaReferenceCueSummary.h"
#include "../src/HyphaReferenceRuntimeView.h"

namespace hypha::tests
{
inline std::shared_ptr<reference_audition::KirinSpectrumWindow> kirinWindow (float tilt, std::array<double, 4> balance, int frames)
{
    auto window = std::make_shared<reference_audition::KirinSpectrumWindow>();
    for (int band = 0; band < 64; ++band)
    {
        window->centersHz.push_back (20.0 * std::pow (1'000.0, band / 63.0));
        const auto level = -30.0f - 0.6f * static_cast<float> (band) + tilt * static_cast<float> (band) / 63.0f;
        window->medianDb.push_back (level);
        window->p10Db.push_back (level - 4.0f);
        window->p90Db.push_back (level + 4.0f);
    }
    window->balanceDb = balance;
    window->frames = window->wantedFrames = frames;
    return window;
}

inline void verifyReferenceCheckPage()
{
    using namespace reference_guide_contract;
    auto state = named ("ready");
    state.separateComparisons = true;
    state.comparisonSlot = 2;
    state.comparisonMode = "loudness_match";
    state.checks = { { "chk-low/cand-1", "Low End  /  Hello" }, { "chk-low/cand-2", "Low End  /  MONTERO" },
                     { "chk-vocal/cand-1", "Vocal  /  Hello" }, { "chk-air/", "Air / NO SOURCE IN KIRIN OS" } };
    state.checkId = "chk-low/cand-1";
    state.aKirin = kirinWindow (0.0f, { -20.0, -14.0, -18.0, -30.0 }, 300);
    state.cueKirin = kirinWindow (-6.0f, { -16.5, -11.5, -15.0, -28.5 }, 300);
    state.aWindowLoudness = -12.0;
    state.cueLoudness = -9.0;  // 鳴らすときの gain は −3 dB
    state.cueStartSeconds = 62.0; state.cueEndSeconds = 92.0; state.sourceDurationSeconds = 295.0; state.cueLoops = true;

    require (std::abs (reference_ui::comparisonGainDb (state) + 3.0) < 1.0e-9
                 && reference_ui::matchReadout (state) == juce::String (juce::CharPointer_UTF8 ("ON PLAY / C \xe2\x88\x92" "3.0 dB")),
             "before C plays, the page says the gain C will play at");
    reference_ui::Component panel;
    panel.setVisible (true);
    panel.setPresentationContext (presentation::forEditor (900, 600));
    panel.setSize (888, 470);
    panel.setState (state);
    auto* tabs = dynamic_cast<reference_ui::CheckTabs*> (panel.findChildWithID ("reference-check-tabs"));
    auto* song = dynamic_cast<juce::ComboBox*> (panel.findChildWithID ("reference-check-song"));
    auto* match = dynamic_cast<juce::TextButton*> (panel.findChildWithID ("reference-match"));
    auto* version = panel.findChildWithID ("reference-version");
    auto* check = panel.findChildWithID ("reference-check");
    require (tabs && song && match && version && check, "the C page owns its tabs, song and MATCH");
    require (tabs->isVisible() && tabs->tabs().size() == 3 && tabs->tabs()[0].label == "Low End"
                 && tabs->tabs()[2].label == "Air" && tabs->selected() == "chk-low"
                 && song->isVisible() && song->getNumItems() == 2 && song->getText() == "Hello"
                 && match->isVisible() && ! version->isVisible() && ! check->isVisible(),
             "300%: the Checks are tabs in the set's order, the song is chosen within the Check, V stays on its page");
    for (size_t index = 0; index < tabs->tabs().size(); ++index)
        require (panel.getLocalBounds().contains (tabs->getBounds()) && ! tabs->tabBounds (index).isEmpty(), "every tab is reachable");

    juce::String chosen;
    int heard = 0, matched = 0;
    panel.onSelectCheck = [&] (const juce::String& id) { chosen = id; };
    panel.onSelectC = [&] { ++heard; };
    panel.onMatch = [&] { ++matched; };
    tabs->onChoose ("chk-vocal");
    require (chosen == "chk-vocal/cand-1", "a tab keeps the song when the Check has it");
    song->setSelectedId (2, juce::sendNotificationSync);
    require (chosen == "chk-low/cand-2", "the song is chosen within the Check");
    match->onClick();
    require (heard == 1 && matched == 0, "MATCH while C is silent plays C matched");

    // 書き出し（見た目の確認用）：KIRIN_REFERENCE_UI_ABCV_OUTPUT=<dir>。
    const auto write = [&panel] (const juce::String& name)
    {
        const auto directory = juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_UI_ABCV_OUTPUT", {});
        if (directory.isEmpty()) return;
        juce::Image image (juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
        juce::Graphics graphics (image);
        graphics.fillAll (juce::Colour (0xff16110d));
        panel.paintEntireComponent (graphics, true);
        juce::FileOutputStream stream { juce::File (directory).getChildFile (name) };
        if (stream.openedOk()) { stream.setPosition (0); stream.truncate(); juce::PNGImageFormat().writeImageToStream (image, stream); }
    };
    write ("abcv_c_900.png");

    state.bSelected = true;
    state.audibleComparisonSlot = 2;
    state.appliedGainDb = -2.5;
    state.cuePlayheadSeconds = 70.0;
    panel.setState (state);
    require (reference_ui::matchReadout (state) == juce::String (juce::CharPointer_UTF8 ("MATCHED / C \xe2\x88\x92" "2.5 dB / FIXED")),
             "while C plays, the page says its gain is matched and fixed");
    match->onClick();
    require (matched == 1 && heard == 1, "MATCH while C plays matches it again");
    write ("abcv_c_900_playing.png");
    state.viewBindings = { "spectrum_full" };
    panel.setState (state);
    write ("abcv_c_900_spectrum.png");
    state.viewBindings.clear();

    state.comparisonMode = "original";
    panel.setState (state);
    require (! match->isVisible() && reference_ui::matchReadout (state) == "ORIGINAL LEVEL"
                 && std::abs (reference_ui::comparisonGainDb (state)) < 1.0e-12,
             "a Check at its original level has no MATCH and compares as heard");

    state.comparisonMode = "loudness_match";
    state.bSelected = false;
    panel.setPresentationContext (presentation::forEditor (600, 400));
    panel.setSize (588, 300);
    panel.setState (state);
    require (! tabs->isVisible() && ! match->isVisible() && version->isVisible(), "200% keeps the selectors it had");
    panel.onSelectCheck = {}; panel.onSelectC = {}; panel.onMatch = {};

    // CHECK SET：Kirin OS で順位を付けたセットだけを順位の順に「1 / 2」を添えて出し、選んでいる Preset は残す。
    reference_ui::State sets;
    sets.presets = { { "basic", "Basic 5" }, { "master", "Mastering" }, { "old", "Old" }, { "other", "Other" } };
    sets.presetId = "old";
    reference_ui::runtime_view::rankCheckSets (sets, {});
    require (sets.presets.size() == 4, "without ranks every Preset stays (older Kirin OS)");
    reference_ui::runtime_view::rankCheckSets (sets, { { "master", "r1", 1 }, { "basic", "r2", 2 } });
    require (sets.presets.size() == 3 && sets.presets[0].label == "Mastering   1 / 2" && sets.presets[1].label == "Basic 5   2 / 2"
                 && sets.presets[2].id == "old",
             "CHECK SET lists the ranked sets in rank order, and keeps the chosen one");
}
}
