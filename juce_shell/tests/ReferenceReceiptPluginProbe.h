// Compiled only into development tests and the test-enabled VST3 wrapper.
#if defined(_WIN32)
 #include <process.h>
 #define RECEIPT_PROBE_EXPORT extern "C" __declspec (dllexport)
#else
 #include <unistd.h>
 #define RECEIPT_PROBE_EXPORT extern "C" __attribute__ ((visibility ("default")))
#endif

#if defined(KIRIN_RECEIPT_PIN_PROBE_ONLY)
 #include "../src/reference_audition/ReferenceReceiptModuleLifetime.h"
RECEIPT_PROBE_EXPORT bool retainReceiptProbeModule();
RECEIPT_PROBE_EXPORT bool retainReceiptProbeModule()
{
    return hypha::reference_audition::receipt_detail::retainModule();
}
#else
 #include "../src/reference_audition/ReferenceRuntimeV2Controller.h"
 #include "KirinLibraryFixture.h"

namespace hypha::tests::receipt_plugin
{
using Runtime = reference_audition::RuntimeV2Controller;
using Writer = reference_audition::LibraryReceivedWriter;
struct WriterTag {};
Writer& writerFor (Runtime&, WriterTag);
template <auto Member> struct WriterAccess
{
    friend Writer& writerFor (Runtime& runtime, WriterTag) { return runtime.*Member; }
};
template struct WriterAccess<&Runtime::libraryReceivedWriter>;
struct CompletionTag {};
std::shared_future<bool> completionFor (Writer&, CompletionTag);
template <auto Member> struct CompletionAccess
{
    friend std::shared_future<bool> completionFor (Writer& writer, CompletionTag) { return writer.*Member; }
};
template struct CompletionAccess<&Writer::running>;
struct Probe
{
    std::unique_ptr<Runtime> runtime;
    std::shared_future<bool> completion;
};
}

RECEIPT_PROBE_EXPORT void* startReceiptControllerProbe (const char*, bool (*) (void*, const char*, const char*), void*);
RECEIPT_PROBE_EXPORT bool destroyReceiptControllerProbe (void*);
RECEIPT_PROBE_EXPORT bool finishReceiptControllerProbe (void*);

RECEIPT_PROBE_EXPORT void* startReceiptControllerProbe (
    const char* sandbox, bool (*store) (void*, const char*, const char*), void* context)
{
    using namespace hypha::tests::receipt_plugin;
    const auto root = kirin_library_fixture::copy (juce::File (sandbox), "library");
    if (root == juce::File()) return nullptr;
    auto probe = std::make_unique<Probe>();
    probe->runtime = std::make_unique<Runtime> (root, Runtime::SelectionGate {}, false,
        [store, context] (const juce::File& file, const juce::String& text)
        { return store (context, file.getFullPathName().toRawUTF8(), text.toRawUTF8()); });
    probe->runtime->reportLibraryReceived();
    hypha::reference_audition::RuntimeIdentity identity;
    identity.library = true;
    identity.runtimeInstanceId = "12345678-1234-4234-8234-123456789abc";
   #if defined(_WIN32)
    identity.hostProcessId = static_cast<std::uint32_t> (::_getpid());
   #else
    identity.hostProcessId = static_cast<std::uint32_t> (::getpid());
   #endif
    identity.hyphaVersion = "1.1.55";
    probe->runtime->configure (identity, 48000, 2);
    return probe.release();
}

RECEIPT_PROBE_EXPORT bool destroyReceiptControllerProbe (void* opaque)
{
    using namespace hypha::tests::receipt_plugin;
    auto& probe = *static_cast<Probe*> (opaque);
    if (! ((juce::Thread&) *probe.runtime).stopThread (5000)) return false;
    probe.completion = completionFor (writerFor (*probe.runtime, {}), {});
    probe.runtime.reset();
    return probe.completion.valid();
}

RECEIPT_PROBE_EXPORT bool finishReceiptControllerProbe (void* opaque)
{
    using namespace hypha::tests::receipt_plugin;
    const std::unique_ptr<Probe> probe (static_cast<Probe*> (opaque));
    return probe->completion.valid() && probe->completion.get();
}
#endif

#undef RECEIPT_PROBE_EXPORT
