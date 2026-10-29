#pragma once
#include "../src/HyphaLanguage.h"
#include "../src/HyphaPairLabels.h"
#include "../src/HyphaWidgets.h"
#include "../src/HyphaTextStyle.h"
#include <stdexcept>

namespace hypha::tests::editor_product
{
inline void renderPairBadges (juce::AudioProcessorEditor& editor, EditableName& name)
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("KIRIN_PAIR_BADGE_OUTPUT", {});
    if (path.isEmpty()) return;
    const juce::File root (path);
    if (root.createDirectory().failed()) throw std::runtime_error ("pair badge directory");
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        i18n::ScopedLanguage scoped (language);
        for (int percent : { 100, 150, 200, 300 })
        {
            editor.setSize (3 * percent, 2 * percent);
            for (int state = 0; state < 3; ++state)
            {
                name.setSelectionPreview ({}, 0);
                name.setModelName (state == 0 ? "Mix" : juce::String());
                if (state == 2 && ! name.setSelectionPreview (pair_labels::candidateLabel ("Mix", "01234567"), 1))
                    throw std::runtime_error ("single candidate does not fit");
                const auto bounds = editor.getLocalArea (&name, name.getLocalBounds());
                text_style::ShownTextLog shown;
                const auto pixels = editor.createComponentSnapshot (bounds);
                const auto expected = state == 0 ? juce::String ("PRE  Mix")
                    : state == 1 ? (language == i18n::Language::english ? juce::String ("NO PRE")
                                  : juce::String::fromUTF8 (u8"PREなし"))
                    : juce::String ("+ Mix");
                if (! shown.texts().contains (expected)) throw std::runtime_error ("pair badge text is clipped");
                const char* states[] = { "paired", "unpaired", "plus" };
                const auto file = juce::String (language == i18n::Language::english ? "en-" : "jp-")
                    + states[state] + "-" + juce::String (percent) + ".png";
                juce::FileOutputStream stream (root.getChildFile (file));
                if (! stream.openedOk() || ! stream.setPosition (0) || stream.truncate().failed()
                    || ! juce::PNGImageFormat().writeImageToStream (pixels, stream))
                    throw std::runtime_error ("pair badge PNG");
                if (percent == 200)
                {
                    const auto contextRoot = root.getChildFile ("context");
                    if (contextRoot.createDirectory().failed()) throw std::runtime_error ("pair context directory");
                    const auto band = juce::Rectangle<int> (0, juce::jmax (0, bounds.getY() - 12),
                        editor.getWidth(), juce::jmin (editor.getHeight() - juce::jmax (0, bounds.getY() - 12),
                                                       bounds.getHeight() + 24));
                    const auto context = editor.createComponentSnapshot (band);
                    juce::FileOutputStream contextStream (contextRoot.getChildFile (file));
                    if (! contextStream.openedOk() || ! contextStream.setPosition (0) || contextStream.truncate().failed()
                        || ! juce::PNGImageFormat().writeImageToStream (context, contextStream))
                        throw std::runtime_error ("pair context PNG");
                }
            }
        }
    }
    name.setSelectionPreview ({}, 0);
    name.setModelName ({});
}
}
