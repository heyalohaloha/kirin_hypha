#pragma once
#include "../src/reference_audition/ReferenceAuditionProtocol.h"
#include "../src/reference_audition/ReferenceValidation.h"
#include <functional>
#include <regex>
#include <vector>

namespace hypha::tests
{
template<class Require> void verifyReferenceValidation (Require require, const juce::String& workId,
                                                        const juce::String& preparationId)
{
    namespace ref = hypha::reference_audition;
    namespace check = hypha::reference_audition::validation;
    require (ref::safeId (workId), "Work UUID must be a safe ID");
    require (! ref::safeId ("../escape"), "path separators must be rejected");
    require (ref::safeUuid (preparationId), "preparation UUID must validate");
    struct Case { const char* expression; juce::String sample; std::function<bool (const juce::String&)> accepts; };
    const std::vector<Case> cases {
        { R"(^[A-Za-z0-9._:-]{1,160}$)", "Ab09._:-", [] (const auto& s) { return check::id (s); } },
        { R"(^[a-f0-9]{64}$)", juce::String::repeatedString ("a0", 32), [] (const auto& s) { return check::sha256 (s); } },
        { R"(^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-5][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$)", "abcdef09-1234-4ABc-8abc-123456789abc", [] (const auto& s) { return check::uuid (s, check::Uuid::versioned); } },
        { R"(^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$)", "abcdef09-1234-4abc-8abc-123456789abc", [] (const auto& s) { return check::uuid (s, check::Uuid::anyLower); } },
        { R"(^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$)", "ABCDEF09-1234-4abc-8abc-123456789abc", [] (const auto& s) { return check::uuid (s, check::Uuid::anyCase); } },
        { R"(^[a-f0-9]{8}-[a-f0-9]{4}-4[a-f0-9]{3}-[89ab][a-f0-9]{3}-[a-f0-9]{12}$)", "abcdef09-1234-4abc-8abc-123456789abc", [] (const auto& s) { return check::uuid (s, check::Uuid::v4Lower); } },
        { R"(^[0-9]+(?:\.[0-9]+)?$)", "0123.45", [] (const auto& s) { return check::decimal (s); } },
        { R"(^[0-9]{4}-(0[1-9]|1[0-2])-([0-2][0-9]|3[01])T([01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\.[0-9]{3}Z$)", "2026-12-31T23:59:59.123Z", [] (const auto& s) { return check::timestamp (s); } },
    };
    for (const auto& item : cases)
    {
        const std::regex oldGrammar (item.expression);
        const auto compare = [&] (const juce::String& candidate)
        {
            require (item.accepts (candidate) == std::regex_match (candidate.toStdString(), oldGrammar),
                     "fixed validation must match the previous ASCII grammar");
        };
        compare ({}); compare (item.sample); compare (item.sample + "x");
        for (const auto* value : { "0", "0.0", "00.00", "1234", ".1", "1.", "1..2", "-1",
                                   "2026-02-00T00:00:00.000Z", "2026-02-31T23:59:59.999Z",
                                   "2026-13-01T00:00:00.000Z", "2026-01-01T24:00:00.000Z" }) compare (value);
        for (const int length : { 1, 63, 64, 65, 127, 128, 159, 160, 161 })
            compare (juce::String::repeatedString ("a", length));
        compare (item.sample.dropLastCharacters (1));
        compare (juce::String::fromUTF8 (u8"é"));
        for (int at = 0; at < item.sample.length(); ++at)
            for (int c = 1; c < 128; ++c)
                compare (item.sample.replaceSection (at, 1, juce::String::charToString (static_cast<juce::juce_wchar> (c))));
    }
    require (ref::safeId ("Ab09._:-") && !ref::safeId ("bad/"), "public ID validator delegates correctly");
    require (ref::safeSha256 (juce::String::repeatedString ("a", 64)) && !ref::safeSha256 ("A"), "public SHA validator delegates correctly");
    require (ref::safeUuid ("abcdef09-1234-4abc-8abc-123456789abc") && !ref::safeUuid ("bad"), "public UUID validator delegates correctly");
    require (ref::safeId (juce::String::repeatedString ("a", 160)), "160-character ID is valid");
    require (!ref::safeId (juce::String::repeatedString ("a", 161)), "161-character ID is invalid");
}
}
