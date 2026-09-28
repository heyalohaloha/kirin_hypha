# 連続PRE/POST比較 G1実測: Studio Pro 8.1.2のloop、seek、遅延補償、到着、転送、周回、plugin sleep、動的PDC（VST3とAU）

作成日: 2026-09-28。
対象: [実装計画](hypha_live_chain_compare_implementation_plan_20260927.md)第12.1節のG1-01（静的対応）とG1-03（境界）のうち、1 hostの一部条件。
第6節は同じhostでのG1-02（到着期限）とG1-06（転送）の限定条件（同一process、短時間）である。
第7節は、第6節の対策（PREの周回）をplugin内で動かした再試験と、そこで見つかったplugin sleepの影響である。
第8節は、呼出しの空白の規則をplugin内で動かした再試験、G1-04（再生中の遅延変更）の限定条件、sleepを避ける手段の観察である。
本書は実測の記録であり、host認定、製品の実装、公開の判断ではない。
2026-09-28の見直しで、第8.2節の判定を事前に決めた件数の条件に照らして書き直し、全試験に共通する限界（遅延の報告の誤り、周回単位の取り違え）を第8.6節に加えた。

## 1. 条件

| 項目 | 値 |
| --- | --- |
| host | macOS Studio Pro 8.1.2 Build 113407（`/Applications/Studio Pro 8.app`）、Intel Mac |
| format / rate | VST3とAU、48 kHz |
| buffer | device 256 samples。pluginへのcallbackは全件2048 frames |
| Song | 新規の空Song。120 BPM、4/4、オーディオのテンポストレッチなし。保存せずに閉じた |
| 音源 | 24秒、48 kHz、stereo float32の識別PCM（固定seedの−40 dBFSノイズ、全1152000 frameが一意）。小節1の頭に配置 |
| 計測器 | 非出荷のClock Diagnostic（`juce_shell/tests/host_clock_diagnostic`、source ee522a53。統合後mainとの差はeditorの書体のみ）。JUCE 4f43011b＋tracked patch 8件（検証script OK）。AU版は同じsourceを作業領域でAUとしてbuildした別ID（`com.kirinmastering.clock-diagnostic-au`）で、auvalをpass |
| 遅延 | 既存の非出荷PDC Validation Delay 4096（1.1.49、B1で使用した版） |

試験Aは「診断 → 診断」、試験Bは「診断 → 遅延4096 → 診断」を同じtrackへ挿入した。
VST3版とAU版で、それぞれ試験AとBを行った。遅延はどちらもVST3版を使った。
各試験で、4秒loop（小節1〜3）を約20秒、8.125秒loop（小節1〜小節5＋16分）を約40秒再生した。
途中でloop内のseekを1回、停止と再生を1回入れた。
操作はClaudeが利用者の承認のもとで画面操作により行った。

照合は識別PCMのexactなsample bitだけで行い、相関は使っていない。
各callbackの先頭と末尾のsampleを音源上の位置へ写し、報告されたproject時刻とVST3連続時刻（auxiliary）と比べた。

## 2. 結果

| 項目 | 試験A 上下とも | 試験B 上（PRE相当） | 試験B 下（POST相当、遅延の後） |
| --- | --- | --- | --- |
| 定常の「内容位置 − project時刻」 | 0 | 0 | 0 |
| loop境界でのcallback分割 | なし。境界をまたぐcallbackが12回。整列した折返しは周期的に起きた | なし（境界をまたぐcallbackが11回） | なし |
| 境界をまたぐcallbackの報告位置 | 折返し前の開始位置（12/12） | 同（11/11） | 下の行を参照 |
| 境界をまたぐcallbackから求めたloop長 | 192000と390000（設定と一致） | 同 | 同 |
| loop折返しのproject時刻 | 内容と一致 | 内容と一致 | 13回すべてで、loop先頭の0に留まる間の内容は折返し前の末尾。12回は2 block（4096 samples）、1回はフェードで一致しないsampleがあり1 blockだけ確認 |
| VST3連続時刻 | loopで途切れない | 同。seek時だけ跳ぶ | 同。同じ周回の同じ内容でPREと同じ値（一致ブロック2106中2104） |
| 出力presentation latency | 報告なし | 4096 | 0 |
| seek | 新位置へ即時 | 新位置へ即時 | 時刻は「新位置 − 4096」へ即時。内容は4096 samplesの間、旧位置の続き |
| 停止後の再生 | 開始位置から | 開始位置から | 時刻は「開始位置 − 4096」から。内容は4096 samplesの無音 |
| hostによるフェード | loop終端の直前でフェードアウト（最後のsample約−5.6e−6）、再生開始とseekの直後にフェードイン | 同 | 同 |

AU版（試験A・Bとも、2048 frames、48 kHz）:

| 項目 | 試験A 上下とも | 試験B 上（PRE相当） | 試験B 下（POST相当、遅延の後） |
| --- | --- | --- | --- |
| 定常の「内容位置 − project時刻」 | 0 | 0 | 0 |
| loop境界でのcallback分割 | なし（境界をまたぐcallbackが12回） | なし（10回） | なし |
| 境界をまたぐcallbackの報告位置 | 折返し前の開始位置（12/12） | 同（10/10） | 折返し前ではない |
| loop折返しのproject時刻 | 内容と一致 | 内容と一致 | VST3と同じく、chain遅延の間loop先頭に留まり、内容は折返し前の末尾 |
| AU render時刻（連続時計） | 途切れない | 途切れない。seekと再生開始でも跳ばない | 同。値の起点がinstanceごとに異なる |
| 同じ内容でのPREとPOSTの連続時計の差K | − | − | 同じ内容のk回目の出現どうしで1771組中1769組が370688の定数。POSTのproject時刻が内容と食い違う24ブロックでもすべて同じK |
| 出力presentation latency | 報告なし | 4096 | 0 |

補足:

- 4秒loop（192000 samples）と2048 framesでは、境界をまたぐ位置が周回ごとに512 samplesずつずれ、4周に1回だけcallback終端とloop終端が一致した。
- POSTでproject時刻が内容と食い違ったブロックは27個（14区間）だった。VST3連続時刻でPREと照合すると25個が正しく対応し、project時刻では0個だった。残る2個はseek直後の区間で、PREにその時刻の内容が存在しない。
- 試験Aの報告なしと試験Bの4096/0から、出力presentation latencyの差（PRE − POST）は間のchain遅延と一致した。この値は任意情報であり、単独で対応の根拠にしない。
- AUのKは、各instanceの起点の差と間のchain遅延を合わせた値と考えられるが、起点は観測できないので、内訳は推定しない。Kが一定であることだけを事実とする。
- K = 370688以外の2組は、同じ内容の出現回数がPREとPOSTで異なる内容（seekをまたぐ区間）であり、k回目どうしの対応付けが成り立たない。

