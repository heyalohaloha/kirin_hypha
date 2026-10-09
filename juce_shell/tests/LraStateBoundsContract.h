#pragma once
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaLanguage.h"
#include <iostream>

namespace hypha::tests::lra_state_bounds
{
inline void verify (KirinObservatoryFrame frame)
{
    int checked = 0;
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (const auto role : { observatory::Role::pre, observatory::Role::post })
            for (const auto size : observatory::sizePresets)
            {
                observatory::View view (role);
                view.setSize (size.width, size.height);
                frame.lra_state = KIRIN_LRA_WARMING;
                frame.lra_elapsed_seconds = 0;
                view.setObservatoryFrame (frame, true);
                const auto baseline = view.createComponentSnapshot (view.getLocalBounds(), true, 2.0f);
                const auto isLra = [&view] (int x, int y) { return view.metricHelpAt ({x, y}).contains ("(LRA)"); };
                juce::Point<int> point (-1, -1);
                for (int y = 0; y < size.height && point.x < 0; y += 8)
                    for (int x = 0; x < size.width; x += 8)
                        if (isLra (x, y)) { point = {x, y}; break; }
                if (point.x < 0) continue; // Compact layouts do not display this card.
                int left = point.x, right = point.x, top = point.y, bottom = point.y;
                while (left > 0 && isLra (left - 1, point.y)) --left;
                while (right + 1 < size.width && isLra (right + 1, point.y)) ++right;
                while (top > 0 && isLra (point.x, top - 1)) --top;
                while (bottom + 1 < size.height && isLra (point.x, bottom + 1)) ++bottom;
                const juce::Rectangle<int> card (left * 2, top * 2,
                    (right - left + 1) * 2, (bottom - top + 1) * 2);
                for (const double seconds : { 5.0, 34.0, 999.0 })
                {
                    frame.lra_elapsed_seconds = seconds;
                    view.setObservatoryFrame (frame, true);
                    const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 2.0f);
                    int inside = 0, outside = 0;
                    for (int y = 0; y < image.getHeight(); ++y)
                        for (int x = 0; x < image.getWidth(); ++x)
                            if (image.getPixelAt (x, y) != baseline.getPixelAt (x, y))
                                card.contains (x, y) ? ++inside : ++outside;
                    if (inside == 0 || outside != 0)
                    {
                        std::cerr << "LRA state escaped its card: " << size.width << 'x' << size.height
                                  << " seconds=" << seconds << " outside=" << outside << '\n';
                        std::exit (1);
                    }
                    const auto directory = juce::SystemStats::getEnvironmentVariable (
                        "KIRIN_HYPHA_LRA_PREVIEW_DIR", {});
                    if (directory.isNotEmpty() && size.width == 900)
                    {
                        const auto name = juce::String (role == observatory::Role::pre ? "pre-" : "post-")
                            + (language == i18n::Language::english ? "en-" : "ja-")
                            + juce::String (int (seconds)) + "-retina.png";
                        auto stream = juce::File (directory).getChildFile (name).createOutputStream();
                        if (!stream || !juce::PNGImageFormat().writeImageToStream (image, *stream)) std::exit (1);
                    }
                    ++checked;
                }
            }
    }
    if (checked == 0) std::exit (1);
    std::cout << "LRA state bounds: " << checked << " Retina images, zero pixels outside card PASS\n";
}
}
