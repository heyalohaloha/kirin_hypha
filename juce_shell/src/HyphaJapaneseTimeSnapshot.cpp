#include "HyphaJapaneseCatalog.h"

namespace hypha::i18n::catalog
{
Section timeSnapshotSection() noexcept
{
    static const Entry entries[] {
        { "Latest Session input coverage is not confirmed; MAX TP is a confirmed lower bound and PLR is unavailable.",
          u8"最新のSession入力範囲を確認できていません。MAX TPは確認済みの下限を示し、PLRは未確定です。" },
        { "Session values cover processed audio. The final analysis chunk is pending; MAX TP is a confirmed lower bound and PLR is unavailable.",
          u8"Sessionの値は処理済みの音声を対象にしています。末尾の解析単位は未処理のため、MAX TPは確認済みの下限を示し、PLRは未確定です。" },
        { "Waiting for corresponding PRE observation", u8"同時刻のPRE観測を待機中" },
        { "PRE observation stopped; history held", u8"PRE観測が停止・履歴を保持" },
        { "PRE and POST observations are incompatible", u8"PREとPOSTの観測は比較できません" },
        { "PRE observation unavailable", u8"PRE観測を取得できません" },
        { "Waiting for corresponding observation", u8"同時刻の観測を待機中" },
        { "Stopped; history held", u8"停止中・履歴を保持" },
        { "Observation expired; history held", u8"観測の期限終了・履歴を保持" },
        { "Observation unavailable", u8"観測を取得できません" },
        { "PSR unavailable for this observation", u8"この観測のPSRは未成立" },
        { "Observed through %1 s", u8"観測済み：%1 sまで" },
        { "Work cannot preserve this observation; save the local PNG instead",
          u8"この観測の意味をWorkへ保存できません。ローカルPNGを保存してください" },
        { "PSR = 400 ms sample peak minus 3 s loudness at one 100 ms point. PSR independently compares POST minus PRE when the same moment is verified; otherwise it shows the local PRE or POST observation. Waiting never substitutes POST.",
          u8"PSRは同じ100 msの観測点の400 msサンプルピークから3 sラウドネスを引いた値です。全体表示とは独立して、同時刻の対応が確認できる場合はPOST−PRE、それ以外は単体のPREまたはPOSTを示します。差分の待機中にPOST値へ置き換えません。" },
        { "PSR history stops at its verified cutoff. Gaps and different runs are not connected.",
          u8"PSR履歴は対応を確認できた時刻まで表示します。欠測や異なる再生区間を線でつなぎません。" },
        { "PLR/CORR follow main. TIME PLR: processed Session prefix at that completed 100 ms point; LEVEL: latest Session. PSR is independent.",
          u8"PLR/CORRは全体表示の対象。TIMEのPLRは完了した100 ms時点の処理済みSession範囲、LEVELは最新。PSRは独立。" },
        { "M, S and TP share one observation. The common time axis follows the local cutoff; a delayed PSR comparison does not stop POST history.",
          u8"M・S・TPは同じ観測の値です。共通の時間軸は単体の観測終端まで進み、PSRの比較待ちでPOST履歴を止めません。" },
    };
    return { "TIME snapshot", entries, sizeof (entries) / sizeof (entries[0]) };
}
}
