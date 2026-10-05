#include "../src/update/UpdateChecker.h"
#include "../src/update/UpdateServiceOwner.h"
#include "../src/reference_audition/ReferenceRuntimeV2Repository.h"
#include "ValidationStorageSandbox.h"
#include "UpdateRecordFixture.h"
#include <atomic>
#include <functional>
#include <filesystem>
#include <iostream>

namespace
{
using namespace update_integration;
namespace update = hypha::update;
namespace reference = hypha::reference_audition;

void require (bool ok, const char* message)
{
    if (! ok) { std::cerr << "Update/runtime integration FAIL: " << message << '\n'; std::exit (1); }
}

void waitFor (const std::function<bool()>& predicate, const char* message)
{
    for (int i = 0; i < 2000; ++i)
    {
        if (predicate()) return;
        juce::Thread::sleep (5);
    }
    require (false, message);
}

juce::File environmentRoot (const char* name)
{
    const auto* value = std::getenv (name);
    require (value != nullptr && juce::String (value).contains ("kirin-hypha-audio-transparency-"),
             "existing validation sandbox redirects every FFI storage path before engine creation");
    return juce::File (value);
}

void writeLicense (const juce::File& identity, const char* license)
{
    require (atomicJson (identity, object ({
        { "schema_version", "1.0" }, { "installation_id", "update-integration-fixture" },
        { "hardware_id", "fixture" },
        { "hardware_components", object ({ { "iop", "fixture" }, { "sn", "fixture" }, { "bd", "fixture" } }) },
        { "machine_signature", "fixture" }, { "license", license },
        { "created_at", "2026-10-05T00:00:00Z" }, { "last_verified_at", "2026-10-05T00:00:00Z" } })),
        "publish disposable Kirin OS entitlement fixture");
}

juce::File publishLibrary (const juce::File& root)
{
    const juce::String presetId = "88888888-8888-4888-8888-888888888888";
    const juce::String revisionId = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    const auto templateReceipt = object ({
        { "preset_id", presetId }, { "revision_id", revisionId },
        { "relative_path", "reference/presets/" + presetId + "/" + revisionId + ".v1.json" },
        { "sha256", juce::String::repeatedString ("b", 64) }, { "bytes", 1024 } });
    // Empty-candidate balance Checks are an existing OS library contract, not a local Factory.
    const auto check = object ({
        { "check_id", "55555555-5555-4555-8555-555555555555" }, { "label", "Fixture balance" },
        { "mode", "audition_with_facts" }, { "view_bindings", juce::Array<juce::var> { "balance" } },
        { "comparison_mode", "original" }, { "candidates", juce::Array<juce::var> {} },
        { "profile_bindings", juce::Array<juce::var> {} } });
    const auto preset = object ({
        { "format", "kirin_hypha_reference_library_preset" }, { "version", "1.0" },
        { "source_template_artifact", templateReceipt }, { "name", "Fixture Reference" },
        { "checks", juce::Array<juce::var> { check } } });
    const auto bytes = juce::JSON::toString (preset, true) + "\n";
    const auto hash = juce::SHA256 (bytes.toRawUTF8(), bytes.getNumBytesAsUTF8()).toHexString();
    const auto file = root.getChildFile ("library/presets/" + hash + ".json");
    require (file.getParentDirectory().createDirectory() && file.replaceWithText (bytes, false, false, "\n"),
             "publish content-addressed Reference preset fixture");
    const auto receipt = object ({
        { "preset_id", presetId }, { "revision_id", revisionId },
        { "relative_path", "plugin_data/reference/v2/library/presets/" + hash + ".json" },
        { "sha256", hash }, { "bytes", juce::int64 (bytes.getNumBytesAsUTF8()) } });
    require (atomicJson (root.getChildFile ("library/manifest.json"), object ({
        { "format", "kirin_hypha_reference_library" }, { "version", "1.0" }, { "revision", 1 },
        { "default_preset_id", presetId }, { "presets", juce::Array<juce::var> { receipt } } })),
        "publish real Reference library manifest contract");
    const auto now = juce::Time::currentTimeMillis();
    require (atomicJson (root.getChildFile ("library/presence.json"), object ({
        { "format", "kirin_hypha_reference_library_presence" }, { "version", "1.0" },
        { "session_id", "77777777-7777-4777-8777-777777777777" },
        { "updated_at_ms", now }, { "expires_at_ms", now + 5000 } })), "publish Reference presence");
    return file;
}

void receiveReference (const juce::File& root)
{
    reference::RuntimeV2Repository receiver (root);
    require (! receiver.refreshLibrary().usable(), "missing Reference library stays absent");
    const auto presetFile = publishLibrary (root);
    const auto received = receiver.refreshLibrary();
    if (! received.usable()) std::cerr << "Reference rejection: " << received.rejectionCode << '\n';
    require (received.usable() && received.workspace->presets.size() == 1
        && received.workspace->presets[0].checks.size() == 1
        && received.workspace->presets[0].checks[0].candidates.empty(),
        "real Reference receiver accepts exact one-preset/one-check OS contract during updater activity");
    require (receiver.refreshLibrary (received.workspace).state == reference::RuntimeWorkspaceLoadState::unchanged,
             "repeated Reference refresh retains immutable publication");
    const auto presence = juce::JSON::parse (root.getChildFile ("library/presence.json"));
    require (receiver.libraryOnline (static_cast<juce::int64> (presence["updated_at_ms"]))
        && ! receiver.libraryOnline (static_cast<juce::int64> (presence["expires_at_ms"])),
        "real receiver keeps the five-second presence lease boundary");
    const auto saved = presetFile.loadFileAsString();
    require (presetFile.replaceWithText ("{broken"), "damage only disposable Reference fixture");
    const auto rejected = receiver.refreshLibrary();
    require (! rejected.usable() && rejected.rejectionCode == "reference_library_preset_rejected",
             "damaged Reference payload is rejected while updater is running");
    require (presetFile.replaceWithText (saved, false, false, "\n") && receiver.refreshLibrary().usable(),
             "restored exact Reference publication remains receivable");
}

void verifyRecord (const juce::File& data)
{
    juce::Array<juce::File> files;
    data.findChildFiles (files, juce::File::findFiles, true, "*.json");
    juce::var manifest;
    for (const auto& file : files)
    {
        const auto candidate = juce::JSON::parse (file);
        if (candidate["schema_version"] == "pair_record_session.v1") manifest = candidate;
        require (! file.getFullPathName().contains ("/.failed/"), "paired fixture creates no failed Record artifacts");
    }
    require (manifest.getDynamicObject() != nullptr, "real FFI writes a committed PairRecordSession manifest");
    juce::var pre, post;
    for (const auto* role : { "pre", "post" })
    {
        const juce::File member (manifest[role]["path"].toString());
        require (member.isAChildOf (data) && member.existsAsFile()
            && member.getFullPathName().contains (".pair_committed"),
            "committed Record member belongs to disposable plugin_data tree");
        const auto value = juce::JSON::parse (member);
        const auto* frames = value["frames"].getArray();
        require (value["schema_version"] == "1.3" && value["status"] == "closed"
            && value["role"] == juce::String (role).toUpperCase()
            && frames != nullptr && frames->size() == 40
            && static_cast<juce::int64> (value["bounce_take"]["duration_samples"]) == recordFrames
            && value["lufs_i"].isDouble(),
            "actual Record JSON has closed status, forty 100ms facts, exact 192000-sample take and loudness");
        bool foundShelf = false;
        for (const auto& file : files)
        {
            if (! file.getFullPathName().contains ("/" + juce::String (role) + "/")) continue;
            const auto shelf = juce::JSON::parse (file);
            foundShelf = foundShelf || (shelf["schema_version"] == "1.3"
                && shelf["checksum"] == value["checksum"]
                && juce::JSON::toString (shelf["frames"]) == juce::JSON::toString (value["frames"]));
        }
        require (foundShelf, "real normal TRACE shelf mirrors committed Record member checksum and facts");
        (juce::String (role) == "pre" ? pre : post) = value;
    }
    require (pre["record_session_id"] == post["record_session_id"]
        && pre["capture_generation_id"] == post["capture_generation_id"]
        && pre["paired_post_instance_id"] == postId && post["paired_pre_instance_id"] == preId,
        "actual PRE/POST Record output retains exact pairing and capture generation");
    const auto difference = static_cast<double> (post["lufs_i"]) - static_cast<double> (pre["lufs_i"]);
    require (std::abs (difference + 6.0) <= 0.1, "half-amplitude POST preserves expected -6.0 LU difference");
    std::cout << "actual plugin_data: PRE/POST 40 facts each, 192000 samples, delta=" << difference << " LU\n";
}

void verifyLifetime (update::Configuration configuration)
{
    std::atomic<bool> entered { false }, drained { false };
    int factories = 0;
    std::weak_ptr<update::Checker> weak;
    configuration.fetch = [&] (const std::atomic<bool>& cancelled) -> std::optional<juce::String>
    {
        entered = true;
        while (! cancelled) juce::Thread::sleep (1);
        juce::Thread::sleep (20); // Destructor must wait for an in-flight worker to return.
        drained = true;
        return {};
    };
    update::ServiceOwner::Factory factory = [&] (const juce::String& format)
    {
        if (auto current = weak.lock()) return current;
        ++factories;
        configuration.format = format;
        auto created = std::make_shared<update::Checker> (configuration);
        weak = created;
        return created;
    };
    auto first = std::make_unique<update::ServiceOwner> (factory);
    auto second = std::make_unique<update::ServiceOwner> (factory);
    require (factories == 0 && weak.expired(), "processor-style owners create no service before an editor opens");
    auto editor = first->get ("VST3");
    editor->setEnabled (true);
    waitFor ([&] { return entered.load(); }, "fixture worker begins before simulated editor close");
    auto otherEditor = second->get ("VST3");
    require (editor == otherEditor && factories == 1, "two live processor owners share one fixture service");
    editor.reset();
    otherEditor.reset();
    require (! weak.expired() && ! drained, "closing both editor references retains active service in processors");
    auto reopenedEditor = first->get ("VST3");
    require (reopenedEditor == weak.lock() && factories == 1, "editor reopen reuses processor-owned service");
    reopenedEditor.reset();
    first.reset();
    require (! weak.expired() && ! drained, "destroying one processor cannot cancel another processor's service");
    second.reset();
    require (weak.expired() && drained, "last owner destruction cancels and joins worker before returning to host");
    std::cout << "Update service ownership: lazy create, two editors close/reopen, last processor drains PASS\n";
}
}

