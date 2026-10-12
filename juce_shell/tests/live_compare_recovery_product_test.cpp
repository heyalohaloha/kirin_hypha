#include "LiveTimingFixtureAccess.h"
#include "EditorProductChecks.h"
#include "ReferenceValidationStorageSandbox.h"
#include "LiveCompareNoticeProductChecks.h"
#include "../src/HyphaLiveCompareRecoveryText.h"
#include "../src/HyphaReferenceAccessPanel.h"

#if JUCE_MAC
void initialiseBlindProductHostApplication();
#endif

namespace hypha::tests::editor_product
{
struct Tick
{
    using Type = void (KirinHyphaEditor::*)();
    friend Type privateMember (Tick);
};
template struct PrivateAccess<Tick, &KirinHyphaEditor::timerCallback>;
struct LayoutReference
{
    using Type = void (KirinHyphaEditor::*)();
    friend Type privateMember (LayoutReference);
};
template struct PrivateAccess<LayoutReference, &KirinHyphaEditor::layoutReferenceAudition>;

struct GeometryCounter final : juce::ComponentListener
{
    int changes = 0;
    void componentMovedOrResized (juce::Component&, bool, bool) override { ++changes; }
};

struct Clock final : juce::AudioPlayHead
{
    bool playing = true;
    std::int64_t position = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing); info.setTimeInSamples (position);
        return info;
    }
};

static void verifyRecoveryGeometry()
{
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
        Processor processor (Processor::Role::Post);
        processor.prepareToPlay (48000, 512);
        std::unique_ptr<KirinHyphaEditor> editor (dynamic_cast<KirinHyphaEditor*> (processor.createEditorIfNeeded()));
        editor->setVisible (true);
        auto* view = component<observatory::View> (*editor);
        auto* reference = component<reference_ui::Component> (*editor);
        auto* access = component<reference_ui::AccessPanel> (*editor);
        auto* strip = component<FeedbackStrip> (*editor);
        require (view && reference && access && strip, "shipping recovery surfaces exist");
        for (const auto preset : observatory::sizePresets)
        for (auto domain : { observatory::Domain::reference, observatory::Domain::frequency,
                             observatory::Domain::time, observatory::Domain::space, observatory::Domain::level })
        {
            editor->setSize (preset.width, preset.height);
            view->onDomainChange (domain);
            (editor.get()->*privateMember (ShowToast {})) ({});
            const auto full = view->analysisBodyBounds();
            (editor.get()->*privateMember (ShowToast {})) ("Timing changed: stop/play DAW (POST)");
            require (reference->getBounds() == view->analysisBodyBounds()
                && access->getBounds() == view->analysisBodyBounds(), "both Reference bodies follow feedback immediately");
            if (domain == observatory::Domain::reference)
            {
                require (! reference->getBounds().intersects (view->statusStripBounds()), "Reference never overlaps feedback");
                for (const bool owned : { false, true })
                {
                    auto state = reference->state();
                    state.osAccess = owned ? os_access::State::ownedDisconnected : os_access::State::unowned;
                    reference->setState (state);
                    (editor.get()->*privateMember (LayoutReference {}))();
                    require (reference->isVisible() == owned && access->isVisible() != owned,
                             "both real Reference surfaces exercised");
                    GeometryCounter counter;
                    reference->addComponentListener (&counter); access->addComponentListener (&counter);
                    strip->addComponentListener (&counter);
                    for (int tick = 0; tick < 100; ++tick)
                        (editor.get()->*privateMember (LayoutReference {}))();
                    require (counter.changes == 0, "unchanged presentation performs zero geometry changes");
                    reference->removeComponentListener (&counter); access->removeComponentListener (&counter);
                    strip->removeComponentListener (&counter);
                    if (strip->isVisible())
                    {
                        const auto point = editor->getLocalPoint (strip, strip->getLocalBounds().getCentre());
                        require (editor->getComponentAt (point) == strip, "feedback remains above real REF/access after refresh");
                    }
                }
                writeReview (*editor, "recovery-ref-" + juce::String (static_cast<int> (language))
                                          + "-" + juce::String (preset.width));
            }
            (editor.get()->*privateMember (Tick {}))();
            require (reference->getBounds() == view->analysisBodyBounds(), "real editor tick never restores stale bounds");
            (editor.get()->*privateMember (ShowToast {})) ({});
            require (view->analysisBodyBounds() == full, "clearing feedback restores the body without resizing the editor");
        }
        processor.editorBeingDeleted (editor.get()); editor.reset(); processor.releaseResources();
    }
}

