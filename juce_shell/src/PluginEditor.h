#pragma once
#include "HyphaLocalBlindReturnIntent.h"

#include <memory>
#include <array>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "HyphaOutputOwnershipText.h"
#include "HyphaAnalysisNavigation.h"
#include "HyphaEditorResizeGrip.h"
#include "HyphaEditorSizeConstrainer.h"
#include "HyphaFeedbackStrip.h"
#include "HyphaHelpLineBar.h"
#include "HyphaHoverHelpPreference.h"
#include "HyphaObservatoryView.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextButton.h"
#include "HyphaTextLookAndFeel.h"
#include "HyphaTheme.h"
#include "HyphaTimePageNavigation.h"
#include "HyphaTooltipLookAndFeel.h"
#include "HyphaUiPreferences.h"
#include "HyphaWidgets.h"
#include "appearance/AppearanceService.h"
#if ! KIRIN_HYPHA_PRE_DISPLAY
 #include "HyphaSpectrumComponent.h"
 #include "HyphaPerceptualComponent.h"
 #include "HyphaAbsoluteComponent.h"
 #include "HyphaAttackComponent.h"
 #include "HyphaReferenceComponent.h"
#include "HyphaVersionBlindScreen.h"
#include "HyphaReferenceTrackingNotice.h"
 #include "HyphaReferencePreparationWatch.h"
 #include "HyphaReferenceAccessPanel.h"
 #include "HyphaLocalBlindComponent.h"
 #include "HyphaLiveBlindComponent.h"
#endif