## 3. 計画への意味

1. **loop境界での分割**: このhostはloop境界でcallbackを分割しない。第7.2節と第7.3節の非分割hostの反例は実在する条件である。exact 4秒loopは、遅延のないchainでは既存rendererが整列する周回（この条件では4周に1回）まで開始を待つ。遅延のあるchainでは、次項により範囲の終端で試行が失効する。
2. **POSTのproject時刻**: 遅延のあるchainでは、折返しのたびにchain遅延の長さだけ誤る。project時刻でPREを引く方式では、loopのたびにL samplesの誤対応か欠落になる。第5.3節の「POSTだけが境界を示す」場合に当たり、A′では周回ごとに再選択が必要になる。
3. **連続時計**: VST3の連続時刻は、PREとPOSTで同じ内容に同じ値を持ち（K = 0）、loopの折返しで途切れなかった。再生位置の移動では値がproject時刻へ戻る。AUのrender時刻は、instanceごとに起点が異なり、PREとPOSTの差Kは一定だった。loop、seek、再生開始のどれでも途切れなかった。
   どちらも「連続時計 + K」を対応の鍵にでき、Kは定常区間のproject時刻の一致から較正できる見込みがある（第6節で較正を実施）。VST3ではK = 0だが、seekと再生開始の直後は、POSTの最初のL samplesに対応するPREが現在のrunに存在しない。ringのその格納位置には以前のrunの音が残るため、書込み末尾だけで到着を判定すると誤って受け入れる（第6節）。PREのrun世代を照合に含めて欠落として扱い、POSTへ倒す。AUでは、seekと再生開始の後も「render時刻とK」でPREとの対応が続いた（第6節）。
   Kは定常区間で常に照合し、変わったら（動的PDC、instanceの再生成など）対応を切る。採否と較正の規則はG1Rの判断事項である。
4. **余白付き窓（第7.2節）**: POST側のproject時刻を使う限り、両端にU = Lの余白が要る。第8版の「終端側は余白を要しない」は、このhostのPOST側では成り立たない。連続時刻を使う場合は、境界を含むcallbackの分割の問題だけが残る。
5. **既存Local Blind**: rendererはPOSTのproject時刻を使う。遅延のあるchainで4秒範囲がloop終端からL samples以内にかかると、再生中に不連続として試行が失効し、POSTの音へ戻る。誤った音は出ないが、利用者から見ると失敗になる。既存Blindのhost gate（G1-10）で扱う。
6. **hostのフェード**: PREとPOSTの入力の両方に同じく入るため、対応は崩れない。固定4秒の範囲がloop終端や再生開始にかかる場合は、フェードも中身に含まれる。

## 4. 範囲と未確認

- 1 host、VST3とAU、48 kHz、process block 2048、遅延4096の1条件、Intel Macでの結果である。遅延pluginはVST3版だけを使った。
- Windows、他のbuffer設定、Dropout Protectionの違い、AAX、他のDAW、動的PDC（G1-04）は測っていない。到着期限（G1-02）と転送（G1-06）は第6節と第7節の限定条件だけである。
- 連続時刻の性質を他のhostや条件へ一般化しない。
- 試験は利用者の承認を得たうえで、Claudeが画面操作で行った。各操作の時刻は秒単位の目安であり、結果はCSVの値だけから導いた。

## 5. 証跡と再現

| 物 | 場所とSHA-256 |
| --- | --- |
| 試験A 上 | `~/KirinValidation/HyphaClockDiagnostic/522d8338a38546eabe791c30eb48e9f8-2618.csv` `25859f81e74a19f0b100cfcabb5891ab0a7978193f64758a4d66133d68862d68` |
| 試験A 下 | `c90099d589fd4aa08448fcf2821145ce-2640.csv` `1a696dd1a730b64b124e15ffda9ebb1e783cdbe86917358ff1a172e73cbaed65` |
| 試験B 上 | `a8b2c1d175914ff4be49d8aa679c81b8-2603.csv` `46d5b1be72634429ebf685fd47f0a085dc543e360f805db710c49efea7c43c58` |
| 試験B 下 | `875133b98c6341ce8437faa3f61891ba-2519.csv` `7ee231c58f3ac6b1a7819cd31634af47ad6439eb3eafef60176cf1915d43c65c` |
| 識別PCM | `~/KirinValidation/G1-loop/hypha-g1-clock-fixture-48k.wav` `727fd737fcf2d0fa53d131530f443d43cf30177b601ef436c690b73a42b57309` |
| loop解析script | `~/KirinValidation/G1-loop/analysis/loop-analyze.mjs` `8d1186670a56591ffe2f170138fbd54952a313cc3e467d5784cfee687ef49f3c`。模擬host 6条件の試験（同じ場所の`loop-analyze.test.mjs`）がpass |
| 診断pluginのbinary | VST3 `1ffa9bf5cf614f3e147394f2d4da5a7d6e36e011798917c683190f01fa728e18`、AU `f029c8fd4c26a296fd1bc8437d4ef92dfee070d308defbc6d977219612d743ff`。試験後にuser領域から削除した |
| AU試験A 上 / 下 | `a7359d7b50a94d2ea1e1ab9c7d18ade6-2110.csv` `bb3df218e844dca258a675df467e75d20e635b6193e8ec70d71b206454eeada3` / `1f1641ab1a44445aa37c7269529c1c27-2134.csv` `d24b61cc6b4552f806fb857c89693d43760b85b0b3eb93b6dd7560139803d315` |
| AU試験B 上 / 下 | `26461e7cd53c4adbbe88d2c69cda3820-2233.csv` `f47ebc355084321e585ac7a5ac0af7b20396eec8f8f398150d49807721097f41` / `c473795e227e49539fc90b08effad478-2137.csv` `82092214f9076adce0fcdc8aa2d6a15bc6b41489d424a21c76c2ceb6b96febb4` |

再現の手順は、既存の`juce_shell/tests/host_clock_diagnostic/README.md`の構成に従う。
48 kHzの識別PCMは同じ場所の`make-fixture.mjs`で生成できる。
解析は`node loop-analyze.mjs fixture.wav trace.csv ...`と、既存の`analyze-trace.mjs`で行った。