int main (int argc, char** argv)
{
    require (argc == 2, "scenario is explicit");
    const juce::String scenario (argv[1]);
    require (scenario == "off" || scenario == "success" || scenario == "failure" || scenario == "cancel"
        || scenario == "lifetime",
             "supported isolated scenario");
    ValidationStorageSandbox sandbox;
   #if JUCE_WINDOWS
    const auto os = environmentRoot ("APPDATA").getChildFile ("Kirin OS");
    const auto data = environmentRoot ("LOCALAPPDATA").getChildFile ("Kirin OS/plugin_data");
   #else
    const auto os = environmentRoot ("HOME").getChildFile ("Library/Application Support/Kirin OS");
    const auto data = os.getChildFile ("plugin_data");
   #endif
    const auto identity = os.getChildFile ("identity.json");
    const auto fixture = juce::JSON::parse (juce::File (__FILE__).getSiblingFile (
        "fixtures/update_manifest_fixture.json"));
    require (fixture["cases"][0]["wire"].isString(), "signed disposable update fixture is present");
    std::atomic<bool> fetching { false }, releaseFetch { false }, fetchFinished { false };
    std::atomic<int> calls { 0 };
    update::Configuration configuration;
    configuration.root = os.getParentDirectory().getChildFile ("UpdateIntegrationFixture");
   #if JUCE_MAC
    require (configuration.root.createDirectory(), "create disposable update root before resolving the macOS /var alias");
    configuration.root = juce::File (juce::String (std::filesystem::canonical (
        configuration.root.getFullPathName().toStdString()).string()));
   #endif
    configuration.version = "1.1.50";
   #if JUCE_WINDOWS
    configuration.platform = "windows";
   #else
    configuration.platform = "macos";
   #endif
    configuration.publicKey = fixture["publicKey"].toString();
    configuration.now = [fixture] { return static_cast<juce::int64> (fixture["now"]); };
    configuration.fetch = [&] (const std::atomic<bool>& cancelled) -> std::optional<juce::String>
    {
        ++calls; fetching = true;
        while (! releaseFetch && ! cancelled) juce::Thread::sleep (1);
        fetchFinished = true;
        if (cancelled || scenario == "failure") return {};
        return fixture["cases"][0]["wire"].toString();
    };
    if (scenario == "lifetime") { verifyLifetime (configuration); return 0; }
    update::Checker checker (configuration);
    if (scenario == "off")
    {
        checker.request (false);
        waitFor ([&] { return ! checker.snapshot().busy && checker.snapshot().revision > 0; }, "OFF check completes");
        require (! checker.snapshot().enabled && calls == 0, "OFF performs zero fixture fetches");
    }
    else
    {
        checker.setEnabled (true);
        waitFor ([&] { return fetching.load(); }, "ON checker enters injected fetch");
        require (checker.snapshot().busy, "updater worker is demonstrably running during receiver/license work");
    }
    receiveReference (data.getChildFile ("reference/v2"));
    require (kirin_hypha_load_license() == 2, "missing disposable identity resolves Unknown");
    require (identity.replaceWithText ("{"), "publish malformed disposable identity");
    require (kirin_hypha_load_license() == 2, "real license recheck rejects malformed identity");
    writeLicense (identity, "sense");
    require (kirin_hypha_load_license() == 1, "real license recheck observes Sense");
    writeLicense (identity, "os");
    require (kirin_hypha_load_license() == 0, "real license recheck recovers Os");
    const auto identityBytes = identity.loadFileAsString();
    {
        RecordPair pair;
        require (pair.pre && pair.post, "real PRE/POST C ABI engines are created in isolated storage");
        pair.drive (15);
        writeLicense (identity, "sense");
        require (! kirin_hypha_keep (pair.post.get()), "user Keep rechecks disk and denies Sense during updater activity");
        writeLicense (identity, "os");
        require (kirin_hypha_keep (pair.post.get()), "user Keep rechecks disk and admits restored Os");
        waitFor ([&] {
            pair.heartbeat();
            return kirin_hypha_keep_phase (pair.post.get()) == KIRIN_KEEP_PHASE_ARMED
                && kirin_hypha_is_recording (pair.pre.get());
        }, "real paired Record reaches both-writer ARMED barrier");
        if (scenario != "off") require (checker.snapshot().busy && ! fetchFinished,
                                         "update fetch overlaps real paired Record admission");
        if (scenario == "cancel") checker.setEnabled (false);
        pair.position = 0;
        pair.drive (10);
        releaseFetch = true;
        if (scenario != "off")
            waitFor ([&] { return fetchFinished && ! checker.snapshot().busy; }, "updater completes while real Record is active");
        require (kirin_hypha_is_recording (pair.post.get()), "update completion/cancellation cannot stop active Record");
        pair.drive (30);
        const auto signal = juce::JSON::parse (data.getChildFile (
            juce::String (project) + "/record_signal/" + postId + ".json"));
        require (signal["status"] == "acknowledged" && publishDrop (data, signal),
                 "publish real exact-generation Drop transaction and disposable four-second WAV");
        waitFor ([&] { pair.heartbeat(); return ! kirin_hypha_is_recording (pair.post.get()); },
                 "real Drop closes POST Record");
        waitFor ([&] { pair.heartbeat(); return ! kirin_hypha_is_recording (pair.pre.get()); },
                 "real Drop closes PRE Record");
    }
    verifyRecord (data);
    require (identity.loadFileAsString() == identityBytes, "updater and Record preserve the restored entitlement bytes");
    const auto state = checker.snapshot();
    require (calls == (scenario == "off" ? 0 : 1), "every scenario retains exactly its expected fetch count");
    require (scenario == "off" ? ! state.enabled
        : scenario == "success" ? state.enabled && state.status == update::Status::available
        : scenario == "failure" ? state.enabled && state.status == update::Status::failed : ! state.enabled,
        "actual update outcome matches OFF/signed-success/failure/cancel scenario");
    if (scenario != "off")
    {
        const auto before = checker.snapshot().revision;
        checker.request (true);
        waitFor ([&] { const auto current = checker.snapshot();
            return ! current.busy && current.revision > before; }, "daily-budget recheck settles");
        require (calls == 1, "success/failure/cancel shares the persisted daily attempt budget");
    }
    std::cout << "Update + real Reference receiver + license recheck + paired Record: " << scenario << " PASS\n";
    return 0;
}
