#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
make clean >/dev/null
make >/dev/null

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

mkdir -p "$tmp/input"
printf 'int add(int a,int b){return a+b;}\n' > "$tmp/a.c"
printf 'const char *msg(void){return "hello";}\n' > "$tmp/b.c"
gcc -shared -fPIC "$tmp/a.c" -o "$tmp/input/liba.so"
gcc -shared -fPIC "$tmp/b.c" -o "$tmp/input/libb.so"
printf 'not a library\n' > "$tmp/input/readme.txt"

./build/sobundle "$tmp/input" "$tmp/out.ofs"

python3 - "$tmp" <<'PY'
import struct, sys, zlib
tmp=sys.argv[1]
data=open(tmp+'/out.ofs','rb').read()
assert data[:4] == b'OFS1'
count=struct.unpack('<I', data[4:8])[0]
assert count == 2, count
off=12
entries=[]
for _ in range(count):
    name=data[off:off+64].split(b'\0',1)[0].decode()
    o,s,ef,h=struct.unpack('<QQII', data[off+64:off+88])
    entries.append((name,o,s,h))
    off+=88
for name,o,s,h in entries:
    assert o % 16384 == 0
    payload=data[o:o+s]
    original=open(tmp+'/input/'+name,'rb').read()
    assert payload == original
    assert zlib.crc32(payload) & 0xffffffff == h
print('OFS test passed')
PY
