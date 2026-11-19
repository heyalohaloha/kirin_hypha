# Explicit opt-in for unsigned local/CI diagnostics. No download or persistent cache setup.
option(KIRIN_HYPHA_USE_CCACHE "Use an installed ccache for C/C++ compilation" OFF)
if(KIRIN_HYPHA_USE_CCACHE)
    if(CMAKE_GENERATOR MATCHES "Visual Studio|Xcode")
        message(FATAL_ERROR "ccache launcher requires a Makefile or Ninja generator")
    endif()
    if(CMAKE_C_COMPILER_LAUNCHER OR CMAKE_CXX_COMPILER_LAUNCHER)
        message(FATAL_ERROR "Refusing to replace an existing compiler launcher")
    endif()
    find_program(KIRIN_HYPHA_CCACHE NAMES ccache REQUIRED)
    # Cache compiler contents, not its path/mtime; do not enable sloppiness or share PCH.
    set(CMAKE_C_COMPILER_LAUNCHER "${CMAKE_COMMAND};-E;env;CCACHE_COMPILERCHECK=content;${KIRIN_HYPHA_CCACHE}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_C_COMPILER_LAUNCHER}")
endif()
