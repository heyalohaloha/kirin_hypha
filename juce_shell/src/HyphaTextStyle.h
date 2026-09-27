#pragma once

#include "HyphaTheme.h"

// Every piece of Hypha's screen text is drawn through these calls (INV-S40). They show the text
// in the current language (HyphaLanguage.h) and, when that text is Japanese, in the native text
// font at the height the caller chose. English is drawn exactly as the caller asked.
namespace hypha::text_style
{
// The text the calls below show for `text`, and the width it takes when drawn in `font`.
juce::String shownText (const juce::String&);
float shownWidth (const juce::Font&, const juce::String&);

int requiredWidth (const juce::Font&, const juce::String&,
                   const typography::TextStyle&, int minimum = 0);
int requiredLineHeight (const typography::TextStyle&, int minimum = 0) noexcept;
juce::String ellipsizedText (const juce::String&, const juce::Font&, float width);
void draw (juce::Graphics&, const juce::String&, juce::Rectangle<int>,
           const presentation::Context&, typography::TextRole,
           juce::Justification, int maximumLines = 1,
           typography::Composition = typography::Composition::shell);
void drawEllipsized (juce::Graphics&, const juce::String&, juce::Rectangle<int>,
                     juce::Justification);
// The height of `shown` (text already in the current language) wrapped to `width` in `font`, as
// drawLines and the wrapping roles draw it: Japanese breaks anywhere and keeps a fifth of a line
// between lines, since it has no spaces or descenders to separate them.
float wrappedHeight (const juce::String& shown, const juce::Font&, int width);
// Up to `maximumLines` lines, wrapped at spaces (or anywhere in Japanese) and never compressed.
void drawLines (juce::Graphics&, const juce::String&, juce::Rectangle<int>,
                juce::Justification, int maximumLines);
void drawText (juce::Graphics&, const juce::String&, juce::Rectangle<int>,
               juce::Justification, bool useEllipsesIfTooLong = true);
void drawText (juce::Graphics&, const juce::String&, juce::Rectangle<float>,
               juce::Justification, bool useEllipsesIfTooLong = true);
void drawText (juce::Graphics&, const juce::String&, int x, int y, int width, int height,
               juce::Justification, bool useEllipsesIfTooLong = true);
}
