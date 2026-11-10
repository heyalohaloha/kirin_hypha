#pragma once

// A BLIND trial never lets POST stand in for a raised PRE at the ceiling. PRE is back at unity
// before its first sample over the ceiling (a step only when that sample opens the block) and POST
// takes the same attenuation, so both sources stay matched; LISTEN keeps the POST fallback.
template <class PairType> void blindMatchesOnTheQuieterSideAtTheCeiling (float postValue, int frames)
{
    using namespace hypha::live_compare;
    const float quiet[] { 0.1f, postValue };
    for (const int spike : { 300, 0 })
    {
        PairType pair;
        pair.ceiling = 0.5f;
        pair.lowerOnCeiling = true;
        pair.calibrate (true, 1.0f);
        for (int i = 0; i < 8; ++i) pair.step (true, true, 2.0f, false, 2, false, true, false, quiet);
        require (pair.post[0][0] == 0.2f, "PRE sounds raised while every sample stays under the ceiling");
        pair.spikeAt = spike; pair.spikeValue = 0.4f; // raised to 0.8, over 0.5
        const auto r = pair.step (true, true, 2.0f, false, 2, false, true, false, quiet);
        require (r.levelShift == 0.5f && ! r.guardTripped && ! r.preWaiting && r.preAudible
                 && r.reason == RecoveryReason::none, "the trial moves to the quieter side instead of playing POST");
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < frames; ++i)
                require (std::fabs (pair.post[c][size_t (i)]) <= 0.5f, "no output sample passes the ceiling");
        require (pair.post[0][size_t (spike)] == 0.4f && pair.post[0][size_t (spike) + 1] == 0.1f,
                 "PRE is at unity from its first sample over the ceiling");
        require (pair.level.value() == 0.5f, "a silent POST takes the lowered level at once, so an immediate switch is matched");
        if (spike > 0)
        {
            require (pair.post[0][0] > 0.199f && pair.post[0][0] < 0.2f, "the gain leaves 2 without a step");
            for (int i = 1; i < spike; ++i)
                require (pair.post[0][size_t (i)] <= pair.post[0][size_t (i - 1)]
                         && pair.post[0][size_t (i - 1)] - pair.post[0][size_t (i)] < 0.001f,
                         "the gain glides down to unity before that sample");
        }
        // The processor then passes unity PRE and POST at the same ratio for the rest of the trial.
        pair.spikeAt = -1;
        pair.postTarget = 0.5f;
        const auto next = pair.step (true, true, 1.0f, false, 2, false, true, false, quiet);
        require (next.levelShift == 1.0f && ! next.guardTripped && next.preAudible && next.stableSource
                 && pair.post[0][0] == 0.1f, "later blocks keep PRE at unity without another switch");
    }
    {
        PairType pair; // SOURCE on POST: PRE is silent, POST takes the same attenuation over 50 ms
        pair.ceiling = 0.5f;
        pair.lowerOnCeiling = true;
        pair.calibrate (false, 1.0f);
        for (int i = 0; i < 4; ++i) pair.step (true, false, 2.0f, false, 2, false, true, false, quiet);
        require (pair.postUntouched(), "POST alone is bit-identical before the switch");
        pair.spikeAt = 200; pair.spikeValue = 0.4f;
        const auto r = pair.step (true, false, 2.0f, false, 2, false, true, false, quiet);
        require (r.levelShift == 0.5f && ! r.guardTripped && ! r.preAudible, "a silent PRE switches the trial too");
        require (pair.post[0][0] < 0.0f && pair.post[0][0] > postValue
                 && pair.post[0][size_t (frames - 1)] > pair.post[0][0], "POST glides toward the lower level");
    }
    {
        PairType pair; // a ceiling episode latched in LISTEN does not hold a BLIND trial on POST
        pair.ceiling = 0.5f;
        pair.calibrate (true, 1.0f);
        for (int i = 0; i < 8; ++i) pair.step (true, true, 2.0f, false, 2, false, false, false, quiet);
        pair.spikeAt = 300; pair.spikeValue = 0.4f;
        require (pair.step (true, true, 2.0f, false, 2, false, false, false, quiet).guardTripped, "LISTEN latches the ceiling");
        pair.spikeAt = -1; pair.lowerOnCeiling = true; pair.postTarget = 0.5f;
        const auto r = pair.step (true, true, 1.0f, false, 2, false, true, false, quiet);
        require (! r.guardTripped && r.reason == RecoveryReason::none, "BLIND at the quieter side is not held by that episode");
    }
    {
        PairType pair; // LISTEN: no BLIND trial, the ceiling still plays POST from the block start
        pair.ceiling = 0.5f;
        pair.calibrate (true, 1.0f);
        for (int i = 0; i < 8; ++i) pair.step (true, true, 2.0f, false, 2, false, false, false, quiet);
        pair.spikeAt = 300; pair.spikeValue = 0.4f;
        const auto r = pair.step (true, true, 2.0f, false, 2, false, false, false, quiet);
        require (r.guardTripped && r.levelShift == 1.0f && pair.postUntouched(), "LISTEN keeps its POST fallback");
    }
}
