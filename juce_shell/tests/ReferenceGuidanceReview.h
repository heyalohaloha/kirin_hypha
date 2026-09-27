#pragma once

#include "CompactReviewShowcase.h"
#include "../src/HyphaReferenceAccessPanel.h"
#include "../src/HyphaReferenceComponent.h"
#include "ReferenceGuideStates.h"

// Look review of the REFERENCE page in each state met on the way to B and C, as the editor
// composes it: the POST shell on REF with the Reference panel in its body. Each state's status is
// the one the editor derives. Written only when KIRIN_HYPHA_REFERENCE_REVIEW_DIR names a directory.
namespace hypha::tests
{
namespace reference_review
{
inline juce::Image render (const Case& item, int width, int height)
{
    using namespace compact_review;
    observatory::View shell (observatory::Role::post);
    shell.setSize (width, height);
    shell.setObservatoryFrame (frame(), true);
    shell.setWatchDisplay (watch(), true);
    shell.setHistory (history());
    shell.setConnection ("PAIR DRUM", COL_LED_BLUE, observatory::ConnectionState::paired);
    shell.setDomain (observatory::Domain::reference);
    shell.setExternalAnalysisBodyActive (true);
    auto image = renderShell (shell);
    const auto body = shell.analysisBodyBounds();
    const auto context = presentation::forEditor (width, height);
    if (item.access)
    {
        reference_ui::AccessPanel panel;
        panel.setPresentationContext (context);
        panel.setSize (body.getWidth(), body.getHeight());
        paintInto (image, panel, body);
    }
    else
    {
        reference_ui::Component panel;
        panel.setPresentationContext (context);
        panel.setSize (body.getWidth(), body.getHeight());
        panel.setState (item.state);
        paintInto (image, panel, body);
    }
    shell.setExternalAnalysisBodyActive (false);
    return image;
}
}

inline bool writeReferenceReview()
{
    const auto* path = std::getenv ("KIRIN_HYPHA_REFERENCE_REVIEW_DIR");
    if (path == nullptr)
        return true;
    const juce::File directory { path };
    if (! directory.createDirectory())
        return false;
    material_cache::Lifetime editorMaterial;
    bool written = true;
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage shown (language);
        const auto prefix = language == i18n::Language::japanese ? juce::String ("ja_") : juce::String();
        for (const auto& preset : observatory::sizePresets)
            for (const auto& item : reference_review::cases())
                written = written && freq_showcase::writePng (
                    directory.getChildFile (prefix + juce::String (preset.width) + "_" + item.name + ".png"),
                    reference_review::render (item, preset.width, preset.height));
    }
    return written;
}
}
