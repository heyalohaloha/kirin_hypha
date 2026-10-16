#!/usr/bin/env bash
# Run the real-thread Rust suites on a slow host before a push. macOS background QoS coalesces
# timers like the virtual CI machines: an 11 ms sleep becomes ~100 ms there. A test that only
# passes on a fast desktop fails here first, without a CI round trip.
# Usage: bash scripts/test_slow_host.sh [extra cargo test filter]
set -euo pipefail
cd "$(cd "$(dirname "$0")/.." && pwd)"

if [[ "$(uname -s)" != "Darwin" ]] || ! command -v taskpolicy >/dev/null; then
  echo "slow host: macOS taskpolicy is required" >&2
  exit 2
fi
slow() { echo "==> (background QoS) $*"; taskpolicy -b "$@"; }

# Each package alone, as CI runs it: a feature enabled only by another package's dev-dependency
# must not hide a different wait in this package's own tests.
slow cargo test -p kirin_measure --release --locked "$@"
slow cargo test -p kirin_hypha_ffi --release --locked "$@"
echo "slow host: PASS"
