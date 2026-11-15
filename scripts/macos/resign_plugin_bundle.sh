#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "Expected one AU or VST3 bundle" >&2
    exit 2
fi
bundle=$1
case "$bundle" in
    *.component|*.vst3) ;;
    *) echo "Expected an AU or VST3 bundle" >&2; exit 2 ;;
esac

# The plist is now final for the local build. Distribution signs these bundles again later.
/usr/bin/codesign --force --sign - "$bundle"
/usr/bin/codesign --verify --strict "$bundle"
