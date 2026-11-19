# Windows SDK declarations remain in private translation units, never public headers.
# The small static archive is available to shipping and native-test targets alike.
if(WIN32)
    add_library(KirinWindowsPlatform STATIC
        src/HyphaMetricChainOwner.cpp
        src/StorageWritePathPolicy.cpp
        src/reference_audition/ReferenceChangeNotesFile.cpp
        src/reference_audition/ReferenceSourceFile.cpp
        src/reference_audition/ReferenceReceiptModuleLifetime.cpp
        src/live_compare/LiveCompareOwnerClaim.cpp)
    target_compile_features(KirinWindowsPlatform PRIVATE cxx_std_17)
    target_compile_definitions(KirinWindowsPlatform PRIVATE JUCE_USE_CURL=0)
    target_link_libraries(KirinWindowsPlatform PRIVATE juce::juce_core)
    link_libraries(KirinWindowsPlatform)
endif()
