#pragma once
#include <juce_core/juce_core.h>
#include <memory>

namespace hypha::vu_calibration
{
// A complete, checked sibling must exist before the one atomic replacement. The injectable
// stream opener lets disposable fixtures cover partial writes and delayed flush failures.
template <typename OpenStream>
bool writeAtomicText (const juce::File& target, const juce::String& text, OpenStream open)
{
    if (target == juce::File {} || target.isDirectory()) return false;
    const auto bytes = text.getNumBytesAsUTF8();
    if (bytes == 0 || bytes > 256) return false;
    juce::TemporaryFile temporary (target, juce::TemporaryFile::useHiddenFile);
    {
        auto stream = open (temporary.getFile());
        if (! stream || ! stream->openedOk() || ! stream->write (text.toRawUTF8(), bytes)) return false;
        stream->flush();
        if (! stream->getStatus().wasOk()
            || stream->getPosition() != static_cast<juce::int64> (bytes)) return false;
    }
    if (temporary.getFile().getSize() != static_cast<juce::int64> (bytes)
        || temporary.getFile().loadFileAsString() != text) return false;
    return temporary.getFile().replaceFileIn (target);
}

inline bool writeAtomicText (const juce::File& target, const juce::String& text)
{
    return writeAtomicText (target, text, [] (const juce::File& file)
        { return std::make_unique<juce::FileOutputStream> (file, 0); });
}
}
