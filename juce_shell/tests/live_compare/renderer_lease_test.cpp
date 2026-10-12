#include "../../src/local_blind/RtPublicationSlot.h"
#include "../../src/live_compare/LiveCompareRendererLease.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>

static void require (bool ok, const char* text)
{
    if (! ok) { std::cerr << "Renderer lease: " << text << '\n'; std::exit (1); }
}

// The same prepare boundary as LISTEN and direct BLIND, with a disposable in-memory mapping.
// A parked Audio Thread reader models END/close arriving during the old renderer's fade.
int main()
{
    struct Mapping { unsigned generation; };
    using Slot = hypha::local_blind::RtPublicationSlot<Mapping>;
    unsigned checked = 0;
    for (bool blind : { false, true })
        for (int repetition = 0; repetition < 32; ++repetition)
        {
            Slot slot;
            std::atomic<unsigned> rendererGeneration { 1 };
            require (slot.publish (std::make_unique<Mapping> (Mapping { 1 })), "old ring published");
            std::atomic<bool> entered { false }, release { false }, stale { false };
            std::thread audio ([&]
            {
                slot.withRealtime ([&] (Mapping& old)
                {
                    entered.store (true);
                    while (! release.load()) std::this_thread::yield();
                    stale.store (old.generation != rendererGeneration.load());
                });
            });
            while (! entered.load()) std::this_thread::yield();
            const auto collect = [] (Slot& lease) { return lease.collect(); };
            const auto prepare = [&] { ++rendererGeneration; };
            // Neither entry may resize or retire the source while the graceful fade is pending.
            require (! hypha::live_compare::prepareRendererAfterLease (slot, true, collect, prepare)
                && slot.hasPublishedRealtime() && rendererGeneration == 1,
                blind ? "BLIND waits for END fade" : "LISTEN waits for END fade");
            // Even when the last audible receipt says POST, a suspended old callback may remain.
            require (! hypha::live_compare::prepareRendererAfterLease (slot, false, collect, prepare)
                && ! slot.hasPublishedRealtime() && rendererGeneration == 1,
                "old publication retired before collection; in-flight reader prevents prepare");
            require (! hypha::live_compare::prepareRendererAfterLease (slot, false, collect, prepare)
                && rendererGeneration == 1, "repeated immediate entry cannot bypass retirement");
            release.store (true); audio.join();
            require (! stale.load(), "old callback uses only its renderer generation");
            require (hypha::live_compare::prepareRendererAfterLease (slot, false, collect, prepare)
                && ! slot.hasStorage() && rendererGeneration == 2, "prepare follows the old callback's exit");
            require (slot.publish (std::make_unique<Mapping> (Mapping { 2 })), "new generation published");
            require (slot.withRealtime ([&] (Mapping& current)
                { require (current.generation == rendererGeneration, "new callback sees its own renderer"); }),
                "normal re-entry remains available");
            ++checked;
        }
    std::cout << "Renderer lease: PASS " << checked << " repeated END/close -> LISTEN/BLIND generations\n";
}
