#pragma once

#include <map>
#include "HyphaReferenceCaptureControls.h"

#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaOsAccess.h"
#include "HyphaPresentationContext.h"
#include "HyphaReferenceGuide.h"
#include "HyphaReferenceSelectorLookAndFeel.h"
#include "HyphaReferenceComparisonView.h"
#include "HyphaReferenceTonalView.h"
#include "HyphaReferenceWorkflowControls.h"
#include "HyphaReferenceSongList.h"
#include "HyphaReferenceCheckTabs.h"
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

// H10: A／B／C／V の役の文字。slot 1 = V（Version）、2 = C（Check）、3 = B（REF、B セットの曲）。
inline const char* roleLetter (int slot) noexcept
{
    return slot == 1 ? "V" : slot == 2 ? "C" : slot == 3 ? "B" : "A";
}

struct SelectionOption
{
    juce::String id;
    juce::String label;
};

// H11: B の曲の Kirin OS の値（既定の Cue）。B の一覧と Balance に出す。
struct SongFact
{
    double lufsI = std::numeric_limits<double>::quiet_NaN();
    bool prepared = false;
    reference_audition::RuntimeSongPreparation preparation; // K13b：Kirin OS がこの曲を準備している状態
    std::vector<double> centersHz;
    std::vector<float> medianDb;
};

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
    juce::String alignmentLabel;
    double aIntegratedLoudness = unavailableValue();
    double aMaximumTruePeakDbtp = unavailableValue();
    double adjustedBIntegratedLoudness = unavailableValue();
    double adjustedBMaximumTruePeakDbtp = unavailableValue();
    double loudnessDeltaBMinusA = unavailableValue();
    double truePeakDeltaBMinusA = unavailableValue();
    double appliedGainDb = unavailableValue();  // 下げる前の A の基準（グラフをそろえる）。読みは heldAttenuationDb を足す
    double heldAttenuationDb = 0.0;              // 承認して A を下げている量（0 以下。2026-10-03、R-12）
    int lowerAOfferSlot = 0;                     // 上限超えで A を下げる承認を出している役（0 は無し）
    double lowerAOfferDb = 0.0;
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
    std::shared_ptr<const reference_audition::VisualTimeline> visualTimeline;
    double visualPositionSeconds = -1.0;
    std::shared_ptr<reference_audition::VisualPreferences> visualPreferences;
    std::shared_ptr<reference_audition::ACaptureAccess> captureAccess;
    std::vector<std::shared_ptr<const reference_audition::RuntimeProfile>> profiles;
    std::vector<float> liveSpectrumDbfs;
    float liveSpectrumMinimumHz = 0.0f;
    float liveSpectrumMaximumHz = 0.0f;
    bool sampleRateApprovalRequired = false;
    int sampleRateApprovalSlot = 0; // 1 = B/Version, 2 = C/Check; never infer from the viewed slot.
    std::int64_t sourceSampleRateHz = 0;
    std::int64_t hostSampleRateHz = 0;
    juce::String presetSelectionAction;
    juce::String candidatePreparationAction;
    bool candidatePreparationPending = false;
    juce::String actionText;
    reference_audition::WorkflowView workflow;
    // H10: B（REF）。Hypha に届いた B セット（B SET）と、選んでいるセットの曲。
    bool referenceReady = false, referenceArmable = false;
    reference_audition::TrackingState tracking = reference_audition::TrackingState::none; // H9: 聴いている役の合わせ方
    SourceStep referenceStep = SourceStep::waitingForKirinOs;
    std::vector<SelectionOption> songSets, songs;
    std::vector<SongFact> songFacts; // H11: songs と同じ順
    std::map<juce::String, std::vector<juce::String>> checkViewBindings; // H13: V のタブの Check ごとの表示
    juce::String songSetId, songId, songSetsIssue; // songSetsIssue：Kirin OS のセットを読めなかった理由（空なら無し）
    // H12: 同じ定義・同じ区間・同じ音量で比べる値。A の直近の窓（Kirin OS の Cue と同じ定義）と C の Cue の
    // 値（gain の前）、gain をそろえる基準（A の窓の音量・Cue の Integrated）、C の画面の Cue の時間軸。
    std::shared_ptr<const reference_audition::KirinSpectrumWindow> aKirin, cueKirin;
    double aWindowLoudness = std::numeric_limits<double>::quiet_NaN(), cueLoudness = std::numeric_limits<double>::quiet_NaN();
    int aWindowBlocks = 0, aWindowNeededBlocks = 0; // 仕様 C：A の窓に入った点と、C の MATCH に要る点（10 Hz）
    double cueStartSeconds = std::numeric_limits<double>::quiet_NaN(), cueEndSeconds = std::numeric_limits<double>::quiet_NaN();
    double sourceDurationSeconds = std::numeric_limits<double>::quiet_NaN(), cuePlayheadSeconds = std::numeric_limits<double>::quiet_NaN();
    bool cueLoops = false;
    juce::String preparationOverdue; // H6: 待ちが上限を超えたときの「理由 / 直し方」（HyphaReferencePreparationWatch）
    reference_audition::RuntimeSongPreparation rolePreparation; // K13b：見ている役の曲を Kirin OS が準備している状態
};

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
        for (auto* button : { &aButton, &bButton, &cButton, &refButton, &blindButton, &oneButton, &twoButton,
                              &revealButton, &endBlindButton, &actionButton, &viewButton })
            button->setPresentationContext (next);
        tonalView.update (current.visualTimeline, presentationContext,
                          isBlindSession (current.blindPhase), current.candidateName, current.cueLabel);
        resized();
        repaint();
    }

    std::function<void()> onSelectA;
    std::function<void()> onSelectB;
    std::function<void()> onSelectC;
    std::function<void()> onSelectRef;                                    // H10: B（REF）
    std::function<void(const juce::String&)> onSelectSong, onSelectSongSet; // H10: B の曲と B SET
    std::function<void(const juce::String&)> onSelectVersion;
    std::function<void(const juce::String&)> onSelectPreset;
    std::function<void(const juce::String&)> onSelectCheck;
    std::function<void(const juce::String&)> onSelectCandidate;
    std::function<void(const juce::String&)> onSelectCue;
    std::function<void(int)> onSelectVisualSlot;
    std::function<void(int)> onOpenLarge; // H10: 300% 未満の C・V を押したとき（広げるだけ、音は変えない）
    std::function<void()> onMatch;        // H12: 鳴っている C の MATCH をもう一度
    std::function<void()> onAction;
    std::function<void()> onStartBlind;
    std::function<void(int)> onSelectBlindStimulus;
    std::function<void()> onRevealBlind;
    std::function<void()> onEndBlind;
    std::function<void()> onStartReview, onStartBookmark, onWorkflowBack;
    std::function<void()> onWorkflowConfirmed, onWorkflowDeferred, onWorkflowEnd;
    std::function<void(double,double)> onCapturedTonalRange;
    // A B or C that cannot be heard yet says why when it is clicked.
    std::function<void(const juce::String&)> onExplain;

    void setState (State);
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

