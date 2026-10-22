#include "../../src/chain_trace/ChainTraceWriter.h"
#include "../host_clock_diagnostic/ChainTraceFixture.h"
#include "../host_clock_diagnostic/IdentityAudit.h"
#include <cstdlib>
#include <iostream>
#include <thread>

static thread_local bool rt = false;
static std::atomic<unsigned> allocations { 0 };
void* operator new (std::size_t n)
{
    if (rt) allocations.fetch_add (1);
    if (auto* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return ::operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
static void require (bool ok) { if (!ok) std::abort(); }
#define assert(expression) require (expression)

static void environment (const char* value)
{
   #if defined (_WIN32)
    _putenv_s ("KIRIN_HYPHA_CHAIN_TRACE_DIR", value);
   #else
    setenv ("KIRIN_HYPHA_CHAIN_TRACE_DIR", value, 1);
   #endif
}

static void writer (const std::filesystem::path& parent)
{
    using hypha::chain_trace::Recorder;
    environment (""); assert (!Recorder::fromEnvironment());
    environment ("relative-folder"); assert (!Recorder::fromEnvironment());
    environment ((parent / "missing").string().c_str()); assert (!Recorder::fromEnvironment());
    std::filesystem::create_directories (parent);
    environment (parent.string().c_str());
    auto recorder = Recorder::fromEnvironment(); assert (recorder != nullptr);
    float left[] { 1, -0.0f, 0.125f, -0.5f }, right[] { -1, 0, 0.25f, 0.5f };
    const float* input[] { left, right };
    rt = true;
    auto* r = recorder->begin (input, 2, 4); assert (r != nullptr);
    r->role = 0; r->rate = 48000; r->precisionBits = 64; r->host.hasPosition = true;
    recorder->finish (input);
    assert (recorder->begin (input, 2, 8193) == nullptr);
    recorder->finish (input);
    rt = false;
    recorder.reset();
    bool checked = false;
    for (const auto& entry : std::filesystem::directory_iterator (parent))
    {
        const auto metaPath = entry.path() / "blocks.jsonl";
        if (!std::filesystem::exists (metaPath)) continue;
        std::ifstream file (metaPath); const std::string text ((std::istreambuf_iterator<char> (file)), {});
        assert (text.find ("\"dropped\":1") != std::string::npos);
        assert (text.find ("\"precisionBits\":64") != std::string::npos);
        std::ifstream pcm (entry.path() / "audio.f32", std::ios::binary);
        float observed[16] {}; pcm.read (reinterpret_cast<char*> (observed), sizeof (observed));
        assert (pcm.gcount() == sizeof (observed));
        assert (std::memcmp (left, observed, sizeof (left)) == 0);
        assert (std::memcmp (right, observed + 4, sizeof (right)) == 0);
        assert (std::memcmp (observed, observed + 8, sizeof (float) * 8) == 0);
        checked = true;
    }
    assert (checked && allocations.load() == 0);
    environment ("");
    std::cout << "writer: opt-in, path refusal, exact PCM, drop footer, zero RT allocation PASS\n";
}

static void concurrentQueue()
{
    auto queue = std::make_unique<hypha::chain_trace::Queue<2>>();
    std::thread consumer ([&] {
        for (unsigned i = 1; i <= 10000; ++i)
        {
            const hypha::chain_trace::Row* r;
            while ((r = queue->front()) == nullptr) std::this_thread::yield();
            assert (r->serial == i && r->pcm[0][0] == static_cast<float> (i));
            queue->pop();
        }
    });
    for (unsigned i = 1; i <= 10000; ++i)
    {
        hypha::chain_trace::Row* r;
        while ((r = queue->begin (1, 1)) == nullptr) std::this_thread::yield();
        r->serial = i; r->pcm[0][0] = static_cast<float> (i); queue->publish();
    }
    consumer.join();
    std::cout << "concurrent queue: 10000 ordered immutable blocks PASS\n";
}

static void fixture()
{
    auto delay = std::make_unique<hypha::chain_trace_fixture::Delay>();
    hypha::chain_trace_fixture::Source source;
    std::array<float, 32> l {}, r {};
    float* io[] {l.data(), r.data()};
    source.process (io, 2, 32, false); assert (l[0] == 0 && r[31] == 0);
    source.process (io, 2, 32, true);
    auto copy = l;
    delay->process (io, 2, 32, 0); assert (copy == l);
    source.process (io, 2, 32, true);
    delay->process (io, 2, 32, 16);
    auto audit = hypha::clock_diagnostic::IdentityAudit::inspect (l.data(), r.data(), 32);
    assert (audit.errors == 0 && audit.first == 17 && audit.last == 48);
    source.process (io, 2, 32, true); delay->process (io, 2, 32, 0);
    audit = hypha::clock_diagnostic::IdentityAudit::inspect (l.data(), r.data(), 32);
    assert (audit.errors == 0 && audit.first == 65);
    delay->reset();
    delay->process (io, 2, 32, hypha::chain_trace_fixture::Delay::maximum);
    assert (l[0] == 0 && r[31] == 0);
    source.process (io, 1, 32, true); assert (l[0] * 536870912.0f == 97);
    std::cout << "fixture: arming, unity, changed delay, max delay, mono identity PASS\n";
}

int main (int argc, char** argv)
{
    auto queue = std::make_unique<hypha::chain_trace::Queue<2>>();
    auto* first = queue->begin (4, 2);
    assert (first != nullptr);
    first->serial = 1;
    assert (queue->front() == nullptr); // Unpublished input must not reach the writer.
    queue->publish();
    auto* second = queue->begin (4, 1);
    assert (second != nullptr);
    second->serial = 2;
    queue->publish();
    assert (queue->begin (4, 2) == nullptr); // Full: never overwrite unread PCM.
    assert (queue->front()->serial == 1);
    queue->pop();
    assert (queue->front()->serial == 2);
    queue->pop();
    assert (queue->front() == nullptr);
    assert (queue->begin (0, 2) == nullptr);
    assert (queue->begin (8193, 2) == nullptr);
    assert (queue->begin (4, 3) == nullptr);
    assert (queue->begin (8192, 1) != nullptr);
    queue->publish();
    queue->pop();
    std::cout << "chain trace queue: 8 checks PASS\n";
    concurrentQueue(); fixture();
    if (argc == 2) writer (std::filesystem::absolute (argv[1]));
}
