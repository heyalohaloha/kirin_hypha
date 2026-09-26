#include "../src/PluginEditor.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaReferenceAccessPanel.h"
#include "../src/HyphaTextStyle.h"
#include "ValidationStorageSandbox.h"
#include "EditorCaptureProductTest.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

#if JUCE_MAC
void initialiseBlindProductHostApplication();
#endif

namespace
{
using Processor = KirinHyphaProcessorBase;
using Domain = hypha::observatory::Domain;
using AnalysisPage = hypha::analysis_navigation::Page;

void require (bool condition, const char* message)
{
    if (! condition) { std::cerr << "Editor surface: " << message << '\n'; std::exit (1); }
}

template <typename T> T* component (juce::Component& parent)
{
    if (auto* result = dynamic_cast<T*> (&parent)) return result;
    for (auto* child : parent.getChildren())
        if (auto* result = component<T> (*child)) return result;
    return nullptr;
}

juce::Component* find (juce::Component& parent, const juce::String& id)
{
    if (parent.getComponentID() == id) return &parent;
    for (auto* child : parent.getChildren())
        if (auto* result = find (*child, id)) return result;
    return nullptr;
}

template <class Tag, typename Tag::Type Member> struct TestAccess
{
    friend typename Tag::Type testMember (Tag) { return Member; }
};

struct SetAnalysisPage
{
    using Type = void (KirinHyphaEditor::*) (AnalysisPage);
    friend Type testMember (SetAnalysisPage);
};
template struct TestAccess<SetAnalysisPage, &KirinHyphaEditor::setAnalysisPage>;

struct FreezeCapture
{
    using Type = hypha::capture::Snapshot (KirinHyphaEditor::*) (int, int);
    friend Type testMember (FreezeCapture);
};
template struct TestAccess<FreezeCapture, &KirinHyphaEditor::freezeObservatoryCapture>;

int differentPixels (const juce::Image& first, const juce::Image& second,
                     juce::Rectangle<int> area)
{
    area = area.getIntersection (first.getBounds()).getIntersection (second.getBounds());
    int changed = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            if (first.getPixelAt (x, y) != second.getPixelAt (x, y)) ++changed;
    return changed;
}

void verifyRecordBodyOwnership()
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
    Processor processor (Processor::Role::Post);
    processor.setMeterContextPreference (hypha::meter_context::MeterContext::trackStem, false);
    processor.setObservatoryDomainPreference (hypha::observatory::stateValue (Domain::frequency));
    processor.prepareToPlay (48'000, 960);
    auto editor = std::unique_ptr<KirinHyphaEditor> (
        dynamic_cast<KirinHyphaEditor*> (processor.createEditorIfNeeded()));
    require (editor != nullptr, "Record body shipping editor opens");
    editor->setSize (600, 400);
    editor->setVisible (true);
    auto* view = component<hypha::observatory::View> (*editor);
    auto* spectrum = component<hypha::SpectrumComponent> (*editor);
    auto* perceptual = component<hypha::PerceptualComponent> (*editor);
    auto* absolute = component<hypha::AbsoluteComponent> (*editor);
    auto* attack = component<hypha::AttackComponent> (*editor);
    require (view && spectrum && perceptual && absolute && attack,
             "Record body uses the shipping component tree");

    struct Case { AnalysisPage page; juce::Component* expected; const char* name; };
    const std::array<Case, 4> cases {{
        { AnalysisPage::spectrum, spectrum, "FREQ" },
        // An unpaired SHARP page deliberately uses the absolute observation worker.
        { AnalysisPage::perceptual, absolute, "SHARP" },
        { AnalysisPage::absolute, absolute, "LIVE" },
        { AnalysisPage::attack, attack, "ATTACK" },
    }};
    for (const auto& test : cases)
    {
        (editor.get()->*testMember (SetAnalysisPage {})) (test.page);
        require (test.expected->isVisible(), "selected external analysis owns the body");
        const auto domainBefore = view->domain();

        KirinRecordDisplay record {};
        record.phase = KIRIN_RECORD_DISPLAY_RESULT_HOLD;
        record.generation = 42;
        record.has_measure = 1;
        record.has_session = 1;
        record.measure.lufs_m = -17.2;
        record.measure.lufs_s = -16.8;
        record.measure.crest = 11.1;
        record.measure.psr = 9.4;
        record.measure.sharpness = 1.3;
        record.session.max_true_peak = -0.8;
        record.session.lufs_i = -16.1;
        view->setRecordDisplay (record, true);
        require (view->recordBodyActive(), "Record result owns the Observatory body");
        require (! test.expected->isVisible(), "Record result retires the external analysis sibling");

        const auto editorBody = editor->getLocalArea (view, view->analysisBodyBounds());
        const auto captureBody = view->captureBodyBounds (1'200, 800, false);
        const auto editorFirst = editor->createComponentSnapshot (editor->getLocalBounds());
        const auto captureFirst = (editor.get()->*testMember (FreezeCapture {})) (1'200, 800).image;
        record.measure.lufs_m = -37.2;
        record.measure.lufs_s = -36.8;
        record.session.lufs_i = -36.1;
        view->setRecordDisplay (record, true);
        const auto editorSecond = editor->createComponentSnapshot (editor->getLocalBounds());
        const auto captureSecond = (editor.get()->*testMember (FreezeCapture {})) (1'200, 800).image;
        require (differentPixels (editorFirst, editorSecond, editorBody) > 100,
                 "Record values reach the shipping editor body");
        require (differentPixels (captureFirst, captureSecond, captureBody) > 100,
                 "Record values reach the synchronous Capture body");

        record.phase = KIRIN_RECORD_DISPLAY_WATCH;
        view->setRecordDisplay (record, false);
        require (! view->recordBodyActive(), "Watch releases Record body ownership");
        require (test.expected->isVisible(), "the selected analysis page returns after Record");
        require (view->domain() == domainBefore, "Record preserves the selected domain");
        std::cout << "Record body " << test.name << ": PASS" << std::endl;
    }
    processor.editorBeingDeleted (editor.get());
    editor.reset();
    processor.releaseResources();
}

struct HostClock final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (recording);
        info.setIsRecording (recording);
        info.setTimeInSamples (position);
        return info;
    }
    bool recording = false;
    std::int64_t position = 0;
};

// Drum hits leave silent blocks between them. The live analysis views must read those as a rest,
// as Watch does, not as the input ending, or FREQ, LIVE, SHARP and DRUM blank between hits. A
// rest longer than the 3 s window, or a stopped transport, still ends the live input.
void verifyLiveInputThroughMusicalRests()
{
    struct Transport final : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (playing);
            info.setTimeInSamples (position);
            return info;
        }
        bool playing = true;
        std::int64_t position = 0;
    } transport;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
    Processor processor (Processor::Role::Post);
    constexpr int block = 480;
    processor.prepareToPlay (48'000, block);
    processor.setPlayHead (&transport);
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    const auto process = [&] (bool audible) {
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < block; ++sample)
                buffer.setSample (channel, sample, audible
                    ? 0.1f * std::sin (0.05f * (float) (transport.position + sample)) : 0.0f);
        processor.processBlock (buffer, midi);
        transport.position += block;
    };
    for (int index = 0; index < 20; ++index)
        process (true);
    require (processor.hasLiveInput(), "audible input is live");
    for (int hit = 0; hit < 8; ++hit)
    {
        for (int rest = 0; rest < 25; ++rest) // 250 ms between hits
        {
            process (false);
            require (processor.hasLiveInput(), "a rest between hits keeps the input live");
        }
        process (true);
    }
    for (int index = 0; index < 330; ++index) // 3.3 s of silence
        process (false);
    require (! processor.hasLiveInput(), "a rest longer than the Watch window ends the live input");
    process (true);
    transport.playing = false;
    process (false);
    require (! processor.hasLiveInput(), "a stopped transport ends the live input at once");
    processor.setPlayHead (nullptr);
    processor.releaseResources();
}

