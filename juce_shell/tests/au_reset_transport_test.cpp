#include <cstdlib>
#include <iostream>

using ComponentResult = int;
using AudioUnitScope = unsigned int;
using AudioUnitElement = unsigned int;
using Boolean = unsigned char;
constexpr int noErr = 0;
struct MusicDeviceBase
{
    virtual ~MusicDeviceBase() = default;
    virtual ComponentResult Reset (AudioUnitScope, AudioUnitElement) { return 37; }
};
struct Processor
{
    void kirinHostTimelineReset (bool knownIn, bool playingIn)
    { known = knownIn; playing = playingIn; ++notified; notifiedBeforeReset = resets == 0; }
    void reset() { ++resets; }
    int notified = 0, resets = 0;
    bool known = false, playing = false, notifiedBeforeReset = false;
};
// Compile the actual AU Reset method with a deterministic host callback. This isolates wrapper
// forwarding from the processor tests without requiring an Apple host, SDK, or plug-in bundle.
struct AudioUnit : MusicDeviceBase
{
    Processor* juceFilter = nullptr;
    bool prepared = true, hostPlaying = false;
    int result = noErr, queried = 0, preparedCalls = 0;
    void prepareToPlay() { prepared = true; ++preparedCalls; }
    int CallHostTransportState (Boolean* playing, Boolean*, double*, Boolean*, double*, double*)
    { ++queried; *playing = hostPlaying; return result; }
    #include "AuTransportResetBody.h"
};
static void require (bool ok, const char* message)
{ if (! ok) { std::cerr << "AU reset transport: " << message << '\n'; std::exit (1); } }
int main()
{
    for (bool known : { false, true }) for (bool playing : { false, true })
    {
        Processor processor;
        AudioUnit unit;
        unit.juceFilter = &processor; unit.hostPlaying = playing; unit.result = known ? noErr : -1;
        require (unit.Reset (0, 0) == 37, "base reset result is preserved");
        require (unit.queried == 1 && processor.notified == 1 && processor.resets == 1
            && processor.notifiedBeforeReset, "reset forwards separately queried transport before resetting");
        require (processor.known == known && processor.playing == playing,
                 "playing seek and unavailable callback cannot become known stopped transport");
    }
    Processor processor;
    AudioUnit unit; unit.prepared = false; unit.juceFilter = &processor;
    require (unit.Reset (0, 0) == 37 && unit.preparedCalls == 1, "existing unprepared lifecycle is preserved");
    AudioUnit empty; require (empty.Reset (0, 0) == 37 && empty.queried == 0, "missing processor is harmless");
    std::cout << "AU reset transport: PASS 6 host/lifecycle cases\n";
}
