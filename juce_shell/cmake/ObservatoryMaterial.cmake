# These painters call the shared Jungle material implementation. Keep their
# source dependency together for plugins, renderers, and component tests.
set(KIRIN_HYPHA_OBSERVATORY_MATERIAL_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../src/HyphaHybridVuPainter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/HyphaObservatoryWorld.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/HyphaJungleMaterial.cpp")
