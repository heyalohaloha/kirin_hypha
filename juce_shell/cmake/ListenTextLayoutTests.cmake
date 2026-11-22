function(kirin_add_listen_layout_test name option)
    add_test(NAME ${name} COMMAND KirinListenTextLayoutTests ${option} ${ARGN})
    set_tests_properties(${name} PROPERTIES TIMEOUT 300)
endfunction()

# Keep the existing selected name for its recovery/name/AX group, never the unpartitioned runner.
kirin_add_listen_layout_test(kirin_listen_text_layout --recovery-only)
kirin_add_listen_layout_test(kirin_listen_text_layout_keep_tail --keep-tail-notice-only)
kirin_add_listen_layout_test(kirin_listen_text_layout_footer --footer-only)
kirin_add_listen_layout_test(kirin_listen_text_layout_blind_ui --blind-ui-only)
kirin_add_listen_layout_test(kirin_listen_text_layout_blind_image --blind-image-only)
kirin_add_listen_layout_test(kirin_listen_text_layout_footer_restore --footer-restoration-only)

# A prime stride distributes the matrix's nested state and language combinations.
foreach(shard RANGE 0 16)
    kirin_add_listen_layout_test(kirin_listen_text_layout_blind_trial_${shard} --blind-trial-layout-only ${shard} 17)
endforeach()

# Two languages and six sizes, one full existing paint group per shard.
foreach(shard RANGE 0 11)
    kirin_add_listen_layout_test(kirin_listen_text_layout_paint_${shard} --paint-only ${shard} 12)
endforeach()