void verifySavedReferenceChoices()
{
    juce::XmlElement xml ("KirinHyphaState");
    hypha::reference_audition::ReferenceComparisonSettings settings;
    settings.version = { "preset-b", "check-b", "candidate-b", "cue-b" };
    settings.check = { "preset-c", "check-c", "candidate-c", "cue-c" };
    settings.write (xml);
    juce::MemoryBlock bytes;
    juce::AudioProcessor::copyXmlToBinary (xml, bytes);
    Processor restored (Processor::Role::Post);
    restored.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
    juce::MemoryBlock saved;
    restored.getStateInformation (saved);
    const auto output = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
    require (output != nullptr, "Reference host XML state round trip");
    const auto choices = hypha::reference_audition::ReferenceComparisonSettings::read (*output);
    require (choices.version.target() == settings.version.target()
        && choices.check.target() == settings.check.target(), "shipping processor retains B/C before prepare");
    require (! restored.referenceAuditionSnapshot().bSelected, "restoring the processor never selects reference audio");
}

void verifyPairHeaderAtEverySize (const juce::File& previews)
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
    Processor processor (Processor::Role::Post);
    processor.prepareToPlay (48'000, 960);
    auto editor = std::unique_ptr<KirinHyphaEditor> (
        dynamic_cast<KirinHyphaEditor*> (processor.createEditorIfNeeded()));
    require (editor != nullptr, "POST editor opens for pair-header geometry");
    auto* name = component<hypha::EditableName> (*editor);
    auto* dropdown = component<hypha::PairDropdownButton> (*editor);
    require (name != nullptr && dropdown != nullptr,
             "shipping pair label and menu target both exist");
    require (name->getParentComponent() == dropdown->getParentComponent(),
             "pair label and menu use one coordinate space");

    for (const auto preset : hypha::observatory::sizePresets)
    {
        editor->setSize (preset.width, preset.height);
        const auto context = hypha::presentation::forEditor (preset.width, preset.height);
        const auto style = hypha::typography::resolve (
            context, hypha::typography::TextRole::selector);
        const auto required = hypha::text_style::requiredWidth (
            hypha::monoFont (context, hypha::typography::TextRole::selector), "PAIR", style);
        require (dropdown->getWidth() == hypha::ui_contract::pairDropdownWidth,
                 "pair menu retains its 28 px target");
        require (! name->getBounds().intersects (dropdown->getBounds()),
                 "pair label never intersects the down-arrow target");
        require (name->getWidth() >= required,
                 "PAIR remains fully paintable beside the down arrow");

        name->setModelName ("PAIR");
        const auto pair = name->createComponentSnapshot (name->getLocalBounds());
        if (previews != juce::File())
        {
            require (previews.createDirectory().wasOk(), "pair-header preview directory");
            auto output = previews.getChildFile (
                "post-pair-header-" + juce::String (preset.width) + ".png").createOutputStream();
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
            require (output != nullptr && output->setPosition (0) && output->truncate().wasOk(),
                     "pair-header preview output");
            require (juce::PNGImageFormat().writeImageToStream (image, *output),
                     "pair-header preview PNG");
        }
        name->setModelName ("PAI");
        const auto pai = name->createComponentSnapshot (name->getLocalBounds());
        require (differentPixels (pair, pai, pair.getBounds()) > 4,
                 "the final R is visible rather than clipped beneath the down arrow");
    }
    processor.editorBeingDeleted (editor.get());
    editor.reset();
    processor.releaseResources();
}

