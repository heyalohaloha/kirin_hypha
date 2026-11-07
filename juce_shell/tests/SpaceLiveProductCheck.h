#pragma once
#include "EditorProductChecks.h"
#include "SpaceLiveContract.h"
#include <cstring>
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

#if JUCE_MAC
namespace hypha::tests
{
void withNativeWindowHost (const std::function<void()>&);
bool setNativeWindowMinimised (void*, bool);
bool setNativeViewHidden (void*, bool);
bool setNativeWindowHidden (void*, bool);
}
#endif

namespace hypha::tests::editor_product
{
struct SpacePageMember
{
    using Type = void (KirinHyphaEditor::*) (observatory::Domain);
    friend Type privateMember (SpacePageMember);
};
template struct PrivateAccess<SpacePageMember, &KirinHyphaEditor::setObservatoryDomain>;
struct SpaceOpenBlind
{
    using Type = void (KirinHyphaEditor::*)();
    friend Type privateMember (SpaceOpenBlind);
};
template struct PrivateAccess<SpaceOpenBlind, &KirinHyphaEditor::openLocalBlindProduct>;
struct SpaceCloseBlind
{
    using Type = void (KirinHyphaEditor::*)();
    friend Type privateMember (SpaceCloseBlind);
};
template struct PrivateAccess<SpaceCloseBlind, &KirinHyphaEditor::closeLocalBlindProduct>;
struct SpaceEditorTick
{
    using Type = void (KirinHyphaEditor::*)();
    friend Type privateMember (SpaceEditorTick);
};
template struct PrivateAccess<SpaceEditorTick, &KirinHyphaEditor::timerCallback>;
struct SpaceClock final : juce::AudioPlayHead
{
    bool playing = true;
    std::int64_t position = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info; info.setIsPlaying (playing); info.setTimeInSamples (position); return info;
    }
};

template <typename Float>
inline void verifySpaceInput (Processor& processor, observatory::View& view, int channels)
{
    SpaceClock clock; processor.setPlayHead (&clock);
    juce::AudioBuffer<Float> audio (channels, 64); juce::MidiBuffer midi;
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < 64; ++i) audio.setSample (c, i, Float (0.1 + i * 0.001 + c * 0.013));
    juce::AudioBuffer<Float> original; original.makeCopyOf (audio);
    processor.processBlock (audio, midi);
    for (int c = 0; c < channels; ++c)
        require (std::memcmp (audio.getReadPointer (c), original.getReadPointer (c), 64 * sizeof (Float)) == 0,
                 "visible SPACE leaves every A bit intact in both precisions");
    auto& screen = space_live_contract::display (view);
    screen.refresh (0);
    require (screen.frame().samples.size() == (channels == 2 ? 64u : 0u),
             "actual processBlock reaches the screen after 64 samples, without a meter publication");
    if (channels == 2)
    {
        require (space_live_contract::sameSample (screen.frame().samples.back().left, float (original.getSample (0, 63))),
                 "screen reads original A through the production input transaction");
        const auto sampled = screen.frame().callbackNanos, revision = screen.frame().revision;
        clock.position += 64; processor.processBlock (audio, midi);
        space_live::Snapshot publication;
        require (sampled != 0 && processor.spaceLive().read (publication, 0) == space_live::Read::ready
                 && publication.callbackNanos > sampled, "real callback publishes after the UI clock sample");
        require (! screen.refresh (sampled) && screen.frame().revision == revision && screen.frame().samples.size() == 64,
                 "real processor-to-display clock race preserves the accepted trace");
        require (screen.refresh (publication.callbackNanos) && screen.frame().revision == publication.revision
                 && screen.frame().samples.size() == publication.samples.size(),
                 "real processor-to-display clock race recovers on the next frame");
        for (int c = 0; c < 2; ++c)
            require (std::memcmp (audio.getReadPointer (c), original.getReadPointer (c), 64 * sizeof (Float)) == 0,
                     "clock-race callback preserves all original A bits");
    }
    const auto fresh = screen.frame().span;
    clock.position = 1024; processor.processBlock (audio, midi); screen.refresh (0);
    if (channels == 2) require (screen.frame().span != fresh && screen.frame().samples.size() == 64,
                                "product seek cannot bridge the previous trace");
    const auto beforeStop = screen.frame().span;
    clock.playing = false; processor.processBlock (audio, midi); screen.refresh (0);
    std::cout << "SPACE stop wrapper=" << int (processor.wrapperType) << " role=" << processor.getName()
              << " channels=" << channels << " precision=" << sizeof (Float) * 8
              << " samples=" << screen.frame().samples.size() << " span=" << beforeStop << "->" << screen.frame().span << '\n';
    require (screen.frame().samples.empty() || (channels == 2 && screen.frame().samples.size() == 64
             && screen.frame().span != beforeStop),
             "stop either empties a non-admitted input or shows only freshly monitored A in a new span");
    screen.refresh (live_compare::callbackWallNanos() + 1'000'000'000ull);
    require (screen.frame().samples.empty(), "a stopped host with no further callbacks expires the trace");
    clock.playing = true; clock.position = 1088; processor.processBlock (audio, midi); screen.refresh (0);
    require (screen.frame().samples.size() == (channels == 2 ? 64u : 0u), "restart recovers on its first verified input block");
    processor.setNonRealtime (true); clock.position += 64; processor.processBlock (audio, midi); screen.refresh (0);
    require (screen.frame().samples.empty(), "offline bounce never drives the live picture");
    processor.setNonRealtime (false); processor.setPlayHead (nullptr);
}

