#pragma once
#include "ChainTraceJson.h"
#include "../live_compare/LiveCompareChainTiming.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <random>
#include <string>
#include <thread>

namespace hypha::chain_trace
{
// Created once on the control thread, only with an explicit existing absolute directory.
// No defaults, OS data folders, settings, audio source generation, or alignment authority.
class Recorder
{
public:
    static std::unique_ptr<Recorder> fromEnvironment() noexcept
    {
        const auto* path = std::getenv ("KIRIN_HYPHA_CHAIN_TRACE_DIR");
        if (path == nullptr || *path == '\0') return {};
        try
        {
            const std::filesystem::path folder (path);
            if (! folder.is_absolute() || ! std::filesystem::is_directory (folder)) return {};
            return std::unique_ptr<Recorder> (new Recorder (std::filesystem::canonical (folder)));
        }
        catch (...) { return {}; } // Diagnostics must never prevent loading the processor.
    }
    ~Recorder()
    {
        stopping.store (true, std::memory_order_release);
        if (worker.joinable()) worker.join();
    }
    Recorder (const Recorder&) = delete;
    Recorder& operator= (const Recorder&) = delete;

    Row* begin (const float* const* input, int channels, int frames) noexcept
    {
        ++serial;
        const auto start = localFrames;
        if (frames > 0) localFrames += static_cast<std::uint64_t> (frames);
        current = nullptr;
        if (failed.load (std::memory_order_acquire)) { dropped.fetch_add (1); return nullptr; }
        current = queue.begin (frames, channels);
        if (current == nullptr) { dropped.fetch_add (1, std::memory_order_relaxed); return nullptr; }
        auto& r = *current;
        r.serial = serial; r.localFrames = start; r.frames = frames; r.channels = channels;
        r.entryNanos = live_compare::callbackWallNanos();
        r.blockSeen = r.timingSeen = r.renderSeen = r.feed = r.outputTaken = r.wallGap = false;
        r.block = {}; r.peer = {}; r.timing = {}; r.decision = {}; r.render = {};
        for (int c = 0; c < channels; ++c)
            std::memcpy (r.pcm[static_cast<std::size_t> (c)].data(), input[c], sizeof (float) * static_cast<std::size_t> (frames));
        return current;
    }
    void finish (const float* const* output) noexcept
    {
        if (current == nullptr) return;
        for (int c = 0; c < current->channels; ++c)
            std::memcpy (current->pcm[static_cast<std::size_t> (c + 2)].data(), output[c],
                         sizeof (float) * static_cast<std::size_t> (current->frames));
        current->exitNanos = live_compare::callbackWallNanos();
        current = nullptr;
        queue.publish();
    }
    Row* row() noexcept { return current; } // Audio Thread only, scoped to one callback.

private:
    explicit Recorder (const std::filesystem::path& folder)
    {
        // Atomic directory creation reserves a new run even in another host process. Files are
        // created only inside that reservation; an old trace can never be truncated or reused.
        std::random_device random;
        const auto name = "chain-" + std::to_string (live_compare::callbackWallNanos())
            + "-" + std::to_string (random());
        const auto directory = folder / name;
        if (! std::filesystem::create_directory (directory)) throw std::runtime_error ("trace reservation failed");
        meta.open (directory / "blocks.jsonl", std::ios::out | std::ios::binary);
        pcm.open (directory / "audio.f32", std::ios::out | std::ios::binary);
        if (! meta || ! pcm) throw std::runtime_error ("trace open failed");
        const std::uint16_t endian = 1;
        meta.imbue (std::locale::classic());
        meta << std::boolalpha << std::setprecision (17);
        meta << "{\"type\":\"header\",\"schema\":1,\"pcm\":\""
             << (*reinterpret_cast<const unsigned char*> (&endian) == 1 ? "f32le" : "f32be")
             << "\",\"maximumFrames\":" << maximumFrames << ",\"queueBlocks\":" << queueBlocks << "}\n";
        meta.flush();
        worker = std::thread ([this] { drain(); });
    }
    void drain() noexcept
    {
        try
        {
            std::uint64_t offset = 0, written = 0;
            for (;;)
            {
                while (const auto* r = queue.front())
                {
                    for (int side = 0; side < 2; ++side)
                        for (int c = 0; c < r->channels; ++c)
                            pcm.write (reinterpret_cast<const char*> (r->pcm[static_cast<std::size_t> (side * 2 + c)].data()),
                                       static_cast<std::streamsize> (r->frames * static_cast<int> (sizeof (float))));
                    rowJson (meta, *r, offset);
                    offset += static_cast<std::uint64_t> (r->frames * r->channels * 2) * sizeof (float);
                    ++written;
                    queue.pop();
                    if (! meta || ! pcm) { failed.store (true, std::memory_order_release); return; }
                }
                pcm.flush(); meta.flush();
                if (! meta || ! pcm) { failed.store (true, std::memory_order_release); return; }
                if (stopping.load (std::memory_order_acquire))
                {
                    // Producer has stopped before destruction. Recheck after acquiring stop.
                    if (queue.front() != nullptr) continue;
                    meta << "{\"type\":\"end\",\"written\":" << written << ",\"callbacks\":" << serial
                         << ",\"dropped\":" << dropped.load() << "}\n";
                    meta.flush();
                    return;
                }
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
            }
        }
        catch (...) { failed.store (true, std::memory_order_release); }
    }
    Queue<> queue;
    static_assert (std::atomic<bool>::is_always_lock_free);
    Row* current = nullptr;
    std::uint64_t serial = 0, localFrames = 0; // Producer; worker reads serial only after stop.
    std::atomic<std::uint64_t> dropped { 0 };
    std::atomic<bool> stopping { false }, failed { false };
    std::ofstream meta, pcm;
    std::thread worker;
};
}