// Exercise the shipping editor's refresh timer, sibling z-order and hit routing together.
// Calling a detached View's onClick cannot detect another pane covering the VU exit.
class SurfaceContract final : private juce::Timer
{
public:
    explicit SurfaceContract (juce::File output) : previews (std::move (output))
    {
        started = std::chrono::steady_clock::now();
        nextCase();
        startTimer (20);
    }

    ~SurfaceContract() override { stopTimer(); closeProcessor(); }
    bool passed = false;

private:
    void closeEditor()
    {
        if (editor) processor->editorBeingDeleted (editor.get());
        editor.reset();
        view = nullptr;
    }

    void openEditor()
    {
        editor.reset (processor->createEditorIfNeeded());
        require (editor != nullptr, "shipping editor opens");
        const auto size = hypha::observatory::sizePresets[sizeIndex];
        editor->setSize (size.width, size.height);
        editor->setVisible (true);
        view = component<hypha::observatory::View> (*editor);
        require (view != nullptr, "shipping Observatory exists");
    }

    void closeProcessor()
    {
        closeEditor();
        if (processor) processor->releaseResources();
        processor.reset();
    }

    void nextCase()
    {
        closeProcessor();
        // Start with the exact failing Reference -> VU route, then all other domains/roles.
        static constexpr Domain domains[] { Domain::reference, Domain::level,
            Domain::time, Domain::frequency, Domain::space };
        const auto domain = domains[domainIndex];
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
        processor = std::make_unique<Processor> (pre ? Processor::Role::Pre : Processor::Role::Post);
        processor->setObservatoryDomainPreference (hypha::observatory::stateValue (domain));
        processor->setHybridVuOnRecordPreference (true);
        processor->setPlayHead (&clock);
        processor->setNonRealtime (false);
        processor->prepareToPlay (48000, 960);
        openEditor();
        stage = 0;
        changed = std::chrono::steady_clock::now();
        std::cout << "role=" << (pre ? "PRE" : "POST") << " domain=" << domainIndex
                  << " width=" << editor->getWidth() << std::endl;
    }