## 6. G1-02とG1-06のプローブ（到着、転送、run世代）

### 6.1 条件

| 項目 | 値 |
| --- | --- |
| host、rate、block | 第1節と同じ（Studio Pro 8.1.2、Intel Mac、48 kHz、callback 2048 frames）。Songは新規の使い捨て（保存せずに閉じた） |
| 計測器 | 非出荷のTransport Probe PRE/POST（VST3とAU、bundle ID `com.kirinmastering.transport-probe-pre/-post`、製品と別名）。JUCEは第1節と同じ |
| 転送 | macOSのPOSIX共有メモリ（`/kh-tprobe-v1`、2^19 frames × 2ch float、descriptor 1024件のseqlock）。PREとPOSTは同じhost process内の別module |
| PRE | 入力を加工せず出力し、連続時計（VST3連続時刻、AU render時刻）で索引した位置へ書く。書込み末尾と公開件数を公開する |
| POST | 定常区間でproject時刻が一致するPRE blockから差K（POSTの連続時計 − 同じ内容のPREの連続時計）の候補を求め、同じ候補が8回続いたら採用する。「連続時計 − K」でPREを引き、到着（書込み末尾 ≥ 必要範囲の末尾）、ring上書き、全sampleのbit一致、processBlock内の所要時間（`mach_absolute_time`）を記録する |
| 操作 | 各試験で、4秒loopと8.125秒loopの再生（VST3試験Aは8.125秒loopだけ）、再生中のseek 1回、停止と再生1回。Claudeが利用者の承認のもとで画面操作で行った |

試験Aは「PRE → POST」、試験Bは「PRE → 既存PDC Validation Delay 4096（VST3版）→ POST」である。
VST3試験Aは、試験Bの後の停止中に遅延pluginだけを外し、同じPRE/POST instanceで続けた。
そのためVST3試験Aの解析は、試験Bの書出し以降の行だけを使った。

### 6.2 結果

評価blockは、再生中でKが較正済みのPOST callbackである。

| 試験 | K | 評価block | 到着 | 余白（samples） | exact不一致 | run世代で再判定 | 所要時間 p99.9 / 最大（POST、PRE） |
| --- | --- | --- | --- | --- | --- | --- | --- |
| AU A | −126976（全件） | 1566 | 1566 | 0 | 0 | 1566を受入れ | 24.5 / 26.7 µs、24.0 / 28.3 µs |
| AU B | 268288（全件） | 1500 | 1500 | 4096 | 0 | 1500を受入れ | 23.8 / 25.7 µs、22.7 / 30.0 µs |
| VST3 B | 0（全件） | 1513 | 1513 | 4096 | 4 block（各2048/2048 sample） | 1509を受入れ、不一致の4 blockだけ棄却 | 23.3 / 25.0 µs、23.1 / 24.1 µs |
| VST3 A | 0（全件） | 942 | 942 | 0 | 0 | 942を受入れ | 22.2 / 22.3 µs、23.5 / 24.2 µs |

- 到着: 書込み末尾による判定では、全5521 blockがPOSTの読取り時点で到着済みだった。このうちVST3試験Bの4 blockは別runの内容であり（6.3節）、run世代で判定し直すと5517 blockが到着済みだった。余白は間のchain遅延と同じで、隣接構成では0だった。隣接構成の2508 blockすべてで、同じcycleのPREがPOSTより先に公開を終えていた。
- K: AUは試験ごとに異なる定数、VST3は0だった。どの試験でも較正後にKは変わらなかった。
- 所要時間: 2048 frames（約42.7 ms）のcallbackあたり最大30.0 µs（約0.07%）。POSTはblock全体のbit比較を含む。page faultを避けるため、共有memoryは割当て時に全pageへ触れてある。
- AU render時刻は、AUの2試験のどちらでも、loop、seek、停止中、再生開始のどれでも途切れなかった。seek後と再生開始後も、POSTの内容は「render時刻 − K」のPREとbit一致した。
- VST3連続時刻は、seekと再生開始でproject位置へ戻った。PREとPOSTは同じcycleで跳び、POSTの跳び先は「PREの新位置 − chain遅延」だった（試験Bで383520と379424）。

### 6.3 VST3試験Bの誤受入れ

seekの直後と再生開始の直後に、POSTの2 block（= chain遅延4096 samples）が到着判定を通り、内容は全sampleで一致しなかった。
POSTが引いた時計値（379424〜383519）は、PREの新しいrun（383520から開始）には存在しない。
ringは時計値を容量2^19 framesで折り返して格納する。その格納位置には、seekの約3.7秒前に時計値1952288〜1956383で書かれた音が残っていた。
書込み末尾（385568以上）は必要範囲の末尾を越えていたので、到着判定を通った。再生開始の直後も、同じ格納位置の同じ音を読んだ。
POSTのproject時刻の不連続として検出できたのは、2 blockのうち最初の1 blockだけである。

オフライン再生で、次の規則を記録済みのcallbackへ当て直した。
PREは、連続時計が直前のblockの末尾と連続しないたびに新しいrunを始める。
POSTは、必要範囲がPREの現在のrunで書かれた範囲（run先頭から書込み末尾まで）に収まり、ring容量内の場合だけ受け入れる。
POSTの各読取りは、両側が記録した公開件数で、その瞬間に公開済みだった最後のPRE blockへ結び付けた。
結果は、4試験の全5521 blockで誤受入れ0、正常blockの棄却0だった。VST3試験Bでは、誤っていた4 blockだけが棄却された。

これはplugin内での実装ではない。
実装では、PREがrunの識別子、run先頭、書込み末尾を一貫して公開し、POSTは読取り中にrunが変わったら棄却する必要がある。
「現在のrunだけを受け入れる」規則は、POSTがPREの跳びより遅れて旧runの範囲を読む配置では、正しい範囲も棄却する（安全側）。今回の配置では起きなかった。

### 6.4 停止中のplugin sleep

VST3試験BとAの間の約9.3分の停止中、PREへのprocess呼出しは179回だった。
同じ間にPOSTへの呼出しは13063回あった。表示していたeditorはPOSTだった。
PREへの呼出しは再生開始で戻った。
insertのメニューでは「プラグインのスリープを有効化」にcheckが付いていた（遅延pluginで確認）。
停止中の出来事であり、6.2節の結果には影響しない。
再生中の無音入力で呼出しが止まるか、AU render時刻がこの眠りをまたいで連続するかは、第7.4節で確かめた（どちらも起きる）。

