# ATTACK Phase 2-R MIDI provenance報告

**日付**：2026-08-30
**実装ラベル**：B-551
**対象profile**：DRUM
**MIDI provenance判定**：Go
**DRUM候補freeze判定**：No-Go、正式採点未実施
**公開判定**：No-Go、ATTACKはOFF

## 1. 判定範囲

B-551は、B-550で固定した290件のDRUM development manifestを、公式E-GMD MIDI-only archiveの実memberへ結び付けた。
同じarchive bytesからSHA-256、ZIP構造、選択member、MIDI eventを検証し、展開rootを正本として信頼する経路を廃止した。

選択290件では、manifest記載のmember SHA-256とarchive内の実bytesが全件一致した。
元MIDIと30秒上限excerptのevent数もB-550 receiptと一致し、cross-IDおよびcross-splitの重複は0件だった。

このGoはMIDI archive-member provenanceに限る。
audio provenance、blind acoustic audit、candidate score、winner、fresh holdout、runtime、公開ATTACKのGoではない。

| 対象 | 判定 | 根拠 |
|---|---|---|
| 公式MIDI archive identity | Go | 107,076,192 bytes、固定SHA-256一致 |
| ZIP構造 | Go | 45,571 entry、独立central-directory検査合格 |
| 選択MIDI member | Go | train 246件、validation 44件、全290件一致 |
| MIDI event再計算 | Go | sourceとexcerptの全aggregate一致 |
| canonical label重複 | Go | raw、source composite、excerpt compositeの全て0件 |
| formal candidate score | No-Go | authorization、audio、context、blind auditが未成立 |
| 2MIX | No-Go、未着手 | DRUMのMIDI契約を転用しない |
| 公開ATTACK | No-Go | runtimeとfresh holdoutを含む公開gate未実施 |

## 2. 固定した親artifact

B-551は、入力pathやCLIから渡されたhashを信頼の起点にしない。
検証コードへ固定したhashと実bytesを比較し、親artifactの意味も再検証する。

| 親artifact | SHA-256 |
|---|---|
| B-550 development manifest | `80ebe2961ece9833f554f98430e6617aad2496603f0c105c611dfa710938ad8c` |
| B-550 fold metadata | `51c0b30d535a2819dd17b0a49e410c378e2d436eb5f98be06caff5757be45675` |
| B-550 development receipt | `57fcd65d2b9f265796ba2142f367a89bd932e1cefd73c76f09e96d743963153a` |
| E-GMD metadata | `80677e8fb00e973f33cb91ddaaf7f0cffe55359f9a76c1833ce56c84d1d92c64` |
| E-GMD MIDI-only archive | `5e70a6f4d760385a5e5ec986a2f02179d96f61181a920e592876b577a75844d3` |
| opened-set ledger | `e9935efba336a40b44ba46bcaa234117927c05e36bb5ca8f80f257b8ba58b3ca` |
| test isolation incident | `27c10a5c6de848029deb18c394181be76ed373d67269773f519708cf53494257` |

development receiptはschema、purpose、profile、dataset identity、artifact名、行数、fold qualification、MIDI aggregate、未解決blockerを再検証する。
receipt全体の固定hashだけに依存せず、将来pinを更新するときも意味の欠落を検出する。

manifestは23列、290行、selection rank連番、selection key再計算、五fold各58件、train 246件、validation 44件を要求する。
source duration decimalから44.1 kHz整数sampleを再計算し、candidate非依存のhash-window境界と各event aggregateを照合する。

## 3. ZIP検証境界

検証器は公式MIDI archiveを一つのfile handleから一回だけ読み、固定長のimmutable bufferへ格納する。
archive SHA-256、ZIP解析、選択memberの展開は全て同じbufferを使う。
hash確認後にpathを開き直す経路はない。

ZIP libraryのmember名検索だけでは、重複名やUnicode extra fieldによる名前置換を見落とす可能性がある。
B-551はEOCDとcentral directoryを独立に解析し、libraryが返すentry数とliteral filenameを照合する。

次の条件を一つでも満たさないarchiveは拒否する。

- archive全体のbyte数とSHA-256が固定値と一致する。
- EOCDがterminal位置にあり、multi-disk、ZIP64 sentinel、central-directory gapを含まない。
- 独立解析とZIP libraryのentry数が45,571件で一致する。
- central directoryの全literal filenameがUTF-8かつ安全なASCII relative pathである。
- exact、canonical、case-foldの各名前集合に重複がない。
- member data rangeが重ならない。
- 選択memberのcentral filenameとlocal header filenameがbyte単位で一致する。
- 選択memberがregular fileであり、symlink、暗号化、非対応圧縮ではない。
- 選択memberの宣言size、実size、展開率、aggregate size、CRC32が固定上限内で一致する。

選択memberはmanifestのrank順に一回だけbounded展開する。
展開後の同じbufferからmember SHA-256とMIDI eventを計算し、decode対象を別のpathから読み直さない。

## 4. MIDIとexcerptの再計算

各MIDIは共通の整数時刻parserでtempo mapを積分し、note-on時刻を整数µsへhalf-upする。
velocity 0を除外し、pitch、velocity、時刻を保持する。

