#include "HyphaLocalBlindSteps.h"

#include "HyphaTextStyle.h"

#include <cmath>

namespace hypha::local_blind_ui
{
namespace
{
// One place for the names' typography: stepFont measures with it and paintSteps draws with it.
constexpr auto stepRole = typography::TextRole::status;
constexpr auto stepComposition = typography::Composition::information;
}

Step stepFor (const local_blind::ProductSessionView& view) noexcept
{
    using Phase = local_blind::ProductSessionPhase;
    switch (view.phase)
    {
        case Phase::idle:
        case Phase::failed:
        case Phase::capturing:
        case Phase::preparing: return Step::capture;
        case Phase::ready: return Step::start;
        case Phase::armed: return Step::listen;
        case Phase::listening: return view.trial.canAnswer ? Step::answer : Step::listen;
        case Phase::revealed: return Step::result;
        case Phase::returnPending:
        case Phase::returned: break;
    }
    return Step::none;
}

std::array<juce::Rectangle<int>, 5> stepCells (juce::Rectangle<int> area) noexcept
{
    std::array<juce::Rectangle<int>, 5> cells;
    const auto width = area.getWidth() / 5;
    for (std::size_t index = 0; index < cells.size(); ++index)
        cells[index] = index + 1 == cells.size() ? area : area.removeFromLeft (width);
    return cells;
}

juce::Font stepFont (presentation::Context context)
{
    return labelFont (context, stepRole, stepComposition);
}

int stepsHeight (presentation::Context context)
{
    // Two pixels for the underline and two between it and the name.
    return juce::roundToInt (std::ceil (stepFont (context).getHeight())) + 4;
}

juce::String stepName (int index)
{
    // Numbered, so each name is its own catalog entry and never translates another "Start".
    static constexpr const char* names[] { "1  Capture", "2  Start", "3  Listen", "4  Answer",
                                           "5  Result" };
    return names[juce::jlimit (0, 4, index)];
}

void paintSteps (juce::Graphics& g, juce::Rectangle<int> area, Step current,
                 presentation::Context context)
{
    if (area.isEmpty() || current == Step::none)
        return;
    const auto lit = static_cast<int> (current);
    const auto cells = stepCells (area);
    g.setFont (labelFont (context, stepRole, stepComposition));
    for (int index = 0; index < 5; ++index)
    {
        auto cell = cells[static_cast<std::size_t> (index)];
        // Every name stays readable text; only the underline, a non-text mark, may be muted.
        const auto text = index == lit ? COL_FLORA_BR
                        : index < lit ? COL_TEXT_SECONDARY
                                      : COL_TEXT_TERTIARY;
        const auto line = index == lit ? COL_FLORA_BR
                        : index < lit ? COL_FLORA.withAlpha (0.7f)
                                      : COL_MUTED;
        g.setColour (line);
        g.fillRect (cell.removeFromBottom (index == lit ? 2 : 1).reduced (3, 0));
        g.setColour (text);
        text_style::drawEllipsized (g, stepName (index), cell.reduced (3, 0),
                                    juce::Justification::centred);
    }
}

juce::String purposeText()
{
    // The answers include no preference and cannot tell apart, so the purpose never demands a pick.
    return "Hear the same 4 seconds of PRE and POST at matched level, without knowing which is "
           "which, and say which you prefer, if either.";
}
}