### 6.5 計画への意味

1. **対応の鍵にrun世代が要る**: 連続時計の値だけではrunを区別できない。VST3連続時刻のように値がseekや再生開始で戻る時計では、同じ値が別のrunで再利用される。鍵は「PREのrun、連続時計、K」とし、run先頭より前の範囲を受け入れない。第9版の「欠落として検出できる」は、この照合を前提にしてだけ成り立つ。
2. **VST3のseekと再生開始**: chain遅延の長さだけ、現在のrunに対応するPREが無い区間が生じる。A′のとおりPREの選択を解除する。
3. **AUのseekと再生開始**: このhostでは全sampleの対応が続いた。A′の「全sampleの対応が続く境界は解除しない」に当たる候補であり、認定は動的PDC、plugin sleep、Windowsの確認後にする。第7.4節のとおり、plugin sleepでKが変わるので、呼出しの空白を扱う規則が前提になる。
4. **到着**: このhostの同一process、負荷なし、2048 framesでは未到着0だった。隣接構成の余白は0であり、同じcycleでPOSTがPREより先に処理されるhostでは全blockが未到着になる。hostごとに測る。
5. **転送**: POSIX共有memoryと事前確保のringで、RTの追加時間は最大30 µs程度だった。30分以上の連続、CPU負荷、別process、file-backed、Windowsは未実施である。

### 6.6 範囲と未確認

- 1 host、Intel Mac、48 kHz、2048 frames、PREとPOSTが同じformat、同じprocess、負荷なし、各試験の再生は約40〜67秒である。
- run世代の規則はオフライン再生での確認であり、plugin内で動かしていない。
- 可変buffer、mixed format、別process、sandbox、file-backed、Windowsのmapping、30分以上の連続、動的PDC、再生中のplugin sleepは未実施である。
- 各操作の時刻は秒単位の目安であり、結果はCSVの値だけから導いた。seekの移動先は位置表示の入力で生じた値であり、狙った小節ではない。

### 6.7 証跡

| 物 | 場所とSHA-256 |
| --- | --- |
| AU試験A PRE / POST | `~/KirinValidation/TransportProbe/pre-8ed4f855328843ffad93c217b0d5cf39-1806.csv` `81c0315f89bb007c6f4b684f3217cca65f40744e8c960dcb4fc266d95b632628` / `post-490087e1a5ec416e8bdd51c21cda83ff-1834.csv` `24c7476b2ce339fa4ad99cec681cefa1d3d0dd550099acf76229ae8213cf3db2` |
| AU試験B PRE / POST | `pre-a9020011831c4420a42c40cfa77c97cb-1830.csv` `903ce64c7fd4bb24bf213df71d07d534c0ae048749d3942430c105369f822700` / `post-286f0a8282f84407ab64148a750364a3-1818.csv` `92b3a274467bd185ce977adaaa4e2da5f8c040efd3e27f3f07259f6ff0dc2839` |
| VST3試験B PRE / POST | `pre-733f5aa34d6e48519a28a15be91a5a1c-1844.csv` `8f74cbc052e1117e8df157e249955d1b7573bd83b5822acd27a1b59c8ac9f179` / `post-3fd41b5f76154bcd855bb57fc2b3ece2-1831.csv` `7fa0250fe124720d313916ef17ca5f7da3cd0e3635c76701cc9def7db3f31c28` |
| VST3試験A（Bを含む通しの書出し）PRE / POST | `pre-733f5aa34d6e48519a28a15be91a5a1c-3170.csv` `1706c28b761a1078a3357b9f98f92b3c9442e44681263aff8fc6b63f9b38a3f4`（1844行目から） / `post-3fd41b5f76154bcd855bb57fc2b3ece2-15949.csv` `434faa4733ffbbe59feb4dc0bf9268b1b3502de1fefb85f82c4571640a9e55f9`（1831行目から） |
| source、解析script | `~/KirinValidation/TransportProbe/tools/`（`SHA256SUMS.txt`）。`Probe.cpp` `b95e5bdc8902eb9971cc1bd5e120bc3799424196606ad85c6533f7b75d7c5b4f`、`ProbeShared.h` `0cfdaa171a9e6c3a54adb2ea80612de4c8a21f3e9d357ae2e3f9ecc2d1926a03`、`probe-analyze.mjs` `90f2d997d651b6e79a9ce4596dd2363d53c11ab3690c98a12b2b104a10ea85c6`、`epoch-replay.mjs` `fcc7d67423507ad11c8a89770483785426ac19fcab2b5ce25d434b68caccfb0e`、`slice-analyze.mjs` `2fdb24a1e0318014af54150e417e120ac9fb515be6c07b7a44a087fc36730d0c` |
| probeのbinary | VST3 PRE `d84406fa3acf0345f8b0226fd9264961deebf537c33cf53caad53357a3dbe302`、VST3 POST `013de70ecbedb9c0dbd37637b738535620b3c27fe40436117414b6c123835ce9`、AU PRE `68101650026971ef7b77385335e7603d9ba25772ea593b9fd663b04a08387fb2`、AU POST `a354f96a06f4618a43b5736b19b07fa323b4d715a383f612e4f8a400ec047613`。AU 2件はauvalをpass。試験後にuser領域から削除し、共有memoryをunlinkした |

解析は`node probe-analyze.mjs pre.csv post.csv`、`node epoch-replay.mjs pre.csv post.csv`、VST3試験Aは`node slice-analyze.mjs pre.csv post.csv 1844 1831`で行った。

## 7. プローブv2: 周回の規則のplugin内実装とplugin sleep

### 7.1 事前に決めた条件

第6節の誤受入れへの対策（PREの周回）を、plugin内で動かして確かめた。測る前に次の条件を決めた。

| ID | 条件 |
| --- | --- |
| C1 誤受入れ0 | 新しい規則で受け入れたblockが、すべてPREとbit一致する |
| C2 棄却の説明 | 棄却したblockを、すべて「PREの今の周回より前」「未着」「上書き済み」「読取り中の書換え」のいずれかに分類できる。件数は、遅延4096のVST3で再配置1回につき2 block、AUと隣接構成で0 |
| C3 対照 | 同じ記録で、旧規則（書込み末尾だけ）の誤受入れが再現する。再現しなければ判定不能 |
| C4 一致 | plugin内の判定が、PREの行からの再計算と全blockで一致する |
| 記録のみ | 所要時間、PREの公開からPOSTの読取りまでの時間、再生中のplugin sleep |

