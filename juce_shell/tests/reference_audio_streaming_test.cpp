#include "../src/reference_audition/ReferenceAudioPages.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>

namespace ref = hypha::reference_audition;
using namespace std::chrono_literals;

namespace
{
    constexpr int rate = 48'000;
    thread_local bool audioCallback = false;

    void require (bool result, const char* message)
    {
        if (!result) { std::cerr << message << '\n'; std::exit (1); }
    }

    template <typename Predicate> bool until (Predicate predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (!predicate())
        {
            if (std::chrono::steady_clock::now() >= deadline) return false;
            std::this_thread::sleep_for (1ms);
        }
        return true;
    }

    struct ReadControl
    {
        std::mutex mutex;
        std::condition_variable condition;
        bool block = false, entered = false, fail = false;
        std::atomic<int> reads { 0 };
        std::atomic<bool> destroyed { false };

        void blockNextRead()
        {
            std::lock_guard<std::mutex> guard (mutex);
            entered = false;
            block = true;
        }
        bool blocked()
        {
            std::lock_guard<std::mutex> guard (mutex);
            return entered;
        }
        void release (bool failReads = false)
        {
            std::lock_guard<std::mutex> guard (mutex);
            block = false;
            fail = failReads;
            condition.notify_all();
        }
    };

    class Reader final : public juce::AudioFormatReader
    {
    public:
        explicit Reader (std::shared_ptr<ReadControl> controlIn, float offsetIn = 0.0f)
            : AudioFormatReader (nullptr, "stream-test"),
              control (std::move (controlIn)), offset (offsetIn)
        {
            sampleRate = rate;
            numChannels = 2;
            lengthInSamples = rate * 20;
            bitsPerSample = 32;
            usesFloatingPointData = true;
        }
        ~Reader() override { control->destroyed.store (true); }
        static float value (int channel, std::int64_t position, float offset)
        {
            return offset + float(channel + 1) * 0.1f + float(position % 1000) / 10'000.0f;
        }
        bool readSamples (int* const* destination, int channels, int start,
                          juce::int64 position, int frames) override
        {
            require (!audioCallback, "decode must never run on the audio callback");
            std::unique_lock<std::mutex> guard (control->mutex);
            control->entered = true;
            control->condition.notify_all();
            control->condition.wait (guard, [&] { return !control->block; });
            control->reads.fetch_add (1);
            if (control->fail) return false;
            for (int c = 0; c < channels; ++c)
                for (int f = 0; f < frames; ++f)
                    reinterpret_cast<float*> (destination[c])[start + f]
                        = value (c, position + f, offset);
            return true;
        }
    private:
        const std::shared_ptr<ReadControl> control;
        const float offset;
    };

    bool render (ref::AudioPages& pages, juce::AudioBuffer<float>& output,
                 std::int64_t position)
    {
        audioCallback = true;
        const auto result = pages.render (output, position, 1.0f);
        audioCallback = false;
        return result;
    }

    bool sameBits (float left, float right)
    { return std::memcmp (&left, &right, sizeof (float)) == 0; }

    void checkAudio (const juce::AudioBuffer<float>& output, std::int64_t position,
                     float offset)
    {
        for (int c = 0; c < output.getNumChannels(); ++c)
            for (int f = 0; f < output.getNumSamples(); ++f)
                require (sameBits (output.getSample (c, f), Reader::value (c, position + f, offset)),
                         "every rendered sample must belong to the requested source and position");
    }

    void refillWithoutControlWorker()
    {
        ref::AudioPages pages;
        auto control = std::make_shared<ReadControl>();
        require (pages.installReaderForTest (std::make_unique<Reader> (control), rate, 2).isEmpty(),
                 "stream source opens");
        juce::AudioBuffer<float> output (2, 512);
        // No control-thread service call after open: alignment/repository work may be blocked.
        // Travel beyond all six physical cache slots, checking both sides of each boundary.
        for (int second = 1; second < 12; ++second)
        {
            const auto position = static_cast<std::int64_t> (second) * rate;
            pages.request (position);
            require (until ([&] { return pages.readyAt (position + rate, 512); }),
                     "refill must advance while the control worker does not service pages");
            require (render (pages, output, position + rate - 256),
                     "prepared page boundaries must remain playable without control service");
            checkAudio (output, position + rate - 256, 0.0f);
        }
        std::cout << "PASS independent refill across 11 page boundaries\n";
    }

