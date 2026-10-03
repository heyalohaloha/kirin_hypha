#include "HyphaReferenceGuide.h"

#include "HyphaReferenceComponent.h"
#include "HyphaReferencePendingUI.h"
#include "HyphaReferenceMetricPainter.h"
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

juce::String headingFor (SourceStep step, bool version)
{
    switch (step)
    {
        case SourceStep::registerVersion: return "Register a Version of this song in Kirin OS";
        case SourceStep::chooseVersion: return "Choose a Version for V";
        case SourceStep::enableCheck: return "No Check is enabled in Kirin OS";
        case SourceStep::chooseSource: return "Choose a source for C in Kirin OS";
        case SourceStep::aligning: return "Aligning V with A";
        case SourceStep::noMatchingPassage: return "V did not match this passage";
        case SourceStep::playAnotherPassage: return "Play another passage to align V";
        case SourceStep::approveSampleRate: return version ? "Approve V conversion" : "Approve C conversion";
        case SourceStep::verifyingSource: return version ? "Verifying V source" : "Verifying C source";
        case SourceStep::loadingAudio: return version ? "Loading V at the playhead" : "Loading C at the playhead";
        case SourceStep::outsideCue: return "This playhead is outside C's Cue";
        case SourceStep::preparing: return version ? "Preparing V" : "Preparing C";
        case SourceStep::attention: return "Open Kirin OS to check the source";
        case SourceStep::waitingForKirinOs: return "Open Kirin OS";
        case SourceStep::playDaw: return "Play the song in your DAW";
        case SourceStep::ready: break;
    }
    return {};
}

juce::String detailFor (SourceStep step)
{
    switch (step)
    {
        case SourceStep::registerVersion:
            return "V plays another Version of the song you are playing, registered in Kirin OS.";
        case SourceStep::chooseVersion:
            return "Choose it in V / VERSION above. V plays another Version of the song you are playing.";
        case SourceStep::enableCheck:
        case SourceStep::chooseSource: return "C plays the source a Check compares your mix with.";
        case SourceStep::aligning: return "Keep playing. V must be a Version of the song you are playing.";
        case SourceStep::noMatchingPassage:
            return "Check that V is a Version of A at this POST, or play a different matching passage.";
        case SourceStep::playAnotherPassage:
            return "This passage repeats in V. Play a part that occurs only once.";
        case SourceStep::approveSampleRate:
            return "Only the audition copy changes. A stays unchanged.";
        case SourceStep::verifyingSource: return "The source is being checked. A stays live.";
        case SourceStep::loadingAudio: return "Keep playing while audio loads.";
        case SourceStep::outsideCue:
            return "Move to the comparison passage, or choose a longer or looping Cue in Kirin OS. A stays live.";
        case SourceStep::preparing: return "This takes a moment.";
        case SourceStep::attention: return "The source changed or could not be opened.";
        case SourceStep::waitingForKirinOs:
            return "Versions and References registered in Kirin OS arrive here automatically.";
        case SourceStep::playDaw: return "V follows the song; C uses its Cue.";
        case SourceStep::ready: break;
    }
    return {};
}

enum class Mark { ready, action, waiting };

Mark markFor (SourceStep step) noexcept
{
    return step == SourceStep::ready ? Mark::ready
         : step == SourceStep::waitingForKirinOs || step == SourceStep::aligning
             || step == SourceStep::verifyingSource || step == SourceStep::loadingAudio
             || step == SourceStep::preparing ? Mark::waiting : Mark::action;
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
    const bool workflowActive = state.workflow.mode != reference_audition::WorkflowView::Mode::normal
        && state.workflow.status != reference_audition::WorkflowView::Status::resumeAvailable;
    // B（REF）の画面は B セットの曲を出す（V・C の始め方の案内は重ねない）。
    result.shown = state.separateComparisons && state.osAccess != os_access::State::unowned && state.comparisonSlot != 3
        && ! state.bSelected && ! isBlindSession (state.blindPhase) && ! workflowActive
        && ! (state.captureAccess && state.captureAccess->capturedView)
        && result.version != SourceStep::ready && result.check != SourceStep::ready
        && ! (state.comparisonSlot == 2 && !state.viewBindings.empty()
            && (state.detailedMeasurement || !state.profiles.empty()
                || (state.visualTimeline && state.visualTimeline->tonalAvailable)));
    const bool versionApproval = result.version == SourceStep::approveSampleRate;
    const bool checkApproval = result.check == SourceStep::approveSampleRate;
    if (state.pendingAudition.stage != reference_audition::PendingAuditionView::Stage::none)
    {
        result.heading = pendingAuditionHeading (state);
        result.detail = state.pendingAudition.stage == reference_audition::PendingAuditionView::Stage::approval
            ? juce::String ("Approve below. A stays live; press A to cancel.")
            : state.pendingAudition.stage == reference_audition::PendingAuditionView::Stage::sourceLevelUnavailable
            ? juce::String ("Prepare this source in Kirin OS. A stays live.")
            : state.pendingAudition.stage == reference_audition::PendingAuditionView::Stage::ceilingExceeded
            ? juce::String ("MATCH exceeds the safe level. A stays live.")
            : pendingAuditionReason (state) + " / "
            + (state.pendingAudition.waiting() ? "A stays live until ready. Press A to cancel."
                                              : "A stays live. Choose the source again.");
        return result;
    }
    if (! state.libraryReceived && ! versionApproval && ! checkApproval)
    {
        result.heading = state.osOnline ? "Receiving from Kirin OS" : "Open Kirin OS";
        result.detail = detailFor (SourceStep::waitingForKirinOs);
        return result;
    }
    const auto first = !state.aAvailable ? SourceStep::playDaw
        : versionApproval ? result.version : checkApproval ? result.check
        : result.version != SourceStep::ready ? result.version : result.check;
    const bool version = versionApproval || (! checkApproval && state.aAvailable && result.version != SourceStep::ready);
    result.heading = headingFor (first, version);
    result.detail = detailFor (first);
    return result;
}

