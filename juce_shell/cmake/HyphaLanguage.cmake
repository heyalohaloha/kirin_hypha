# Screen text in the current language (INV-S40): the translation lookup, its Japanese catalog and
# the shared UI preference file that remembers the choice. text_style draws every piece of screen
# text through the lookup, so every target that compiles HyphaTextStyle.cpp needs these too.
set(KIRIN_HYPHA_LANGUAGE_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../src/HyphaLanguage.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/HyphaUiPreferences.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/HyphaJapaneseObservatory.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/HyphaJapaneseAnalysis.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/HyphaJapaneseMenus.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/HyphaJapaneseReference.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/HyphaJapaneseReferenceCapture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/HyphaJapaneseReferenceGuide.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/HyphaJapaneseReferenceAbcv.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/HyphaJapaneseBlind.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/HyphaJapaneseInformation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/HyphaJapaneseNotices.cpp")
