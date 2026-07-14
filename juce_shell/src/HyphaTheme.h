#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

// B-054: element-for-element port of crates/hypha_gui/{palette,common,led}.rs into JUCE.
// palette.rs is the single source of truth for colour (no new colours hardcoded) — every
// constant below is the exact RGB from palette.rs. No red / pure white (#ffffff) / neon
// (品位原則 / G-72-10). Formatting/colour helpers mirror common.rs (fmt_val / fmt_delta /
// val_color / tp_color / tp_over); breathe()/dim() mirror led.rs.
namespace hypha
{
    // ── palette.rs (exact) ───────────────────────────────────────────────────────────────
    inline const juce::Colour BG            { 0xff0d0f1a }; // #0D0F1A panel/window fill
    inline const juce::Colour COL_NORMAL    { 0xffe0e0e0 }; // #E0E0E0 values / title (not pure white)
    inline const juce::Colour COL_MUTED     { 0xff606060 }; // #606060 labels / units / "---"
    inline const juce::Colour COL_FLORA     { 0xffd4a043 }; // #D4A043 name / flora line / Keeping / preset LED
    inline const juce::Colour COL_FLORA_BR  { 0xffffe0a0 }; // #FFE0A0 TP > -1.0 dBTP (luminance up, same hue)
    inline const juce::Colour COL_LED_BLUE  { 0xff4488cc }; // #4488CC WatchBreathing
    inline const juce::Colour COL_LED_GREEN { 0xff4cc07a }; // #4CC07A RecordStandby / RecordActive
    inline const juce::Colour COL_LED_YELLOW{ 0xffccaa44 }; // #CCAA44 Error (measure thread)
    inline const juce::Colour COL_LED_GREY  { 0xff555558 }; // #555558 Idle

    // Field / button background fill. NOT a new palette colour: derived from BG (lifted
    // slightly). egui styles widgets via framework visuals — palette.rs has no widget-bg
    // constant — so we derive one from BG here (single derivation). Still far from #ffffff
    // (Dark Cockpit). Defined after BG so its static initialisation is sequenced after it.
    inline const juce::Colour kFieldFill = BG.brighter (0.05f);

    // ── fonts: exact native families also loaded by hypha_gui::install_native_font_contract ─
    inline juce::Font labelFont (float h)
    {
        return juce::Font (".SF NS", h, juce::Font::plain);
    }
    inline juce::Font monoFont  (float h)
    {
        return juce::Font (".SF NS Mono", h, juce::Font::plain);
    }

    // Δ (U+0394) built from its codepoint to avoid non-ASCII source/escape issues.
    inline juce::String delta() { return juce::String::charToString ((juce::juce_wchar) 0x0394); }

    // ── common.rs: value/delta formatting (1 decimal, "---" for NaN/None) ──────────────────
    inline juce::String fmtVal (double v)
    {
        if (std::isnan (v)) return "---";
        return juce::String (v, 1);
    }

    inline juce::String fmtDelta (double v)
    {
        if (std::isnan (v)) return "---";
        // fmt_delta: Some(x>=0) -> "+{:.1}", Some(x) -> "{:.1}" (the minus comes from the number).
        return (v >= 0.0 ? "+" : "") + juce::String (v, 1);
    }

    // val_color: Some -> COL_NORMAL, None -> COL_MUTED.
    inline juce::Colour valColour (double v) { return std::isnan (v) ? COL_MUTED : COL_NORMAL; }

    // tp_over: tp.is_some() && tp > -1.0 dBTP.
    inline bool tpOver (double v) { return ! std::isnan (v) && v > -1.0; }

    // tp_color: > -1.0 -> COL_FLORA_BR ; Some -> COL_NORMAL ; None -> COL_MUTED.
    inline juce::Colour tpColour (double v)
    {
        if (std::isnan (v)) return COL_MUTED;
        return tpOver (v) ? COL_FLORA_BR : COL_NORMAL;
    }

    // ── led.rs: dim() / breathe() ──────────────────────────────────────────────────────────
    inline juce::Colour dim (juce::Colour c, float f)
    {
        auto ch = [f] (juce::uint8 v) { return (juce::uint8) juce::jlimit (0, 255, juce::roundToInt ((float) v * f)); };
        return juce::Colour (ch (c.getRed()), ch (c.getGreen()), ch (c.getBlue()));
    }

    // breathe(base, t, period, minF, maxF): sin_01 = (sin(2π t / period) + 1) * 0.5 ;
    // factor = minF + (maxF - minF) * sin_01 ; dim each channel by factor.
    inline juce::Colour breathe (juce::Colour base, double t, double period, float minF, float maxF)
    {
        const double sin01 = (std::sin (juce::MathConstants<double>::twoPi * t / period) + 1.0) * 0.5;
        const float  factor = minF + (maxF - minF) * (float) sin01;
        return dim (base, factor);
    }

    // ── exact hover help strings (common.rs / editor.rs). em dash = U+2014, UTF-8. ──────────
    inline juce::String helpLufsM() { return juce::CharPointer_UTF8 ("Momentary Loudness \xe2\x80\x94 ITU BS.1770 (LUFS)"); }
    inline juce::String helpTp()    { return juce::CharPointer_UTF8 ("True Peak \xe2\x80\x94 inter-sample peak, ITU BS.1770 (dBTP)"); }
    inline juce::String helpCrest() { return juce::CharPointer_UTF8 ("Crest Factor \xe2\x80\x94 peak vs RMS difference (dB)"); }
    inline juce::String helpPsr()   { return juce::CharPointer_UTF8 ("Peak to Short-term Loudness Ratio (dB)"); }
    inline juce::String helpN()     { return juce::CharPointer_UTF8 ("Loudness \xe2\x80\x94 Zwicker model, ISO 532-1 (sone)"); }
    inline juce::String helpSharp() { return juce::CharPointer_UTF8 ("Sharpness \xe2\x80\x94 high-frequency content weighting,\nDIN 45692 (acum)"); }
}
