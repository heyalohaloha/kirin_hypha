#include "HyphaSpacePainter.h"

#include "HyphaMonoSumPainter.h"

#include "HyphaTheme.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"

#include <cmath>
#if JUCE_MAC
 #include <objc/message.h>
 #include <objc/runtime.h>
#elif JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace hypha::space_field
{
namespace
{
struct Layout
{
    juce::Rectangle<int> title, field, plot, balance, correlation, mono;
    bool glance = false;
};

Layout layoutFor (juce::Rectangle<int> area, bool compact, presentation::Context presentation)
{
    Layout result;
    result.glance = compact && presentation.density == observatory::Density::compact;
    if (result.glance)
    {
        area.reduce (6, 4);
        const int column = juce::jlimit (56, 96, (area.getWidth() - area.getHeight()) / 2);
        result.balance = area.removeFromLeft (column);
        result.correlation = area.removeFromRight (column);
        const int side = juce::jmin (area.getWidth(), area.getHeight());
        result.field = juce::Rectangle<int> (0, 0, side, side).withCentre (area.getCentre())
                           .reduced (main_frame::inset());
        result.plot = result.field.reduced (8);
        return result;
    }
    area.reduce (compact ? 6 : 9, compact ? 5 : 7);
    result.title = area.removeFromTop (compact ? 14 : 18);
    const int gap = compact ? 5 : 8;
    // MONO is added only where it costs nothing that SPACE already shows. The scatter answers
    // "wide or narrow" at a glance and stays the panel's own picture; MONO is a chart to read,
    // and it is worth having only once there is room for both to be themselves.
    //
    // With MONO the square keeps the panel's full height on the left, and BAL and CORR side by
    // side, then MONO under them, share the column on its right. MONO used to run under the whole
    // row, which cut the square to half the height and left wide empty sides at 300%. The column
    // has to hold MONO's plot, its six-second field and both boxes beside a full-height square,
    // measured from the panel rather than a size preset: today only the largest editor has it.
    constexpr int monoStripMinimum = 116;
    constexpr int monoColumnMinimum = 360;
    const int fieldInset = compact ? 11 : 16;
    const int metricsHeight = juce::jlimit (96, 150, area.getHeight() / 4);
    const bool showMono = ! compact
                       && area.getWidth() - area.getHeight() - gap >= monoColumnMinimum
                       && area.getHeight() - metricsHeight - gap >= monoStripMinimum;
    auto& field = result.field; auto& mono = result.mono;
    auto& balance = result.balance; auto& correlationBox = result.correlation;
    if (showMono)
    {
        field = area.removeFromLeft (area.getHeight());
        area.removeFromLeft (gap);
        auto metrics = area.removeFromTop (metricsHeight);
        area.removeFromTop (gap);
        mono = area;
        balance = metrics.removeFromLeft ((metrics.getWidth() - gap) / 2);
        metrics.removeFromLeft (gap);
        correlationBox = metrics;
    }
    else
    {
        const int metricWidth = juce::jlimit (82, compact ? 102 : 168,
                                              juce::roundToInt (area.getWidth() * 0.31f));
        auto metrics = area.removeFromRight (metricWidth);
        area.removeFromRight (gap);
        const int side = juce::jmin (area.getWidth(), area.getHeight());
        field = juce::Rectangle<int> (0, 0, side, side).withCentre (area.getCentre());
        balance = metrics.removeFromTop ((metrics.getHeight() - gap) / 2);
        metrics.removeFromTop (gap);
        correlationBox = metrics;
    }
    field.reduce (main_frame::inset(), main_frame::inset()); // the page's main window, in its frame
    result.plot = field.reduced (fieldInset);
    return result;
}

void drawPanel (juce::Graphics& g, juce::Rectangle<int> area,
                bool compact, float radius = 4.0f)
{
    surface_material::paintPanel (g, area.toFloat(), compact ? 0.96f : 0.76f, radius);
}

juce::String balanceText (const KirinMeterSession& meter, bool available)
{
    if (! available)
        return "---";
    if (meter.balance_state == KIRIN_BALANCE_LEFT_ONLY)
        return "L ONLY";
    if (meter.balance_state == KIRIN_BALANCE_RIGHT_ONLY)
        return "R ONLY";
    if (meter.balance_state != KIRIN_BALANCE_NUMERIC || ! std::isfinite (meter.balance_db))
        return "---";
    return (meter.balance_db >= 0.0 ? "+" : "") + juce::String (meter.balance_db, 1);
}

void drawMetric (juce::Graphics& g,
                 juce::Rectangle<int> area,
                 const char* label,
                 const juce::String& value,
                 const char* unit,
                 bool compact,
                 presentation::Context presentation)
{
    drawPanel (g, area, compact);
    area.reduce (compact ? 5 : 9, compact ? 3 : 7);
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (labelFont (presentation, typography::TextRole::metricLabel,
                          typography::Composition::visualization));
    text_style::drawText (g, label, area.removeFromTop (compact ? 10 : 16),
                juce::Justification::centredLeft);
    auto unitArea = juce::Rectangle<int> {};
    if (! compact && juce::String (unit).isNotEmpty())
        unitArea = area.removeFromBottom (14);
    g.setColour (value == "---" ? COL_MUTED : COL_NORMAL);
    drawTabularText (g, monoFont (presentation, typography::TextRole::primaryValue,
                                  typography::Composition::visualization), value,
                     area.toFloat(), juce::Justification::centred);
    if (! unitArea.isEmpty())
    {
        g.setColour (COL_TEXT_TERTIARY);
        g.setFont (labelFont (presentation, typography::TextRole::unit,
                              typography::Composition::visualization));
        text_style::drawText (g, unit, unitArea, juce::Justification::centred);
    }
}

void drawFieldAxes (juce::Graphics& g, juce::Rectangle<float> plot)
{
    const auto centre = plot.getCentre();
    g.setColour (COL_MUTED.withAlpha (0.30f));
    g.drawLine (centre.x, plot.getY(), centre.x, plot.getBottom(), 0.7f);
    g.drawLine (plot.getX(), centre.y, plot.getRight(), centre.y, 0.7f);

    juce::Path physicalBounds;
    physicalBounds.startNewSubPath (centre.x, plot.getY());
    physicalBounds.lineTo (plot.getRight(), centre.y);
    physicalBounds.lineTo (centre.x, plot.getBottom());
    physicalBounds.lineTo (plot.getX(), centre.y);
    physicalBounds.closeSubPath();
    g.setColour (COL_FLORA.withAlpha (0.23f));
    g.strokePath (physicalBounds, juce::PathStrokeType (0.8f));
}

void drawDensity (juce::Graphics& g,
                  juce::Rectangle<float> plot,
                  const KirinMeterSession& meter)
{
    const float cellW = plot.getWidth() / (float) KIRIN_STEREO_FIELD_SIZE;
    const float cellH = plot.getHeight() / (float) KIRIN_STEREO_FIELD_SIZE;
    for (size_t index = 0; index < KIRIN_STEREO_FIELD_BINS; ++index)
    {
        const auto density = meter.field_density[index];
        if (density == 0u)
            continue;
        const float strength = (float) density / 255.0f;
        const auto row = (float) (index / KIRIN_STEREO_FIELD_SIZE);
        const auto column = (float) (index % KIRIN_STEREO_FIELD_SIZE);
        const auto cell = juce::Rectangle<float> (
            plot.getX() + column * cellW,
            plot.getY() + row * cellH,
            cellW + 0.35f,
            cellH + 0.35f).reduced (0.15f);
        const auto colour = COL_SPECTRUM_POST.interpolatedWith (COL_FLORA_BR,
                                                                strength * 0.40f);
        g.setColour (colour.withAlpha (0.08f + strength * 0.78f));
        g.fillRoundedRectangle (cell, juce::jmin (1.2f, cellW * 0.28f));
    }
}

// A bare number beside the 100% scatter: its label above, the value in the largest of the
// metric faces that fits the column, no panel.
void drawBareMetric (juce::Graphics& g, juce::Rectangle<int> column, const char* label,
                     const juce::String& value, presentation::Context presentation)
{
    const auto captionFont = labelFont (presentation, typography::TextRole::metricLabel,
                                        typography::Composition::visualization);
    auto valueFont = monoFont (presentation, typography::TextRole::primaryValue,
                               typography::Composition::facts);
    for (const auto composition : { typography::Composition::instrument,
                                    typography::Composition::visualization })
        if (tabularTextWidth (valueFont, value) > (float) column.getWidth())
            valueFont = monoFont (presentation, typography::TextRole::primaryValue, composition);
    const auto labelHeight = juce::roundToInt (captionFont.getHeight());
    const auto valueHeight = juce::roundToInt (valueFont.getHeight()) + 2;
    auto cell = column.withSizeKeepingCentre (column.getWidth(), labelHeight + valueHeight);
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (labelFont (presentation, typography::TextRole::metricLabel,
                          typography::Composition::visualization));
    text_style::drawText (g, label, cell.removeFromTop (labelHeight), juce::Justification::centred);
    g.setColour (value == "---" ? COL_MUTED : COL_NORMAL);
    drawTabularText (g, valueFont, value, cell.toFloat(), juce::Justification::centred);
}

// 100% is read at a glance: the scatter takes the whole height, BAL stands on its left and CORR
// on its right as bare numbers. The title row goes; a field still warming states its count in the
// corner, and a field that cannot be drawn states why in its centre.
void paintGlance (juce::Graphics& g, const Layout& placement, const KirinMeterSession& meter,
                  bool available, bool fieldAvailable, const juce::String& state,
                  presentation::Context presentation, space_live::Display* live)
{
    const auto& field = placement.field;
    const auto& balance = placement.balance;
    const auto& correlation = placement.correlation;
    drawPanel (g, field, true);
    main_frame::paint (g, field.toFloat());
    const auto plot = placement.plot.toFloat();
    drawFieldAxes (g, plot);
    if (live) live->paint (g, plot);
    else if (fieldAvailable) drawDensity (g, plot, meter);
    if ((! live && state != "30/30") || (available && meter.channels == 1))
    {
        g.setColour (fieldAvailable ? COL_SPECTRUM_POST : COL_TEXT_SECONDARY);
        g.setFont (monoFont (presentation, typography::TextRole::legend,
                             typography::Composition::visualization));
        text_style::drawText (g, state, field.reduced (5, 3),
                    fieldAvailable ? juce::Justification::topLeft : juce::Justification::centred);
    }
    drawBareMetric (g, balance, "BAL", balanceText (meter, available), presentation);
    drawBareMetric (g, correlation, "CORR",
                    available && std::isfinite (meter.correlation)
                        ? juce::String (meter.correlation, 2) : juce::String ("---"),
                    presentation);
}
}

juce::Rectangle<int> plotBounds (juce::Rectangle<int> area, bool compact, presentation::Context context)
{
    return layoutFor (area, compact, context).plot;
}

void paint (juce::Graphics& g,
            juce::Rectangle<int> area,
            const KirinMeterSession& meter,
            const mono_sum_history::History& monoHistory,
            bool available,
            bool compactMeter,
            presentation::Context presentation, space_live::Display* live)
{
    const bool compact = compactMeter;
    drawPanel (g, area, compact);
    const auto placement = layoutFor (area, compact, presentation);
    const bool glance = placement.glance;
    const auto title = placement.title;
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (monoFont (presentation, typography::TextRole::legend,
                         typography::Composition::visualization));
    text_style::drawText (g, live ? "80 ms" : "3 S",
                title, juce::Justification::centredLeft);
    const bool fieldAvailable = available && meter.channels == 2
                             && meter.field_size == KIRIN_STEREO_FIELD_SIZE
                             && meter.field_observation_count > 0u;
    const auto fieldState = ! available ? hypha::emDash()
                          : meter.channels != 2 ? juce::String ("MONO INPUT")
                          : meter.field_observation_count < 30u
                              ? "WARMING " + juce::String (meter.field_observation_count) + "/30"
                              : juce::String ("30/30");
    const auto compactFieldState = ! available ? hypha::emDash()
                                 : meter.channels != 2 ? juce::String ("MONO")
                                 : meter.field_observation_count < 30u
                                     ? juce::String (meter.field_observation_count) + "/30"
                                     : juce::String ("30/30");
    if (glance)
    {
        paintGlance (g, placement, meter, available, fieldAvailable, compactFieldState, presentation, live);
        return;
    }
    g.setColour (fieldAvailable ? COL_SPECTRUM_POST : COL_MUTED);
    if (! live || meter.channels == 1)
        text_style::drawText (g, compact ? compactFieldState : fieldState,
                title, juce::Justification::centredRight);

    const auto& field = placement.field;
    const auto& mono = placement.mono;
    const auto& balance = placement.balance;
    const auto& correlationBox = placement.correlation;
    drawPanel (g, field, compact);
    main_frame::paint (g, field.toFloat());
    const auto plot = placement.plot.toFloat();
    drawFieldAxes (g, plot);
    if (live) live->paint (g, plot);
    else if (fieldAvailable) drawDensity (g, plot, meter);
    if (! fieldAvailable && ! compact && ! live)
    {
        g.setColour (COL_TEXT_SECONDARY);
        g.setFont (monoFont (presentation, typography::TextRole::status,
                             typography::Composition::visualization));
        text_style::drawText (g, fieldState, plot.getSmallestIntegerContainer(),
                    juce::Justification::centred);
    }
    if (! mono.isEmpty())
    {
        drawPanel (g, mono, compact);
        mono_sum_curve::paint (g, mono.reduced (4, 3), meter, monoHistory, available, false,
                               true, presentation);
    }

    drawMetric (g, balance, compact ? "BAL" : "L/R BALANCE",
                balanceText (meter, available), "dB L/R", compact, presentation);
    const auto correlation = available && std::isfinite (meter.correlation)
        ? juce::String (meter.correlation, 2) : juce::String ("---");
    drawMetric (g, correlationBox, compact ? "CORR" : "CORRELATION", correlation, "3 S",
                compact, presentation);
}
}

