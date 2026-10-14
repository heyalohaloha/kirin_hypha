#include "../src/AtomicFile.h"
#include "TestTemporaryDirectory.h"
#include "TestPathBudget.h"
#include "PresetAdoptionPublicationTest.h"
#include <iostream>
#include <thread>
#include <vector>

namespace
{
void require (bool ok, const char* message)
{
    if (! ok) { std::cerr << "Atomic file: " << message << '\n'; std::exit (1); }
}

struct FailedStream
{
    juce::FileOutputStream stream;
    bool partial;
    FailedStream (const juce::File& file, bool shortWrite) : stream (file), partial (shortWrite) {}
    bool openedOk() const { return stream.openedOk(); }
    bool writeText (const juce::String& text, bool unicode, bool header, const char* endings)
    {
        const auto ok = stream.writeText (partial ? text.substring (0, 1) : text, unicode, header, endings);
        return ok && ! partial;
    }
    void flush() { stream.flush(); }
    juce::Result getStatus() const { return juce::Result::fail ("injected flush failure"); }
};
}

int main (int argc, char** argv)
{
    const auto root = hypha::tests::temporaryParent().getNonexistentChildFile ("atom", {}, false);
    require (root.createDirectory().wasOk(), "create fixture");
    const auto name = juce::String::repeatedString ("a", 64) + ".json";
    const bool boundaryIo = argc == 2 && juce::String (argv[1]) == "--boundary-io";
    // Pure path construction exercises the legacy failure boundary on every platform without
    // adding intentionally over-budget files to the native storage fixture.
    for (int length : { 246, 247, 249, 259 })
    {
        const auto padding = length - root.getFullPathName().length() - name.length() - 2;
        require (padding > 0, "build root leaves room for boundary model");
        const auto target = root.getChildFile (juce::String::repeatedString ("p", padding)).getChildFile (name);
        const auto temporary = hypha::storage::temporarySibling (target);
        require (target.getFullPathName().length() == length, "exact path boundary");
        require (temporary.getParentDirectory() == target.getParentDirectory()
            && temporary != target && temporary.getFileName().length() == 14
            && temporary.getFullPathName().length() <= 259, "bounded same-directory staging");
        if (length > 246) require (length + 13 > 259, "legacy suffix would exceed limit");
        if (boundaryIo)
        {
            require (target.getParentDirectory().createDirectory().wasOk(), "create boundary parent");
            require (hypha::storage::writeText (target, "first") && hypha::storage::writeText (target, "second")
                && target.loadFileAsString() == "second", "real boundary write and replacement");
            require (target.getParentDirectory().findChildFiles (juce::File::findFiles, false).size() == 1,
                     "boundary publication leaves no staging file");
        }
    }
    if (boundaryIo)
    {
        namespace ref = hypha::reference_audition;
        const juce::String preset = "11111111-1111-4111-8111-111111111111";
        const auto receipt = [&] (const juce::String& revision)
        {
            ref::RuntimeSourcePresetReceipt value;
            value.presetId = preset; value.revisionId = revision;
            value.relativePath = "reference/presets/" + preset + "/" + revision + ".v1.json";
            value.sha256 = juce::String::repeatedString ("a", 64); value.bytes = 10;
            return value;
        };
        hypha::tests::verifyPresetAdoptionPublication (root, { "post", preset, 42 },
            receipt ("22222222-2222-4222-8222-222222222222"),
            receipt ("33333333-3333-4333-8333-333333333333"), 243);
        require (hypha::tests::removeFixtureDirectory (root), "boundary cleanup");
        return 0;
    }
    const auto file = root.getChildFile (name);
    require (hypha::storage::writeText (file, "{\"revision\":1}\n", false, false, "\n"), "first publication");
    require (hypha::storage::writeText (file, "{\"revision\":2}\n", false, false, "\n")
        && file.loadFileAsString() == "{\"revision\":2}\n", "exact replacement bytes");
    require (! hypha::storage::writeText (file.getChildFile ("blocked.json"), "invalid")
        && file.loadFileAsString() == "{\"revision\":2}\n", "blocked parent preserves target");
    const auto directory = root.getChildFile ("directory.json");
    require (directory.createDirectory().wasOk(), "blocked destination");
    require (! hypha::storage::writeText (directory, "invalid") && directory.isDirectory(), "refuse directory target");
    for (bool partial : { false, true })
        require (! hypha::storage::writeTextWithStream (file, "replacement", false, false, "\n",
            [partial] (const juce::File& stage) { return std::make_unique<FailedStream> (stage, partial); })
            && file.loadFileAsString() == "{\"revision\":2}\n", "partial write and failed flush preserve old bytes");
    require (! hypha::storage::writeTextWithStream (file, "replacement", false, false, "\n",
        [] (const juce::File&) { return std::unique_ptr<juce::FileOutputStream> {}; })
        && file.loadFileAsString() == "{\"revision\":2}\n", "open failure preserves old bytes");
    const auto renameTarget = root.getChildFile ("rename.json");
    require (! hypha::storage::writeTextWithStream (renameTarget, "replacement", false, false, "\n",
        [&] (const juce::File& stage)
        {
            require (renameTarget.createDirectory().wasOk()
                && renameTarget.getChildFile ("keep").appendText ("preserve"), "block replacement after staging starts");
            return std::make_unique<juce::FileOutputStream> (stage);
        }) && renameTarget.getChildFile ("keep").loadFileAsString() == "preserve", "failed rename keeps destination");
    const auto text = root.getChildFile ("text");
    require (hypha::storage::writeText (text, "a\nb\n") && text.loadFileAsString() == "a\r\nb\r\n",
             "JUCE default line endings are preserved");
    require (hypha::storage::writeText (text, "") && text.getSize() == 0, "empty replacement");
    std::vector<std::thread> writers;
    for (int i = 0; i < 8; ++i) writers.emplace_back ([&, i]
    {
        require (hypha::storage::writeText (file, juce::String::repeatedString (juce::String (i), 4096)),
                 "concurrent complete publication");
    });
    for (auto& writer : writers) writer.join();
    const auto published = file.loadFileAsString();
    require (published.length() == 4096 && published == juce::String::repeatedString (published.substring (0, 1), 4096),
             "concurrent publication has no mixed bytes");
    require (root.findChildFiles (juce::File::findFiles, false).size() == 2, "no staging leftovers");
    require (hypha::tests::removeFixtureDirectory (root), "fixture cleanup");
    std::cout << "Atomic file: PASS (boundaries, bytes, failures, concurrent writers, cleanup)\n";
}
