#include "../../src/chain_trace/ChainTrace.h"
#include <cstdio>
#include <cstdlib>
int main()
{
    auto queue = std::make_unique<hypha::chain_trace::Queue<>>();
    // Preparation and playback can each deliver 128 callbacks before the writer is scheduled.
    for (unsigned i = 0; i < 256; ++i)
    {
        auto* row = queue->begin (512, 2);
        if (!row) { std::fputs ("diagnostic queue dropped a preparation/start burst\n", stderr); return 1; }
        row->serial = i; row->pcm[0][0] = static_cast<float> (i); queue->publish();
    }
    for (unsigned i = 0; i < 256; ++i)
    {
        const auto* row = queue->front();
        if (!row || row->serial != i || row->pcm[0][0] != static_cast<float> (i)) std::abort();
        queue->pop();
    }
    if (queue->front()) std::abort();
    static_assert (sizeof (*queue) < 36u * 1024u * 1024u);
    for (std::size_t i = 0; i < hypha::chain_trace::queueBlocks; ++i)
    { if (!queue->begin (8192, 2)) std::abort(); queue->publish(); }
    if (queue->begin (8192, 2)) std::abort(); // full rejects the entire callback
    queue->pop();
    if (!queue->begin (8192, 1)) std::abort(); // one reclaimed slot, no extra allocation

    std::puts ("diagnostic queue: 256 undrained callbacks in order PASS");
}
