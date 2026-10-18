#pragma once

#include "ReadmeMediaReview.h"
#include "EditorReachabilityCheck.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

// 2026-10-18: the REF page's B (the song list and its set), C (the Check tabs) and V at every editor size,
// display scale and language: every control it shows lies inside the page, a press at its centre reaches
// it and Tab reaches it. The page is laid out at the body the shipping view gives it at that size.
namespace hypha::tests
{
inline void verifyReferenceReachability()
{
    int cells = 0;
    constexpr std::array<std::pair<int, int>, 8> sizes {{
        { 300, 200 }, { 303, 202 }, { 375, 250 }, { 450, 300 }, { 600, 400 }, { 750, 500 }, { 897, 598 }, { 900, 600 } }};
    // One page kept while the editor is resized, as the editor does: from 900 x 600 down to 300 x 200 and back
    // (a control that only the largest size shows must not stay behind at a smaller one).
    std::vector<std::pair<int, int>> walk (sizes.rbegin(), sizes.rend());
    walk.insert (walk.end(), sizes.begin() + 1, sizes.end());
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage shown (language);
        for (const auto& [page, state] : { std::pair { "B", readme_media::songPage() }, std::pair { "C", readme_media::checkPage() },
                                           std::pair { "V", readme_media::versionPage() } })
        {
            reference_ui::Component panel;
            panel.setVisible (true);
            panel.setState (state);
            for (const auto& [width, height] : walk)
            {
                observatory::View shell (observatory::Role::post);
                shell.setSize (width, height);
                shell.setDomain (observatory::Domain::reference);
                shell.setExternalAnalysisBodyActive (true);
                const auto body = shell.analysisBodyBounds();
                panel.setPresentationContext (presentation::forEditor (width, height));
                panel.setSize (body.getWidth(), body.getHeight());
                cells += editor_reachability::checkAtDisplayScales (panel, [&] (float scale, const juce::String& problem)
                {
                    std::cerr << "REF " << page << ' ' << width << "x" << height << " @" << scale
                              << (language == i18n::Language::japanese ? " ja: " : " en: ") << problem << '\n';
                    std::exit (EXIT_FAILURE);
                }) > 0 ? 1 : 0;
            }
        }
    }
    std::cout << "Reference reach: PASS (" << cells << " pages x sizes x languages, each at 4 display scales)\n";
}
}
