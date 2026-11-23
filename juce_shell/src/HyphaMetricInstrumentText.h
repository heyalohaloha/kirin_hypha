#pragma once

#include "HyphaMeterGlass.h"
#include "HyphaPreSingleDialPreview.h"
#include "HyphaMetricNeedle.h"
#include "HyphaTextStyle.h"
#include <cmath>

// Where the words of the one-dial (PRE) and two-dial (POST) metric faces stand: on the lit glass, inside
// the gold rim, beside the hubs, and clear of the arcs, the ticks and one another.
namespace hypha::metric_face
{
inline presentation::Context instrumentContext (presentation::Context context) noexcept
{
    context.referenceCompact = true; // the 12 px floor, at every editor size
    return context;
}

inline juce::Font instrumentFont (const presentation::Context& context, typography::TextRole role = typography::TextRole::legend)
{ return labelFont (instrumentContext (context), role, typography::Composition::instrument); }

inline int instrumentLine (const presentation::Context& context)
{ return juce::jmax (12, static_cast<int> (std::ceil (instrumentFont (context).getHeight()))); }

inline juce::String candidateSignedNumber (double value)
{
    if (! std::isfinite (value)) return juce::String::charToString (0x2014);
    return (value > 0 ? "+" : "") + juce::String (value, 2);
}

struct CandidateWord
{
    juce::String text;
    juce::Rectangle<int> box;
};

struct CandidateWords
{
    CandidateWord headline, pre, reference, post, zero, song, notice, noticeEnd; // noticeEnd: a reason's second line
    bool noticeIsSong = false; // while the zero is prepared its place names the song
};

// The lowest pixel row of words on the glass.
inline int glassFloor (juce::Rectangle<int> chassis)
{ return static_cast<int> (std::floor (glass::y (chassis, glass::bottom))); }

// The row of the figures beside the hubs, and that row raised where a row of words must fit under it.
inline int figureRow (juce::Rectangle<int> chassis, int line)
{ return juce::roundToInt (glass::y (chassis, 630.0f) - static_cast<float> (line) * 0.5f); }
inline int raisedFigureRow (juce::Rectangle<int> chassis, int line)
{ return juce::jmin (figureRow (chassis, line), glassFloor (chassis) - 2 * line); }

inline bool candidateReady (const metric_needle::Frame& frame) noexcept
{ return frame.status == metric_needle::Status::ready && std::isfinite (frame.median); }

// The face's name, or why it has no zero. With a needle the PRE dial's name stands at its left; without
// one it stands in the open glass between the arc and the hub. POST names both dials between them (a small
// POST without a zero moves the status to its reason, in candidateWords).
inline juce::Rectangle<int> candidateHeadlineBounds (juce::Rectangle<int> chassis, bool one, bool ready,
                                                     const presentation::Context& context)
{
    const auto line = instrumentLine (context);
    if (one && ready)
    {
        const auto centre = juce::jmin (chassis.getY() + juce::roundToInt (static_cast<float> (chassis.getHeight()) * 0.495f) + 9,
                                        raisedFigureRow (chassis, line) - (line + 1) / 2);
        const auto width = juce::roundToInt (static_cast<float> (chassis.getWidth()) * 0.185f);
        return {chassis.getX() + juce::roundToInt (static_cast<float> (chassis.getWidth()) * 0.045f), centre - line / 2, width, line};
    }
    const auto width = chassis.getWidth() / 3;
    if (one)
    {
        // Two lines, this one and the reason or the song under it, centred between the arc and the hub.
        const auto middle = glass::y (chassis, (376.0f + glass::hubRow - glass::hubRadius) * 0.5f);
        return juce::Rectangle<int> (width, line).withCentre ({chassis.getCentreX(), 0})
            .withY (juce::roundToInt (middle) - line);
    }
    // Above the figures at the two arcs' inner ends, which reach furthest up at the smallest size.
    const auto dial = hybrid_vu::needle_painter::dialGeometry (hybrid_vu::needle_painter::faceBounds (chassis), 0);
    const auto end = dial.pivot + (hybrid_vu::needle_painter::dialPoint (dial, 1.0f) - dial.pivot) * 1.22f;
    const auto centre = juce::jmin (chassis.getY() + juce::roundToInt (static_cast<float> (chassis.getHeight()) * 0.31f) + 9,
                                    static_cast<int> (std::floor (end.y)) - 6 - (line + 1) / 2);
    return juce::Rectangle<int> (width, line).withCentre ({chassis.getCentreX(), centre});
}

inline CandidateWords candidateWords (juce::Rectangle<int> chassis, const metric_needle::Frame& frame,
                                      const metric_needle::Reading& facts, bool one, const presentation::Context& context)
{
    CandidateWords words;
    const auto font = instrumentFont (context);
    const auto line = instrumentLine (context);
    const auto ready = candidateReady (frame);
    const auto shownWidth = [&font] (const juce::String& text) {
        return static_cast<int> (std::ceil (text_style::shownWidth (font, text, text_style::LabelPolicy::fixed))) + 3;
    };
    // One row of glass, as wide as its lower edge allows.
    const auto rim = [&chassis] (int lowerPixel) { return glass::inset (glass::row (chassis, static_cast<float> (lowerPixel))); };

    words.headline.text = frame.status == metric_needle::Status::measuring ? juce::String ("PREPARING")
        : ! ready ? juce::String ("UNAVAILABLE") : juce::String (metric_needle::label (frame.face));
    words.headline.box = candidateHeadlineBounds (chassis, one, ready, context);

    // The lowest row ends on the glass; the figures keep their own row above it.
    const auto floorPixel = glassFloor (chassis);
    const auto lowest = floorPixel - line;
    const auto usual = figureRow (chassis, line);
    const auto raised = raisedFigureRow (chassis, line);
    const auto figure = [&] (CandidateWord& word, const juce::String& text, float centre, int top) {
        word.text = text;
        word.box = juce::Rectangle<int> (juce::roundToInt (static_cast<float> (chassis.getWidth()) * 0.24f), line)
            .withCentre ({chassis.getX() + juce::roundToInt (static_cast<float> (chassis.getWidth()) * centre), 0}).withY (top);
    };
    const auto song = juce::String::fromUTF8 (frame.song.c_str());
    const auto zero = ready ? "0 = " + juce::String (frame.median, 2) + " " + metric_needle::unit (frame.face) : juce::String();
    const auto reason = ! ready && frame.status != metric_needle::Status::measuring && ! frame.reason.empty()
        ? text_style::shownText (metric_needle::reasonText (frame.reason)) : juce::String();
    const auto cut = [&font] (const juce::String& text, int width) {
        return text_style::ellipsizedKeepingVersion (text, requiresNativeTextFont (text) ? nativeTextFontLike (font) : font,
                                                     static_cast<float> (juce::jmax (0, width - 3)));
    };
    const auto place = [&] (CandidateWord& word, const juce::String& text, juce::Rectangle<int> cell) {
        word.text = cut (text, cell.getWidth());
        word.box = cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), shownWidth (word.text)), cell.getHeight());
    };

    if (one)
    {
        figure (words.pre, "PRE " + candidateSignedNumber (facts.own), 0.145f, raised);
        figure (words.reference, "REF " + candidateSignedNumber (facts.reference), 0.855f, raised);
        if (ready)
        {
            const auto edge = rim (floorPixel);
            place (words.zero, zero, glass::band (chassis, edge, glass::singleHub - glass::hubRadius, lowest, floorPixel));
            place (words.song, song, glass::band (chassis, glass::singleHub + glass::hubRadius, glass::width - edge, lowest, floorPixel));
            return words;
        }
        const auto under = words.headline.box.getBottom();
        words.noticeIsSong = reason.isEmpty();
        place (words.notice, reason.isNotEmpty() ? reason : song, glass::band (chassis, 330.0f, glass::width - 330.0f, under, under + line));
        return words;
    }

    figure (words.pre, "PRE " + candidateSignedNumber (facts.pre), 0.145f, usual);
    figure (words.post, "POST " + candidateSignedNumber (facts.own), 0.855f, usual);
    const auto column = glass::band (chassis, glass::pairedHubs[0] + glass::hubRadius,
                                     glass::pairedHubs[1] - glass::hubRadius, lowest, floorPixel);
    // Between the hubs: the zero and its song share the lowest row where it is wide enough, else take one each.
    const bool shared = column.getWidth() >= 14 * line;
    figure (words.reference, "REF " + candidateSignedNumber (facts.reference), 0.5f,
            shared ? raised : juce::jmin (usual, lowest - 2 * line));
    if (! ready)
    {
        words.noticeIsSong = reason.isEmpty();
        if (shared) { place (words.notice, reason.isNotEmpty() ? reason : song, column); return words; }
        // Small sizes: the status and its reason (two lines when one is too narrow) stand under the REF figure.
        if (reason.isNotEmpty() && shownWidth (reason) > column.getWidth())
        {
            const auto lines = twoRows (reason, font, static_cast<float> (column.getWidth() - 3));
            place (words.notice, lines[0], column.translated (0, -line));
            place (words.noticeEnd, lines[1], column);
        }
        else place (words.notice, reason.isNotEmpty() ? reason : song, column);
        words.headline.box = words.headline.box.withY (words.notice.box.getY() - line);
        words.reference.box = words.reference.box.withY (words.headline.box.getY() - line);
        return words;
    }
    if (! shared)
    {
        place (words.zero, zero, column.translated (0, -line));
        place (words.song, song, column);
        return words;
    }
    const auto gap = line;
    const auto zeroWidth = juce::jmin (column.getWidth(), shownWidth (zero));
    const auto songShown = cut (song, column.getWidth() - zeroWidth - gap);
    const auto songWidth = songShown.isEmpty() ? 0 : juce::jmin (column.getWidth() - zeroWidth - gap, shownWidth (songShown));
    auto row = column.withSizeKeepingCentre (zeroWidth + (songWidth > 0 ? gap + songWidth : 0), line);
    words.zero = { zero, row.removeFromLeft (zeroWidth) };
    if (songWidth > 0)
        words.song = { songShown, row.removeFromRight (songWidth) };
    return words;
}
}
