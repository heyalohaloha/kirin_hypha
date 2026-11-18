#pragma once
#include "kirin_hypha_ffi.h"
#include "kirin_hypha_reference_live_metrics.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace hypha::blind_details
{
// Message thread only; no IO, demand acquisition, RT publication or new measurement.
// begin at accepted BLIND start, prime with the then-current snapshots, freeze at REVEAL.
// A missing initial snapshot makes the first later snapshot a baseline, never evidence.
struct Summary
{
    double median = std::numeric_limits<double>::quiet_NaN();
    double seconds = 0;
    std::size_t observations = 0;
    bool sufficient = false, capacityExceeded = false;
};
class Series
{
public:
    static constexpr std::size_t capacity = 6000; // bounded per-trial memory, not a sliding median
    void add (double value, std::uint64_t frames)
    {
        if (!std::isfinite (value) || frames == 0 || overflow) return;
        if (values.size() == capacity) { overflow = true; return; }
        values.push_back (value); covered += frames;
    }
    Summary summary (std::uint32_t rate) const
    {
        Summary result;
        result.seconds = rate ? static_cast<double> (covered) / rate : 0;
        result.observations = values.size(); result.capacityExceeded = overflow;
        result.sufficient = rate != 0 && covered >= std::uint64_t (rate) * 3 && !overflow;
        if (!result.sufficient || values.empty()) return result;
        auto ordered = values; std::sort (ordered.begin(),ordered.end());
        const auto middle = ordered.size()/2;
        result.median = ordered.size()%2 ? ordered[middle] : ordered[middle-1]*.5 + ordered[middle]*.5;
        return result;
    }
private:
    std::vector<double> values;
    std::uint64_t covered = 0;
    bool overflow = false;
};
struct Key
{
    std::uint64_t epoch = 0, generation = 0;
    std::uint32_t rate = 0, channels = 0, mode = 0, aperture = 0;
    bool operator== (const Key& other) const noexcept
    { return epoch==other.epoch && generation==other.generation && rate==other.rate
        && channels==other.channels && mode==other.mode && aperture==other.aperture; }
};
struct Cursor
{
    Key key;
    std::uint64_t last = 0;
    bool primed = false;
    void prime (Key next, std::uint64_t end) { key=next; last=end; primed=true; }
    std::uint64_t advance (std::uint64_t end)
    {
        if (!primed || end<=last) return 0;
        const auto frames = std::min<std::uint64_t> (end-last,key.aperture);
        last=end; return frames;
    }
};
struct Result
{
    Summary psr, crest, sharp;
    std::array<Summary,KIRIN_SPECTRUM_BAND_COUNT> frequency;
    float minHz = 0, maxHz = 0;
    std::uint32_t frequencyMode = 0;
    bool hasFrequency = false;
};
class Collector
{
public:
    void begin() { clear(); collecting=true; }
    void clear() { *this = Collector {}; }
    void freeze() noexcept { collecting=false; }
    bool active() const noexcept { return collecting; }
    void level (const KirinReferenceLiveMetricFrame& f)
    {
        if (!collecting || f.version!=1 || f.struct_size!=sizeof f || !format(f.sample_rate,f.channels)) return;
        const Key key {f.measurement_epoch,f.generation,f.sample_rate,f.channels,0,f.sample_rate/10};
        if (!levelCursor.primed || !(levelCursor.key==key))
        { crest=Series {}; psr=Series {}; levelCursor.prime(key,f.observed_frames); return; }
        const auto frames=levelCursor.advance(f.observed_frames);
        if ((f.valid_mask&5u)==5u) crest.add(f.own_crest_db-f.pre_crest_db,frames);
        if ((f.valid_mask&10u)==10u) psr.add(f.own_psr_db-f.pre_psr_db,frames);
    }
    void perceptual (const KirinPerceptualBatch& batch)
    {
        if (!collecting || batch.count>KIRIN_PERCEPTUAL_BATCH_CAPACITY) return;
        const auto& f=batch.latest;
        if (!sharpValid(f)) return;
        const Key key=sharpKey(f);
        if (!sharpCursor.primed || !(sharpCursor.key==key))
        { sharp=Series {}; sharpCursor.prime(key,static_cast<std::uint64_t>(f.presentation_end_samples)); return; }
        for (std::uint32_t i=0;i<batch.count;++i)
        {
            const auto& v=batch.frames[i];
            if (!sharpValid(v) || !(sharpKey(v)==key)) continue;
            const auto frames=sharpCursor.advance(static_cast<std::uint64_t>(v.presentation_end_samples));
            if (std::isfinite(v.pre_sharpness) && std::isfinite(v.post_sharpness)) sharp.add(v.delta_sharpness,frames);
        }
        // Some callers supply only latest; duplicates in the recovery batch count once.
        const auto frames=sharpCursor.advance(static_cast<std::uint64_t>(f.presentation_end_samples));
        if (std::isfinite(f.pre_sharpness) && std::isfinite(f.post_sharpness)) sharp.add(f.delta_sharpness,frames);
    }
    // Feed the existing FREQ batch together with pollSpectrumBatch's delta continuity.
    // Keep its original 256 bands. Never invent a coarse band partition.
    void spectrum (const KirinSpectrumBatch& batch, std::uint64_t continuity)
    {
        if (!collecting || batch.count>KIRIN_SPECTRUM_BATCH_CAPACITY) return;
        const auto& f=batch.latest;
        if (!spectrumValid(f)) return;
        const Key key {continuity,0,f.sample_rate,f.channels,f.channel_mode,f.aperture_samples};
        if (!frequencyCursor.primed || !(frequencyCursor.key==key) || minHz!=f.min_hz || maxHz!=f.max_hz)
        {
            frequency={}; minHz=f.min_hz; maxHz=f.max_hz;
            frequencyCursor.prime(key,static_cast<std::uint64_t>(f.presentation_end_samples)); return;
        }
        const auto add=[&](const KirinSpectrumView& v)
        {
            if (!spectrumValid(v) || v.sample_rate!=key.rate || v.channels!=key.channels
                || v.channel_mode!=key.mode || v.aperture_samples!=key.aperture || v.min_hz!=minHz || v.max_hz!=maxHz) return;
            const auto frames=frequencyCursor.advance(static_cast<std::uint64_t>(v.presentation_end_samples));
            for (std::size_t i=0;i<frequency.size();++i)
                if (std::isfinite(v.pre_dbfs[i]) && std::isfinite(v.post_dbfs[i])) frequency[i].add(v.display_db[i],frames);
        };
        for (std::uint32_t i=0;i<batch.count;++i) add(batch.frames[i]);
        add(f);
    }
    // Compute once at REVEAL (copies/sorts), not in the repaint/timer hot path.
    Result result() const
    {
        Result out; out.psr=psr.summary(levelCursor.key.rate); out.crest=crest.summary(levelCursor.key.rate);
        out.sharp=sharp.summary(sharpCursor.key.rate);
        out.minHz=minHz; out.maxHz=maxHz; out.frequencyMode=frequencyCursor.key.mode;
        for (std::size_t i=0;i<frequency.size();++i)
        { out.frequency[i]=frequency[i].summary(frequencyCursor.key.rate); out.hasFrequency|=out.frequency[i].sufficient; }
        return out;
    }
private:
    static bool format(std::uint32_t rate,std::uint32_t channels)
    { return rate>=8000 && rate<=384000 && rate%10==0 && (channels==1 || channels==2); }
    static bool sharpValid(const KirinPerceptualView& f)
    { return f.status==KIRIN_SPECTRUM_ACTIVE && f.has_data && format(f.sample_rate,f.channels)
        && f.aperture_samples>0 && f.aperture_samples<=f.sample_rate
        && f.presentation_end_samples>=0; }
    static Key sharpKey(const KirinPerceptualView& f)
    { return {static_cast<std::uint64_t>(f.state_epoch_samples),0,f.sample_rate,f.channels,f.channel_mode,f.aperture_samples}; }
    static bool spectrumValid(const KirinSpectrumView& f)
    { return f.status==KIRIN_SPECTRUM_ACTIVE && f.has_data && format(f.sample_rate,f.channels)
        && f.aperture_samples>0 && f.aperture_samples<=f.sample_rate && f.presentation_end_samples>=0
        && std::isfinite(f.min_hz) && std::isfinite(f.max_hz) && f.min_hz>0 && f.max_hz>f.min_hz; }
    bool collecting=false;
    Cursor levelCursor,sharpCursor,frequencyCursor;
    Series psr,crest,sharp;
    std::array<Series,KIRIN_SPECTRUM_BAND_COUNT> frequency;
    float minHz=0,maxHz=0;
};
}
