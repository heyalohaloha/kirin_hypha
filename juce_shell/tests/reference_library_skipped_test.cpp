// 2026-10-06：Kirin OS の項目を 1 つ受け付けられないときは、その項目だけを外し、ほかは使う（1 つの名前のせいで
// ライブラリ全体を捨て、新しく開いた Hypha は Kirin OS を待ったまま、開いていた Hypha は黙って古いライブラリを
// 使い続けていた）。外した項目は名前と、名前の字が原因か（直し方は Kirin OS で名前を直す）を残す。1 項目ずつの確かめは
// 今までと同じ厳しさ。試験は tests/fixtures の Kirin OS の書き出しの写し（試験用のフォルダの中）だけを書き換える。
#include "reference_runtime_test_support.h"
#include "KirinLibraryFixture.h"
#include "../src/reference_audition/ReferenceLibraryOpenOutcome.h"
#include "../src/reference_audition/ReferenceLibrarySets.h"
#include "../src/reference_audition/ReferenceRuntimeV2Repository.h"
#include "../src/reference_audition/ReferenceRuntimeEventTransport.h"
#include "../src/reference_audition/ReferenceTextEdges.h"

void testReferenceLibrarySkipped (const juce::File&);

namespace
{
juce::File libraryCopy (const juce::File& sandbox, const char* name)
{
    const auto root = kirin_library_fixture::copy (sandbox, name);
    require (root != juce::File(), "the Kirin OS library fixture is copied inside the test folder");
    return root;
}

// manifest の項目が指すファイルを書き換え、受け取り（sha256・bytes・relative_path）を合わせる（Kirin OS が書いた形のまま）。
void rewriteArtifact (const juce::File& root, juce::var& receipt, const char* folder, const juce::var& content)
{
    const auto json = ref::RuntimeEventTransport::canonicalJson (content);
    const auto hash = juce::SHA256 (json.toRawUTF8(), json.getNumBytesAsUTF8()).toHexString();
    const auto file = root.getChildFile ("library/" + juce::String (folder) + "/" + hash + ".json");
    require (file.isAChildOf (root) && file.replaceWithText (json), "the changed item is written inside the copy");
    auto* object = receipt.getDynamicObject();
    object->setProperty ("sha256", hash);
    object->setProperty ("bytes", static_cast<juce::int64> (json.getNumBytesAsUTF8()));
    object->setProperty ("relative_path", "plugin_data/reference/v2/library/" + juce::String (folder) + "/" + hash + ".json");
}

juce::var readJsonFile (const juce::File& file) { return juce::JSON::parse (file); }

// 2026-10-07: Hypha checks a Kirin OS name's ends against Kirin OS's own trim set, the same on every
// OS (JUCE's trim() takes U+2000B or U+20020 for a space on Windows). A Version name cut to 80 ends
// without a space. An open request's file gone before 15 s is an open; at or after it, Kirin OS may
// have removed an expired or late request, so it is not.
void testNameEdgesAndOpenOutcome()
{
    using hypha::reference_text::trimmed;
    using hypha::reference_text::trimEdges;
    const auto c = [] (juce::juce_wchar character) { return juce::String::charToString (character); };
    require (trimmed (c (0x2000b) + "Song") && trimmed ("Song" + c (0x20020)) && trimmed ("A") && trimmed ({}),
             "a character beyond the BMP is never an edge space");
    for (const auto space : { 0x09, 0x0d, 0x20, 0x85, 0xa0, 0x1680, 0x180e, 0x2000, 0x200a, 0x200b, 0x2028, 0x2029,
                              0x202f, 0x205f, 0x3000, 0xfeff })
        require (! trimmed (c (static_cast<juce::juce_wchar> (space)) + "A")
                     && ! trimmed ("A" + c (static_cast<juce::juce_wchar> (space))),
                 "every character Kirin OS trims is an edge space");
    require (trimEdges (c (0x3000) + " Name" + c (0x200b)) == "Name" && trimEdges ("  ").isEmpty(),
             "trimEdges removes them from both ends");
    require (hypha::reference_text::cut (juce::String::repeatedString ("A", 79) + " B", 80)
                 == juce::String::repeatedString ("A", 79),
             "a name cut to 80 does not end in a space");
    using ref::LibraryOpenOutcome;
    require (ref::libraryOpenOutcome (false, 1000) == LibraryOpenOutcome::pending
                 && ref::libraryOpenOutcome (true, 14999) == LibraryOpenOutcome::opened
                 && ref::libraryOpenOutcome (true, 15000) == LibraryOpenOutcome::timedOut
                 && ref::libraryOpenOutcome (false, 15000) == LibraryOpenOutcome::timedOut,
             "an open request is pending, opened or timed out by its own deadline");
}
}

