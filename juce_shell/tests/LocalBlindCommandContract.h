#pragma once

static void localBlindCommandContract()
{
    // Same encoded kinds as LocalBlindTrial: 1/2 = sources, 3 = STOP, 4 = RETURN, 5/6 = named.
    for (std::uint64_t laterKind : { 1, 2, 3, 4, 5, 6 })
    {
        TrialCommandState command;
        require (command.issue (1), "arm source one");
        const auto rendered = command.load();
        require (command.issue (laterKind), "newer control intent");
        const auto later = command.load();
        require (! command.advanceRendered (rendered, 2) && command.load() == later,
                 "late RT completion cannot overwrite selection, STOP, or RETURN");
        require (command.issue (3) && command.load() > later,
                 "control sequence stays monotonic after rejected RT advancement");
    }
    TrialCommandState command;
    require (command.issue (1), "arm automatic advance");
    const auto rendered = command.load();
    require (command.advanceRendered (rendered, 2) && (command.load() & 7u) == 2,
             "unchanged rendered intent may advance once");
    const auto advanced = command.load();
    require (! command.advanceRendered (rendered, 2) && command.load() == advanced,
             "one completion cannot be replayed");
    require (command.issue (3) && command.issue (4), "STOP then RETURN after RT advancement");
    require ((command.load() & 7u) == 4 && command.load() > advanced,
             "explicit RETURN remains the final intent");
}
