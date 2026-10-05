#include "../src/update/UpdateManifest.h"
#include <iostream>

int main (int argc, char** argv)
{
    using namespace hypha::update;
    const auto fixture = argc > 1 ? juce::File (argv[1])
                                 : juce::File (__FILE__).getSiblingFile ("fixtures/update_manifest_fixture.json");
    const auto value = juce::JSON::parse (fixture.loadFileAsString());
    const auto publicKey = value["publicKey"].toString();
    const auto now = static_cast<juce::int64> (value["now"]);
    const auto* cases = value["cases"].getArray();
    if (cases == nullptr || cases->isEmpty() || publicKey.isEmpty()) return 1;
    int failures = 0;
    for (const auto& row : *cases)
    {
        const auto result = verifyManifest (row["wire"].toString(), publicKey, now,
                                            static_cast<juce::int64> (row["previous"]));
        if (result.has_value() != static_cast<bool> (row["expected"]))
        {
            std::cerr << "FAIL " << row["name"].toString() << '\n'; ++failures;
        }
        if (result && (result->version != "1.1.51" || result->sourceCommit != juce::String::repeatedString ("a", 40)
                       || result->sequence != 5 || result->platforms.size() != 2 || result->formats.size() != 3))
            ++failures;
    }
    const auto valid = (*cases)[0]["wire"].toString();
    if (verifyManifest (valid, "", now, 0) || verifyManifest (valid, "invalid", now, 0)
        || verifyManifest (valid, publicKey.replaceSection (0, 5, "00003"), now, 0)
        || verifyManifest (valid, publicKey, -1, 0) || verifyManifest (valid, publicKey, now, -1)
        || verifyManifest ("{\"payload\":[[[[]]]],\"signature\":\"x\"}", publicKey, now, 0)
        || verifyManifest (juce::String::repeatedString ("x", maximumManifestBytes + 1), publicKey, now, 0)) ++failures;
    for (const auto text : { "1.2.3", "0.0.0", "2147483647.1.0" }) if (! validSemVer (text)) ++failures;
    for (const auto text : { "01.2.3", "1.2", "1..3", "1.2.3-beta", "1.2.3+build", "2147483648.0.0" })
        if (validSemVer (text)) ++failures;
    if (compareVersions ("1.2.9", "1.2.10") != -1 || compareVersions ("2.0.0", "1.99.99") != 1
        || compareVersions ("1.2.3", "1.2.3") != 0) ++failures;
    std::cout << "update manifest: " << cases->size() << " signed cases, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
