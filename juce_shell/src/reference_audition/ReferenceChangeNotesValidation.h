#pragma once
#include "ReferenceChangeNotesModel.h"
#include "ReferenceRuntimeRepositoryParsing.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

namespace hypha::reference_audition::change_notes_detail
{
using namespace runtime_repository_parsing;
inline constexpr std::int64_t maxInteger = 9'007'199'254'740'991;
inline constexpr std::int64_t maxBytes = 1'048'576;
inline bool schemaInteger (const juce::var& value, std::int64_t low, std::int64_t high, std::int64_t& output)
{
    if (value.isInt() || value.isInt64()) return exactInteger (value, low, high, output);
    if (! value.isDouble()) return false;
    const double number = static_cast<double> (value);
    if (! std::isfinite (number) || std::floor (number) != number
        || number < static_cast<double> (low) || number > static_cast<double> (high)) return false;
    output = static_cast<std::int64_t> (number); return true;
}
inline bool stringValue (const juce::var& value, juce::String& output)
{
    if (! value.isString()) return false;
    output = value.toString(); return true;
}
inline bool properties (const juce::var& value, std::initializer_list<const char*> keys)
{
    const auto* object = value.getDynamicObject();
    return object != nullptr && exactProperties (*object, keys);
}
inline bool receiptValid (const ChangeNotesReceipt& r)
{
    return sha256 (r.sha256) && r.bytes >= 1 && r.bytes <= maxBytes
        && r.relativePath == "plugin_data/reference/v2/notes/" + r.sha256 + ".json";
}
inline bool parseReceipt (const juce::var& value, ChangeNotesReceipt& output)
{
    ChangeNotesReceipt next;
    if (! properties (value, { "relative_path", "sha256", "bytes" })
        || ! stringValue (value["relative_path"], next.relativePath)
        || ! stringValue (value["sha256"], next.sha256)
        || ! schemaInteger (value["bytes"], 1, maxBytes, next.bytes) || ! receiptValid (next))
        return false;
    output = std::move (next); return true;
}
inline bool identityMatches (const juce::var& value, const RuntimeCandidate& candidate)
{
    if (! properties (value, { "work_id", "recording_id", "version_id", "sha256_file", "sha256_pcm" })) return false;
    juce::String work, recording, version, file, pcm;
    if (! stringValue (value["work_id"], work) || ! uuidV4 (work)
        || ! stringValue (value["recording_id"], recording) || ! uuidV4 (recording)
        || ! stringValue (value["version_id"], version) || ! uuidV4 (version)
        || ! stringValue (value["sha256_file"], file) || ! sha256 (file)
        || ! stringValue (value["sha256_pcm"], pcm) || ! sha256 (pcm)) return false;
    return work == candidate.sourceWorkId && recording == candidate.sourceRecordingId
        && version == candidate.sourceVersionId
        && work + ":" + recording + ":" + version + ":" + file + ":" + pcm == candidate.sourceIdentityKey;
}
inline bool originValid (const juce::String& text)
{
    if (text.isEmpty() || text.length() > 128) return false;
    for (auto c : text)
        if (! ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
               || c == '.' || c == '_' || c == ':' || c == '-')) return false;
    return true;
}
inline bool parseNote (const juce::var& value, const ChangeNotes& parent, ChangeNote& note)
{
    if (! properties (value, { "note_id", "origin_id", "number", "at_seconds", "at_sample", "text", "done" })
        || ! stringValue (value["note_id"], note.noteId) || note.noteId.length() < 3 || note.noteId.length() > 140
        || ! stringValue (value["origin_id"], note.originId) || ! originValid (note.originId)
        || ! schemaInteger (value["number"], 1, maxInteger, note.number)
        || note.noteId != note.originId + "#" + juce::String (note.number)
        || ! schemaInteger (value["at_sample"], 0, maxInteger, note.atSample)
        || ! stringValue (value["text"], note.text) || note.text.isEmpty() || note.text.getNumBytesAsUTF8() > 8192
        || ! value["done"].isBool()) return false;
    const auto& seconds = value["at_seconds"];
    if (! seconds.isDouble() && ! seconds.isInt() && ! seconds.isInt64()) return false;
    note.atSeconds = static_cast<double> (seconds); note.done = static_cast<bool> (value["done"]);
    if (! std::isfinite (note.atSeconds) || note.atSeconds < 0.0
        || note.atSeconds > static_cast<double> (parent.totalSampleFrames) / static_cast<double> (parent.sampleRateHz)) return false;
    // Positive Math.round semantics without x + 0.5 rounding upward early near 2^52.
    const double product = note.atSeconds * static_cast<double> (parent.sampleRateHz);
    const double integral = std::floor (product);
    const double rounded = integral + (product - integral >= 0.5 ? 1.0 : 0.0);
    const double clamped = std::min (static_cast<double> (parent.totalSampleFrames - 1), rounded);
    return std::isfinite (clamped) && clamped >= 0.0 && note.atSample == static_cast<std::int64_t> (clamped);
}
inline ChangeNotesReadResult validate (const juce::var& value, const RuntimeCandidate& candidate,
                                      const juce::String& artifactSha256)
{
    const auto fail = [] (const char* issue) { return ChangeNotesReadResult { {}, issue }; };
    if (! properties (value, { "format", "version", "time_base", "source_identity", "source_artifact_sha256",
                               "sample_rate_hz", "total_sample_frames", "open_count", "notes" })
        || ! value["format"].isString() || value["format"] != "kirin_hypha_reference_change_notes"
        || ! value["version"].isString() || value["version"] != "1.0"
        || ! value["time_base"].isString() || value["time_base"] != "source_seconds" || ! sha256 (artifactSha256))
        return fail ("reference_change_notes_schema");
    auto result = std::make_shared<ChangeNotes>(); result->artifactSha256 = artifactSha256;
    juce::String sourceHash;
    if (! schemaInteger (value["sample_rate_hz"], 8000, 768000, result->sampleRateHz)
        || ! schemaInteger (value["total_sample_frames"], 1, maxInteger, result->totalSampleFrames)
        || ! schemaInteger (value["open_count"], 1, 1000, result->openCount)
        || ! stringValue (value["source_artifact_sha256"], sourceHash) || ! sha256 (sourceHash))
        return fail ("reference_change_notes_schema");
    if (candidate.sourceKind != "work_version" || ! candidate.prepared || candidate.cues.size() != 1
        || candidate.cues.front().startSample != 0 || candidate.cues.front().loopEnabled
        || ! identityMatches (value["source_identity"], candidate) || sourceHash != candidate.sourceArtifact.sha256
        || result->sampleRateHz != candidate.cues.front().sampleRateHz
        || result->totalSampleFrames != candidate.cues.front().endSample)
        return fail ("reference_change_notes_identity");
    const auto* notes = value["notes"].getArray();
    if (notes == nullptr || notes->isEmpty() || notes->size() > 1000) return fail ("reference_change_notes_schema");
    std::set<juce::String> ids;
    std::tuple<std::int64_t, juce::String, std::int64_t> previous;
    std::int64_t open = 0;
    for (const auto& item : *notes)
    {
        ChangeNote note;
        if (! parseNote (item, *result, note) || ! ids.insert (note.noteId).second)
            return fail ("reference_change_notes_item");
        const auto key = std::make_tuple (note.atSample, note.originId, note.number);
        if (! result->notes.empty() && ! (previous < key)) return fail ("reference_change_notes_order");
        previous = key; if (! note.done) ++open;
        result->notes.push_back (std::move (note));
    }
    if (open != result->openCount) return fail ("reference_change_notes_count");
    return { std::move (result), {} };
}
}