static void verifyHoldAfterEnd()
{
    Processor processor (Processor::Role::Post);
    processor.prepareToPlay (48000, 512);
    Clock clock;
    processor.setPlayHead (&clock);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    const auto process = [&]
    { buffer.clear(); processor.processBlock (buffer, midi); clock.position += 512; processor.serviceLiveCompare(); };
    LiveTimingFixtureAccess::holdContent (processor, -3000);
    processor.finishLiveCompare(); process();
    using namespace live_compare_ui;
    require (processor.isPlaying() && ! processor.liveCompareStatus().finishing, "END acknowledged during playback");
    require (processor.liveCompareStatus().contentHeld
        && namedPresentation (processor.liveCompareStatus()).action == RecoveryAction::stopPlay,
        "END leaves the current hold and actionable stop/play instruction");
    clock.playing = false; process();
    require (! processor.liveCompareStatus().contentHeld, "actual stopped callback clears content hold");
    clock.playing = true; process();
    require (processor.liveBlindStatus().stage == live_compare::BlindStage::idle,
             "playback restart never resurrects the invalidated trial");
    require (processor.getLatencySamples() == 0 && buffer.getMagnitude (0, 512) == 0.0f,
             "recovery does not generate sound or latency");
    processor.releaseResources();
}

// Footer-only snapshots omit the feedback strip owned by the real editor. Exercise that
// composition too: the held/LOOP remedy must actually paint, not merely fit an unused rectangle.
static void verifyLoopPresentation()
{
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        Processor processor (Processor::Role::Post);
        processor.prepareToPlay (48000, 512);
        std::unique_ptr<KirinHyphaEditor> editor (dynamic_cast<KirinHyphaEditor*> (processor.createEditorIfNeeded()));
        editor->setVisible (true);
        auto* view = component<observatory::View> (*editor);
        auto* strip = component<FeedbackStrip> (*editor);
        require (view && strip, "real editor owns the recovery strip");
        for (const auto preset : observatory::sizePresets)
        for (int scenario = 0; scenario < 5; ++scenario)
        {
            editor->setSize (preset.width, preset.height);
            live_compare::Status status;
            status.active = true;
            status.matchHeld = scenario >= 2;
            status.matched = scenario == 1;
            status.preSelected = status.preWaiting = scenario < 2 || scenario == 3;
            status.timingReentryPending = scenario == 3;
            status.interrupted = scenario == 4;
            if (status.interrupted) status.reason = live_compare::RecoveryReason::callbackGap;
            status.observation = scenario == 0 ? live_compare::RecoveryReason::loopUnproven
                : scenario == 1 ? live_compare::RecoveryReason::loopWaiting
                : scenario >= 3 ? live_compare::RecoveryReason::loopUnproven : live_compare::RecoveryReason::none;
            observatory::LiveCompareFooter footer;
            footer.active = footer.entryEnabled = true;
            footer.preSelected = status.preSelected; footer.preWaiting = status.preWaiting;
            footer.matched = status.matched || status.matchHeld; footer.matchHeld = status.matchHeld;
            view->setLiveCompareFooter (footer);
            (editor.get()->*privateMember (ShowToast {})) (live_compare_ui::namedRecovery (status));
            require (strip->isVisible() == view->statusStripFolded() && view->feedback().isNotEmpty(),
                     "LOOP/HELD remedy uses the visible strip or footer, according to its full width");
            require (strip->isVisible() || view->feedbackDetailsAnchor().isVisible(), "recovery text is not hidden");
            if (strip->isVisible())
            {
                const auto point = editor->getLocalPoint (strip, strip->getLocalBounds().getCentre());
                require (editor->getComponentAt (point) == strip, "LOOP/HELD remedy is not behind the analysis body");
            }
            writeReview (*editor, "loop-remedy-" + juce::String (static_cast<int> (language))
                + "-" + juce::String (scenario) + "-" + juce::String (preset.width));
        }
        processor.editorBeingDeleted (editor.get()); editor.reset(); processor.releaseResources();
    }
}

