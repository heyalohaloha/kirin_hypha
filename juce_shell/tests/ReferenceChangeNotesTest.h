#pragma once
#include "../src/reference_audition/ReferenceChangeNotesReader.h"
#include <limits>

namespace hypha::tests::change_notes
{
using namespace reference_audition;
// Only caller-owned synthetic fixture files; no JUCE sibling staging paths.
inline bool writeOwnedFixture(const juce::File& file, const juce::String& text)
{
    juce::FileOutputStream out(file);
    if (!out.openedOk() || !out.setPosition(0) || out.truncate().failed()) return false;
    const bool written=out.write(text.toRawUTF8(), static_cast<size_t>(text.getNumBytesAsUTF8()));
    out.flush(); return written && out.getStatus().wasOk();
}
inline juce::var fixture()
{
    return juce::JSON::parse (R"json({
      "format":"kirin_hypha_reference_change_notes","version":"1.0","time_base":"source_seconds",
      "source_identity":{"work_id":"11111111-1111-4111-8111-111111111111",
        "recording_id":"22222222-2222-4222-8222-222222222222","version_id":"33333333-3333-4333-8333-333333333333",
        "sha256_file":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "sha256_pcm":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},
      "source_artifact_sha256":"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
      "sample_rate_hz":48000,"total_sample_frames":96000,"open_count":1,
      "notes":[{"note_id":"reply#1","origin_id":"reply","number":1,"at_seconds":2,
        "at_sample":95999,"text":"Check the ending.\nKeep this plain.","done":false}]
    })json");
}
inline RuntimeCandidate candidate()
{
    const auto v = fixture(); const auto id = v["source_identity"];
    RuntimeCandidate c; c.sourceKind = "work_version";
    c.sourceWorkId = id["work_id"].toString(); c.sourceRecordingId = id["recording_id"].toString();
    c.sourceVersionId = id["version_id"].toString();
    c.sourceIdentityKey = c.sourceWorkId + ":" + c.sourceRecordingId + ":" + c.sourceVersionId
        + ":" + id["sha256_file"].toString() + ":" + id["sha256_pcm"].toString();
    c.sourceArtifact.sha256 = v["source_artifact_sha256"].toString();
    c.cues.push_back ({ "cue", "Whole", 48000, 0, 96000, false }); return c;
}
template <typename Require>
int verify (const juce::File& isolatedRoot, Require require)
{
    namespace detail = change_notes_detail;
    int cases = 0;
    const auto check = [&] (bool pass, const char* message) { ++cases; require (pass, message); };
    const auto c = candidate(); const auto hash = juce::String::repeatedString ("d", 64);
    const auto parse = [&] (const juce::var& v) { return detail::validate (v, c, hash); };
    const auto original = fixture(); const auto good = parse (original);
    check (good.notes && good.issue.isEmpty() && good.notes->notes[0].atSample == 95999, "notes endpoint accepted");
    check (good.notes && good.notes->notes[0].text.containsChar ('\n'), "notes plain newline retained");
    const auto reject = [&] (juce::var v) { const auto result = parse (v); check (! result.notes && result.issue.isNotEmpty(), "bad notes rejected independently"); };
    for (const char* field : { "format", "version", "time_base", "source_identity", "source_artifact_sha256",
                              "sample_rate_hz", "total_sample_frames", "open_count", "notes" })
    { auto v = original.clone(); v.getDynamicObject()->removeProperty (field); reject (v); }
    { auto v = original.clone(); v.getDynamicObject()->setProperty ("extra", true); reject (v); }
    for (const char* field : { "work_id", "recording_id", "version_id", "sha256_file", "sha256_pcm" })
    { auto v = original.clone(); v["source_identity"].getDynamicObject()->setProperty (field, "different"); reject (v); }
    for (const char* field : { "sample_rate_hz", "total_sample_frames", "open_count" })
    { auto v = original.clone(); v.getDynamicObject()->setProperty (field, true); reject (v); }
    for (const auto value : { -1.0, 2.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
    { auto v = original.clone(); v["notes"][0].getDynamicObject()->setProperty ("at_seconds", value); reject (v); }
    for (const char* field : { "note_id", "origin_id", "text" })
    { auto v = original.clone(); v["notes"][0].getDynamicObject()->setProperty (field, ""); reject (v); }
    { auto v = original.clone(); v["notes"][0].getDynamicObject()->setProperty ("done", 1); reject (v); }
    { auto v = original.clone(); v["notes"].getArray()->add (v["notes"][0].clone()); reject (v); }
    { auto v = original.clone(); v["notes"][0].getDynamicObject()->setProperty ("text", juce::String::repeatedString ("a", 8193)); reject (v); }
    { auto v = original.clone(); v.getDynamicObject()->setProperty ("sample_rate_hz", 48000.0);
      check (parse (v).notes != nullptr, "schema integer double accepted"); }
    { auto v = original.clone(); v["notes"][0].getDynamicObject()->setProperty ("at_seconds", 0.5 / 48000.0);
      v["notes"][0].getDynamicObject()->setProperty ("at_sample", 1); check (parse (v).notes != nullptr, "positive half round"); }
    { auto v = original.clone(); auto second = v["notes"][0].clone();
      second.getDynamicObject()->setProperty ("number", 2); second.getDynamicObject()->setProperty ("note_id", "reply#2");
      second.getDynamicObject()->setProperty ("at_seconds", 0); second.getDynamicObject()->setProperty ("at_sample", 0);
      v["notes"].getArray()->add (second); v.getDynamicObject()->setProperty ("open_count", 2); reject (v); }
    { auto v = original.clone(); v["notes"][0].getDynamicObject()->setProperty ("done", true); reject (v); }
    { auto v = original.clone(); v["source_identity"].getDynamicObject()->setProperty ("unknown", 1); reject (v); }
    { auto v = original.clone(); v["notes"][0].getDynamicObject()->setProperty ("unknown", 1); reject (v); }
    { auto other = c; other.sourceArtifact.sha256 = hash;
      check (! detail::validate (original, other, hash).notes, "source receipt identity mismatch"); }
    for (int variant = 0; variant < 2; ++variant)
    { auto other = c; if (variant == 0) ++other.cues[0].sampleRateHz; else ++other.cues[0].endSample;
      check (! detail::validate (original, other, hash).notes, "cue format identity mismatch"); }
    check (isolatedRoot.getChildFile ("notes").createDirectory().wasOk(), "isolated notes directory");
    const auto publish = [&] (const juce::String& text) {
        ChangeNotesReceipt r; r.bytes = static_cast<std::int64_t> (text.getNumBytesAsUTF8());
        r.sha256 = juce::SHA256 (text.toRawUTF8(), static_cast<size_t> (r.bytes)).toHexString();
        r.relativePath = "plugin_data/reference/v2/notes/" + r.sha256 + ".json";
        require (writeOwnedFixture(isolatedRoot.getChildFile ("notes/" + r.sha256 + ".json"), text), "write immutable synthetic notes");
        return r;
    };
    const auto receipt = publish (juce::JSON::toString (original));
    check (readChangeNotes (isolatedRoot, receipt, c).notes != nullptr, "real receipt bytes hash read");
    { auto v = original.clone();
      v["notes"][0].getDynamicObject()->setProperty ("at_sample", 95998);
      const auto r = publish (juce::JSON::toString (v));
      const auto result = readChangeNotes (isolatedRoot, r, c);
      check (! result.notes && result.issue == "reference_change_notes_item", "in-range wrong sample rejected by rounding contract"); }
    { auto v = original.clone(); auto* items = v["notes"].getArray(); items->clear();
      // Valid sorted unique entries: one open plus 1000 done keeps open_count legal,
      // so rejection exercises the notes array limit rather than its count field.
      for (int i = 1; i <= 1001; ++i)
      { auto note = original["notes"][0].clone();
        note.getDynamicObject()->setProperty ("number", i);
        note.getDynamicObject()->setProperty ("note_id", "reply#" + juce::String (i));
        note.getDynamicObject()->setProperty ("done", i != 1); items->add (note); }
      const auto r = publish (juce::JSON::toString (v));
      require (r.bytes <= detail::maxBytes, "array limit fixture stays under byte cap");
      const auto result = readChangeNotes (isolatedRoot, r, c);
      check (! result.notes && result.issue == "reference_change_notes_schema", "1001 notes rejected independently of open count"); }
    { const auto text = juce::JSON::toString (original);
      const auto padded = text + juce::String::repeatedString (" ",
          static_cast<int> (detail::maxBytes + 1 - static_cast<std::int64_t> (text.getNumBytesAsUTF8())));
      const auto r = publish (padded);
      require (r.bytes == detail::maxBytes + 1, "oversize fixture is exactly one byte over cap");
      const auto result = readChangeNotes (isolatedRoot, r, c);
      check (! result.notes && result.issue == "reference_change_notes_receipt", "over one MiB rejected with matching receipt hash and bytes"); }
    { const auto text = juce::JSON::toString (original);
      const auto changed = text.replace ("Check the ending.", "Check the Ending.");
      require (changed != text && changed.getNumBytesAsUTF8() == text.getNumBytesAsUTF8(),
               "hash mismatch fixture preserves exact byte count");
      const auto file = isolatedRoot.getChildFile ("notes/" + receipt.sha256 + ".json");
      require (writeOwnedFixture (file, changed), "write same-length corrupt owned fixture");
      const auto result = readChangeNotes (isolatedRoot, receipt, c);
      check (! result.notes && result.issue == "reference_change_notes_hash", "same-length corruption reaches SHA rejection");
      require (writeOwnedFixture (file, text), "restore owned fixture after hash-only rejection"); }
    auto bad = receipt; ++bad.bytes;
    check (! readChangeNotes (isolatedRoot, bad, c).notes, "byte mismatch rejected");
    bad = receipt; bad.relativePath = "../notes.json";
    check (! readChangeNotes (isolatedRoot, bad, c).notes, "path escape rejected");
    { const auto r = publish ("{\"a\":1,\"\\u0061\":2}");
      check (! readChangeNotes (isolatedRoot, r, c).notes, "escaped duplicate object key rejected"); }
    { const auto r = publish ("[[[[[[0]]]]]]");
      check (! readChangeNotes (isolatedRoot, r, c).notes, "depth rejected before recursive parser"); }
    { const auto file = isolatedRoot.getChildFile ("notes/" + receipt.sha256 + ".json");
      require (writeOwnedFixture(file, "{}"), "corrupt isolated notes");
      check (! readChangeNotes (isolatedRoot, receipt, c).notes, "changed bytes rejected"); }
#if ! JUCE_WINDOWS
    { const auto outside = isolatedRoot.getChildFile ("outside.json");
      const auto text = juce::JSON::toString (original);
      require (writeOwnedFixture(outside, text), "outside synthetic fixture");
      const auto leaf = isolatedRoot.getChildFile ("notes/" + receipt.sha256 + ".json");
      require (leaf.deleteFile(), "remove owned corrupt fixture");
      require (::symlink (outside.getFullPathName().toRawUTF8(), leaf.getFullPathName().toRawUTF8()) == 0, "create leaf link fixture");
      check (! readChangeNotes (isolatedRoot, receipt, c).notes, "leaf no follow");
      require (::unlink (leaf.getFullPathName().toRawUTF8()) == 0, "remove leaf link");
      const auto notes = isolatedRoot.getChildFile ("notes"), held = isolatedRoot.getChildFile ("held-notes");
      require (notes.moveFileTo (held), "move owned notes fixture");
      require (::symlink (held.getFullPathName().toRawUTF8(), notes.getFullPathName().toRawUTF8()) == 0, "create directory link fixture");
      check (! readChangeNotes (isolatedRoot, receipt, c).notes, "directory no follow");
      require (::unlink (notes.getFullPathName().toRawUTF8()) == 0 && held.moveFileTo (notes), "restore directory fixture"); }
#endif
    return cases;
}
}