namespace hypha::space_live
{
bool Display::nativeShowing (const juce::ComponentPeer& peer)
{
   #if JUCE_MAC
    // Plug-in peers are NSViews. A host can conceal their native ancestors without
    // changing any JUCE Component flags; only the message thread queries AppKit.
    const auto view = static_cast<id> (peer.getNativeHandle());
    if (view == nullptr) return false;
    const auto object = reinterpret_cast<id (*) (id, SEL)> (objc_msgSend);
    const auto flag = reinterpret_cast<BOOL (*) (id, SEL)> (objc_msgSend);
    const auto window = object (view, sel_registerName ("window"));
    return window != nullptr && flag (window, sel_registerName ("isVisible"))
        && ! flag (window, sel_registerName ("isMiniaturized"))
        && ! flag (view, sel_registerName ("isHiddenOrHasHiddenAncestor"));
   #elif JUCE_WINDOWS
    // An embedded peer's own flags do not reflect a hidden or minimised native host parent.
    auto window = static_cast<HWND> (peer.getNativeHandle());
    if (window == nullptr || ! IsWindowVisible (window)) return false;
    for (; window != nullptr; window = GetAncestor (window, GA_PARENT))
        if (IsIconic (window)) return false;
   #else
    juce::ignoreUnused (peer);
   #endif
    return true;
}
void Display::buildRaster (int pixels)
{
    raster = juce::Image (juce::Image::ARGB, pixels, pixels, true);
    ink.assign (static_cast<std::size_t> (pixels * pixels), 0.0f);
    const auto warp = [] (double v) { return std::copysign (std::sqrt (std::min (1.0, std::abs (v))), v); };
    for (std::size_t i = 0; i < snapshot.samples.size(); ++i)
    {
        const auto s = snapshot.samples[i];
        if (! std::isfinite (s.left) || ! std::isfinite (s.right)
            || std::abs (s.left) + std::abs (s.right) < 1.0e-12f) continue;
        const float x = static_cast<float> ((warp ((double (s.left) - s.right) * 0.5) + 1.0) * 0.5 * (pixels - 2));
        const float y = static_cast<float> ((1.0 - warp ((double (s.left) + s.right) * 0.5)) * 0.5 * (pixels - 2));
        const int ix = static_cast<int> (x), iy = static_cast<int> (y);
        const float fx = x - ix, fy = y - iy;
        // Old points leave a faint layer under the bright newest samples, in one fixed window.
        const float weight = 0.22f + 0.78f * static_cast<float> (i + 1) / static_cast<float> (snapshot.samples.size());
        const auto add = [&] (int dx, int dy, float amount)
        { ink[static_cast<std::size_t> ((iy + dy) * pixels + ix + dx)] += amount * weight; };
        add (0, 0, (1 - fx) * (1 - fy)); add (1, 0, fx * (1 - fy));
        add (0, 1, (1 - fx) * fy); add (1, 1, fx * fy);
    }
    juce::Image::BitmapData data (raster, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < pixels; ++y)
        for (int x = 0; x < pixels; ++x)
        {
            const float amount = ink[static_cast<std::size_t> (y * pixels + x)];
            if (amount <= 0.0f) continue;
            const float strength = 1.0f - std::exp (-amount * 6.0f);
            data.setPixelColour (x, y, COL_SPECTRUM_POST.interpolatedWith (COL_FLORA_BR, strength * 0.32f)
                .withAlpha (strength * 0.94f));
        }
}
}
