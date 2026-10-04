#pragma once

// H12: C（CHECK）の画面（300%）。CHECK SET・Check のタブ（CHECK セットの順）・曲・Cue・MATCH、
// 見比べ（Cue 対 A の同じ長さの直近、同じ音量）、4 帯域の要約、Cue の時間軸。200% は今までの選択欄。
#include "ReferenceGuideContractTest.h"
#include "ReferenceHoverHelpTest.h"
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
    // 2026-10-05（Daisuke「入りきらないときは 2 段にする」）：Check が多くて 1 段に入らなければ 2 段に分け、どの名前も
    // 切らない（Mac の実機の 300% で Mastering の 8 項目が「音色…」「セク…」と切れていた）。
    {
        auto many = state;
        many.checks.clear();
        for (const auto* label : { "Tonal balance", "Loudness", "True Peak", "Dynamics", "Stereo and phase",
                                   "Low-end consistency", "Section difference", "Album context" })
            many.checks.push_back ({ juce::String ("chk-") + juce::String (label).removeCharacters (" ") + "/ref-a",
                                     juce::String (label) + "  /  Hello" });
        many.checkId = many.checks.front().id;
        const auto font = labelFont (presentation::forEditor (900, 600), typography::TextRole::body, typography::Composition::information);
        for (const auto language : { i18n::Language::english, i18n::Language::japanese })
        {
            const i18n::ScopedLanguage scoped (language);
            panel.setState (many);
            require (tabs->tabs().size() == 8 && tabs->getHeight() >= 48 && panel.getLocalBounds().contains (tabs->getBounds()),
                     "eight Checks take two rows at 300%");
            for (size_t index = 0; index < tabs->tabs().size(); ++index)
                require (text_style::shownWidth (font, tabs->tabs()[index].label) + 20.0f
                             <= static_cast<float> (tabs->tabBounds (index).getWidth()),
                         "every Check's name shows whole on two rows: " + tabs->tabs()[index].label);
        }
        panel.setState (state);
        require (tabs->getHeight() < 48, "three Checks stay on one row");
    }

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
    // 鳴っていれば gain だけ（合わせて固定したことは状態の行が言い、色で鳴っていると分かる。2026-10-05、Mac の実機で
    // 「MATCH済み / C −1.2…」と切れた）。
    require (reference_ui::matchReadout (state) == juce::String (juce::CharPointer_UTF8 ("C \xe2\x88\x92" "2.5 dB")),
             "while C plays, the page reads the gain it plays at (the status line says it is matched and fixed)");
    match->onClick();
    require (matched == 1 && heard == 1, "MATCH while C plays matches it again");
    write (panel, "abcv_c_900_playing.png");
    state.viewBindings = { "spectrum_full" };
    panel.setState (state);
    write (panel, "abcv_c_900_spectrum.png");
    // 2026-10-04（下の行の説明）：指した項目の説明。部品は今の説明（ツールチップ）、図は描いたときに添えた説明。
    namespace text = reference_ui::help_text;
    std::set<juce::String> helpSeen;
    {
        const auto texts = helpTextsOf (panel);
        for (const auto* expected : { text::cueSpectrum, text::blauert, text::cueBands, text::cueBar, text::checkTabs, text::match })
            require (texts.count (expected) == 1, juce::String ("pointing at the C page explains it: ") + expected);
        require (texts.count (match->getTooltip()) == 1 && reference_ui::help::shownInLine (*match),
                 "a control's own help goes to the line too, not to a popup");
        for (const auto* id : { "reference-a", "reference-b", "reference-c", "reference-ref" })
            if (auto* button = dynamic_cast<juce::Button*> (panel.findChildWithID (id)); button != nullptr && button->isVisible())
                require (panel.helpAt (button->getBounds().getCentre()) == button->getTooltip(),
                         juce::String ("pointing at a role button puts its help in the line: ") + id + " -> \""
                             + panel.helpAt (button->getBounds().getCentre()) + "\" vs \"" + button->getTooltip() + "\"");
        helpSeen.insert (texts.begin(), texts.end());
    }
    // 2026-10-04（範囲の帯）：Dynamics・Loudness・Stereo・Waveform・Transient は A と C の帯。A がたまる前は C だけ。
    {
        auto strips = state;
        auto measurement = std::make_shared<reference_audition::RuntimeDetailedMeasurement>();
        measurement->audio = { 48'000, 2, 295 * 48'000 };
        const std::int64_t hop = 9'600;  // 200 ms（Kirin OS の長い曲の区間）
        const auto hops = 295 * 48'000 / hop;
        reference_audition::RuntimeNullableIntegerSeries crest, lufsM, lufsS, correlation, width;
        reference_audition::RuntimeMeasurementWaveform waveform { hop, { {}, {} }, { {}, {} } };
        reference_audition::RuntimeMeasurementTransient transient { hop, {} };
        for (std::int64_t index = 0; index < hops; ++index)
        {
            const auto wave = std::sin (static_cast<double> (index) * 0.37);
            crest.push_back (std::llround ((12.0 + 1.5 * wave) * 1000.0));
            lufsM.push_back (std::llround ((-9.0 + 3.0 * wave) * 1000.0));
            lufsS.push_back (std::llround ((-9.5 + 2.0 * wave) * 1000.0));
            correlation.push_back (std::llround ((0.6 + 0.1 * wave) * 1000.0));
            width.push_back (std::llround ((50.0 + 8.0 * wave) * 100.0));
            for (int c = 0; c < 2; ++c)
            {
                waveform.samplePeakMillidbfs[static_cast<size_t> (c)].push_back (std::llround ((-1.0 + 0.8 * wave) * 1000.0));
                waveform.rmsMillidbfs[static_cast<size_t> (c)].push_back (std::llround ((-13.0 + 1.5 * wave) * 1000.0));
            }
            transient.onsetStrengthQ15.push_back (std::llround (std::max (0.0, wave) * 0.2 * 32'767.0));
        }
        measurement->dynamics = reference_audition::RuntimeMeasurementTimeline { hop, { { "crest_millidb", crest } } };
        measurement->loudness = reference_audition::RuntimeMeasurementTimeline { hop, { { "lufs_m_millilu", lufsM }, { "lufs_s_millilu", lufsS } } };
        measurement->stereo = reference_audition::RuntimeMeasurementTimeline { hop, { { "correlation_milli", correlation }, { "width_basis_points", width } } };
        measurement->waveform = waveform;
        measurement->transient = transient;
        strips.cueMeasurement = measurement;
        strips.cuePart = reference_audition::CuePart::chorus;
        auto timeline = std::make_shared<reference_audition::VisualTimeline>();
        timeline->binding.matchWindowBlocks = 300;
        timeline->aTickChannels = 2;
        auto ticks = std::make_shared<std::vector<KirinReferenceVisualBin>>();
        for (int index = 0; index < 300; ++index)
        {
            const auto wave = std::sin (static_cast<double> (index) * 0.21);
            KirinReferenceVisualBin bin {};
            bin.frames = 4'800;
            const auto mid = 48.0 * (1.0 + 0.3 * wave), side = mid * 0.16;
            bin.mid = mid; bin.side = side; bin.cross = mid - side;
            bin.rms[0] = bin.rms[1] = std::sqrt ((mid + side) / 4'800.0);
            bin.peak[0] = bin.peak[1] = bin.rms[0] * 3.2;
            bin.true_peak = bin.rms[0] * 3.4;
            bin.momentary_lufs = -11.0 + 2.0 * wave;
            bin.short_lufs = -11.5 + 1.0 * wave;
            ticks->push_back (bin);
        }
        timeline->aTicks = ticks;
        strips.visualTimeline = timeline;
        strips.aKirin = state.aKirin;
        const auto differentPixels = [] (const juce::Image& left, const juce::Image& right)
        {
            int count = 0;
            for (int y = 0; y < std::min (left.getHeight(), right.getHeight()); ++y)
                for (int x = 0; x < std::min (left.getWidth(), right.getWidth()); ++x)
                    if (left.getPixelAt (x, y) != right.getPixelAt (x, y)) ++count;
            return count;
        };
        juce::Image previous;
        for (const auto* binding : { "dynamics", "loudness", "stereo", "waveform", "transient" })
        {
            strips.viewBindings = { binding };
            panel.setState (strips);
            write (panel, juce::String ("abcv_c_900_") + binding + ".png");
            const auto image = panel.createComponentSnapshot (panel.getLocalBounds());
            require (previous.isNull() || differentPixels (previous, image) > 500, "each Check draws its own strips");
            previous = image;
        }
        strips.viewBindings = { "dynamics", "loudness" };  // Kirin OS の Dynamics の Check は 2 つの図を並べる（狭い枠）
        panel.setState (strips);
        write (panel, "abcv_c_900_dynamics_loudness.png");
        auto waiting = strips;
        waiting.viewBindings = { "dynamics" };
        waiting.visualTimeline = std::make_shared<reference_audition::VisualTimeline>();
        panel.setState (waiting);
        write (panel, "abcv_c_900_dynamics_waiting.png");
        const auto waitingImage = panel.createComponentSnapshot (panel.getLocalBounds());
        strips.viewBindings = { "dynamics" };
        panel.setState (strips);
        require (differentPixels (waitingImage, panel.createComponentSnapshot (panel.getLocalBounds())) > 200,
                 "until A has 3 seconds, only C's strips are drawn");
        for (const auto* binding : { "dynamics", "loudness", "stereo", "waveform", "transient" })
        {
            strips.viewBindings = { binding };
            panel.setState (strips);
            const auto texts = helpTextsOf (panel);
            require (texts.count (text::strips) == 1 && texts.size() >= 6, juce::String ("pointing at the strips explains them: ") + binding);
            helpSeen.insert (texts.begin(), texts.end());
        }
        for (const auto* expected : { text::crest, text::movement, text::momentary, text::width, text::correlation, text::peak,
                                      text::rms, text::onset, text::attack })
            require (helpSeen.count (expected) == 1, juce::String ("every strip explains itself: ") + expected);
    }
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
    require (helpTextsOf (panel).empty() && ! reference_ui::help::shownInLine (*version),
             "below 300% the help stays in the popups it had");
    panel.onSelectCheck = {}; panel.onSelectC = {}; panel.onMatch = {};

    // H13: V の画面（300%）。VERSION（全幅）と、WHOLE（タイムライン）の後に決まった 4 つのタブ（音色・ダイナミクス・
    // ステレオ・低域、2026-10-04 Daisuke。C の CHECK SET と切り離す）。タブは見るものだけを替え、音も C の選択も
    // 変えない。項目のタブでは同じ区間の A と V を同じ定義で比べる。
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
    const auto labelsOf = [] (const reference_ui::CheckTabs& strip)
    {
        juce::StringArray labels;
        for (const auto& tab : strip.tabs()) labels.add (tab.label);
        return labels.joinIntoString (",");
    };
    require (vTabs && lanes && vTabs->isVisible() && labelsOf (*vTabs) == "WHOLE,TONE,DYNAMICS,STEREO,LOW END"
                 && vTabs->selected() == "whole" && lanes->isVisible() && lanes->sameSectionCheck().isEmpty()
                 && vPanel.findChildWithID ("reference-version")->isVisible() && ! vPanel.findChildWithID ("reference-preset")->isVisible()
                 && ! vPanel.findChildWithID ("reference-cue")->isVisible() && ! vPanel.findChildWithID ("reference-check-song")->isVisible(),
             "300% V: VERSION, WHOLE and the fixed TONE / DYNAMICS / STEREO / LOW END; C's CHECK SET, song and Cue stay on C");
    {
        auto* versionField = vPanel.findChildWithID ("reference-version");
        auto* cButton = vPanel.findChildWithID ("reference-c");
        require (versionField != nullptr && cButton != nullptr && versionField->getWidth() > 400,
                 "without the CHECK SET, VERSION takes the whole selector row (the AUTO mark is not cut)");
        auto noChecks = vState;
        noChecks.checks.clear();
        vPanel.setState (noChecks);
        require (vTabs->isVisible() && vTabs->tabs().size() == 5, "V's tabs do not depend on C's Checks");
        vPanel.setState (vState);
    }
    juce::String vChosen;
    vPanel.onSelectCheck = [&] (const juce::String& id) { vChosen = id; };
    vTabs->onChoose ("v-low");
    require (lanes->sameSectionCheck() == "LOW END" && vChosen.isEmpty() && reference_ui::sameSectionReady (timeline.get()),
             "a V tab compares the same section without changing the sound or C's choice");
    write (vPanel, "abcv_v_900_check.png");
    // 2026-10-04（範囲の帯）：Dynamics・Stereo などの Check は、同じ区間の A と V の時間の線と帯。
    {
        auto pairs = std::make_shared<reference_audition::VisualTimeline> (*timeline);
        auto a = std::make_shared<std::vector<KirinReferenceVisualBin>>(), v = std::make_shared<std::vector<KirinReferenceVisualBin>>();
        for (int index = 0; index < 240; ++index)
        {
            for (auto [ticks, level, spread] : { std::tuple { a.get(), 1.0, 0.16 }, std::tuple { v.get(), 0.8, 0.25 } })
            {
                const auto wave = std::sin (static_cast<double> (index) * 0.17 + (level < 1.0 ? 0.6 : 0.0));
                KirinReferenceVisualBin bin {};
                bin.frames = 4'800;
                const auto mid = 48.0 * level * (1.0 + 0.4 * wave), side = mid * spread;
                bin.mid = mid; bin.side = side; bin.cross = mid - side;
                bin.rms[0] = bin.rms[1] = std::sqrt ((mid + side) / 4'800.0);
                bin.peak[0] = bin.peak[1] = bin.rms[0] * (3.0 + 0.4 * wave);
                bin.true_peak = bin.peak[0] * 1.05;
                bin.momentary_lufs = -11.0 + 20.0 * std::log10 (level) + 2.5 * wave;
                bin.short_lufs = -11.5 + 20.0 * std::log10 (level) + 1.2 * wave;
                ticks->push_back (bin);
            }
        }
        pairs->aPairTicks = a;
        pairs->vPairTicks = v;
        pairs->pairTickChannels = 2;
        auto stripsState = vState;
        stripsState.visualTimeline = pairs;
        stripsState.checkViewBindings.clear();  // V の項目の表示は決まっていて、C の Check の表示を見ない
        vPanel.setState (stripsState);
        vTabs->onChoose ("v-dynamics");
        write (vPanel, "abcv_v_900_dynamics.png");
        {
            const auto texts = helpTextsOf (vPanel);
            for (const auto* expected : { text::crest, text::movement, text::attack, text::onset })
                require (texts.count (expected) == 1, juce::String ("V's DYNAMICS shows crest, movement, attack and onset: ") + expected);
            helpSeen.insert (texts.begin(), texts.end());
        }
        const auto dynamicsImage = vPanel.createComponentSnapshot (vPanel.getLocalBounds());
        vTabs->onChoose ("v-stereo");
        write (vPanel, "abcv_v_900_stereo.png");
        {
            const auto texts = helpTextsOf (vPanel);
            for (const auto* expected : { text::width, text::correlation, text::timeLines, text::versionBands, text::versionTabs })
                require (texts.count (expected) == 1, juce::String ("pointing at the V page explains it: ") + expected);
            helpSeen.insert (texts.begin(), texts.end());
        }
        int changed = 0;
        const auto stereoImage = vPanel.createComponentSnapshot (vPanel.getLocalBounds());
        for (int y = 0; y < stereoImage.getHeight(); ++y)
            for (int x = 0; x < stereoImage.getWidth(); ++x)
                if (stereoImage.getPixelAt (x, y) != dynamicsImage.getPixelAt (x, y)) ++changed;
        require (changed > 500, "V's DYNAMICS and STEREO draw their own strips");
        vPanel.setState (vState);
    }
    vTabs->onChoose ("whole");
    require (lanes->sameSectionCheck().isEmpty(), "WHOLE returns to the song timeline");
    {
        const auto texts = helpTextsOf (vPanel);
        require (texts.count (text::whole) == 1 && texts.count (text::versionTabs) == 1, "pointing at WHOLE explains it");
        helpSeen.insert (texts.begin(), texts.end());
    }
    verifyReferenceHelpFits (helpSeen);
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
