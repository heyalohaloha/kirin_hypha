#include "../src/appearance/AppearanceContract.h"
#include "../src/appearance/AppearanceFileLock.h"
#include "../src/appearance/AppearanceService.h"
#include "../src/appearance/AppearanceStorage.h"

#include <cstdlib>
#include <iostream>
#include <thread>

namespace
{
constexpr auto activationId = "123e4567-e89b-42d3-a456-426614174000";
constexpr auto replacementId = "9a17dfd5-4dba-4f23-99c7-2c2709dfbb92";
constexpr auto activatedAt = "2026-09-11T04:05:06.000Z";
constexpr auto maskingGuideId = "8bcb7a52-7aac-4da6-b284-5997969481be";

void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "Appearance contract failed at line " << line << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_APPEARANCE_REQUIRE(expression) require ((expression), #expression, __LINE__)

juce::String activationJson (const juce::String& id = activationId)
{
    return juce::String ("{\n"
           "  \"schema_version\": \"1.1.0\",\n"
           "  \"kind\": \"kirin_os_jungle_activation\",\n"
           "  \"activation_id\": \"") + id + "\",\n"
           "  \"activated_at\": \"2026-09-11T04:05:06.000Z\",\n"
           "  \"jungle_activated_at\": \"2026-09-11T04:04:00.000Z\",\n"
           "  \"masking_guide_id\": \"8bcb7a52-7aac-4da6-b284-5997969481be\",\n"
           "  \"masking_sent_at\": \"2026-09-11T04:05:06.000Z\",\n"
           "  \"theme\": \"ce2226\"\n"
           "}\n";
}

juce::File makeRoot()
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("kirin-hypha-appearance", {}, false);
    KIRIN_APPEARANCE_REQUIRE (root.createDirectory().wasOk());
    return root;
}

void writeActivation (hypha::appearance::Storage& storage,
                      const juce::String& id = activationId)
{
    KIRIN_APPEARANCE_REQUIRE (
        storage.activationFile().getParentDirectory().createDirectory().wasOk());
    KIRIN_APPEARANCE_REQUIRE (storage.activationFile().replaceWithText (activationJson (id)));
    KIRIN_APPEARANCE_REQUIRE (
        storage.activationFile().getSiblingFile (
            storage.activationFile().getFileName() + ".bak").replaceWithText (activationJson (id)));
}

void verifyDecoding()
{
    using namespace hypha::appearance;
    const auto activation = decodeActivation (activationJson());
    KIRIN_APPEARANCE_REQUIRE (activation.state == DecodeState::valid);
    KIRIN_APPEARANCE_REQUIRE (activation.value->id == activationId);
    KIRIN_APPEARANCE_REQUIRE (activation.value->activatedAt == activatedAt);
    KIRIN_APPEARANCE_REQUIRE (activation.value->maskingGuideId == maskingGuideId);
    KIRIN_APPEARANCE_REQUIRE (decodeActivation (
        activationJson().replace ("\"theme\": \"ce2226\"",
                                  "\"theme\": \"ce2226\", \"user\": \"private\"")).state
        == DecodeState::invalid);
    KIRIN_APPEARANCE_REQUIRE (decodeActivation (
        activationJson().replace ("1.1.0", "2.0.0")).state == DecodeState::futureSchema);

    auto preference = defaultPreference();
    const auto encodedDefault = encodePreference (preference);
    KIRIN_APPEARANCE_REQUIRE (encodedDefault.getNumBytesAsUTF8() < maxDocumentBytes);
    const auto decodedDefault = decodePreference (encodedDefault);
    KIRIN_APPEARANCE_REQUIRE (decodedDefault.state == DecodeState::valid);
    KIRIN_APPEARANCE_REQUIRE (! makeSnapshot (*decodedDefault.value, true).enabled);

    preference.revision = 1;
    preference.activationSeen = true;
    preference.firstActivationId = activationId;
    const auto snapshot = makeSnapshot (preference, true);
    KIRIN_APPEARANCE_REQUIRE (snapshot.enabled && snapshot.noticePending);
    preference.choice = Choice::off;
    KIRIN_APPEARANCE_REQUIRE (! makeSnapshot (preference, true).enabled);
}

void verifyStorageAndRecovery()
{
    using namespace hypha::appearance;
    const auto root = makeRoot();
    {
        Storage storage (root);
        writeActivation (storage);
        const auto activation = storage.readActivation();
        KIRIN_APPEARANCE_REQUIRE (activation.state == StoredState::valid);
        auto projected = storage.updatePreference (activation.value, std::nullopt, false);
        KIRIN_APPEARANCE_REQUIRE (projected.state == TransactionState::persisted);
        KIRIN_APPEARANCE_REQUIRE (projected.preference.revision == 1);
        KIRIN_APPEARANCE_REQUIRE (projected.preference.firstActivationId == activationId);

        KIRIN_APPEARANCE_REQUIRE (storage.preferenceFile().replaceWithText ("{ broken"));
        const auto recovered = storage.readPreference();
        KIRIN_APPEARANCE_REQUIRE (recovered.state == StoredState::valid && recovered.fromBackup);
        const auto off = storage.updatePreference (std::nullopt, Choice::off, false);
        KIRIN_APPEARANCE_REQUIRE (off.state == TransactionState::persisted);
        KIRIN_APPEARANCE_REQUIRE (off.preference.revision == 2);
        KIRIN_APPEARANCE_REQUIRE (decodePreference (
            storage.preferenceFile().loadFileAsString()).state == DecodeState::valid);

        writeActivation (storage, replacementId);
        const auto unchanged = storage.updatePreference (
            storage.readActivation().value, std::nullopt, false);
        KIRIN_APPEARANCE_REQUIRE (unchanged.preference.firstActivationId == activationId);
        KIRIN_APPEARANCE_REQUIRE (unchanged.preference.choice == Choice::off);
    }
    KIRIN_APPEARANCE_REQUIRE (root.deleteRecursively());
}

void verifyConcurrentFieldUpdates()
{
    using namespace hypha::appearance;
    const auto root = makeRoot();
    Storage initial (root);
    writeActivation (initial);
    const auto activation = initial.readActivation().value;
    KIRIN_APPEARANCE_REQUIRE (
        initial.updatePreference (activation, std::nullopt, false).state
        == TransactionState::persisted);

    TransactionResult choiceResult;
    TransactionResult noticeResult;
    std::thread choiceThread ([&]
    {
        Storage storage (root);
        choiceResult = storage.updatePreference (std::nullopt, Choice::off, false);
    });
    std::thread noticeThread ([&]
    {
        Storage storage (root);
        noticeResult = storage.updatePreference (std::nullopt, std::nullopt, true);
    });
    choiceThread.join();
    noticeThread.join();
    KIRIN_APPEARANCE_REQUIRE (choiceResult.state == TransactionState::persisted);
    KIRIN_APPEARANCE_REQUIRE (noticeResult.state == TransactionState::persisted);

    Storage final (root);
    const auto preference = final.readPreference().value;
    KIRIN_APPEARANCE_REQUIRE (preference.has_value());
    KIRIN_APPEARANCE_REQUIRE (preference->revision == 3);
    KIRIN_APPEARANCE_REQUIRE (preference->choice == Choice::off);
    KIRIN_APPEARANCE_REQUIRE (preference->noticeAcknowledged);
    KIRIN_APPEARANCE_REQUIRE (root.deleteRecursively());
}

void verifyService()
{
    using namespace hypha::appearance;
    const auto root = makeRoot();
    Storage setup (root);
    writeActivation (setup);
    {
        Service service (root);
        service.refreshSynchronouslyForTest();
        KIRIN_APPEARANCE_REQUIRE (service.snapshot().enabled);
        KIRIN_APPEARANCE_REQUIRE (service.snapshot().noticePending);
        const auto request = service.setChoice (Choice::off);
        KIRIN_APPEARANCE_REQUIRE (request.state == UserActionState::pending);
        for (int attempt = 0; attempt < 100
             && service.latestUserAction().state == UserActionState::pending; ++attempt)
            juce::Thread::sleep (5);
        KIRIN_APPEARANCE_REQUIRE (service.latestUserAction().state == UserActionState::persisted);
        KIRIN_APPEARANCE_REQUIRE (! service.snapshot().enabled);
        KIRIN_APPEARANCE_REQUIRE (service.snapshot().persistent);
        const auto stored = setup.readPreference();
        KIRIN_APPEARANCE_REQUIRE (stored.value->choice == Choice::off);

        FileLock blocker (setup.preferenceLockDirectory());
        KIRIN_APPEARANCE_REQUIRE (blocker.tryAcquire());
        const auto sessionOnly = service.setChoice (Choice::on);
        KIRIN_APPEARANCE_REQUIRE (sessionOnly.state == UserActionState::pending);
        for (int attempt = 0; attempt < 150
             && service.latestUserAction().state == UserActionState::pending; ++attempt)
            juce::Thread::sleep (5);
        KIRIN_APPEARANCE_REQUIRE (service.latestUserAction().state == UserActionState::failed);
        KIRIN_APPEARANCE_REQUIRE (service.snapshot().enabled);
        KIRIN_APPEARANCE_REQUIRE (! service.snapshot().persistent);
        KIRIN_APPEARANCE_REQUIRE (setup.readPreference().value->choice == Choice::off);
    }
    KIRIN_APPEARANCE_REQUIRE (root.deleteRecursively());
}

void verifyFuturePreferenceIsPreserved()
{
    using namespace hypha::appearance;
    const auto root = makeRoot();
    Storage setup (root);
    writeActivation (setup);
    const auto future = juce::String ("{\n  \"schema_version\": \"2.0.0\",\n")
        + "  \"future\": true\n}\n";
    KIRIN_APPEARANCE_REQUIRE (
        setup.preferenceFile().getParentDirectory().createDirectory().wasOk());
    KIRIN_APPEARANCE_REQUIRE (setup.preferenceFile().replaceWithText (future));
    const auto preserved = setup.preferenceFile().loadFileAsString();

    KIRIN_APPEARANCE_REQUIRE (
        setup.updatePreference (setup.readActivation().value, Choice::on, true).state
        == TransactionState::unsupported);
    KIRIN_APPEARANCE_REQUIRE (setup.preferenceFile().loadFileAsString() == preserved);
    {
        Service service (root);
        service.refreshSynchronouslyForTest();
        const auto snapshot = service.snapshot();
        KIRIN_APPEARANCE_REQUIRE (! snapshot.activationSeen);
        KIRIN_APPEARANCE_REQUIRE (! snapshot.enabled);
    }
    KIRIN_APPEARANCE_REQUIRE (root.deleteRecursively());
}

int runLockChild (const juce::String& rootPath)
{
    hypha::appearance::FileLock lock (juce::File (rootPath).getChildFile ("test.lock"));
    if (! lock.tryAcquire())
        return EXIT_FAILURE;
    juce::File (rootPath).getChildFile ("child-ready").replaceWithText ("ready");
    juce::Thread::sleep (500);
    return EXIT_SUCCESS;
}

void verifyOperatingSystemLock()
{
    const auto root = makeRoot();
    {
        hypha::appearance::FileLock first (root.getChildFile ("same-process.lock"));
        hypha::appearance::FileLock second (root.getChildFile ("same-process.lock"));
        KIRIN_APPEARANCE_REQUIRE (first.tryAcquire());
        KIRIN_APPEARANCE_REQUIRE (! second.tryAcquire());
    }
    const auto executable = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    juce::ChildProcess child;
    KIRIN_APPEARANCE_REQUIRE (child.start (juce::StringArray {
        executable.getFullPathName(), "--lock-child", root.getFullPathName()
    }));
    const auto ready = root.getChildFile ("child-ready");
    for (int attempt = 0; attempt < 100 && ! ready.existsAsFile(); ++attempt)
        juce::Thread::sleep (5);
    KIRIN_APPEARANCE_REQUIRE (ready.existsAsFile());
    hypha::appearance::FileLock competing (root.getChildFile ("test.lock"));
    KIRIN_APPEARANCE_REQUIRE (! competing.tryAcquire());
    KIRIN_APPEARANCE_REQUIRE (child.waitForProcessToFinish (2000));

    {
        hypha::appearance::FileLock afterExit (root.getChildFile ("test.lock"));
        KIRIN_APPEARANCE_REQUIRE (afterExit.tryAcquire());
    }
    KIRIN_APPEARANCE_REQUIRE (root.deleteRecursively());
}
}

int main (int argc, char** argv)
{
    if (argc == 3 && juce::String (argv[1]) == "--lock-child")
        return runLockChild (juce::String (argv[2]));
    verifyDecoding();
    verifyStorageAndRecovery();
    verifyConcurrentFieldUpdates();
    verifyService();
    verifyFuturePreferenceIsPreserved();
    verifyOperatingSystemLock();
    std::cout << "Appearance contract: PASS\n";
    return EXIT_SUCCESS;
}