source eventは、先頭noteから30,000 µs以内を一つにするnon-chaining compoundである。
excerptは先にraw noteを半開sample区間でcropし、そのraw noteだけからcompoundを作り直す。

crop判定には次の整数式を使う。

`start_sample * 1,000,000 <= note_micros * 44,100 < end_sample * 1,000,000`

したがってend sample上のnoteは含まれない。
時刻境界へ`f64`の丸め誤差を持ち込まない。

sourceとexcerptについて、raw note、compound、kick-only、hat-onlyを各memberで比較する。
全290件のaggregateは次のとおり一致した。

| 範囲 | raw note | compound | kick-only | hat-only |
|---|---:|---:|---:|---:|
| source | 77,161 | 53,868 | 4,499 | 9,070 |
| excerpt | 25,168 | 18,211 | 1,342 | 3,471 |

選択memberの非圧縮合計は932,889 bytes、圧縮合計は546,934 bytesだった。
excerptが空のperformance IDは0件である。

## 5. canonical重複監査

異なるperformance IDへ同じ素材が混入すると、290件という見かけの証拠量だけが増える。
B-551はmember bytesに加えて、MIDI意味列を正規化したdigestも比較する。

sourceはabsolute integer µsを使い、note列とcompound列を別domainでhashする。
excerptは`time_micros * 44,100 - start_sample * 1,000,000`というexact cross-product numeratorを使い、core startからの相対時刻へ正規化する。
pitch、velocity、class flag、compound内のnote範囲、excerpt長もbindingへ含める。

重複を拒否するkeyは次の三種類である。

- raw archive member SHA-256。
- source note digestとsource compound digestのcomposite。
- 空でないexcerptのnote digest、compound digest、excerpt長のcomposite。

note digestまたはcompound digestの片方だけが一致しても拒否しない。
単純なリズムclassやevent時刻だけの偶然一致を、同一素材と誤判定しないためである。

| 重複class | group数 | cross-split group数 |
|---|---:|---:|
| raw member | 0 | 0 |
| source canonical composite | 0 | 0 |
| nonempty excerpt canonical composite | 0 | 0 |

## 6. receiptの再現性

同じコードと固定入力から二回実行し、508,423 bytesのreceiptがbyte単位で一致した。
receipt SHA-256は`7c923cf224f8201d0496c304cb160b0cc8859340cdb0b74c7b490b3cd6223447`である。

receiptは親hash、archive構造、全290 memberの個別証拠、aggregate、重複監査、隔離assertion、未解決blockerをrank順に記録する。
時刻、hostname、絶対path、candidate scoreを含めない。

出力は同一directoryの一時fileへcreate-newで書き、file sync後にhard linkでno-clobber publishする。
既存resultを上書きせず、stage失敗時はこの実行が作った一時fileだけを回収する。

## 7. 隔離assertionの意味

B-551はarchive全体107,076,192 bytesを不透明な圧縮bytesとして読み、hashとcentral-directory検査へ使った。
したがって「testを含むarchive bytesへ一切触れていない」とは主張しない。

semanticに展開してMIDI parserへ渡したのは、manifestで固定したtrain 246件とvalidation 44件だけである。
unselected memberとtest memberの展開およびsemantic parseは0件だった。
audio、fresh holdout、2MIX、candidate scoreも開いていない。

これらは実行経路が記録した`operational_assertions_not_evidence`である。
mount、ACL、sandboxの到達不能性を証明するものではなく、formal authorizationがcaller booleanとして信頼することもない。

## 8. 残るNo-Go条件

MIDI member provenanceが成立しても、MIDI compoundを可聴attackの公開正解として使えるとは確定しない。
固定audio excerptを二人が候補出力なしで注釈し、MIDI proxyのPrecisionとRecallを別々に検証する必要がある。

formal scorerは引き続きfilesystem access前に停止する。
B-551 receiptをFormalAuthorizationへ結ぶsource-pinned semantic verifier、audio ingest receipt、実source context、blind audit、sealed candidate planが未完成だからである。

次工程を次の順序へ固定する。

1. official audio archiveを同じone-buffer原則で検証し、source PCM、core PCM、guard付きPCMのhashと重複を記録する。
2. source sample 0をframe originとし、実contextをdecodeしながらcore半開区間だけを採点する。
3. 固定audio bytesでblind acoustic auditを完了する。
4. development、MIDI、audio、fold、audit、candidate planをsource commitの認可hashへ結ぶ。
5. sealed candidate setを五fold、LODO、LOSO、runtimeで一度だけ評価する。
6. guardian裁定または新しい未接触holdoutを確定してfresh評価へ進む。

工程5までDRUM candidate freezeはNo-Goである。
工程6とPhase 3以降のruntime、macOS、Windows gateが完了するまでATTACKは公開No-Go、OFFを維持する。

2MIXは同じ画面の別profileだが、DRUMのMIDI receipt、annotation、threshold、definition hash、Goを転用しない。
2MIX用dataとaudio-only annotationは独立に作る。

## 9. 参照

- [zip 8.6.0 API documentation](https://docs.rs/zip/8.6.0/zip/)
- [Expanded Groove MIDI Dataset](https://magenta.withgoogle.com/datasets/e-gmd)
- `docs/transient_delta_phase2_formal_development_gate_report_20260830.md`
- `docs/transient_delta_phase2_recovery_plan_20260830.md`
