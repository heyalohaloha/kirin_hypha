#pragma once

#include "AttackPaintColourLog.h"
#include "AttackUiLaneContract.h"
#include "AttackV2Fixtures.h"
#include "../src/HyphaAttackLoupePainter.h"
#include "../src/HyphaAttackStage.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaMaterialCache.h"
#include "../src/HyphaTextStyle.h"

namespace hypha::attack_ui_test::loupe_paint_contract
{
inline juce::Image render (const KirinAttackDetail* pre, const KirinAttackDetail* post,
                           presentation::Context context, int width, int height,
                           float dpi = 1.0f, PaintColourLog* log = nullptr)
{
    juce::Image image (juce::Image::RGB, juce::roundToInt (width * dpi),
                       juce::roundToInt (height * dpi), true);
    PaintColourContext recorder (image, log);
    juce::Graphics g (recorder);
    g.addTransform (juce::AffineTransform::scale (dpi));
    g.fillAll (BG);
    attack_loupe::paintPanel (g, { 0, 0, width, height });
    attack_loupe::paint (g, { 0, 0, width, height }, pre, post, context);
    return image;
}

// A constant measured envelope makes the submitted stroke's height its actual thickness.
// It stays separate from the RMS steps, grid, labels and short legend samples. JUCE expands
// strokes into fill paths before the image context receives them; no painter helper is mocked.
inline const PaintColourLog::PathInk* envelope (const PaintColourLog& log, juce::Colour colour)
{
    const PaintColourLog::PathInk* found = nullptr;
    for (const auto& ink : log.paths)
    {
        const auto bounds = ink.path.getBounds();
        if (ink.colour.withAlpha (1.0f) == colour && bounds.getWidth() > 180
            && bounds.getHeight() < 4 && (!found || ink.colour.getAlpha() > found->colour.getAlpha()))
            found = &ink;
    }
    return found;
}

inline int runs (const juce::Path& path)
{
    const auto bounds = path.getBounds();
    bool previous = false;
    int count = 0;
    for (float x = bounds.getX() + 0.25f; x < bounds.getRight(); x += 0.5f)
    {
        const bool inside = path.contains (x, bounds.getCentreY());
        if (inside && !previous) ++count;
        previous = inside;
    }
    return count;
}

inline float minimumGap (const juce::Path& path)
{
    const auto bounds = path.getBounds();
    float gapStart = 0, minimum = bounds.getWidth();
    bool previous = false, seenInk = false;
    for (float x = bounds.getX() + 0.25f; x < bounds.getRight(); x += 0.5f)
    {
        const bool inside = path.contains (x, bounds.getCentreY());
        if (inside && !previous && seenInk) minimum = std::min (minimum, x - gapStart);
        if (!inside && previous) gapStart = x;
        seenInk = seenInk || inside;
        previous = inside;
    }
    return minimum;
}

inline bool readableLabel (const text_style::ShownTextLog& labels, const PaintColourLog& ink,
                           const juce::String& text, juce::Colour colour, float minimumHeight)
{
    for (int i = 0; i < labels.texts().size(); ++i)
    {
        if (labels.texts()[i] != text) continue;
        const auto area = labels.areas()[static_cast<std::size_t> (i)];
        bool drawn = false;
        for (const auto& glyph : ink.glyphs)
            if (area.contains (glyph.position))
            {
                if (glyph.colour != colour || glyph.fontHeight < minimumHeight) return false;
                drawn = true;
            }
        return drawn;
    }
    return false;
}

inline juce::Image composite (const KirinAttackDetail& detail, presentation::Context context,
                              float inheritedOpacity, PaintColourLog* log = nullptr)
{
    juce::Image cache (juce::Image::ARGB, 300, 134, true);
    { juce::Graphics g (cache); attack_loupe::paint (g, cache.getBounds(), &detail, &detail, context); }
    juce::Image image (juce::Image::RGB, 300, 134, true);
    PaintColourContext recorder (image, log);
    juce::Graphics g (recorder); g.fillAll (BG);
    g.setColour (COL_NORMAL.withAlpha (inheritedOpacity));
    attack_loupe::paintPanel (g, image.getBounds());
    g.drawImageAt (cache, 0, 0);
    return image;
}

inline bool verify()
{
    bool ok = true;
    const auto check = [&] (bool passed, const char* message, int editor, float dpi) {
        if (!passed) std::cerr << "HIT paint editor=" << editor << " dpi=" << dpi << ": " << message << '\n';
        ok = passed && ok;
    };
    constexpr std::array<float, 5> labelHeights { 11, 11, 11.5f, 13, 16 };
    auto detail = laneDetail (192000);
    std::fill (std::begin (detail.shape), std::end (detail.shape), 0.25f);
    detail.attack_rms_dbfs = -36; detail.body_rms_dbfs = -48;
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        i18n::ScopedLanguage locale (language);
        for (std::size_t index = 0; index < observatory::sizePresets.size(); ++index)
        {
            const auto& preset = observatory::sizePresets[index];
            const auto context = presentation::forEditor (preset.width, preset.height);
            for (const auto extent : { juce::Point<int> { 232, 156 }, juce::Point<int> { 300, 134 } })
            for (auto dpi : { 1.0f, 2.0f }) for (int sides : { 1, 2, 3 })
            {
                PaintColourLog ink;
                text_style::ShownTextLog labels;
                const auto image = render (sides & 1 ? &detail : nullptr, sides & 2 ? &detail : nullptr,
                                           context, extent.x, extent.y, dpi, &ink);
                const auto pre = envelope (ink, juce::Colour (attack_ui::preTraceColour));
                const auto post = envelope (ink, juce::Colour (attack_ui::waveformColour));
                if (sides & 1)
                {
                    check (pre && pre->colour == juce::Colour (attack_ui::preTraceColour),
                           "PRE envelope uses opaque ivory", preset.width, dpi);
                    check (pre && std::abs (pre->path.getBounds().getHeight() - 1.8f) < 0.01f,
                           "PRE envelope is 1.8 logical pixels", preset.width, dpi);
                    check (pre && runs (pre->path) > 10 && minimumGap (pre->path) >= 3.0f,
                           "PRE envelope has visible gaps over the solid POST", preset.width, dpi);
                }
                else check (!pre, "absent PRE has no envelope", preset.width, dpi);
                if (sides & 2)
                {
                    check (post && post->colour == juce::Colour (attack_ui::waveformColour),
                           "POST envelope uses opaque champagne gold", preset.width, dpi);
                    check (post && std::abs (post->path.getBounds().getHeight() - 2.6f) < 0.01f,
                           "POST envelope is 2.6 logical pixels", preset.width, dpi);
                    check (post && runs (post->path) == 1,
                           "POST envelope is continuous", preset.width, dpi);
                }
                else check (!post, "absent POST has no envelope", preset.width, dpi);
                check (labels.texts().contains ("HIT 150 ms") && labels.texts().contains ("-20 ms")
                    && labels.texts().contains ("+130 ms"), "fixed time span labels remain visible", preset.width, dpi);
                bool readableTicks = false;
                for (const auto& rect : ink.rects)
                    readableTicks = readableTicks || (rect.colour == COL_TEXT_SECONDARY && rect.area.getHeight() >= 2.5f);
                for (const auto* label : { "HIT 150 ms", "-20 ms", "+130 ms" })
                    check (readableLabel (labels, ink, label, COL_NORMAL, labelHeights[index]),
                           "each title and axis label uses readable ivory and semantic label size", preset.width, dpi);
                check (readableTicks, "graticule uses readable secondary text colour", preset.width, dpi);
                if (sides == 3)
                    check (labels.texts().contains ("PRE") && labels.texts().contains ("POST"),
                           "legend identifies both line styles", preset.width, dpi);
                if (sides & 1)
                    check (readableLabel (labels, ink, "PRE", juce::Colour (attack_ui::preTraceColour), labelHeights[index]),
                           "PRE legend uses its opaque ivory", preset.width, dpi);
                if (sides & 2)
                    check (readableLabel (labels, ink, "POST", juce::Colour (attack_ui::waveformColour), labelHeights[index]),
                           "POST legend uses its opaque gold", preset.width, dpi);
                const auto font = monoFont (context, typography::TextRole::legend, typography::Composition::visualization)
                    .withExtraKerningFactor (attack_stage::captionTracking (context));
                for (int i = 0; i < labels.texts().size(); ++i)
                    check (text_style::shownWidth (font, labels.texts()[i])
                               <= labels.areas()[static_cast<std::size_t> (i)].getWidth(),
                           "labels fit their cells without truncation", preset.width, dpi);
                check (image.isValid(), "paint reaches the real image", preset.width, dpi);
            }
            const auto empty = render (nullptr, nullptr, context, 300, 134);
            for (int fault : { 0, 1, 2, 3, 4 })
            {
                auto invalid = detail;
                if (fault == 0) invalid.sample_rate = 0;
                if (fault == 1) invalid.bin_frames = 0;
                if (fault == 2) invalid.shape_count = 1;
                if (fault == 3) invalid.shape_count = KIRIN_ATTACK_SHAPE_CAPACITY + 1;
                if (fault == 4) invalid.shape_end_sample = invalid.event_sample;
                check (differences (empty, render (&invalid, &invalid, context, 300, 134)) == 0,
                       "unusable data keeps NO HIT", preset.width, 1);
            }
            const auto before = render (&detail, &detail, context, 300, 134);
            render (nullptr, nullptr, context, 300, 134);
            check (differences (before, render (&detail, &detail, context, 300, 134)) == 0,
                   "restoring a hit reproduces its drawing", preset.width, 1);
            PaintColourLog compositeInk;
            const auto faintInput = composite (detail, context, 0.17f, &compositeInk);
            check (!compositeInk.images.empty() && compositeInk.images.back().opacity == 1.0f,
                   "cached hit is submitted at full opacity", preset.width, 1);
            check (differences (faintInput, composite (detail, context, 1.0f)) == 0,
                   "cached hit is independent of preceding stroke alpha", preset.width, 1);
        }
    }
    std::cout << "HIT paint contract: " << (ok ? "PASS" : "FAIL") << '\n';
    return ok;
}

