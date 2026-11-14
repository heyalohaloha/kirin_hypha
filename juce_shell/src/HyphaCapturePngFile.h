#pragma once
#include <juce_graphics/juce_graphics.h>
#include "AtomicFile.h"
namespace hypha::capture
{
// Why a chosen PNG was not saved, so the screen can say what to do (2026-11-11).
enum class PngSave { saved, notMade, folderLocked, notWritten, notReplaced };

// The chooser has already confirmed replacement. Write a complete sibling first so
// an existing Capture is never appended to, and a failed write leaves it intact.
inline PngSave writePng (const juce::Image& image, const juce::File& target)
{
    if (image.isNull()) return PngSave::notMade;
    if (target == juce::File {}) return PngSave::notWritten;
    if (target.isDirectory()) return PngSave::notReplaced;
    juce::MemoryOutputStream encoded;
    if (! juce::PNGImageFormat().writeImageToStream (image, encoded)
        || encoded.getDataSize() == 0) return PngSave::notMade;
    if (! target.getParentDirectory().isDirectory() || ! target.getParentDirectory().hasWriteAccess())
        return PngSave::folderLocked;
    juce::TemporaryFile temporary (target, hypha::storage::temporarySibling (target));
    {
        juce::FileOutputStream stream (temporary.getFile(), 0);
        if (! stream.openedOk()) return PngSave::folderLocked;
        if (! stream.write (encoded.getData(), encoded.getDataSize())) return PngSave::notWritten;
        stream.flush();
        if (! stream.getStatus().wasOk()) return PngSave::notWritten;
    }
    if (temporary.getFile().getSize() != static_cast<juce::int64> (encoded.getDataSize())) return PngSave::notWritten;
    return temporary.getFile().replaceFileIn (target) ? PngSave::saved : PngSave::notReplaced;
}

inline bool saveFrozenPng (const juce::Image& image, const juce::File& target)
{
    return writePng (image, target) == PngSave::saved;
}
}
