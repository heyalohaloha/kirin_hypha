#pragma once

#include "../src/PluginEditor.h"
#include "../src/HyphaChainTimingPreference.h"
#include "../src/HyphaEditorSizeConstrainer.h"
#include "../src/HyphaFeedbackStrip.h"
#include "../src/HyphaLanguage.h"
#include "LanguageMisses.h"
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

struct SyncLanguage
{
    using Type = void (KirinHyphaEditor::*) (bool);
    friend Type privateMember (SyncLanguage);
};
template struct PrivateAccess<SyncLanguage, &KirinHyphaEditor::syncLanguage>;

struct UpdatePost
{
    using Type = void (KirinHyphaEditor::*)();
    friend Type privateMember (UpdatePost);
};
template struct PrivateAccess<UpdatePost, &KirinHyphaEditor::updatePost>;

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
// the bottom edge of the body: above the analysis page that owns the body and receiving the
// pointer. Without feedback the strip carries the footer's short status (WAITING here, before any
// audio) and the domain cycle keeps its whole row. At the larger sizes the footer keeps both.
inline void verifyFoldedFeedbackStrip()
{
    const juce::String message ("Jungle Mode changed for this session only");
    // In Japanese too (INV-S40): the strip carries the translated notice whole.
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
        for (const auto role : { Processor::Role::Post, Processor::Role::Pre })
            for (const auto& preset : observatory::sizePresets)
            {
                const i18n::ScopedLanguage shown (language);
                const auto reviewPrefix = language == i18n::Language::japanese ? "ja_" : "";
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
                require (! folded || view->sessionBounds().getWidth() == 0, "the cycle keeps its whole row");

                (shipping->*privateMember (ShowToast {})) (message);
                const auto body = view->bodyBounds();
                const auto area = view->statusStripBounds();
                const bool overflow = text_style::shownWidth (monoFont (view->presentationContext(),
                    typography::TextRole::action), message) > view->sessionBounds().getWidth() - 14;
                const bool usesStrip = folded || overflow;
                require (strip->isVisible() == usesStrip, "folded or overflowing feedback uses the full-width strip");
                require (view->feedbackDetailsAnchor().isVisible() == ! usesStrip,
                         "fitting feedback stays in the footer");
                if (usesStrip)
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
                    writeReview (*editor, reviewPrefix + juce::String (role == Processor::Role::Post ? "post" : "pre")
                                              + "_feedback_strip_" + juce::String (preset.width));
                }
                else
                    require (area == view->sessionBounds(), "without folding, status stays in the footer");

                (shipping->*privateMember (ShowToast {})) ({});
                require (view->footerStatus() == "WAITING", "no audio yet: the status is WAITING");
                require (strip->isVisible() == folded && (! folded || strip->text() == "WAITING"),
                         "after the feedback the strip returns to the short status");
                processor.editorBeingDeleted (editor.get());
                editor.reset();
                processor.releaseResources();
            }
}

// The chain timing on POST's footer follows the user's switch (INV-LC25): off by default, the
// readout reaches the shipping view on the next update once it is on, and never reaches PRE.
// Before any audio it is dashes, so WAITING keeps the folded strip.
inline void verifyChainTimingFooterSwitch()
{
    auto& preference = ChainTimingFooterPreference::shared();
    require (! preference.isEnabled(), "the footer chain time is off by default");
    for (const bool enabled : { false, true })
    {
        require (preference.setEnabled (enabled), "the switch is saved in the sandbox");
        for (const auto role : { Processor::Role::Post, Processor::Role::Pre })
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
            Processor processor (role);
            processor.prepareToPlay (48'000, 960);
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorIfNeeded());
            auto* shipping = dynamic_cast<KirinHyphaEditor*> (editor.get());
            auto* view = component<observatory::View> (*editor);
            require (shipping != nullptr && view != nullptr, "editor and view exist");
            editor->setSize (300, 200);
            if (role == Processor::Role::Post)
                (shipping->*privateMember (UpdatePost {})) ();
            const bool shown = enabled && role == Processor::Role::Post;
            require (view->chainReadoutForTest() == (shown ? "CHAIN LOAD --" : ""),
                     "only POST shows the readout, only while it is turned on");
            if (role == Processor::Role::Post)
            {
                auto* strip = dynamic_cast<FeedbackStrip*> (find (*editor, "feedback-strip"));
                require (strip != nullptr && strip->text() == "WAITING", "WAITING keeps the folded strip");
            }
            processor.editorBeingDeleted (editor.get());
            editor.reset();
            processor.releaseResources();
        }
    }
    require (preference.setEnabled (false), "the sandbox switch is turned off again");
}

