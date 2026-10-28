#pragma once

namespace hypha::tests::reference_control
{
struct MappedPositionTag {};
std::int64_t mappedPositionFor (Runtime&, std::int64_t, MappedPositionTag);
template <auto Member> struct MappedPositionAccess
{
    friend std::int64_t mappedPositionFor (Runtime& r, std::int64_t host, MappedPositionTag)
    { return (r.*Member) (host); }
};
template struct MappedPositionAccess<&Runtime::mappedSourcePosition>;
struct FixturePositionTag {};
void pinFixturePosition (Runtime&, std::int64_t, std::int64_t, FixturePositionTag);
template <auto Lock, auto Generation, auto Host, auto Source> struct FixturePositionAccess
{
    friend void pinFixturePosition (Runtime& r, std::int64_t host, std::int64_t source, FixturePositionTag)
    {
        const juce::ScopedLock lock (r.*Lock);
        (r.*Generation).fetch_add (1, std::memory_order_acq_rel);
        (r.*Host).store (host - source, std::memory_order_relaxed);
        (r.*Source).store (0, std::memory_order_relaxed);
        (r.*Generation).fetch_add (1, std::memory_order_release);
    }
};
template struct FixturePositionAccess<&Runtime::mappingWriteLock, &Runtime::mappingGeneration,
    &Runtime::bHostAnchor, &Runtime::bSourceAnchor>;
struct ReaderTag {};
std::unique_ptr<juce::AudioFormatReader>& readerFor (reference_audition::AudioPages&, ReaderTag);
template <auto Member> struct ReaderAccess
{
    friend std::unique_ptr<juce::AudioFormatReader>& readerFor (reference_audition::AudioPages& p, ReaderTag)
    { return p.*Member; }
};
template struct ReaderAccess<&reference_audition::AudioPages::reader>;
}

class UnreadablePageReader final : public juce::AudioFormatReader
{
public:
    explicit UnreadablePageReader (const juce::AudioFormatReader& source)
        : juce::AudioFormatReader (nullptr, "unreadable fixture")
    {
        sampleRate = source.sampleRate; numChannels = source.numChannels;
        lengthInSamples = source.lengthInSamples; bitsPerSample = 32; usesFloatingPointData = true;
    }
    bool readSamples (int* const*, int, int, juce::int64, int) override { return false; }
};

