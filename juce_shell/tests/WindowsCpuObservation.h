#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

// A diagnostic, not a performance acceptance gate: no editor, routing proof, Record,
// or optional analysis. CPU includes every worker in this isolated host process.
namespace hypha::validation
{
struct ProcessCounters
{
    double kernel = 0.0, user = 0.0;
    IO_COUNTERS io {};
};

inline ProcessCounters processCounters()
{
    FILETIME created {}, exited {}, kernel {}, user {};
    if (! GetProcessTimes (GetCurrentProcess(), &created, &exited, &kernel, &user))
        throw std::runtime_error ("GetProcessTimes failed");
    const auto ticks = [] (FILETIME value)
    { return (static_cast<uint64_t> (value.dwHighDateTime) << 32) | value.dwLowDateTime; };
    ProcessCounters result;
    result.kernel = static_cast<double> (ticks (kernel)) / 10'000'000.0;
    result.user = static_cast<double> (ticks (user)) / 10'000'000.0;
    if (! GetProcessIoCounters (GetCurrentProcess(), &result.io))
        throw std::runtime_error ("GetProcessIoCounters failed");
    return result;
}

class CpuPlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (true);
        info.setTimeInSamples (sample);
        info.setTimeInSeconds (static_cast<double> (sample) / 48'000.0);
        return info;
    }
    int64_t sample = 0;
};

inline void observeCpu (const char* label,
                        std::vector<std::unique_ptr<juce::AudioPluginInstance>>& instances,
                        CpuPlayHead& playHead, bool processAudio, int seconds)
{
    using Clock = std::chrono::steady_clock;
    constexpr int frames = 512;
    juce::AudioBuffer<float> buffer (2, frames);
    juce::MidiBuffer midi;
    int64_t callbacks = 0;
    int64_t lateBlocks = 0;
    double maximumCallbackMs = 0.0;
    double callbackSeconds = 0.0;
    SYSTEM_INFO system {};
    GetSystemInfo (&system);
    const auto before = processCounters();
    const auto start = Clock::now();
    auto deadline = start;
    const auto period = std::chrono::duration_cast<Clock::duration> (
        std::chrono::duration<double> (frames / 48'000.0));
    const auto end = start + std::chrono::seconds (seconds);
    while (Clock::now() < end)
    {
        // Service the native host message thread: the plugin's 50 ms timer enables
        // IO after state restore. An unpumped console would omit that part of the host.
        MSG message {};
        while (PeekMessageW (&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
                throw std::runtime_error ("Diagnostic message loop was asked to quit");
            TranslateMessage (&message);
            DispatchMessageW (&message);
        }
        if (processAudio)
        {
            for (int channel = 0; channel < 2; ++channel)
                for (int frame = 0; frame < frames; ++frame)
                    buffer.setSample (channel, frame, 0.5f * static_cast<float> (std::sin (
                        juce::MathConstants<double>::twoPi * 1000.0
                        * static_cast<double> (playHead.sample + frame) / 48'000.0)));
            const auto callbackStart = Clock::now();
            for (auto& instance : instances)
            {
                instance->processBlock (buffer, midi);
                if (instance->getLatencySamples() != 0)
                    throw std::runtime_error ("Diagnostic observed non-zero plugin latency");
            }
            const auto duration = std::chrono::duration<double> (Clock::now() - callbackStart).count();
            callbackSeconds += duration;
            maximumCallbackMs = std::max (maximumCallbackMs, duration * 1000.0);
            ++callbacks;
            playHead.sample += frames;
        }
        deadline += period;
        if (Clock::now() > deadline)
            ++lateBlocks;
        // No catch-up burst: lateness is reported, then timing restarts from now.
        deadline = std::max (deadline, Clock::now());
        std::this_thread::sleep_until (deadline);
    }
    const auto elapsed = std::chrono::duration<double> (Clock::now() - start).count();
    const auto after = processCounters();
    const auto kernel = after.kernel - before.kernel, user = after.user - before.user;
    const auto cpu = kernel + user;
    std::cout << "CPU_OBSERVATION phase=" << label << " instances=" << instances.size()
              << " wall_s=" << elapsed << " cpu_s=" << cpu
              << " kernel_s=" << kernel << " user_s=" << user
              << " cores=" << cpu / elapsed
              << " machine_percent=" << cpu / elapsed / system.dwNumberOfProcessors * 100.0
              << " callbacks=" << callbacks << " late_blocks=" << lateBlocks
              << " callback_wall_s=" << callbackSeconds
              << " max_callback_ms=" << maximumCallbackMs
              << " io_read_ops=" << after.io.ReadOperationCount - before.io.ReadOperationCount
              << " io_write_ops=" << after.io.WriteOperationCount - before.io.WriteOperationCount
              << " io_other_ops=" << after.io.OtherOperationCount - before.io.OtherOperationCount
              << " io_read_bytes=" << after.io.ReadTransferCount - before.io.ReadTransferCount
              << " io_write_bytes=" << after.io.WriteTransferCount - before.io.WriteTransferCount
              << std::endl;
}

inline void runCpuObservation (const juce::String& pre, const juce::String& post, int pairs)
{
    if (pairs < 1 || pairs > 16)
        throw std::runtime_error ("Diagnostic pair count must be 1..16");
    juce::VST3PluginFormat format;
    CpuPlayHead playHead;
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> instances;
    observeCpu ("empty-host", instances, playHead, false, 6);
    for (const auto& path : { pre, post })
    {
        juce::OwnedArray<juce::PluginDescription> descriptions;
        format.findAllTypesForFile (descriptions, path);
        if (descriptions.size() != 1)
            throw std::runtime_error ("Diagnostic requires exactly one VST3 component per bundle");
        for (int index = 0; index < pairs; ++index)
        {
            juce::String error;
            auto instance = format.createInstanceFromDescription (*descriptions[0], 48'000.0, 512, error);
            if (instance == nullptr)
                throw std::runtime_error (error.toStdString());
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (juce::AudioChannelSet::stereo());
            layout.outputBuses.add (juce::AudioChannelSet::stereo());
            if (! instance->setBusesLayout (layout))
                throw std::runtime_error ("Diagnostic stereo layout rejected");
            instance->setNonRealtime (false);
            instance->setPlayHead (&playHead);
            instance->prepareToPlay (48'000.0, 512);
            if (auto* bypass = instance->getBypassParameter())
                bypass->setValueNotifyingHost (0.0f);
            instances.push_back (std::move (instance));
        }
    }
    observeCpu ("prepared-no-callbacks", instances, playHead, false, 6);
    observeCpu ("playing-no-editor", instances, playHead, true, 12);
    observeCpu ("stopped-first", instances, playHead, false, 6);
    observeCpu ("stopped-settled", instances, playHead, false, 6);
    for (auto& instance : instances)
    {
        instance->setPlayHead (nullptr);
        instance->releaseResources();
    }
    instances.clear();
    observeCpu ("destroyed", instances, playHead, false, 6);
}
} // namespace hypha::validation