### 7.2 条件

| 項目 | 値 |
| --- | --- |
| host、rate、block | 第1節と同じ（Studio Pro 8.1.2、Intel Mac、48 kHz、callback 2048 frames）。新規の使い捨てSongで、保存せずに閉じた |
| 構成 | 同じ識別PCMを置いた2 track。track 1はVST3のPRE → 遅延 → POST、track 2はAUのPRE → 遅延 → POST。遅延は既存のPDC Validation Delay 4096（VST3）。試験Bの後、停止中に両trackの遅延を外して試験Aを続けた |
| 計測器 | Transport Probe v2（0.0.2、非出荷、製品と別ID）。formatごとに別のPOSIX共有メモリを使い、全instanceを一括で開始・書出しした |
| 規則 | PREは、連続時計が直前のblockと連続しないたびに周回を改め、周回番号、周回の先頭、書込み末尾をheaderのseqlockの下で公開する。POSTは、必要範囲がPREの今の周回で書かれた範囲に入り、ring容量内で、読取り中に書換えがない場合だけ受け入れる。旧規則の判定も同時に記録した |
| 事前確認 | Studio Pro相当のhost模型（DAWなし）でpluginと同じ判定コードを動かし、期待をすべて満たした。pluginval（strictness 5）とauvalにpass |
| 操作 | 各試験で、8.125秒loopを約20秒再生、再生中のseek、停止と再生、loopを切って0から再生し同じ周回の中で後方seek、前方seekで音の終端（24秒）の先の無音まで約15秒 |

### 7.3 結果

| 試験 | 評価block | 新規則の受入れ（うち誤り） | 棄却（理由） | 旧規則の誤受入れ | 再計算との一致 | 判定 |
| --- | --- | --- | --- | --- | --- | --- |
| VST3 B（遅延4096） | 1494 | 1484（0） | 10（今の周回より前） | 10 | 1494/1494 | C1〜C4合格 |
| VST3 A（隣接） | 1404 | 1404（0） | 0 | 0 | 1404/1404 | C1、C2、C4合格。C3は対象外 |
| AU B（遅延4096） | 1749 | 1484（0） | 265（未着） | 0 | 1749/1749 | C1、C4合格。C2は分類が合格、件数が0にならない |
| AU A（隣接） | 1660 | 1388（0） | 272（未着） | 0 | 1660/1660 | 同上 |

- VST3の棄却は、seek、停止後の再生、0からの再生、後方seek、前方seekの各直後の2 block（chain遅延）だけだった。すべてPREの今の周回より前を指し、中身は全sampleで不一致だった。
- 後方seekは、loopを切った最初の周回の中で行った。VST3連続時刻は約4秒前に書いた同じ値へ戻り、旧規則はそれも受け入れた。第6.3節の「同じ格納位置の別の時計値」とは別の、同じ時計値の再利用である。
- AUの棄却はすべて未着で、原因はplugin sleepだった（7.4節）。
- 所要時間（callbackあたり）: p99.9は各試験で26.4 µs以下。最大はVST3のPOSTの64.7 µs（POST sleepの試行中の1回、周期の約0.15%）で、それ以外は35.5 µs以下だった。
- PREの公開からPOSTの読取りまで（壁時計、同じcycle）: 中央値は隣接構成で18〜20 µs、遅延ありで83〜100 µs。

### 7.4 plugin sleepとAUのK

- Studio Proは、入力が無音のまま約4秒たったpluginの呼出しを止めた。停止中だけでなく、再生中の音の終端の後でも起きた。editorを表示していたinstanceは呼ばれ続けた。insertのメニューでは「プラグインのスリープを有効化」が有効だった。
- AUのrender時刻は、そのinstanceが呼ばれた分だけ進む。呼ばれない間は止まり、再開時も跳ばない（周回は改まらない）。
- そのためAUのKは、「PREが呼ばれなかったframe数 − POSTが呼ばれなかったframe数」だけ変わった。7回の変化のうち6回はこの差と完全に一致し、1回は2 block（壁時計からの推定の丸め）の差だった。
- PREの方が長く眠った向き（Kが増える4回）では、古いKの範囲は未着となり、再較正（同じ候補8回）まで棄却された。
- POSTだけが眠った向き（Kが減る3回）では、古いKの範囲はPREの過去の音を指した。ずれがring容量（約10.9秒）を超えた2回（55.6秒、47.2秒）は上書き済みとして棄却された。容量内の1回（5.2秒）では、新旧どちらの規則も8 blockを受け入れ、中身は全sampleで不一致だった。これは事前に予測し、その条件を作って再現した。
- VST3連続時刻はproject位置に基づくので、sleepでKは変わらなかった（0のまま）。PREが眠っている間のPOSTの範囲は未着として棄却され、PREの再開時には周回が改まった。

### 7.5 呼出しの空白を加えた規則（記録からの再計算）

「PREは呼出しの空白（壁時計で約107 ms超）でも周回を改め、POSTは自分の呼出しの空白でKを無効にし、同じ候補8回で較正し直す」という規則を、全記録に当て直した。

- 通常の呼出し間隔は42.3〜43.0 ms、空白はすべて2秒以上だった。閾値107 ms、300 ms、1秒で結果は同じだった。
- AU 4035 block: 誤受入れは8から0になった。代わりに再較正中の棄却が40 block増えた。
- VST3 3524 block: 誤受入れは0のまま。再較正中の棄却が58 block増えた。
- この規則はplugin内では動かしていない。

### 7.6 計画への意味

1. **VST3（このhost）**: 周回の規則はplugin内で機能した。seekと再生開始の直後、chain遅延の長さだけPOSTへ倒れる。
2. **AU（このhost）**: 周回の規則だけでは足りない。呼出しの空白を時計の不連続として扱う規則（PREは周回を改め、POSTはKを無効にする）が要る。
3. **host認定**: 周回の規則は、seek直後の古い音にhostが新しい周回の外の番号を付けることを前提にする。Studio Proはこれを満たした。新しい周回の中の番号を付けるhostでは検出できないことを、DAWなしのhost模型で確かめた。認定では中身の照合で確かめる。
4. **静かな区間**: 入力が約4秒無音になるとPREが止まり、その間はPREの比較音がない。音が戻った後の比較をA′で解除するか、自動で戻すかは、B（自動再開）の要否とあわせて判断が要る。
5. **Kの照合**: 再配置の直後には、古いPREの記録と結び付いた単発の食い違う候補が出た（VST3で2回、AUで数回）。Kの照合は定常区間の連続一致で行い、単発の食い違いで対応を切らない。