struct SpaceIngressMember
{
    using Type = bool (Processor::*) (const juce::AudioBuffer<float>&, const HostProcessClock&, int, int,
                                      int64_t, uint64_t, bool);
    friend Type privateMember (SpaceIngressMember);
};
template struct PrivateAccess<SpaceIngressMember, &Processor::feedMeasurementInput>;
inline void verifyStoppedSpaceIngress (Processor& processor, observatory::View& view)
{
    // Record admits non-silent monitoring while stopped. Exercise that same shipping input
    // transaction directly; this does not bypass or alter the separate Record admission policy.
    processor.spaceLive().interrupt();
    auto& screen = space_live_contract::display (view);
    HostProcessClock clock; clock.hasPosition = true; clock.clockSource = KIRIN_HYPHA_CLOCK_PROJECT_TIMELINE;
    juce::AudioBuffer<float> audio (2, 64);
    std::uint64_t span = 0;
    int block = 0;
    for (const int position : { 128, 128, 4096, 4096, 0, 0 })
    {
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 64; ++i)
            audio.setSample (c, i, 0.05f * float (block + 1) + float (i + c) * 0.0001f);
        juce::AudioBuffer<float> original; original.makeCopyOf (audio);
        clock.positionSamples = position;
        require ((processor.*privateMember (SpaceIngressMember {})) (audio, clock, 0, 64, position, 64, false),
                 "stopped monitoring reaches the production input transaction");
        screen.refresh (0);
        require (screen.frame().samples.size() == (block % 2 == 0 ? 64u : 128u),
                 "stationary monitoring accumulates, forward and backward stopped seeks start fresh");
        require (block % 2 == 0 ? screen.frame().span != span : screen.frame().span == span,
                 "stopped project span changes only at a coordinate discontinuity");
        for (int c = 0; c < 2; ++c)
            require (std::memcmp (audio.getReadPointer (c), original.getReadPointer (c), 64 * sizeof (float)) == 0,
                     "stopped ingress leaves all A input bits unchanged");
        for (std::size_t i = 0; i < 64; ++i)
        {
            const auto sample = screen.frame().samples[screen.frame().samples.size() - 64 + i];
            require (space_live_contract::sameSample (sample.left, original.getSample (0, int (i)))
                     && space_live_contract::sameSample (sample.right, original.getSample (1, int (i))),
                     "stopped ingress displays exact new PCM after either seek");
        }
        span = screen.frame().span; ++block;
    }
    processor.spaceLive().interrupt(); screen.refresh (0);
}