// A host that reports neither a continuous clock nor its output latency: LISTEN starts, POST plays,
// PRE waits, and the footer names the host instead of a stop/play that would not help. Once the same
// instance has seen a clock, a missing clock keeps its transient remedy.
static void verifyHostWithoutOutputLatency (const juce::File& sandbox)
{
    const auto storage = sandbox.getChildFile ("host-without-clock");
    require (storage.createDirectory(), "isolated preference storage for this host");
    ScopedChainTimingStorage preferences (storage);
    using namespace live_compare_ui;
    NoticeRig r;
    r.clock.continuous = r.clock.latency = false;
    r.advance (0.3); r.selectPair();
    require (r.post->startLiveCompare() == live_compare::StartResult::started, "LISTEN starts on a host without a clock");
    r.expectOrdinaryAudio = false;
    r.post->selectLiveComparePre (true); r.advance (0.6);
    const auto waiting = r.post->liveCompareStatus();
    require (waiting.active && waiting.preSelected && waiting.preWaiting && ! waiting.preAudible
        && waiting.observation == Reason::clockMissing && waiting.clockUnavailable,
        "PRE waits for a clock this host never supplies");
    const auto notice = currentNamedPresentation (waiting);
    require (notice.action == RecoveryAction::noOutputLatency && r.view().feedback() == notice.instruction
        && r.editor->statusStory().joinIntoString (" ").contains ("PRE timing cannot be verified"),
        "the footer and the story name the host, not a restart");
    r.clock.playing = false; r.advance (0.2); r.clock.playing = true; r.advance (0.4);
    require (currentNamedPresentation (r.post->liveCompareStatus()).action == RecoveryAction::noOutputLatency,
             "stop/play on this host does not change the explanation");
    LiveTimingFixtureAccess::blindWaiting (*r.post, live_compare::BlindStage::preparing);
    const auto blind = r.post->liveBlindStatus();
    require (blind.clockUnavailable && blindRecovery (blind, false).action == RecoveryAction::noOutputLatency,
             "Blind preparation on this host explains the same boundary");
    LiveTimingFixtureAccess::blindWaiting (*r.post, live_compare::BlindStage::idle);
    r.post->finishLiveCompare(); r.advance (0.3);
    require (! r.post->liveCompareStatus().active, "END still closes the session");
    // The same instance later sees a continuous clock: from then on a missing clock is transient.
    // A VST3 host looping a selection meanwhile never asks for loop playback off: that guidance
    // belongs to an uncertified AAX host alone.
    r.clock.continuous = true; r.advance (0.1);
    r.clock.continuous = false; r.clock.loopRange = true; r.advance (0.1);
    require (r.post->startLiveCompare() == live_compare::StartResult::started, "LISTEN starts again");
    r.post->selectLiveComparePre (true); r.advance (0.4);
    const auto transient = r.post->liveCompareStatus();
    const auto remedy = currentNamedPresentation (transient);
    require (transient.active && ! transient.clockUnavailable && transient.loopBlock == hypha::live_compare::LoopPairingBlock::none
        && remedy.action != RecoveryAction::noOutputLatency && remedy.action != RecoveryAction::loopOff
        && juce::String (remedy.instruction).startsWith ("Clock missing"),
        "a host that has shown a clock keeps the transient clock remedy; VST3 loops ask for no loop change");
    r.post->finishLiveCompare(); r.advance (0.3);
}
}

int main()
{
    ReferenceValidationStorageSandbox sandbox;
    // macOS JUCE resolves the home without HOME; keep PRE display files out of the real Kirin OS.
    hypha::pre_display::Controller::placeUnderForTest (sandbox.directory());
   #if JUCE_MAC
    initialiseBlindProductHostApplication();
   #endif
    juce::ScopedJuceInitialiser_GUI init;
    hypha::i18n::holdLanguage (true);
    hypha::tests::editor_product::verifyRecoveryGeometry();
    hypha::tests::editor_product::verifyHoldAfterEnd();
    hypha::tests::editor_product::verifyLoopPresentation();
    hypha::tests::editor_product::verifyNoticeLifecycle (sandbox.directory());
    hypha::tests::editor_product::verifyHostWithoutOutputLatency (sandbox.directory());
    std::cout << "Recovery product: PASS (geometry, current recovery, actual pair/restore/LISTEN/END, bounded notices, host without a clock)\n";
}
