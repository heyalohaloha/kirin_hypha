// A を Kirin 指紋と同じ定義で測り、Version の指紋と照合する（ReferenceKirinFingerprint）。Kirin OS の
// 解析と指紋（tests/fixtures/kirin_fingerprint/kirin_os_fingerprints.json）と、同じ信号を Hypha で測った値を
// 比べる：100 ms ごとのクロマ、LUFS-M、指紋のビット。照合は Kirin OS の照合と同じ一致率・ずれ・関係になる。
// Kirin OS は 100 ms の窓の一部を 1 サンプル早く読む（Kirin OS 側の不具合として別に直す）ので、クロマは
// その差の分だけ許す。
#include "reference_runtime_test_support.h"
#include "../src/reference_audition/ReferenceKirinFingerprint.h"
#include "../src/reference_audition/ReferenceVersionIdentify.h"

#include <cmath>
#include <map>

void testReferenceKirinFingerprint();

namespace
{
using namespace hypha::reference_audition;

void check (bool condition, const juce::String& message) { require (condition, message.toRawUTF8()); }

// fixture の説明どおりの信号（float32、interleaved の 2 ch）。lead 秒の無音を前に置ける。
std::vector<float> melody (int rate, double seconds, double lead = 0.0)
{
    constexpr double tau = 6.283185307179586476925286766559;
    const double notes[] { 261.63, 329.63, 392.0, 440.0, 523.25, 293.66, 349.23, 493.88, 220.0, 587.33, 659.25, 246.94 };
    const auto frames = static_cast<int> (rate * seconds), silent = static_cast<int> (rate * lead), half = rate / 2;
    std::uint32_t state = 4242;
    double phase = 0.0;
    std::vector<float> pcm (static_cast<size_t> (frames) * 2, 0.0f);
    for (int n = 0; n < frames - silent; ++n)
    {
        const auto step = n / half;
        phase += tau * notes[step % 12] / rate;
        state = state * 1664525u + 1013904223u;
        const auto noise = static_cast<double> (state) / 2147483648.0 - 1.0;
        const auto value = (0.15 + 0.1 * (step % 4)) * std::sin (phase);
        pcm[static_cast<size_t> (silent + n) * 2] = static_cast<float> (value);
        pcm[static_cast<size_t> (silent + n) * 2 + 1] = static_cast<float> (value + 0.03 * noise);
    }
    return pcm;
}

KirinFingerprint measured (int rate, const std::vector<float>& pcm, int ticks)
{
    KirinFingerprintMeter meter;
    check (meter.configure (rate, 2, ticks), "the fingerprint meter accepts " + juce::String (rate) + " Hz stereo");
    const auto frames = static_cast<int> (pcm.size() / 2);
    for (int offset = 0; offset < frames; offset += 256)
        meter.push (pcm.data() + static_cast<size_t> (offset) * 2, std::min (256, frames - offset));
    return meter.fingerprint (ticks);
}

KirinFingerprint fromFixture (const juce::var& signal)
{
    juce::MemoryOutputStream signs, loudness;
    juce::Base64::convertFromBase64 (signs, signal["chroma_signs_b64"].toString());
    juce::Base64::convertFromBase64 (loudness, signal["loudness_b64"].toString());
    return decodeFingerprint (signs.getMemoryBlock(), loudness.getMemoryBlock(), static_cast<int> (signal["ticks"]));
}
}

