#pragma once
#include "../src/HyphaSpaceLiveDisplay.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaMaterialCache.h"
#include "../src/HyphaTextStyle.h"
#include <cstring>
#include <iostream>
#include <future>
#include <set>
#include <thread>

namespace hypha::tests::space_live_contract
{
inline void require (bool ok, const char* why)
{ if (! ok) { std::cerr << "SPACE live: " << why << '\n'; std::exit (1); } }
struct DisplayMember
{
    using Type = space_live::Display observatory::View::*;
    friend Type displayMember (DisplayMember);
};
template <typename Tag, typename Tag::Type Member> struct MemberAccess
{ friend typename Tag::Type displayMember (Tag) { return Member; } };
template struct MemberAccess<DisplayMember, &observatory::View::spaceLive>;
inline space_live::Display& display (observatory::View& view)
{ return view.*displayMember (DisplayMember {}); }
inline bool sameSample (float a, float b) { return std::memcmp (&a, &b, sizeof (a)) == 0; }
inline std::vector<float> signal (int frames, double phase = 0.0)
{
    std::vector<float> data (static_cast<std::size_t> (frames * 2));
    for (int i = 0; i < frames; ++i)
    {
        data[static_cast<std::size_t> (i * 2)] = float (0.42 * std::sin (i * 0.031 + phase));
        data[static_cast<std::size_t> (i * 2 + 1)] = float (0.31 * std::sin (i * 0.047 + phase * 0.3));
    }
    return data;
}
// Reach the shared read transaction without adding a callback or wait to the audio path.
struct ObservedReadMember
{
    using Observer = std::function<void (std::size_t)>;
    using Type = space_live::Read (space_live::LiveTap::*) (space_live::Snapshot&, std::uint64_t, const Observer&) const;
    friend Type observedReadMember (ObservedReadMember);
};
template <typename Tag, typename Tag::Type Member> struct ReadAccess
{ friend typename Tag::Type observedReadMember (Tag) { return Member; } };
template struct ReadAccess<ObservedReadMember,
    &space_live::LiveTap::readObserved<ObservedReadMember::Observer>>;
inline void verifyConcurrentReads()
{
    space_live::LiveTap tap; tap.prepare (48'000); tap.setEnabled (true);
    space_live::Snapshot frame;
    static constexpr std::size_t block = 512, window = 3840;
    std::size_t publications = 0, ready = 0, rejected = 0;
    const auto publish = [&] {
        std::array<float, block * 2> data {};
        const auto start = publications * block;
        for (std::size_t i = 0; i < block; ++i)
        { data[i * 2] = float (start + i + 1); data[i * 2 + 1] = -data[i * 2]; }
        tap.push (data.data(), int (block), 2, true, true, std::int64_t (start), 0);
        ++publications;
    };
    const auto verify = [&] {
        require (tap.read (frame, 0) == space_live::Read::ready, "parallel transaction must permit successful public reads");
        ++ready;
        const auto end = publications * block;
        const auto count = std::min (end, window);
        require (frame.revision == 2 + publications * 2 && frame.span == 2 && frame.sampleRate == 48'000
                 && frame.samples.size() == count, "revision, span and window endpoint belong to the same publication");
        for (std::size_t i = 0; i < count; ++i)
            require (sameSample (frame.samples[i].left, float (end - count + i + 1))
                     && sameSample (frame.samples[i].right, -float (end - count + i + 1)),
                     "every sample follows the exact stream order through ring wrap and partial windows");
    };
    publish(); verify();
    for (int round = 0; round < 32; ++round)
    {
        std::promise<void> copying, written;
        auto copyStarted = copying.get_future(); auto writeFinished = written.get_future();
        std::thread writer ([&] {
            copyStarted.wait(); // Test coordination only; push itself never waits.
            publish(); written.set_value();
        });
        const auto midpoint = frame.samples.size() / 2;
        const ObservedReadMember::Observer overlap = [&] (std::size_t i) {
            if (i == midpoint) { copying.set_value(); writeFinished.wait(); }
        };
        const auto result = (tap.*observedReadMember (ObservedReadMember {})) (frame, 0, overlap);
        writer.join();
        require (result == space_live::Read::busy, "publication inside the sample copy must reject the entire window");
        ++rejected; verify();
    }
    require (ready == 33 && rejected == 32, "successful reads and synchronized overlapping writes are mandatory");
}
inline void verifyBurstExpiry()
{
    for (const auto gap : { 120'000'000ull, 120'000'001ull, 150'000'000ull })
        for (const bool readDuringGap : { false, true })
        {
            space_live::LiveTap tap; tap.prepare (48'000); tap.setEnabled (true);
            space_live::Snapshot frame;
            static constexpr int block = 512;
            constexpr std::uint64_t last = 1'007'000'000;
            const auto publish = [&] (int index, std::uint64_t nanos) {
                std::array<float, block * 2> data {};
                for (int i = 0; i < block; ++i)
                { data[std::size_t (i * 2)] = float (index * block + i + 1); data[std::size_t (i * 2 + 1)] = -data[std::size_t (i * 2)]; }
                tap.push (data.data(), block, 2, true, true, index * block, nanos);
            };
            for (int i = 0; i < 8; ++i) publish (i, 1'000'000'000 + std::uint64_t (i) * 1'000'000);
            require (tap.read (frame, last) == space_live::Read::ready && frame.samples.size() == 3840,
                     "ahead-of-time callbacks fill the live window");
            const auto previous = frame.span;
            const bool expired = gap > 120'000'000ull;
            if (readDuringGap)
                require (tap.read (frame, last + gap) == (expired ? space_live::Read::empty : space_live::Read::ready),
                         "trace expiry has an inclusive lifetime boundary");
            publish (8, last + gap);
            require (tap.read (frame, last + gap) == space_live::Read::ready
                     && frame.samples.size() == (expired ? 512u : 3840u) && (frame.span != previous) == expired,
                     "burst recovery resets an expired span with or without an intervening reader");
            const auto start = 9 * block - int (frame.samples.size());
            for (std::size_t i = 0; i < frame.samples.size(); ++i)
                require (sameSample (frame.samples[i].left, float (start + int (i) + 1))
                         && sameSample (frame.samples[i].right, -float (start + int (i) + 1)),
                         "every recovered sample belongs to the accepted span in exact stream order");
        }
}
// Pause an actual publication to keep every UI read busy without sleeps or thread races.
struct BeginWriteMember
{
    using Type = void (space_live::LiveTap::*)();
    friend Type displayMember (BeginWriteMember);
};
template struct MemberAccess<BeginWriteMember, &space_live::LiveTap::begin>;
struct EndWriteMember
{
    using Type = void (space_live::LiveTap::*) (std::uint64_t);
    friend Type displayMember (EndWriteMember);
};
template struct MemberAccess<EndWriteMember, &space_live::LiveTap::publish>;
struct TimerMember
{
    using Type = void (space_live::Display::*)();
    friend Type displayMember (TimerMember);
};
template struct MemberAccess<TimerMember, &space_live::Display::timerCallback>;
inline void tick (space_live::Display& screen) { (screen.*displayMember (TimerMember {}))(); }
inline void verifyBusyExpiry()
{
    for (const int frames : { 64, 48'000 })
    {
        space_live::LiveTap tap; tap.prepare (48'000);
        juce::Component owner; owner.setVisible (true);
        space_live::Display screen (owner, [] {}); screen.setSource (&tap); screen.setPresented (true);
        auto data = signal (frames);
        constexpr std::uint64_t stamp = 1'000'000'000;
        tap.push (data.data(), frames, 2, true, true, 0, stamp);
        require (screen.refresh (stamp) && screen.frame().callbackNanos == stamp,
                 "accepted display owns its callback timestamp");
        const auto lifetime = frames == 64 ? 120'000'000ull : 2'020'000'000ull;
        require (screen.frame().lifetimeNanos == lifetime, "accepted lifetime respects large host blocks");
        (tap.*displayMember (BeginWriteMember {}))();
        space_live::Snapshot rejected;
        require (tap.read (rejected, stamp + lifetime + 1) == space_live::Read::busy, "writer remains busy beyond expiry");
        require (! screen.refresh (stamp + lifetime / 2) && ! screen.refresh (stamp + lifetime)
                 && ! screen.frame().samples.empty(), "busy retains accepted points through the inclusive deadline");
        require (screen.refresh (stamp + lifetime + 1) && screen.frame().samples.empty(),
                 "busy independently expires and invalidates the accepted trace");
        require (! screen.refresh (stamp + lifetime + 16'000'000) && screen.frame().samples.empty()
                 && tap.enabled() && screen.running(), "continued contention stays empty without ending demand");
        (tap.*displayMember (EndWriteMember {})) (stamp);
        auto fresh = signal (64, 1.2);
        tap.push (fresh.data(), 64, 2, true, true, frames, stamp + lifetime + 20'000'000);
        require (screen.refresh (stamp + lifetime + 20'000'000) && screen.frame().samples.size() == 64,
                 "first fresh callback automatically recovers after busy expiry");
        for (std::size_t i = 0; i < 64; ++i)
            require (sameSample (screen.frame().samples[i].left, fresh[i * 2])
                     && sameSample (screen.frame().samples[i].right, fresh[i * 2 + 1]), "busy recovery has only new PCM");
    }
}
inline void verifyClockReadRace()
{
    space_live::LiveTap tap; tap.prepare (48'000);
    juce::Component owner; owner.setVisible (true);
    space_live::Display screen (owner, [] {}); screen.setSource (&tap); screen.setPresented (true);
    const auto data = signal (64);
    constexpr std::uint64_t stamp = 1'000'000'000, sampled = stamp + 1'000'000, published = sampled + 1;
    tap.push (data.data(), 64, 2, true, true, 0, stamp);
    require (screen.refresh (stamp), "clock race starts with an accepted frame");
    const auto revision = screen.frame().revision;
    // UI samples t0, then the callback publishes at t1, then UI reads. Neither clock is zero.
    const auto fresh = signal (64, 1.2);
    tap.push (fresh.data(), 64, 2, true, true, 64, published);
    space_live::Snapshot frame;
    require (tap.read (frame, sampled) == space_live::Read::busy, "newer callback is clock contention, not an empty trace");
    require (! screen.refresh (sampled) && screen.frame().revision == revision && screen.frame().samples.size() == 64,
             "a callback after the UI clock sample preserves every accepted point");
    require (screen.refresh (published) && screen.frame().samples.size() == 128
             && screen.frame().callbackNanos == published, "next frame automatically admits the newer publication");
    for (std::size_t i = 0; i < 64; ++i)
        require (sameSample (screen.frame().samples[i].left, data[i * 2])
                 && sameSample (screen.frame().samples[64 + i].right, fresh[i * 2 + 1]), "clock race keeps exact ordered PCM");
    require (screen.refresh (published + 120'000'001) && screen.frame().samples.empty(),
             "clock contention does not disable callback expiry");
    // With no accepted frame, the race stays empty until its first verified publication.
    tap.setEnabled (false); tap.setEnabled (true);
    tap.push (data.data(), 64, 2, true, true, 128, published + 1);
    require (! screen.refresh (published) && screen.frame().samples.empty(), "first-frame contention invents no points");
    require (screen.refresh (published + 1) && screen.frame().samples.size() == 64,
             "first-frame contention recovers without changing selection or demand");
}
inline void verifyTap()
{
    space_live::LiveTap tap; space_live::Snapshot frame;
    auto data = signal (512);
    tap.prepare (48'000);
    tap.push (data.data(), 512, 2, true, true, 0, 1'000'000'000);
    require (tap.read (frame, 0) == space_live::Read::empty, "hidden tap is idle");
    tap.setEnabled (true);
    const auto copy = data;
    for (int i = 0; i < 8; ++i)
    {
        tap.push (data.data(), 512, 2, true, true, i * 512, 1'000'000'000 + std::uint64_t (i) * 10'666'667);
        require (tap.read (frame, 0) == space_live::Read::ready, "every block is published, including the first");
        require (frame.samples.size() == std::min<std::size_t> (std::size_t (i + 1) * 512, 3840), "only the latest 80 ms are retained");
    }
    require (std::memcmp (copy.data(), data.data(), data.size() * sizeof (float)) == 0, "tap never changes its input");
    auto epoch = frame.span;
    tap.push (data.data(), 512, 2, true, true, 0, 1'090'000'000);
    require (tap.read (frame, 0) == space_live::Read::ready && frame.samples.size() == 512 && frame.span != epoch,
             "seek starts with only fresh PCM");
    tap.setEnabled (false); tap.setEnabled (true);
    require (tap.read (frame, 0) == space_live::Read::empty, "off/on cannot revive an old publication before a callback");
    tap.push (data.data(), 512, 2, true, false, 0, 1'100'000'000);
    require (tap.read (frame, 1'230'000'001) == space_live::Read::empty, "host callback loss expires the trace");
    tap.push (data.data(), 512, 2, true, false, 0, 2'000'000'000);
    require (tap.read (frame, 0) == space_live::Read::ready && frame.samples.size() == 512, "callback gap discards the old tail");
    tap.interrupt(); require (tap.read (frame, 0) == space_live::Read::empty, "stop/bypass/hold clear the live shape");
    tap.push (data.data(), 512, 1, false, false, 0, 0);
    require (tap.read (frame, 0) == space_live::Read::empty, "mono never invents stereo");
    for (const double rate : { 44'100.0, 48'000.0, 96'000.0, 192'000.0, 384'000.0 })
    {
        tap.prepare (rate);
        const int count = int (rate * 0.1);
        auto large = signal (count); tap.push (large.data(), count, 2, true, false, 0, 0);
        require (tap.read (frame, 0) == space_live::Read::ready && frame.samples.size() == std::size_t (std::ceil (rate * 0.08)),
                 "oversized callback publishes just its newest window at each rate");
        const auto first = std::size_t (count) - frame.samples.size();
        require (sameSample (frame.samples.front().left, large[first * 2]) && sameSample (frame.samples.back().right, large.back()), "window copies exact samples");
    }
    verifyConcurrentReads(); verifyBurstExpiry();
    tap.prepare (std::numeric_limits<double>::quiet_NaN());
    tap.push (data.data(), 512, 2, true, false, 0, 0);
    require (tap.read (frame, 0) == space_live::Read::empty, "invalid rate remains unavailable");
    require (space_live::Display::refreshIntervalMs == 16, "nominal 60 Hz uses the actual 16 ms JUCE timer interval");
}
inline void verifyClockBoundaries()
{
    space_live::LiveTap tap; tap.prepare (48'000); tap.setEnabled (true);
    space_live::Snapshot frame;
    const auto data = signal (64);
    const auto push = [&] (bool positioned, std::int64_t position, std::uint8_t source, std::uint64_t reset) {
        tap.push (data.data(), 64, 2, false, positioned, position, 0, source, reset);
        require (tap.read (frame, 0) == space_live::Read::ready, "clock boundary immediately admits fresh input");
    };
    push (true, 0, KIRIN_HYPHA_CLOCK_AUDIO_RENDER_TIMELINE, 0);
    push (true, 64, KIRIN_HYPHA_CLOCK_AUDIO_RENDER_TIMELINE, 0);
    require (frame.samples.size() == 128, "render clock without transport retains continuous blocks");
    auto span = frame.span;
    push (true, 128, KIRIN_HYPHA_CLOCK_PROJECT_TIMELINE, 0);
    require (frame.samples.size() == 64 && frame.span != span, "clock kind change cuts even matching coordinates");
    push (true, 128, KIRIN_HYPHA_CLOCK_PROJECT_TIMELINE, 0);
    require (frame.samples.size() == 128, "stopped project monitoring need not advance project position");
    for (const std::int64_t position : { 4096, 128 })
    {
        span = frame.span;
        auto fresh = signal (64, double (position));
        tap.push (fresh.data(), 64, 2, false, true, position, 0, KIRIN_HYPHA_CLOCK_PROJECT_TIMELINE);
        require (tap.read (frame, 0) == space_live::Read::ready && frame.samples.size() == 64 && frame.span != span,
                 "forward and backward stopped project seeks discard all prior points");
        for (std::size_t i = 0; i < 64; ++i)
            require (sameSample (frame.samples[i].left, fresh[i * 2]) && sameSample (frame.samples[i].right, fresh[i * 2 + 1]),
                     "stopped seek window contains exactly the new PCM");
        push (true, position, KIRIN_HYPHA_CLOCK_PROJECT_TIMELINE, 0);
        require (frame.samples.size() == 128, "monitoring continues at the new stationary project position");
    }
    span = frame.span;
    push (false, 0, KIRIN_HYPHA_CLOCK_PROJECT_TIMELINE, 0);
    require (frame.samples.size() == 64 && frame.span != span, "lost position cannot bridge the old coordinate span");
    span = frame.span;
    push (true, 192, KIRIN_HYPHA_CLOCK_PROJECT_TIMELINE, 0);
    require (frame.samples.size() == 64 && frame.span != span, "restored position starts a fresh coordinate span");
    span = frame.span;
    push (true, 192, KIRIN_HYPHA_CLOCK_PROJECT_TIMELINE, 1);
    require (frame.samples.size() == 64 && frame.span != span, "Reset cuts a stationary project-monitoring span too");
}
inline void verifyFineCoordinates()
{
    space_live::LiveTap tap; tap.prepare (48'000);
    observatory::View view (observatory::Role::pre); view.setVisible (true);
    view.setSpaceLiveTap (&tap); view.setDomain (observatory::Domain::space);
    juce::Image a (juce::Image::ARGB, 500, 500, true), b (juce::Image::ARGB, 500, 500, true);
    float sample[2] { 0.25f, 0.25f };
    tap.push (sample, 1, 2, true, false, 0, 0); display (view).refresh (0);
    { juce::Graphics g (a); display (view).paint (g, a.getBounds().toFloat()); }
    sample[0] = sample[1] = 0.251f; // same old 25x25 cell; still a visible subpixel movement
    tap.interrupt(); tap.push (sample, 1, 2, true, false, 0, 0); display (view).refresh (0);
    { juce::Graphics g (b); display (view).paint (g, b.getBounds().toFloat()); }
    int changed = 0, lit = 0;
    for (int y = 0; y < 500; ++y) for (int x = 0; x < 500; ++x)
    { changed += a.getPixelAt (x, y) != b.getPixelAt (x, y); lit += a.getPixelAt (x, y).getAlpha() != 0; }
    require (changed > 0 && lit <= 4, "one fine point occupies at most four partial pixels and moves inside an old cell");
}
inline void verifyRendering (const KirinMeterSession& meter)
{
    using namespace observatory;
    space_live::LiveTap tap; tap.prepare (48'000);
    const auto out = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_SPACE_TEST_DIR", {});
    material_cache::Lifetime material;
    for (const auto role : { Role::pre, Role::post })
        for (const auto language : { i18n::Language::english, i18n::Language::japanese })
            for (const auto size : { std::pair { 300, 200 }, { 375, 250 }, { 450, 300 }, { 600, 400 }, { 900, 600 } })
            {
                i18n::ScopedLanguage locale (language);
                View view (role); view.setSize (size.first, size.second); view.setVisible (true);
                view.setSpaceLiveTap (&tap); view.setDomain (Domain::space);
                KirinObservatoryFrame published {}; published.version = KIRIN_OBSERVATORY_FRAME_VERSION;
                published.meter = meter; published.signal_state = KIRIN_SIGNAL_STATE_ACTIVE;
                view.setObservatoryFrame (published, true);
                auto samples = signal (3840); tap.push (samples.data(), 3840, 2, true, true, 0, 0);
                require (display (view).refresh (0), "frame refresh uses the production reader");
                require (display (view).frame().samples.size() == 3840, "no 1024-point observation cap in live view");
                juce::Image image (juce::Image::ARGB, size.first, size.second, true);
                const auto paint = [&] { juce::Graphics g (image); view.paintEntireComponent (g, true); };
                // The first material use is vector paint, the second builds its retained raster.
                // Compare steady trace updates against the same material phase everywhere.
                paint(); paint();
                const auto shifted = signal (3840, 1.2);
                const auto dirty = view.spaceTraceBounds();
                require (view.bodyBounds().contains (dirty), "live invalidation stays inside its diagram");
                const auto paintFrame = [&] {
                    juce::Graphics g (image); g.reduceClipRegion (dirty); view.paintEntireComponent (g, true);
                };
                const auto started = juce::Time::getMillisecondCounterHiRes();
                for (int i = 0; i < 8; ++i)
                {
                    tap.push ((i % 2 == 0 ? samples : shifted).data(), 3840, 2, true, false, 0, 0);
                    display (view).refresh (0); paintFrame();
                }
                const auto cost = (juce::Time::getMillisecondCounterHiRes() - started) / 8;
                std::cout << "SPACE live " << size.first << 'x' << size.second << " role=" << int (role)
                          << " language=" << int (language) << " plot_px=" << dirty.getWidth() - 4
                          << " ms/frame=" << cost << '\n';
                require (cost < 16.0, "fresh raster and the production dirty repaint fit the 16 ms frame");
                juce::Image full (juce::Image::ARGB, size.first, size.second, true);
                { juce::Graphics g (full); view.paintEntireComponent (g, true); }
                int differences = 0;
                for (int y = 0; y < size.second; ++y) for (int x = 0; x < size.first; ++x)
                    differences += image.getPixelAt (x, y) != full.getPixelAt (x, y);
                if (differences != 0)
                {
                    std::cerr << "SPACE dirty pixels=" << differences << " bounds=" << dirty.toString() << '\n';
                    if (out.isNotEmpty())
                        for (const auto& item : { std::pair { "dirty", image }, std::pair { "full", full } })
                        {
                            auto file = juce::File (out).getChildFile (juce::String (item.first) + "-failure.png").createOutputStream();
                            require (file && juce::PNGImageFormat().writeImageToStream (item.second, *file), "dirty-frame diagnostic image");
                        }
                }
                require (differences == 0, "production dirty frame equals a full repaint of the fresh points");
                const auto help = view.metricHelpAt (view.bodyBounds().getCentre());
                require (help.contains ("80 ms") && i18n::hasTranslation (help), "diagram keeps its translated one-line help");
                if (out.isNotEmpty())
                {
                    const auto directory = juce::File (out); require (directory.createDirectory().wasOk(), "image directory");
                    auto file = directory.getChildFile ("live-" + juce::String (int (role)) + "-" + juce::String (int (language))
                        + "-" + juce::String (size.first) + ".png").createOutputStream();
                    require (file && juce::PNGImageFormat().writeImageToStream (image, *file), "live image");
                }
                auto mono = published;
                mono.meter.channels = 1; mono.meter.field_size = 0; mono.meter.field_observation_count = 0;
                mono.meter.balance_state = KIRIN_BALANCE_UNAVAILABLE;
                mono.meter.correlation = std::numeric_limits<double>::quiet_NaN(); mono.meter.mono_sum_band_count = 0;
                view.setObservatoryFrame (mono, true);
                tap.push (samples.data(), 3840, 1, true, false, 0, 0); display (view).refresh (0);
                require (display (view).frame().samples.empty(), "all sizes, roles and languages leave mono free of a stereo trace");
                {
                    text_style::ShownTextLog shown;
                    paint();
                    const auto compactReason = i18n::tr ("MONO");
                    const auto fullReason = i18n::tr ("MONO INPUT");
                    require (shown.texts().contains (compactReason) || shown.texts().contains (fullReason),
                             "real live painter states the mono reason at every size in both languages");
                }
                if (out.isNotEmpty())
                {
                    auto file = juce::File (out).getChildFile ("mono-" + juce::String (int (role)) + "-" + juce::String (int (language))
                        + "-" + juce::String (size.first) + ".png").createOutputStream();
                    require (file && juce::PNGImageFormat().writeImageToStream (image, *file), "mono image");
                }
                view.setDomain (Domain::level);
                require (! tap.enabled() && ! display (view).running(), "page switch stops capture and frame timer");
                view.setDomain (Domain::space); view.setVisible (false);
                require (! tap.enabled(), "hidden component stops at the visibility event");
            }
}
inline void verify()
{
    verifyTap(); verifyBusyExpiry(); verifyClockReadRace(); verifyClockBoundaries(); verifyFineCoordinates();
    std::cout << "SPACE tap: 25x25 -> continuous float coordinates; 3000 -> 80 ms; 100 -> block ms; 10 -> 60 Hz\n";
}
}