// The language changes while an editor is open (INV-S40): the editor lays itself out again and
// draws in the new language, and switching back draws exactly what it drew before. With
// KIRIN_HYPHA_COMPACT_REVIEW_DIR set, each domain is also written in Japanese at 100%, 125% and 300%.
inline void verifyLanguageSwitch()
{
    const auto samePixels = [] (const juce::Image& left, const juce::Image& right) {
        for (int y = 0; y < left.getHeight(); ++y)
            for (int x = 0; x < left.getWidth(); ++x)
                if (left.getPixelAt (x, y) != right.getPixelAt (x, y)) return false;
        return true;
    };
    for (const auto& preset : { observatory::sizePresets[0], observatory::sizePresets[1],
                                observatory::sizePresets.back() })
        for (const auto& [domain, name] : { std::pair { observatory::Domain::level, "level" },
                                            std::pair { observatory::Domain::time, "time" },
                                            std::pair { observatory::Domain::frequency, "freq" },
                                            std::pair { observatory::Domain::space, "space" },
                                            std::pair { observatory::Domain::reference, "ref" } })
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
            Processor processor (Processor::Role::Post);
            processor.setObservatoryDomainPreference (observatory::stateValue (domain));
            processor.prepareToPlay (48'000, 960);
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorIfNeeded());
            editor->setSize (preset.width, preset.height);
            editor->setVisible (true);
            auto* shipping = dynamic_cast<KirinHyphaEditor*> (editor.get());
            require (shipping != nullptr, "the shipping editor opens");
            const auto snapshot = [&editor] {
                return editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            };
            snapshot(); // the first paint builds the material caches that later paints reuse
            const auto english = snapshot();
            {
                const i18n::ScopedLanguage japanese (i18n::Language::japanese);
                const language_misses::Scope watching;
                (shipping->*privateMember (SyncLanguage {})) (true);
                require (! samePixels (english, snapshot()), "an open editor draws the new language");
                writeReview (*editor, "ja_editor_" + juce::String (name) + "_" + juce::String (preset.width));
            }
            (shipping->*privateMember (SyncLanguage {})) (true);
            require (samePixels (english, snapshot()), "switching back draws exactly what it drew");
            processor.editorBeingDeleted (editor.get());
            editor.reset();
            processor.releaseResources();
        }
    language_misses::report ("editor");
}

// Above 300% the shipping editor takes only the magnified steps of its display (450% and 600% on
// DPI 2): a corner drag lands on the nearest step, keeping the edge not dragged, and a magnified
// editor lays the Inspection View out at 900 x 600 and scales it to fill the window.
inline void verifyMagnifiedEditor()
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
    Processor processor (Processor::Role::Post);
    processor.prepareToPlay (48'000, 960);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorIfNeeded());
    auto* rule = dynamic_cast<EditorSizeConstrainer*> (editor->getConstrainer());
    require (rule != nullptr, "the editor sizes itself by the step rule");
    rule->setDisplayScale (2.0f);
    rule->setSizeLimits (300, 200, 2700, 1800);
    const auto drag = [rule] (juce::Rectangle<int> bounds, bool left, bool top) {
        rule->checkBounds (bounds, { 0, 0, 900, 600 }, { 0, 0, 4000, 4000 }, top, left, ! top, ! left);
        return bounds;
    };
    require (drag ({ 0, 0, 720, 480 }, false, false) == juce::Rectangle<int> (0, 0, 720, 480),
             "up to 300% any 3:2 size stays");
    require (drag ({ 0, 0, 1300, 867 }, false, false) == juce::Rectangle<int> (0, 0, 1350, 900),
             "past 300% the size lands on the nearest step");
    require (drag ({ 0, 0, 1000, 667 }, false, false) == juce::Rectangle<int> (0, 0, 900, 600),
             "just past 300% returns to 300%");
    require (drag ({ 0, 0, 1700, 1133 }, false, false) == juce::Rectangle<int> (0, 0, 1800, 1200),
             "600% is the next step on DPI 2");
    require (drag ({ -400, -267, 1300, 867 }, true, true).getBottomRight() == juce::Point<int> (900, 600),
             "a drag from the top left keeps the bottom right edge");
    require (rule->allowedSize ({ 1200, 800 }).width == 1350, "a saved size that is not a step lands on one");
    require (rule->allowedSize ({ 3600, 2400 }).width == 2700, "and the largest step within the display");

    editor->setSize (1350, 900);
    auto* view = component<observatory::View> (*editor);
    require (view != nullptr && view->getWidth() == 900 && view->getHeight() == 600,
             "the magnified editor lays the Inspection View out at 900 x 600");
    const auto transform = view->getParentComponent()->getTransform();
    require (std::abs (transform.mat00 - 1.5f) < 1.0e-6f && std::abs (transform.mat11 - 1.5f) < 1.0e-6f,
             "and scales it by 1.5 to fill 1350 x 900");
    auto* sizeButton = dynamic_cast<juce::Button*> (&view->sizeMenuAnchor());
    require (sizeButton != nullptr && sizeButton->getButtonText() == "450%", "the size reads 450%");
    processor.editorBeingDeleted (editor.get());
    editor.reset();
    processor.releaseResources();
}
}