### 7.7 範囲と未確認

- 1 host、Intel Mac、48 kHz、2048 frames、同じprocessでの結果である。editorの表示状態がsleepに影響した。
- 呼出しの空白の規則はplugin内で動かしていない。
- tail長の報告などでsleepを避けられるかは確かめていない。
- Windows、他のbuffer設定、動的PDC、別process、30分以上の連続は未実施である。
- seekの移動先は位置表示の入力で生じた値であり、狙った位置ではない。

### 7.8 証跡

| 物 | 場所とSHA-256 |
| --- | --- |
| 最終の書出し（各instanceの全行） | `~/KirinValidation/TransportProbe2/`。VST3 PRE `vst3-pre-19879ccf0eba4b039f15d966e92cf259-4165.csv` `06de90be7161f20045de353839afafb6e97abcc84a6eb875677f2d81bc404513`、VST3 POST `vst3-post-1eb575bcc5eb4f1b8094ba9d2c41ff72-4683.csv` `a4694a52c2a8c88593ce65244133016cb84ac8e54c72bc3fb847f865b3e8195e`、AU PRE `au-pre-7c4dc657aef8428b8bc7d5737c76e390-7351.csv` `5380c52dd370b5ea023447ded873147d75ca3cf3aae3242717456d66e8ce687d`、AU POST `au-post-f9867606ff2845e386ab9920503ad51e-12480.csv` `2b41a8675cdcc42be56743f77450a82fcdd18654933f2b56f50d4f562062f58e` |
| 途中の書出し | 同じ場所。試験Bの終わり（VST3 1652/1656行、AU 1652/2422行）、試験Aの終わり（VST3 3206/3722行、AU 3206/7627行）、POST sleepの各試行の終わり。全件のhashは`CSV-SHA256.txt` |
| source、模型試験、解析 | `tools/`（`SHA256SUMS.txt`）。`ProbeShared.h` `99272c1c686f65dbf1bbc0bd1ac288ce524edfa1327567d95e4c2d857f721ea1`、`Probe.cpp` `edc46bdae97d32b20da89c1b89a3953bbced03eb596f41c2bce6b371b988b717`、`probe-logic-test.cpp` `5d011d24c3b4fd48f6abf65175e518ee729161c897a3fde097f190dd3195ad94`、`analyze2.mjs` `87378ec964ff0fc342af974e637ab3e736b6ee9f343132e844ee53179c7c7188`、`gap-replay.mjs` `2462f71e63432b1ae4736766bc75fd668ce6ea4c8099a521b45ed4c3aab7168d` |
| binary | VST3 PRE `993eb741dc15048d0536087567597e498a73f1c1e302c473f8b9379d84159ff4`、VST3 POST `c9a2e60b5e68122e12442f426c8bc1088f15b1c0521ba265b1932925467cff67`、AU PRE `316beb3de9843e563425ce5ee1bac1c0669503f5767f5732447b2e9bbd47047a`、AU POST `31489f05a455f834a02dbc6ccdf5f807f6f751f3cec2efdf7b4c4ff7f5ae2ba1`。pluginval（strictness 5）とauvalにpass。試験後にuser領域から削除し、共有memoryをunlinkした |

解析は`node analyze2.mjs pre.csv post.csv [preFrom postFrom]`、空白の規則は`node gap-replay.mjs pre.csv post.csv [gapMs]`で行った。

## 8. プローブv3: 呼出しの空白の規則、動的PDC、infinite tail

### 8.1 事前に決めた条件と構成

第7.5節の規則（PREは呼出しの空白でも周回を改め、POSTは自分の空白でKを無効にして較正し直す）をplugin内で動かした。
判定の条件はC1〜C4（第7.1節）と同じで、v3の規則に対して判定する。
同じ記録にv2の規則（空白を扱わない）とv1の規則（書込み末尾だけ）の判定も残し、対照にした。
G1-04（再生中の遅延変更）とsleepを避ける手段は探索として測り、合否に使う条件は置かなかった。

| 項目 | 値 |
| --- | --- |
| host等 | 第1節と同じ（Studio Pro 8.1.2、Intel Mac、48 kHz、callback 2048 frames）。新規の使い捨てSongで、保存せずに閉じた |
| 構成 | track 1はVST3のPRE → 遅延切替 → POST、track 3はAUのPRE → 遅延切替 → POST、track 2は識別PCMとTail Monitorだけ |
| 計測器 | Transport Probe v3（0.0.3）、Latency Switch（VST3、4096と0を切り替える純粋な遅延。音の遅延と報告latencyを同じ時点で変える）、Tail Monitor（VST3、infinite tailを報告する素通し）。いずれも非出荷で製品と別ID |
| 空白の判定 | 直前の呼出しからの壁時計の間隔が、直前のblock長の2.5倍（20 ms以上）を超えたとき |
| 事前確認 | host模型（DAWなし）でpluginと同じ判定コードを動かし、期待をすべて満たした（v2の規則はAUのPOSTだけのsleepで誤受入れし、v3は0）。pluginval（strictness 5、VST3 4本）とauval（AU 2本）にpass |
| 区間 | 1: 遅延4096で第7.2節と同じ操作（loop、seek、停止と再生、同じ周回の中の後方seek、前方seekで無音へ）。2: AUのPREのeditorを表示したまま、8秒再生、9秒停止、6秒再生（POSTだけのsleep）。3: 8.125秒loopを再生中に遅延を4096 → 0 → 4096と切り替え（G1-04）。4: 停止中に遅延を0へ切り替え、loop、seek、停止と再生（隣接） |

### 8.2 結果（v3の規則）

