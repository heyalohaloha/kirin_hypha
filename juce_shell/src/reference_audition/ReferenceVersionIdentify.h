#pragma once

#include "ReferenceKirinFingerprint.h"
#include "ReferenceRuntimeV2Model.h"

#include <map>
#include <vector>

namespace hypha::reference_audition
{
// H7: V の自動特定（方向設計 §3.4）。A の直近の指紋（Kirin OS と同じ定義。曲の頭からの位置に置く）を、
// Kirin OS が Version ごとに残した指紋（ranges の fingerprint）と照合し、「同じ曲」以上で一致率の最も高い
// Version を AUTO にする。照合のずれは ±30 秒（DAW の位置と Version の時間の差）。手動の選択は常に残し、
// AUTO は Version を選んでいないときだけ選ぶ（画面）。Kirin OS のしきい値（K6）をそのまま使う。
struct VersionMatch
{
    juce::String versionId;
    double agreement = 0.0;
    FingerprintMatch::Relation relation = FingerprintMatch::Relation::unknown;
};

struct VersionIdentity
{
    std::vector<VersionMatch> matches;  // 一致率の高い順
    juce::String autoId;                // 同じ曲以上のうち一致率が最も高い Version（無ければ空）
    double autoAgreement = 0.0;
};

class VersionIdentifier
{
public:
    // Version の指紋を ranges から読む。library（publication と sets）が同じなら読み直さない。
    void prepare (const juce::File& root, const RuntimeWorkspace&);
    // 試験用：Version の指紋を直接置く。
    void setCandidates (std::vector<std::pair<juce::String, KirinFingerprint>>);
    size_t candidateCount() const noexcept { return candidates.size(); }
    // A の直近の指紋（slice）。最後の区切りが曲の頭から endTick 番目（100 ms 単位）。
    VersionIdentity identify (const KirinFingerprint& slice, std::int64_t endTick) const;

private:
    std::vector<std::pair<juce::String, KirinFingerprint>> candidates;
    std::map<juce::String, KirinFingerprint> printsByRanges;  // ranges の sha256 ごと
    juce::String preparedKey;
};
}
