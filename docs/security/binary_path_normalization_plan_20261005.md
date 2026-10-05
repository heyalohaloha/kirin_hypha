# 配布バイナリのpath露出と将来build方針 — 2026-10-05

状態: H1。既存assetを保持し、将来producerの対策を提案する。今回build flags、製品source、署名、既存Releaseは変更していない。前段監査の再実施ではなく、保存済みv1.1.50配布物をsection単位で追加評価した。

## 実際に確認した露出

最新ReleaseのmacOS ZIP、macOS PKG、Windows installerの取得時checksumは照合済み。ZIPの42 entries、PKG展開payloadの83 entriesを読み取った。実行・installはしていない。

| ZIP内の実行file | SHA-256 | owner-home Cargo registry path occurrences |
|---|---|---|
| Kirin Hypha 1.1.50/VST3/PRE Kirin Hypha.vst3/Contents/MacOS/Kirin Hypha PRE | `3a3e0486e4eac67351056a7221274ade42cefa6d16e74808a83f5061eca93294` | x86_64 86 / arm64 78 |
| Kirin Hypha 1.1.50/VST3/POST Kirin Hypha.vst3/Contents/MacOS/Kirin Hypha POST | `87919c18e00c82baac64dd8040000483e88b976cf1a2ef01ebd625e20081464f` | x86_64 86 / arm64 78 |
| Kirin Hypha 1.1.50/Audio Unit/Kirin Hypha PRE.component/Contents/MacOS/Kirin Hypha PRE | `bbe4198ce4a956981a4587ab1e085b09eddc8db8e8f506f9582c3903df22c29c` | x86_64 86 / arm64 78 |
| Kirin Hypha 1.1.50/Audio Unit/Kirin Hypha POST.component/Contents/MacOS/Kirin Hypha POST | `8f86afb1249b15f5b38bf23aff95ad2dfb21e588dd1ac9e109754228488a2a0b` | x86_64 86 / arm64 78 |

4 filesそれぞれ164箇所、ZIP計656箇所。PKG内は同一hashの4実行fileで656箇所。ZIPとPKGの両containerを合計すると、重複を含め1,312箇所。Mach-O各sliceのload commandsとsection範囲を解析すると、全件が`__TEXT/__cstring`にあり、`__DWARF` sectionはない。debug symbolをstripするだけでは、この文字列領域を除去できない。Rust dependencyの`file!()`、panic等に使われるsource location由来である可能性が高いが、各call siteとの対応は未確認。

第三者はbuilderのlocal usernameとCargo registryのdependency source位置を知る。path自体はcredential、接続先、実行権限を与えず、既知の公開作者名との関連を強めるprivacy影響が中心。取得済みpayloadのscanでこれ以外のowner emailやcredentialは検出していないが、全過去binaryを保証する結果ではない。値は本書へ再掲しない。

Windows `.exe`は非圧縮領域のASCII/UTF-16 scanのみ確認済み。圧縮payload、PDB、Windows AAXおよび全過去配布物は未確認。macOS結果をWindowsの安全判定へ流用しない。

## 実在producerを通る対策案

| 段階 | 現行経路 | 将来の対策範囲 |
|---|---|---|
| macOS診断 | `scripts/build_hypha.mjs` → Apple 2 targetsのCargo FFI → lipo → CMake/JUCE | checkoutだけでなくCargo home/registry、generated source rootsをRust compile前に正規化 |
| macOS正式AU/VST3 | `scripts/release_hypha.mjs` → `scripts/ls_release/hypha_release_local.mjs` → `scripts/build_juce_universal.sh` | 両architectureのFFI `.a`からbundleまで同一policy |
| macOS正式AAX | 同release entry → `scripts/build_aax_universal.sh` → FFI/lipo/CMake → PACE/Apple署名 | 署名前に正規化。licensed SDK/toolを公開しない |
| CMake fallback | `juce_shell/CMakeLists.txt`のdefault FFI custom target → Cargo | 明示FFI overrideとfallbackの双方を対象 |
| Windows公開CI | `ci.yml` Cargo FFI → CMake/JUCE → unsigned installer | Rust依存、C++ macro/debug、linker PDB locatorを別々に検査 |
| Windows licensed AAX | `scripts/build_aax_windows.ps1`、private signing factory | 現行factory実機は未確認。公開producerのcommand planと私有側受入を照合してから導入 |

