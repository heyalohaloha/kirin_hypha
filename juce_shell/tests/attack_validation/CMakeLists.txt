cmake_minimum_required(VERSION 3.22)
project(HyphaAttackValidation VERSION 0.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(HYPHA_SHELL "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(HYPHA_JUCE_SOURCE "${HYPHA_SHELL}/JUCE" CACHE PATH "Read-only JUCE 7.0.12 source")
add_subdirectory("${HYPHA_JUCE_SOURCE}" JUCE)
juce_add_binary_data(KirinAttackValidationData SOURCES
    ${HYPHA_SHELL}/../crates/hypha_gui/assets/attack_specimen_body_v2.png)
juce_add_console_app(KirinAttackUiContractTests PRODUCT_NAME "Hypha Attack Validation")
target_sources(KirinAttackUiContractTests PRIVATE
    ../AttackUiContractTest.cpp
    ${HYPHA_SHELL}/src/HyphaAttackComponent.cpp
    ${HYPHA_SHELL}/src/HyphaAttackDetailView.cpp
    ${HYPHA_SHELL}/src/HyphaAttackInteraction.cpp
    ${HYPHA_SHELL}/src/HyphaAttackMetricPainter.cpp
    ${HYPHA_SHELL}/src/HyphaAttackOverviewGlyphPainter.cpp
    ${HYPHA_SHELL}/src/HyphaAttackOrganismPainter.cpp
    ${HYPHA_SHELL}/src/HyphaAttackPainter.cpp
    ${HYPHA_SHELL}/src/HyphaAttackSpecimenPainter.cpp
    ${HYPHA_SHELL}/src/HyphaTypography.cpp)
target_include_directories(KirinAttackUiContractTests PRIVATE
    ${HYPHA_SHELL}/../crates/kirin_hypha_ffi/include)
target_compile_definitions(KirinAttackUiContractTests PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
if(MSVC)
    target_compile_definitions(KirinAttackUiContractTests PRIVATE _CRT_SECURE_NO_WARNINGS=1)
    target_compile_options(KirinAttackUiContractTests PRIVATE /utf-8)
endif()
target_link_libraries(KirinAttackUiContractTests PRIVATE KirinAttackValidationData juce::juce_gui_basics
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
enable_testing()
add_test(NAME kirin_attack_ui_contract COMMAND KirinAttackUiContractTests)
