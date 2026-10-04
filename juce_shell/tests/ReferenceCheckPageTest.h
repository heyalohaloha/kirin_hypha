#pragma once

// H12: C（CHECK）の画面（300%）。CHECK SET・Check のタブ（CHECK セットの順）・曲・Cue・MATCH、
// 見比べ（Cue 対 A の同じ長さの直近、同じ音量）、4 帯域の要約、Cue の時間軸。200% は今までの選択欄。
#include "ReferenceGuideContractTest.h"
#include "../src/HyphaReferenceCueSummary.h"
#include "../src/HyphaReferenceRuntimeView.h"
#include "../src/HyphaReferenceVersionPage.h"

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
    // 仕様 C：A が Cue の長さ（最長 30 秒）たまるまでは合わせない。gain の代わりに進み具合を出す。
    {
        auto waiting = state;
        waiting.aWindowLoudness = std::numeric_limits<double>::quiet_NaN();
        waiting.aWindowBlocks = 120; waiting.aWindowNeededBlocks = 300;
        require (reference_ui::matchReadout (waiting) == "A 12 / 30 S"
                     && i18n::translate (reference_ui::matchReadout (waiting), i18n::Language::japanese) != "A 12 / 30 S",
                 "while A fills the Cue's window, the page says how far it has come");
    }
    // 2026-10-04（Daisuke「A直近10秒 / Bサビ」）：凡例は比べる側が曲のどの部分かを言う。B は時刻も添える。
    {
        using reference_audition::CuePart;
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        require (reference_ui::cuePartLegend ("B", CuePart::chorus, 62.0, 84.0, true) == "B CHORUS 1:02-1:24"
                     && reference_ui::cuePartLegend ("B", CuePart::loudest, 45.0, 75.0, true) == "B LOUDEST 30 S 0:45-1:15"
                     && reference_ui::cuePartLegend ("B", CuePart::cue, 5.0, 15.0, true) == "B CUE 0:05-0:15"
                     && reference_ui::cuePartLegend ("B", CuePart::whole, 0.0, 259.0, true) == "B WHOLE"
                     && reference_ui::cuePartLegend ("C", CuePart::chorus, 62.0, 84.0, false) == "C CHORUS"
                     && reference_ui::cuePartLegend ("C", CuePart::unknown, nan, nan, false) == "C CUE",
                 "the legend names the part each side plays");
        const auto japanese = [] (const juce::String& english) { return i18n::translate (english, i18n::Language::japanese); };
        require (japanese ("B CHORUS 1:02-1:24") == juce::String (juce::CharPointer_UTF8 ("B\xe3\x82\xb5\xe3\x83\x93 1:02-1:24"))
                     && japanese ("B WHOLE") == juce::String (juce::CharPointer_UTF8 ("B\xe6\x9b\xb2\xe5\x85\xa8\xe4\xbd\x93"))
                     && japanese ("A LAST 18 S / C CHORUS")
                            == juce::String (juce::CharPointer_UTF8 ("A\xe7\x9b\xb4\xe8\xbf\x91" "18\xe7\xa7\x92 / C\xe3\x82\xb5\xe3\x83\x93"))
                     && japanese ("B LOUDEST 30 S 0:45-1:15") != "B LOUDEST 30 S 0:45-1:15"
                     && japanese ("C LOUDEST 30 S") != "C LOUDEST 30 S" && japanese ("C WHOLE") != "C WHOLE",
                 "the parts read in Japanese (A\u76f4\u8fd110\u79d2 / B\u30b5\u30d3)");
        require (japanese (reference_ui::listeningGuide ('V')).contains (juce::String (juce::CharPointer_UTF8 ("\xe8\x80\xb3")))  // 耳
                     && japanese (reference_ui::listeningGuide ('C')).contains ("C")
                     && japanese ("B SET RANGE") != "B SET RANGE",
                 "the listening guide and the B set range read in Japanese");
        auto chorusCheck = state;
        chorusCheck.cuePart = CuePart::chorus;
        require (reference_ui::cueSpectrumLegend (chorusCheck).contains (" / C CHORUS"), "the C legend names the chorus");
    }
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
    const auto write = [] (juce::Component& target, const juce::String& name)
    {
        const auto directory = juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_UI_ABCV_OUTPUT", {});
        if (directory.isEmpty()) return;
        juce::Image image (juce::Image::ARGB, target.getWidth(), target.getHeight(), true);
        juce::Graphics graphics (image);
        graphics.fillAll (juce::Colour (0xff16110d));
        target.paintEntireComponent (graphics, true);
        juce::FileOutputStream stream { juce::File (directory).getChildFile (name) };
        if (stream.openedOk()) { stream.setPosition (0); stream.truncate(); juce::PNGImageFormat().writeImageToStream (image, stream); }
    };
    write (panel, "abcv_c_900.png");

    state.bSelected = true;
    state.audibleComparisonSlot = 2;
    state.appliedGainDb = -2.5;
    state.cuePlayheadSeconds = 70.0;
    panel.setState (state);
    require (reference_ui::matchReadout (state) == juce::String (juce::CharPointer_UTF8 ("MATCHED / C \xe2\x88\x92" "2.5 dB / FIXED")),
             "while C plays, the page says its gain is matched and fixed");
    match->onClick();
    require (matched == 1 && heard == 1, "MATCH while C plays matches it again");
    write (panel, "abcv_c_900_playing.png");
    state.viewBindings = { "spectrum_full" };
    panel.setState (state);
    write (panel, "abcv_c_900_spectrum.png");
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

    // H13: V の画面（300%）。VERSION と CHECK SET（C と共用）、WHOLE（タイムライン）と Check のタブ。タブは
    // 見るものだけを替え、音も C の選択も変えない。Check のタブでは同じ区間の A と V を同じ定義で比べる。
    auto vState = state;
    vState.comparisonSlot = 1;
    vState.bSelected = true;
    vState.audibleComparisonSlot = 1;
    vState.appliedGainDb = -0.9;
    vState.versions = { { "v1", "Mix v7" }, { "v2", "Mix v6" } };
    vState.versionId = "v1";
    auto timeline = std::make_shared<reference_audition::VisualTimeline>();
    timeline->aPairKirin = kirinWindow (0.0f, { -20.0, -14.0, -18.0, -30.0 }, 300);
    timeline->vPairKirin = kirinWindow (3.0f, { -19.5, -13.0, -17.5, -29.0 }, 300);
    vState.visualTimeline = timeline;
    reference_ui::Component vPanel;
    vPanel.setVisible (true);
    vPanel.setPresentationContext (presentation::forEditor (900, 600));
    vPanel.setSize (888, 470);
    vPanel.setState (vState);
    auto* vTabs = dynamic_cast<reference_ui::CheckTabs*> (vPanel.findChildWithID ("reference-check-tabs"));
    auto* lanes = dynamic_cast<reference_ui::ComparisonView*> (vPanel.findChildWithID ("reference-comparison-view"));
    require (vTabs && lanes && vTabs->isVisible() && vTabs->tabs().size() == 4 && vTabs->tabs()[0].label == "WHOLE"
                 && vTabs->selected() == "whole" && lanes->isVisible() && lanes->sameSectionCheck().isEmpty()
                 && vPanel.findChildWithID ("reference-version")->isVisible() && vPanel.findChildWithID ("reference-preset")->isVisible()
                 && ! vPanel.findChildWithID ("reference-cue")->isVisible() && ! vPanel.findChildWithID ("reference-check-song")->isVisible(),
             "300% V: VERSION, the shared CHECK SET and WHOLE first; C's song and Cue stay on the C page");
    juce::String vChosen;
    vPanel.onSelectCheck = [&] (const juce::String& id) { vChosen = id; };
    vTabs->onChoose ("chk-low");
    require (lanes->sameSectionCheck() == "Low End" && vChosen.isEmpty() && reference_ui::sameSectionReady (timeline.get()),
             "a Check tab compares the same section without changing the sound or C's choice");
    write (vPanel, "abcv_v_900_check.png");
    vTabs->onChoose ("whole");
    require (lanes->sameSectionCheck().isEmpty(), "WHOLE returns to the song timeline");
    vPanel.onSelectCheck = {};

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