現行repoはこれらを横断するpath remappingを強制しない。環境から継承される外部flagsの有無は確認していない。文書からpathを消しただけでは将来binaryの再発を防がない。

Rustは`--remap-path-prefix FROM=TO`で、compilerが生成するmacro/panic/debug等のsource位置を中立rootへ置換する案とする。複数規則の順序、Windows separator、外部linkerの残存pathを実出力で確認する。これはbest effortである。[rustc公式説明](https://doc.rust-lang.org/rustc/remap-source-paths.html)。Cargoのflags優先順位はencoded env、通常env、target config、build config。既存flagsを失わず、spaceを含むprefixを扱い、explicit targetのhost build script/proc macroも別に検査する。[Cargo設定](https://doc.rust-lang.org/cargo/reference/config.html#buildrustflags)。

Clang C/C++は`-ffile-prefix-map`候補でmacro/debug/coverageのpathを正規化する。debugだけの`-fdebug-prefix-map`では`__FILE__`を対象にできない。[Clang公式reference](https://clang.llvm.org/docs/ClangCommandLineReference.html)。Windows MSVC C++の汎用source prefix mappingは今回未確認。`.NET`のPathMapをMSVCへ推測適用しない。中立build rootと`/PDBALTPATH:%_PDB%`は候補だが、後者はPE内のPDB locatorを変えるだけで、実PDBのsource path対策ではない。[MSVC公式reference](https://learn.microsoft.com/en-us/cpp/build/reference/pdbaltpath-use-alternate-pdb-path?view=msvc-170)。

## 最小fixtureで確認したこと

2026-10-05、隔離した一時directoryで、spaceを含む架空のsource prefixを使用し、Rust `file!()`とC++ `__FILE__`だけをcompile・実行した。

| compiler / command差分 | plain | remapped |
|---|---|---|
| rustc 1.94.1、`--remap-path-prefix <fixture-source>=/hypha-source` | 架空prefixがmacro出力とbinaryに残る | `/hypha-source/main.rs`になり、元prefixのbinary一致なし |
| Apple clang 17.0.0、`-ffile-prefix-map=<fixture-source>=/hypha-source` | 架空prefixがmacro出力とbinaryに残る | `/hypha-source/main.cpp`になり、元prefixのbinary一致なし |

4 compiles / 4 executions成功。fixtureはshipping source、Cargo dependency graph、Apple arm64、Windows、licensed AAX、PDB/OSO、crash symbolication、署名・公証を検証していない。実配布候補への適用完了とは扱わない。

## 将来実装の受入と副作用

1. 各producerの意味あるcommand-plan検証でcustom Cargo home、space、flags precedence、両Apple targets、generated rootsを扱う。FROMの実個人値はlogへ出さず、中立rootとpolicy IDのみをpublic provenanceへ記録する。
2. compiler policy/toolchainをcache・source provenanceへ結び、新しいunsigned候補を隔離buildする。現行source fingerprintはambient flagsを証明しない。`CARGO_TARGET_DIR`だけ変更すると既存lipoの期待pathがずれるため、producerのpath derivationも一貫させる。
3. Rust `.a`、各architectureのbundle、Windows圧縮payload/PDBをscanし、個人home/usernameが残らないことと必要なcrate/file/line情報を確認する。RT、bit identical、C ABI、format、host、package、signalの既存gateを維持する。
4. 中立pathはreproducibilityを改善し得るが、完全再現buildの証明ではない。debuggerのsource mapping、symbol server、panic報告とcrash診断は別に試験し、公開sourceへの対応を維持する。
5. binary bytesが変わるためhash・既存signature・notarization receiptを再利用しない。新候補のtrusted署名・公証・host/installer受入と3配布チャネルの正式gateを通す。既存signed binaryを直接編集しない。

推奨判断: **future-only fix**。既存v1.1.50 assetは今回保持し、H1の限定的privacy露出として記録する。credential rotationやGit history rewriteを必要とする根拠は未検出。重大PIIや実credentialの追加発見時は再分類して停止・報告する。将来producer実装と正式releaseは別の承認対象であり、今回のdoc候補へ実行code変更を混ぜない。
