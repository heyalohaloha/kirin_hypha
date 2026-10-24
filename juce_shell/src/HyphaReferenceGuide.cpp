#include "HyphaReferenceGuide.h"

#include "HyphaReferenceComponent.h"
#include "HyphaReferencePendingUI.h"
#include "HyphaReferenceMetricPainter.h"
#include "HyphaReferenceStages.h"
#include "HyphaTextStyle.h"

#include <cmath>
#include <tuple>

namespace hypha::reference_ui
{
namespace
{
// A step the page itself can deliver never reads "Ready" while its button cannot: the DAW has
// stopped, or the callback has not confirmed the buffer yet.
SourceStep shownStep (SourceStep step, bool audible, bool playing) noexcept
{
    if (audible) return SourceStep::ready;
    if (step == SourceStep::ready || (! playing && step == SourceStep::aligning))
        return playing ? SourceStep::loadingAudio : SourceStep::playDaw;
    return step;
}

enum class Mark { ready, action, waiting };

// The guide marks a step that settles by itself (one with a wait limit) apart from one the person moves.
Mark markFor (SourceStep step) noexcept
{
    return step == SourceStep::ready ? Mark::ready : automaticStage (step) ? Mark::waiting : Mark::action;
}

juce::Colour markColour (Mark mark)
{
    return mark == Mark::ready ? COL_SPECTRUM_DELTA_BR : mark == Mark::action ? COL_FLORA_BR : COL_MUTED;
}

int lineOf (presentation::Context context, typography::TextRole role)
{
    return juce::roundToInt (std::ceil (typography::resolve (
        context, role, typography::Composition::information).lineHeight));
}

// The height `text` takes wrapped to `width` in the font it is drawn in.
int wrappedHeight (const juce::String& text, const juce::Font& font, int width)
{
    return juce::roundToInt (std::ceil (text_style::shownWrappedHeight (text, font, width)));
}

struct Row
{
    const char* letter;
    const char* role;
    juce::String state;
    Mark mark;
};

juce::String detailForWidth (const juce::String& text, const juce::Font& font, int width)
{
    const auto shown = text_style::shownText (text);
    if (! requiresJapaneseGlyphs (shown) || text != stageOf (SourceStep::playDaw).detail) return text;
    const auto clauses = juce::StringArray::fromTokens (shown, juce::String::fromUTF8 (u8"、"), {});
    juce::String result, line;
    for (int i = 0; i < clauses.size(); ++i)
    {
        const auto clause = clauses[i] + (i + 1 < clauses.size() ? juce::String::fromUTF8 (u8"、") : juce::String());
        if (line.isNotEmpty() && text_style::shownWidth (font, line + clause) > static_cast<float> (width))
        { result += line + "\n"; line.clear(); }
        line += clause;
    }
    return result + line;
}

bool stackRow (const Row& item, presentation::Context context, int width)
{
    if (! observatory::isFullDensity (context.density)) return false;
    const auto font = labelFont (context, typography::TextRole::body, typography::Composition::information);
    const auto role = juce::roundToInt (std::ceil (text_style::shownWidth (font, item.role)));
    const auto state = juce::roundToInt (std::ceil (text_style::shownWidth (font, item.state)));
    return role + 8 + state > width - 34;
}

// At the compact sizes a row is the letter and its step; from 200% it also says what the source is.
bool paintRow (juce::Graphics& g, juce::Rectangle<int> row, const Row& item,
               presentation::Context context)
{
    const bool described = observatory::isFullDensity (context.density);
    const bool stacked = stackRow (item, context, row.getWidth());
    using typography::TextRole;
    using typography::Composition;
    const auto colour = markColour (item.mark);
    const auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre (
        { static_cast<float> (row.getX()) + 4.0f, static_cast<float> (row.getCentreY()) });
    g.setColour (colour);
    if (item.mark == Mark::ready) g.fillEllipse (dot);
    else g.drawEllipse (dot, 1.2f);
    row.removeFromLeft (14);
    g.setFont (labelFont (context, TextRole::action, Composition::information));
    text_style::drawText (g, item.letter, row.removeFromLeft (20), juce::Justification::centredLeft);
    const auto font = labelFont (context, TextRole::body, Composition::information);
    const auto stateWidth = juce::roundToInt (std::ceil (text_style::shownWidth (font, item.state)));
    // Leave the whole reason its measured width, including longer Japanese B states.
    const auto neededRole = juce::roundToInt (std::ceil (text_style::shownWidth (font, item.role))) + 8;
    const auto roleWidth = juce::jlimit (0, row.getWidth(),
        juce::jmin (juce::jmax (row.getWidth() * 11 / 20, neededRole), row.getWidth() - stateWidth));
    const auto role = stacked ? row.removeFromTop (row.getHeight() / 2)
        : described ? row.removeFromLeft (roleWidth).withTrimmedRight (8) : juce::Rectangle<int>();
    g.setColour (COL_TEXT_SECONDARY);
    g.setFont (labelFont (context, TextRole::body, Composition::information));
    if (described)
        text_style::drawEllipsized (g, item.role, role, juce::Justification::centredLeft);
    g.setColour (item.mark == Mark::waiting ? COL_TEXT_SECONDARY : colour);
    text_style::drawEllipsized (g, item.state, row, juce::Justification::centredLeft);
    return (! described || text_style::shownWidth (font, item.role) <= static_cast<float> (role.getWidth()))
        && stateWidth <= row.getWidth();
}
}

Guide guide (const State& state)
{
    Guide result;
    result.playing = state.aAvailable;
    result.reference = state.referenceStep;  // B's button uses this same editor step.
    result.version = shownStep (state.versionStep, canHearVersion (state), state.aAvailable);
    result.check = shownStep (state.checkStep, canHearCheck (state), state.aAvailable);
    // B（REF）の画面は B セットの曲を出す（V・C の始め方の案内は重ねない）。
    result.shown = state.separateComparisons && state.osAccess != os_access::State::unowned && state.comparisonSlot != 3
        && ! state.bSelected && ! isBlindSession (state.blindPhase)
        && result.version != SourceStep::ready && result.check != SourceStep::ready
        && ! (state.comparisonSlot == 2 && !state.viewBindings.empty()
            && (state.detailedMeasurement || !state.profiles.empty()
                || (state.visualTimeline && (state.visualTimeline->aKirin || state.visualTimeline->aTicks))));
    if (state.pendingAudition.stage != reference_audition::PendingAuditionView::Stage::none)
    {
        result.heading = pendingAuditionHeading (state);
        if (state.pendingAudition.slot == 1 && !state.pendingAudition.waiting())
        {
            result.stoppedVersion = true;
            result.shown = state.separateComparisons && state.comparisonSlot == 1
                && state.osAccess != os_access::State::unowned && !state.bSelected
                && !isBlindSession (state.blindPhase);
            result.detail = versionStoppedText (state).fromFirstOccurrenceOf (" / ", false, false);
            return result;
        }
        result.detail = state.pendingAudition.stage == reference_audition::PendingAuditionView::Stage::sourceLevelUnavailable
            ? juce::String ("Prepare this source in Kirin OS. A stays live.")
            : state.pendingAudition.stage == reference_audition::PendingAuditionView::Stage::ceilingExceeded
            ? juce::String ("MATCH exceeds the safe level. A stays live.")
            : pendingAuditionReason (state) + " / "
            + (state.pendingAudition.waiting() ? "A stays live until ready. Press A to cancel."
                                              : "A stays live. Choose the source again.");
        return result;
    }
    if (! state.libraryReceived)
    {
        const auto step = state.osOnline ? SourceStep::waitingForKirinOs : SourceStep::openKirinOs;
        result.heading = stageHeading (step, 2);
        result.detail = stageOf (step).detail;
        return result;
    }
    const bool version = state.comparisonSlot == 1;
    const auto first = !state.aAvailable ? SourceStep::playDaw
        : version ? result.version : result.check;
    result.heading = stageHeading (first, version ? 1 : 2);
    result.detail = stageOf (first).detail;
    // 断られた理由のコードを残す（2026-10-18。知らない理由も、コードで Kirin OS 側と照らせる）。
    if (const auto& code = version ? state.versionRejectionCode : state.checkRejectionCode;
        code.isNotEmpty() && kindOf (first) == StatusKind::unable)
        result.detail += " Reason code: " + code;
    return result;
}

void Component::syncSourceButtons()
{
    // Side by side, B and C stay clickable while they cannot be heard; the one-slot page keeps B
    // disabled until it is ready.
    const bool versionAudible = canHearVersion (current), checkAudible = canHearCheck (current);
    const bool bQueue = canQueueSource (current, true), cQueue = canQueueSource (current, false);
    const bool waiting = current.pendingAudition.waiting();
    // 予約（DAW の再生を待つ）は色で示し、何を待つかは状態の行が言う（「V...」は文字が切れたように見えた。2026-10-04）。
    bButton.setButtonText ("V");
    cButton.setButtonText ("C");
    bButton.setAttention (waiting && current.pendingAudition.slot == 1);
    cButton.setAttention (waiting && current.pendingAudition.slot == 2);
    // 300% 未満の C と V は薄く、押すと 300% に広げる（openLarge）。
    const bool opensLarge = current.separateComparisons && ! current.blindLargeScreen;
    bButton.setEnabled (current.separateComparisons || versionAudible);
    bButton.setReady (! opensLarge && (! current.separateComparisons || versionAudible || bQueue));
    bButton.setTooltip (opensLarge ? "Open V at 300%" : bQueue ? "Queue V for DAW playback. A stays live until ready; press A to cancel."
        : ! current.separateComparisons || versionAudible
        ? "Audition the Version from Kirin OS (V)." : unavailableText (current, true));
    cButton.setReady (! opensLarge && (checkAudible || cQueue));
    cButton.setTooltip (opensLarge ? "Open C at 300%" : cQueue ? "Queue C for DAW playback. A stays live until ready; press A to cancel."
        : checkAudible ? "Audition the Check's song from Kirin OS (C)." : unavailableText (current, false));
    guideShown = guide (current).shown;
}

bool Component::explainUnavailable (bool version)
{
    if (current.separateComparisons && onSelectVisualSlot) onSelectVisualSlot (version ? 1 : 2);
    const bool audible = version ? ! current.separateComparisons || canHearVersion (current)
                                 : canHearCheck (current);
    if (audible || canQueueSource (current, version)) return false;
    if (onExplain) onExplain (unavailableText (current, version));
    return true;
}

void Component::paintSourceHints (juce::Graphics& g) const
{
    // One of B and C can be heard and the other cannot: what the other needs, after its label.
    const juce::Graphics::ScopedSaveState saved (g);
    const auto shown = guide (current);
    const auto font = labelFont (presentationContext, typography::TextRole::unit,
                                 typography::Composition::information);
    g.setFont (labelFont (presentationContext, typography::TextRole::unit,
                          typography::Composition::information));
    for (const auto& [box, step, label] : { std::tuple { &versionBox, shown.version, "V / VERSION" },
                                            std::tuple { &checkBox, shown.check, "C / CHECK" } })
    {
        // An empty B / VERSION already reads "Choose Version".
        if (step == SourceStep::ready || step == SourceStep::chooseVersion || ! selectionVisible (*box))
            continue;
        auto row = box->getBounds().withY (box->getY() - 17).withHeight (15).withTrimmedRight (4);
        row.removeFromLeft (juce::roundToInt (text_style::shownWidth (font, label)) + 12);
        g.setColour (markColour (markFor (step)));
        text_style::drawEllipsized (g, stepText (step), row, juce::Justification::centredLeft);
    }
}

juce::String unavailableText (const State& state, bool version)
{
    const auto shown = guide (state);
    const auto step = ! state.libraryReceived ? (state.osOnline ? SourceStep::waitingForKirinOs : SourceStep::openKirinOs)
        : version ? shown.version : shown.check;
    return juce::String (version ? "V: " : "C: ") + stepText (step);
}

GuideFit paintGuide (juce::Graphics& g, juce::Rectangle<int> area, const Guide& shown,
                     presentation::Context context)
{
    using typography::TextRole;
    using typography::Composition;
    GuideFit fit;
    const bool full = observatory::isFullDensity (context.density);
    const bool inspection = context.density == observatory::Density::inspection;
    const auto headingRole = full ? TextRole::sectionTitle : TextRole::status;
    reference_metric_painter::paintPanel (g, area.toFloat(), 0.72f);
    auto content = area.reduced (inspection ? 18 : full ? 14 : 7, inspection ? 14 : full ? 8 : 4);
    if (shown.stoppedVersion && !full)
    {
        // The stopped role and its recovery action take priority over role-summary rows.
        const auto message = shown.heading + " / " + shown.detail;
        const auto font = labelFont (context, TextRole::body, Composition::information);
        const bool single = text_style::shownWidth (font, message) <= static_cast<float> (content.getWidth());
        const auto height = single ? juce::roundToInt (std::ceil (font.getHeight()))
                                   : wrappedHeight (message, font, content.getWidth());
        fit.heading = fit.detail = height <= content.getHeight();
        g.setColour (COL_TEXT_SECONDARY);
        g.setFont (labelFont (context, TextRole::body, Composition::information));
        if (single) text_style::drawEllipsized (g, message, content, juce::Justification::topLeft);
        else text_style::drawLines (g, message, content, juce::Justification::topLeft, 4);
        return fit;
    }
    const auto heading = content.removeFromTop (lineOf (context, headingRole));
    fit.heading = text_style::shownWidth (labelFont (context, headingRole, Composition::information),
                                          shown.heading) <= static_cast<float> (heading.getWidth());
    g.setColour (COL_OBSERVATORY_VALUE);
    g.setFont (labelFont (context, headingRole, Composition::information));
    text_style::drawEllipsized (g, shown.heading, heading, juce::Justification::centredLeft);
    content.removeFromTop (full ? 4 : 1);

    // Why, whole when it fits and as one line when it does not.
    const auto bodyFont = labelFont (context, TextRole::body, Composition::information);
    const auto detail = detailForWidth (shown.detail, bodyFont, content.getWidth());
    const auto detailHeight = wrappedHeight (detail, bodyFont, content.getWidth());
    fit.detail = detailHeight <= content.getHeight();
    g.setColour (full ? COL_NORMAL : COL_TEXT_SECONDARY);
    g.setFont (labelFont (context, TextRole::body, Composition::information));
    if (fit.detail)
        text_style::drawLines (g, detail, content.removeFromTop (detailHeight),
                               juce::Justification::topLeft, 4);
    else
        text_style::drawEllipsized (g, detail, content.removeFromTop (juce::jmin (
            content.getHeight(), lineOf (context, TextRole::body))), juce::Justification::centredLeft);
    content.removeFromTop (inspection ? 12 : 4);

    // A, B, C, V in button order. Drop A first (the heading says what it needs), then V;
    // with only two rows, keep B's new explanation and C.
    const auto rowHeight = juce::jmax (lineOf (context, TextRole::body), inspection ? 26 : full ? 18 : 16);
    const Row rows[] {
        { "A", "Your mix, live from the DAW", shown.playing ? "Playing" : "Stopped",
          shown.playing ? Mark::ready : Mark::action },
        { "B", "A Reference song, from Kirin OS", stepText (shown.reference),
          markFor (shown.reference) },
        { "C", "The source of a Check, from Kirin OS", stepText (shown.check),
          markFor (shown.check) },
        { "V", "A Version of this song, from Kirin OS", stepText (shown.version),
          markFor (shown.version) },
    };
    // A long role and reason use separate lines; retain only whole rows in the available height.
    const auto heightFor = [&] (int index) { return rowHeight * (stackRow (rows[index], context, content.getWidth()) ? 2 : 1); };
    int count = 4;
    for (; count >= 2; --count)
    {
        int needed = 0;
        const auto first = count == 4 ? 0 : 1;
        for (int index = first; index < first + count; ++index) needed += heightFor (index);
        if (needed <= content.getHeight()) break;
    }
    if (count < 2)
        return fit;
    fit.rowCount = count;
    const auto first = count == 4 ? 0 : 1;
    for (auto index = first; index < first + count; ++index)
        fit.rows = paintRow (g, content.removeFromTop (heightFor (index)), rows[index], context) && fit.rows;

    // How to use them, at the full sizes.
    // 2026-10-04：VIEW の行は B-1174 で外した（見せる比較は開いている役の画面が決める）。
    const auto footnote = juce::String ("Press A, B, C or V to switch audio and open its page. "
                                      "The audition level is shown while listening.");
    const auto footnoteHeight = wrappedHeight (footnote, bodyFont, content.getWidth());
    if (! full || content.getHeight() < footnoteHeight + 12)
        return fit;
    content.removeFromTop (12);
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (labelFont (context, TextRole::body, Composition::information));
    text_style::drawLines (g, footnote, content.removeFromTop (footnoteHeight),
                           juce::Justification::topLeft, 3);
    return fit;
}
}
