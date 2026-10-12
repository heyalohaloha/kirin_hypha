#pragma once
#include "../src/pre_display/PreDisplayController.h"
#include <chrono>
#include <stdexcept>
#include <thread>

namespace hypha::tests
{
inline void requirePublicationStorage (const juce::File& sandbox)
{
    // Check BEFORE constructing processors or starting a worker: a regression must fail without
    // creating even a temporary lease in the user's tree. Resolving the path itself is read-only.
    const auto expected = sandbox.getChildFile ("Kirin OS/plugin_data/pre_display/v1");
    if (pre_display::Controller::transportRoot() != expected)
        throw std::runtime_error ("PRE/POST guide transport must resolve inside the fixture");
}

inline void verifyPublicationStorage (const juce::File& sandbox)
{
    requirePublicationStorage (sandbox);
    const auto root = pre_display::Controller::transportRoot();
    const auto hasLease = [&] (const char* kind, const char* id)
    {
        return root.getChildFile (kind).getChildFile (juce::String (id) + ".json").existsAsFile();
    };
    pre_display::ClockTap clock;
    {
        // Both default constructors must select the same isolated location, just as the two
        // roles in the publication product fixture do, independently of its compiled role.
        pre_display::Controller pre (clock), post (clock), invalid (clock);
        pre_display::RuntimeIdentity identity;
        identity.instanceId = "fixture-instance";
        identity.projectUuid = "fixture-project";
        identity.runtimeInstanceId = "fixture-pre";
        identity.role = pre_display::GuideTargetRole::pre;
        pre.configureAndStart (identity);
        identity.runtimeInstanceId = "fixture-post";
        identity.role = pre_display::GuideTargetRole::post;
        post.configureAndStart (identity);
        identity.runtimeInstanceId = "fixture-invalid";
        identity.instanceId = "../invalid";
        invalid.configureAndStart (identity);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (5);
        while (! (hasLease ("presence", "fixture-pre") && hasLease ("capability", "fixture-pre")
               && hasLease ("presence", "fixture-post") && hasLease ("capability", "fixture-post")))
        {
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error ("both roles must publish leases in the fixture");
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
        if (hasLease ("presence", "fixture-invalid") || hasLease ("capability", "fixture-invalid"))
            throw std::runtime_error ("invalid identity must not publish a lease");
    }
    for (const auto* kind : { "presence", "capability" })
        if (hasLease (kind, "fixture-pre") || hasLease (kind, "fixture-post"))
            throw std::runtime_error ("controller teardown must remove its own fixture leases");
}
}
