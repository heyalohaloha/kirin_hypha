#pragma once
#include "kirin_hypha_ffi.h"
#include <utility>

// An ended A/B still draining its return keeps the project's capture exclusion on the POST's
// replacement engine. Attach consumes the hold; a null engine releases it.
extern "C"
{
typedef struct KirinHyphaLocalBlindHold KirinHyphaLocalBlindHold;
KirinHyphaLocalBlindHold* kirin_hypha_detach_local_blind (KirinHypha* handle, uint64_t scope_epoch);
bool kirin_hypha_attach_local_blind (KirinHypha* handle, KirinHyphaLocalBlindHold* hold);
typedef struct CandidateTailHold CandidateTailHold;
CandidateTailHold* kirin_hypha_candidate_tail_begin (KirinHypha* handle);
bool kirin_hypha_candidate_tail_attach (KirinHypha* handle, CandidateTailHold* hold);
void kirin_hypha_candidate_tail_end (CandidateTailHold* hold);
bool kirin_hypha_all_keep_tail_pending (KirinHypha* handle);
bool kirin_hypha_keep_all_resume (KirinHypha* handle);
bool kirin_hypha_keep_all_stage (KirinHypha* handle);
typedef struct CandidateKeepRoster CandidateKeepRoster;
CandidateKeepRoster* kirin_hypha_keep_roster_detach (KirinHypha* handle);
bool kirin_hypha_keep_roster_attach (KirinHypha* handle, CandidateKeepRoster* roster);
void kirin_hypha_keep_roster_drop (CandidateKeepRoster* roster);
}

// Non-RT, guarded by the processor's engine lock across every replacement.
class CandidateKeepRosterOwner
{
public:
    ~CandidateKeepRosterOwner() { kirin_hypha_keep_roster_drop (saved); }
    void detach (KirinHypha* engine) { if (engine && !saved) saved = kirin_hypha_keep_roster_detach (engine); }
    void attach (KirinHypha* engine) { if (engine && saved && kirin_hypha_keep_roster_attach (engine, saved)) saved = nullptr; }
private:
    CandidateKeepRoster* saved = nullptr;
};

class CandidateTailOwner
{
public:
    ~CandidateTailOwner() { clear(); }
    bool begin (KirinHypha* engine) { if (!hold) hold = kirin_hypha_candidate_tail_begin (engine); return hold != nullptr; }
    void attach (KirinHypha* engine) { if (hold && engine) kirin_hypha_candidate_tail_attach (engine, hold); }
    void clear() { if (hold) kirin_hypha_candidate_tail_end (std::exchange (hold, nullptr)); }
private:
    CandidateTailHold* hold = nullptr;
};
