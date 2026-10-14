# Included once after project(KirinHypha), then applied after all targets exist.
# Append to each module's link options; never replace CMake's platform flags.
function(kirin_hypha_link_evidence)
    if(NOT MSVC)
        message(FATAL_ERROR "Formal Windows link evidence requires MSVC")
    endif()
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/link-evidence")
    foreach(role IN ITEMS PRE POST)
        if(NOT TARGET KirinHypha${role}_VST3)
            message(FATAL_ERROR "Missing shipping VST3 target for ${role}")
        endif()
        target_link_options(KirinHypha${role}_VST3 PRIVATE
            "/MAP:${CMAKE_BINARY_DIR}/link-evidence/KirinHypha${role}.map")
    endforeach()
endfunction()
cmake_language(DEFER CALL kirin_hypha_link_evidence)
