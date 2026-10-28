# 未署名候補を手で通す

署名の前に同じ source の未署名候補で普通の操作を一度通す。公開 CI の待ち時間に実施できる。
この記録は署名済み候補の正式受入を代用しない。採用 source を直したら影響範囲を通し直す。
DAW の起動、挿入、操作、保存、再起動、書き出しはすべて担当者が手で行う。

## 準備

[build-only 入口](../hypha_build_entry.md)で未署名候補を作る。AAX は外部 SDK と開発用 host の条件を確認する。
未署名確認のために license の移動を要求しない。既存の公開版、session、設定を保全し、
同じ ID の複数候補を同居させず、読み込んだ path と候補 hash を run identity に一度記録する。
Windows は同じ source の未署名 factory artifact を使う。

## host ごとの通し

1. POST を先に挿し、PRE が無い表示を確認する。PRE を挿して組を選ぶ。
2. 普通の音を再生し、表示、音の経路、Keep → Stop → Record を確認する。
3. bypass と解除、停止と再開、seek を試す。直した時計・ループ・PDC の部分は fixRows に従って深く見る。
4. editor の開閉と resize を一度行い、session を保存する。DAW を終了し、別 process で開き直して組と値を確認する。
5. 同じ区間を Hypha 無し／通常計測ありで書き出す。比較試聴は終了し、gain、dither、normalization、
   fade、tail、sample rate、channel、開始位置、長さを両方で揃える。非周期的な信号を含める。
   書き出しは別々の file にし、float64 を推奨する。float32 への丸めだけでは double の下位 bit の後退を捕まえられない。
6. Record を閉じ、writer が終了してから選んだ record directory を収集する。出力先は source の外に新設する。

```bash
# PCM16/24/32、IEEE float32/64、対応する WAVE extensible を decoded sample ごとに比較。
# gain、整列、trim、許容差は一切適用しない。出力の保存先は新しい束の中を選ぶ。
PYTHONDONTWRITEBYTECODE=1 python3 scripts/acceptance/wav_null.py /exports/without.wav /exports/with.wav > /evidence/null.json
node scripts/acceptance/collect_records.mjs /closed/record_sessions /evidence/records
```

WAV 比較の exit 0 は同じ rate・channel・frame 数で全 sample の差が 0、非有限値が 0。
exit 1 は差または非有限値、exit 2 は不正／非対応／異なる geometry。差がある場合の latency は未確定なので
`latencySamples: null` とする。差が無い場合だけ固定した frame 位置での `latencySamples: 0` を報告する。
無音だけの一致は host latency の証明にならない。RF64 と圧縮 WAV は対象外で、通常 RIFF WAV へ書き出す。
JSON の hash は比較した WAV 自体に結ぶ。record manifest は全収集 file の相対 path・bytes・SHA-256 を保持する。
収集中の変更、symlink、既存の収集先は拒否する。収集だけで Record の整合性を PASS としない。

Logic は Cycle → Keep → 周回 → Stop と停止再開、REAPER は loop と PDC、
Windows の停止型 bypass、AAX は読み込んだ path と host 固有の時計を担当者が確認する。
共通の流れは代表 host に寄せ、もう一方の OS は固有部分を確認する。

## REAPER の証拠収集

[設定例](reaper_acceptance_config.example.json)は形だけを示す。実際の path、音源、鍵は
repository 外の本人専用設定に書き、設定 file は本人だけが読める権限にする。
設定・操作 log・鍵設定の確認記録は所有者以外への mode 権限に加え ACL も検査する。
拡張 ACL がある場合は保守的に拒否するので、ACL を変更せず本人専用の安全な場所に保存し直す。
凍結前に REAPER を使う host 行と候補の Record 鍵設定を結ぶ。候補の producer が実際に使用した
鍵設定を独立に確認し、候補 ID・commit・鍵の照合値・確認内容を [形の例](reaper_key_provenance.example.json)
に従って repository 外へ保存する。照合値は次の command で取得する。鍵そのものを report や証拠へ書かない。
この確認ができない候補では REAPER の証拠を PASS 行に結ばない。

```bash
node scripts/acceptance/reaper_policy.mjs --key-proof PRIVATE_CONFIG
node scripts/acceptance/reaper_policy.mjs --bind PROFILE vst3 PRIVATE_CONFIG PRIVATE_PRODUCER_EVIDENCE
```

