#pragma once

#include <map>
#include <set>

#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaReferenceAction.h"
#include "HyphaReferenceGainText.h"
#include "HyphaOsAccess.h"
#include "HyphaPresentationContext.h"
#include "HyphaReferenceGuide.h"
#include "HyphaReferenceStatusModel.h"
#include "HyphaReferenceSelectorLookAndFeel.h"
#include "HyphaReferenceHelp.h"
#include "HyphaReferenceStatusStrip.h"
#include "HyphaReferenceComparisonView.h"
#include "HyphaReferenceSongList.h"
#include "HyphaReferenceCheckTabs.h"
#include "reference_audition/ReferenceCuePart.h"
#include "reference_audition/ReferenceKirinSpectrum.h"
#include "reference_audition/ReferenceRuntimeV2Measurement.h"
#include "reference_audition/ReferenceRuntimeV2Profile.h"
#include "reference_audition/ReferencePendingAudition.h"
#include "reference_audition/ReferenceTrackingState.h"

namespace hypha::reference_ui
{
enum class Readiness
{
    disconnected,
    waiting,
    verifying,
    ready,
    rejected,
};

enum class BlindPhase
{
    unavailable,
    available,
    starting,
    active,
    revealed,
    invalidated,
};

inline bool isBlindSession (BlindPhase phase) noexcept
{
    return phase == BlindPhase::starting || phase == BlindPhase::active
        || phase == BlindPhase::revealed || phase == BlindPhase::invalidated;
}

inline bool isBlindAudition (BlindPhase phase) noexcept
{
    return phase == BlindPhase::active || phase == BlindPhase::revealed;
}

inline double unavailableValue() noexcept
{
    return std::numeric_limits<double>::quiet_NaN();
}

// A／B／C／V の役の文字。slot 1 = V（Version）、2 = C（Check）、3 = B（REF、B セットの曲）。
inline const char* roleLetter (int slot) noexcept
{
    return slot == 1 ? "V" : slot == 2 ? "C" : slot == 3 ? "B" : "A";
}

struct SelectionOption
{
    juce::String id;
    juce::String label;
};

// B の曲の Kirin OS の値（既定の Cue）。B の一覧と Balance に出す。
struct SongFact
{
    double lufsI = std::numeric_limits<double>::quiet_NaN();
    bool prepared = false;
    reference_audition::RuntimeSongPreparation preparation; // Kirin OS がこの曲を準備している状態
    std::vector<double> centersHz;
    std::vector<float> medianDb;
    // 既定の Cue が曲のどの部分か（凡例「Bサビ 1:02-1:24」。Kirin OS は Cue を決めていない曲にサビ候補を渡す）。
    reference_audition::CuePart part = reference_audition::CuePart::unknown;
    double partStartSeconds = std::numeric_limits<double>::quiet_NaN(), partEndSeconds = std::numeric_limits<double>::quiet_NaN();
};

// C の Cue の値がまだ無いとき：曲全体の値で比べている（wholeSong）／曲全体のスペクトルの定義が A と違うので出さない
// （noSpectrum。ReferenceWholeSongSpectrum.h）。
enum class CueSubstitute { none, wholeSong, noSpectrum };

struct State
{
    bool osOnline = false, libraryReceived = false, blindLargeScreen = true;
    bool separateComparisons = false, versionReady = false, checkReady = false;
    // Where B and C each stand, whichever of them the page shows (the guide names both).
    SourceStep versionStep = SourceStep::waitingForKirinOs, checkStep = SourceStep::waitingForKirinOs;
    int comparisonSlot = 2, audibleComparisonSlot = 0;
    bool transportPlaying = false, versionArmable = false, checkArmable = false;
    reference_audition::PendingAuditionView pendingAudition;
    juce::String versionId;
    std::vector<SelectionOption> versions;
    Readiness readiness = Readiness::disconnected;
    juce::String title;
    juce::String sourceLabel;
    juce::String status;
    bool kirinOsRequest = false;  // status は Kirin OS へ頼んだことの途中の文（段階の文より先に言う。HyphaReferenceRuntimeStatus.h）
    juce::String alignmentLabel;
    double aIntegratedLoudness = unavailableValue();
    double aMaximumTruePeakDbtp = unavailableValue();
    double adjustedBIntegratedLoudness = unavailableValue();
    double adjustedBMaximumTruePeakDbtp = unavailableValue();
    double appliedGainDb = unavailableValue();  // 下げる前の A の基準（グラフをそろえる）。読みは displayGainDb を通す
    double heldAttenuationDb = 0.0;              // 承認して A を下げている量（0 以下。2026-10-03、R-12）
    bool aAvailable = false;
    bool gainLimited = false;
    bool comparisonFallbackOriginal = false;
    bool originalAudition = false; // Explicit OS mode, not a failed match.
    bool bSelected = false;
    bool auditionBuffered = false;
    os_access::State osAccess = os_access::State::unowned;
    BlindPhase blindPhase = BlindPhase::unavailable;
    int activeBlindStimulus = 0;
    int pendingBlindStimulus = 0;
    int answeredBlindStimulus = 0;
    bool blindStimulusOneHeard = false;
    bool blindStimulusTwoHeard = false;
    bool blindPaused = false, blindOutsideSong = false;
    bool blindLowerAApprovalRequired = false;
    double blindRequiredAAttenuationDb = 0.0;
    juce::String blindReveal;
    bool blindOneIsComparison = false;  // 開示の後：1 が比べる側（VERSION BLIND の画面の「1: V」）
    juce::String presetId;
    juce::String checkId;
    juce::String candidateId;
    juce::String cueId;
    juce::String presetName;
    juce::String checkLabel;
    juce::String candidateName;
    juce::String cueLabel;
    juce::String comparisonMode;
    juce::String presentationLayout { "auto" };
    std::vector<juce::String> viewBindings;
    std::vector<SelectionOption> presets;
    std::vector<SelectionOption> checks;
    std::vector<SelectionOption> candidates;
    std::vector<SelectionOption> cues;
    std::shared_ptr<const reference_audition::RuntimeDetailedMeasurement> detailedMeasurement;
    // 範囲の帯（HyphaReferenceRangeStrips.h）：C の曲の Kirin OS の詳しい値（Cue で切り出す）。
    std::shared_ptr<const reference_audition::RuntimeDetailedMeasurement> cueMeasurement;
    std::shared_ptr<const reference_audition::VisualTimeline> visualTimeline;
    double visualPositionSeconds = -1.0;
    std::shared_ptr<reference_audition::VisualPreferences> visualPreferences;
    std::vector<std::shared_ptr<const reference_audition::RuntimeProfile>> profiles;
    std::vector<float> liveSpectrumDbfs;
    float liveSpectrumMinimumHz = 0.0f;
    float liveSpectrumMaximumHz = 0.0f;
    std::int64_t sourceSampleRateHz = 0;
    std::int64_t hostSampleRateHz = 0;
    bool candidatePreparationPending = false;
    juce::String actionText;   // ボタンの文。action と一緒に決める
    ActionIntent action;       // 押したときにすること（ほかの印から推し量らない）
    // B（REF）。Hypha に届いた B セット（B SET）と、選んでいるセットの曲。
    bool referenceReady = false, referenceArmable = false;
    reference_audition::TrackingState tracking = reference_audition::TrackingState::none; // 聴いている役の合わせ方
    SourceStep referenceStep = SourceStep::waitingForKirinOs;
    std::vector<SelectionOption> songSets, songs;
    std::vector<SongFact> songFacts; // songs と同じ順
    std::map<juce::String, std::vector<juce::String>> checkViewBindings; // V のタブの Check ごとの表示
    juce::String songSetId, songId, songSetsIssue; // songSetsIssue：Kirin OS のセットを読めなかった理由（空なら無し）
    // 同じ定義・同じ区間・同じ音量で比べる値。A の直近の窓（Kirin OS の Cue と同じ定義）と C の Cue の
    // 値（gain の前）、gain をそろえる基準（A の窓の音量・Cue の Integrated）、C の画面の Cue の時間軸。
    std::shared_ptr<const reference_audition::KirinSpectrumWindow> aKirin, cueKirin;
    reference_audition::CuePart cuePart = reference_audition::CuePart::unknown;  // C の Cue が曲のどの部分か（凡例）
    std::set<juce::String> listeningChecks;  // 耳で聴き比べる Check（図の代わりに聴き比べの案内を出す）
    double peakShortfallDb = 0.0;  // 鳴っている役が上限まで上げても A に届かない量（0.5 dB 以下）
    double aWindowLoudness = std::numeric_limits<double>::quiet_NaN(), cueLoudness = std::numeric_limits<double>::quiet_NaN();
    int aWindowBlocks = 0, aWindowNeededBlocks = 0; // 仕様 C：A の窓に入った点と、C の MATCH に要る点（10 Hz）
    double cueStartSeconds = std::numeric_limits<double>::quiet_NaN(), cueEndSeconds = std::numeric_limits<double>::quiet_NaN();
    double sourceDurationSeconds = std::numeric_limits<double>::quiet_NaN(), cuePlayheadSeconds = std::numeric_limits<double>::quiet_NaN();
    bool cueLoops = false;
    juce::String preparationOverdue; // 待ちが上限を超えたときの「理由 / 直し方」（HyphaReferencePreparationWatch）
    reference_audition::RuntimeSongPreparation rolePreparation; // 見ている役の曲を Kirin OS が準備している状態
    CueSubstitute cueSubstitute = CueSubstitute::none;          // C の Cue の値が無いときの代わり（状態の行が言う）
};

// 画面が読む gain：比べる側に掛かる gain（下げる前の A の基準）に、承認して下げた A の量を足す。読みはすべてここを
// 通し、部品は自分で足さない（足す所と足さない所があると、同じ大きさの曲が +0.0 と +6.0 に分かれた。2026-10-06）。
// 数の書き方は gainText（HyphaReferenceGainText.h）。
inline double displayGainDb (const State& state, double gainDb) noexcept
{
    return gainDb + state.heldAttenuationDb;
}

inline bool canSelectB (const State& state) noexcept
{
    return os_access::featureReady (state.osAccess)
        && state.readiness == Readiness::ready && state.auditionBuffered
        && state.aAvailable;
}

inline bool canStartBlind (const State& state) noexcept
{
    return state.blindPhase == BlindPhase::available
        && ! state.blindLowerAApprovalRequired
        && (state.separateComparisons ? state.osAccess != os_access::State::unowned
            && state.libraryReceived && state.aAvailable && state.versionReady : canSelectB (state));
}

// B and C as their buttons deliver them: Kirin OS has sent the library, the DAW plays A, and the
// source itself is ready.
inline bool canHearVersion (const State& state) noexcept
{
    const bool sources = state.osAccess != os_access::State::unowned
        && state.libraryReceived && state.aAvailable;
    return state.separateComparisons ? sources && state.versionReady : canSelectB (state);
}

inline bool canHearCheck (const State& state) noexcept
{
    return state.separateComparisons && state.osAccess != os_access::State::unowned
        && state.libraryReceived && state.aAvailable && state.checkReady;
}

inline bool canHearReference (const State& state) noexcept
{
    return state.separateComparisons && state.osAccess != os_access::State::unowned
        && state.libraryReceived && state.aAvailable && state.referenceReady;
}

class Component final : public juce::Component
{
public:
    Component();
    ~Component() override { setLookAndFeel (nullptr); }

