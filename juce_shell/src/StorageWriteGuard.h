#pragma once
#include "StorageWritePathPolicy.h"
#include <mutex>
#include <optional>
#include <cstdint>

// Same definitions in all translation units. One executable's inline state is NOT
// a policy transport to separately loaded DLLs/plugins or Rust libraries.
// JUCE shared-code objects use hidden visibility, while plain native executables
// may not. Keep the state, its initialization guard and accessors module-local
// with identical visibility in every TU; no inter-DSO state sharing is promised.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(hidden)
#endif
namespace hypha::storage::write_guard {
namespace detail {
struct Runtime {
    std::mutex mutex;
    bool enabled = false, valid = false;
    juce::String root;
    std::optional<FixturePolicy> fixture;
    std::uint64_t permitted = 0;
};
inline Runtime runtime;
}
inline bool validRoot(const juce::String& root) {
    try {
        if (root.isEmpty() || !juce::File::isAbsolutePath(root)) return false;
        return detail::allowed(juce::File(root).getChildFile("guard-root-check"), {root,{}});
    } catch (...) { return false; }
}
// Irreversible. Failed initialization remains enabled+invalid, never shipping mode.
inline bool enable(const juce::String& root) {
    auto& s=detail::runtime; const std::lock_guard<std::mutex> lock(s.mutex);
    if(s.enabled) return s.valid && s.root==root;
    s.enabled=true; s.root=root; s.valid=validRoot(root); return s.valid;
}
inline bool enableFromEnvironment() {
    return enable(juce::SystemStats::getEnvironmentVariable("KIRIN_HYPHA_TEST_STORAGE_ROOT",{}));
}
inline bool permitMutation(const juce::File& destination) {
    auto& s=detail::runtime; const std::lock_guard<std::mutex> lock(s.mutex);
    if(!s.enabled) return true;
    if(!s.valid) return false;
    try {
        // The fixture may narrow allowed roots/add exclusions, never widen startup authority.
        if(!detail::allowed(destination,{s.root,{}})) return false;
        if(s.fixture && !detail::allowed(destination,*s.fixture)) return false;
    } catch (...) { return false; }
    ++s.permitted; return true;
}
class ScopedFixturePolicy {
public:
    explicit ScopedFixturePolicy(FixturePolicy policy) {
        auto& s=detail::runtime; const std::lock_guard<std::mutex> lock(s.mutex);
        previous=s.fixture; s.fixture=std::move(policy);
    }
    ~ScopedFixturePolicy() {
        auto& s=detail::runtime; const std::lock_guard<std::mutex> lock(s.mutex); s.fixture=std::move(previous);
    }
    ScopedFixturePolicy(const ScopedFixturePolicy&)=delete;
    ScopedFixturePolicy& operator=(const ScopedFixturePolicy&)=delete;
private:
    std::optional<FixturePolicy> previous;
};
inline std::uint64_t permittedMutationEntries() {
    auto& s=detail::runtime; const std::lock_guard<std::mutex> lock(s.mutex); return s.permitted;
}
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
