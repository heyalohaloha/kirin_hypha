#include "reference_runtime_test_support.h"

#include "../src/reference_audition/ReferenceRuntimeV2SourceCache.h"

void testRuntimeV2SourceCache();

void testRuntimeV2SourceCache()
{
    ref::RuntimeV2SourceCache cache;
    auto first = std::make_shared<ref::RuntimeSource>();
    auto second = std::make_shared<ref::RuntimeSource>();
    auto third = std::make_shared<ref::RuntimeSource>();
    auto fourth = std::make_shared<ref::RuntimeSource>();
    cache.remember ("one", first);
    cache.remember ("two", second);
    cache.remember ("three", third);
    require (cache.find ("one") == first,
             "a previously verified B source must remain available for fast recall");
    cache.remember ("four", fourth);
    require (cache.size() == ref::RuntimeV2SourceCache::capacity
             && cache.find ("two") == nullptr
             && cache.find ("one") == first,
             "the verified B cache must remain bounded and evict its least recent source");
}
