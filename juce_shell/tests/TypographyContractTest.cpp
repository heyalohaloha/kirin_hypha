#include "TypographyContractTest.h"

#include "../src/HyphaAnalysisUiText.h"
#include "../src/HyphaAttackStage.h"
#include "../src/HyphaAttackUiContract.h"
#include "../src/HyphaObservatoryContract.h"
#include "../src/HyphaSpacePainter.h"
#include "../src/HyphaSurfacePresentation.h"
#include "../src/HyphaTheme.h"
#include "../src/HyphaTextStyle.h"
#include "../src/HyphaTimeHistoryPainter.h"
#include "../src/HyphaTooltipLookAndFeel.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
namespace
{
void require (bool condition, const char* expression, int line)
{
    if (condition) return;
    std::cerr << "Typography contract failed at line " << line
              << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_TYPOGRAPHY_REQUIRE(expression) require ((expression), #expression, __LINE__)

constexpr std::array roles {
    typography::TextRole::shellTitle, typography::TextRole::navigation,
    typography::TextRole::primaryValue, typography::TextRole::secondaryValue,
    typography::TextRole::metricLabel, typography::TextRole::unit,
    typography::TextRole::axis, typography::TextRole::legend,
    typography::TextRole::readout, typography::TextRole::sectionTitle,
    typography::TextRole::body, typography::TextRole::status,
    typography::TextRole::action, typography::TextRole::selector,
    typography::TextRole::menu, typography::TextRole::tooltip,
    typography::TextRole::captureMetadata,
};

constexpr std::array compositions {
    typography::Composition::shell, typography::Composition::facts,
    typography::Composition::visualization, typography::Composition::information,
    typography::Composition::instrument,
};

bool fits (const juce::Font& font, const juce::String& text, float width)
{
    return std::ceil (font.getStringWidthFloat (text)) <= width;
}

double relativeLuminance (juce::Colour colour)
{
    const auto linear = [] (float channel)
    {
        return channel <= 0.04045f ? channel / 12.92
            : std::pow ((channel + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear (colour.getFloatRed())
         + 0.7152 * linear (colour.getFloatGreen())
         + 0.0722 * linear (colour.getFloatBlue());
}

double contrastRatio (juce::Colour foreground, juce::Colour background)
{
    const auto foregroundLuminance = relativeLuminance (foreground);
    const auto backgroundLuminance = relativeLuminance (background);
    const auto lighter = juce::jmax (foregroundLuminance, backgroundLuminance);
    const auto darker = juce::jmin (foregroundLuminance, backgroundLuminance);
    return (lighter + 0.05) / (darker + 0.05);
}

bool hasGlyph (const juce::Font& font, juce::juce_wchar codepoint)
{
    juce::Array<int> glyphs;
    juce::Array<float> offsets;
    font.getGlyphPositions (juce::String::charToString (codepoint), glyphs, offsets);
    return glyphs.size() == 1 && glyphs[0] != 0
        && offsets.size() == 2 && offsets[1] > offsets[0];
}

void verifyResolvedStyles()
{
    for (const auto role : roles)
        for (const auto composition : compositions)
        {
            auto previous = 0.0f;
            for (const auto preset : observatory::sizePresets)
            {
                const auto context = presentation::forEditor (preset.width, preset.height);
                const auto style = typography::resolve (context, role, composition);
                KIRIN_TYPOGRAPHY_REQUIRE (style.fontHeight >= 11.0f);
                KIRIN_TYPOGRAPHY_REQUIRE (style.lineHeight >= style.fontHeight);
                KIRIN_TYPOGRAPHY_REQUIRE (style.horizontalPadding >= 0.0f);
                KIRIN_TYPOGRAPHY_REQUIRE (style.fontHeight >= previous);
                KIRIN_TYPOGRAPHY_REQUIRE (
                    std::abs (style.fontHeight
                              - typography::roleScale (role, composition)[
                                  static_cast<std::size_t> (presentation::densityIndex (preset.density))])
                    < 0.001f);
                previous = style.fontHeight;
            }
        }

    for (const auto preset : observatory::sizePresets)
    {
        const auto context = presentation::forEditor (preset.width, preset.height);
        KIRIN_TYPOGRAPHY_REQUIRE (
            typography::resolve (context, typography::TextRole::primaryValue,
                                 typography::Composition::facts).fontHeight
            > typography::resolve (context, typography::TextRole::secondaryValue,
                                   typography::Composition::facts).fontHeight);
    }

    for (const auto boundary : { 338, 413, 525, 750 })
        for (const auto role : roles)
        {
            const auto before = typography::resolve (
                presentation::forEditor (boundary - 1, (boundary - 1) * 2 / 3), role).fontHeight;
            const auto after = typography::resolve (
                presentation::forEditor (boundary, boundary * 2 / 3), role).fontHeight;
            KIRIN_TYPOGRAPHY_REQUIRE (after >= before);
            KIRIN_TYPOGRAPHY_REQUIRE (after - before < 0.1f);
        }

    const auto compact = presentation::forEditor (300, 200);
    const auto inspection = presentation::forEditor (900, 600);
    KIRIN_TYPOGRAPHY_REQUIRE (
        typography::resolve (compact, typography::TextRole::primaryValue,
                             typography::Composition::facts).fontHeight
        > typography::resolve (compact, typography::TextRole::secondaryValue,
                               typography::Composition::facts).fontHeight);
    KIRIN_TYPOGRAPHY_REQUIRE (
        typography::resolve (compact, typography::TextRole::sectionTitle,
                             typography::Composition::information).fontHeight
        > typography::resolve (compact, typography::TextRole::body,
                               typography::Composition::information).fontHeight);
    KIRIN_TYPOGRAPHY_REQUIRE (
        typography::resolve (compact, typography::TextRole::body).overflow
        == typography::Overflow::wrap);
    KIRIN_TYPOGRAPHY_REQUIRE (
        typography::resolve (compact, typography::TextRole::selector).overflow
        == typography::Overflow::ellipsize);
    KIRIN_TYPOGRAPHY_REQUIRE (
        typography::resolve (compact, typography::TextRole::primaryValue).overflow
        == typography::Overflow::preserve);

    const auto compactPopup = presentation::forOutput (
        300, 200, presentation::OutputTarget::popup);
    const auto inspectionPopup = presentation::forOutput (
        900, 600, presentation::OutputTarget::popup);
    const auto compactTooltip = presentation::forOutput (
        300, 200, presentation::OutputTarget::tooltip);
    const auto inspectionTooltip = presentation::forOutput (
        900, 600, presentation::OutputTarget::tooltip);
    KIRIN_TYPOGRAPHY_REQUIRE (std::abs (
        typography::resolve (compactPopup, typography::TextRole::menu).fontHeight
        - typography::resolve (inspectionPopup, typography::TextRole::menu).fontHeight) < 0.001f);
    KIRIN_TYPOGRAPHY_REQUIRE (std::abs (
        typography::resolve (compactTooltip, typography::TextRole::tooltip).fontHeight
        - typography::resolve (inspectionTooltip, typography::TextRole::tooltip).fontHeight) < 0.001f);

    const auto compactHeight = labelFont (
        compact, typography::TextRole::shellTitle).getHeight();
    const auto inspectionHeight = labelFont (
        inspection, typography::TextRole::shellTitle).getHeight();
    KIRIN_TYPOGRAPHY_REQUIRE (inspectionHeight > compactHeight);
    KIRIN_TYPOGRAPHY_REQUIRE (std::abs (
        labelFont (compact, typography::TextRole::shellTitle).getHeight() - compactHeight) < 0.001f);
}

void verifyPreservedSurfaceText()
{
    KIRIN_TYPOGRAPHY_REQUIRE (contrastRatio (COL_TEXT_SECONDARY, BG) >= 4.5);
    KIRIN_TYPOGRAPHY_REQUIRE (contrastRatio (COL_TEXT_TERTIARY, BG) >= 4.5);
    KIRIN_TYPOGRAPHY_REQUIRE (
        contrastRatio (COL_TEXT_SECONDARY, BG) > contrastRatio (COL_TEXT_TERTIARY, BG));

    for (const auto preset : observatory::sizePresets)
    {
        const auto context = presentation::forEditor (preset.width, preset.height);
        const auto body = ui_contract::spectrumPlotBounds (preset.width, preset.height);
        const auto actionStyle = typography::resolve (
            context, typography::TextRole::action,
            typography::Composition::visualization);
        const auto actionFont = monoFont (
            context, typography::TextRole::action,
            typography::Composition::visualization);
        const auto actionRequired = juce::jmax (
            text_style::requiredWidth (actionFont, "VIEW  OVERLAY", actionStyle),
            text_style::requiredWidth (actionFont, "VIEW  2 ROWS", actionStyle));
        KIRIN_TYPOGRAPHY_REQUIRE (fits (
            actionFont, "VIEW  OVERLAY",
            attack_ui::modeControlWidth (body.width, actionRequired)));

        // DRUM lanes: names, values and withheld reasons fit their columns at every editor size.
        const auto shell = observatory::shellLayout (observatory::Role::post, preset,
                                                     observatory::GuidePresence::absent);
        const auto drum = attack_ui::layoutFor (
            shell.body.width, shell.body.height - observatory::timeNavigationHeight (preset.density),
            context);
        // Widths use the fonts the DRUM painters use, including label tracking.
        const auto required = [&context] (typography::TextRole role, const juce::String& text,
                                          float tracking = 0.0f) {
            return text_style::requiredWidth (attack_stage::trackedFont (context, role, tracking),
                text, typography::resolve (context, role, typography::Composition::visualization)); };
        if (drum.arrangement == attack_ui::Arrangement::lanes)
        {
            for (const auto* name : { "TRANSIENT", "STRENGTH", "CREST", "SHARPNESS", "HISTORY" })
                KIRIN_TYPOGRAPHY_REQUIRE (required (typography::TextRole::metricLabel, name,
                                                    attack_stage::labelTracking (context))
                                          <= drum.labelWidth - 8);
            for (const auto* value : { "+12.3 dB", "-10.0 dBFS", "-0.12 acum", "5.25 acum" })
                KIRIN_TYPOGRAPHY_REQUIRE (required (typography::TextRole::secondaryValue, value)
                                          <= drum.readoutWidth - 12);
            for (const auto* reason : { "QUIET AFTER", "NEXT HIT", "POST ONLY" })
                KIRIN_TYPOGRAPHY_REQUIRE (required (typography::TextRole::readout, reason,
                                                    attack_stage::captionTracking (context))
                                          <= drum.readoutWidth - 12);
        }
        else
        {
            // Each one-row cell is led by its lane accent; the text receives the rest.
            for (std::size_t lane = 0; lane < attack_ui::laneCount; ++lane)
                KIRIN_TYPOGRAPHY_REQUIRE (required (typography::TextRole::readout, "TR +12.3")
                                          <= attack_ui::lineCell (drum, lane).width
                                               - attack_ui::lineAccentWidth);
        }
        KIRIN_TYPOGRAPHY_REQUIRE (
            attack_ui::headerHeightFor (context)
            >= attack_ui::titleRowHeight (context) + attack_ui::statusRowHeight (context));

        const auto axisFont = monoFont (
            context, typography::TextRole::axis,
            typography::Composition::visualization);
        KIRIN_TYPOGRAPHY_REQUIRE (fits (
            axisFont, preset.density == observatory::Density::compact ? "S<0" : "SIDE < 0",
            space_field::axisLabelWidth (
                context, preset.density == observatory::Density::compact)));

        const auto bodyFont = monoFont (
            context, typography::TextRole::body,
            typography::Composition::visualization);
        const auto plrDefinition = preset.width >= 600
            ? "SESSION FACT / TP MAX - LUFS-I" : "TP MAX - LUFS-I";
        KIRIN_TYPOGRAPHY_REQUIRE (fits (
            bodyFont, plrDefinition,
            time_history::auxLabelWidth (context, true, false, body.width)));
    }
}

void verifySurfaceInventory()
{
    KIRIN_TYPOGRAPHY_REQUIRE (
        surface_presentation::descriptors.size()
        == static_cast<std::size_t> (surface_presentation::Id::count));
    for (std::size_t index = 0; index < surface_presentation::descriptors.size(); ++index)
    {
        const auto& surface = surface_presentation::descriptors[index];
        KIRIN_TYPOGRAPHY_REQUIRE (static_cast<std::size_t> (surface.id) == index);
        KIRIN_TYPOGRAPHY_REQUIRE (juce::String (surface.label).isNotEmpty());
    }
    for (const auto page : analysis_navigation::timePages)
        KIRIN_TYPOGRAPHY_REQUIRE (
            surface_presentation::descriptor (surface_presentation::forTimePage (page))
                .composition != typography::Composition::shell);
}

void verifyProductTypography()
{
    constexpr auto compact = presentation::forEditor (300, 200);
    const auto titleFont = labelFont (compact, typography::TextRole::shellTitle);
    const auto statusFont = monoFont (compact, typography::TextRole::status);
    if (usingKimeraTypography())
    {
        KIRIN_TYPOGRAPHY_REQUIRE (titleFont.getTypefaceName().containsIgnoreCase ("Waldenburg"));
        KIRIN_TYPOGRAPHY_REQUIRE (statusFont.getTypefaceName().containsIgnoreCase ("Waldenburg"));
    }
    else
    {
        KIRIN_TYPOGRAPHY_REQUIRE (titleFont.getTypefaceName().equalsIgnoreCase (
            nativeFallbackLabelFontFamily()));
        KIRIN_TYPOGRAPHY_REQUIRE (statusFont.getTypefaceName().equalsIgnoreCase (
            nativeFallbackMonoFontFamily()));
    }
#if KIRIN_HYPHA_KIMERA_EMBEDDED
    KIRIN_TYPOGRAPHY_REQUIRE (usingKimeraTypography());
#endif
    KIRIN_TYPOGRAPHY_REQUIRE (std::abs (tabularTextWidth (statusFont, "-11.1")
                                        - tabularTextWidth (statusFont, "-88.8")) < 0.01f);

    KIRIN_TYPOGRAPHY_REQUIRE (fits (titleFont, ui_contract::preTitle,
                                    static_cast<float> (ui_contract::preTitleWidth)));
    KIRIN_TYPOGRAPHY_REQUIRE (fits (titleFont, ui_contract::postTitle,
                                    static_cast<float> (ui_contract::preTitleWidth + 8)));

    const auto deltaFont = labelFont (compact, typography::TextRole::metricLabel,
                                      typography::Composition::facts);
    KIRIN_TYPOGRAPHY_REQUIRE (delta().length() == 1 && delta()[0] == 0x0394);
    KIRIN_TYPOGRAPHY_REQUIRE (emDash().length() == 1 && emDash()[0] == 0x2014);
    KIRIN_TYPOGRAPHY_REQUIRE (hasGlyph (deltaFont, 0x0394));
    for (const auto codepoint : { juce::juce_wchar { 0x25cf }, juce::juce_wchar { 0x25cc },
                                  juce::juce_wchar { 0x2014 } })
        KIRIN_TYPOGRAPHY_REQUIRE (hasGlyph (statusFont, codepoint));

    const auto menu = presentation::forOutput (300, 200, presentation::OutputTarget::popup);
    KIRIN_TYPOGRAPHY_REQUIRE (hasGlyph (labelFont (menu, typography::TextRole::menu), 0x00b7));
    KIRIN_TYPOGRAPHY_REQUIRE (hasGlyph (nativeTextFont (menu, typography::TextRole::menu), 0x66f4));
    KIRIN_TYPOGRAPHY_REQUIRE (fits (statusFont, juce::CharPointer_UTF8 ("PAIR ●"),
                                    static_cast<float> (ui_contract::pairStatusWidth)));
    KIRIN_TYPOGRAPHY_REQUIRE (fits (statusFont, juce::CharPointer_UTF8 ("PAIR ◌"),
                                    static_cast<float> (ui_contract::pairStatusWidth)));
    KIRIN_TYPOGRAPHY_REQUIRE (fits (statusFont, juce::CharPointer_UTF8 ("PAIR —"),
                                    static_cast<float> (ui_contract::pairStatusWidth)));

    const auto selectorFont = monoFont (compact, typography::TextRole::selector);
    const auto legendFont = monoFont (compact, typography::TextRole::legend,
                                      typography::Composition::visualization);
    const auto readoutFont = monoFont (compact, typography::TextRole::readout,
                                       typography::Composition::visualization);
    KIRIN_TYPOGRAPHY_REQUIRE (fits (selectorFont, "DRUM BUS", 160.0f));
    KIRIN_TYPOGRAPHY_REQUIRE (fits (selectorFont, "pair: DRUM BUS", 160.0f));
    KIRIN_TYPOGRAPHY_REQUIRE (fits (legendFont, "PRE", ui_contract::spectrumPreLegendLabelWidth));
    KIRIN_TYPOGRAPHY_REQUIRE (fits (legendFont, "POST", ui_contract::spectrumPostLegendLabelWidth));
    KIRIN_TYPOGRAPHY_REQUIRE (fits (readoutFont, "22.0 kHz", ui_contract::spectrumHoverFrequencyWidth));
    KIRIN_TYPOGRAPHY_REQUIRE (fits (readoutFont, juce::CharPointer_UTF8 ("Δ+24.0"),
                                    ui_contract::spectrumHoverDeltaWidth));
    KIRIN_TYPOGRAPHY_REQUIRE (fits (readoutFont, "PRE -144.0", ui_contract::spectrumExpandedPreWidth));
    KIRIN_TYPOGRAPHY_REQUIRE (fits (readoutFont, "POST -144.0", ui_contract::spectrumExpandedPostWidth));

    const auto slotsText = analysis_ui::slotsInUse ("Mix, Vocal");
    KIRIN_TYPOGRAPHY_REQUIRE (! slotsText.containsChar ('\n'));
    KIRIN_TYPOGRAPHY_REQUIRE (analysis_ui::slotsInUse ({}).equalsIgnoreCase ("ANALYSIS IN USE"));
    const auto compactAnalysisBounds = ui_contract::spectrumPlotBounds (300, 200);
    KIRIN_TYPOGRAPHY_REQUIRE (fits (
        monoFont (compact, typography::TextRole::status, typography::Composition::visualization),
        slotsText, compactAnalysisBounds.width - ui_contract::spectrumPlotLeftInset
                 - ui_contract::spectrumPlotRightInset));

    const auto tooltipFont = labelFont (
        presentation::forOutput (300, 200, presentation::OutputTarget::tooltip),
        typography::TextRole::tooltip);
    const auto tooltipMaximumWidth = ui_contract::editorWidth - 2 * ui_contract::margin - 14;
    const juce::StringArray tooltipTexts {
        ui_contract::spectrumTooltip (false), ui_contract::spectrumTooltip (true),
        analysis_ui::switchViewTooltip ("Frequency Delta"),
        analysis_ui::switchViewTooltip ("Sharpness Delta"),
        analysis_ui::switchViewTooltip ("POST live facts")
    };
    for (const auto& text : tooltipTexts)
        KIRIN_TYPOGRAPHY_REQUIRE (fits (tooltipFont, text, tooltipMaximumWidth));
}

void verifyTooltipBounds()
{
    const juce::StringArray texts {
        analysis_ui::channelModeTooltip (0u), analysis_ui::spectrumPlotTooltip(),
        analysis_ui::focusTrailTooltip (true), analysis_ui::sharpnessDeltaTooltip(),
        analysis_ui::liveOverviewTooltip(), helpLufsM(), helpLufsS(), helpTp(), helpSharp()
    };
    TooltipLookAndFeel lookAndFeel;
    for (const auto preset : observatory::sizePresets)
    {
        const juce::Rectangle<int> parent (0, 0, preset.width, preset.height);
        const auto available = parent.reduced (ui_contract::margin);
        for (const auto& text : texts)
            for (const auto position : { parent.getTopLeft(), parent.getTopRight(),
                                         parent.getBottomLeft(), parent.getBottomRight() })
                KIRIN_TYPOGRAPHY_REQUIRE (
                    available.contains (lookAndFeel.getTooltipBounds (text, position, parent)));
    }
}

// A status that may wrap (INACTIVE, PREPARING ANALYSIS) is justified inside its own area: a
// centred status stands whole in the middle, never half outside on the right.
void verifyWrappedStatusPlacement()
{
    const auto context = presentation::forEditor (300, 200);
    const juce::Rectangle<int> area (10, 10, 240, 40);
    for (const auto justification : { juce::Justification::centred, juce::Justification::centredLeft,
                                      juce::Justification::centredRight })
    {
        juce::Image image (juce::Image::ARGB, 260, 60, true);
        juce::Graphics g (image);
        g.setColour (juce::Colours::white);
        g.setFont (monoFont (context, typography::TextRole::status, typography::Composition::visualization));
        text_style::draw (g, "INACTIVE", area, context, typography::TextRole::status, justification, 2,
                          typography::Composition::visualization);
        int left = image.getWidth(), right = -1;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                if (image.getPixelAt (x, y).getAlpha() > 64)
                {
                    left = std::min (left, x);
                    right = std::max (right, x);
                }
        const auto textWidth = g.getCurrentFont().getStringWidthFloat ("INACTIVE");
        KIRIN_TYPOGRAPHY_REQUIRE ((float) (right - left + 1) >= textWidth * 0.8f);
        if (justification == juce::Justification::centred)
            KIRIN_TYPOGRAPHY_REQUIRE (std::abs ((left + right) / 2 - area.getCentreX()) <= 3);
        else if (justification == juce::Justification::centredLeft)
            KIRIN_TYPOGRAPHY_REQUIRE (left - area.getX() <= 3);
        else
            KIRIN_TYPOGRAPHY_REQUIRE (area.getRight() - right <= 4);
    }
}
}

void verifyTypographyContract()
{
    verifyWrappedStatusPlacement();
    verifyResolvedStyles();
    verifyPreservedSurfaceText();
    verifySurfaceInventory();
    verifyProductTypography();
    verifyTooltipBounds();
    std::cout << "Typography contract: PASS (17 roles, 5 compositions, 5 anchors, 14 surfaces)\n";
}
}
