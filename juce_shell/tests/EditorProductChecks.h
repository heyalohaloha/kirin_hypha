#pragma once

#include "../src/PluginEditor.h"
#include "../src/HyphaFeedbackStrip.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaTextStyle.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

// Shipping-editor checks that need a real processor and editor but no message loop.
namespace hypha::tests::editor_product
{
using Processor = KirinHyphaProcessorBase;

inline void require (bool condition, const char* message)
{
    if (! condition) { std::cerr << "Editor product: " << message << '\n'; std::exit (1); }
}

inline juce::Component* find (juce::Component& parent, const juce::String& id)
{
    if (parent.getComponentID() == id) return &parent;
    for (auto* child : parent.getChildren())
        if (auto* result = find (*child, id)) return result;
    return nullptr;
}

template <typename T> T* component (juce::Component& parent)
{
    if (auto* result = dynamic_cast<T*> (&parent)) return result;
    for (auto* child : parent.getChildren())
        if (auto* result = component<T> (*child)) return result;
    return nullptr;
}

template <class Tag, typename Tag::Type Member> struct PrivateAccess
{
    friend typename Tag::Type privateMember (Tag) { return Member; }
};

struct ShowToast
{
    using Type = void (KirinHyphaEditor::*) (const juce::String&);
    friend Type privateMember (ShowToast);
};
template struct PrivateAccess<ShowToast, &KirinHyphaEditor::showToast>;

// A look review of the shipping editor, written only when KIRIN_HYPHA_COMPACT_REVIEW_DIR is set.
inline void writeReview (juce::Component& editor, const juce::String& name)
{
    const auto* directory = std::getenv ("KIRIN_HYPHA_COMPACT_REVIEW_DIR");
    if (directory == nullptr)
        return;
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
    const auto file = juce::File (directory).getChildFile (name + ".png");
    file.deleteFile();
    juce::FileOutputStream output (file);
    require (output.openedOk() && juce::PNGImageFormat().writeImageToStream (image, output), "review PNG");
}

// Drum hits leave silent blocks between them. The live analysis views must read those as a rest,
// as Watch does, not as the input ending, or FREQ, LIVE, SHARP and DRUM blank between hits. A
// rest longer than the 3 s window, or a stopped transport, still ends the live input.
inline void verifyLiveInputThroughMusicalRests()
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

// Where the footer folds into the header (100% and 125%), feedback is shown whole in a strip over
// the bottom edge of the body: above the analysis page that owns the body, receiving the pointer,
// and gone again with the feedback. At the larger sizes the footer keeps it and no strip appears.
inline void verifyFoldedFeedbackStrip()
{
    const juce::String message ("Jungle Mode changed for this session only");
    for (const auto role : { Processor::Role::Post, Processor::Role::Pre })
        for (const auto& preset : observatory::sizePresets)
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
            Processor processor (role);
            // POST FREQ: an analysis page owns the body. PRE: the Observatory paints LEVEL itself.
            processor.setObservatoryDomainPreference (observatory::stateValue (
                role == Processor::Role::Post ? observatory::Domain::frequency : observatory::Domain::level));
            processor.prepareToPlay (48'000, 960);
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorIfNeeded());
            require (editor != nullptr, "shipping editor opens");
            editor->setSize (preset.width, preset.height);
            editor->setVisible (true);
            auto* shipping = dynamic_cast<KirinHyphaEditor*> (editor.get());
            auto* view = component<observatory::View> (*editor);
            auto* strip = dynamic_cast<FeedbackStrip*> (find (*editor, "feedback-strip"));
            require (shipping != nullptr && view != nullptr && strip != nullptr, "editor, view and strip exist");
            const bool folded = observatory::footerFolds (preset.density);
            require (view->statusStripFolded() == folded, "the status strip folds with the footer");
            require (! strip->isVisible(), "no strip without feedback");

            (shipping->*privateMember (ShowToast {})) (message);
            const auto body = view->bodyBounds();
            const auto area = view->statusStripBounds();
            require (strip->isVisible() == folded, "the strip shows feedback only where the footer folds");
            require (view->feedbackDetailsAnchor().isVisible() == ! folded,
                     "the footer's status line shows feedback where the footer stays");
            if (folded)
            {
                require (area.getX() == body.getX() && area.getRight() == body.getRight()
                             && area.getBottom() == body.getBottom() && area.getY() > body.getY(),
                         "the strip spans the bottom edge of the body");
                const auto context = presentation::forEditor (preset.width, preset.height);
                const auto needed = text_style::requiredWidth (
                    monoFont (context, typography::TextRole::status), message,
                    typography::resolve (context, typography::TextRole::status));
                std::cout << "Feedback strip " << preset.width << ": " << area.getWidth() << " wide, text "
                          << needed << '\n';
                require (needed + 12 <= area.getWidth(), "the feedback reads whole");
                const auto centre = editor->getLocalPoint (strip, strip->getLocalBounds().getCentre());
                require (editor->getComponentAt (centre) == strip, "the strip is above the page and takes the pointer");
                writeReview (*editor, juce::String (role == Processor::Role::Post ? "post" : "pre")
                                          + "_feedback_strip_" + juce::String (preset.width));
            }
            else
                require (area == view->sessionBounds(), "without folding, status stays in the footer");

            (shipping->*privateMember (ShowToast {})) ({});
            require (! strip->isVisible(), "the strip leaves with the feedback");
            processor.editorBeingDeleted (editor.get());
            editor.reset();
            processor.releaseResources();
        }
}
}
