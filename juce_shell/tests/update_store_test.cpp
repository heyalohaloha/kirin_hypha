#include "../src/update/UpdateStore.h"

#include <cstdlib>
#include <iostream>
#include <thread>

#if ! JUCE_WINDOWS
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace
{
using Store = hypha::update::Store;
void require (bool value, const char* message)
{
    if (value) return;
    std::cerr << "Update store: " << message << '\n';
    std::exit (1);
}

int child (int argc, char** argv)
{
    if (argc != 3) return -1;
    const juce::String mode (argv[1]);
    Store store (juce::File (juce::String::fromUTF8 (argv[2])));
    if (mode == "--busy") return store.acquire() ? 1 : 0;
    if (mode != "--reserve-and-exit") return -1;
    if (! store.acquire()) return 1;
    Store::State state;
    if (! store.read (state)) return 1;
    state.enabled = true;
    state.lastAttempt = 1'800'000'000'000;
    if (! store.save (state)) return 1;
    std::_Exit (0); // Simulate a crashed owner: no destructor/unlock, but the reservation is durable.
}

void runChild (const char* mode, const juce::File& root)
{
    juce::ChildProcess process;
    juce::StringArray arguments;
    arguments.add (juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName());
    arguments.add (mode);
    arguments.add (root.getFullPathName());
    require (process.start (arguments), "start isolated child process");
    require (process.waitForProcessToFinish (10'000), "child process finishes within fixture deadline");
    require (process.getExitCode() == 0, "child process lock expectation");
}

void normalAndLock (const juce::File& fixture)
{
    const auto root = fixture.getChildFile ("normal/UpdateCheck/v1");
    Store first (root), second (root);
    Store::State initial;
    require (! first.read (initial) && ! first.save (initial), "read/save require the OS lock");
    require (first.acquire() && ! first.acquire(), "acquire is non-reentrant");
    require (first.read (initial) && ! initial.enabled && initial.lastAttempt == 0,
             "missing state starts disabled without a budget claim");
    require (! second.acquire(), "separate Store in same process cannot enter");
    bool otherThreadEntered = false;
    std::thread thread ([&] { Store threaded (root); otherThreadEntered = threaded.acquire(); });
    thread.join();
    require (! otherThreadEntered, "different thread and Store cannot enter");
    runChild ("--busy", root);
    initial.enabled = true; initial.lastAttempt = 1234; initial.lastSuccess = 1234; initial.lastSequence = 17;
    initial.wire = "{\"payload\":\"unchanged\\nwire\"}";
    initial.dismissedVersion = "1.2.3";
    require (first.save (initial), "durable atomic save");
    Store::State observed;
    require (second.observe (observed) && observed.enabled && observed.lastAttempt == 1234
        && observed.wire == initial.wire && observed.lastSequence == 17,
        "contending reader observes committed preference without claiming owner lock");
    require (! second.acquire() && ! second.save (observed),
        "observation cannot grant a write lease or a check reservation");
    first.release();
    require (second.acquire(), "next owner enters after release");
    Store::State restored;
    require (second.read (restored) && restored.enabled && restored.lastAttempt == 1234
        && restored.lastSuccess == 1234 && restored.lastSequence == 17 && restored.wire == initial.wire
        && restored.dismissedVersion == "1.2.3", "all state survives reopening");
    auto rollback = restored;
    rollback.lastSequence = 16;
    require (! second.save (rollback), "publication sequence cannot regress in storage");
    rollback = restored; rollback.lastAttempt = 1233; rollback.lastSuccess = 1233;
    require (! second.save (rollback), "attempt and success stamps cannot regress in storage");
    require (root.findChildFiles (juce::File::findFiles, false, "pending-*.tmp").isEmpty(),
             "atomic writes leave no temporary files");
    second.release();
    runChild ("--reserve-and-exit", root);
    require (first.acquire() && first.read (restored) && restored.lastAttempt == 1'800'000'000'000,
             "OS releases a dead owner while its attempted-check budget remains");
}

void rejectedStates (const juce::File& fixture)
{
    const auto root = fixture.getChildFile ("rejected");
    Store store (root);
    require (store.acquire(), "rejection fixture lock");
    const auto file = root.getChildFile ("state.json");
    const juce::String valid = R"({"format":"kirin_hypha_update_cache","version":"1.0","enabled":false,"last_attempt_seconds":0,"last_success_seconds":0,"last_sequence":0,"wire":"","dismissed_version":""})";
    const juce::StringArray bad {
        "{", "", valid + "trailing", valid.replace ("false", "0"),
        valid.replace ("\"last_attempt_seconds\":0", "\"last_attempt_seconds\":-1"),
        valid.replace ("\"last_success_seconds\":0", "\"last_success_seconds\":1"),
        valid.replace ("\"last_success_seconds\":0", "\"last_success_seconds\":-1"),
        valid.replace ("\"last_sequence\":0", "\"last_sequence\":0.5"),
        valid.replace ("\"version\":\"1.0\"", "\"version\":\"2.0\""),
        valid.replace ("\"enabled\":false", "\"enabled\":true,\"enabled\":false"),
        valid.replace ("\"dismissed_version\":\"\"", "\"dismissed_version\":\"\",\"extra\":1"),
        juce::String::repeatedString ("x", 32 * 1024 + 1)
    };
    for (const auto& bytes : bad)
    {
        require (file.replaceWithText (bytes), "write malformed disposable state");
        Store::State result;
        require (! store.read (result) && ! store.observe (result) && ! store.save ({}),
                 "damaged state cannot be read, observed or silently repaired");
    }
    require (file.replaceWithText (valid), "restore valid disposable state");
    Store::State oversized;
    oversized.wire = juce::String::repeatedString ("a", 16 * 1024 + 1);
    require (! store.save (oversized), "oversized cached response cannot be saved");
    const char nul[] = { '\0' };
    require (file.appendData (nul, 1), "append a NUL to the disposable state");
    require (! store.read (oversized) && ! store.observe (oversized), "embedded NUL cannot hide trailing state data");
    store.release();
    require (file.deleteFile() && file.createDirectory(), "make state an unusable directory");
    require (store.acquire() && ! store.read (oversized) && ! store.observe (oversized)
        && ! store.save ({}), "non-regular state fails closed");
    const auto notDirectory = fixture.getChildFile ("not-a-directory");
    require (notDirectory.replaceWithText ("fixture"), "make unusable root");
    Store denied (notDirectory.getChildFile ("UpdateCheck"));
    require (! denied.acquire(), "root access failure cannot claim a check");
    const auto absent = fixture.getChildFile ("observation-must-not-create/UpdateCheck/v1");
    Store observer (absent);
    require (! observer.observe (oversized) && ! absent.getParentDirectory().getParentDirectory().exists(),
             "observation of absent state never creates storage or lock files");
}

void linkedPaths (const juce::File& fixture)
{
   #if ! JUCE_WINDOWS
    const auto unrelated = fixture.getChildFile ("unrelated");
    require (unrelated.createDirectory(), "create unrelated sentinel directory");
    const auto sentinel = unrelated.getChildFile ("sentinel");
    require (sentinel.replaceWithText ("unchanged"), "write unrelated sentinel");
    const auto linked = fixture.getChildFile ("linked-root");
    require (::symlink (unrelated.getFullPathName().toRawUTF8(), linked.getFullPathName().toRawUTF8()) == 0,
             "create disposable directory symlink");
    Store linkedStore (linked.getChildFile ("child"));
    Store::State result;
    require (! linkedStore.observe (result) && ! linkedStore.acquire() && ! unrelated.getChildFile ("child").exists(),
             "linked parent is rejected before creating or writing any descendant");
    const auto root = fixture.getChildFile ("linked-state");
    Store store (root);
    require (store.acquire(), "linked-state fixture lock");
    const auto state = root.getChildFile ("state.json");
    require (::symlink (sentinel.getFullPathName().toRawUTF8(), state.getFullPathName().toRawUTF8()) == 0,
             "create disposable state symlink");
    require (! store.read (result) && ! store.observe (result) && ! store.save ({}),
             "linked state cannot be followed or replaced");
    require (sentinel.loadFileAsString() == "unchanged", "unrelated sentinel remains untouched");
    store.release();
    require (state.deleteFile(), "remove only fixture symlink");
    require (::link (sentinel.getFullPathName().toRawUTF8(), state.getFullPathName().toRawUTF8()) == 0,
             "create disposable state hard link");
    require (store.acquire() && ! store.read (result) && ! store.observe (result) && ! store.save ({}),
             "multiply linked state is rejected");
    store.release();
    require (state.deleteFile() && sentinel.loadFileAsString() == "unchanged", "unlink only disposable alias");
    require (::chmod (root.getFullPathName().toRawUTF8(), 0500) == 0, "make fixture unwritable");
    require (store.acquire() && ! store.save ({}), "reservation persistence failure fails closed");
    store.release();
    require (::chmod (root.getFullPathName().toRawUTF8(), 0700) == 0, "restore disposable fixture permissions");
   #else
    juce::ignoreUnused (fixture); // Reparse rejection is also enforced on every opened native handle.
   #endif
}
}

int main (int argc, char** argv)
{
    const auto outcome = child (argc, argv);
    if (outcome >= 0) return outcome;
   #if JUCE_MAC
    const auto parent = juce::File ("/private/tmp"); // Avoid the system /var symlink: parents are checked with O_NOFOLLOW.
   #else
    const auto parent = juce::File::getSpecialLocation (juce::File::tempDirectory);
   #endif
    const auto fixture = parent.getChildFile ("hypha-update-store-" + juce::Uuid().toString());
    require (fixture.createDirectory(), "create disposable test root");
    struct Cleanup { juce::File path; ~Cleanup() { path.deleteRecursively(); } } cleanup { fixture };
    normalAndLock (fixture);
    rejectedStates (fixture);
    linkedPaths (fixture);
    std::cout << "Update store fixtures: pass\n";
}