    void blockedAndFailedReadPreservesA()
    {
        ref::AudioPages pages;
        auto control = std::make_shared<ReadControl>();
        require (pages.installReaderForTest (std::make_unique<Reader> (control), rate, 2).isEmpty(),
                 "failure source opens");
        control->blockNextRead();
        pages.request (4 * rate);
        require (until ([&] { return control->blocked(); }), "background reader reaches controlled barrier");
        auto controlCycle = std::async (std::launch::async, [&]
        {
            // Both controllers request service; RuntimeV2 also publishes cue pinning
            // before it drains A capture. Neither operation may wait for B decoding.
            pages.service();
            pages.setPinnedCue (10 * rate, 11 * rate, true);
        });
        require (controlCycle.wait_for (3s) == std::future_status::ready,
                 "control service and cue publication must finish while B decoding is blocked");
        controlCycle.get();
        juce::AudioBuffer<float> a (2, 512);
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < 512; ++f) a.setSample (c, f, -0.25f);
        require (!render (pages, a, 4 * rate), "pending data cannot be rendered");
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < 512; ++f)
                require (sameBits (a.getSample (c, f), -0.25f), "cache miss must leave all A samples untouched");
        const auto previousReads = control->reads.load();
        control->release (true);
        require (until ([&] { return control->reads.load() > previousReads; }), "failed read completes");
        require (!pages.readyAt (4 * rate, 512), "failed decode is never published ready");
        control->release();
        require (until ([&] { return pages.readyAt (4 * rate, 512); }), "refill retries after recoverable I/O failure");
        require (until ([&] { return pages.readyAt (10 * rate, 512); }),
                 "cue published during blocked I/O is prefetched after decoding resumes");
        require (render (pages, a, 4 * rate), "recovered data is playable");
        checkAudio (a, 4 * rate, 0.0f);
        std::cout << "PASS pending/failed decode preserves A and can recover\n";
    }

    void closeWaitsForOwnedReader()
    {
        auto pages = std::make_unique<ref::AudioPages>();
        auto control = std::make_shared<ReadControl>();
        require (pages->installReaderForTest (std::make_unique<Reader> (control), rate, 2).isEmpty(),
                 "lifecycle source opens");
        control->blockNextRead();
        pages->request (8 * rate);
        require (until ([&] { return control->blocked(); }), "reader is held before close");
        std::promise<void> closing;
        auto closed = std::async (std::launch::async, [&] { closing.set_value(); pages->close(); });
        closing.get_future().wait();
        require (closed.wait_for (20ms) == std::future_status::timeout && !control->destroyed.load(),
                 "close must not destroy an in-flight reader");
        control->release();
        require (closed.wait_for (3s) == std::future_status::ready, "close finishes once I/O releases");
        closed.get();
        require (!pages->sourceOpen() && control->destroyed.load(), "closed generation retires its reader");
        auto replacement = std::make_shared<ReadControl>();
        require (pages->installReaderForTest (std::make_unique<Reader> (replacement, 0.5f), rate, 2).isEmpty(),
                 "new source reopens the same streaming owner");
        pages->request (8 * rate);
        require (until ([&] { return pages->readyAt (8 * rate, 512); }), "replacement refill is independent");
        juce::AudioBuffer<float> output (2, 512);
        require (render (*pages, output, 8 * rate), "replacement renders");
        checkAudio (output, 8 * rate, 0.5f);
        replacement->blockNextRead();
        pages->request (15 * rate);
        require (until ([&] { return replacement->blocked(); }), "reader is held before destruction");
        auto retired = std::async (std::launch::async, [&] { pages.reset(); });
        require (retired.wait_for (20ms) == std::future_status::timeout && !replacement->destroyed.load(),
                 "destruction joins refill before releasing reader storage");
        replacement->release();
        require (retired.wait_for (3s) == std::future_status::ready, "destruction joins without forced thread kill");
        retired.get();
        require (replacement->destroyed.load(), "replacement reader is released");
        std::cout << "PASS close/reopen/destruction serialize with refill\n";
    }
}

int main()
{
    refillWithoutControlWorker();
    blockedAndFailedReadPreservesA();
    closeWaitsForOwnedReader();
}
