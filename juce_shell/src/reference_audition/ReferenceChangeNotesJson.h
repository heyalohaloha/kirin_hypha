#pragma once
#include <juce_core/juce_core.h>
#include <set>

namespace hypha::reference_audition::change_notes_detail
{
// Bound nesting before JUCE's recursive parser and reject duplicate (including escaped) object keys.
class JsonShape final
{
public:
    JsonShape (const char* data, size_t size) : cursor (data), end (data + size) {}
    bool valid() { return value (0) && (space(), cursor == end); }
private:
    void space() { while (cursor != end && (*cursor == ' ' || *cursor == '\n' || *cursor == '\r' || *cursor == '\t')) ++cursor; }
    bool take (char c) { space(); if (cursor == end || *cursor != c) return false; ++cursor; return true; }
    bool string (juce::String* decoded = nullptr)
    {
        space(); if (cursor == end || *cursor != '"') return false;
        const auto* start = cursor++;
        bool escaped = false;
        while (cursor != end)
        {
            const auto c = *cursor++;
            if (! escaped && c == '"')
            {
                juce::var token;
                if (juce::JSON::parse ("[" + juce::String::fromUTF8 (start, static_cast<int> (cursor - start)) + "]", token).failed()
                    || token.getArray() == nullptr || token.getArray()->size() != 1 || ! token[0].isString()) return false;
                if (decoded) *decoded = token[0].toString();
                return true;
            }
            if (! escaped && c == '\\') escaped = true; else escaped = false;
        }
        return false;
    }
    bool value (int depth)
    {
        space(); if (cursor == end) return false;
        if (*cursor == '"') return string();
        if (*cursor == '{')
        {
            if (depth >= 3) return false;
            ++cursor; if (take ('}')) return true;
            std::set<juce::String> keys;
            do { juce::String key;
                if (! string (&key) || ! keys.insert (key).second || ! take (':') || ! value (depth + 1)) return false;
                if (take ('}')) return true;
            } while (take (','));
            return false;
        }
        if (*cursor == '[')
        {
            if (depth >= 3) return false;
            ++cursor; if (take (']')) return true;
            do { if (! value (depth + 1)) return false; if (take (']')) return true; } while (take (','));
            return false;
        }
        const auto* start = cursor;
        while (cursor != end && *cursor != ',' && *cursor != '}' && *cursor != ']' && *cursor != ' '
               && *cursor != '\n' && *cursor != '\r' && *cursor != '\t') ++cursor;
        return cursor != start; // Full parser validates number/literal syntax after this bounded traversal.
    }
    const char* cursor;
    const char* end;
};
}
