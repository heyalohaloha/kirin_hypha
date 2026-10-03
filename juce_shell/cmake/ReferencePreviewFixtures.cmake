# H15: Kirin OS の「Hypha ではこう見える」の描画の道具を、ABCV の 4 つの要求の例で動かす。
#   reference_preview_request.v1.json        C の画面（元の音量の Check、選択肢が 1 つずつ）
#   reference_preview_request_match.v1.json  C の画面（MATCH のある Check、CHECK SET・Check・曲・Cue が 2 つずつ）
#   reference_preview_request_blind.v1.json  VERSION BLIND の表示の例（始めも記録もしない）
#   reference_preview_request_ja.v1.json     日本語の画面（要求の locale で描く）
# どれも renderer_version を返し、900×600 の PNG（中身のある大きさ）を書くこと。
foreach (example IN ITEMS "" "_match" "_blind" "_ja")
    set (input "${FIXTURES}/reference_preview_request${example}.v1.json")
    set (output "${OUTPUT_DIR}/reference_preview_contract${example}.png")
    file (REMOVE "${output}")
    execute_process (COMMAND "${RENDERER}" "${input}" "${output}"
                     RESULT_VARIABLE result OUTPUT_VARIABLE printed ERROR_VARIABLE failed)
    if (NOT result EQUAL 0 OR NOT printed MATCHES "renderer_version" OR NOT EXISTS "${output}")
        message (FATAL_ERROR "reference preview${example} failed (${result}): ${failed}")
    endif ()
    file (SIZE "${output}" bytes)
    if (bytes LESS 50000)
        message (FATAL_ERROR "reference preview${example} wrote an empty-looking image (${bytes} bytes)")
    endif ()
endforeach ()
message ("renderer_version: the four ABCV preview examples rendered")
