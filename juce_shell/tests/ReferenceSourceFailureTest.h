#pragma once
#include "../src/AtomicFile.h"
namespace
{
void verifySourceFailureReasons (const juce::File& root, ref::RuntimeCandidate candidate)
{
    const auto file = root.getChildFile ("undecodable-fixture.wav");
    require (hypha::storage::writeText (file, "fixture bytes that are not encoded audio"), "write disposable undecodable source");
    const auto hash = juce::SHA256 (file).toHexString();
    const auto pcm = juce::String::repeatedString ("f", 64);
    candidate.sourceIdentityKey = "catalog:source-test:" + hash + ":" + pcm;
    candidate.sourceArtifact = stageRuntimeV2Artifact (root, "sources", makeRuntimeV2Source (file, hash, pcm));
    ref::RuntimeV2SourceRepository repository (root);
    const auto source = repository.load (candidate);
    require (source.accepted(), "source descriptor and hash are valid before decoder failure");
    require (repository.verifySourceFile (*source.source) == "reference_source_decode_failed",
             "a valid hash with undecodable audio reports open/decode failure rather than changed source");
    require (file.deleteFile(), "remove only fixture audio");
    require (repository.verifySourceFile (*source.source) == "reference_source_read_failed",
             "a removed source reports the unavailable file separately");
}
}
