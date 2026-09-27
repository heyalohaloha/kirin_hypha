#pragma once

#include "CompactReviewShowcase.h"

#include <cmath>

// Look review of the pages the 300% report named, at the full sizes (200% and 300%): TIME
// HISTORY with its PLR and CORR lanes, RUN, and SPACE with MONO. Steady data, four playback runs
// and a MONO shape with a low-end loss. Written only when KIRIN_HYPHA_LARGE_REVIEW_DIR names a
// directory.
namespace hypha::tests
{
namespace large_review
{
inline std::vector<KirinMeterHistoryEntry> runs()
{
    auto result = compact_review::history();
    for (std::size_t index = 0; index < result.size(); ++index)
        result[index].run_id = 1u + static_cast<std::uint64_t> (index / 75u);
    return result;
}

inline KirinObservatoryFrame monoFrame (int observation)
{
    auto frame = compact_review::frame();
    auto& meter = frame.meter;
    meter.observed_frames += static_cast<std::uint64_t> (observation) * 4'800u;
    meter.mono_sum_band_count = static_cast<std::uint8_t> (KIRIN_MONO_SUM_BAND_COUNT);
    meter.mono_sum_approximate_below_hz = 30.0f;
    for (std::size_t band = 0u; band < KIRIN_MONO_SUM_BAND_COUNT; ++band)
    {
        const auto ratio = KIRIN_MONO_SUM_MAX_HZ / KIRIN_MONO_SUM_MIN_HZ;
        const auto centreHz = KIRIN_MONO_SUM_MIN_HZ
            * std::pow (ratio, (static_cast<float> (band) + 0.5f)
                                   / static_cast<float> (KIRIN_MONO_SUM_BAND_COUNT));
        const auto octavesFrom80 = std::log2 (centreHz / 80.0f);
        meter.mono_sum_db[band] = -0.7f - 17.0f * std::exp (-octavesFrom80 * octavesFrom80 * 2.0f);
    }
    return frame;
}
}

inline bool writeLargePageReview()
{
    const auto* path = std::getenv ("KIRIN_HYPHA_LARGE_REVIEW_DIR");
    if (path == nullptr)
        return true;
    const juce::File directory { path };
    if (! directory.createDirectory())
        return false;
    using namespace compact_review;
    material_cache::Lifetime editorMaterial;
    bool written = true;
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage shown (language);
        const auto prefix = language == i18n::Language::japanese ? juce::String ("ja_") : juce::String();
        for (const auto size : { std::array<int, 2> { 600, 400 }, std::array<int, 2> { 900, 600 } })
        {
            const auto name = [&directory, &prefix, size] (const char* page)
            { return directory.getChildFile (prefix + juce::String (size[0]) + "_" + page + ".png"); };
            observatory::View shell (observatory::Role::post);
            shell.setSize (size[0], size[1]);
            for (int observation = 0; observation < 60; ++observation)
                shell.setObservatoryFrame (large_review::monoFrame (observation), true);
            shell.setWatchDisplay (watch(), true);
            shell.setHistory (large_review::runs());
            shell.setConnection ("PAIR DRUM", COL_LED_BLUE, observatory::ConnectionState::paired);
            shell.setDomain (observatory::Domain::time);
            written = written && freq_showcase::writePng (name ("time_history"), renderShell (shell));
            shell.setRunSummaryMode (true);
            written = written && freq_showcase::writePng (name ("run"), renderShell (shell));
            shell.setRunSummaryMode (false);
            shell.setDomain (observatory::Domain::space);
            written = written && freq_showcase::writePng (name ("space"), renderShell (shell));
        }
    }
    return written;
}
}