inline void verifySpaceBlind (Processor& processor, KirinHyphaEditor& editor, observatory::View& view)
{
    SpaceClock clock; processor.setPlayHead (&clock);
    juce::AudioBuffer<float> audio (2, 64); audio.clear(); audio.addSample (0, 0, 0.25f);
    juce::MidiBuffer midi;
    auto& screen = space_live_contract::display (view);
    processor.processBlock (audio, midi); screen.refresh (0);
    const auto previous = screen.frame().span;
    require (! screen.frame().samples.empty(), "SPACE has a trace before opening Blind");
    (editor.*privateMember (SpaceOpenBlind {}))();
    require (view.isVisible() && ! view.isEnabled() && ! processor.spaceLive().enabled()
             && ! screen.running() && screen.frame().samples.empty(),
             "real Blind entry conceals SPACE, immediately stopping capture and timer and clearing the trace");
    clock.position += 64; processor.processBlock (audio, midi);
    require (! screen.refresh (0) && screen.frame().samples.empty(), "covered SPACE cannot collect audio");
    (editor.*privateMember (SpaceCloseBlind {}))();
    require (view.domain() == observatory::Domain::space && processor.spaceLive().enabled() && screen.running()
             && ! screen.refresh (0) && screen.frame().samples.empty(),
             "closing Blind retains SPACE selection and resumes demand without reviving the hidden window");
    clock.position += 64; processor.processBlock (audio, midi); screen.refresh (0);
    require (screen.frame().samples.size() == 64 && screen.frame().span != previous,
             "first callback after Blind starts a fresh SPACE window automatically");
    processor.setPlayHead (nullptr);
}

#if JUCE_WINDOWS
inline void verifySpaceWindowsParent (Processor& processor, KirinHyphaEditor& editor, observatory::View& view)
{
    struct NativeHost
    {
        HWND root = CreateWindowExW (0, L"STATIC", L"", WS_OVERLAPPEDWINDOW,
                                    0, 0, 960, 640, nullptr, nullptr, GetModuleHandleW (nullptr), nullptr);
        HWND child = root ? CreateWindowExW (0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                            0, 0, 920, 620, root, nullptr, GetModuleHandleW (nullptr), nullptr) : nullptr;
        ~NativeHost() { if (root) DestroyWindow (root); }
    } host;
    require (host.root && host.child, "native parent hierarchy exists");
    ShowWindow (host.root, SW_SHOWNOACTIVATE);
    // A plug-in wrapper owns the peer through an opaque component. Windows refuses a semi-transparent
    // child window to an executable without a Windows 8 manifest, so the editor cannot be that peer.
    juce::Component wrapper; wrapper.setOpaque (true); wrapper.setBounds (editor.getLocalBounds());
    wrapper.addAndMakeVisible (editor); wrapper.setVisible (true); wrapper.addToDesktop (0, host.child);
    auto* const peer = wrapper.getPeer();
    require (peer != nullptr && IsWindow (static_cast<HWND> (peer->getNativeHandle())), "native child peer exists");
    auto& screen = space_live_contract::display (view);
    SpaceClock clock; processor.setPlayHead (&clock);
    juce::AudioBuffer<float> audio (2, 64); juce::MidiBuffer midi;
    const auto process = [&] {
        audio.clear(); audio.addSample (0, 0, 0.25f); clock.position += 64;
        processor.processBlock (audio, midi);
    };
    for (const int condition : { 0, 1, 2 })
    {
        screen.refreshVisibility(); process(); screen.refresh (0);
        require (screen.frame().samples.size() == 64, "embedded Windows editor admits fresh SPACE");
        const auto span = screen.frame().span;
        auto target = condition == 0 ? host.child : host.root;
        ShowWindow (target, condition == 2 ? SW_SHOWMINNOACTIVE : SW_HIDE);
        require (editor.isVisible() && editor.isShowing(), "native host changes leave embedded JUCE showing flags unchanged");
        require (condition == 2 ? bool (IsIconic (host.root)) : ! IsWindowVisible (target), "native parent is actually concealed");
        space_live_contract::tick (screen);
        require (! screen.running() && ! processor.spaceLive().enabled() && screen.frame().samples.empty(),
                 "hidden or minimised native ancestors stop PCM, frame timer and trace");
        process();
        (editor.*privateMember (SpaceEditorTick {}))();
        require (! screen.running() && ! processor.spaceLive().enabled(), "editor polling leaves hidden native SPACE idle");
        ShowWindow (target, condition == 2 ? SW_SHOWNOACTIVATE : SW_SHOWNA);
        require (! IsIconic (host.root) && IsWindowVisible (host.child), "native parent restored");
        (editor.*privateMember (SpaceEditorTick {}))();
        require (view.domain() == observatory::Domain::space && screen.running() && processor.spaceLive().enabled()
                 && screen.frame().samples.empty(), "native restoration resumes selection without reviving old PCM");
        process(); screen.refresh (0);
        require (screen.frame().samples.size() == 64 && screen.frame().span != span,
                 "Windows native restoration starts with only fresh callback PCM");
        processor.spaceLive().interrupt(); screen.refresh (0);
    }
    processor.setPlayHead (nullptr); wrapper.removeFromDesktop(); wrapper.removeChildComponent (&editor);
}
#endif

