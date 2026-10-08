#pragma once
#include <juce_graphics/juce_graphics.h>

namespace hypha::capture
{
// The chooser has already confirmed replacement. Write a complete sibling first so
// an existing Capture is never appended to, and a failed write leaves it intact.
inline bool saveFrozenPng (const juce::Image& image, const juce::File& target)
{
    if (image.isNull() || target == juce::File {} || target.isDirectory()) return false;
    juce::MemoryOutputStream encoded;
    if (! juce::PNGImageFormat().writeImageToStream (image, encoded)
        || encoded.getDataSize() == 0) return false;
    juce::TemporaryFile temporary (target, juce::TemporaryFile::useHiddenFile);
    {
        juce::FileOutputStream stream (temporary.getFile(), 0);
        if (! stream.openedOk() || ! stream.write (encoded.getData(), encoded.getDataSize())) return false;
        stream.flush();
        if (! stream.getStatus().wasOk()) return false;
    }
    if (temporary.getFile().getSize() != static_cast<juce::int64> (encoded.getDataSize())) return false;
    return temporary.getFile().replaceFileIn (target);
}
}
