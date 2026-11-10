#!/usr/bin/env bash
# Route the CI's CMake compiles through ccache. Only the objects are reused: every configure,
# link and test still runs. Paths are made relative to the checkout so all shards share hits.
set -euo pipefail
cache_dir="${1:?cache directory required}"
if ! command -v ccache >/dev/null; then
  HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_INSTALL_CLEANUP=1 HOMEBREW_NO_ENV_HINTS=1 brew install ccache
fi
mkdir -p "$cache_dir"
{
  echo "CCACHE_DIR=$cache_dir"
  echo "CCACHE_BASEDIR=${GITHUB_WORKSPACE:?}"
  echo "CCACHE_COMPILERCHECK=content"
  echo "CCACHE_MAXSIZE=2G"
  for language in C CXX OBJC OBJCXX; do echo "CMAKE_${language}_COMPILER_LAUNCHER=ccache"; done
} >> "${GITHUB_ENV:?}"
CCACHE_DIR="$cache_dir" ccache --zero-stats