void testReferenceLibrarySkipped (const juce::File& sandbox)
{
    testNameEdgesAndOpenOutcome();
    const auto root = libraryCopy (sandbox, "library-skipped");
    const auto manifestFile = root.getChildFile ("library/manifest.json");
    auto manifest = readJsonFile (manifestFile);
    // 保存した Preset（Saved mix）の Dynamics の曲の Cue の名前に、Hypha が受け付けない字（U+0085）。
    juce::var* savedReceipt = nullptr;
    juce::var saved;
    for (auto& receipt : *manifest["presets"].getArray())
    {
        const auto preset = readJsonFile (root.getChildFile ("library/presets/" + receipt["sha256"].toString() + ".json"));
        if (preset["name"] == "Saved mix") { savedReceipt = &receipt; saved = preset.clone(); }
    }
    require (savedReceipt != nullptr, "the fixture has the saved Preset");
    auto& checks = *saved["checks"].getArray();
    const auto dynamics = std::find_if (checks.begin(), checks.end(), [] (const juce::var& check) { return check["label"] == "Dynamics"; });
    require (dynamics != checks.end() && (*dynamics)["candidates"].size() == 1, "the saved Preset's Dynamics has one song");
    auto broken = (*dynamics)["candidates"][0].clone();
    broken["cues"][0].getDynamicObject()->setProperty ("label", "Chorus" + juce::String::charToString (0x85) + "2");
    auto kept = broken.clone();  // 同じ Check の、読める 2 曲目（別の候補・別の音源）
    kept.getDynamicObject()->setProperty ("candidate_id", "11111111-2222-4333-8444-555555555555");
    kept.getDynamicObject()->setProperty ("display_name", "Second track");
    kept["cues"][0].getDynamicObject()->setProperty ("label", "Chorus");
    auto identity = kept["source_identity"].clone();
    identity.getDynamicObject()->setProperty ("catalog_reference_id", "catalog:second");
    identity.getDynamicObject()->setProperty ("sha256_file", juce::String::repeatedString ("c", 64));
    identity.getDynamicObject()->setProperty ("sha256_pcm", juce::String::repeatedString ("d", 64));
    kept.getDynamicObject()->setProperty ("source_identity", identity);
    (*dynamics).getDynamicObject()->setProperty ("candidates", juce::Array<juce::var> { broken, kept });
    rewriteArtifact (root, *savedReceipt, "presets", saved);
    // Version は名前の食い違い（descriptor と候補の名前が違う）：名前の字ではなく、中身の不一致。
    auto& versionReceipt = manifest["versions"].getArray()->getReference (0);
    auto version = readJsonFile (root.getChildFile ("library/versions/" + versionReceipt["sha256"].toString() + ".json"));
    version.getDynamicObject()->setProperty ("display_name", "Another name");
    rewriteArtifact (root, versionReceipt, "versions", version);
    require (writeJson (manifestFile, manifest), "the copy's manifest points at the changed items");

    ref::RuntimeV2Repository repository (root);
    const auto loaded = repository.refreshLibrary();
    require (loaded.usable(), ("one unreadable item does not throw the library away: " + loaded.rejectionCode).toRawUTF8());
    const auto& workspace = *loaded.workspace;
    const auto preset = std::find_if (workspace.presets.begin(), workspace.presets.end(),
                                      [] (const auto& item) { return item.name == "Saved mix"; });
    require (preset != workspace.presets.end(), "the saved Preset is still there");
    const auto check = std::find_if (preset->checks.begin(), preset->checks.end(), [] (const auto& item) { return item.label == "Dynamics"; });
    require (check != preset->checks.end() && check->candidates.size() == 1 && check->candidates[0].displayName == "Second track",
             "only the song with the unreadable Cue name is left out");
    require (std::none_of (workspace.presets.begin(), workspace.presets.end(), [] (const auto& item) { return item.versionEntry; }),
             "the Version that does not match itself is left out");
    require (workspace.librarySkipped.size() == 2
                 && workspace.librarySkipped[0].name == juce::String ((*dynamics)["candidates"][0]["display_name"].toString())
                 && workspace.librarySkipped[0].nameUnreadable
                 && workspace.librarySkipped[1].name.isNotEmpty() && ! workspace.librarySkipped[1].nameUnreadable,
             "each item left out is named, with whether its name is the reason");

    // sets.json の曲の名前に受け付けない字（U+2028）：その曲だけを外し、画面に出せる形の名前で言う。
    auto sets = readJsonFile (root.getChildFile ("library/sets.json"));
    sets["song_sets"][0]["songs"][1].getDynamicObject()->setProperty ("display_name", "Line" + juce::String::charToString (0x2028) + "break");
    require (writeJson (root.getChildFile ("library/sets.json"), sets), "a changed sets file inside the copy");
    juce::String rejection;
    std::vector<ref::RuntimeSkippedItem> skipped;
    const auto read = ref::readReferenceLibrarySets (root, workspace, rejection, skipped);
    require (read.has_value() && rejection.isEmpty() && read->songSets.size() == 1 && read->songSets[0].songs.size() == 1
                 && skipped.size() == 1 && skipped[0].name == "Line?break" && skipped[0].nameUnreadable,
             "a set song with an unreadable name is left out by itself and named");
    const auto refreshed = repository.refreshLibrary (loaded.workspace);
    require (refreshed.usable() && refreshed.workspace->setsSkipped.size() == 1 && refreshed.workspace->librarySkipped.size() == 2,
             "the workspace keeps what it left out from the manifest and from the sets");

    // A name that starts with the kanji U+2000B and ends with U+20020 is a clean Kirin OS name on
    // every OS: it is read whole, not left out. Written as Kirin OS writes, in raw UTF-8 (JUCE's own
    // JSON writer would split each into two \u escapes, which its reader does not join again).
    const auto kanji = juce::String::charToString (0x2000b) + "Song" + juce::String::charToString (0x20020);
    sets["song_sets"][0]["songs"][1].getDynamicObject()->setProperty ("display_name", kanji);
    require (root.getChildFile ("library/sets.json").replaceWithText (ref::RuntimeEventTransport::canonicalJson (sets)),
             "a renamed set song inside the copy, in raw UTF-8");
    rejection.clear();
    skipped.clear();
    const auto kanjiRead = ref::readReferenceLibrarySets (root, workspace, rejection, skipped);
    require (kanjiRead.has_value() && rejection.isEmpty() && skipped.empty() && kanjiRead->songSets[0].songs.size() == 2
                 && kanjiRead->songSets[0].songs[1].displayName == kanji,
             "a name with a character beyond the BMP at either end is read whole");
}
