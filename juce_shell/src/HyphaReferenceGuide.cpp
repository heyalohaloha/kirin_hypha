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

// At the compact sizes a row is the letter and its step; from 200% it also says what the source is.
bool paintRow (juce::Graphics& g, juce::Rectangle<int> row, const Row& item,
               presentation::Context context)
{
    const bool described = observatory::isFullDensity (context.density);
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
    const auto role = described ? row.removeFromLeft (row.getWidth() * 11 / 20).withTrimmedRight (8)
                                : juce::Rectangle<int>();
    const auto font = labelFont (context, TextRole::body, Composition::information);
    g.setColour (COL_TEXT_SECONDARY);
    g.setFont (labelFont (context, TextRole::body, Composition::information));
    if (described)
        text_style::drawEllipsized (g, item.role, role, juce::Justification::centredLeft);
    g.setColour (item.mark == Mark::waiting ? COL_TEXT_SECONDARY : colour);
    text_style::drawEllipsized (g, item.state, row, juce::Justification::centredLeft);
    return (! described || text_style::shownWidth (font, item.role) <= static_cast<float> (role.getWidth()))
        && text_style::shownWidth (font, item.state) <= static_cast<float> (row.getWidth());
}
}

Guide guide (const State& state)
{
    Guide result;
    result.playing = state.aAvailable;
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
    const bool audible = version ? ! current.separateComparisons || canHearVersion (current)
                                 : canHearCheck (current);
    if (audible || canQueueSource (current, version)) return false;
    if (onSelectVisualSlot) onSelectVisualSlot (version ? 1 : 2);
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
    const auto heading = content.removeFromTop (lineOf (context, headingRole));
    fit.heading = text_style::shownWidth (labelFont (context, headingRole, Composition::information),
                                          shown.heading) <= static_cast<float> (heading.getWidth());
    g.setColour (COL_OBSERVATORY_VALUE);
    g.setFont (labelFont (context, headingRole, Composition::information));
    text_style::drawEllipsized (g, shown.heading, heading, juce::Justification::centredLeft);
    content.removeFromTop (full ? 4 : 1);

    // Why, whole when it fits and as one line when it does not.
    const auto bodyFont = labelFont (context, TextRole::body, Composition::information);
    const auto detailHeight = wrappedHeight (shown.detail, bodyFont, content.getWidth());
    fit.detail = detailHeight <= content.getHeight();
    g.setColour (full ? COL_NORMAL : COL_TEXT_SECONDARY);
    g.setFont (labelFont (context, TextRole::body, Composition::information));
    if (fit.detail)
        text_style::drawLines (g, shown.detail, content.removeFromTop (detailHeight),
                               juce::Justification::topLeft, 4);
    else
        text_style::drawEllipsized (g, shown.detail, content.removeFromTop (juce::jmin (
            content.getHeight(), lineOf (context, TextRole::body))), juce::Justification::centredLeft);
    content.removeFromTop (inspection ? 12 : 4);

    // A, B and C with where each stands; B and C alone when three rows do not fit, since the
    // heading already says what A needs.
    const auto rowHeight = juce::jmax (lineOf (context, TextRole::body), inspection ? 26 : full ? 18 : 16);
    const Row rows[] {
        { "A", "Your mix, live from the DAW", shown.playing ? "Playing" : "Stopped",
          shown.playing ? Mark::ready : Mark::action },
        { "V", "A Version of this song, from Kirin OS", stepText (shown.version),
          markFor (shown.version) },
        { "C", "The source of a Check, from Kirin OS", stepText (shown.check),
          markFor (shown.check) },
    };
    const auto count = juce::jmin (3, content.getHeight() / rowHeight);
    if (count < 2)
        return fit;
    fit.rowCount = count;
    for (auto index = 3 - count; index < 3; ++index)
        fit.rows = paintRow (g, content.removeFromTop (rowHeight), rows[index], context) && fit.rows;

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