    void setPresentationContext (presentation::Context next)
    {
        if (presentationContext == next) return;
        presentationContext = next;
        selectorLookAndFeel.setPresentationContext (next);
        // 子の部品を巡って渡す（手で並べると MATCH のボタンだけ漏れ、300% でも 100% の字だった。2026-10-06）。
        // 状態の行は足元の段に移ると REF の子ではないので、それも巡る。
        passPresentationContext (*this, next);
        passPresentationContext (statusStrip, next);
        resized();
        repaint();
    }

    std::function<void()> onSelectA;
    std::function<void()> onSelectB;
    std::function<void()> onSelectC;
    std::function<void()> onSelectRef;                                    // B（REF）
    std::function<void(const juce::String&)> onSelectSong, onSelectSongSet; // B の曲と B SET
    std::function<void(const juce::String&)> onSelectVersion;
    std::function<void(const juce::String&)> onSelectPreset;
    std::function<void(const juce::String&)> onSelectCheck;
    std::function<void(const juce::String&)> onSelectCandidate;
    std::function<void(const juce::String&)> onSelectCue;
    std::function<void(int)> onSelectVisualSlot;
    std::function<void(int)> onOpenLarge; // 300% 未満の C・V を押したとき（広げるだけ、音は変えない）
    std::function<void()> onMatch;        // 鳴っている C の MATCH をもう一度
    std::function<void()> onAction;
    std::function<void()> onStartBlind;  // 始めた後の 1・2・開示・終了は PRE/POST Blind と同じ画面（HyphaVersionBlindScreen.h）
    // A B or C that cannot be heard yet says why when it is clicked.
    std::function<void(const juce::String&)> onExplain;