// A shared native host does not forward all conceal/restore operations as visibility changes.
// Keep the shipping editor attached to that window and drive its real surviving timer entry.
inline void verifySpaceNativeRestore (Processor& processor, KirinHyphaEditor& editor, observatory::View& view)
{
   #if JUCE_MAC
    withNativeWindowHost ([&] {
        juce::Component host;
        host.setSize (editor.getWidth(), editor.getHeight());
        host.addToDesktop (juce::ComponentPeer::windowHasTitleBar | juce::ComponentPeer::windowHasMinimiseButton);
        host.setVisible (true);
        editor.addToDesktop (0, host.getPeer()->getNativeHandle());
        struct VisibilityEvents final : juce::ComponentListener
        {
            void componentVisibilityChanged (juce::Component&) override { ++count; }
            int count = 0;
        } events;
        editor.addComponentListener (&events);
        auto& screen = space_live_contract::display (view);
        SpaceClock clock; processor.setPlayHead (&clock);
        juce::AudioBuffer<float> audio (2, 64); juce::MidiBuffer midi;
        const auto process = [&] {
            audio.clear(); audio.addSample (0, 0, 0.25f); clock.position += 64;
            processor.processBlock (audio, midi);
        };
        for (const auto conceal : { setNativeWindowMinimised, setNativeViewHidden, setNativeWindowHidden })
        {
            screen.refreshVisibility(); process(); screen.refresh (0);
            require (screen.frame().samples.size() == 64, "embedded native SPACE starts live");
            const auto span = screen.frame().span;
            require (conceal (host.getPeer()->getNativeHandle(), true), "native host concealment confirmed");
            require (editor.isVisible() && (editor.isShowing() == (conceal != setNativeWindowMinimised)),
                     "native parent view/window hiding preserves JUCE showing flags");
            space_live_contract::tick (screen); // The real frame timer detects native concealment.
            require (! screen.running() && ! processor.spaceLive().enabled() && screen.frame().samples.empty(),
                     "native concealment retires SPACE capture, frame timer and trace");
            process(); (editor.*privateMember (SpaceEditorTick {}))();
            require (! screen.running() && ! processor.spaceLive().enabled() && screen.frame().samples.empty(),
                     "hidden callbacks and editor polling leave SPACE idle");
            require (conceal (host.getPeer()->getNativeHandle(), false), "native host restoration confirmed");
            require (editor.isVisible() && editor.isShowing() && events.count == 0,
                     "shared native restoration leaves JUCE visibility unchanged");
            (editor.*privateMember (SpaceEditorTick {}))();
            require (view.domain() == observatory::Domain::space && processor.spaceLive().enabled() && screen.running()
                     && screen.frame().samples.empty(), "editor timer resumes selected SPACE without a resize or page change");
            process(); screen.refresh (0);
            require (screen.frame().samples.size() == 64 && screen.frame().span != span,
                     "restored native host displays only its first fresh callback");
            for (std::size_t i = 0; i < 64; ++i)
                require (space_live_contract::sameSample (screen.frame().samples[i].left, audio.getSample (0, int (i)))
                         && space_live_contract::sameSample (screen.frame().samples[i].right, audio.getSample (1, int (i))),
                         "native restoration keeps only exact fresh PCM");
            processor.spaceLive().interrupt(); screen.refresh (0);
        }
        processor.setPlayHead (nullptr);
        editor.removeComponentListener (&events); editor.removeFromDesktop();
    });
   #elif JUCE_WINDOWS
    verifySpaceWindowsParent (processor, editor, view);
   #else
    juce::ignoreUnused (processor, editor, view);
   #endif
}