void Component::syncSourceButtons()
{
    // Side by side, B and C stay clickable while they cannot be heard; the one-slot page keeps B
    // disabled until it is ready.
    const bool versionAudible = canHearVersion (current), checkAudible = canHearCheck (current);
    const bool bQueue = canQueueSource (current, true), cQueue = canQueueSource (current, false);
    const bool waiting = current.pendingAudition.waiting();
    bButton.setButtonText (waiting && current.pendingAudition.slot == 1 ? "V..." : "V");
    cButton.setButtonText (waiting && current.pendingAudition.slot == 2 ? "C..." : "C");
    bButton.setAttention (waiting && current.pendingAudition.slot == 1);
    cButton.setAttention (waiting && current.pendingAudition.slot == 2);
    // H10: 300% 未満の C と V は薄く、押すと 300% に広げる（openLarge）。
    const bool opensLarge = current.separateComparisons && ! current.blindLargeScreen;
    bButton.setEnabled (current.separateComparisons || versionAudible);
    bButton.setReady (! opensLarge && (! current.separateComparisons || versionAudible || bQueue));
    bButton.setTooltip (opensLarge ? "Open V at 300%" : bQueue ? "Queue V for DAW playback. A stays live until ready; press A to cancel."
        : ! current.separateComparisons || versionAudible
        ? "Audition the Version from Kirin OS (V)." : unavailableText (current, true));
    cButton.setReady (! opensLarge && (checkAudible || cQueue));
    cButton.setTooltip (opensLarge ? "Open C at 300%" : cQueue ? "Queue C for DAW playback. A stays live until ready; press A to cancel."
        : checkAudible ? juce::String() : unavailableText (current, false));
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

juce::String stepText (SourceStep step)
{
    switch (step)
    {
        case SourceStep::ready: return "Ready";
        case SourceStep::waitingForKirinOs: return "Waiting for Kirin OS";
        case SourceStep::registerVersion: return "Register a Version in Kirin OS";
        case SourceStep::chooseVersion: return "Choose a Version";
        case SourceStep::enableCheck: return "Enable a Check in Kirin OS";
        case SourceStep::chooseSource: return "Choose a source in Kirin OS";
        case SourceStep::playDaw: return "Ready when the DAW plays";
        case SourceStep::aligning: return "Aligning with A. Keep playing";
        case SourceStep::noMatchingPassage: return "No verified match here; check Version";
        case SourceStep::playAnotherPassage: return "Play another passage";
        case SourceStep::approveSampleRate: return "Approve rate conversion";
        case SourceStep::verifyingSource: return "Verifying source";
        case SourceStep::loadingAudio: return "Loading audio here; keep playing";
        case SourceStep::outsideCue: return "Outside Cue; move or choose longer Cue";
        case SourceStep::preparing: return "Preparing";
        case SourceStep::attention: return "Check the source in Kirin OS";
    }
    return {};
}

juce::String unavailableText (const State& state, bool version)
{
    const auto shown = guide (state);
    const auto step = ! state.libraryReceived ? SourceStep::waitingForKirinOs
        : version ? shown.version : shown.check;
    return juce::String (version ? "V: " : "C: ")
        + (step == SourceStep::waitingForKirinOs && ! state.osOnline ? juce::String ("Open Kirin OS")
                                                                      : stepText (step));
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
    const auto footnote = juce::String ("Press A, B, C or V to switch audio. VIEW changes only the visuals. "
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
