#pragma once
#include <juce_core/juce_core.h>

namespace hypha::storage
{
inline juce::String temporaryName() { return ".h" + juce::Uuid().toString().substring (0, 12); }

// Non-RT only. Keep the staging name independent of a UUID/hash target name. A sibling
// retains JUCE's same-volume replacement and RAII cleanup, including failed publication.
inline juce::File temporarySibling (const juce::File& target)
{
    for (;;)
    {
        const auto file = target.getSiblingFile (temporaryName());
        if (file != target && ! file.exists() && ! file.isSymbolicLink()) return file;
    }
}

template <typename OpenStream>
bool writeTextWithStream (const juce::File& target, const juce::String& text,
                          bool asUnicode, bool writeHeaderBytes,
                          const char* lineFeed, OpenStream open)
{
    if (target == juce::File {} || target.isDirectory()) return false;
    juce::TemporaryFile temporary (target, temporarySibling (target));
    {
        auto stream = open (temporary.getFile());
        if (! stream || ! stream->openedOk() || ! stream->writeText (text, asUnicode, writeHeaderBytes, lineFeed))
            return false;
        stream->flush();
        if (stream->getStatus().failed()) return false;
    }
    return temporary.overwriteTargetFileWithTemporary();
}

inline bool writeText (const juce::File& target, const juce::String& text,
                       bool asUnicode = false, bool writeHeaderBytes = false,
                       const char* lineFeed = "\r\n")
{
    return writeTextWithStream (target, text, asUnicode, writeHeaderBytes, lineFeed,
        [] (const juce::File& file) { return std::make_unique<juce::FileOutputStream> (file); });
}
}