    void setState (State);
    // 状態の行は、足元の段があるとき（150% 以上）はどの画面でも足元の左に出す（エディターが決めて置く。
    // 2026-10-04）。足元の段が無い 100%・125% は REF の一番下（同じ左下）。
    bool statusInFooter() const noexcept { return statusFooterMode; }
    // 状態の行に押せるボタン（承認・VERSION BLIND）があるか。知らせと重なるときは行を REF の中へ戻す。
    bool statusRowHasControls() const noexcept;
    // Blind の開始・比較中・中断のあいだは状態の行を出さない（Blind の画面が自分の段に出す。状態の文や gain は
    // どちらが鳴っているかの手がかりになる）。REVEAL の後は出す。
    bool statusRowConcealed() const noexcept;
    void setStatusInFooter (bool value) { if (statusFooterMode != value) { statusFooterMode = value; resized(); repaint(); } }
    StatusStrip& footerStatusStrip() noexcept { return statusStrip; }
    // 状態の文の出し方（区切りごとに入るところまで）。足元で切れているときに文を指すと、全文を足元の段に出す。
    struct StatusTextLayout
    {
        StatusLine line;
        juce::Rectangle<int> primary, textArea, gainArea;
        juce::String text, gain;  // text：出す文（訳した後、区切りごとに入るところまで）
        bool cut = false;
    };
    StatusTextLayout statusTextLayout (juce::Rectangle<int> statusArea) const;
    juce::String statusLineHelp (juce::Point<int> stripPoint) const;
    juce::String statusLineWhole() const;  // 状態の文が切れているときの全文（切れていなければ空）
    const State& state() const noexcept { return current; }
    bool detailedLayout() const noexcept;
    int comparisonButtonWidth() const noexcept { return detailedLayout() ? 80 : current.separateComparisons ? 36 : 48; }
    // Whether the last paint showed the guide whole (HyphaReferenceGuide.h); checked by the tests.
    const GuideFit& guideFit() const noexcept { return lastGuideFit; }
    bool shortPanel() const noexcept { return getHeight()<150 && !isBlindSession(current.blindPhase); }
    int panelHeaderHeight() const noexcept { return shortPanel() ? 20 : detailedLayout() ? 42 : 34; }
    int panelPickerHeight() const noexcept { return shortPanel() ? 18 : 24; }
    int panelGap() const noexcept { return shortPanel() ? 2 : 4; }
    juce::Rectangle<int> panelArea() const noexcept { return getLocalBounds().reduced(6,shortPanel() ? 3 : 6); }

