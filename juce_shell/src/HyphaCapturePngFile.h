#pragma once
#include <juce_graphics/juce_graphics.h>
#include <functional>
#include "AtomicFile.h"
namespace hypha::capture
{
// Why a chosen PNG was not saved, so the screen can say what to do (2026-11-11).
enum class PngSave { saved, notMade, folderLocked, notWritten, notReplaced };
inline bool replacePngApproved (int button) noexcept { return button == 1; }

// Confirm the actual PNG path, not the name before its extension is normalized.
inline void choosePngTarget (const juce::File& chosen,
                             std::function<void (const juce::File&, std::function<void (bool)>)> confirm,
                             std::function<void (const juce::File&)> save)
{
    if (chosen == juce::File {}) return;
    const auto target = chosen.withFileExtension (".png");
    if (! target.exists()) { save (target); return; }
    confirm (target, [target, saveFn = std::move (save)] (bool approved)
    {
        if (approved) saveFn (target);
    });
}

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
    if (! target.getParentDirectory().isDirectory()
       #if ! JUCE_WINDOWS
        || ! target.getParentDirectory().hasWriteAccess()
       #endif
       )
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
