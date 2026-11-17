#pragma once

#include "HyphaLuNeedle.h"
#include "HyphaUiPreferences.h"
#include "HyphaVuCalibrationFile.h"

#include <utility>

namespace hypha::lu_needle
{
// PRE has no Reference of its own. POST shares the 0 of its LU face (the song, its loudness and
// the set's range) with the PRE of the same chain, in one small file named by the chain's scope
// (the scope the shared 0 VU uses). The song at A's position is not shared: it moves ten times a
// second and belongs to POST's window. Message thread only; the audio path never touches it.
class SharedZero final
{
public:
    explicit SharedZero (juce::File directory = ui_preferences::defaultFile().getSiblingFile ("lu-reference"))
        : root (std::move (directory)) {}

    // POST: publishes when the shared part changed. A failed write is tried again on the next change.
    void publish (const juce::String& scope, const Facts& facts)
    {
        if (! validScope (scope)) return;
        const auto text = encoded (scope, facts);
        if (scope == writtenScope && text == writtenText) return;
        if (root.createDirectory().failed()) return;
        if (! vu_calibration::writeAtomicText (root.getChildFile (scope + ".txt"), text)) return;
        writtenScope = scope;
        writtenText = text;
    }

    // PRE: the shared 0, read at most four times a second. No file, another chain's file or a
    // damaged one all read as "Reference cannot be read", which keeps the VU face.
    Facts read (const juce::String& scope, juce::uint32 now)
    {
        if (scope.isEmpty()) return cached; // the chain is still being resolved: keep what was read
        if (scope == readScope && haveRead && now - readAt < refreshMs) return cached;
        readScope = scope;
        readAt = now;
        haveRead = true;
        cached = {};
        if (! validScope (scope)) return cached;
        const auto file = root.getChildFile (scope + ".txt");
        const auto size = file.getSize();
        if (! file.existsAsFile() || size <= 0 || size > 256) return cached;
        cached = decoded (scope, file.loadFileAsString());
        return cached;
    }

    static juce::String encoded (const juce::String& scope, const Facts& facts)
    {
        const auto milli = [] (double value)
        { return std::isfinite (value) ? juce::String (juce::roundToInt (value * 1000.0)) : juce::String ("-"); };
        const bool ready = facts.readable && facts.zero == Zero::ready;
        auto text = juce::String (header) + "\n" + scope + "\n"
            + (facts.readable ? "1" : "0") + " " + juce::String (static_cast<int> (facts.zero)) + " "
            + milli (ready ? facts.zeroLufs : nan) + " " + milli (ready ? facts.playedOffsetLu : nan) + " "
            + milli (facts.readable ? facts.band.lowLufs : nan) + " "
            + milli (facts.readable ? facts.band.highLufs : nan) + "\n";
        // The whole file stays within the atomic writer's 256 bytes: a long name is cut for sharing.
        auto name = facts.readable ? facts.song.replaceCharacters ("\r\n", "  ") : juce::String();
        while (name.isNotEmpty() && text.getNumBytesAsUTF8() + name.getNumBytesAsUTF8() + 1 > 250)
            name = name.dropLastCharacters (1);
        return text + name + "\n";
    }

    static Facts decoded (const juce::String& scope, const juce::String& text)
    {
        const auto lines = juce::StringArray::fromLines (text);
        if (lines.size() < 4 || lines[0] != header || lines[1] != scope) return {};
        const auto fields = juce::StringArray::fromTokens (lines[2], " ", {});
        if (fields.size() != 6) return {};
        const auto value = [&fields] (int index)
        {
            const auto& field = fields[index];
            if (field == "-" || field.isEmpty() || ! field.containsOnly ("-0123456789")) return nan;
            return static_cast<double> (field.getIntValue()) / 1000.0;
        };
        Facts facts;
        facts.readable = fields[0] == "1";
        if (! facts.readable) return {};
        const auto zero = fields[1].getIntValue();
        facts.zero = zero == 2 ? Zero::ready : zero == 1 ? Zero::noValue : Zero::noSong;
        facts.zeroLufs = value (2);
        facts.playedOffsetLu = std::isfinite (value (3)) ? value (3) : 0.0;
        if (facts.zero == Zero::ready && ! std::isfinite (facts.zeroLufs)) facts.zero = Zero::noValue;
        facts.band = { value (4), value (5) };
        if (! facts.band.valid() || facts.band.highLufs < facts.band.lowLufs) facts.band = {};
        facts.song = lines[3];
        return facts;
    }

private:
    static constexpr const char* header = "KIRIN_HYPHA_LU_ZERO_V1";
    static constexpr juce::uint32 refreshMs = 250;
    static bool validScope (const juce::String& scope)
    { return scope.length() == 64 && scope.containsOnly ("0123456789abcdef"); }

    juce::File root;
    juce::String writtenScope, writtenText, readScope;
    Facts cached;
    juce::uint32 readAt = 0;
    bool haveRead = false;
};
}