inline void testReferencePageWaitFailure (
    hypha::tests::reference_control::Comparison& c, hypha::tests::reference_control::Runtime& runtime,
    ref::AudioPages& pages, const juce::File& root, const juce::AudioBuffer<float>& source, int cursor, bool switching, PageWaitChange change)
{
    namespace control = hypha::tests::reference_control;
    const bool outside = change == PageWaitChange::negative || change == PageWaitChange::atEnd
        || change == PageWaitChange::pastEnd;
    const auto position = outside ? change == PageWaitChange::negative ? std::int64_t (-1)
        : pages.lengthInSamples() + (change == PageWaitChange::pastEnd ? 256 : 0) : std::int64_t (cursor);
    const auto previousPosition = control::mappedPositionFor (runtime, cursor, {});
    if (!switching) c.selectA();
    juce::AudioBuffer<float> input (2, 256);
    for (int ch = 0; ch < 2; ++ch) input.copyFrom (ch, 0, source, ch, cursor, 256);
    c.observeTransport (cursor, true, true);
    c.observeAInput (input, cursor, true, true, true);
    // Pin a stable published map after a complete, contiguous A observation.
    // The separate seek case exercises the real offset and its interrupted publication.
    if (outside) control::pinFixturePosition (runtime, cursor, position, {});
    require (control::mappedPositionFor (runtime, cursor, {}) == position, "the boundary fixture publishes the requested source position");
    if (!switching) c.requestAudition (1, -22, -6);
    c.servicePendingAudition (-22, -6, true, 100.0);
    const auto initial = control::pageIntentFactsFor (c, {});
    const auto print = [&] (const char* phase)
    {
        const auto s = c.snapshot();
        std::cout << "PAGE_WAIT phase=" << phase << " switching=" << switching << " condition=" << int (change)
                  << " position=" << position << " stage=" << int (s.pendingAudition.stage)
                  << " waiting=" << s.pendingAudition.waiting() << " reason=" << s.pendingAudition.reasonCode << std::endl;
    };
    const auto terminal = [&]
    {
        const auto s = c.snapshot(); const auto facts = control::pageIntentFactsFor (c, {});
        require (!s.pendingAudition.waiting() && s.pendingAudition.stage != ref::PendingAuditionView::Stage::none
                     && facts.active == 0 && facts.switchingSlot == 0 && !c.pendingAuditionNeedsService(),
                 "unusable pages consume the ordinary pending intent and switch");
        require (s.pendingAudition.reasonCode.isNotEmpty(), "a terminal page failure preserves its reason");
    };
    if (outside)
    {
        print ("outside"); terminal();
        require (c.snapshot().pendingAudition.reasonCode == "reference_position_outside_source",
                 "a stable position outside V has its own terminal reason");
    }
    else
    {
        require (initial.active != 0 && c.snapshot().pendingAudition.waiting(), "in-range page wait starts armed");
        if (change == PageWaitChange::slowPages)
        {
            c.servicePendingAudition (-22, -6, true, 102.999);
            require (c.snapshot().pendingAudition.reasonCode != "reference_page_wait_slow", "no slow-I/O reason before three seconds");
            c.servicePendingAudition (-22, -6, true, 103.0); print ("three-seconds");
            require (c.snapshot().pendingAudition.reasonCode == "reference_page_wait_slow", "three seconds exposes a page-specific recovery reason");
            for (double now : { 103.001, 160.0 })
            {
                c.servicePendingAudition (-22, -6, true, now);
                const auto facts = control::pageIntentFactsFor (c, {});
                require (c.snapshot().pendingAudition.waiting() && facts.active == initial.active
                             && facts.switchingSlot == initial.switchingSlot,
                         "slow I/O preserves the same choice beyond the display boundary");
            }
            pages.request (cursor); control::servicePages (pages, {});
            c.servicePendingAudition (-22, -6, true, 160.001);
            require (c.snapshot().bSelected, "late refill starts without a second press");
            c.selectA(); return;
        }
        if (change == PageWaitChange::readFailure)
        {
            auto& reader = control::readerFor (pages, {});
            auto original = std::move (reader);
            reader = std::make_unique<UnreadablePageReader> (*original);
            pages.request (cursor);
            for (int attempt = 1; attempt <= 3; ++attempt)
            {
                control::servicePages (pages, {});
                c.servicePendingAudition (-22, -6, true, 100.0 + attempt);
                if (attempt < 3) require (c.snapshot().pendingAudition.waiting(), "isolated read errors remain retryable");
            }
            reader = std::move (original);
            print ("read-failed"); terminal();
            require (c.snapshot().pendingAudition.reasonCode == "reference_audio_read_failed", "repeated decoder failure has a terminal reason");
        }
        else
        {
            const juce::File file (runtime.visualBinding().source->absolutePath);
            require (file.isAChildOf (root), "only an isolated fixture source may be withdrawn");
            const auto held = file.getSiblingFile (file.getFileName() + ".held");
            // Release the stopped fixture decoder before a Windows rename.
            pages.close();
            require (file.moveFileTo (held), "withdraw the fixture source");
            c.servicePendingAudition (-22, -6, true, 101.0);
            require (held.moveFileTo (file), "restore the isolated fixture file");
            require (pages.open (*runtime.visualBinding().source, 48000, 2, false).isEmpty(),
                     "the restored fixture source can be decoded again");
            control::stopPageRefill (pages);
            print ("source-withdrawn"); terminal();
        }
    }
    if (outside) control::pinFixturePosition (runtime, cursor, previousPosition, {});
    for (int ch = 0; ch < 2; ++ch) input.copyFrom (ch, 0, source, ch, cursor + 256, 256);
    c.observeTransport (cursor + 256, true, true);
    c.observeAInput (input, cursor + 256, true, true, true);
    pages.request (cursor + 256); control::servicePages (pages, {});
    for (int n = 0; n < 3; ++n) c.servicePendingAudition (-22, -6, true, 170.0 + n);
    require (!c.snapshot().bSelected && control::pageIntentFactsFor (c, {}).active == 0,
             "moving back or repairing the reader cannot resurrect a terminal intent");
    c.selectA();
}
