#include "../../src/live_compare/LiveCompareAuthority.h"
#include "../../src/live_compare/LiveCompareCompletion.h"
#include <cstdlib>
#include <iostream>

using namespace hypha::live_compare;
static void require (bool ok, const char* message)
{
    if (! ok) { std::cerr << message << '\n'; std::abort(); }
}

int main()
{
    Authority authority;
    Completion completion;
    const auto old = authority.ticket();
    require (authority.arm (old), "explicit initial session arms");
    const auto end = completion.request();
    {
        const auto restore = authority.restoringState();
        require (! authority.permitted() && authority.restoring(), "restore revokes before parsing");
        require (! authority.arm (old) && ! authority.arm (authority.ticket()), "no admission during restore");
        {
            const auto nested = authority.restoringState();
            require (authority.generation() == 2, "overlapping restores have distinct generations");
        }
        require (authority.restoring() && ! authority.permitted(), "inner restore cannot release outer fence");
    }
    require (! authority.restoring() && ! authority.permitted(), "restore completion never resumes output");
    require (! authority.arm (old), "delayed old start cannot arm");
    require (completion.pending() && completion.command() == end, "restore preserves explicit END");
    completion.observe (end, false, false, 1.0f);
    require (completion.pending(), "an ineligible callback still cannot complete END");
    completion.observe (end, true, false, 1.0f);
    require (! completion.pending(), "normal RT completion still works after restore");
    require (authority.arm (authority.ticket()) && authority.permitted(), "new explicit session may arm");

    int cases = 0;
    for (bool active : { false, true })
        for (bool reuse : { false, true })
            for (bool restoring : { false, true })
                for (bool finishing : { false, true })
                    for (bool owned : { false, true })
                        for (float actual : { 1.0f, 0.1f })
                            for (float target : { 1.0f, 0.1f })
                            {
                                const auto result = entryAdmission (reuse, active, restoring, finishing, owned, actual, target);
                                const auto expected = restoring ? StartResult::notReady
                                    : finishing ? StartResult::returnPending
                                    : ((! reuse || ! active) && (actual != 1.0f || target != 1.0f)) ? StartResult::returnRequired
                                    : owned ? StartResult::comparisonBusy : StartResult::started;
                                require (result == expected, "admission matrix disagrees");
                                ++cases;
                            }
    std::cout << "Live compare authority: PASS restore/END and " << cases << " admission cases\n";
}
