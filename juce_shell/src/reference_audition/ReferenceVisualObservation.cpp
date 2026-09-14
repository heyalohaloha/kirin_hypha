#include "ReferenceVisualObservation.h"
#include "ReferenceVisualAudio.h"
#include <cmath>
namespace hypha::reference_audition
{
VisualObservation::VisualObservation (Binding callback, std::shared_ptr<ReferenceAnalysis> owner,
                                      juce::File runtimeRoot)
    : juce::Thread ("Reference view"), analysis(std::move(owner)),
      tonalRepository (std::move (runtimeRoot)), binding (std::move (callback))
{ formats.registerBasicFormats(); startThread (juce::Thread::Priority::low); }
VisualObservation::~VisualObservation()
{
    setPresented (false); signalThreadShouldExit(); notify(); stopThread (-1);
    clearMeters(); clearTonal(); admission.reset();
}
void VisualObservation::configure (double sampleRate, int channels)
{
    const int rate = std::isfinite (sampleRate) && sampleRate >= 8000.0 && sampleRate <= 768000.0
        ? int (std::llround (sampleRate)) : 0;
    const juce::ScopedLock lock (controlLock);
    if (configuredRate == rate && configuredChannels == channels) return;
    accepting.store (false, std::memory_order_release);
    configuredRate = rate; configuredChannels = channels;
    generation.fetch_add (1, std::memory_order_acq_rel);
}
void VisualObservation::setPresented (bool value)
{
    const juce::ScopedLock lock (controlLock);
    if (presented == value) return;
    presented = value;
    accepting.store (false, std::memory_order_release);
    generation.fetch_add (1, std::memory_order_acq_rel);
    if (!value) admission.reset();
}
void VisualObservation::pauseAdmission()
{
    const juce::ScopedLock lock (controlLock);
    paused = true; accepting.store (false, std::memory_order_release);
    generation.fetch_add (1, std::memory_order_acq_rel);
    admission.reset();
}
void VisualObservation::resumeObservation()
{
    const juce::ScopedLock lock (controlLock);
    paused = false;
}
void VisualObservation::enqueue(const float* pcm,int frames,int channels,std::int64_t position,std::uint64_t epoch,std::uint64_t discontinuity) noexcept
{
    if(!epoch || epoch!=inputGeneration() || frames<1 || frames>256 || channels<1 || channels>2) return;
    const auto write=writeIndex.load(std::memory_order_relaxed),next=(write+1)%queueSize;
    if(next==readIndex.load(std::memory_order_acquire)) { ++rtDiscontinuity; return; }
    auto& block=(*queue)[write]; block.position=position; block.frames=frames; block.channels=channels;
    block.discontinuity=discontinuity+rtDiscontinuity; block.generation=epoch;
    std::copy_n(pcm,size_t(frames*channels),block.pcm.data());
    writeIndex.store(next,std::memory_order_release);
}
std::shared_ptr<const VisualTimeline> VisualObservation::snapshot() const
{ const juce::ScopedLock lock (snapshotLock); return published; }
void VisualObservation::publish()
{
    ++timeline.revision; dirty = false;
    auto next = std::make_shared<const VisualTimeline> (timeline);
    const juce::ScopedLock lock (snapshotLock); published = std::move (next);
}
void VisualObservation::clearMeters()
{
    kirin_reference_visual_drop (aMeter); kirin_reference_visual_drop (bMeter);
    aMeter = bMeter = nullptr; expected = -1; completeBin = false; measuring = false;
}
void VisualObservation::clearTonal()
{
    kirin_reference_tonal_drop (tonalMeter); tonalMeter = nullptr;
    tonalExpected = -1; tonalDiscontinuity = 0;
    timeline.tonal = {}; timeline.tonalAvailable = false;
}
bool VisualObservation::resetMeters()
{
    clearMeters(); ++timeline.pass; dirty = true;
    aMeter = kirin_reference_visual_create (uint32_t (timeline.binding.hostRate), uint32_t (timeline.binding.channels));
    bMeter = kirin_reference_visual_create (uint32_t (timeline.binding.hostRate), uint32_t (timeline.binding.channels));
    return aMeter && bMeter;
}
bool VisualObservation::resetTonal()
{
    if (! tonalMeter)
        tonalMeter = kirin_reference_tonal_create (uint32_t (configuredRate), uint32_t (configuredChannels));
    else if (! kirin_reference_tonal_reset (tonalMeter))
    { clearTonal(); return false; }
    timeline.tonal = {}; timeline.tonalAvailable = false; dirty = true;
    return tonalMeter != nullptr;
}
void VisualObservation::consumeTonal (const Block& block)
{
    if (block.channels != configuredChannels)
    { clearTonal(); dirty = true; return; }
    if (! tonalMeter || block.position != tonalExpected || block.discontinuity != tonalDiscontinuity)
        if (! resetTonal()) return;
    tonalDiscontinuity = block.discontinuity;
    if (! kirin_reference_tonal_push (tonalMeter, block.pcm.data(), size_t (block.frames * block.channels))
        || ! kirin_reference_tonal_snapshot (tonalMeter, &timeline.tonal))
    { clearTonal(); dirty = true; return; }
    tonalExpected = block.position + block.frames;
    timeline.tonalAvailable = timeline.tonal.valid_bits != 0;
    dirty = true;
}
void VisualObservation::consumePair (const Block& block)
{
    const auto& map = timeline.binding;
    std::int64_t position = 0;
    if (!map.mapPosition (block.position, position)) { clearMeters(); return; }
    const auto end = VisualTimeline::outputSample (map.source->audio.totalSampleFrames,
        map.source->audio.sampleRateHz, map.hostRate);
    if (position < 0 || position >= end || block.channels != map.channels) { clearMeters(); return; }
    if (position != expected || block.discontinuity != previousDiscontinuity || !aMeter)
        if (!resetMeters()) return;
    previousDiscontinuity = block.discontinuity;
    int offset = 0;
    const auto limit = int (juce::jmin<std::int64_t> (block.frames, end - position));
    while (offset < limit)
    {
        const auto at = position + offset;
        auto index = size_t (static_cast<long double> (at) * map.source->audio.sampleRateHz / map.hostRate / timeline.hop);
        while (index + 1 < timeline.bins.size() && timeline.boundary (index + 1) <= at) ++index;
        if (index >= timeline.bins.size()) break;
        const auto start = timeline.boundary (index), finish = juce::jmin (end, timeline.boundary (index + 1));
        const int count = int (juce::jmin<std::int64_t> (limit - offset, finish - at));
        if (count < 1) break;
        if (at == start) { completeBin = true; measuring = true; }
        if (!measuring) { offset += count; continue; }
        const auto pcmOffset = size_t (offset * map.channels), sampleCount = size_t (count * map.channels);
        if (!kirin_reference_visual_push (aMeter, block.pcm.data() + pcmOffset, sampleCount)
            || !kirin_reference_visual_push (bMeter, bPcm.data() + pcmOffset, sampleCount))
        { clearMeters(); return; }
        if (at + count == finish)
        {
            VisualPairBin bin;
            if (kirin_reference_visual_finish (aMeter, &bin.a) && kirin_reference_visual_finish (bMeter, &bin.b)
                && completeBin && bin.a.frames == uint64_t (finish - start) && bin.b.frames == bin.a.frames)
            { bin.pass = timeline.pass; timeline.bins[index] = bin; dirty = true; }
            completeBin = false;
        }
        offset += count;
    }
    expected = position + limit;
}
void VisualObservation::run()
{
    double nextPublish = 0;
    juce::String readerKey;
    std::uint64_t workerGeneration = 0;
    double nextRevisionCheck = 0.0; bool sourceUnchanged = false; juce::String checkedKey;
    double nextTonalCheck = 0.0; juce::String tonalPublicationKey;
    while (!threadShouldExit())
    {
        bool visible = false; int rate = 0, channels = 0;
        { const juce::ScopedLock lock (controlLock); visible = presented; rate = configuredRate; channels = configuredChannels; }
        if (!visible)
        {
            if (timeline.observing || timeline.pairedObserving)
            { timeline.observing = timeline.pairedObserving = false; dirty = true; clearMeters(); clearTonal(); }
            if (dirty) publish();
            readIndex.store (writeIndex.load (std::memory_order_acquire), std::memory_order_release);
            wait (100); continue;
        }
        ReferenceAnalysis::Lease job;
        { const juce::ScopedLock lock(controlLock); job=admission; }
        const auto requestedGeneration=generation.load(std::memory_order_acquire);
        auto next = binding(); // No admission/control lock while consulting the runtime.
        const auto checkedAt = juce::Time::getMillisecondCounterHiRes();
        if (!next.hidden && next.source && (checkedAt >= nextRevisionCheck || checkedKey != next.key))
        {
            RuntimeV2SourceRepository verifier (juce::File {});
            sourceUnchanged = verifier.verifySourceRevision (*next.source).isEmpty();
            checkedKey = next.key; nextRevisionCheck = checkedAt + 100.0;
        }
        next.aligned = next.aligned && sourceUnchanged;
        juce::String observedTonalKey = tonalPublicationKey;
        bool tonalPublicationChanged = false;
        if (checkedAt >= nextTonalCheck)
        {
            observedTonalKey = tonalRepository.publicationKey();
            tonalPublicationChanged = observedTonalKey != tonalPublicationKey;
            nextTonalCheck = checkedAt + 1000.0;
        }
        const bool bindingChanged = timeline.binding.key != next.key
            || timeline.binding.aligned != next.aligned;
        auto nextTonalReference = timeline.tonalReference;
        auto nextTonalGenre = timeline.tonalGenre;
        if (bindingChanged || tonalPublicationChanged)
        {
            nextTonalReference = !next.hidden && next.source
                ? tonalRepository.load (*next.source, next.cueStartSample, next.cueEndSample) : nullptr;
            nextTonalGenre = !next.hidden
                ? tonalRepository.loadGenre (next.presetId, next.presetRevisionId, next.checkId) : nullptr;
        }
        bool wanted = false, pairWanted = false;
        {
            const juce::ScopedLock lock (controlLock);
            if(requestedGeneration!=generation.load(std::memory_order_acquire)) continue;
            wanted = presented && !paused && rate >= 40000 && rate <= 768000
                && channels >= 1 && channels <= 2;
            pairWanted = wanted && !next.hidden && next.aligned && next.source && next.overview
                && next.overview->waveform && next.hostRate >= 8000 && next.hostRate <= 768000;
            if (bindingChanged)
            {
                clearMeters(); timeline.binding = next; timeline.bins.clear(); timeline.hop = 0;
                ++timeline.pass; dirty = true;
                if (next.overview && next.overview->waveform && next.source)
                {
                    timeline.hop = next.overview->waveform->framesPerBin;
                    const auto count = timeline.hop > 0 ? (next.source->audio.totalSampleFrames + timeline.hop - 1) / timeline.hop : 0;
                    if (count > 0 && count <= 2048) timeline.bins.resize (size_t (count));
                }
            }
            else timeline.binding = next;
            if (bindingChanged || tonalPublicationChanged)
            {
                timeline.tonalReference = std::move (nextTonalReference);
                timeline.tonalGenre = std::move (nextTonalGenre);
                tonalPublicationKey = observedTonalKey; dirty = true;
            }
            pairWanted = pairWanted && !timeline.bins.empty();
            if (!wanted) admission.reset();
            if(!analysis->current(admission)) admission.reset();
            if(wanted && !admission) admission=analysis->acquire();
            const bool observing = wanted && bool(admission);
            dirty = dirty || timeline.observing != observing;
            timeline.observing = observing;
            dirty = dirty || timeline.pairedObserving != (pairWanted && observing);
            timeline.pairedObserving = pairWanted && observing;
            if (accepting.exchange (timeline.observing, std::memory_order_acq_rel) && !timeline.observing)
                generation.fetch_add (1, std::memory_order_acq_rel);
            if (workerGeneration != generation.load (std::memory_order_acquire))
            { clearMeters(); clearTonal(); ++timeline.pass; dirty = true; workerGeneration = generation.load (std::memory_order_acquire); }
        }
        if(!job) { const juce::ScopedLock lock(controlLock); job=admission; }
        const auto read = readIndex.load (std::memory_order_relaxed);
        if (read != writeIndex.load (std::memory_order_acquire))
        {
            const auto& block = (*queue)[read];
            const auto epoch = generation.load (std::memory_order_acquire);
            bool decoded = false;
            if (timeline.observing && analysis->current(job) && block.generation == epoch)
            {
                consumeTonal (block);
                if (timeline.pairedObserving)
                {
                    if (readerKey != next.key)
                    { reader.reset (formats.createReaderFor (juce::File (next.source->absolutePath))); readerKey = next.key; }
                    RuntimeV2SourceRepository verifier (juce::File {});
                    std::int64_t position = 0;
                    if (reader && next.mapPosition (block.position, position) && verifier.verifySourceRevision (*next.source).isEmpty())
                    {
                        decoded = true;
                        for (int offset = 0; offset < block.frames && decoded; offset += 256)
                        {
                            const auto frames = juce::jmin (256, block.frames-offset);
                            decoded = readReferenceVisualAudio (*reader, position+offset, frames, int (next.hostRate),
                                next.channels, bAudio, scratch, [this, epoch, &job] (const auto& convert) {
                                    const juce::ScopedLock lock (controlLock);
                                    if (!analysis->current(job) || !presented || paused || epoch != generation.load (std::memory_order_acquire)) return false;
                                    convert(); return true;
                                });
                            if (decoded) for (int c=0; c<next.channels; ++c) for (int i=0; i<frames; ++i)
                                bPcm[size_t ((offset+i)*next.channels+c)] = bAudio.getSample (c,i);
                        }
                        decoded = decoded && verifier.verifySourceRevision (*next.source).isEmpty();
                    }
                }
            }
            if (decoded && analysis->current(job) && block.generation == generation.load (std::memory_order_acquire)) consumePair (block);
            else if (timeline.pairedObserving) clearMeters();
            readIndex.store ((read + 1) % queueSize, std::memory_order_release);
        }
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (dirty && now >= nextPublish) { publish(); nextPublish = now + 100.0; }
        job.reset();
        if (read == writeIndex.load (std::memory_order_acquire)) wait (next.hidden ? 100 : 10);
    }
}
}
