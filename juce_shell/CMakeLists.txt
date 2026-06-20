cmake_minimum_required(VERSION 3.22)

# B-092: minimum macOS deployment target for the shipped JUCE bundles (judgment A-1: next
# Release = JUCE universal AU+VST3). Set BEFORE project() so compiler/SDK detection uses it.
# Single source of truth: scripts/build_juce_universal.sh and .github/workflows/ci.yml inherit
# this via a fresh configure (no per-script -D). The Rust staticlib's own per-arch defaults
# (x86_64 10.12 / aarch64 11.0) are below 12.0, so the linked binary's LC_BUILD_VERSION minos
# is governed by this value.
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS deployment target")

# B-114: version は Cargo.toml を単一ソースとする（CMake へのハードコード廃止）。
# crates/hypha_pre/Cargo.toml の [package] version (3 部 semver) を configure 時に抽出し、
# JUCE バンドルの PROJECT_VERSION（= CFBundleShortVersionString 由来）に流す。egui VST3 も同 Cargo
# version（B-109 stamp）なので、出荷4物の版は Cargo.toml 1 箇所で決まる。
file(READ "${CMAKE_CURRENT_LIST_DIR}/../crates/hypha_pre/Cargo.toml" KIRIN_CARGO_TOML)
string(REGEX MATCH "version[ \t]*=[ \t]*\"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _KIRIN_VER_MATCH "${KIRIN_CARGO_TOML}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "B-114: failed to parse [package] version from crates/hypha_pre/Cargo.toml")
endif()
project(KirinHypha VERSION ${CMAKE_MATCH_1} LANGUAGES C CXX)

# B-082: arch は cache 変数化（FORCE 撤去）。dev は x86_64 単独（既存 build dir / 高速）、
# 出荷は -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" で universal（scripts/build_juce_universal.sh）。
set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Build architectures (set 'x86_64;arm64' for universal ship build)")

# B-082: リンクする kirin_hypha_ffi staticlib のパス。dev は host single-arch
# (target/release/...a)、universal 出荷時は lipo 済 universal .a を -DKIRIN_FFI_LIB で渡す。
set(KIRIN_FFI_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../target/release/libkirin_hypha_ffi.a"
    CACHE FILEPATH "Path to the kirin_hypha_ffi staticlib (universal lipo for ship)")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 7.0.12 (GPLv3 option), pinned as submodule. Does not touch crates/ or xtask.
add_subdirectory(JUCE)

# B-054: embed the mycelium background PNG (the same GPLv3 asset egui uses:
# crates/hypha_gui/assets/bg_mycelium.png, 300×200, brightness ~15%) as BinaryData so the
# editor can draw it without a runtime file dependency. Shared by both plugin targets.
juce_add_binary_data(KirinHyphaData
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../crates/hypha_gui/assets/bg_mycelium.png)

# B-070: PRE and POST are two plugin targets sharing one role-parameterized base
# (KirinHyphaProcessorBase). They link the same kirin_hypha_ffi staticlib and compile the
# same PluginProcessor.cpp / PluginEditor.cpp; only the per-target PluginMain{PRE,POST}.cpp
# (the createPluginFilter Role) and the AU/VST3 identifiers differ.
# B-094: BUNDLE_ID is the canonical CFBundleIdentifier (com.kirinmastering.hypha.{pre,post})
# matching the published v1.0.0 — set explicitly so it is not the JUCE default com.<company>.<target>.
# AU identity (PLUGIN_MANUFACTURER_CODE Kirn / PLUGIN_CODE Khpr|Khpo / AU_MAIN_TYPE aufx) is unchanged.
function(add_kirin_plugin TARGET CODE PRODUCT MAIN_SRC BUNDLE_ID)
    juce_add_plugin(${TARGET}
        PLUGIN_MANUFACTURER_CODE Kirn          # newly assigned (>=1 uppercase)
        PLUGIN_CODE ${CODE}                    # Khpr (PRE) / Khpo (POST); exactly 1 uppercase
        FORMATS AU VST3
        PRODUCT_NAME ${PRODUCT}
        COMPANY_NAME "Kirin"
        BUNDLE_ID ${BUNDLE_ID}                 # B-094: canonical com.kirinmastering.hypha.{pre,post}
        AU_MAIN_TYPE kAudioUnitType_Effect
        VST3_CATEGORIES Fx Analyzer
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        # B-130 (G-115-373 / §7): JUCE 既定の AU resourceUsage（network.client +
        # temporary-exception.files.all.read-write）を抑止し、下の POST_BUILD で自前最小
        # （files.all のみ・network.client なし）を宣言する。Hypha は通信せず plugin_data 書込のみ。
        SUPPRESS_AU_PLIST_RESOURCE_USAGE TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(${TARGET} PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/HyphaWidgets.cpp     # B-054: LED / mycelium bg / metric cells / editable name
        src/PostControls.cpp     # B-054: POST Keep/Stop/Note/Sense button row
        ${MAIN_SRC})

    # kirin_hypha_ffi: C ABI header + Rust staticlib (B-052 boundary, unchanged).
    target_include_directories(${TARGET} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../crates/kirin_hypha_ffi/include)

    # B-054: link the embedded mycelium PNG (provides BinaryData.h to the editor).
    target_link_libraries(${TARGET} PRIVATE ${KIRIN_FFI_LIB} KirinHyphaData)

    target_compile_definitions(${TARGET} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)          # permitted: built under GPLv3

    target_compile_options(${TARGET} PUBLIC
        -include "${CMAKE_CURRENT_LIST_DIR}/src/KirinJucePluginConfig.h")

    # B-130 (G-115-373): juce_audio_utils（AudioThumbnail/CD/DeviceSelector 用・実使用 grep 0）を
    # plugin に必要な最小モジュールへ差し替え。これで juce_audio_utils 由来の DiscRecording.framework
    # link を落とす。AudioProcessor(Editor) は juce_audio_processors、widget は juce_gui_basics。
    # （注: WebKit.framework は juce_gui_extra ← juce_audio_processors の必須依存ゆえ config では落ちない。）
    target_link_libraries(${TARGET}
        PRIVATE
            juce::juce_audio_processors
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # B-130 (G-115-373 / §7): SUPPRESS で消えた AU resourceUsage に、自前最小（files.all のみ・
    # network.client なし）を AU bundle の Info.plist へ POST_BUILD で付与する。AU sandbox（GarageBand 等）
    # で plugin_data（bundle 外）書込に files exception が要るため files.all は保持。network.client は §7 で除去。
    add_custom_command(TARGET ${TARGET}_AU POST_BUILD
        COMMAND /bin/sh -c
            "/usr/libexec/PlistBuddy -c 'Delete :AudioComponents:0:resourceUsage' \"$1\" >/dev/null 2>&1 || true; /usr/libexec/PlistBuddy -c 'Add :AudioComponents:0:resourceUsage dict' -c 'Add :AudioComponents:0:resourceUsage:temporary-exception.files.all.read-write bool true' \"$1\""
            _
            "$<TARGET_FILE_DIR:${TARGET}_AU>/../Info.plist"
        VERBATIM
        COMMENT "B-130: AU resourceUsage = files.all only (no network.client)")
endfunction()

add_kirin_plugin(KirinHyphaPRE  Khpr "Kirin Hypha PRE"  src/PluginMainPRE.cpp  "com.kirinmastering.hypha.pre")
add_kirin_plugin(KirinHyphaPOST Khpo "Kirin Hypha POST" src/PluginMainPOST.cpp "com.kirinmastering.hypha.post")
