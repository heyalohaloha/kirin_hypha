#pragma once
#include "ReaperPdcMailbox.h"
#include "ReaperChainStatus.h"
#include <array>
#include <charconv>
#include <cstring>
#include <string>
#include <vector>

namespace hypha::live_compare
{
// Public read-only REAPER API signatures. All calls stay on the message thread.
// https://www.reaper.fm/sdk/reascript/reascripthelp.html#TrackFX_GetNamedConfigParm
struct ReaperChainApi
{
    bool (*valid) (void*, void*, const char*) = nullptr;
    int (*count) (void*) = nullptr;
    int (*inputCount) (void*) = nullptr;
    bool (*named) (void*, int, const char*, char*, int) = nullptr;
    const void* (*fxGuid) (void*, int) = nullptr;
    const void* (*trackGuid) (void*) = nullptr;
    bool (*enabled) (void*, int) = nullptr;
    bool (*offline) (void*, int) = nullptr;
    int (*playing) (void*) = nullptr;
    int (*changes) (void*) = nullptr;
    double (*trackInfo) (void*, const char*) = nullptr;
    double (*playRate) (void*) = nullptr;
    bool complete() const noexcept
    { return valid && count && inputCount && named && fxGuid && trackGuid && enabled && offline
        && playing && changes && trackInfo && playRate; }
};

inline bool reaperUnsigned (const std::string& text, std::uint32_t& value)
{
    if (text.empty() || text.front() < '0' || text.front() > '9') return false;
    const auto read = std::from_chars (text.data(), text.data() + text.size(), value);
    return read.ec == std::errc {} && read.ptr == text.data() + text.size();
}

// A deterministic opaque fingerprint covers process/parent identity, every GUID in order,
// PDC/mode/enabled/offline and scheduling settings. No pointer, path or display name is sent
// to RT/shared memory. Independent PRE/POST readers must observe the identical full tuple.
struct ReaperFingerprint
{
    std::uint64_t a = 14695981039346656037ull, b = 7809847782465536322ull;
    void append (const void* data, std::size_t size)
    {
        const auto* bytes = static_cast<const unsigned char*> (data);
        for (std::size_t i = 0; i < size; ++i)
        { a = (a ^ bytes[i]) * 1099511628211ull; b = (b ^ bytes[i]) * 14029467366897019727ull; }
        a = (a ^ 255u) * 1099511628211ull; b = (b ^ 255u) * 14029467366897019727ull;
    }
    void text (const std::string& s) { append (s.data(), s.size()); }
    template <typename T> void number (T value) { append (&value, sizeof (value)); }
};

inline ReaperPdcObservation readReaperChain (const ReaperChainApi& api, void* project, void* track,
                                            void* take, std::uint64_t process,
                                            ReaperChainRestriction* restriction = nullptr)
{
    const auto reject = [&] (ReaperChainRestriction reason) {
        if (restriction) *restriction = reason; return ReaperPdcObservation {}; };
    if (restriction) *restriction = ReaperChainRestriction::unavailable;
    if (take) return reject (ReaperChainRestriction::routing);
    if (!api.complete() || !project || !track || take || process == 0
        || !api.valid (project, project, "ReaProject*") || !api.valid (project, track, "MediaTrack*")
        || (api.playing (project) & 3) != 1 || api.playRate (project) != 1.0) return {};
    const auto changes = api.changes (project);
    const auto count = api.count (track);
    if (api.inputCount (track) != 0) return reject (ReaperChainRestriction::inputFx);
    if (count < 2 || count > 256) return {};
    const auto channels = api.trackInfo (track, "I_NCHAN");
    if (!(channels <= 2 && channels >= 2)) return reject (ReaperChainRestriction::channels); // mono inserts on a two-channel track are allowed
    ReaperFingerprint hash;
    hash.number (process); hash.number (reinterpret_cast<std::uintptr_t> (project));
    const auto* trackId = api.trackGuid (track);
    if (!trackId) return {};
    hash.append (trackId, 16); hash.number (count);
    for (const auto* key : { "I_PERFFLAGS", "I_RECARM", "I_RECMON" })
    {
        const auto value = api.trackInfo (track, key);
        if (!std::isfinite (value) || value < 0 || value > 0xffffffffu
            || !(value <= std::floor (value) && value >= std::floor (value))) return {};
        hash.number (static_cast<std::uint32_t> (value));
    }
    auto read = [&] (int fx, const char* key, std::string& out)
    {
        std::array<char, 1024> buffer; buffer.fill ('\x7f');
        if (!api.named (track, fx, key, buffer.data(), static_cast<int> (buffer.size()))) return false;
        const auto* end = static_cast<const char*> (std::memchr (buffer.data(), 0, buffer.size()));
        if (!end || end == buffer.data() + buffer.size() - 1) return false;
        out.assign (buffer.data(), static_cast<std::size_t> (end - buffer.data())); return true;
    };
    auto integer = [&] (int fx, const char* key, std::uint32_t& out)
    { std::string value; if (!read (fx, key, value) || !reaperUnsigned (value, out)) return false;
      hash.text (key); hash.text (value); return true; };
    int pre = -1, post = -1;
    std::uint32_t actual = 0;
    std::vector<std::array<unsigned char, 16>> ids;
    for (int i = 0; i < count; ++i)
    {
        const auto* guid = api.fxGuid (track, i);
        if (!guid) return {};
        std::array<unsigned char, 16> id {}; std::memcpy (id.data(), guid, id.size());
        for (const auto& seen : ids) if (seen == id) return {};
        ids.push_back (id); hash.append (id.data(), id.size());
        std::string type, identity, original, value;
        if (!read (i, "fx_type", type) || !read (i, "fx_ident", identity) || identity.empty()
            || !read (i, "original_name", original) || original.empty()) return {};
        // Only the actual VST3 class identifier names a role. Renamed FX labels never do.
        const bool isPre = type == "VST3" && identity.find ("{4B6972696E4879706861505245763031}") != std::string::npos;
        const bool isPost = type == "VST3" && identity.find ("{4B6972696E4879706861504F53547631}") != std::string::npos;
        if (isPre) { if (pre >= 0) return {}; pre = i; }
        if (isPost) { if (post >= 0) return {}; post = i; }
        if (type == "Container" || type == "ReWire" || type == "ReaInsert"
            || original.find ("ReaInsert") != std::string::npos) return reject (ReaperChainRestriction::routing);
        // container_count is absent for ordinary FX. A successful container/parent response
        // is not coerced to zero; this route has no container or nested-slot proof.
        if (read (i, "container_count", value) || read (i, "parent_container", value)) return reject (ReaperChainRestriction::routing);
        hash.text (type); hash.text (identity); hash.text (original);
        const bool on = api.enabled (track, i), off = api.offline (track, i);
        hash.number (on); hash.number (off);
        if ((isPre || isPost) && (!on || off)) return {};
        std::uint32_t pdc = 0, mode = 0, parallel = 0, instanceOversample = 0, chainOversample = 0;
        std::uint32_t delay = 0, reporting = 0;
        if (!integer (i, "pdc", pdc) || !integer (i, "chain_pdc_actual", delay)
            || !integer (i, "chain_pdc_reporting", reporting) || !integer (i, "chain_pdc_mode", mode)
            || !integer (i, "parallel", parallel) || !integer (i, "instance_oversample_shift", instanceOversample)
            || !integer (i, "chain_oversample_shift", chainOversample)) return {};
        if (mode != 1) return reject (ReaperChainRestriction::pdcMode);
        if (parallel != 0 || instanceOversample != 0 || chainOversample != 0) return reject (ReaperChainRestriction::routing);
        if (delay < reporting || delay > ReaperTimingLimits::delay || (i > 0 && delay != actual)) return {};
        actual = delay;
    }
    if (pre < 0 || post <= pre || api.count (track) != count || api.changes (project) != changes
        || (api.playing (project) & 3) != 1) return {};
    if (restriction) *restriction = ReaperChainRestriction::none;
    return { hash.a, hash.b, actual, hash.a != 0 && hash.b != 0 };
}
}