| 区間 | format | 評価block | v3の受入れ（うち誤り） | v3の棄却（理由） | 対照の誤受入れ（v2 / v1） | 判定 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 遅延4096 | VST3 | 1407 | 1389（0） | 18（K無効10、周回より前8） | 0 / 10 | C1、C4合格。C2は分類が合格、件数が不合格 |
| 1 遅延4096 | AU | 1674 | 1397（0） | 277（未着） | 0 / 0 | C1、C4合格。C2は分類が合格、件数が不合格 |
| 2 POSTだけのsleep | VST3 | 337 | 317（0） | 20（K無効） | 0 / 4 | C1、C4合格。C2は分類が合格、件数が不合格 |
| 2 POSTだけのsleep | AU | 337 | 317（0） | 20（K無効） | 8 / 8 | C1、C4合格。C2は分類が合格、件数が不合格。C3（対照の再現）あり |
| 3 G1-04 | VST3 | 585 | 575（5） | 10（K無効） | 5 / 7 | C1不合格（8.3節）。C2は分類が合格、件数が不合格 |
| 3 G1-04 | AU | 585 | 575（21） | 10（K無効） | 21 / 21 | C1不合格（8.3節）。C2は分類が合格、件数が不合格 |
| 4 隣接 | VST3 | 890 | 882（0） | 8（K無効） | 0 / 0 | C1、C4合格。C2は分類が合格、件数が不合格 |
| 4 隣接 | AU | 890 | 882（0） | 8（K無効） | 0 / 0 | C1、C4合格。C2は分類が合格、件数が不合格 |

- C2は、分類では全区間で合格し、件数ではどの区間も不合格だった。
  - 分類: 棄却はすべて「K無効」「周回より前」「未着」のいずれかで、「上書き済み」と「読取り中の書換え」は0だった。「K無効」はv3の規則が加えた理由で、第7.1節の分類にはない。
  - 件数: 条件（遅延4096のVST3で再配置1回につき2 block、AUと隣接構成で0）は、第7節の周回の規則だけを前提に決めたもので、v3の規則を加えたときに改めなかった。超えた原因は、呼出しの空白の後の再較正（同じ候補8回）と、区間1のAUのPREのsleep（未着277 block）である。どちらも規則の設計どおりの棄却だが、事前の条件に照らせば不合格である。
  - 訂正: 本節の判定欄は当初、件数を満たさない区間も「C2合格」と記していた（2026-09-28に訂正）。
- C1の「誤受入れ0」は、識別PCMで見分けられる誤りに限る。周回単位の取り違えと、遅延の報告の誤りによるずれは、この試験では検出できない（8.6節）。
- C4は全区間で合格した。空白の判定、Kの較正状態、判定結果のいずれも、記録からの再計算と一致した。
- 区間2のAUでは、POSTだけが5.1秒眠った直後の8 blockを、v2とv1の規則が受け入れ、中身は全sampleで不一致だった。v3はPOSTの空白でKを無効にし、その8 blockを棄却した。第7.4節の誤受入れを、plugin内の規則で防げた。
- 区間1のAUの未着277 blockは、無音区間でPREが眠り、画面を表示していたPOSTが呼ばれ続けた間の棄却である（安全側）。
- 所要時間（callbackあたり）: p99.9は25.9 µs以下、最大は30.5 µsだった。

### 8.3 G1-04: 再生中の遅延変更

- hostは切替のたびに報告latencyの変更を受け付けた（画面の遅延表示が85.3 ms → 0.0 ms → 85.3 msと変わった）。切替の前後で呼出しの空白は出なかった。
- 遅延切替は、ある block から音の遅延を変えた。POSTの中身はそのblockから変わったが、時計の側の変化は遅れて現れた。

| format | 切替 | POSTの時計の変化 | 誤受入れ（v3） | 候補が食い違ったら直ちにKを無効にする規則（M1、記録からの再計算） |
| --- | --- | --- | --- | --- |
| VST3 | 4096 → 0 | 2 block後（85 ms）にproject時刻と連続時刻が進み、以後は正しく対応 | 2 block | 2 block（Kが変わらないので効かない） |
| VST3 | 0 → 4096 | 3 block後（128 ms）に戻り、以後は正しく対応 | 3 block | 3 block |
| AU | 4096 → 0 | render時刻は跳ばず、Kが4096減った。新しいKの候補は128 ms後、較正は427 ms後 | 10 block | 3 block |
| AU | 0 → 4096 | Kが4096増えた。新しい候補は171 ms後、較正は469 ms後 | 11 block | 4 block |

- M1の代償は、全記録でAUが30 block、VST3が7 blockの追加の棄却だった（再配置の直後の単発の食い違いでもKを無効にするため）。
- どの規則でも、切替の直後の2〜4 block（85〜171 ms）は時計から検出できなかった。このhostでは、遅延の変更を音より先に観測できない。

### 8.4 infinite tailとplugin sleep

