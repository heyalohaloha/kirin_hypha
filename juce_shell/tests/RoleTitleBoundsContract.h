#pragma once
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaTextStyle.h"
#include "../src/HyphaLanguage.h"
#include <stdexcept>

namespace hypha::tests::role_title_bounds
{
inline void require (bool ok, const char* message)
{
    if (! ok) throw std::runtime_error (message);
}
inline void verify()
{
    int cases = 0;
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (const auto role : { observatory::Role::pre, observatory::Role::post })
            for (const auto preset : observatory::sizePresets)
                for (const bool vu : { false, true })
                {
                    observatory::View view (role);
                    view.setSize (preset.width, preset.height);
                    view.setManualHybridVuVisible (vu);
                    const auto text = juce::String (role == observatory::Role::post ? "POST" : "PRE");
                    const auto context = presentation::forEditor (preset.width, preset.height);
                    const auto font = labelFont (context, typography::TextRole::shellTitle)
                                          .withExtraKerningFactor (vu ? 0.12f : 0.0f);
                    juce::Image image (juce::Image::ARGB, preset.width, preset.height, true);
                    juce::Graphics graphics (image);
                    text_style::ShownTextLog log;
                    view.paintEntireComponent (graphics, true);
                    juce::Rectangle<float> title, brand;
                    for (int i = 0; i < log.texts().size(); ++i)
                    {
                        const auto box = log.areas()[static_cast<size_t> (i)];
                        // Parent header paints first; child selector logs use child-local coordinates.
                        if (box.getY() > preset.height / 5.0f) continue;
                        if (log.texts()[i] == text && title.isEmpty()) title = box;
                        if (log.texts()[i] == "HYPHA" || log.texts()[i] == "H Y P H A") brand = box;
                    }
                    require (! title.isEmpty() && ! brand.isEmpty(), "role and brand titles must be painted");
                    const auto width = text_style::shownWidth (font, text);
                    std::cout << "ROLE_TITLE " << text << ' ' << preset.width << " vu=" << vu
                              << " lang=" << int (language) << " glyph=" << width
                              << " box=" << title.getWidth() << std::endl;
                    const auto path = juce::SystemStats::getEnvironmentVariable ("KIRIN_ROLE_TITLE_PREVIEW_DIR", {});
                    if (path.isNotEmpty())
                    {
                        const juce::File directory (path);
                        require (directory.createDirectory().wasOk(), "create role-title preview directory");
                        auto output = directory.getChildFile (text.toLowerCase() + "-" + juce::String (preset.width)
                            + (vu ? "-vu-" : "-level-") + (language == i18n::Language::english ? "en" : "ja") + ".png").createOutputStream();
                        require (output && output->setPosition (0) && output->truncate().wasOk()
                                 && juce::PNGImageFormat().writeImageToStream (image, *output), "write role-title preview");
                    }
                    require (width <= title.getWidth() + 0.01f, "role title must fit without losing its last glyph");
                    require (! title.intersects (brand), "role and brand title boxes must not overlap");
                    require (image.getBounds().toFloat().contains (title), "role title stays inside the editor");
                    const auto brandText = vu && preset.width >= 700 ? "H Y P H A" : "HYPHA";
                    const auto brandFont = labelFont (context, typography::TextRole::shellTitle);
                    require (text_style::shownWidth (brandFont, brandText) <= brand.getWidth() + 0.01f,
                             "brand title must also fit without trading one clipped word for another");
                    ++cases;
                }
    }
    require (cases == 40, "all role, language, size and mode cases ran");
    std::cout << "Role title bounds: " << cases << " cases PASS\n";
}
}
