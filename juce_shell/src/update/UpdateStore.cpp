#include "UpdateStore.h"

#include <cerrno>
#include <cstring>
#include <set>
#include <vector>

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <fcntl.h>
 #include <sys/file.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace hypha::update
{
namespace
{
constexpr std::size_t maximumBytes = 32 * 1024;
constexpr std::int64_t maximumInteger = 9'007'199'254'740'991;
constexpr auto stateName = "state.json";
constexpr auto lockName = "owner.lock";

bool valid (const Store::State& value)
{
    return value.lastAttempt >= 0 && value.lastAttempt <= maximumInteger
        && value.lastSuccess >= 0 && value.lastSuccess <= value.lastAttempt
        && value.lastSequence >= 0 && value.lastSequence <= maximumInteger
        && value.wire.getNumBytesAsUTF8() <= 16 * 1024
        && value.dismissedVersion.length() <= 128;
}

// JUCE's parser collapses duplicate properties. Reject them before interpreting state.
bool uniqueFlatKeys (const juce::String& text)
{
    std::set<juce::String> keys;
    bool quoted = false, escaped = false, collectingKey = false, started = false, finished = false;
    int depth = 0;
    juce::String token;
    const auto* cursor = text.toRawUTF8();
    for (std::size_t index = 0; cursor[index] != 0; ++index)
    {
        const auto c = cursor[index];
        if (! quoted && finished && ! juce::CharacterFunctions::isWhitespace (c)) return false;
        if (! quoted && ! started && ! juce::CharacterFunctions::isWhitespace (c))
        {
            if (c != '{') return false;
            started = true;
        }
        if (quoted)
        {
            if (escaped) { if (collectingKey) return false; escaped = false; continue; }
            if (c == '\\') { escaped = true; continue; }
            if (c == '"')
            {
                quoted = false;
                if (collectingKey && ! keys.insert (token).second) return false;
                collectingKey = false;
            }
            else if (collectingKey) token += juce::String::charToString (static_cast<juce::juce_wchar> (c));
            continue;
        }
        if (c == '"')
        {
            std::size_t next = index;
            while (next > 0 && juce::CharacterFunctions::isWhitespace (cursor[next - 1])) --next;
            collectingKey = depth == 1 && next > 0 && (cursor[next - 1] == '{' || cursor[next - 1] == ',');
            token = {}; quoted = true;
        }
        else if (c == '{' || c == '[') { if (++depth > 1) return false; }
        else if (c == '}' || c == ']') { if (--depth == 0) finished = true; }
    }
    return started && finished && ! quoted && depth == 0 && keys.size() == 8;
}

bool decode (const juce::String& text, Store::State& state)
{
    if (! uniqueFlatKeys (text)) return false;
    const auto value = juce::JSON::parse (text);
    const auto* object = value.getDynamicObject();
    if (object == nullptr || object->getProperties().size() != 8) return false;
    for (const auto* name : { "format", "version", "enabled", "last_attempt_seconds", "last_success_seconds", "last_sequence", "wire", "dismissed_version" })
        if (! object->hasProperty (name)) return false;
    const auto integer = [] (const juce::var& v) { return v.isInt() || v.isInt64(); };
    if (value["format"] != "kirin_hypha_update_cache" || value["version"] != "1.0"
        || ! value["enabled"].isBool() || ! integer (value["last_attempt_seconds"])
        || ! integer (value["last_success_seconds"])
        || ! integer (value["last_sequence"]) || ! value["wire"].isString()
        || ! value["dismissed_version"].isString()) return false;
    Store::State parsed;
    parsed.enabled = static_cast<bool> (value["enabled"]);
    parsed.lastAttempt = static_cast<juce::int64> (value["last_attempt_seconds"]);
    parsed.lastSuccess = static_cast<juce::int64> (value["last_success_seconds"]);
    parsed.lastSequence = static_cast<juce::int64> (value["last_sequence"]);
    parsed.wire = value["wire"].toString();
    parsed.dismissedVersion = value["dismissed_version"].toString();
    if (! valid (parsed)) return false;
    state = std::move (parsed);
    return true;
}

juce::String encode (const Store::State& state)
{
    auto* value = new juce::DynamicObject();
    value->setProperty ("format", "kirin_hypha_update_cache");
    value->setProperty ("version", "1.0");
    value->setProperty ("enabled", state.enabled);
    value->setProperty ("last_attempt_seconds", static_cast<juce::int64> (state.lastAttempt));
    value->setProperty ("last_success_seconds", static_cast<juce::int64> (state.lastSuccess));
    value->setProperty ("last_sequence", static_cast<juce::int64> (state.lastSequence));
    value->setProperty ("wire", state.wire);
    value->setProperty ("dismissed_version", state.dismissedVersion);
    return juce::JSON::toString (juce::var (value), true);
}
}

struct Store::Lock
{
   #if JUCE_WINDOWS
    HANDLE file = INVALID_HANDLE_VALUE;
    std::vector<HANDLE> directories;
    OVERLAPPED range {};
    ~Lock()
    {
        if (file != INVALID_HANDLE_VALUE) { UnlockFileEx (file, 0, 1, 0, &range); CloseHandle (file); }
        for (const auto handle : directories) CloseHandle (handle);
    }
    bool openDirectory (const juce::File& root, bool create = true)
    {
        std::vector<juce::File> chain;
        for (auto current = root; ; current = current.getParentDirectory())
        {
            chain.push_back (current);
            if (current == current.getParentDirectory()) break;
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        {
            if (create && ! it->createDirectory()) return false;
            const auto handle = CreateFileW (it->getFullPathName().toWideCharPointer(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE) return false;
            directories.push_back (handle);
            BY_HANDLE_FILE_INFORMATION info {};
            if (! GetFileInformationByHandle (handle, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        }
        return true;
    }
    bool regular (HANDLE handle)
    {
        BY_HANDLE_FILE_INFORMATION info {};
        return GetFileInformationByHandle (handle, &info)
            && info.nNumberOfLinks == 1
            && !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
    }
   #else
    int directory = -1, file = -1;
    ~Lock() { if (file >= 0) { flock (file, LOCK_UN); close (file); } if (directory >= 0) close (directory); }
    bool openDirectory (const juce::File& root, bool create = true)
    {
        directory = ::open ("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (directory < 0) return false;
        juce::StringArray components;
        components.addTokens (root.getFullPathName(), "/", {});
        components.removeEmptyStrings();
        for (const auto& component : components)
        {
            if (component == "." || component == "..") return false;
            const auto* name = component.toRawUTF8();
            if (create && ::mkdirat (directory, name, 0700) != 0 && errno != EEXIST) return false;
            const auto next = ::openat (directory, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (next < 0) return false;
            ::close (directory); directory = next;
        }
        return true;
    }
    bool regular (int fd) { struct stat info {}; return ::fstat (fd, &info) == 0 && S_ISREG (info.st_mode) && info.st_nlink == 1; }
   #endif
};

Store::Store (juce::File rootIn) : directory (std::move (rootIn)) {}
Store::~Store() = default;
juce::File Store::root() const { return directory; }
void Store::release() { heldLock.reset(); }

bool Store::acquire()
{
    if (heldLock != nullptr) return false;
    auto candidate = std::make_unique<Lock>();
    if (! candidate->openDirectory (directory)) return false;
   #if JUCE_WINDOWS
    candidate->file = CreateFileW (directory.getChildFile (lockName).getFullPathName().toWideCharPointer(),
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (candidate->file == INVALID_HANDLE_VALUE || ! candidate->regular (candidate->file)
        || ! LockFileEx (candidate->file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                        0, 1, 0, &candidate->range)) return false;
   #else
    candidate->file = ::openat (candidate->directory, lockName, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (candidate->file < 0 || ! candidate->regular (candidate->file)
        || ::flock (candidate->file, LOCK_EX | LOCK_NB) != 0) return false;
   #endif
    heldLock = std::move (candidate);
    return true;
}

bool Store::read (State& result)
{
    return heldLock != nullptr && readState (*heldLock, result);
}

bool Store::observe (State& result)
{
    // Atomic rename guarantees either one complete old snapshot or one complete
    // new snapshot. Observation grants no lease, save authority or HTTP budget.
    // Never create folders, touch owner.lock, wait or poll another worker.
    Lock reader;
    return reader.openDirectory (directory, false) && readState (reader, result);
}

bool Store::readState (Lock& access, State& result)
{
    juce::MemoryBlock bytes;
   #if JUCE_WINDOWS
    const auto file = CreateFileW (directory.getChildFile (stateName).getFullPathName().toWideCharPointer(),
        GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) return false;
        result = {}; return true;
    }
    LARGE_INTEGER size {};
    bool ok = access.regular (file) && GetFileSizeEx (file, &size)
        && size.QuadPart > 0 && size.QuadPart <= static_cast<LONGLONG> (maximumBytes);
    DWORD actual = 0;
    if (ok) { bytes.setSize (static_cast<std::size_t> (size.QuadPart)); ok = ReadFile (file, bytes.getData(), static_cast<DWORD> (bytes.getSize()), &actual, nullptr) && actual == bytes.getSize(); }
    CloseHandle (file);
   #else
    const auto file = ::openat (access.directory, stateName, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (file < 0) { if (errno != ENOENT) return false; result = {}; return true; }
    struct stat info {};
    bool ok = ::fstat (file, &info) == 0 && S_ISREG (info.st_mode) && info.st_nlink == 1
        && info.st_size > 0 && static_cast<std::uint64_t> (info.st_size) <= maximumBytes;
    if (ok)
    {
        bytes.setSize (static_cast<std::size_t> (info.st_size));
        std::size_t done = 0;
        while (done < bytes.getSize())
        {
            const auto n = ::read (file, static_cast<char*> (bytes.getData()) + done, bytes.getSize() - done);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { ok = false; break; }
            done += static_cast<std::size_t> (n);
        }
    }
    ::close (file);
   #endif
    if (! ok || std::memchr (bytes.getData(), 0, bytes.getSize()) != nullptr
        || ! juce::CharPointer_UTF8::isValidString (static_cast<const char*> (bytes.getData()), static_cast<int> (bytes.getSize()))) return false;
    return decode (juce::String::fromUTF8 (static_cast<const char*> (bytes.getData()), static_cast<int> (bytes.getSize())), result);
}

bool Store::save (const State& state)
{
    if (heldLock == nullptr || ! valid (state)) return false;
    State previous;
    if (! read (previous)) return false; // Never silently repair damaged state or overwrite a linked file.
    if (state.lastAttempt < previous.lastAttempt || state.lastSuccess < previous.lastSuccess
        || state.lastSequence < previous.lastSequence) return false;
    const auto text = encode (state);
    const auto length = text.getNumBytesAsUTF8();
    if (length == 0 || length > maximumBytes) return false;
    const auto temporaryName = "pending-" + juce::Uuid().toString() + ".tmp";
   #if JUCE_WINDOWS
    const auto temporary = directory.getChildFile (temporaryName);
    const auto file = CreateFileW (temporary.getFullPathName().toWideCharPointer(), GENERIC_WRITE, 0,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = heldLock->regular (file) && WriteFile (file, text.toRawUTF8(), static_cast<DWORD> (length), &written, nullptr)
        && written == length && FlushFileBuffers (file);
    CloseHandle (file);
    if (ok) ok = MoveFileExW (temporary.getFullPathName().toWideCharPointer(),
        directory.getChildFile (stateName).getFullPathName().toWideCharPointer(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (! ok) temporary.deleteFile();
   #else
    const auto file = ::openat (heldLock->directory, temporaryName.toRawUTF8(), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (file < 0) return false;
    std::size_t written = 0;
    bool ok = true;
    while (written < length)
    {
        const auto n = ::write (file, text.toRawUTF8() + written, length - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = false; break; }
        written += static_cast<std::size_t> (n);
    }
    if (ok) ok = ::fsync (file) == 0;
    ::close (file);
    if (ok) ok = ::renameat (heldLock->directory, temporaryName.toRawUTF8(), heldLock->directory, stateName) == 0;
    if (ok) ok = ::fsync (heldLock->directory) == 0;
    if (! ok) ::unlinkat (heldLock->directory, temporaryName.toRawUTF8(), 0);
   #endif
    return ok;
}
}