AU の場合は `au` とする。`--bind` は凍結前の profile の該当行へ方式と確認済み鍵設定を登録し、
凍結後の変更を拒否する。別の host の手動行は従来どおり。候補の producer の設定と、この私用設定が
一致することの確認は人が行う。照合値だけから producer がその鍵を使ったとは推定しない。
候補・凍結・PRE / POST binary と private な保存先を設定する。操作の開始前に log を初期化し、各操作を実際に
行った後、それに対応する event を一つ記録する。途中の log を完成済みとして収集できない。

```bash
PYTHONDONTWRITEBYTECODE=1 node scripts/acceptance/reaper_run.mjs --record-operations PRIVATE_CONFIG
```

表示された各操作を行ってから Enter を押す。順序は start、insert、record-start、record-stop、
save、close、reopen、export、finish。途中で終了した log は不完全として保持する。
個別に続ける場合は `--event PRIVATE_CONFIG EVENT` で次の操作だけを記録する。
log は操作を記録するものであり、独立 review が実際の操作を確認する。開き直しと export の後、
設定の PID を開き直した REAPER のものに更新し、候補と凍結は変えずに収集する。
開始前に project／session、選択範囲の開始・終了 frame、選択した音源の file と SHA-256、rate、
frame 数、PRE／POST の instance ID を設定する。選択音源 file の hash は開始時に確認し、基準 export と
同じ bytes でなければならない。Record の期待 WAV は processed export の hash・rate・frame 数に一致させる。
保存・閉じる・開き直し・export の記録も同じ設定へ結ぶ。Record の commit 時刻は停止から終了までに入れる。

```bash
PYTHONDONTWRITEBYTECODE=1 node scripts/acceptance/reaper_run.mjs PRIVATE_CONFIG
```

収集は process に mapping された binary の path・inode と file の hash、Record の組と checksum、
export の一致を確認する。読み込み後に binary を差し替えず、変更した場合は host を再起動する。
同じ executable 名の別 copy が同時に mapping されている場合は拒否する。mapping は track 上の選択までは証明しないため、独立 review で確認する。
選んだ pair の manifest と二つの member だけを収集し、他の session は複製しない。
export の実 file は複製せず private な path と hash を記録するので、正式 gate が終わり証拠の保持条件を確認するまで保持する。
結果は独立 review 待ちで、これだけで実 host の PASS にしない。独立 review 後の A4 report では、
対応する macOS の format 行の evidence に次の形で結ぶ。sha256 は収集の出力に表示される。
正式 gate は凍結候補の PRE / POST binary、
選択した Record、保持した export を再照合する。手動で記入する結果・実測・oracle は省かない。

```json
{ "kind": "reaper", "format": "vst3", "path": "PRIVATE_OBSERVATION_PATH", "sha256": "OBSERVATION_SHA256" }
```

同じ行に `kind: "host-identity"` の独立確認 file とその hash も結ぶ。[形の例](reaper_host_identity.example.json)を
repository 外に作る。この file は
`schema: "hypha-host-identity-v1"`、同じ candidate と freeze SHA、開き直した process ID、
`os: "macOS"`、`osVersion`、`locale`、`architecture: "x86_64"`、独立確認の記録 `review` を持つ。
process の mapping だけでは host architecture を証明しないため、担当者が別途確かめる。
独立 review では、実際に開いた project と選択区間、音源と二つの出力の由来、各 track の PRE／POST と
instance ID、Record の開始・停止と commit、保存後に閉じて同じ project を開き直したことを確認して記録する。
hash と手入力 log は実操作の由来そのものを証明しない。

Logic は Cycle と停止・再開で通常音声を聴き、Pro Tools は PRE / POST を挿して通常音声を聴く。
host 固有の時計と結果は担当者がそれぞれ短い手動行に残す。

## 受入と記録の型

- 一行一束。複数行が同じ証拠 file を指してよい。identity（候補、OS、locale、host、path）は run の始めに一度。
- 人が hash を固める境目は候補、凍結、packages、最終公開物の四点。途中の実 file 照合は coordinator に任せる。
- 独立 review は host の通しの最後に一度。ケースごとの待ちを作らない。
- 二分進まなければ、操作、表示、状態、再開条件を一行に記録して次へ。調査は確認済み FAIL に絞る。
- 未実施を PASS にしない。全行の穴埋めや coverage 件数を完了条件にしない。
  本人の延期判断は凍結後に正式な owner-decision へ結び、定量的安全条件を緩めない。

正式受入の依頼には [雛形](acceptance_request_template.md)を使う。