    juce::Button& vuButton()
    {
        auto* button = dynamic_cast<juce::Button*> (find (*editor, "observatory-hybrid-vu"));
        require (button != nullptr && button->isEnabled(), "VU control exists and is enabled");
        for (auto* ancestor = static_cast<juce::Component*> (button); ancestor != nullptr;
             ancestor = ancestor->getParentComponent())
            require (ancestor->isVisible(), "VU control and every ancestor are visible");
        const auto point = editor->getLocalPoint (button, button->getLocalBounds().getCentre());
        require (editor->getComponentAt (point) == button,
                 "VU control receives the actual pointer hit after the editor refresh");
        return *button;
    }

    void verifySurface (bool vu)
    {
        require (view->hybridVuVisible() == vu, "expected normal/VU surface");
        vuButton();
        if (auto* access = component<hypha::reference_ui::AccessPanel> (*editor))
        {
            require (! access->isVisible() || (! vu && view->domain() == Domain::reference),
                     "Reference access never covers VU or another domain");
            if (access->isVisible())
                require (access->getBounds() == view->analysisBodyBounds(), "Reference returns to its body bounds");
        }
        require (processor->getLatencySamples() == 0, "display transitions retain zero latency");
        if (! vu)
        {
            // A connection may arrive after the editor has laid out an empty guide rail.
            // Exercise that transition through the shipping hierarchy, including VU return.
            view->setGuide ("CONNECT  Saved Work", {}, true);
            auto* guide = dynamic_cast<juce::Button*> (&view->guideDetailsAnchor());
            require (guide != nullptr && guide->isVisible() && ! guide->getBounds().isEmpty(),
                     "late connection action is visible");
            const auto point = editor->getLocalPoint (guide, guide->getLocalBounds().getCentre());
            require (editor->getComponentAt (point) == guide, "one guide action owns the actual pointer hit");
        }
    }