private:
    class SideButton final : public juce::TextButton
    {
    public:
        explicit SideButton (const juce::String& text);
        void setPresentationContext (presentation::Context next) noexcept
        {
            presentationContext = next;
        }
        // Not ready yet: drawn like a disabled button, but a click still reaches onClick to explain.
        void setReady (bool next) { if (ready != next) { ready = next; repaint(); } }
        void setAttention (bool next) { if (attention != next) { attention = next; repaint(); } }
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        presentation::Context presentationContext = presentation::defaultContext();
        bool ready = true, attention = false;
    };

    State current;
    bool guideShown = false;
    GuideFit lastGuideFit;
    ComparisonView comparisonView;
    TonalView tonalView;
    CaptureControls captureControls;
    WorkflowControls workflowControls;
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
    SideButton refButton { "B" }; // H10: B（REF、B セットの曲）
    juce::ComboBox songSetBox, songBox;
    SongList songList; // H11: B の画面の左の曲の一覧
    CheckTabs checkTabs; // H12: C の画面の Check のタブ（CHECK セットの順）
    juce::ComboBox checkSongBox; // H12: いまの Check の曲
    SideButton matchButton { "MATCH" };
    SideButton blindButton { "VERSION BLIND" };
    SideButton oneButton { "1" };
    SideButton twoButton { "2" };
    SideButton revealButton { "REVEAL" };
    SideButton endBlindButton { "END" };
    SideButton actionButton { "OPEN KIRIN OS" };
    SideButton viewButton { "VIEW A/C" };

    void configureVisualNavigation();
    // H10: B（REF）の役のボタンと B SET・曲の選択（HyphaReferenceRoles.cpp）。
    void configureRoles();
    void syncRoles (bool blindSession, bool workflowActive);
    bool explainReference();
    // H12: C の画面（300%）。CHECK SET・Check のタブ・曲・Cue・MATCH、4 帯域と Cue の時間軸
    // （HyphaReferenceCheckPage.cpp）。
    bool checkPage() const noexcept;
    static constexpr int checkPageRows = 40 + 4 + 28 + 4 + 38; // CHECK SET と曲・タブ・Cue と MATCH
    int checkFooterHeight() const noexcept;
    void configureCheckPage();
    void syncCheckPage (bool blindSession, bool workflowActive);
    void layoutCheckPage (juce::Rectangle<int>& area);
    void paintCheckPageLabels (juce::Graphics&) const;
    juce::Rectangle<int> paintCheckFooter (juce::Graphics&, juce::Rectangle<int> area) const;
    // H13: V の画面（300%）。VERSION と CHECK SET（C と共用）、WHOLE（タイムライン）と Check のタブ。
    bool versionPage() const noexcept;
    static constexpr int versionPageRows = 40 + 4 + 28;
    juce::String versionTab { "whole" };
    void updateVisualNavigation (bool enabled);

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