inline bool writePng (const juce::File& file, const juce::Image& image)
{
    juce::FileOutputStream stream (file);
    juce::PNGImageFormat png;
    return stream.openedOk() && stream.setPosition (0) && stream.truncate().wasOk()
        && png.writeImageToStream (image, stream);
}

// Optional review artifacts use the production component, its responsive geometry and immutable
// summary packets. Both sides intentionally coincide, so colour alone cannot conceal a trace.
inline bool writePreviews()
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("KIRIN_ATTACK_LOUPE_PREVIEW_DIR", {});
    if (path.isEmpty()) return true;
    const juce::File directory (path);
    if (!directory.createDirectory().wasOk()) return false;
    material_cache::Lifetime lifetime;
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        i18n::ScopedLanguage locale (language);
        for (const auto& preset : observatory::sizePresets)
        {
            const auto context = presentation::forEditor (preset.width, preset.height);
            const auto shell = observatory::shellLayout (observatory::Role::post, preset,
                                                         observatory::GuidePresence::absent);
            const auto suffix = juce::String (preset.width) + "-" + (language == i18n::Language::english ? "en" : "ja");
            const auto detail = laneDetail (480000);
            for (auto dpi : { 1.0f, 2.0f })
            {
                const auto ending = suffix + (dpi > 1 ? "@2x.png" : ".png");
                if (!writePng (directory.getChildFile ("hit-overlap-" + ending),
                               render (&detail, &detail, context, 300, 134, dpi))) return false;
                if (!writePng (directory.getChildFile ("no-hit-" + ending),
                               render (nullptr, nullptr, context, 300, 134, dpi))) return false;
                for (const auto band : { 0, 5 })
                {
                    auto view = std::make_unique<AttackComponent>();
                    view->setPresentationContext (context);
                    view->setSize (shell.body.width, shell.body.height - observatory::timeNavigationHeight (preset.density));
                    view->setOverlayMode (true); view->setBand (static_cast<std::uint8_t> (band));
                    auto packet = band == 0 ? attack_v2_test::allSummary (0) : attack_v2_test::denseSummary();
                    packet.header.cutoff_sample = detail.shape_end_sample;
                    if (band == 0)
                    {
                        packet.has_representative_pre = packet.has_representative_post = 1;
                        packet.representative_pre = packet.representative_post = detail;
                    }
                    else
                    {
                        const auto overlap = [] (auto& points) {
                            for (auto& point : points)
                            {
                                point.post_mean = point.pre_mean;
                                point.post_min = point.pre_min; point.post_max = point.pre_max;
                            }
                        };
                        overlap (packet.head); overlap (packet.tail);
                        for (std::size_t lane = 0; lane < 4; ++lane)
                        {
                            const auto unit = packet.lanes[lane].whole_interval.unit;
                            auto zero = attack_v2_test::interval (0, 0); zero.unit = unit;
                            packet.lanes[lane].whole_interval = zero; packet.lanes[lane].exact_median = 0;
                            for (auto& row : packet.evidence) row[lane].interval = zero;
                        }
                    }
                    auto h = packet.header; h.kind = 4; h.band = 0; h.struct_size = sizeof (KirinAttackNavigationV2);
                    auto wave = std::make_unique<KirinAttackWaveformBatch>();
                    wave->count = KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY;
                    for (std::uint32_t i = 0; i < wave->count; ++i)
                    {
                        auto& p = wave->points[i]; p.generation = h.source.generation;
                        p.sample_rate = h.source.sample_rate; p.channels = 2;
                        p.start_sample = h.cutoff_sample - 288000 + i * 480; p.end_sample = p.start_sample + 480;
                        float amplitude = 0.001f;
                        for (std::uint32_t hit = 0; hit < packet.cohort_count; ++hit)
                        {
                            const auto age = p.end_sample - packet.events[hit].event_sample;
                            if (age >= 0) amplitude += 0.5f * std::exp (-static_cast<float> (age) / 1800);
                        }
                        p.rms_dbfs = 20 * std::log10 (amplitude);
                    }
                    if (!view->setNavigationV2 (h, {packet.events, packet.events + packet.cohort_count}, *wave, *wave, 0)
                        || !view->setSummarySnapshotV2 (packet, 0)) return false;
                    juce::Image image (juce::Image::RGB, juce::roundToInt (view->getWidth() * dpi),
                                       juce::roundToInt (view->getHeight() * dpi), true);
                    juce::Graphics g (image); g.fillAll (BG); g.addTransform (juce::AffineTransform::scale (dpi));
                    view->paintEntireComponent (g, true);
                    if (!writePng (directory.getChildFile ((band == 0 ? "drum-all-overlap-" : "drum-band-overlap-") + ending), image))
                        return false;
                }
            }
        }
    }
    return true;
}
}
