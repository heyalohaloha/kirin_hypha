# One dependency graph lets Make/MSBuild schedule independent native targets together.
# Discover targets after registration so adding a test cannot silently omit its build.
add_custom_target(KirinNativeTests)
get_property(kirin_native_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
set(kirin_native_build_inventory "")
foreach(target IN LISTS kirin_native_targets)
    get_target_property(type ${target} TYPE)
    if(target MATCHES "^Kirin" AND
       (type STREQUAL "EXECUTABLE" OR type STREQUAL "OBJECT_LIBRARY"))
        add_dependencies(KirinNativeTests ${target})
        string(APPEND kirin_native_build_inventory "${target}\n")
    endif()
endforeach()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/native-build-targets-$<CONFIG>.txt"
    CONTENT "${kirin_native_build_inventory}")
if(WIN32)
    add_custom_target(KirinWindowsPreflight
        DEPENDS KirinNativeTests KirinHyphaPRE_VST3 KirinHyphaPOST_VST3)
endif()
