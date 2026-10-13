#include "ReaperFixture.h"
#include "../ProductTestClock.h"
#include "../../src/live_compare/ReaperChainReader.h"
#include <string>
#include <thread>
using namespace reaper_fixture;
namespace
{
int trackToken, projectToken;
struct Fake
{
    bool playing = true, valid = true, duplicate = false, fail = false, malformed = false;
    bool container = false, off = false, external = false, changing = false, closedIdentity = false;
    int input = 0, parallel = 0, mode = 1, oversample = 0, revision = 1, channels = 2;
    std::uint32_t delay = 4096;
    std::array<std::array<unsigned char, 16>, 4> guid {};
    Fake() { for (unsigned i = 0; i < guid.size(); ++i) guid[i][0] = static_cast<unsigned char> (i + 1); }
} fake;
bool named (void*, int slot, const char* key, char* out, int size)
{
    const std::string name (key); std::string value;
    if (fake.fail && name == "chain_pdc_actual") return false;
    if (name == "container_count" || name == "parent_container")
    { if (!fake.container) return false; value = "0"; }
    else if (name == "fx_type") value = "VST3";
    else if (name == "original_name") value = fake.external ? "ReaInsert (Cockos)" : "Test effect";
    // REAPER 7.71 reports "<path><id>{<class id>" with no closing brace; older forms close it.
    else if (name == "fx_ident") value = slot == 0 || (slot == 1 && fake.duplicate)
        ? (fake.closedIdentity ? "arbitrary-name.vst3<1{4B6972696E4879706861505245763031}>"
                               : "/plug-ins/arbitrary-name.vst3<1618689294{4B6972696E4879706861505245763031")
        : slot == 2 ? (fake.closedIdentity ? "other-name.vst3<2{4B6972696E4879706861504F53547631}>"
                                           : "/plug-ins/other-name.vst3<650474387{4B6972696E4879706861504F53547631")
                    : "delay.vst3";
    else if (name == "chain_pdc_actual") value = fake.malformed ? "4294967296" : std::to_string (fake.delay);
    else if (name == "chain_pdc_reporting" || name == "pdc") value = std::to_string (fake.delay);
    else if (name == "chain_pdc_mode") value = std::to_string (fake.mode);
    else if (name == "parallel") value = std::to_string (fake.parallel);
    else if (name == "instance_oversample_shift" || name == "chain_oversample_shift") value = std::to_string (fake.oversample);
    else return false;
    std::snprintf (out, static_cast<std::size_t> (size), "%s", value.c_str()); return true;
}
ReaperChainApi api()
{
    return {
        [] (void*, void*, const char*) { return fake.valid; }, [] (void*) { return 3; },
        [] (void*) { return fake.input; }, named,
        [] (void*, int slot) -> const void* { return fake.guid[static_cast<std::size_t> (slot)].data(); },
        [] (void*) -> const void* { return fake.guid[3].data(); },
        [] (void*, int) { return !fake.off; }, [] (void*, int) { return false; },
        [] (void*) { return fake.playing ? 1 : 0; }, [] (void*) { return fake.changing ? ++fake.revision : fake.revision; },
        [] (void*, const char* key) { return std::string (key) == "I_NCHAN" ? static_cast<double> (fake.channels) : 0.0; },
        [] (void*) { return 1.0; }
    };
}
auto read() { return readReaperChain (api(), &projectToken, &trackToken, nullptr, 987); }
void reader()
{
    require (read().valid, "main chain including slot zero binds by class/GUID, not display name");
    fake.closedIdentity = true; require (read().valid, "a closing brace after the class id is also accepted"); fake = {};
    for (const char* other : { "/x.vst3<1{4B6972696E4879706861505245763031AB", "/4B6972696E4879706861505245763031.vst3<1{00",
                               "/x.vst3<1{4B6972696E4879706861505245763031>", "4B6972696E4879706861505245763031" })
        require (!reaperIdentityNamesClass (other, "4B6972696E4879706861505245763031"),
                 "only an exact class id after '{' and before the end or '}' names a role");
    require (reaperIdentityNamesClass ("/p/Kirin Hypha PRE.vst3<1618689294{4B6972696E4879706861505245763031",
                                       "4B6972696E4879706861505245763031"), "REAPER's measured identity names PRE");
    const auto original = read();
    for (auto reason : { ReaperChainRestriction::channels, ReaperChainRestriction::inputFx,
                         ReaperChainRestriction::pdcMode, ReaperChainRestriction::routing })
    {
        if (reason == ReaperChainRestriction::channels) fake.channels = 4;
        if (reason == ReaperChainRestriction::inputFx) fake.input = 1;
        if (reason == ReaperChainRestriction::pdcMode) fake.mode = 0;
        if (reason == ReaperChainRestriction::routing) fake.parallel = 1;
        ReaperChainRestriction actual {};
        require (!readReaperChain (api(), &projectToken, &trackToken, nullptr, 987, &actual).valid
                 && actual == reason && *reaperRestrictionText (actual), "unsupported tracks have a concrete non-RT reason");
        fake = {};
    }
    for (bool enabled : { false, true }) for (bool interested : { false, true }) for (bool playing : { false, true })
        require (reaperMonitorRequired (enabled, interested, playing) == (enabled && interested && playing),
                 "only a playing instance needed for preparation or audition polls the host API");
    ++fake.revision; require (read().same (original), "unrelated project edits do not change the chain fingerprint"); fake = {};
    fake.changing = true; require (!read().valid, "an edit during one read rejects that observation"); fake = {};
    fake.playing = false; require (!read().valid, "actual success before playback is rejected"); fake = {};
    fake.duplicate = true; require (!read().valid, "duplicate role rejected"); fake = {};
    fake.valid = false; require (!read().valid, "stale parent rejected"); fake = {};
    fake.input = 1; require (!read().valid, "input chain cannot impersonate main slot"); fake = {};
    fake.fail = true; require (!read().valid, "missing API is not zero"); fake = {};
    fake.malformed = true; require (!read().valid, "overflow rejected"); fake = {};
    fake.container = true; require (!read().valid, "container rejected"); fake = {};
    fake.external = true; require (!read().valid, "external hardware insert rejected independently of label"); fake = {};
    fake.parallel = 1; require (!read().valid, "parallel rejected"); fake = {};
    fake.mode = 2; require (!read().valid, "PDC ignore rejected"); fake = {};
    fake.oversample = 1; require (!read().valid, "oversampling rejected"); fake = {};
    fake.off = true; require (!read().valid, "bypassed endpoint rejected"); fake = {};
    std::swap (fake.guid[0], fake.guid[1]); require (!read().same (original), "GUID order changes proof"); fake = {};
    fake.delay = 0; require (read().valid && !read().same (original), "valid zero and changed delay"); fake = {};
    require (!readReaperChain (api(), &projectToken, &trackToken, &trackToken, 987).valid, "take refused");
    auto incomplete = api(); incomplete.named = nullptr;
    require (!readReaperChain (incomplete, &projectToken, &trackToken, nullptr, 987).valid, "missing interface API");
}
void mailbox()
{
    hypha::tests::ProductTestClock time;
    ReaperPdcMailbox box;
    Host host;
    const auto reading = read();
    BlockClock b;
    auto callback = [&] (bool play = true, bool advance = true)
    {
        host.now = time.nanos(); b = host.block (host.postCounter, host.postPosition);
        b.playing = play; host.postPosition += frames;
        box.observe (b, rate, true); if (advance) time.advanceBlock (frames, rate);
    };
    auto poll = [&] (bool play = true)
    {
        ReaperCallbackWitness witness;
        require (box.witness (witness), "callback witness is coherent");
        box.publish (reading, witness, witness, play, time.nanos());
    };
    callback (false); poll (false); callback (false);
    require (b.runtimeChainA == 0, "preplay API value never reaches RT");
    callback(); poll(); callback(); require (b.runtimeChainA == 0, "single postplay read is insufficient");
    poll(); callback(); require (b.runtimeChainA == reading.chainA, "progressing postplay bound arrives on next callback");
    const auto revision = b.runtimeRevision;
    poll(); poll(); callback();
    require (b.runtimeChainA != 0, "repeat timer tick without another callback retains original expiry");
    for (int i = 0; i < 128; ++i) callback (true, false);
    require (b.runtimeChainA != 0, "128 callback burst does not fabricate elapsed wall time");
    poll(); callback(); require (b.runtimeRevision == revision, "identical polls do not change proof revision");
    time.advance (std::chrono::milliseconds (501)); callback();
    require (b.runtimeChainA == reading.chainA && b.runtimeExpired && b.runtimeRevision == revision, "expiry keeps identity but withholds permission");
    poll(); callback(); require (!b.runtimeExpired && b.runtimeRevision == revision, "same proof after expiry retains revision");
    box.invalidate(); callback(); require (b.runtimeChainA == 0, "prepare/host epoch fences old token");
    poll(); callback(); poll(); callback(); require (b.runtimeChainA != 0, "fresh epoch can reacquire evidence only");
    auto changed = reading; changed.delay += 4096; changed.chainA ^= 8;
    const auto changedPoll = [&] {
        ReaperCallbackWitness witness; require (box.witness (witness), "changed callback witness");
        box.publish (changed, witness, witness, true, time.nanos());
    };
    changedPoll(); callback(); require (b.runtimeChainA == 0, "new API delay first revokes the old certificate");
    changedPoll(); callback();
    require (b.maximumDelaySamples == changed.delay && b.runtimeRevision != revision,
             "fresh changed bound is a different proof, never appended to old K");
    for (int i = 0; i < 400; ++i) callback (true, false);
    require (b.runtimeExpired, "processed-sample expiry also bounds an arbitrarily fast burst");
    callback (false); callback(); require (b.runtimeChainA == 0, "stop/play cannot reuse token");
}
void atomicSnapshot()
{
    AtomicWords<3> words;
    std::atomic<bool> done { false };
    std::thread writer ([&] { for (std::uint64_t i = 1; i < 100000; ++i) words.store ({ i, i * 3, i * 7 }); done.store (true); });
    while (!done.load())
    {
        std::array<std::uint64_t, 3> values {};
        if (words.load (values)) require (values[1] == values[0] * 3 && values[2] == values[0] * 7, "no torn token accepted");
    }
    writer.join();
}
}
int main()
{
    reader(); mailbox(); atomicSnapshot();
    std::puts ("REAPER API negatives, chain identity, callback mailbox, expiry and atomic coherence PASS");
}
