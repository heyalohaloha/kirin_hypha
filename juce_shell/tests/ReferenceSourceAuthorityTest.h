#pragma once
// A receipt registered again for the same path and bytes (another file object, e.g. the file was
// replaced by an identical copy) keeps its own authority: the old receipt's refusal must not stop it.
#include "../src/reference_audition/ReferenceSourceRevisionService.h"

inline void testReferenceSourceAuthorityPerObject()
{
    namespace ref = hypha::reference_audition;
    ref::RuntimeSource oldSource;
    oldSource.absolutePath = "/fixture/reference-authority.wav";
    oldSource.sourceFileSha256 = juce::String::repeatedString ("a", 64);
    oldSource.audio.sampleRateHz = 48'000; oldSource.audio.channels = 2; oldSource.audio.totalSampleFrames = 48'000;
    oldSource.revision = { "1", "100", "4096", "10", "10" };
    auto newSource = oldSource;
    newSource.revision = { "1", "200", "4096", "20", "20" };
    auto& service = ref::revision_detail::service();
    const auto watched = service.watch (newSource);
    const auto before = watched->invalidation.load();
    require (service.check (oldSource, newSource.revision, nullptr).state == ref::SourceRevisionState::refused,
             "the old receipt is refused against the new file");
    require (watched->invalidation.load() == before, "the old receipt's refusal leaves the new receipt alone");
    require (service.check (newSource, newSource.revision, nullptr).state == ref::SourceRevisionState::matched,
             "the new receipt still matches");
}