void testReferenceKirinFingerprint()
{
    const auto fixture = juce::JSON::parse (juce::File (KIRIN_REFERENCE_FIXTURE_DIR)
                                                .getChildFile ("kirin_fingerprint/kirin_os_fingerprints.json"));
    const auto* signals = fixture["signals"].getArray();
    require (signals != nullptr && signals->size() == 3, "the Kirin OS fingerprint fixture is readable");
    std::map<juce::String, KirinFingerprint> kirin, hypha;
    for (const auto& signal : *signals)
    {
        const auto name = signal["name"].toString();
        const int rate = static_cast<int> (signal["sample_rate"]);
        const int ticks = static_cast<int> (signal["ticks"]);
        kirin[name] = fromFixture (signal);
        const auto pcm = melody (rate, 15.0, name == "m48late" ? 1.0 : 0.0);
        hypha[name] = measured (rate, pcm, ticks);
        check (static_cast<int> (hypha[name].bits.size()) == ticks && kirin[name].bits.size() == hypha[name].bits.size(),
               "Hypha and Kirin OS hold the same ticks for " + name);

        // クロマと LUFS-M（区切りごと）。Kirin OS の読み込みは区切りの一部で 1 サンプル早い（その区切りは 1 サンプル
        // 重なって最後の 1 サンプルが抜ける）。区切りごとに「そのまま」と「1 サンプル早い」の両方を測り、Kirin OS
        // の値に近い方で進めて、Kirin OS が読んだ音の並びを再現する。どちらかで 0.01 dB 以内なら定義が同じ。
        KirinFingerprintMeter meter;
        meter.configure (rate, 2, ticks);
        const auto* chroma = signal["chroma_millidb"].getArray();
        const auto* lufs = signal["lufs_millilu"].getArray();
        const auto chromaTicks = static_cast<int> (chroma->size()) / 12;
        const auto tick = rate / 10;
        double worstChroma = 0.0, worstLufs = 0.0;
        int early = 0;
        for (int index = 0; index < chromaTicks; ++index)
        {
            const auto distance = [&] (const KirinFingerprintMeter& candidate)
            {
                std::array<double, 12> levels {};
                double loudness = 0.0;
                candidate.lastTick (levels, loudness);
                double worst = 0.0;
                for (int pitch = 0; pitch < 12; ++pitch)
                    worst = std::max (worst, std::abs (levels[static_cast<size_t> (pitch)] - static_cast<double> ((*chroma)[index * 12 + pitch]) / 1000.0));
                return std::pair { worst, loudness };
            };
            auto normal = meter, shifted = meter;
            normal.push (pcm.data() + static_cast<size_t> (index * tick) * 2, tick);
            if (index > 0) shifted.push (pcm.data() + static_cast<size_t> (index * tick - 1) * 2, tick);
            const auto a = distance (normal);
            const auto b = index > 0 ? distance (shifted) : std::pair { 1.0e9, 0.0 };
            const bool useShifted = b.first < a.first;
            meter = useShifted ? shifted : normal;
            early += useShifted ? 1 : 0;
            const auto& chosen = useShifted ? b : a;
            worstChroma = std::max (worstChroma, chosen.first);
            const auto& expected = (*lufs)[index];
            // 400 ms に満たない区切りと、デジタルの無音（Kirin OS は値なし、Hypha は −300 LUFS。指紋ではどちらも −70）。
            check (expected.isVoid() == (! std::isfinite (chosen.second) || chosen.second < -70.0),
                   "LUFS-M is missing for the same ticks (" + name + ")");
            if (! expected.isVoid()) worstLufs = std::max (worstLufs, std::abs (chosen.second - static_cast<double> (expected) / 1000.0));
        }
        check (worstLufs < 0.02, "LUFS-M follows Kirin OS within 0.02 LU (" + name + ", worst " + juce::String (worstLufs, 4) + ")");
        check (worstChroma < 0.01, "the chroma is Kirin OS's definition (" + name + ": worst " + juce::String (worstChroma, 4)
                                       + " dB, " + juce::String (early) + " ticks read one sample early by Kirin OS)");
        int same = 0;
        for (size_t index = 0; index < hypha[name].bits.size(); ++index)
            same += 12 - [] (std::uint16_t value) { int count = 0; for (; value != 0; value &= static_cast<std::uint16_t> (value - 1)) ++count; return count; }
                             (static_cast<std::uint16_t> (hypha[name].bits[index] ^ kirin[name].bits[index]));
        check (same >= static_cast<int> (hypha[name].bits.size()) * 12 * 97 / 100,
               "the fingerprint bits are Kirin OS's (" + name + ": " + juce::String (same) + " of "
                   + juce::String (static_cast<int> (hypha[name].bits.size()) * 12) + ")");
    }

    // 照合は Kirin OS の照合と同じ答え（一致率・相関・ずれ・関係）。
    for (const auto& item : *fixture["comparisons"].getArray())
    {
        const auto match = compareFingerprints (kirin[item["a"].toString()], kirin[item["b"].toString()]);
        check (std::lround (match.agreement * 1000.0) == static_cast<int> (item["agreement_milli"])
                   && std::lround (match.loudnessCorrelation * 1000.0) == static_cast<int> (item["loudness_correlation_milli"])
                   && std::abs (match.offsetTicks / 10.0 - static_cast<double> (item["offset_seconds"])) < 1.0e-9
                   && match.relation == FingerprintMatch::Relation::nearIdentical,
               "comparison matches Kirin OS for " + item["a"].toString() + " and " + item["b"].toString());
    }
    // Hypha が測った A と Kirin OS の Version の指紋：同じ音源、ずれ 0（1 秒遅れなら +1 秒）。
    const auto own = compareFingerprints (hypha["m48"], kirin["m48"]);
    const auto late = compareFingerprints (hypha["m48"], kirin["m48late"]);
    check (own.relation == FingerprintMatch::Relation::nearIdentical && own.offsetTicks == 0 && own.agreement > 0.98
               && late.relation == FingerprintMatch::Relation::nearIdentical && late.offsetTicks == 10,
           "A measured by Hypha finds the Kirin OS Version and its offset");
    // 曲の途中の 11 秒だけでも見つかる（曲の頭からの位置に置いた疎な並び）。
    auto slice = hypha["m48"];
    for (size_t index = 0; index < 40; ++index) slice.lufs[index] = -70.0f;
    const auto partial = compareFingerprints (slice, kirin["m48"]);
    check (partial.relation == FingerprintMatch::Relation::nearIdentical && partial.offsetTicks == 0,
           "a part of the song found at its place");
    // 違う曲（音名の並びを逆にした信号ではないが、音の無い指紋）とは照合できない。
    KirinFingerprint silent;
    silent.bits.assign (150, 0);
    silent.lufs.assign (150, -70.0f);
    check (compareFingerprints (hypha["m48"], silent).relation == FingerprintMatch::Relation::unknown,
           "too little sound in common gives no answer");
    // V の自動特定：A の直近（曲の 3〜15 秒、曲の頭からの位置に置く）を Version の指紋と照合し、Kirin OS の
    // 「同じ曲」以上で一致率の最も高いものを AUTO にする。音名をずらした別の曲は選ばない。
    auto other = kirin["m48"];
    for (auto& bits : other.bits) bits = static_cast<std::uint16_t> (((bits << 6) | (bits >> 6)) & 0x0fff);
    VersionIdentifier identifier;
    identifier.setCandidates ({ { "preset/check/other", other }, { "preset/check/late", kirin["m48late"] } });
    KirinFingerprint recent;
    recent.bits.assign (hypha["m48"].bits.begin() + 30, hypha["m48"].bits.end());
    recent.lufs.assign (hypha["m48"].lufs.begin() + 30, hypha["m48"].lufs.end());
    const auto identity = identifier.identify (recent, 149);
    check (identity.autoId == "preset/check/late" && identity.autoAgreement > 0.95 && ! identity.matches.empty()
               && identity.matches.front().versionId == "preset/check/late" && ! identity.matches.front().anywhere,
           "the Version whose fingerprint agrees at the DAW position is AUTO (" + identity.autoId + ")");
    check (identifier.identify ({}, 149).autoId.isEmpty(), "without A's sound nothing is chosen");
    // 曲が DAW の時間軸のどこにあっても（アルバムの 2 曲目・位置が分からない）時間軸全体で探して見つける。
    // Windows の windef.h は near と far を空のマクロにするので、名前に使わない。
    const auto elsewhere = identifier.identify (recent, 2'149);
    const auto unplaced = identifier.identify (recent, -1);
    check (elsewhere.autoId == "preset/check/late" && elsewhere.matches.front().anywhere && unplaced.autoId == "preset/check/late",
           "a song placed later on the DAW timeline, or with no DAW position, is found anywhere");
    identifier.setCandidates ({ { "preset/check/other", other } });
    check (identifier.identify (recent, 149).autoId.isEmpty() && identifier.identify (recent, 2'149).autoId.isEmpty(),
           "a different song is never AUTO");
    // 同じ曲の別ミックス（音名が少し違い、音量の流れは似ている）は Kirin OS の「同じ曲」で AUTO にする
    // （作業中のミックスと前の書き出しを結ぶのが V の自動特定の役目）。
    auto remix = kirin["m48late"];
    for (std::size_t tick = 0; tick < remix.bits.size(); ++tick)
    {
        remix.bits[tick] = static_cast<std::uint16_t> (remix.bits[tick] ^ (1u << (tick % 12)) ^ (1u << ((tick + 5) % 12)));
        remix.lufs[tick] = remix.lufs[tick] > -50.0f ? remix.lufs[tick] * 0.8f - 2.0f : remix.lufs[tick];
    }
    identifier.setCandidates ({ { "preset/check/remix", remix } });
    const auto sameSong = identifier.identify (recent, 149);
    check (! sameSong.matches.empty() && sameSong.matches.front().relation == FingerprintMatch::Relation::sameSong
               && sameSong.autoId == "preset/check/remix",
           "another mix of the same song is AUTO");
    // 音名の並びは合っても音量の流れがついてこない（別の曲の似た所）は AUTO にしない（相関の下限 0.3）。
    auto lookalike = kirin["m48late"];
    std::uint32_t seed = 12345;  // 元の音量の流れと関係のない流れ（決まった擬似乱数、−35〜−15 LUFS）
    for (auto& lufs : lookalike.lufs)
    {
        seed = seed * 1664525u + 1013904223u;
        lufs = -35.0f + 20.0f * static_cast<float> (seed >> 8) / static_cast<float> (1u << 24);
    }
    identifier.setCandidates ({ { "preset/check/lookalike", lookalike } });
    const auto unrelated = identifier.identify (recent, 149);
    check (! unrelated.matches.empty() && unrelated.matches.front().loudnessCorrelation < autoMinimumLoudnessCorrelation
               && unrelated.autoId.isEmpty(),
           "a passage whose harmony agrees but whose loudness does not follow is never AUTO ("
               + (unrelated.matches.empty() ? juce::String ("none") : juce::String (unrelated.matches.front().agreement, 3) + " / "
                  + juce::String (unrelated.matches.front().loudnessCorrelation, 3) + (unrelated.matches.front().anywhere ? " anywhere" : " near"))
               + ")");
    // 位置の手がかりの無い照合では、弱い「同じ曲」（一致率 0.62〜0.70）は採らない（取り違えを防ぐ）。
    auto weak = kirin["m48late"];
    for (std::size_t tick = 0; tick < weak.bits.size(); ++tick)
        weak.bits[tick] = static_cast<std::uint16_t> (weak.bits[tick] ^ (tick % 3 == 0 ? 0x00f : tick % 3 == 1 ? 0x0f0 : 0xf00));
    identifier.setCandidates ({ { "preset/check/weak", weak } });
    const auto weakNear = identifier.identify (recent, 149), weakFar = identifier.identify (recent, 2'149);
    // 弱い同じ曲は DAW の位置では必ず AUTO になる（何も AUTO にならなくても通る書き方だった。2026-10-06）。
    check (! weakNear.matches.empty() && weakNear.matches.front().agreement < strongSameSongAgreement
               && weakNear.matches.front().agreement >= 0.62
               && weakNear.matches.front().relation == FingerprintMatch::Relation::sameSong
               && weakNear.autoId == "preset/check/weak" && weakFar.autoId.isEmpty(),
           "a weak same-song match is AUTO at the DAW position, never anywhere on the timeline ("
               + (weakNear.matches.empty() ? juce::String ("none") : juce::String (weakNear.matches.front().agreement, 3) + " / "
                  + juce::String (weakNear.matches.front().loudnessCorrelation, 3)) + ")");

    // 選び方：同じ Version が 2 回続けて最良になってから選ぶ。利用者が選んだ Version は替えない。AUTO の選んだ
    // ものは、別の Version が一致率で 0.02 以上上回り続けたときだけ選び直す。
    const auto best = [] (const juce::String& id, double agreement, const juce::String& otherId, double otherAgreement) {
        VersionIdentity result;
        result.matches = { { id, agreement, FingerprintMatch::Relation::sameSong, 0.8, false, true },
                           { otherId, otherAgreement, FingerprintMatch::Relation::sameSong, 0.8, false, true } };
        result.autoId = id; result.autoAgreement = agreement;
        return result;
    };
    AutoVersionChooser chooser;
    check (chooser.next (best ("v2", 0.90, "v1", 0.80), {}, false).isEmpty()
               && chooser.next (best ("v2", 0.90, "v1", 0.80), {}, false) == "v2",
           "AUTO chooses after the same best Version twice in a row");
    check (chooser.next (best ("v2", 0.90, "v1", 0.80), "v1", false).isEmpty()
               && chooser.next (best ("v2", 0.90, "v1", 0.80), "v1", false).isEmpty(),
           "a Version the user chose is never replaced");
    check (chooser.next (best ("v2", 0.81, "v1", 0.80), "v1", true).isEmpty()
               && chooser.next (best ("v2", 0.81, "v1", 0.80), "v1", true).isEmpty(),
           "AUTO keeps its choice while another Version is only slightly better");
    check (chooser.next (best ("v2", 0.90, "v1", 0.80), "v1", true).isEmpty()
               && chooser.next (best ("v2", 0.90, "v1", 0.80), "v1", true) == "v2",
           "AUTO corrects its own choice when another Version stays clearly better");
    check (chooser.next (best ("v2", 0.90, "v1", 0.80), "v2", true).isEmpty(), "nothing to do once chosen");

    // 音量は Kirin OS と同じ 2 段で丸める（millilu の整数にしてから 0.5 LU）。−20.2504 は −20250 millilu を経て
    // −20.0（1 段なら −20.5）。
    const auto rounded = fingerprintFrom ({ std::array<double, 12> {} }, { -20.2504 });
    check (rounded.lufs.size() == 1 && std::abs (rounded.lufs[0] + 20.0f) < 1.0e-6f, "loudness is rounded in Kirin OS's two steps");
    std::cout << "Reference Kirin fingerprint: chroma, LUFS-M, bits, comparison and V identification follow Kirin OS PASS\n";
}
