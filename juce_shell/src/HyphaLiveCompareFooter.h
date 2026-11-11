#pragma once
namespace hypha::observatory
{
// The live PRE / POST compare as the footer shows it (INV-LC4). PRE can be selected while POST
// still sounds, until the correspondence at this position is proven: the PRE control says WAIT.
struct LiveCompareFooter
{
    bool entryEnabled = false; // this POST can open a session (host format, platform, layout)
    bool entrySupported = false; // this POST offers LISTEN and A/B at all (INV-LC9); greyed when not enabled
    const char* entryRefusal = ""; // why LISTEN and A/B cannot open now
    const char* blindRefusal = ""; // why BLIND cannot open now
    bool active = false, candidate = false;
    bool finishing = false, blindAvailable = false;
    bool preSelected = false;
    bool preWaiting = false;
    bool matched = false, matchHeld = false; // fixed gain exists; held needs reconfirmation
    bool matchLimited = false; // that MATCH stopped at the true-peak ceiling: PRE is still quieter
    int preGainTenthsDb = 0;   // that gain in 0.1 dB steps, as the PRE control shows it
    int postHeldTenthsDb = 0;  // approved POST attenuation (0 or below), held until RETURN
    bool contentHeld = false;  // INV-LC10: POST until playback stops and restarts
    bool following = false;    // INV-LC16: AUTO keeps PRE's gain on POST's loudness
    bool compensationOff = false; // INV-LC8: the host's delay compensation is off; PRE waits
    const char* recoveryHelp = "";
    bool operator== (const LiveCompareFooter& other) const noexcept
    {
        return candidate == other.candidate && finishing == other.finishing && blindAvailable == other.blindAvailable
            && entryEnabled == other.entryEnabled && entrySupported == other.entrySupported
            && entryRefusal == other.entryRefusal && blindRefusal == other.blindRefusal && active == other.active
            && preSelected == other.preSelected && preWaiting == other.preWaiting
            && matched == other.matched && matchHeld == other.matchHeld && matchLimited == other.matchLimited
            && preGainTenthsDb == other.preGainTenthsDb && postHeldTenthsDb == other.postHeldTenthsDb
            && contentHeld == other.contentHeld && following == other.following
            && compensationOff == other.compensationOff && recoveryHelp == other.recoveryHelp;
    }
};

}
