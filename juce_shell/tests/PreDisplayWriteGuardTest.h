#pragma once
#include "StorageWriteGuardStartup.h"
#include "ShippingDisabledGuardTest.h"
#include "../src/AtomicFile.h"
#include "TestTemporaryDirectory.h"
#include "TestPathBudget.h"
#include "../src/pre_display/PreDisplayPresence.h"
#include <optional>
#include <iostream>
namespace hypha::tests {
template<class Require> void verifyPreDisplayWrites(const juce::File& root, Require require, bool guarded) {
    namespace pd=pre_display;
    namespace guard=storage::write_guard;
    int assertions=0;
    const auto check=[&](bool ok,const char* message){++assertions;require(ok,message);};
    pd::RuntimeIdentity identity;
    identity.instanceId="guard-instance"; identity.projectUuid="guard-project";
    identity.hostProcessId=1;
    pd::ClockSnapshot clock; clock.sampleRate=48000.0;
    const auto destination=root.getChildFile("publish");
    if(guarded) {
        guard::ScopedFixturePolicy deny({{}, {}});
        const auto entries=guard::permittedMutationEntries();
        check(!pd::writePresence(destination,identity,clock,1000,1000),"denied presence publication");
        check(!pd::writeCapability(destination,identity,1000),"denied capability publication");
        check(!destination.exists() && entries==guard::permittedMutationEntries(),
              "rejected publications do not mkdir or enter mutation");
    }
    check(pd::writePresence(destination,identity,clock,1000,1000),"isolated presence publication");
    check(pd::writeCapability(destination,identity,1000),"isolated capability publication");
    const auto presence=destination.getChildFile("presence/guard-instance.json");
    const auto capability=destination.getChildFile("capability/guard-instance.json");
    const auto before=presence.loadFileAsString();
    check(before.endsWithChar('\n') && !before.contains("\r"),"publication retains LF bytes");
    check(static_cast<juce::int64>(juce::JSON::parse(before).getProperty("lease_expires_at_ms",{}))==2500,
          "presence lease remains 1500ms");
    check(pd::writePresence(destination,identity,clock,1100,1100),"isolated presence update");
    check(static_cast<juce::int64>(juce::JSON::parse(presence).getProperty("lease_expires_at_ms",{}))==2600,
          "updated presence lease remains 1500ms");
    const auto ack=destination.getChildFile("ack/guard-instance.json");
    check(ack.getParentDirectory().createDirectory().wasOk() && storage::writeText(ack,"fixture-ack"),
          "seed isolated acknowledgement deletion control");
    const auto current=presence.loadFileAsString(), cap=capability.loadFileAsString();
    if(guarded) {
        guard::ScopedFixturePolicy deny({{}, {}});
        const auto entries=guard::permittedMutationEntries();
        pd::removePresence(presence); pd::removeAcknowledgement(ack); pd::removeCapability(capability);
        check(presence.loadFileAsString()==current && capability.loadFileAsString()==cap
              && ack.loadFileAsString()=="fixture-ack","revoked lease cleanup preserves all bytes");
        check(entries==guard::permittedMutationEntries(),"revoked three-delete mutation entries zero");
    }
    pd::removePresence(presence); pd::removeAcknowledgement(ack); pd::removeCapability(capability);
    check(!presence.exists() && !ack.exists() && !capability.exists(),"authorized lease cleanup");
    std::cout << "PRE_DISPLAY_GUARD_ASSERTIONS=" << assertions << " enabled=" << guarded << '\n';
}
template<class Require> std::optional<int> preDisplayGuardStartup(int argc,char** argv,Require require) {
    const auto mode=argc==2 ? juce::String(argv[1]) : juce::String();
    const auto configured=juce::SystemStats::getEnvironmentVariable("KIRIN_HYPHA_TEST_STORAGE_ROOT",{});
    if(mode=="--guard-shipping-disabled") {
        if(!storage::write_guard::validRoot(configured)) return 78;
        const auto root=shippingDisabledFixture(require).getNonexistentChildFile("pdg",{},false);
        const auto before=storage::write_guard::permittedMutationEntries();
        require(root.createDirectory().wasOk(),"disabled guard fixture root");
        verifyPreDisplayWrites(root,require,false);
        require(before==storage::write_guard::permittedMutationEntries(),"disabled PRE writes do not enter guarded mutations");
        require(removeFixtureDirectory(root),"disabled guard fixture cleanup"); return 0;
    }
    if(!initializeStorageWriteGuard()) return 78;
    if(mode=="--guard-startup-only") return 0;
    return std::nullopt;
}
}