- Tail Monitor（VST3、infinite tailを報告）は、全体で8456回呼ばれ、一度も空白がなかった。停止中、無音区間、区間の間の長い停止を含む。同じ間に、tail 0を報告するPREとPOSTは何度も眠った。
- このhostでは、infinite tailの報告でplugin sleepを避けられる可能性がある。AU、他のhost、offline renderでのtailの扱い、停止中のCPU負荷は確かめていない。
- 外部資料: Studio OneのPlug-in Napは5.4で加わった機能で、何もしていないpluginを止めてCPUを空ける。instrumentと、Main出力のinsertには働かない（[Sound On Soundの解説](https://www.soundonsound.com/techniques/studio-one-54-plug-nap-real-time-chord-detection)）。今回のStudio Proでは、insertのメニューにplugin単位の「プラグインのスリープを有効化」があった（第7.4節）。Main出力に置いた場合の挙動は試していない。

### 8.5 計画への意味

1. **呼出しの空白の規則**: plugin内で機能した。再配置、PREだけのsleep、POSTだけのsleepで、両formatとも誤受入れ0だった（識別PCMで見分けられる範囲。8.6節）。対応の鍵の規則として採る候補にする。
2. **動的PDC**: 時計だけでは、遅延の変更直後の2〜4 block（このhostで最大171 ms）の誤対応を防げない。短い誤対応を許すか、中身による検出（G1-05）を加えるか、比較中の遅延変更を対象外として扱うかは、G1Rでの判断が要る。AUではM1で誤対応の区間を10〜11 blockから3〜4 blockに縮められる。
3. **静かな区間**: infinite tailの報告でPREとPOSTの眠りを避けられるなら、第7.6節の「静かな区間の後の比較」の問題を小さくできる。副作用を確かめたうえで判断する。

G1Rの決定と、その後の見直し（遅延の報告の誤りへの警告など）は、[実装計画](hypha_live_chain_compare_implementation_plan_20260927.md)の第0節と第5.2節に置く。

### 8.6 範囲と未確認

- 1 host、Intel Mac、48 kHz、2048 frames、同じprocessでの1回の試験である。
- 遅延切替は、音と報告を同時に変える1つの型である。報告が先に来る型、報告が来ない型、遅延量が異なる場合は測っていない。8.3節の2〜4 block（85〜171 ms）は、このhost、2048 frames、この型での値であり、他の条件へ一般化しない。
- M1はplugin内で動かしていない。
- Tail MonitorはVST3だけで、tail以外の条件（editorの表示など）との切り分けは行っていない。
- Windows、他のbuffer設定、別process、30分以上の連続は未実施である。
- AAX（Pro Tools）は試していない。AAXでは、POSTに遅延補償済みの位置が渡るか、どの時計が連続するかが未確認であり、本書の規則がそのまま成り立つとは限らない（実装計画第5.4節）。
- **遅延の報告への依存**: 対応の正しさは、PREとPOSTの間のpluginが遅延を正しく報告し、hostがそれで遅延を補償することを前提にする。今回のchainは、報告が正しい遅延（PDC Validation Delay、Latency Switch）だけだった。遅延を報告しない、または誤って報告するpluginが間にあると、時計の対応は正しく見えたまま、PREが誤差の分ずれ続ける。どの時計の規則でも検出できず、8.3節の区間と違って自然には終わらない。
- **周回単位の取り違え**: 識別PCMは位置ごとに一意だが、loopのどの周回でも同じ音である。対応がloop長の整数倍ずれても中身はbit一致し、照合は正しいと数える。本書の「誤受入れ0」は、この種類の誤りには及ばない。
  - chainの遅延がloop長以上だと、Kの較正はproject時刻の一致を、POSTが聞いている周回ではなく新しい周回のPREの記録と結び付け、周回単位でずれたKを採り得る（判定コードの作りからの推論。未試験）。
  - 今回のloop（4秒と8.125秒）は、遅延4096（約85 ms）より十分長かった。
  - 周回ごとに印が変わるfixtureでの試験と、遅延がloop長以上の条件の試験が要る（実装計画第12.1節のG1-03）。
- **プローブの共有memory**: sample本体は非atomicに読み書きし、読取り中の書換えはseqlockで検出して棄却した。C++のmemory modelではdata raceを含む書き方であり、検証用の道具としてだけ使う。製品には流用せず、規格上の競合がない方式を設計する（実装計画LC-05）。

### 8.7 証跡

証跡の実体（CSV、source、binary）は公開repositoryの外（作業者の手元）にあり、本書にはhashだけを載せる。第三者は本書だけでは再現も検証もできない。repositoryへ入れるかは、実装の承認の前に決める（実装計画第16節）。

| 物 | 場所とSHA-256 |
| --- | --- |
| 最終の書出し | `~/KirinValidation/TransportProbe3/`。VST3 PRE `vst3-pre-d887f856510441e5be3d860b4f474f82-3766.csv` `e98ae9d2e5f08302e39a952f67e48143bd6db6536e1abfdf51db6a3c76c597e3`、VST3 POST `vst3-post-c6e46e7f65d94dd1b3e040372bff177f-3778.csv` `532250c42e2f6ce72b5ddbb728b4d4f6d3b0109f79ed450df87fc47036ec9e9f`、AU PRE `au-pre-c95150aedc224eabbe32bd4a22f983f1-6759.csv` `169605191413d9797e0739f5fc860a574852cf8ef2b9153e482fb0ea0ff0a301`、AU POST `au-post-c567c643aeb1425dafeec45f6a163786-5441.csv` `89e6cbccd8ba4d0134938ae8ae9f99016d592e8a8a8bc65b6d62f309f183d80b`、Tail Monitor `vst3-monitor-deebfd58f70746c8bb6c50943e27a61a-8456.csv` `4e233fe740bc9fdea4bfa48d98d4ed7dc8d104b198e23b99fde26240dfbd7634`、Latency Switch `vst3-switch-4e77744e7da54444b5183f271abafbc4-3778.csv` `73fd3a9dd0597b200852d631296fb35065a39489359f41fb90a3e8115c97ddad` と `vst3-switch-186a542b6c46405ca1422be2025c5472-3992.csv` `5d455c460a0ea402058352b88c2ffdb7af67fdb462c97d330d8ec51f0b866050` |
| 途中の書出しと区間の境界 | 同じ場所（全28件のhashは`CSV-SHA256.txt`）。各区間の境界は、区間の終わりに一括で書き出した行数（VST3 1565/1569、2053/2059、2769/2779、AU 1565/2032、2450/3723、3809/4443） |
| source、模型試験、解析 | `tools/`（`SHA256SUMS.txt`）。`ProbeShared.h` `a2cf54c5c55e175285182456f71d7f119cd1007f574ab2e27393c056ffd1fdf1`、`Probe.cpp` `8e723bf590aee14c22f2a7d2a234ae7e6ab9f87db43c047b3dc351dc05771d5b`、`Aux.cpp` `ec020a14176e420fc3660f724672062c1b6955d3d0b1b2e79a6de4dc464b0ef9`、`probe-logic-test.cpp` `7ab023a4d0d348953651691248fd65a9c04f1a84dc106410721ffb999f68a2fb`、`analyze3.mjs` `e59fb5b504573f62d367212fbe232e94d33b1fc0e8f7b1bf68dd1f9fe29cac02`、`phases3.mjs` `1fe4fd8c50416d7fe583ca01605cdecdd283a17c66618873ea3cd0300b65268e`、`m1-replay.mjs` `ef63f0ade11fa82c59d35ea880450458a355a33ae267f66bf70c9d7fa3fd1c6b` |
| binary | `tools/BINARIES-SHA256.txt`。VST3 PRE `6329f124f245635eaf4a3bae39cf1aab2b45f4752f31f207048228b55c0a0c8d`、VST3 POST `d3ea4ecdc2ecdc545eb4bd583a3482cf27f9b232700c79a97665d5ed72e3797a`、AU PRE `21a51e0552d744937394e18fe35ac96d57da70608da477612104b2e0ab0ba12c`、AU POST `f7150af4eb23f17fc8e828cf41eb73e4365c5ad5cfebfce54202190f631d472d`、Latency Switch `bfe05cab1ad382a2d83ad3ad181007ef6ccbfbb2a85640c7e8f2febd380bc2b5`、Tail Monitor `1e37e8aff7bec904c8a6f03300c759b390d4786e11f9ace360d841b3aba29c4e`。試験後にuser領域から削除し、共有memoryをunlinkした |

区間ごとの判定は`node phases3.mjs`（結果は`tools/phases3-result.json`）、M1は`node m1-replay.mjs pre.csv post.csv`で求めた。
