#!/usr/bin/env bash
# Verify that every Android artifact is an AArch64 (arm64-v8a) ELF.
set -euo pipefail

cd "$(dirname "$0")/.."

readelf="${ANDROID_READELF:-}"
if [ -z "$readelf" ] || [ ! -x "$readelf" ]; then
    readelf="$(command -v llvm-readelf || command -v readelf)"
fi

failed=0
for artifact in build/lib/arm64-v8a/libloader.so build/lib/arm64-v8a/sobundle; do
    if [ ! -f "$artifact" ]; then
        echo "missing arm64 artifact: $artifact" >&2
        failed=1
        continue
    fi
    machine="$("$readelf" -h "$artifact" | awk -F: '/Machine:/{gsub(/^[ \t]+/, "", $2); print $2}')"
    class="$("$readelf" -h "$artifact" | awk -F: '/Class:/{gsub(/^[ \t]+/, "", $2); print $2}')"
    case "$machine" in
        AArch64*) ;;
        *)
            echo "$artifact is not AArch64 (machine: $machine)" >&2
            failed=1
            ;;
    esac
    case "$class" in
        ELF64*) ;;
        *)
            echo "$artifact is not ELF64 (class: $class)" >&2
            failed=1
            ;;
    esac
    echo "$artifact: $(file -b "$artifact")"
done

expected_dir="build/lib/arm64-v8a"
if find build/lib -name '*.so' -o -name 'sobundle' | grep -v "^$expected_dir/" | grep . >/dev/null; then
    echo "unexpected non-arm64-v8a artifact under build/lib" >&2
    find build/lib -name '*.so' -o -name 'sobundle' >&2
    failed=1
fi

if [ "$failed" -ne 0 ]; then
    echo "arm64 check FAILED" >&2
    exit 1
fi

if ! "$readelf" -sW build/lib/arm64-v8a/libloader.so | grep 'JNI_OnLoad' >/dev/null; then
    echo "libloader.so does not export JNI_OnLoad" >&2
    exit 1
fi

echo "arm64 check passed"
