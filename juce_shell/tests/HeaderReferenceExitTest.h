#pragma once
#include "ReferenceOneSongLibrary.h"
#include "EditorProductChecks.h"
#include <chrono>
#include <thread>

namespace hypha::tests::editor_product
{
struct HeaderProcessorTick { using Type = void (Processor::*)(); friend Type privateMember (HeaderProcessorTick); };
template struct PrivateAccess<HeaderProcessorTick, &Processor::timerCallback>;
struct HeaderEditorTick { using Type = void (KirinHyphaEditor::*)(); friend Type privateMember (HeaderEditorTick); };
template struct PrivateAccess<HeaderEditorTick, &KirinHyphaEditor::timerCallback>;

// This editor test links the shipping processor: use its actual clock and audio entry, with
// no native-only clock override. The input is independent of the Reference tone.
struct HeaderReferenceAudio
{
    struct Clock final : juce::AudioPlayHead
    {
        std::int64_t position = 0;
        juce::Optional<PositionInfo> getPosition() const override
        { PositionInfo info; info.setIsPlaying (true); info.setTimeInSamples (position); return info; }
    } clock;
    std::unique_ptr<Processor> post;
    std::unique_ptr<KirinHyphaEditor> editor;
    juce::AudioBuffer<float> postAudio { 2, 512 };
    juce::MidiBuffer midi;
    bool expectOrdinaryAudio = false;
    HeaderReferenceAudio()
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
        post = std::make_unique<Processor> (Processor::Role::Post);
        auto layout = post->getBusesLayout();
        layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
        layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
        require (post->setBusesLayout (layout), "header audio negotiates stereo");
        post->setPlayHead (&clock);
        prepareFixtureProcessor (*post, 48000, 512);
        editor.reset (dynamic_cast<KirinHyphaEditor*> (post->createEditorIfNeeded()));
        require (editor != nullptr, "header audio uses the shipping editor");
        editor->setVisible (true); editor->setSize (900, 600);
    }
    ~HeaderReferenceAudio()
    { post->editorBeingDeleted (editor.get()); editor.reset(); post->releaseResources(); }
    observatory::View& view() { return *component<observatory::View> (*editor); }
    static float input (std::int64_t position, int channel)
    { return 0.08f * static_cast<float> (std::sin (0.071 * static_cast<double> (position) + channel)); }
    void advance (double seconds)
    {
        auto due = std::chrono::steady_clock::now();
        for (int block = 0; block < static_cast<int> (std::ceil (seconds * 48000 / 512)); ++block)
        {
            for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < 512; ++n)
                postAudio.setSample (ch, n, input (clock.position + n, ch));
            post->processBlock (postAudio, midi);
            if (expectOrdinaryAudio)
                for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < 512; ++n)
                    require (postAudio.getSample (ch, n) == input (clock.position + n, ch),
                             "every sample after the header exit is the bit-identical A input");
            clock.position += 512;
            (post.get()->*privateMember (HeaderProcessorTick {}))();
            (editor.get()->*privateMember (HeaderEditorTick {}))();
            due += std::chrono::nanoseconds (512LL * 1'000'000'000LL / 48000);
            if (std::chrono::steady_clock::now() < due) std::this_thread::sleep_until (due);
        }
    }
};

// The real header must release audible Reference PCM as well as change the meter page.
inline void verifyHeaderReferenceExitProduct (const juce::File& sandbox)
{
    const auto reference = reference_audition::RuntimeV2Repository::transportRoot();
   #if JUCE_WINDOWS
    const auto identity = sandbox.getChildFile ("home/AppData/Roaming/Kirin OS/identity.json");
   #else
    const auto identity = sandbox.getChildFile ("home/Library/Application Support/Kirin OS/identity.json");
   #endif
    require (reference.isAChildOf (sandbox) && identity.isAChildOf (sandbox), "header audio writes only inside its fixture");
    require (identity.getParentDirectory().createDirectory().wasOk()
        && storage::writeText (identity, "{\"license\":\"os\"}"), "header fixture has its own Reference access");
    OneSongLibrary song;
    song.frequencyHz = 1500.0; song.integratedLufs = -26.7; song.truePeakDbtp = -26.0;
    writeOneSongLibrary (reference, song);
    HeaderReferenceAudio rig;
    rig.post->refreshLicenseForUserAction();
    require (rig.post->licenseIsOs(), "header audio has fixture Reference access");
    rig.expectOrdinaryAudio = false;
    rig.view().onDomainChange (observatory::Domain::reference);
    for (int n = 0; n < 300 && !rig.post->referenceAuditionSnapshot().referenceReady; ++n) rig.advance (0.05);
    require (rig.post->referenceAuditionSnapshot().referenceReady, "header fixture prepares the actual Reference song");
    rig.advance (4.0); // A's existing MATCH observation window.
    for (const auto* id : { "observatory-meter-context-track", "observatory-meter-context-2mix" })
    {
        rig.view().onDomainChange (observatory::Domain::time);
        auto* referenceButton = dynamic_cast<juce::Button*> (find (*rig.editor, "observatory-reference"));
        require (referenceButton != nullptr, "header REFERENCE button exists");
        referenceButton->onClick();
        require (rig.view().domain() == observatory::Domain::reference && rig.post->selectReferenceRef(),
                 "header REFERENCE opens the real audition before leaving it");
        for (int n = 0; n < 120 && rig.post->referenceOutputSlot() != 3; ++n) rig.advance (0.05);
        rig.advance (0.1);
        double power = 0.0, sine = 0.0, cosine = 0.0;
        for (int n = 0; n < 512; ++n)
        {
            const auto out = rig.postAudio.getSample (0, n);
            const auto phase = 6.283185307179586 * song.frequencyHz * n / 48000.0;
            power += out * out; sine += out * std::sin (phase); cosine += out * std::cos (phase);
        }
        require (rig.post->referenceOutputSlot() == 3 && rig.post->referenceAuditionSnapshot().bSelected
            && power > 0.0 && 2.0 * (sine * sine + cosine * cosine) / (512.0 * power) > 0.9,
                 "Reference is selected and its distinct tone actually sounds before each header exit");
        auto* destination = dynamic_cast<juce::Button*> (find (*rig.editor, id));
        require (destination != nullptr, "header measurement button exists");
        destination->onClick();
        require (rig.view().domain() == observatory::Domain::time && destination->getToggleState(),
                 "header exit restores the remembered measurement page");
        rig.advance (0.1); // Complete the established Reference-to-A fade.
        const auto state = rig.post->referenceAuditionSnapshot();
        require (!state.bSelected && !state.pendingAudition.waiting() && state.switchingSlot == 0
            && rig.post->referenceOutputSlot() == 0, "both header exits select A and release Reference output");
        rig.expectOrdinaryAudio = true;
        rig.advance (0.1); // Check every A sample against the independent input, bit for bit.
        rig.expectOrdinaryAudio = false;
    }
}
}
