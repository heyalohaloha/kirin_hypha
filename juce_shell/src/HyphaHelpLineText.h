#pragma once

#include <juce_core/juce_core.h>

#include <array>

// The help line (PluginEditorHelpLine.cpp, HyphaHelpLineBar.h). Pointing at a graph, a value or a
// tab, the line has the whole footer row (about 850 px at 300%) and shows the help as the bubble
// has it; only the few too long for that row have a shorter version in `row`. Pointing at a control
// in the footer, the line has only the status at the left (326 px at 300%), so the footer controls'
// helps have a short version in `footer`. All in English; the Japanese is in the catalog
// (HyphaJapaneseHelpLine.cpp) like every screen sentence. The bubble at 200% and below keeps the
// full help.
namespace hypha::help_line
{
struct Shortened
{
    const char* help;
    const char* line;
};

// Too long for the whole footer row (in English or in Japanese).
inline constexpr std::array<Shortened, 10> row {{
    { "Click to hold; measurement continues. HOST ~ is the window endpoint on the host project/render clock, "
      "not guaranteed project time or the exact peak. LIVE resumes scrolling.",
      "Click to hold; measurement continues. HOST ~ is the window end on the host clock, not project time." },
    { "Copy TP window endpoint on the host clock (project or render clock, not guaranteed project time)",
      "Copy the TP window end on the host clock (project or render clock)" },
    { "DELAY: POST arrival minus PRE arrival in this band, ms. Arrival is where the envelope rises through its peak - "
      "20 dB. RINGING: the previous hit still rings in this band, so the start cannot be timed.",
      "DELAY: POST arrival minus PRE arrival in this band, ms; arrival is where the envelope rises through peak - 20 dB." },
    { "ATT: the rise from 10 % to 90 % of the band peak, ms. A rise shorter than the band's time resolution (one "
      "period) reads as an upper bound, such as <16 ms.",
      "ATT: the rise from 10 % to 90 % of the band peak, ms; one shorter than a period reads as an upper bound." },
    { "REL: the fall from the band peak to peak - 20 dB, ms. >288 ms: still ringing where the measured tail ends. "
      "NEXT HIT: the next hit came first. LONG TAIL: both sides ring past the tail.",
      "REL: the fall from the band peak to peak - 20 dB, ms. >288 ms: still ringing where the tail ends." },
    { "LEVEL: the band's peak envelope level. POST - PRE in dB when paired, dBFS otherwise. When one side has no "
      "sound in the band, the difference reads as a bound, such as <-66 dB.",
      "LEVEL: the band's peak envelope level; POST - PRE in dB when paired, dBFS otherwise." },
    { "DELAY: POST arrival minus PRE arrival of each recent hit in this band, as dots; the bar is the median. The "
      "figure under it says how many hits lie on the median's side of zero.",
      "DELAY: POST minus PRE arrival of each recent hit as dots; the bar is the median." },
    { "ATT: the rise of each recent hit in this band, POST minus PRE, as dots; the bar is the median. Inside the "
      "shaded span (one period of the band) no difference can be told apart.",
      "ATT: POST minus PRE rise of each recent hit as dots; the bar is the median. The shade is one period." },
    { "The recent hits that rise in this band, summed up: each lane's median and how many hits agree. Click a dot to "
      "see that hit; END or LIVE returns here.",
      "Summary of the recent hits in this band: each lane's median and how many agree. Click a dot for that hit." },
    { "This PRE predates bands and sends none: update PRE to compare the band. POST's own values are shown meanwhile.",
      "This PRE predates bands: update PRE to compare the band. POST's own values are shown meanwhile." },
}};

// The footer's own controls: only the status at the left of the footer is theirs.
inline constexpr std::array<Shortened, 15> footer {{
    { "Finish Keep / Record before PRE / POST Blind", "Finish Keep / Record before Blind" },
    { "Fix the last four seconds of PRE and POST and open them in PRE / POST Blind", "Open the last 4 s of PRE and POST in Blind" },
    { "Switch between PRE and POST of this chain while the song plays. Keep this window open (pin it in Studio One "
      "/ Studio Pro, turn off Target in Pro Tools); closing or replacing it returns to POST",
      "Switch PRE and POST while the song plays" },
    { "Return POST to its normal level; it rises by the amount shown", "Return POST to its normal level" },
    { "End Blind Compare and return to live A. / END returns to A", "End Blind Compare and return to live A." },
    { "Start a separate Version Blind trial. Check Preset settings and facts are hidden.",
      "Start a separate Version Blind trial." },
    { "AUTO: PRE follows POST loudness within 0.5 dB, up to 6 dB from your MATCH. Press to MATCH again or stop AUTO",
      "AUTO: PRE follows POST loudness. Press to rematch" },
    { "MATCH stopped at the true-peak ceiling: PRE is still quieter than POST. Press to measure again",
      "MATCH stopped at the true-peak ceiling" },
    { "Press to MATCH again or to let PRE follow POST (AUTO)", "Press to MATCH again or follow (AUTO)" },
    { "Match PRE to POST loudness over the latest four seconds", "Match PRE to POST over the latest 4 s" },
    { "PRE is held because the latency changed. Stop and restart playback", "Latency changed: stop and restart playback" },
    { "PRE is selected. POST plays until PRE is confirmed at this position", "POST plays until PRE is confirmed here" },
    { "PRE waits while delay compensation is off in Pro Tools. Turn it on to hear PRE",
      "Turn on delay compensation to hear PRE" },
    { "Listen to PRE, the input of this chain, at the level MATCH set", "Listen to PRE at the MATCH level" },
    { "Listen to PRE, the input of this chain. MATCH levels it to POST", "Listen to PRE, the input of this chain" },
}};

// `wholeRow`: the line has the whole footer row (true) or only the status at its left (false).
inline juce::String forLine (const juce::String& help, bool wholeRow)
{
    // A bubble's later lines repeat the control's own name; the line keeps the first.
    const auto first = help.upToFirstOccurrenceOf ("\n", false, false);
    // ATTACK's band chips: the band, its range and how a hit is measured; switching stays in the bubble.
    if (wholeRow && first.startsWith ("Octave band ") && first.contains ("; switching"))
        return first.upToFirstOccurrenceOf ("; switching", false, false) + ".";
    const auto shortened = [&first] (const auto& table) -> juce::String
    {
        for (const auto& entry : table)
            if (first == entry.help) return entry.line;
        return first;
    };
    return wholeRow ? shortened (row) : shortened (footer);
}
}
