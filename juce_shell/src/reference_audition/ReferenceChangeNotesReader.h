#pragma once
#include "ReferenceChangeNotesValidation.h"
#include "ReferenceChangeNotesFile.h"
#include "ReferenceChangeNotesJson.h"
#include <juce_cryptography/juce_cryptography.h>

namespace hypha::reference_audition
{
// Only the selected Version's existing non-audio worker calls this bounded reader.
inline ChangeNotesReadResult readChangeNotes (const juce::File& root,
                                              const ChangeNotesReceipt& receipt,
                                              const RuntimeCandidate& candidate)
{
    const auto fail = [] (const char* issue) { return ChangeNotesReadResult { {}, issue }; };
    if (! change_notes_detail::receiptValid (receipt)) return fail ("reference_change_notes_receipt");
    change_notes_detail::NotesFile stream (root, receipt.sha256);
    if (stream.length() != receipt.bytes) return fail ("reference_change_notes_bytes");
    juce::MemoryBlock bytes (static_cast<size_t> (receipt.bytes));
    size_t offset = 0;
    while (offset < bytes.getSize())
    {
        const auto received = stream.read (static_cast<char*> (bytes.getData()) + offset,
                                            static_cast<int> (bytes.getSize() - offset));
        if (received <= 0) return fail ("reference_change_notes_bytes");
        offset += static_cast<size_t> (received);
    }
    char extra = 0;
    if (stream.read (&extra, 1) != 0 || stream.length() != receipt.bytes)
        return fail ("reference_change_notes_bytes");
    if (juce::SHA256 (bytes).toHexString() != receipt.sha256) return fail ("reference_change_notes_hash");
    const auto* raw = static_cast<const char*> (bytes.getData());
    const auto size = static_cast<int> (bytes.getSize());
    if ((size >= 3 && static_cast<unsigned char> (raw[0]) == 0xef
                   && static_cast<unsigned char> (raw[1]) == 0xbb
                   && static_cast<unsigned char> (raw[2]) == 0xbf)
        || ! juce::CharPointer_UTF8::isValidString (raw, size)) return fail ("reference_change_notes_encoding");
    // Embedded NUL must not truncate the JSON accepted after the receipt was hashed.
    for (int i = 0; i < size; ++i) if (raw[i] == '\0') return fail ("reference_change_notes_encoding");
    if (! change_notes_detail::JsonShape (raw, bytes.getSize()).valid()) return fail ("reference_change_notes_schema");
    juce::var document;
    if (juce::JSON::parse (juce::String::fromUTF8 (raw, size), document).failed())
        return fail ("reference_change_notes_schema");
    return change_notes_detail::validate (document, candidate, receipt.sha256);
}
}