    void preview (const char* state)
    {
        if (previews == juce::File() || pre || domainIndex != 0) return;
        require (previews.createDirectory().wasOk(), "preview directory");
        const auto name = "post-reference-" + juce::String (state) + "-" + juce::String (editor->getWidth());
        auto image = editor->createComponentSnapshot (editor->getLocalBounds());
        juce::FileOutputStream output (previews.getChildFile (name + ".png"));
        require (output.openedOk() && output.setPosition (0) && output.truncate().wasOk(), "preview output");
        require (juce::PNGImageFormat().writeImageToStream (image, output), "preview PNG");
    }

    void timerCallback() override
    {
        require (std::chrono::steady_clock::now() - started < std::chrono::seconds (110), "surface round trip timeout");
        buffer.clear();
        processor->processBlock (buffer, midi);
        clock.position += buffer.getNumSamples();
        require (buffer.getMagnitude (0, buffer.getNumSamples()) == 0.0f, "display keeps silent A input unchanged");
        const auto elapsed = std::chrono::steady_clock::now() - changed;
        if (elapsed < std::chrono::milliseconds (180)) return;
        // Button commands and the editor's timer are separate message-loop events.
        // Wait for the requested transition before asserting sibling hit routing;
        // a loaded CI runner need not dispatch both within a fixed 180 ms window.
        const bool expectedVu = stage == 1 || stage == 2 || stage == 4;
        if (view->hybridVuVisible() != expectedVu)
        {
            if (elapsed < std::chrono::seconds (2)) return;
            std::cerr << "stage=" << stage << " expected VU=" << expectedVu
                      << " actual VU=" << view->hybridVuVisible() << '\n';
            require (false, "requested surface transition did not arrive");
        }
        switch (stage)
        {
            case 0: verifySurface (false); vuButton().triggerClick(); break;
            case 1:
                preview ("vu");
                verifySurface (true);
                closeEditor(); openEditor();
                break;
            case 2: verifySurface (true); vuButton().triggerClick(); break;
            case 3:
                verifySurface (false); preview ("returned");
                clock.recording = true;
                break;
            case 4: verifySurface (true); vuButton().triggerClick(); break;
            case 5: verifySurface (false); clock.recording = false; break;
            case 6:
                verifySurface (false);
                ++completed;
                if (++sizeIndex == hypha::observatory::sizePresets.size())
                {
                    sizeIndex = 0;
                    ++domainIndex;
                    if (pre && domainIndex == 3) ++domainIndex; // PRE has no FREQ.
                    if (domainIndex == 5)
                    {
                        if (pre)
                        {
                            passed = true;
                            std::cout << "PASS surface cases=" << completed << std::endl;
                            stopTimer();
                            juce::MessageManager::getInstance()->stopDispatchLoop();
                            return;
                        }
                        pre = true; domainIndex = 1; // PRE has no Reference.
                    }
                }
                nextCase();
                return;
        }
        ++stage;
        changed = std::chrono::steady_clock::now();
    }

    juce::File previews;
    HostClock clock;
    juce::AudioBuffer<float> buffer { 2, 960 };
    juce::MidiBuffer midi;
    std::unique_ptr<Processor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    hypha::observatory::View* view = nullptr;
    std::chrono::steady_clock::time_point started, changed;
    std::size_t sizeIndex = 0;
    int domainIndex = 0, stage = 0, completed = 0;
    bool pre = false;
};
}

int main (int argc, char** argv)
{
    ValidationStorageSandbox sandbox;
   #if JUCE_MAC
    initialiseBlindProductHostApplication();
   #endif
    juce::ScopedJuceInitialiser_GUI init;
    verifyRecordBodyOwnership();
    verifyLiveInputThroughMusicalRests();
    verifySavedReferenceChoices();
    const auto previews = argc > 1 ? juce::File (argv[1]) : juce::File();
    verifyPairHeaderAtEverySize (previews);
    std::unique_ptr<SurfaceContract> contract;
    CaptureProductContract capture([&] { contract=std::make_unique<SurfaceContract>(previews); });
    juce::MessageManager::getInstance()->runDispatchLoop();
    return contract && contract->passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
