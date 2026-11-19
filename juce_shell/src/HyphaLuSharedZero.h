#pragma once

#include "HyphaLuNeedle.h"
#include "HyphaUiPreferences.h"
#include "HyphaVuCalibrationFile.h"
#include <memory>

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

    // Publication requires the same retained chain lease as PSR/CREST. This
    // class never elects a second owner. Non-RT processor service only.
    void publish (const juce::String& scope, const Facts& facts, const std::shared_ptr<void>& lease,
                  juce::int64 now = juce::Time::currentTimeMillis())
    {
        if (!lease || !validScope (scope) || now <= 0) return;
        if (scope != writtenScope) { writtenScope = scope; writtenAt = 0; }
        if (writtenAt > 0 && now >= writtenAt && now - writtenAt < refreshMs) return;
        if (revision >= 999999999999999999ULL) return;
        const auto prefix = juce::String (leaseHeader) + "\n" + owner + " "
            + juce::String (++revision) + " " + juce::String (now) + "\n";
        const auto text = prefix + encoded (scope, facts, 250 - static_cast<int> (prefix.getNumBytesAsUTF8()));
        if (root.isSymbolicLink() || root.createDirectory().failed()) return;
        if (vu_calibration::writeAtomicText (root.getChildFile (scope + ".txt"), text)) writtenAt = now;
    }

    Facts read (const juce::String& scope, juce::int64 now = juce::Time::currentTimeMillis())
    {
        if (! validScope (scope)) { cached = {}; readScope.clear(); haveRead = false; return {}; }
        if (scope != readScope) { cached = {}; readScope = scope; readOwner.clear(); readRevision = 0; cachedAt = 0; haveRead = false; }
        if (haveRead && now >= readAt && now - readAt < refreshMs)
            return fresh (cachedAt, now) ? cached : Facts {};
        readAt = now; haveRead = true; cached = {};
        const auto file = root.getChildFile (scope + ".txt");
        const auto size = file.getSize();
        if (root.isSymbolicLink() || file.isSymbolicLink() || !file.existsAsFile() || size <= 0 || size > 256) return {};
        const auto text = file.loadFileAsString();
        const auto lines = juce::StringArray::fromLines (text);
        if (lines.size() < 6 || lines[0] != leaseHeader) return {}; // unleased V1 cannot stay alive forever
        const auto fields = juce::StringArray::fromTokens (lines[1], " ", {});
        if (fields.size() != 3 || fields[0].length() != 32 || !fields[0].containsOnly ("0123456789abcdef")
            || fields[1].isEmpty() || fields[1].length() > 18 || !fields[1].containsOnly ("0123456789")
            || fields[2].isEmpty() || fields[2].length() > 18 || !fields[2].containsOnly ("0123456789")) return {};
        const auto generation = fields[1].getLargeIntValue(), at = fields[2].getLargeIntValue();
        if (generation <= 0 || !fresh (at, now)) return {};
        if (readOwner == fields[0] && (generation < readRevision || at < cachedAt)) return {};
        if (cachedAt > 0 && at < cachedAt) return {};
        readOwner = fields[0]; readRevision = generation; cachedAt = at;
        cached = decoded (scope, text.fromFirstOccurrenceOf ("\n", false, false)
                                     .fromFirstOccurrenceOf ("\n", false, false));
        return cached;
    }

    static juce::String encoded (const juce::String& scope, const Facts& facts, int maxBytes = 250)
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
        while (name.isNotEmpty() && text.getNumBytesAsUTF8() + name.getNumBytesAsUTF8() + 1 > maxBytes)
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
    static constexpr const char* leaseHeader = "KIRIN_HYPHA_LU_LEASE_V2";
    static constexpr juce::int64 refreshMs = 250, leaseMs = 1500;
    static bool fresh (juce::int64 at, juce::int64 now) { return at > 0 && now >= at && now - at < leaseMs; }
    static bool validScope (const juce::String& scope)
    { return scope.length() == 64 && scope.containsOnly ("0123456789abcdef"); }

    juce::File root;
    juce::String writtenScope, readScope, readOwner;
    juce::String owner = juce::Uuid().toString().removeCharacters ("-");
    std::uint64_t revision = 0;
    juce::int64 writtenAt = 0, cachedAt = 0, readRevision = 0;
    Facts cached;
    juce::int64 readAt = 0;
    bool haveRead = false;
};
}