inline void verifySpaceLiveProduct()
{
    using namespace observatory;
    for (const auto wrapper : { juce::AudioProcessor::wrapperType_AudioUnit,
                                juce::AudioProcessor::wrapperType_VST3, juce::AudioProcessor::wrapperType_AAX })
        for (const auto role : { Processor::Role::Pre, Processor::Role::Post })
            for (const int channels : { 1, 2 })
                for (const bool precise : { false, true })
                {
                    juce::AudioProcessor::setTypeOfNextNewPlugin (wrapper);
                    Processor processor (role);
                    juce::AudioProcessor::BusesLayout layout;
                    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
                    layout.inputBuses.add (set); layout.outputBuses.add (set);
                    require (processor.setBusesLayout (layout), "mono/stereo layout");
                    processor.setProcessingPrecision (precise ? juce::AudioProcessor::doublePrecision : juce::AudioProcessor::singlePrecision);
                    processor.setObservatoryDomainPreference (stateValue (Domain::space));
                    prepareFixtureProcessor (processor, 48'000, 64);
                    std::unique_ptr<juce::AudioProcessorEditor> owner (processor.createEditor());
                    require (owner != nullptr, "shipping editor exists"); owner->setVisible (true);
                    auto* editor = dynamic_cast<KirinHyphaEditor*> (owner.get());
                    auto* view = component<View> (*owner);
                    require (editor && view && processor.spaceLive().enabled(), "real editor enables only its visible SPACE page");
                    if (precise) verifySpaceInput<double> (processor, *view, channels);
                    else verifySpaceInput<float> (processor, *view, channels);
                    if (channels == 2 && ! precise) verifyStoppedSpaceIngress (processor, *view);
                    if (role == Processor::Role::Post && channels == 2 && ! precise)
                        verifySpaceBlind (processor, *editor, *view);
                    if (channels == 2 && ! precise)
                        verifySpaceNativeRestore (processor, *editor, *view);
                    (editor->*privateMember (SpacePageMember {})) (Domain::level);
                    require (! processor.spaceLive().enabled() && ! space_live_contract::display (*view).running(), "real page switch retires demand and timer");
                    (editor->*privateMember (SpacePageMember {})) (Domain::space);
                    editor->setVisible (false); require (! processor.spaceLive().enabled(), "hidden editor stops acquisition immediately");
                    editor->setVisible (true); require (processor.spaceLive().enabled(), "showing retained editor resumes demand");
                    view->setManualHybridVuVisible (true); require (! processor.spaceLive().enabled(), "VU replaces SPACE without leaving its tap running");
                    view->setManualHybridVuVisible (false); require (processor.spaceLive().enabled(), "returning from VU resumes SPACE");
                    view->setHostRecording (true); require (! processor.spaceLive().enabled(), "record-triggered VU retires SPACE demand");
                    view->dismissHybridVuForCurrentRecording(); require (processor.spaceLive().enabled(), "dismissing record VU resumes the selected SPACE");
                    view->setHostRecording (false);
                    processor.editorBeingDeleted (owner.get()); owner.reset();
                    require (! processor.spaceLive().enabled(), "closing editor retires its tap");
                    processor.releaseResources();
                }
    std::cout << "SPACE product: PASS (PRE/POST, AU/VST3/AAX ingress, float/double, mono/stereo, seek, stop, offline, hidden, page, VU, Blind, close)\n";
}
}
