#pragma once
#include "HyphaMetricNeedle.h"
#include "HyphaUiPreferences.h"
#include "HyphaVuCalibrationFile.h"
#include <array>
#include <iomanip>
#include <locale>
#include <sstream>

namespace hypha::metric_needle
{
// Reference-only facts. Never put PRE/POST live measurements or a moving REF
// needle here. The rate/channel pair identifies the native gain-one definition.
struct SharedReferenceZero
{
    Face face = Face::psr;
    Status status = Status::unavailable;
    std::uint32_t rate = 0, channels = 0;
    std::uint64_t generation = 0;
    double median = missing;
    Band band;
    juce::String song;
    std::string reason; // Fixed public code only; never a source path or free-form error.
};

// Message-thread only; one instance per editor/processor. A stopped publisher
// loses its authority even if its last file remains. No Audio Thread access.
class SharedMetricZero final
{
public:
    static constexpr juce::int64 leaseMs = 1500, refreshMs = 250;
    explicit SharedMetricZero (juce::File directory = ui_preferences::defaultFile().getSiblingFile ("metric-reference"))
        : root (std::move (directory)), owner (juce::Uuid().toString().removeCharacters ("-")) {}

    bool publish (const juce::String& scope, const SharedReferenceZero& zero,
                  juce::int64 now = juce::Time::currentTimeMillis())
    {
        if (!validScope (scope) || !metric (zero.face)) return false;
        auto& slot = writes[index (zero.face)];
        // Scope or state changes wait at most one refresh interval. Old files
        // cannot be refreshed by calls suppressed by this rate limit.
        if (slot.at != 0 && now >= slot.at && now - slot.at < refreshMs) return false;
        const auto text = encoded (scope, owner, zero, now);
        if (text.isEmpty() || root.createDirectory().failed()
            || !vu_calibration::writeAtomicText (file (scope, zero.face), text)) return false;
        slot.at = now;
        return true;
    }

    SharedReferenceZero read (const juce::String& scope, Face face, std::uint32_t rate,
                              std::uint32_t channels, bool referenceReadable,
                              juce::int64 now = juce::Time::currentTimeMillis())
    {
        SharedReferenceZero absent; absent.face = face;
        if (!metric (face)) return absent;
        auto& slot = reads[index (face)];
        if (!referenceReadable || !validScope (scope) || !validFormat (rate, channels))
        { slot = {}; return absent; }
        if (slot.scope != scope || slot.rate != rate || slot.channels != channels)
        { slot = {}; slot.scope = scope; slot.rate = rate; slot.channels = channels; }
        if (slot.readAt != 0 && now >= slot.readAt && now - slot.readAt < refreshMs)
            return fresh (slot.writtenAt, now) ? slot.zero : absent;
        slot.readAt = now;
        const auto target = file (scope, face);
        const auto size = target.getSize();
        if (!target.existsAsFile() || size <= 0 || size > 256) { slot.zero = absent; return absent; }
        auto record = decode (scope, face, rate, channels, target.loadFileAsString(), now);
        if (!record.valid) { slot.zero = absent; return absent; }
        // Retain high-water evidence across damaged or stale reads. A delayed
        // write from the same publisher must not resurrect an older selection.
        if (slot.owner == record.owner && (record.zero.generation < slot.generation
            || record.writtenAt < slot.writtenAt)) { slot.zero = absent; return absent; }
        // New publisher lifetimes are allowed, but an older timestamp is not.
        if (slot.writtenAt != 0 && record.writtenAt < slot.writtenAt)
        { slot.zero = absent; return absent; }
        slot.owner = record.owner; slot.generation = record.zero.generation;
        slot.writtenAt = record.writtenAt; slot.zero = std::move (record.zero);
        return slot.zero;
    }

    static juce::String encoded (const juce::String& scope, const juce::String& publisher,
                                 const SharedReferenceZero& zero, juce::int64 now)
    {
        if (!validScope (scope) || !validOwner (publisher) || !metric (zero.face)
            || !validFormat (zero.rate, zero.channels) || zero.generation == 0 || now <= 0
            || static_cast<unsigned> (zero.status) > static_cast<unsigned> (Status::unavailable)) return {};
        const bool ready = zero.status == Status::ready;
        if (!validReason (zero.reason) || (ready && (!std::isfinite (zero.median) || !validBand (zero.band)))) return {};
        std::ostringstream fields; fields.imbue (std::locale::classic());
        fields << publisher.toStdString() << ' ' << zero.generation << ' ' << now << ' '
               << static_cast<unsigned> (zero.face) << ' ' << zero.rate << ' ' << zero.channels << ' '
               << static_cast<unsigned> (zero.status) << ' ' << number (ready ? zero.median : missing) << ' '
               << number (ready ? zero.band.low : missing) << ' ' << number (ready ? zero.band.high : missing);
        if (!zero.reason.empty()) fields << ' ' << zero.reason; // Optional field: old ten-field records remain valid.
        const auto prefix = juce::String ("KHMZ1\n") + scope + "\n" + juce::String (fields.str()) + "\n";
        if (prefix.getNumBytesAsUTF8() + 1 > 256) return {};
        auto name = zero.song.replaceCharacters ("\r\n", "  ");
        while (prefix.getNumBytesAsUTF8() + name.getNumBytesAsUTF8() + 1 > 256)
            name = name.dropLastCharacters (1);
        return prefix + name + "\n";
    }