    void paint (juce::Graphics&) override;
    void resized() override;

    // 300% の B・C・V で指している項目の説明（英語。HyphaReferenceHelp.h・HyphaReferenceHoverHelp.cpp）。エディターの
    // 説明の行（PluginEditorHelpLine.cpp）が足元に出す。`local` は REF の中の位置（試験も使う）。
    juce::String helpAt (juce::Point<int> local);
    bool helpInLine() const noexcept;

private:
    class SideButton final : public juce::TextButton
    {
    public:
        explicit SideButton (const juce::String& text);
        void setPresentationContext (presentation::Context next)
        {
            presentationContext = next;
            getProperties().set ("presentation_width", next.logicalWidth);  // どのボタンも今の文脈か（試験）
        }
        // Not ready yet: drawn like a disabled button, but a click still reaches onClick to explain.
        void setReady (bool next) { if (ready != next) { ready = next; repaint(); } }
        // 予約（DAW の再生を待つ）の印。部品の性質 "waiting" にも置く（試験・読み上げが文字に頼らない）。
        void setAttention (bool next) { if (attention != next) { attention = next; getProperties().set ("waiting", next); repaint(); } }
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        presentation::Context presentationContext = presentation::defaultContext();
        bool ready = true, attention = false;
    };

    static void passPresentationContext (juce::Component& parent, presentation::Context next)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* button = dynamic_cast<SideButton*> (child)) button->setPresentationContext (next);
            passPresentationContext (*child, next);
        }
    }

    State current;
    bool guideShown = false;
    GuideFit lastGuideFit;
    ComparisonView comparisonView;
    presentation::Context presentationContext = presentation::defaultContext();
    ReferenceSelectorLookAndFeel selectorLookAndFeel;
    juce::Label connectionStatus;
    juce::ComboBox presetBox;
    juce::ComboBox versionBox;
    juce::ComboBox checkBox;
    juce::ComboBox candidateBox;
    juce::ComboBox cueBox;
    std::array<juce::Label, 5> selectionReadouts;
    SideButton aButton { "A" };
    SideButton bButton { "V" };   // V（Version）。ID は既存の契約のため "reference-b" のまま
    SideButton cButton { "C" };
    SideButton refButton { "B" }; // B（REF、B セットの曲）
    juce::ComboBox songSetBox, songBox;
    SongList songList; // B の画面の左の曲の一覧
    CheckTabs checkTabs; // C の画面の Check のタブ（CHECK セットの順）
    juce::ComboBox checkSongBox; // いまの Check の曲
    SideButton matchButton { "MATCH" };
    SideButton blindButton { "VERSION BLIND" };
    SideButton actionButton { "OPEN KIRIN OS" };
    StatusStrip statusStrip;  // 状態の行と、VERSION BLIND・アクションのボタン（HyphaReferenceStatusRow.cpp）
    std::vector<help::Region> helpRegions;  // 最後に描いた図に添えた説明の場所
    bool statusFooterMode = false;
    int statusRowHeight() const noexcept;
    bool statusLineShown() const noexcept;
    void layoutStatusRow (juce::Rectangle<int>);
    void paintStatusRow (juce::Graphics&, juce::Rectangle<int>) const;

    // B（REF）の役のボタンと B SET・曲の選択（HyphaReferenceRoles.cpp）。
    void configureRoles();
    void syncRoles (bool blindSession);
    bool explainReference();
    // C の画面（300%）。CHECK SET・Check のタブ・曲・Cue・MATCH、4 帯域と Cue の時間軸
    // （HyphaReferenceCheckPage.cpp）。
    bool checkPage() const noexcept;
    // 2026-10-04：選択欄は A・B・C・V のボタンの段、MATCH はタブの段、CUE は Cue の時間軸の段に置く（図を大きく）。
    static constexpr int checkPageRows = 28; // タブ（右に A の読みと MATCH）。入りきらなければ 2 段（checkTabsHeight）
    static constexpr int checkFooterRow = 24; // 4 帯域の 1 段・CUE と時間軸の 1 段
    bool rolePage() const noexcept;  // 300% の B・C・V（Blind の外）
    juce::Rectangle<int> cueRowBounds() const noexcept;
    int checkFooterHeight() const noexcept;
    void configureCheckPage();
    void syncCheckPage (bool blindSession);
    int checkTabsHeight() const;  // C は Check のタブが 1 段に入らなければ 2 段ぶん（2026-10-05）
    void layoutCheckPage (juce::Rectangle<int>& area, juce::Rectangle<int> selectors);
    void paintCheckPageLabels (juce::Graphics&) const;
    // 耳で聴き比べる Check（Kirin OS の audition_only）。C の画面は図の代わりに案内を出す。
    bool listeningCheck() const;
    void paintListeningPanel (juce::Graphics&, juce::Rectangle<int>) const;
    juce::Rectangle<int> paintCheckFooter (juce::Graphics&, juce::Rectangle<int> area) const;
    // V の画面（300%）。VERSION と CHECK SET（C と共用）、WHOLE（タイムライン）と Check のタブ。
    bool versionPage() const noexcept;
    static constexpr int versionPageRows = 28;  // タブ（選択欄はボタンの段）
    juce::String versionTab { "whole" };

    bool selectionVisible (const juce::ComboBox&) const;
    // B and C while they cannot be heard: dimmed, with the reason on hover and after a click, and
    // the guide in the comparison's place when neither can (HyphaReferenceGuide.cpp).
    void syncSourceButtons();
    bool explainUnavailable (bool version);
    bool openLarge (int slot);
    void paintSourceHints (juce::Graphics&) const;
    void layoutSelectionReadouts();
    void syncSelectionControl (juce::ComboBox&, const std::vector<SelectionOption>&,
                               const juce::String& selectedId);
    static juce::String selectedOptionId (const juce::ComboBox&,
                                          const std::vector<SelectionOption>&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Component)
};
}
