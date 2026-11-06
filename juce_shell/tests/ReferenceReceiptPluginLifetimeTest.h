#pragma once
#include "../src/AtomicFile.h"
#include <condition_variable>
#include <future>
#include <mutex>

namespace
{
struct PluginReceiptStore
{
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false;
    int calls = 0;
    static bool save (void* context, const char* file, const char* text)
    {
        auto& self = *static_cast<PluginReceiptStore*> (context);
        {
            std::unique_lock<std::mutex> lock (self.mutex);
            ++self.calls;
            self.entered = true;
            self.changed.notify_all();
            self.changed.wait (lock, [&] { return self.released; });
        }
        return hypha::storage::writeText (juce::File (file), juce::String::fromUTF8 (text), false, false, "\n");
    }
    bool awaitEntry()
    {
        std::unique_lock<std::mutex> lock (mutex);
        return changed.wait_for (lock, std::chrono::seconds (30), [&] { return entered; });
    }
    void release()
    {
        const std::lock_guard<std::mutex> lock (mutex);
        released = true;
        changed.notify_all();
    }
};

int testReceiptPluginLifetime (const juce::File& plugin, const juce::File& pinModule)
{
    ValidationStorageSandbox sandbox;
    // This tiny module has no Rust TLV or JUCE static owners that could mask a missing pin.
    juce::DynamicLibrary control;
    require (control.open (pinModule.getFullPathName()), "load an unpinned control module");
    control.close();
    require (!mapped (pinModule), "the control module really unmaps before any pin");
    require (control.open (pinModule.getFullPathName()), "reload the control module");
    const auto retain = reinterpret_cast<bool(*)()> (control.getFunction ("retainReceiptProbeModule"));
    require (retain != nullptr && retain() && retain(), "pin the production code once for repeated requests");
    control.close();
    require (mapped (pinModule), "the explicit production pin survives the host's last unload");

    for (int cycle = 0; cycle < 3; ++cycle)
    {
        juce::DynamicLibrary library;
        require (library.open (plugin.getFullPathName()), "load the real POST VST3 binary");
        require (library.getFunction ("GetPluginFactory") != nullptr, "the loaded image is a VST3 plugin");
        using Start = void* (*) (const char*, bool (*) (void*, const char*, const char*), void*);
        const auto start = reinterpret_cast<Start> (library.getFunction ("startReceiptControllerProbe"));
        const auto destroy = reinterpret_cast<bool(*)(void*)> (library.getFunction ("destroyReceiptControllerProbe"));
        const auto finish = reinterpret_cast<bool(*)(void*)> (library.getFunction ("finishReceiptControllerProbe"));
        require (start != nullptr && destroy != nullptr && finish != nullptr, "test-only plugin probe exports");
        const auto root = sandbox.directory().getChildFile ("cycle-" + juce::String (cycle));
        require (root.createDirectory().wasOk(), "isolated plugin receipt fixture");
        PluginReceiptStore store;
        auto* instance = start (root.getFullPathName().toRawUTF8(), &PluginReceiptStore::save, &store);
        require (instance != nullptr && store.awaitEntry(), "the real controller worker enters the held receipt save");
        auto unloading = std::async (std::launch::async, [&]
        {
            const bool destroyed = destroy (instance);
            library.close();
            return destroyed;
        });
        const bool closed = unloading.wait_for (std::chrono::seconds (5)) == std::future_status::ready;
        if (!closed) store.release(); // A waiting-destruction regression must fail without hanging the test.
        require (unloading.get() && closed, "controller destruction and plugin unload return while storage is held");
        require (mapped (plugin), "notification code remains resident after the host closes the plugin");
        store.release();
        require (finish (instance), "owned notification inputs complete after actual plugin close");
        const auto receipt = root.getChildFile ("library/library/received/12345678-1234-4234-8234-123456789abc.json");
        const auto json = juce::JSON::parse (receipt);
        require (store.calls == 1 && json["format"] == "kirin_hypha_reference_library_received"
            && json["runtime_instance_id"] == receipt.getFileNameWithoutExtension(),
            "exactly one valid receipt survives deletion and unload, with no pending write");
    }
    std::cout << "Reference receipt lifetime: explicit loader pin, held store, controller destruction and 3 real VST3 close/reopen cycles PASS\n";
    return 0;
}
}
