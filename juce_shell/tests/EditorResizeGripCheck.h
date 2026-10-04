#pragma once

// 2026-10-04: the editor's own corner grip (Daisuke chose it for Mac and Windows). Studio on
// Windows and Pro Tools give a plug-in window no frame to drag, so this is the corner a user holds.
#include "EditorProductChecks.h"
#include "../src/HyphaEditorResizeGrip.h"

namespace hypha::tests::editor_product
{
inline void verifyResizeGrip()
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
    Processor processor (Processor::Role::Post);
    processor.prepareToPlay (48'000, 960);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorIfNeeded());
    auto* grip = dynamic_cast<EditorResizeGrip*> (find (*editor, "editor-resize-grip"));
    auto* view = component<observatory::View> (*editor);
    auto* rule = dynamic_cast<EditorSizeConstrainer*> (editor->getConstrainer());
    require (grip != nullptr && view != nullptr && rule != nullptr, "the editor carries its own corner grip");
    editor->setVisible (true); // hit tests answer only for a shown editor; showing it reads the display
    rule->setDisplayScale (2.0f);
    rule->setSizeLimits (300, 200, 2700, 1800);

    std::vector<observatory::EditorSize> sizes;
    for (const auto preset : observatory::sizePresets)
        sizes.push_back ({ preset.width, preset.height });
    sizes.push_back ({ 1350, 900 }); // 450 %, the 300 % layout magnified by 1.5
    for (const auto size : sizes)
    {
        editor->setSize (size.width, size.height);
        const auto corner = editor->getLocalBounds().getBottomRight();
        require (grip->isVisible() && grip->getBounds().getBottomRight() == corner
                     && grip->getWidth() == EditorResizeGrip::geometryFor (size.width, size.height).side,
                 "the grip sits in the bottom-right corner at every size");
        require (editor->getComponentAt (corner - juce::Point<int> (2, 2)) == grip,
                 "the corner itself takes the drag");
        auto& sizeButton = view->sizeMenuAnchor();
        const auto sizeArea = editor->getLocalArea (&sizeButton, sizeButton.getLocalBounds());
        require (editor->getComponentAt (sizeArea.getBottomRight() - juce::Point<int> (1, 1)) != grip
                     && editor->getComponentAt (sizeArea.getCentre()) != grip,
                 "the size button stays whole to the click");
    }
    require (grip->getWidth() == 24, "a magnified editor scales the corner with its layout");

    // A drag on the grip goes through the size rule: 3:2 from the width asked for, 100% at least.
    editor->setSize (600, 400);
    auto& target = static_cast<juce::Component&> (*grip);
    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    const juce::Point<float> down { 10.0f, 10.0f };
    const auto at = [&] (juce::Point<float> offset)
    {
        return juce::MouseEvent (source, down + offset, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, grip, grip,
                                 now, down, now, 1, offset != juce::Point<float>());
    };
    target.mouseDown (at ({}));
    target.mouseDrag (at ({ 150.0f, 40.0f }));
    require (editor->getWidth() == 750 && editor->getHeight() == 500, "a drag on the grip keeps 3:2");
    target.mouseDrag (at ({ -400.0f, -400.0f }));
    require (editor->getWidth() == 300 && editor->getHeight() == 200, "and stops at 100%");
    target.mouseUp (at ({ -400.0f, -400.0f }));
    require (grip->getBounds().getBottomRight() == editor->getLocalBounds().getBottomRight(),
             "the grip follows the corner it moved");
    processor.editorBeingDeleted (editor.get());
    editor.reset();
    processor.releaseResources();
}
}
