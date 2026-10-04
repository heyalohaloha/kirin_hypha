# H15: Kirin OS の「Hypha ではこう見える」の描画の道具を、ABCV の 4 つの要求の例で動かす。
#   reference_preview_request.v1.json        C の画面（元の音量の Check、選択肢が 1 つずつ）
#   reference_preview_request_match.v1.json  C の画面（MATCH のある Check、CHECK SET・Check・曲・Cue が 2 つずつ）
#   reference_preview_request_blind.v1.json  VERSION BLIND の表示の例（始めも記録もしない）
#   reference_preview_request_ja.v1.json     日本語の画面（要求の locale で描く）
#   reference_preview_request_dynamics.v1.json  C の画面の Dynamics の Check（範囲の帯の形。値は「—」）
# どれも renderer_version を返し、900×600 の PNG（中身のある大きさ）を書くこと。Blind の例は、VERSION BLIND の
# 画面（1・2・開示・終了）が出なければ道具そのものが失敗する（空の絵を返さない）。
foreach (example IN ITEMS "" "_match" "_blind" "_ja" "_dynamics")
    set (input "${FIXTURES}/reference_preview_request${example}.v1.json")
    set (output "${OUTPUT_DIR}/reference_preview_contract${example}.png")
    file (REMOVE "${output}")
    execute_process (COMMAND "${RENDERER}" "${input}" "${output}"
                     RESULT_VARIABLE result OUTPUT_VARIABLE printed ERROR_VARIABLE failed)
    if (NOT result EQUAL 0 OR NOT printed MATCHES "renderer_version" OR NOT EXISTS "${output}")
        message (FATAL_ERROR "reference preview${example} failed (${result}): ${failed}")
    endif ()
    file (SIZE "${output}" bytes)
    # Blind の例は PRE/POST Blind と同じ平らな画面で小さく縮む（Windows の文字の描き方では 2 万バイトを切る）。中身は
    # 道具が自分で確かめる（1・2・終了が出なければ道具が失敗する）。ほかは REF の画面。
    set (minimum 50000)
    if (example STREQUAL "_blind")
        set (minimum 8000)
    endif ()
    if (bytes LESS minimum)
        message (FATAL_ERROR "reference preview${example} wrote an empty-looking image (${bytes} bytes)")
    endif ()
endforeach ()
message ("renderer_version: the five ABCV preview examples rendered")