// B-054: full UI rebuild to egui parity (crates/hypha_pre/editor.rs + hypha_post/editor.rs +
// hypha_gui). 300×200 mycelium-textured panel. No measurement logic lives here (R-12 / R-22):
// the editor only formats values the Rust engine produces. palette.rs is the colour source of
// truth (no new colours hardcoded). No red / pure white (#ffffff) / neon (品位原則 / G-72-10).
//
// PRE/POST share the Observatory. The editor refreshes producer snapshots and control state; all
// visible measurement rendering is owned by the Observatory and its analysis bodies.
class KirinHyphaEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit KirinHyphaEditor (KirinHyphaProcessorBase&);
    ~KirinHyphaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    // 300% 以上は、指している項目の説明を足元に出す（PluginEditorHelpLine.cpp）。`point` はエディターの座標。
    // wholeRow：足元の段の全幅（図・値・タブ）か、左の状態の所だけ（足元のボタン）か。出さないときは空。
    struct HelpLine { juce::String text; bool wholeRow = false; };
    HelpLine helpLineAt (juce::Point<int> point);
    void mouseMove (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    bool helpLineActive() const;
    void updateHelpLine();
    class PairMenuLookAndFeel final : public hypha::TextLookAndFeel
    {
    public:
        PairMenuLookAndFeel()
        {
            setColour (juce::PopupMenu::backgroundColourId, hypha::BG);
            setColour (juce::PopupMenu::textColourId, hypha::COL_NORMAL);
            setColour (juce::PopupMenu::headerTextColourId, hypha::COL_FLORA);
            setColour (juce::PopupMenu::highlightedBackgroundColourId,
                       hypha::kFieldFill.brighter (0.08f));
            setColour (juce::PopupMenu::highlightedTextColourId, hypha::COL_FLORA_BR);
        }
        juce::Font getPopupMenuFont() override
        {
            return hypha::nativeTextFont (hypha::presentation::forOutput (
                450, 300, hypha::presentation::OutputTarget::popup),
                hypha::typography::TextRole::menu);
        }
        void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
        {
            hypha::TextLookAndFeel::drawPopupMenuBackground (g, width, height);
        }
        // Menus are built in English and shown in the current language by TextLookAndFeel,
        // in the native menu font above, which carries Japanese (INV-S40).
    };

    void timerCallback() override;
    // The language every hosted editor shows follows LanguagePreference; a change lays the whole
    // editor out again, since text widths change (PluginEditorLanguage.cpp).
    void syncLanguage (bool layOutOnChange = true);
    void applyLanguage();
    void addLanguageMenu (juce::PopupMenu&) const;
    bool handleLanguageMenu (int result);
    unsigned int appliedLanguageRevision = 0;
    void updatePre();
    void updatePost();
    void refreshObservatory();
    void applyPresentationContext();
    // Beyond 300% the Observatory is the Inspection View magnified; its children lay out at the
    // logical 900 x 600, so they take their presentation from the viewport, not the window.
    hypha::presentation::Context logicalPresentationContext() const;
    // The size rule for the display the editor is on: free 3:2 up to 300%, then the magnified steps
    // on whole device pixels that fit the display (HyphaEditorSizeConstrainer.h).
    void updateResizeLimits();
    hypha::EditorSizeConstrainer sizeConstrainer;
    // The corner a user drags where the host has no window frame for it (HyphaEditorResizeGrip.h);
    // shown whenever the editor may be resized.
    hypha::EditorResizeGrip resizeGrip { this, &sizeConstrainer };
    hypha::HelpLineBar helpLineBar; // the help line over the footer at 300% and above
    void configureMeterContext();
    void showNoteDialog();
    void setObservatoryDomain (hypha::observatory::Domain domain);
    void beginObservatoryCapture();
    void chooseObservatoryCapture (int width, int height);
#if KIRIN_HYPHA_GUIDE_TRANSPORT
    void attachObservatoryCapture (
        int width, int height, const hypha::pre_display::WorkReference& expectedWork);
#endif
    hypha::capture::DisplayMetadata availableCaptureMetadata() const;
    hypha::capture::Snapshot freezeObservatoryCapture (int width, int height);
#if ! KIRIN_HYPHA_PRE_DISPLAY
    using AnalysisPage = hypha::analysis_navigation::Page;
    void setAnalysisPage (AnalysisPage page);
    void configureSharpnessAnalysis (int pairStatus);
    void configureSpectrumAnalysis();
    hypha::analysis::Demand desiredAnalysisDemand() const noexcept;
    bool analysisSurfaceShowing() const noexcept;
    bool externalAnalysisBodyShowing() const noexcept;
    void updateAnalysisBodyPresentation();
    void syncAnalysisDemand();
    void configureSpectrumCallbacks();
    void updateTimePageNavigation();
    void cycleSpectrumSize();
    void updateSpectrumSizeControl();
    void configureReferenceAudition();
    void showReferenceInformationMenu();
    void layoutReferenceAudition();
    void refreshReferenceAudition (const KirinObservatoryFrame&, bool frameAvailable);
    void wireReferenceRoles();  // B（REF）の押し方と B の曲・B SET（PluginEditorReferenceRoles.cpp）
    void applyReferenceRoles (hypha::reference_ui::State&, const hypha::reference_audition::Snapshot&);
    void openReferenceLarge (int slot);
    // 2026-10-03（R-12）：上限超えの MATCH を、承認して A を下げて合わせる（PluginEditorReferenceLowerA.cpp）。
    double referenceLowerAApprovedDb = 0.0; // 承認した量（鳴らす時点で深く下げ直したら一度だけ知らせる）
    bool offerReferenceLowerA (int slot, const hypha::reference_audition::Snapshot& role);
    void approveOfferedLowerA (const hypha::reference_audition::LowerAOffer&);
    void applyReferenceLowerA (hypha::reference_ui::State&, const hypha::reference_audition::Snapshot&);
    bool returnReferenceLevelIfHeld();
    int referenceHeldTenthsDb() const;
    hypha::reference_ui::PreparationWatch referencePreparationWatch;
    hypha::reference_audition::VersionIdentity referenceVersionIdentity; double referenceIdentifyAtMs = 0.0; hypha::reference_audition::AutoVersionChooser referenceAutoChooser;
    void configureLocalBlindProduct();
    void openLocalBlindProduct();
    void beginLocalBlindProductCapture();
    void closeLocalBlindProduct();
    void refreshLocalBlindProduct();
    void layoutLocalBlindProduct();
    void setLocalBlindIsolation (bool active);
    void configureLiveBlind();
    void addLiveCompareMenu (juce::PopupMenu&, bool keepActive);
    bool handleLiveCompareMenu (int result);
    void openLiveBlind();
    void refreshLiveBlind();
    void configureLiveCompare();
    void refreshLiveCompare();
    void chooseLiveCompareMatch (const hypha::live_compare::MatchPlan&);
    void applyLiveCompareChoice (const hypha::live_compare::MatchPlan&, hypha::live_compare::MatchChoice);
    void pinLiveCompareForBlind();
    void monitorLiveCompareOffset (const hypha::live_compare::Status&, double now);
    void matchLiveCompare();
    void chooseLiveCompareFollow();
    void followLiveCompare (const hypha::live_compare::Status&, double now);
    void stopLiveCompareAuto (const juce::String& notice);
    bool refreshAnalysisViews (bool alive, int signalState, bool recording,
                               bool armed, bool acknowledged, bool presetAvailable,
                               int pairStatus);
#endif

    static constexpr int jungleModeMenuAction = 13;
    static constexpr int chainFooterMenuAction = 690; // information menu, POST only
    juce::String chainFooterText; // the footer chain readout while it is turned on; POST only
    void refreshWatchSnapshot();
    uint8_t refreshRecordPhase();
    void showCandidateMenu();
    void refreshPairPreview (bool demand);
    void selectPairPreview();
    hypha::pair_preview::Ticket pairPreview;
    KirinPairPreviewValue pairPreviewShown {};
    bool pairPreviewWasFocused = false;
    double pairPreviewRefreshAt = 0;
    double pairPreviewNextDemandAt = 0;
    double pairPreviewRetrySeconds = 1.05;
    std::uint64_t pairPreviewObservedGeneration = 0;
    void showOperationsMenu();
    void showTimeRangeMenu();
    void showMeterContextMenu (juce::Component& anchor);
    void applyMeterContextChoice (hypha::meter_context::MeterContext);
    void showDomainMenu();
    void showSizeMenu();
    void showGuideInformationMenu();
    void showFeedbackInformationMenu();
    void handleOperationsMenu (int result);
    void showInformationMenu();
    void handleInformationMenu (int result);
    bool informationBlockedByBlind() const;
    void handleCandidateMenu (int result,
                              const juce::Array<KirinHyphaProcessorBase::PreCandidate>& candidates);
    static PairMenuLookAndFeel& pairMenuLookAndFeel();
    void showToast (const juce::String& msg);
    // 押した操作を、出力の持ち主の表（processor と同じ答え）で先に確かめる。断るなら理由を言って true（R-28）。
    bool outputRefused (hypha::output_owner::Activity activity)
    {
        const auto decision = processorRef.outputDecision (activity);
        if (decision.refused()) showToast (hypha::output_owner::refusalText (decision.reason));
        return decision.refused();
    }
#if ! KIRIN_HYPHA_PRE_DISPLAY
    juce::String liveCompareWarningText() const { return liveCompareWarning; }
#else
    juce::String liveCompareWarningText() const { return {}; }
#endif
    void updateFeedback (double now, bool keeping, const juce::String& persistentError);
    void layoutBodyAndFeedback();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    void placeReferenceStatus();  // 300% の B・C・V の状態の行を足元の段へ
   #endif
    juce::String instanceId8() const; // first 8 chars of instance_id (empty-name fallback)
    double nowSecs() const { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
    void commitEditorSizeStateIfSettled (bool force);
    void refreshAppearance();
    void requestJungleChoice (bool enabled);
    void releaseAppearanceVisibility();

    KirinHyphaProcessorBase& processorRef;
    // Static surface material is cached as images while this editor is open (HyphaMaterialCache.h).
    hypha::material_cache::Lifetime materialCache;
    // JUCE labels, buttons and the NOTE dialog in the current language; declared before every
    // component that uses it, so it outlives them.
    hypha::TextLookAndFeel textLookAndFeel;
    const bool isPost;
    juce::Component scaleRoot;
    hypha::observatory::View observatoryView;

    hypha::MyceliumBackground bg;
    hypha::StatusLed          led;
    hypha::EditableName       nameField;                  // PRE name / POST exact-pair selector
    hypha::FeedbackStrip      feedbackStrip;              // compact sizes: toast > persistent error > Keeping
    std::unique_ptr<juce::FileChooser> captureChooser;
    std::unique_ptr<juce::AlertWindow> noteDialog;
    hypha::capture::PrivacyOptions capturePrivacy;         // editor-lifetime, private by default
    hypha::PairDropdownButton pairDropdown;                // POST: vector arrow / candidate / All Keep / All Stop
#if ! KIRIN_HYPHA_PRE_DISPLAY
    hypha::HyphaTextButton    spectrumToggle { "" };        // POST: meters / Analysis page
    hypha::TimePageNavigation timePageNavigation;            // Compact cycle / Observatory tabs
    hypha::HyphaTextButton    spectrumSizeToggle { "" };    // POST Analysis: 100/125/150/200/300 percent
    hypha::SpectrumComponent  spectrumView;                 // POST-only signed difference plot
    hypha::PerceptualComponent perceptualView;               // paired POST-minus-PRE Sharpness
    hypha::AbsoluteComponent absoluteView;                    // POST-only absolute observation timeline
    hypha::AttackComponent attackView;         // POST ATTACK product view
    hypha::reference_ui::Component referenceView; // POST-only Kirin OS prepared A/B
    hypha::reference_ui::AccessPanel referenceAccessView;
    hypha::local_blind_ui::Component localBlindView;
    hypha::live_blind_ui::Component liveBlindView;
    hypha::blind_ui::ScreenComponent versionBlindView { "version-blind" };  // REF の VERSION BLIND（LIVE BLIND と同じ画面）
    hypha::reference_ui::VersionBlindNotice versionBlindNotice;  // VERSION BLIND の操作の失敗（Blind の画面が言う）
    juce::uint32 versionBlindNoticeUntil = 0;
    bool liveBlindOpen = false;
    bool liveCompareFinishingSeen = false;
#endif
    hypha::TooltipLookAndFeel tooltipLookAndFeel;
    hypha::HoverHelpTooltipWindow tooltip { this, 550 };    // user-level, bounded hover help

    hypha::observatory::Domain observatoryDomain = hypha::observatory::Domain::level;
    size_t observatorySizeIndex = 0;
#if ! KIRIN_HYPHA_PRE_DISPLAY
    AnalysisPage analysisPage = AnalysisPage::meters;
    bool sharpnessUsesAbsolute = false;
    KirinAttackEventBatch cachedAttackEvents {};
    KirinAttackWaveformBatch cachedAttackWaveform {};
    KirinAttackDetailBatch cachedAttackDetails {};
    KirinAttackWaveformBatch cachedAttackPreWaveform {};
    KirinAttackDetailBatch cachedAttackPreDetails {};
    KirinAttackPairEventBatch cachedAttackPairEvents {};
    KirinAttackStats cachedAttackStats {};
    KirinAttackBandBatch cachedAttackBand {};
    KirinAttackBandSummary cachedAttackBandSummary {};
    std::int64_t cachedAttackLatest = -1;
    std::uint32_t cachedAttackRate = 0;
    std::uint64_t cachedAttackGeneration = 0;
    juce::Point<int> localBlindReturnSize;
    bool localBlindOpen = false;
    double localBlindPresentationAt = -1.0;
    hypha::local_blind_ui::ReturnIntent localBlindReturnIntent;
    bool localBlindPreflight = false;
    struct LocalBlindUnderlyingState
    {
        juce::Component::SafePointer<juce::Component> component;
        bool accessible = true;
        bool enabled = true;
        bool restoreEnabled = false;
    };
    std::vector<LocalBlindUnderlyingState> localBlindUnderlyingStates;
    bool liveCompareMatched = false;
    bool liveCompareLimited = false;
    bool liveCompareInterruptSeen = false;
    bool liveCompareActiveSeen = false;
    // INV-LC7 / LC10: the content offset, measured every two seconds of proven playback.
    struct LiveCompareOffsetWatch
    {
        std::uint32_t run = 0;
        double nextAt = 0.0, warningUntil = 0.0, rate = 0.0;
        std::int64_t lag = 0;
        hypha::live_compare::OffsetMonitor monitor;
    };
    LiveCompareOffsetWatch liveCompareOffset;
    // INV-LC16: AUTO and the point the last explicit MATCH approved (PRE gain, true-peak ceiling).
    struct LiveCompareAuto
    {
        bool on = false;
        double approvedPreDb = 0.0, ceilingDbtp = 0.0, nextAt = 0.0;
    };
    LiveCompareAuto liveCompareAuto;
    hypha::reference_ui::TrackingStopNotice referenceTrackingStop;  // 追従が止まった知らせ（同じ役の同じ試みで一度）
    juce::String referenceSetsIssueShown;    // Kirin OS のセットの一部を読めなかったことを一度だけ知らせる
    juce::String liveCompareWarning;
    double liveComparePreWaitUntil = 0.0;
#endif
    int    floraY      = 0;       // y of the flora separator line
    juce::Rectangle<int> titleArea;

    bool   prevAck     = false;
    double bannerUntil = 0.0;
    double toastUntil  = 0.0;
    double editorSizeLastChangedAt = 0.0;
    bool editorSizePersistenceReady = false;
    bool editorSizeStateDirty = false;
    juce::String toastText;
    double pathAnomalyUntil = 0.0;        // B-128 (G-115-371 D3): restore identity anomaly latch
    juce::String pathAnomalyText;         //   drained 文言を fade まで保持
    KirinWatchDisplay observatoryWatchDisplay {};
    bool haveObservatoryWatchDisplay = false;
    std::array<KirinChainPoint, KIRIN_CHAIN_CAPACITY> chainPoints {};
    std::uint64_t comparisonObservedGeneration = 0;
    std::uint64_t comparisonActionAfterGeneration = 0;
    bool comparisonActionAwaitingResult = false;
    std::uint64_t analysisOwnerToken = 0;
    KirinRecordDisplay cachedRecordDisplay {};
    bool haveRecordDisplay = false;
    std::uint64_t observedHostProcessHeartbeat = 0;
    double observedHostProcessHeartbeatAt = 0.0;
    hypha::appearance::Snapshot appearanceSnapshot;
    bool appearanceVisibleRegistered = false;
    bool appearanceApplyPending = false;
    std::uint64_t appearanceActionAwaited = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KirinHyphaEditor)
};
