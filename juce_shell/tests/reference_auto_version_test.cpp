// 2026-10-06：V の AUTO（ReferenceVersionIdentify）。
//  - AUTO にできるかのしきい値（DAW の位置では Kirin OS の同じ曲：一致率 0.70 以上かつ相関 0.3 以上、または弱い同じ曲
//    0.62 以上かつ相関 0.5 以上。時間軸全体では 0.70 以上かつ相関 0.3 以上だけ）を表で固める。
//  - 選び直しの規則（chooseAutoVersion）を表で固める：手の選択は替えない、2 回続けて、0.02 の差、今の選択が一瞬
//    条件を外れても最後に分かった一致率を基準にする（相関が一瞬下がるだけで差なしに替わり、戻ると替わり直していた）。
//  - Version の指紋の読み込み（VersionIdentifier::prepare）を、Kirin OS の書き出しの写しで一度通し、読めなかった
//    ものは 5 秒ごとに読み直す。
#include "reference_runtime_test_support.h"
#include "KirinLibraryFixture.h"
#include "../src/reference_audition/ReferenceRuntimeV2Repository.h"
#include "../src/reference_audition/ReferenceVersionIdentify.h"

void testReferenceAutoVersion (const juce::File&);

namespace
{
using namespace hypha::reference_audition;

void check (bool condition, const juce::String& message) { require (condition, message.toRawUTF8()); }

FingerprintMatch matchOf (double agreement, double correlation)
{
    FingerprintMatch match;
    match.agreement = agreement;
    match.loudnessCorrelation = correlation;
    match.relation = agreement >= strongSameSongAgreement ? FingerprintMatch::Relation::sameSong
                   : agreement >= 0.62 ? FingerprintMatch::Relation::sameSong : FingerprintMatch::Relation::different;
    return match;
}

void thresholds()
{
    struct Row { double agreement, correlation; bool anywhere, eligible; const char* why; };
    const Row rows[] {
        { 0.70, 0.30, false, true, "the Kirin OS same song with the loudness following" },
        { 0.70, 0.29, false, false, "harmony alone is not enough" },
        { 0.69, 0.49, false, false, "a weak match needs the loudness to follow closely" },
        { 0.62, 0.50, false, true, "a weak same song at the DAW position" },
        { 0.61, 0.90, false, false, "below the weak same song" },
        { 0.70, 0.30, true, true, "a strong match anywhere on the timeline" },
        { 0.69, 0.90, true, false, "no weak match anywhere on the timeline" },
    };
    for (const auto& row : rows)
        check (autoEligible (matchOf (row.agreement, row.correlation), row.anywhere) == row.eligible,
               juce::String ("AUTO threshold: ") + row.why);
}

VersionIdentity identityOf (std::initializer_list<VersionMatch> matches)
{
    VersionIdentity identity;
    identity.matches = matches;
    std::stable_sort (identity.matches.begin(), identity.matches.end(), [] (const auto& a, const auto& b) { return a.agreement > b.agreement; });
    for (const auto& match : identity.matches)
        if (match.eligible) { identity.autoId = match.versionId; identity.autoAgreement = match.agreement; break; }
    return identity;
}

VersionMatch versionMatch (const char* id, double agreement, bool eligible = true)
{
    return { id, agreement, FingerprintMatch::Relation::sameSong, eligible ? 0.8 : 0.1, false, eligible };
}

void choosing()
{
    struct Step { VersionIdentity identity; const char* chosen; };
    struct Case { const char* why; juce::String current; bool currentIsAuto; std::vector<Step> steps; };
    const std::vector<Case> cases {
        { "with V empty, the same best Version twice in a row is chosen", {}, false,
          { { identityOf ({ versionMatch ("v2", 0.90), versionMatch ("v1", 0.80) }), "" },
            { identityOf ({ versionMatch ("v2", 0.90), versionMatch ("v1", 0.80) }), "v2" } } },
        { "a Version the user chose is never replaced", "v1", false,
          { { identityOf ({ versionMatch ("v2", 0.99), versionMatch ("v1", 0.70) }), "" },
            { identityOf ({ versionMatch ("v2", 0.99), versionMatch ("v1", 0.70) }), "" } } },
        { "AUTO keeps its choice while another is less than 0.02 better", "v1", true,
          { { identityOf ({ versionMatch ("v2", 0.819), versionMatch ("v1", 0.80) }), "" },
            { identityOf ({ versionMatch ("v2", 0.819), versionMatch ("v1", 0.80) }), "" } } },
        { "AUTO changes when another is 0.02 better twice in a row", "v1", true,
          { { identityOf ({ versionMatch ("v2", 0.83), versionMatch ("v1", 0.80) }), "" },
            { identityOf ({ versionMatch ("v2", 0.83), versionMatch ("v1", 0.80) }), "v2" } } },
        { "one better reading is not enough", "v1", true,
          { { identityOf ({ versionMatch ("v2", 0.83), versionMatch ("v1", 0.80) }), "" },
            { identityOf ({ versionMatch ("v1", 0.80), versionMatch ("v2", 0.78) }), "" },
            { identityOf ({ versionMatch ("v2", 0.83), versionMatch ("v1", 0.80) }), "" } } },
        // 今の選択の相関が一瞬下がって条件を外れても、最後に分かった一致率（0.85）を基準にする。
        { "a moment out of the conditions does not hand AUTO to a weaker Version", "v1", true,
          { { identityOf ({ versionMatch ("v1", 0.85), versionMatch ("v2", 0.80) }), "" },
            { identityOf ({ versionMatch ("v1", 0.85, false), versionMatch ("v2", 0.80) }), "" },
            { identityOf ({ versionMatch ("v1", 0.85, false), versionMatch ("v2", 0.80) }), "" },
            { identityOf ({ versionMatch ("v1", 0.85), versionMatch ("v2", 0.80) }), "" } } },
        { "a Version that leaves the list keeps its last agreement as the bar", "v1", true,
          { { identityOf ({ versionMatch ("v1", 0.85), versionMatch ("v2", 0.80) }), "" },
            { identityOf ({ versionMatch ("v2", 0.86) }), "" },
            { identityOf ({ versionMatch ("v2", 0.86) }), "" },
            { identityOf ({ versionMatch ("v2", 0.87) }), "" },
            { identityOf ({ versionMatch ("v2", 0.87) }), "v2" } } },
    };
    for (const auto& item : cases)
    {
        AutoChoiceMemory memory;
        auto current = item.current;
        auto isAuto = item.currentIsAuto;
        int index = 0;
        for (const auto& step : item.steps)
        {
            auto choice = chooseAutoVersion (step.identity, current, isAuto, memory);
            memory = choice.memory;
            check (choice.chosen == juce::String (step.chosen),
                   juce::String ("AUTO choice: ") + item.why + " (step " + juce::String (++index) + ": " + choice.chosen + ")");
            if (choice.chosen.isNotEmpty()) { current = choice.chosen; isAuto = true; }
        }
    }
}

// Version の指紋を、Kirin OS の書き出しの写しから読む。読めなかった ranges は 5 秒たつまで読み直さない。
void preparing (const juce::File& sandbox)
{
    const auto root = kirin_library_fixture::copy (sandbox, "auto-version-library");
    require (root != juce::File(), "the Kirin OS library fixture is copied inside the test folder");
    RuntimeV2Repository repository (root);
    const auto loaded = repository.refreshLibrary();
    require (loaded.usable(), "the fixture library must be read");
    const auto rangesFolder = root.getChildFile ("ranges");
    const auto hidden = root.getChildFile ("ranges-hidden");
    require (rangesFolder.moveFileTo (hidden), "the Cue values are not written yet");
    VersionIdentifier identifier;
    identifier.prepare (root, *loaded.workspace, 1'000);
    check (identifier.candidateCount() == 0, "a Version whose Cue values cannot be read yet has no fingerprint");
    require (hidden.moveFileTo (rangesFolder), "Kirin OS finishes writing the Cue values");
    identifier.prepare (root, *loaded.workspace, 1'000 + VersionIdentifier::retryMs - 1);
    check (identifier.candidateCount() == 0, "the Cue values are not read again before 5 seconds");
    identifier.prepare (root, *loaded.workspace, 1'000 + VersionIdentifier::retryMs + 1);
    check (identifier.candidateCount() == 1, "after 5 seconds the Version's own fingerprint is read");
}
}

void testReferenceAutoVersion (const juce::File& sandbox)
{
    thresholds();
    choosing();
    preparing (sandbox);
}