    static SharedReferenceZero decoded (const juce::String& scope, Face face,
        std::uint32_t rate, std::uint32_t channels, const juce::String& text, juce::int64 now)
    { return decode (scope, face, rate, channels, text, now).zero; }

private:
    struct Write { juce::int64 at = 0; };
    struct Read
    {
        juce::String scope, owner;
        std::uint32_t rate = 0, channels = 0;
        std::uint64_t generation = 0;
        juce::int64 readAt = 0, writtenAt = 0;
        SharedReferenceZero zero;
    };
    struct Record
    {
        SharedReferenceZero zero;
        juce::String owner;
        juce::int64 writtenAt = 0;
        bool valid = false;
    };
    static std::size_t index (Face face) { return static_cast<std::size_t> (face) - 2; }
    static bool validScope (const juce::String& value)
    { return value.length() == 64 && value.containsOnly ("0123456789abcdef"); }
    static bool validOwner (const juce::String& value)
    { return value.length() == 32 && value.containsOnly ("0123456789abcdef"); }
    static bool validReason (const std::string& value)
    {
        return value.empty() || value == "select_reference_post" || value == "preparing" || value == "unsupported_format"
            || value == "read_failed" || value == "source_changed" || value == "private_copy_required"
            || value == "capacity_exceeded" || value == "audio_too_short" || value == "cue_too_short"
            || value == "no_valid_value" || value == "analysis_failed";
    }
    static bool validFormat (std::uint32_t rate, std::uint32_t channels)
    { return rate >= 8000 && rate <= 384000 && rate % 10 == 0 && (channels == 1 || channels == 2); }
    static bool fresh (juce::int64 written, juce::int64 now)
    { return written > 0 && now >= written && now - written < leaseMs; }
    static bool validBand (Band band)
    { return (std::isnan (band.low) && std::isnan (band.high))
        || (std::isfinite (band.low) && std::isfinite (band.high) && band.low <= band.high); }
    static std::string number (double value)
    {
        if (!std::isfinite (value)) return "-";
        std::ostringstream stream; stream.imbue (std::locale::classic());
        stream << std::setprecision (17) << value; return stream.str();
    }
    static bool unsignedNumber (const juce::String& text, std::uint64_t& value)
    {
        if (text.isEmpty() || !text.containsOnly ("0123456789")) return false;
        value = 0;
        for (const auto digit : text.toStdString()) {
            const auto n = static_cast<unsigned> (digit - '0');
            if (value > (std::numeric_limits<std::uint64_t>::max() - n) / 10) return false;
            value = value * 10 + n;
        }
        return true;
    }
    static bool parseNumber (const juce::String& text, double& value)
    {
        if (text == "-") { value = missing; return true; }
        std::istringstream stream (text.toStdString()); stream.imbue (std::locale::classic());
        stream >> std::noskipws >> value;
        return !stream.fail() && stream.eof() && std::isfinite (value);
    }
    static Record decode (const juce::String& scope, Face face, std::uint32_t rate,
        std::uint32_t channels, const juce::String& text, juce::int64 now)
    {
        Record record; record.zero.face = face;
        const auto reject = record;
        if (!validScope (scope) || !metric (face) || !validFormat (rate, channels)
            || text.getNumBytesAsUTF8() > 256) return reject;
        const auto lines = juce::StringArray::fromLines (text);
        if (lines.size() < 4 || lines.size() > 5 || (lines.size() == 5 && lines[4].isNotEmpty())
            || lines[0] != "KHMZ1" || lines[1] != scope) return reject;
        const auto fields = juce::StringArray::fromTokens (lines[2], " ", {});
        if ((fields.size() != 10 && fields.size() != 11) || !validOwner (fields[0])) return reject;
        if (fields.size() == 11) {
            record.zero.reason = fields[10].toStdString();
            if (record.zero.reason.empty() || !validReason (record.zero.reason)) return reject;
        }
        std::array<std::uint64_t, 6> ints {};
        for (std::size_t i = 0; i < ints.size(); ++i)
            if (!unsignedNumber (fields[static_cast<int> (i) + 1], ints[i])) return reject;
        if (ints[0] == 0 || ints[1] > static_cast<std::uint64_t> (std::numeric_limits<juce::int64>::max())
            || !fresh (static_cast<juce::int64> (ints[1]), now)
            || ints[2] != static_cast<unsigned> (face) || ints[3] != rate || ints[4] != channels
            || ints[5] > static_cast<unsigned> (Status::unavailable)) return reject;
        auto& zero = record.zero;
        zero.status = static_cast<Status> (ints[5]); zero.generation = ints[0]; zero.rate = rate; zero.channels = channels;
        if (!parseNumber (fields[7], zero.median) || !parseNumber (fields[8], zero.band.low)
            || !parseNumber (fields[9], zero.band.high) || !validBand (zero.band)) return reject;
        if (zero.status == Status::ready && !std::isfinite (zero.median)) return reject;
        if (zero.status != Status::ready && (!std::isnan (zero.median)
            || !std::isnan (zero.band.low) || !std::isnan (zero.band.high))) return reject;
        zero.song = lines[3]; record.owner = fields[0]; record.writtenAt = static_cast<juce::int64> (ints[1]);
        record.valid = true; return record;
    }
    juce::File file (const juce::String& scope, Face face) const
    { return root.getChildFile (scope + "-" + juce::String (static_cast<int> (face)) + ".txt"); }
    juce::File root;
    juce::String owner;
    std::array<Write, 3> writes;
    std::array<Read, 3> reads;
};
}
