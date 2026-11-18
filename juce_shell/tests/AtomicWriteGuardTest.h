#pragma once
#include "../src/AtomicFile.h"
#include "../src/appearance/AppearanceStorage.h"
#include <iostream>
namespace hypha::tests {
template<class Require> void verifyAtomicWriteGuard(const juce::File& root, Require require) {
    namespace guard=storage::write_guard;
    int assertions=0, opened=0;
    const auto check=[&](bool value,const char* message){++assertions;require(value,message);};
    const auto allowed=root.getChildFile("guard-allowed");
    check(allowed.createDirectory().wasOk(),"guard fixture root");
    const auto blocked=allowed.getChildFile("blocked");
    const auto before=guard::permittedMutationEntries();
    {
        guard::ScopedFixturePolicy policy({allowed.getFullPathName(),{blocked}});
        check(!storage::writeTextWithStream(blocked.getChildFile("value"),"denied",false,false,"\n",
            [&](const juce::File& path){++opened;return std::make_unique<juce::FileOutputStream>(path);}),
            "protected atomic write rejected");
    }
    check(opened==0 && !blocked.exists(),"denial precedes stream and parent creation");
    check(guard::permittedMutationEntries()==before,"denied atomic mutation entries zero");
    const auto authority=juce::File(juce::SystemStats::getEnvironmentVariable("KIRIN_HYPHA_TEST_STORAGE_ROOT",{}));
    const auto outside=authority.getParentDirectory().getChildFile("guard-outside-never-created");
    {
        // Even a broader fixture override cannot expand the fixed startup authority.
        guard::ScopedFixturePolicy policy({authority.getParentDirectory().getFullPathName(),{}});
        check(!guard::permitMutation(outside),"fixture override cannot widen startup root");
    }
    check(!guard::enable(authority.getParentDirectory().getFullPathName()),"cannot reinitialize wider root");
    check(!guard::permitMutation(outside),"failed widening leaves original authority");
    check(storage::writeText(allowed.getChildFile("ok"),"same bytes"),"guarded isolated publication succeeds");
    check(allowed.getChildFile("ok").loadFileAsString()=="same bytes","guard preserves bytes");
    // These non-inline methods are linked from KirinHyphaPOST shared-code objects,
    // not compiled in this test TU. They must see this TU's policy and counter.
    const auto sharedRoot=allowed.getChildFile("shared-code");
    appearance::Storage sharedStorage(sharedRoot);
    {
        guard::ScopedFixturePolicy policy({{}, {}});
        const auto entries=guard::permittedMutationEntries();
        check(sharedStorage.updatePreference(std::nullopt,std::nullopt,true).state==appearance::TransactionState::ioFailure,
              "shared-code writer sees test TU denial");
        check(!sharedStorage.preferenceFile().getParentDirectory().exists()
              && guard::permittedMutationEntries()==entries,"shared-code denial performs no mutation");
    }
    const auto entries=guard::permittedMutationEntries();
    appearance::Activation activation;
    activation.id="123e4567-e89b-42d3-a456-426614174000";
    const auto saved=sharedStorage.updatePreference(activation,std::nullopt,true);
    check(saved.state==appearance::TransactionState::persisted,
          "shared-code guarded save succeeds");
    check(guard::permittedMutationEntries()>entries,"shared-code writer increments same TU counter");
    const auto primary=sharedStorage.preferenceFile().loadFileAsString();
    check(saved.backupUpdated && primary.isNotEmpty()
          && primary==sharedStorage.preferenceBackupFile().loadFileAsString(),
          "shared-code primary and backup bytes match");
    const auto decoded=appearance::decodePreference(primary);
    check(decoded.state==appearance::DecodeState::valid && decoded.value.has_value()
          && decoded.value->activationSeen && decoded.value->firstActivationId==activation.id
          && decoded.value->noticeAcknowledged,"shared-code persisted activation is valid");
    std::cout << "ATOMIC_GUARD_ASSERTIONS=" << assertions << '\n';
}
}
