#include "ReferenceWorkflowStorage.h"
#include "ReferenceRuntimeRepositoryParsing.h"

#if ! JUCE_WINDOWS
 #include <fcntl.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace hypha::reference_audition
{
namespace
{
constexpr std::int64_t maximumBytes = 64 * 1024;

bool fsyncDirectory (const juce::File& directory)
{
   #if JUCE_WINDOWS
    juce::ignoreUnused (directory);
    return true;
   #else
    const int handle = ::open (directory.getFullPathName().toRawUTF8(), O_RDONLY);
    if (handle < 0) return false;
    const bool ok = ::fsync (handle) == 0;
    ::close (handle);
    return ok;
   #endif
}

bool makePrivate (const juce::File& file)
{
   #if JUCE_WINDOWS
    juce::ignoreUnused (file);
    return true;
   #else
    return ::chmod (file.getFullPathName().toRawUTF8(), S_IRUSR | S_IWUSR) == 0;
   #endif
}

bool safeParent (const juce::File& root, const juce::File& parent)
{
    if (! root.createDirectory() || root.isSymbolicLink()
        || ! parent.createDirectory() || parent.isSymbolicLink()) return false;
    for (auto current = parent; current != root; current = current.getParentDirectory())
        if (! current.isAChildOf (root) || ! current.isDirectory() || current.isSymbolicLink())
            return false;
    return true;
}
}

bool writeWorkflowDurable (const juce::File& root, const juce::File& target,
                           const juce::String& content, bool replace)
{
    const auto bytes = static_cast<std::int64_t> (content.getNumBytesAsUTF8());
    const auto parent = target.getParentDirectory();
    if (bytes < 1 || bytes > maximumBytes || ! target.isAChildOf (root)
        || ! safeParent (root, parent) || target.isSymbolicLink()) return false;
    if (! replace && target.existsAsFile()) return ! target.isSymbolicLink()
        && target.loadFileAsString() == content;
    const auto temporary = target.getSiblingFile (
        "." + target.getFileName() + "." + juce::Uuid().toDashedString() + ".tmp");
    auto stream = temporary.createOutputStream();
    if (stream == nullptr || ! stream->openedOk()
        || ! stream->write (content.toRawUTF8(), static_cast<size_t> (bytes))) return false;
    stream->flush();
    const bool flushed = ! stream->getStatus().failed();
    stream.reset();
    if (! flushed || ! makePrivate (temporary)
        || (replace ? ! temporary.replaceFileIn (target)
                    : target.exists() || ! temporary.moveFileTo (target)))
    { temporary.deleteFile(); return false; }
    return fsyncDirectory (parent);
}

juce::String workflowCheckpointFromHash (const juce::String& hash)
{
    if (! runtime_repository_parsing::sha256 (hash)) return {};
    auto value = hash.substring (0, 32).toStdString();
    value[12] = '4';
    value[16] = "89ab"[static_cast<unsigned> (value[16]) & 3u];
    return juce::String (value.substr (0, 8) + "-" + value.substr (8, 4) + "-"
        + value.substr (12, 4) + "-" + value.substr (16, 4) + "-" + value.substr (20, 12));
}
}
