#include "ValidationStorageSandbox.h"
#include "../src/pre_display/PreDisplayController.h"
#include "../src/pre_display/PreDisplayPresence.h"
#include "../src/reference_audition/ReferenceAuditionRepository.h"
#include "../src/reference_audition/ReferenceRuntimeV2Repository.h"
#include "../src/reference_audition/ReferenceRecoveryTransport.h"
#include "../src/CaptureWorkAttachment.h"
#include "../src/appearance/AppearanceStorage.h"
#include <chrono>
#include <iostream>
#include <thread>

namespace
{
constexpr auto variable = "KIRIN_HYPHA_TEST_STORAGE_ROOT";
void require (bool ok, const char* why)
{
    if (! ok) throw std::runtime_error (why);
}
void setRoot (const char* value)
{
   #if JUCE_WINDOWS
    require (::_putenv_s (variable, value == nullptr ? "" : value) == 0, "set test root");
   #else
    require ((value == nullptr ? ::unsetenv (variable) : ::setenv (variable, value, 1)) == 0,
             "set test root");
   #endif
}
juce::File pluginData (const juce::File& root)
{
   #if JUCE_WINDOWS
    return root.getChildFile ("home/AppData/Local/Kirin OS/plugin_data");
   #else
    return root.getChildFile ("home/Library/Application Support/Kirin OS/plugin_data");
   #endif
}
void verifyPaths (const juce::File& data)
{
    // These getters perform no writes. Check every path before starting any worker, so the
    // original regression fails without touching the real user's directories.
    bool valid = true;
    const auto check = [&] (bool ok, const char* why)
    {
        if (! ok) { std::cerr << why << '\n'; valid = false; }
    };
    check (hypha::pre_display::Controller::transportRoot() == data.getChildFile ("pre_display/v1"),
             "pre_display must follow the Rust storage layout");
    check (hypha::reference_audition::Repository::transportRoot() == data.getChildFile ("hypha_ab/v1"),
             "legacy Reference must follow the Rust storage layout");
    check (hypha::reference_audition::RuntimeV2Repository::transportRoot() == data.getChildFile ("reference/v2"),
             "Reference must follow the Rust storage layout");
    check (hypha::reference_audition::RecoveryTransport::transportRoot() == data.getChildFile ("reference/v2"),
             "Reference recovery must follow the Rust storage layout");
    check (hypha::capture::WorkAttachmentController::transportRoot() == data.getChildFile ("hypha_capture/v1"),
             "Capture Work must follow the Rust storage layout");
    check (hypha::appearance::Storage::defaultSharedPluginDataRoot() == data,
             "appearance must follow the Rust storage layout");
    require (valid, "storage path contract");
}
void verifyLeases (const juce::File& data)
{
    verifyPaths (data);
    const auto root = data.getChildFile ("pre_display/v1");
    const auto exists = [&] (const char* kind, const char* id)
    { return root.getChildFile (kind).getChildFile (juce::String (id) + ".json").existsAsFile(); };
    hypha::pre_display::ClockTap clock;
    {
        hypha::pre_display::Controller pre (clock), post (clock), invalid (clock);
        hypha::pre_display::RuntimeIdentity identity;
        identity.instanceId = "fixture-instance";
        identity.projectUuid = "fixture-project";
        identity.runtimeInstanceId = "fixture-pre";
        pre.configureAndStart (identity);
        identity.role = hypha::pre_display::GuideTargetRole::post;
        identity.runtimeInstanceId = "fixture-post";
        post.configureAndStart (identity);
        identity.instanceId = "../invalid";
        identity.runtimeInstanceId = "fixture-invalid";
        invalid.configureAndStart (identity);
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds (5);
        while (! (exists ("presence", "fixture-pre") && exists ("capability", "fixture-pre")
               && exists ("presence", "fixture-post") && exists ("capability", "fixture-post")))
        {
            require (std::chrono::steady_clock::now() < until, "both roles publish to the same fixture");
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
        require (! exists ("presence", "fixture-invalid") && ! exists ("capability", "fixture-invalid"),
                 "invalid identity cannot publish");
    }
    for (auto kind : { "presence", "capability" })
        require (! exists (kind, "fixture-pre") && ! exists (kind, "fixture-post"), "own leases removed on teardown");
}
}

int main (int argc, char** argv)
try
{
    require (argc == 2, "explicit storage scenario required");
    const juce::String mode (argv[1]);
    const auto inherited = juce::SystemStats::getEnvironmentVariable (variable, {});
    require (inherited.isNotEmpty(), "CTest storage root required");
    const auto root = juce::File (inherited).getChildFile (juce::String::fromUTF8 (u8"storage paths 測定-")
                                                        + juce::Uuid().toString());
    if (mode == "environment")
    {
        setRoot (root.getFullPathName().toRawUTF8());
        verifyPaths (pluginData (root));
        require (! root.exists(), "resolving an absent root must not create it");
        verifyLeases (pluginData (root));
        hypha::pre_display::Controller::placeUnderForTest (root);
        hypha::reference_audition::RuntimeV2Repository::setTransportRootForTesting (root);
        require (hypha::pre_display::Controller::transportRoot() == root.getChildFile ("Kirin OS/plugin_data/pre_display/v1")
                 && hypha::reference_audition::RuntimeV2Repository::transportRoot() == root,
                 "explicit transport overrides retain precedence");
        hypha::pre_display::Controller::placeUnderForTest ({});
        hypha::reference_audition::RuntimeV2Repository::setTransportRootForTesting ({});
        verifyPaths (pluginData (root));
    }
    else if (mode == "sandbox")
    {
        {
            ValidationStorageSandbox sandbox;
            require (juce::SystemStats::getEnvironmentVariable (variable, {}) == sandbox.directory().getFullPathName(),
                     "sandbox must retain a per-fixture test storage root");
            verifyLeases (pluginData (sandbox.directory()));
           #if JUCE_WINDOWS
            const auto data = juce::File (juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {}))
                                 .getChildFile ("Kirin OS/plugin_data");
           #else
            const auto data = juce::File (juce::SystemStats::getEnvironmentVariable ("HOME", {}))
                                 .getChildFile ("Library/Application Support/Kirin OS/plugin_data");
           #endif
            require (data == pluginData (sandbox.directory()), "fixture readers and product writers share storage");
            {
                ValidationStorageSandbox nested;
                verifyPaths (pluginData (nested.directory()));
            }
            verifyPaths (data);
            require (juce::SystemStats::getEnvironmentVariable (variable, {}) == sandbox.directory().getFullPathName(),
                     "nested fixture restores its parent's root");
        }
        require (juce::SystemStats::getEnvironmentVariable (variable, {}) == inherited, "sandbox restores inherited root");
    }
    else if (mode == "defaults")
    {
        // Read-only: never construct a controller while testing real default paths.
        setRoot (nullptr);
       #if JUCE_WINDOWS
        const auto data = juce::File::getSpecialLocation (juce::File::windowsLocalAppData)
                              .getChildFile ("Kirin OS/plugin_data");
       #else
        const auto data = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                              .getChildFile ("Library/Application Support/Kirin OS/plugin_data");
       #endif
        verifyPaths (data);
        setRoot ("");
        verifyPaths (data);
    }
    else if (mode == "relative")
    {
        const auto relative = root.getRelativePathFrom (juce::File::getCurrentWorkingDirectory());
        setRoot (relative.toRawUTF8());
        verifyPaths (pluginData (root));
        require (! root.exists(), "relative path resolution is read-only");
    }
    else if (mode == "unwritable")
    {
        setRoot (root.getFullPathName().toRawUTF8());
        verifyPaths (pluginData (root));
        require (root.getParentDirectory().createDirectory().wasOk() && root.replaceWithText ("fixture"), "blocked root fixture");
        hypha::pre_display::RuntimeIdentity identity;
        identity.instanceId = "fixture-instance";
        identity.projectUuid = "fixture-project";
        identity.hostProcessId = 1;
        const auto transport = hypha::pre_display::Controller::transportRoot();
        require (! hypha::pre_display::writePresence (transport, identity, {}, 0, 1)
                 && ! hypha::pre_display::writeCapability (transport, identity, 1), "unwritable root fails without fallback");
        verifyPaths (pluginData (root));
        require (root.loadFileAsString() == "fixture", "blocked root retained");
    }
    else throw std::runtime_error ("unknown storage scenario");
    require (root.deleteRecursively(), "remove only the owned fixture");
    std::cout << "Storage paths " << mode << " PASS\n";
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
