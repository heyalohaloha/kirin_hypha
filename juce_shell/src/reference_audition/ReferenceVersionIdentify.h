#pragma once

#include "ReferenceKirinFingerprint.h"
#include "ReferenceRuntimeV2Model.h"

#include <map>
#include <memory>
#include <vector>

namespace hypha::reference_audition
{
// V の自動特定。A の直近の指紋（Kirin OS と同じ定義）を、Kirin OS が Version ごとに
// 残した指紋（ranges の fingerprint）と照合し、Kirin OS の「同じ曲」以上で一致率の最も高い Version を AUTO に
// する（作業中のミックスと前の書き出しは、ふつう「同じ曲」になる）。Kirin OS のしきい値をそのまま使う。
//  - まず DAW の位置で照合する（曲の頭からの位置に置き、ずれは ±30 秒）。
//  - どれも AUTO に届かなければ、曲が DAW の時間軸のどこにあっても探す（アルバムの 2 曲目など）。位置の手がかりが
//    無いぶん、弱い「同じ曲」（一致率 0.62〜0.70）は採らない。
//  - AUTO にするのは Kirin OS の「同じ曲」のうち、一致率 0.70 以上の側にも音量の流れの相関 0.3 以上があるもの。
//    曲全体どうしで決めた Kirin OS のしきい値に比べ、直近 30 秒の切り出しは別の曲と偶然そろいやすい（別の曲の似た所は
//    一致率が届いても音量の流れがついてこない）。この下限で、当たる数を変えずに誤りを減らす（2026-10-03）。
// 選び方（chooseAutoVersion）：利用者が選んだ Version は替えない。同じ Version が 2 回続けて最良になってから
// 選び、AUTO の選んだものは、別の Version が一致率で 0.02 以上、2 回続けて上回ったときだけ選び直す。今の選択が照合に
// 無い・一時的に条件を外れたときは、最後に分かった一致率を基準にする（2026-10-06：相関が一瞬下がるだけで差なしに替わり、
// 戻ると替わり直していた）。V の選択だけを替え、鳴っている B・C は止めない（ReferenceComparisonController::selectVersion の
// automatic）。
// Version の指紋の読み込み（ファイル）は Reference の作業スレッドで行い、照合はメッセージスレッドで行う。
struct VersionMatch
{
    juce::String versionId;
    double agreement = 0.0;
    FingerprintMatch::Relation relation = FingerprintMatch::Relation::unknown;  // Kirin OS の関係（一覧に出す）
    double loudnessCorrelation = 0.0;
    bool anywhere = false;  // DAW の位置から離れた所で見つけた（時間軸全体の照合）
    bool eligible = false;  // AUTO にできる（下の条件）
};

inline constexpr double strongSameSongAgreement = 0.70;  // Kirin OS の「同じ曲」の一致率だけの条件
inline constexpr double autoMinimumLoudnessCorrelation = 0.3;  // AUTO の一致率 0.70 以上の側の音量の流れの相関の下限
// AUTO にできるか。DAW の位置では Kirin OS の同じ曲（0.70 以上の側に相関の下限を足す）、時間軸全体では 0.70 以上の側だけ。
bool autoEligible (const FingerprintMatch&, bool anywhere) noexcept;

struct VersionIdentity
{
    std::vector<VersionMatch> matches;  // 一致率の高い順
    juce::String autoId;                // AUTO にできるもののうち一致率が最も高い Version（無ければ空）
    double autoAgreement = 0.0;
};

class VersionIdentifier
{
public:
    // Version の指紋を ranges から読む（作業スレッド）。library（publication と sets）が同じなら読み直さない。
    // 読めなかった ranges（Kirin OS が書いている途中など）は覚えず、retryMs ごとに読み直す。指紋の無い
    // ranges（古い Kirin OS）は読めたものとして覚える。今の library で使わない指紋は捨てる。
    void prepare (const juce::File& root, const RuntimeWorkspace&, std::int64_t nowMs);
    // 試験用：Version の指紋を直接置く。
    void setCandidates (std::vector<std::pair<juce::String, KirinFingerprint>>);
    size_t candidateCount() const;
    // A の直近の指紋（slice）。最後の区切りが曲の頭から endTick 番目（100 ms 単位）。どのスレッドからでもよい。
    VersionIdentity identify (const KirinFingerprint& slice, std::int64_t endTick) const;

    static constexpr std::int64_t retryMs = 5000;

private:
    using Candidates = std::vector<std::pair<juce::String, KirinFingerprint>>;
    std::shared_ptr<const Candidates> current() const;

    mutable juce::CriticalSection lock;  // candidates の差し替えだけを守る
    std::shared_ptr<const Candidates> candidates = std::make_shared<const Candidates>();
    // ここから下は prepare（作業スレッド）だけが使う。
    std::map<juce::String, KirinFingerprint> printsByRanges;  // ranges の sha256 ごと（読めたものだけ）
    juce::String preparedKey;
    std::int64_t retryAtMs = 0;  // 読めなかった ranges があれば、この時刻を過ぎたら読み直す
};

// AUTO の選び直しの規則が覚えていること（呼ぶ側が持つ）。
struct AutoChoiceMemory
{
    juce::String candidate;        // 続けて最良になっている Version
    int streak = 0;                // 何回続けて最良になったか
    juce::String knownId;          // 一致率を最後に知った AUTO の選択
    double knownAgreement = 0.0;   // その一致率
};

struct AutoChoice
{
    juce::String chosen;   // 選ぶ Version（替えなければ空）
    AutoChoiceMemory memory;
};

inline constexpr int autoConfirmations = 2;       // 同じ Version が続けて最良になる回数
inline constexpr double autoSwitchMargin = 0.02;  // AUTO の選んだものを替えるのに要る一致率の差

// AUTO の選び直しの規則（純粋な関数。表の試験で固める）。currentId は今の V の選択、currentIsAuto はそれを AUTO が
// 選んだか。手の選択は替えない。V が空なら、同じ Version が 2 回続けて最良になってから選ぶ。AUTO の選択は、ほかの
// Version がその一致率（今の照合で条件を満たしていればその値、無ければ最後に分かった値）を 0.02 以上、2 回続けて上回った
// ときだけ替える（INV-S51）。
AutoChoice chooseAutoVersion (const VersionIdentity&, const juce::String& currentId, bool currentIsAuto, AutoChoiceMemory);

// 画面が照合のたびに呼ぶ入れ物（規則は chooseAutoVersion）。選ぶ Version を返す（選ばなければ空）。
class AutoVersionChooser
{
public:
    juce::String next (const VersionIdentity& identity, const juce::String& currentId, bool currentIsAuto)
    {
        auto choice = chooseAutoVersion (identity, currentId, currentIsAuto, memory);
        memory = std::move (choice.memory);
        return choice.chosen;
    }

private:
    AutoChoiceMemory memory;
};
}
