#!/usr/bin/env bash
# Execute the packager as an AArch64 binary under qemu-user.  This checks the
# ARM64 code path even when the build machine is x86_64.  It is a development
# test: the release artifacts are built for Android with the NDK, not with the
# cross compiler used here.
set -euo pipefail

cd "$(dirname "$0")/.."

cross="${ARM64_LINUX_CC:-aarch64-linux-gnu-gcc}"
emulator="${ARM64_EMULATOR:-qemu-aarch64}"

for tool in "$cross" "$emulator"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "skip: $tool is not installed" >&2
        exit 0
    fi
done

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$cross" -static -std=c11 -O2 -Wall -Wextra -Wpedantic \
    -D_POSIX_C_SOURCE=200809L -o "$tmp/sobundle" sobundle_pack.c

mkdir -p "$tmp/input"
printf 'int add(int a,int b){return a+b;}\n' > "$tmp/a.c"
printf 'const char *msg(void){return "hello";}\n' > "$tmp/b.c"
"$cross" -shared -fPIC "$tmp/a.c" -o "$tmp/input/liba.so"
"$cross" -shared -fPIC "$tmp/b.c" -o "$tmp/input/libb.so"

"$emulator" "$tmp/sobundle" "$tmp/input" "$tmp/out.ofs"

python3 - "$tmp" <<'PY'
import struct, sys, zlib
tmp = sys.argv[1]
data = open(tmp + '/out.ofs', 'rb').read()
assert data[:4] == b'OFS1'
count = struct.unpack('<I', data[4:8])[0]
assert count == 2, count
off = 12
for _ in range(count):
    name = data[off:off + 64].split(b'\0', 1)[0].decode()
    o, s, ef, h = struct.unpack('<QQII', data[off + 64:off + 88])
    assert o % 16384 == 0
    payload = data[o:o + s]
    original = open(tmp + '/input/' + name, 'rb').read()
    assert payload == original
    assert zlib.crc32(payload) & 0xffffffff == h
    off += 88
print('ARM64 execution test passed')
PY
