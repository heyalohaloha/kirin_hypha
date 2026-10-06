#pragma once

// Kirin OS の書き出しの写し（tests/fixtures/kirin_os_library_abcv）を試験のフォルダへ写す。
// 写しは Mac の Kirin OS が書いたもので、音源の記述（sources/<sha256>.json）の場所は POSIX の絶対パス。Windows は
// それを絶対パスと認めない（Kirin OS も Windows では Windows の絶対パスを書く）。どの環境でも同じ道を通るよう、
// 場所をその試験のフォルダの中の絶対パスに書き換え、記述の sha256 と、それを指す一覧・Preset・Version と manifest の
// 受け取り（sha256・bytes・relative_path）を付け替える（Kirin OS が書く形と同じ）。音源の中身の値（ranges）は替えない。
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

#include <map>
#include <vector>

namespace kirin_library_fixture
{
using Hashes = std::map<juce::String, juce::String>;  // 前の sha256 → 今の sha256

inline juce::String sha256Of (const juce::File& file)
{
    return juce::SHA256 (file).toHexString();
}

// 文字列の中の前の sha256 を今のものに替え、今の sha256 を持つ受け取りの bytes を今の大きさにする。
inline void retarget (juce::var& value, const Hashes& hashes, const std::map<juce::String, juce::int64>& sizes)
{
    if (value.isString())
    {
        auto text = value.toString();
        for (const auto& [before, now] : hashes) text = text.replace (before, now);
        value = text;
        return;
    }
    if (auto* array = value.getArray())
    {
        for (auto& item : *array) retarget (item, hashes, sizes);
        return;
    }
    if (auto* object = value.getDynamicObject())
    {
        std::vector<juce::Identifier> names;
        for (const auto& property : object->getProperties()) names.push_back (property.name);
        for (const auto& name : names)
        {
            auto child = object->getProperty (name);
            retarget (child, hashes, sizes);
            object->setProperty (name, child);
        }
        const auto size = sizes.find (object->getProperty ("sha256").toString());
        if (size != sizes.end() && object->hasProperty ("bytes")) object->setProperty ("bytes", size->second);
    }
}

// folder の中の JSON を付け替えて書き直す。rename なら名前（中身の sha256）も替え、その付け替えを hashes に足す。
inline bool rewrite (const juce::File& folder, bool rename, Hashes& hashes, std::map<juce::String, juce::int64>& sizes)
{
    const Hashes earlier = hashes;
    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        auto json = juce::JSON::parse (file);
        if (json.isVoid()) return false;
        retarget (json, earlier, sizes);
        if (! file.replaceWithText (juce::JSON::toString (json, true))) return false;
        if (! rename) continue;
        const auto now = sha256Of (file);
        if (now == file.getFileNameWithoutExtension()) continue;
        const auto renamed = folder.getChildFile (now + ".json");
        hashes[file.getFileNameWithoutExtension()] = now;
        sizes[now] = file.getSize();
        if (! file.moveFileTo (renamed)) return false;
    }
    return true;
}

// 音源の記述の場所をこのフォルダの中の絶対パスにし、記述を指すものを順に付け替える（記述 → Preset・Version → manifest・一覧）。
inline bool placeSources (const juce::File& root)
{
    Hashes hashes;
    std::map<juce::String, juce::int64> sizes;
    const auto sources = root.getChildFile ("sources");
    const auto audio = root.getChildFile ("fixture/source.wav").getFullPathName();
    for (const auto& file : sources.findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        auto json = juce::JSON::parse (file);
        auto* place = json["file"].getDynamicObject();
        if (place == nullptr || ! place->hasProperty ("absolute_path")) return false;
        place->setProperty ("absolute_path", audio);
        if (! file.replaceWithText (juce::JSON::toString (json, true))) return false;
        const auto now = sha256Of (file);
        hashes[file.getFileNameWithoutExtension()] = now;
        sizes[now] = file.getSize();
        if (! file.moveFileTo (sources.getChildFile (now + ".json"))) return false;
    }
    const auto library = root.getChildFile ("library");
    return rewrite (library.getChildFile ("presets"), true, hashes, sizes)
        && rewrite (library.getChildFile ("versions"), true, hashes, sizes)
        && rewrite (library, false, hashes, sizes);
}

// sandbox の中に name で写し、場所を整える。できなければ空の File（呼ぶ側が require で止める）。
inline juce::File copy (const juce::File& sandbox, const juce::String& name)
{
    const auto source = juce::File (KIRIN_REFERENCE_FIXTURE_DIR).getChildFile ("kirin_os_library_abcv");
    const auto root = sandbox.getChildFile (name);
    if (! root.isAChildOf (sandbox) || ! source.isDirectory() || ! source.copyDirectoryTo (root) || ! placeSources (root))
        return {};
    return root;
}
}
