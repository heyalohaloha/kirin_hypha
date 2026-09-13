#include "../src/PluginProcessor.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaReferenceAccessPanel.h"
#include "ValidationStorageSandbox.h"

#include <chrono>
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
    verifySavedReferenceChoices();
    SurfaceContract contract (argc > 1 ? juce::File (argv[1]) : juce::File());
    juce::MessageManager::getInstance()->runDispatchLoop();
    return contract.passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
