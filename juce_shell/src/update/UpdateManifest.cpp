#include "UpdateManifest.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <array>
#include <limits>
#include <set>

namespace hypha::update
{
namespace
{
constexpr juce::int64 safeInteger = 9007199254740991LL;

bool lowerHex (const juce::String& value, int length)
{
    return value.length() == length && value.containsOnly ("0123456789abcdef");
}

std::optional<std::array<juce::int64, 3>> versionParts (const juce::String& text)
{
    if (text.isEmpty() || text.length() > 32 || ! text.containsOnly ("0123456789."))
        return {};
    auto parts = juce::StringArray::fromTokens (text, ".", "");
    if (parts.size() != 3) return {};
    std::array<juce::int64, 3> values {};
    for (int i = 0; i < 3; ++i)
    {
        const auto& part = parts[i];
        if (part.isEmpty() || part.length() > 10 || ! part.containsOnly ("0123456789")
            || (part.length() > 1 && part.startsWithChar ('0')))
            return {};
        values[(size_t) i] = part.getLargeIntValue();
        if (values[(size_t) i] > std::numeric_limits<int>::max()) return {};
    }
    return values;
}

// JUCE's JSON parser accepts duplicate object names. Reject them before parsing,
// including escaped field names, so signatures cannot bless ambiguous schemas.
bool uniqueRootKeys (const juce::String& text)
{
    std::set<juce::String> keys;
    std::array<juce::juce_wchar, 2> containers {};
    int depth = 0;
    bool expectKey = false, started = false, finished = false;
    auto cursor = text.getCharPointer();
    while (! cursor.isEmpty())
    {
        const auto c = cursor.getAndAdvance();
        const bool whitespace = c == ' ' || c == '\t' || c == '\r' || c == '\n';
        // JSON::parse may ignore trailing bytes. The signed object must be the
        // entire document, not a valid prefix of garbage or another JSON value.
        if (finished)
        {
            if (! whitespace) return false;
            continue;
        }
        if (! started)
        {
            if (whitespace) continue;
            if (c != '{') return false;
            started = true;
        }
        if (c == '"')
        {
            const bool key = depth == 1 && expectKey;
            juce::String name;
            bool closed = false;
            while (! cursor.isEmpty())
            {
                const auto character = cursor.getAndAdvance();
                if (character == '\\')
                {
                    if (key) return false;
                    if (cursor.isEmpty()) return false;
                    cursor.getAndAdvance();
                }
                else if (character == '"') { closed = true; break; }
                else if (key)
                {
                    name += juce::String::charToString (character);
                    if (name.length() > 32) return false;
                }
            }
            if (! closed) return false;
            if (key && ! keys.insert (name).second) return false;
            expectKey = false;
        }
        else if (c == '{' || c == '[')
        {
            // Envelope is flat and payload arrays contain only scalar strings.
            // Bound nesting before JUCE parses any untrusted JSON.
            if (depth == (int) containers.size()) return false;
            containers[(size_t) depth++] = c;
            if (depth == 1) expectKey = true;
        }
        else if (c == '}' || c == ']')
        {
            if (depth == 0 || containers[(size_t) --depth] != (c == '}' ? '{' : '[')) return false;
            if (depth == 0) finished = true;
        }
        else if (c == ',' && depth == 1) expectKey = true;
    }
    return started && finished;
}

bool exactFields (const juce::var& value, std::initializer_list<const char*> fields)
{
    const auto* object = value.getDynamicObject();
    if (object == nullptr || object->getProperties().size() != (int) fields.size()) return false;
    for (auto field : fields) if (! object->hasProperty (field)) return false;
    return true;
}

std::optional<juce::MemoryBlock> decodeBase64 (const juce::var& value)
{
    if (! value.isString()) return {};
    const auto text = value.toString();
    if (text.isEmpty() || text.length() > maximumManifestBytes
        || text.length() % 4 != 0
        || ! text.containsOnly ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/="))
        return {};
    juce::MemoryOutputStream stream;
    if (! juce::Base64::convertFromBase64 (stream, text)) return {};
    auto bytes = stream.getMemoryBlock();
    if (juce::Base64::toBase64 (bytes.getData(), bytes.getSize()) != text) return {};
    return bytes;
}

bool verifySignature (const juce::MemoryBlock& payload, const juce::MemoryBlock& signature,
                      const juce::String& keyText)
{
    if (! validPublicKey (keyText)) return false;
    const auto modulusText = keyText.substring (6);
    if (signature.getSize() != 256) return false;
    juce::BigInteger modulus;
    modulus.parseString (modulusText, 16);
    auto reversed = signature;
    auto* signatureBytes = static_cast<uint8_t*> (reversed.getData());
    std::reverse (signatureBytes, signatureBytes + reversed.getSize());
    juce::BigInteger decoded;
    decoded.loadFromMemoryBlock (reversed);
    if (decoded.isZero() || decoded >= modulus) return false;
    const juce::RSAKey key (keyText);
    if (! key.isValid() || ! key.applyToValue (decoded)) return false;

    // RFC 8017 EMSA-PKCS1-v1_5 SHA-256: compare every encoded byte, not just
    // a digest suffix (which would allow malformed padding/DigestInfo).
    std::array<uint8_t, 256> expected {};
    expected[1] = 1;
    std::fill (expected.begin() + 2, expected.begin() + 204, 0xff);
    constexpr std::array<uint8_t, 19> prefix {{
        0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
        0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20 }};
    std::copy (prefix.begin(), prefix.end(), expected.begin() + 205);
    const auto digest = juce::SHA256 (payload.getData(), payload.getSize()).getRawData();
    std::copy_n (static_cast<const uint8_t*> (digest.getData()), 32, expected.begin() + 224);
    const auto decodedBytes = decoded.toMemoryBlock(); // JUCE BigInteger is little-endian.
    if (decodedBytes.getSize() > expected.size()) return false;
    std::array<uint8_t, 256> actual {};
    const auto* bytes = static_cast<const uint8_t*> (decodedBytes.getData());
    for (size_t i = 0; i < decodedBytes.getSize(); ++i) actual[255 - i] = bytes[i];
    return actual == expected;
}

std::optional<juce::int64> integer (const juce::var& value)
{
    if (! value.isInt() && ! value.isInt64()) return {};
    const auto number = static_cast<juce::int64> (value);
    if (number < 0 || number > safeInteger) return {};
    return number;
}

bool stringSet (const juce::var& value, const juce::StringArray& allowed, juce::StringArray& output)
{
    const auto* array = value.getArray();
    if (array == nullptr || array->isEmpty() || array->size() > allowed.size()) return false;
    for (const auto& item : *array)
    {
        if (! item.isString() || ! allowed.contains (item.toString()) || output.contains (item.toString()))
            return false;
        output.add (item.toString());
    }
    return true;
}
}

bool validSemVer (const juce::String& version) { return versionParts (version).has_value(); }

bool validPublicKey (const juce::String& key)
{
    if (! key.startsWith ("10001,") || key.length() != 518) return false;
    const auto modulus = key.substring (6);
    return lowerHex (modulus, 512) && juce::String ("89abcdef").containsChar (modulus[0])
        && juce::String ("13579bdf").containsChar (modulus[511]);
}

int compareVersions (const juce::String& left, const juce::String& right)
{
    const auto a = versionParts (left), b = versionParts (right);
    if (! a || ! b) return 0;
    return *a < *b ? -1 : (*a > *b ? 1 : 0);
}

std::optional<Manifest> verifyManifest (const juce::String& wire, const juce::String& publicKey,
                                      juce::int64 now, juce::int64 previousSequence)
{
    if (publicKey.isEmpty() || now < 0 || now > safeInteger || previousSequence < 0
        || previousSequence > safeInteger || wire.isEmpty()
        || wire.getNumBytesAsUTF8() > maximumManifestBytes || ! uniqueRootKeys (wire)) return {};
    const auto envelope = juce::JSON::parse (wire);
    if (! exactFields (envelope, { "payload", "signature" })) return {};
    const auto payload = decodeBase64 (envelope["payload"]), signature = decodeBase64 (envelope["signature"]);
    if (! payload || ! signature || ! verifySignature (*payload, *signature, publicKey)) return {};
    const auto* bytes = static_cast<const char*> (payload->getData());
    if (payload->getSize() == 0 || ! juce::CharPointer_UTF8::isValidString (bytes, (int) payload->getSize())
        || std::find (bytes, bytes + payload->getSize(), '\0') != bytes + payload->getSize()) return {};
    const auto text = juce::String::fromUTF8 (bytes, (int) payload->getSize());
    if (! uniqueRootKeys (text)) return {};
    const auto value = juce::JSON::parse (text);
    if (! exactFields (value, { "schema", "product", "channel", "version", "source_commit",
                               "published_at", "expires_at", "publication_sequence", "withdrawn",
                               "platforms", "formats" })) return {};
    const auto schema = integer (value["schema"]), published = integer (value["published_at"]),
               expires = integer (value["expires_at"]), sequence = integer (value["publication_sequence"]);
    if (! schema || *schema != 1 || ! published || ! expires || ! sequence
        || ! value["product"].isString() || value["product"].toString() != "kirin-hypha"
        || ! value["channel"].isString() || value["channel"].toString() != "stable"
        || ! value["version"].isString() || ! validSemVer (value["version"].toString())
        || ! value["source_commit"].isString() || ! lowerHex (value["source_commit"].toString(), 40)
        || ! value["withdrawn"].isBool() || *sequence < previousSequence || *sequence == 0
        || *published > now || *expires <= now || *expires <= *published
        || *expires - *published > maximumManifestLifetime) return {};
    Manifest result;
    result.version = value["version"].toString(); result.sourceCommit = value["source_commit"].toString();
    result.publishedAt = *published; result.expiresAt = *expires; result.sequence = *sequence;
    result.withdrawn = static_cast<bool> (value["withdrawn"]);
    if (! stringSet (value["platforms"], { "macos", "windows" }, result.platforms)
        || ! stringSet (value["formats"], { "AU", "VST3", "AAX" }, result.formats)) return {};
    if (result.platforms.size() == 1 && result.platforms[0] == "windows" && result.formats.contains ("AU"))
        return {};
    return result;
}
}
