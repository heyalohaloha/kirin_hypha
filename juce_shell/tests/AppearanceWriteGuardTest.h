#pragma once
#include "../src/appearance/AppearanceTestWriteGuard.h"
#include <filesystem>
#include "DisposableDirectoryAlias.h"
namespace hypha::tests
{
template<class Require> void verifyAppearanceWriteGuard (const juce::File& fixtureRoot, Require require)
{
    using namespace appearance;
    using namespace appearance::test_write_guard;
    int assertions=0;
    const auto check=[&](bool value,const char* message){++assertions;require(value,message);};
    const auto allowed=fixtureRoot.getChildFile("allowed");
    const auto protectedRoot=fixtureRoot.getChildFile("protected");
    check(allowed.createDirectory().wasOk()&&protectedRoot.createDirectory().wasOk(),"prepare isolated guard roots");
    const auto denied=[&](const juce::File& root,FixturePolicy policy){
        ScopedFixturePolicy scoped(std::move(policy));
        const auto before=permittedMutationEntries();
        Storage storage(root);
        const auto result=storage.updatePreference(std::nullopt,std::nullopt,true);
        check(result.state==TransactionState::ioFailure,"denied preference reports existing IO failure");
        FileLock lock(root.getChildFile("blocked.lock"));
        check(!lock.tryAcquire(),"denied lock not acquired");
        check(permittedMutationEntries()==before,"zero permitted mutation entries for denied write and lock");
        check(!storage.preferenceFile().getParentDirectory().exists(),"denied write did not create parent");
        check(!root.getChildFile("blocked.lock").exists(),"denied lock directory absent");
    };
    denied(protectedRoot,{allowed.getFullPathName(),{protectedRoot}});
    denied(fixtureRoot.getChildFile("allowed-other"),{allowed.getFullPathName(),{protectedRoot}});
    denied(allowed.getChildFile("unset"),{{},{protectedRoot}});
    denied(allowed.getChildFile("relative"),{"relative",{protectedRoot}});
    denied(allowed.getChildFile("missing"),{fixtureRoot.getChildFile("missing-root").getFullPathName(),{protectedRoot}});
    denied(protectedRoot,{protectedRoot.getFullPathName(),{protectedRoot}});
    denied(protectedRoot,{fixtureRoot.getFullPathName(),{protectedRoot}});
#if JUCE_MAC || JUCE_WINDOWS
    // Separate roots on case-sensitive volumes; aliases on insensitive volumes.
    // Either way the test-only conservative policy must reject both spellings.
    const auto caseProtected=fixtureRoot.getChildFile("CaseProtected").getChildFile("Kirin OS");
    const auto caseAllowed=fixtureRoot.getChildFile("caseprotected").getChildFile("kirin os");
    check(caseProtected.createDirectory().wasOk()&&caseAllowed.createDirectory().wasOk(),
          "prepare isolated case-alias roots");
    denied(caseAllowed,{caseAllowed.getFullPathName(),{caseProtected}});
    // Missing protected suffix must not evade the same conservative comparison.
    const auto missingProtected=fixtureRoot.getChildFile("MISSINGCASE").getChildFile("Kirin OS");
    const auto missingAllowed=fixtureRoot.getChildFile("missingcase");
    check(missingAllowed.createDirectory().wasOk(),"prepare missing case suffix fixture");
    denied(missingAllowed.getChildFile("kirin os"),{missingAllowed.getFullPathName(),{missingProtected}});
#endif
    {
        ScopedFixturePolicy scoped({allowed.getFullPathName(),{protectedRoot}});
        const auto live=allowed.getChildFile("normal");
        Storage storage(live);
        Activation activation; activation.id="123e4567-e89b-42d3-a456-426614174000";
        check(storage.updatePreference(activation,std::nullopt,true).state==TransactionState::persisted,
              "normal isolated preference still persists");
        check(storage.preferenceFile().existsAsFile()&&storage.preferenceBackupFile().existsAsFile(),
              "normal primary and backup exist");
        const auto lockPath=live.getChildFile("normal.lock");
        { FileLock lock(lockPath);check(lock.tryAcquire(),"normal lock acquired"); }
        check(!lockPath.exists(),"normal lock released");
    }
    const auto stale=protectedRoot.getChildFile("stale.lock");
    check(stale.createDirectory().wasOk(),"fixture creates synthetic protected stale lock");
    check(stale.setLastModificationTime(juce::Time::getCurrentTime()-juce::RelativeTime::seconds(60)),"fixture dates stale lock");
    {
        ScopedFixturePolicy scoped({allowed.getFullPathName(),{protectedRoot}});
        const auto before=permittedMutationEntries();
        FileLock lock(stale);
        check(!lock.tryAcquire()&&stale.isDirectory(),"protected stale lock not removed or acquired");
        check(permittedMutationEntries()==before,"stale retry has zero permitted mutations");
    }
    {
        ScopedFixturePolicy scoped({allowed.getFullPathName(),{protectedRoot}});
        const auto path=allowed.getChildFile("revoke.lock");
        auto lock=std::make_unique<FileLock>(path);
        check(lock->tryAcquire(),"fixture acquires lock before revocation");
        ScopedFixturePolicy revoked({{}, {protectedRoot}});
        const auto before=permittedMutationEntries(); lock.reset();
        check(path.isDirectory()&&permittedMutationEntries()==before,"destructor refuses mutation after policy revocation");
    }
    const auto outside=fixtureRoot.getChildFile("outside");
    const auto inner=allowed.getChildFile("inner");
    check(outside.createDirectory().wasOk() && inner.createDirectory().wasOk(),"prepare disposable junction endpoints");
    const auto sentinel=outside.getChildFile("keep");
    check(sentinel.replaceWithText("untouched"),"seed external fixture sentinel");
    const auto outward=allowed.getChildFile("escape");
    const auto inward=outside.getChildFile("enter");
    const auto internal=allowed.getChildFile("inner-link");
    check(createDisposableDirectoryAlias(outside,outward),"create outward directory alias");
    check(createDisposableDirectoryAlias(inner,inward),"create inward directory alias");
    check(createDisposableDirectoryAlias(inner,internal),"create child directory alias");
    denied(outward.getChildFile("child"),{allowed.getFullPathName(),{protectedRoot}});
    denied(inward.getChildFile("child"),{allowed.getFullPathName(),{protectedRoot}});
    denied(internal.getChildFile("child"),{allowed.getFullPathName(),{protectedRoot}});
    {
        ScopedFixturePolicy scoped({allowed.getFullPathName(),{protectedRoot}});
        Storage storage(inner.getChildFile("direct"));
        check(storage.updatePreference(std::nullopt,std::nullopt,true).state==TransactionState::persisted,
              "ordinary inner path still permits writes");
    }
    check(removeDisposableDirectoryAlias(outward) && removeDisposableDirectoryAlias(inward)
          && removeDisposableDirectoryAlias(internal),"remove aliases without traversing targets");
    check(sentinel.loadFileAsString()=="untouched" && inner.isDirectory(),"alias cleanup preserves target contents");
#if JUCE_WINDOWS
    // Emulate relocated KnownFolders through the existing narrowing-only fixture policy.
    // SHSetKnownFolderPath/registry edits are intentionally never used.
    for(const auto* name : {"moved-documents","moved-local","moved-roaming"}) {
        const auto moved=fixtureRoot.getChildFile(name).getChildFile("Kirin OS");
        check(moved.createDirectory().wasOk(),"prepare relocated KnownFolder-equivalent fixture");
        denied(moved,{fixtureRoot.getFullPathName(),{moved}});
        ScopedFixturePolicy scoped({allowed.getFullPathName(),{moved}});
        Storage storage(allowed.getChildFile(name));
        check(storage.updatePreference(std::nullopt,std::nullopt,true).state==TransactionState::persisted,
              "relocated protected fixture leaves unrelated allowed root writable");
    }
    std::cout << "APPEARANCE_GUARD_REPARSE_FIXTURE=PASS KNOWNFOLDERS_EQUIVALENT=PASS\n";
#endif
    std::cout << "APPEARANCE_GUARD_ASSERTIONS=" << assertions << '\n';
}
}
