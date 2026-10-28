# Sobundle

Sobundle packs multiple compiled Android native shared libraries (`.so`) for
**one CPU architecture, `arm64-v8a` (AArch64)**, into a single aligned
`libraries.ofs` container and provides a tiny JNI bootstrap loader,
`libloader.so`.

Sobundle is ARM64-only. It does not build, package or publish x86, x86_64,
armeabi-v7a or any other Android ABI. The `linux-x86_64` path in the NDK
toolchain below is the NDK's **host** toolchain directory, not a target ABI:
everything built for Android is AArch64.

## Build

Both Android artifacts (the `libloader.so` loader and the `sobundle` packager
for Android/Termux), in `build/lib/arm64-v8a/`:

```sh
make
```

Only the loader:

```sh
make android
```

Host packager for development and tests (not a published release asset):

```sh
make host
```

Verify that every Android artifact is an AArch64 ELF64 file:

```sh
make check-arm64
```

Run the packager as an AArch64 binary under qemu-user (needs
`aarch64-linux-gnu-gcc` and `qemu-aarch64`; skipped when they are missing):

```sh
make test-arm64
```

The Android NDK is located through `ANDROID_NDK`. Override it if needed:

```sh
make ANDROID_NDK=/path/to/ndk ANDROID_API=24
```

`ANDROID_ABI` and `ANDROID_TRIPLE` are fixed to `arm64-v8a` and
`aarch64-linux-android`. Any other value is rejected by the Makefile.

## Usage

On an ARM64 Linux/Android (Termux) host:

```sh
./build/lib/arm64-v8a/sobundle <input_so_dir> <output_ofs_path>
```

Example:

```sh
./build/lib/arm64-v8a/sobundle lib/arm64-v8a build/libraries.ofs
```

The host build has the same command line:

```sh
./build/host/sobundle lib/arm64-v8a build/libraries.ofs
```

Only ARM64 `.so` inputs belong in the container. The resulting layout for an
APK is:

```text
lib/
└── arm64-v8a/
    ├── libloader.so
    └── libraries.ofs
```

## OFS format

- Magic: `OFS1` (`0x4F465331`)
- `uint32_t` file count
- `uint32_t` reserved flags (`OFS_FLAG_ALIGN_16K` when 16 KiB aligned)
- `file_count` packed `ofs_entry_t` records
- Raw ELF payloads, each starting at a 16 KiB boundary

Each entry stores the original file name, absolute payload offset, payload size,
ELF `e_flags`, and CRC-32 of the payload. Input ELF bytes are copied verbatim.

## Runtime

`System.loadLibrary("loader")` triggers `JNI_OnLoad`, which locates
`libraries.ofs`, parses its index, extracts each payload into the cache
directory, and `dlopen`s it. `ofs_resolve_symbol()` can then resolve symbols
from the loaded libraries.

## Releases

A release contains `libloader.so` and `sobundle`, both built by the Android NDK
for `arm64-v8a`/API 24, plus the source archive. No x86/x86_64 binary is
published; x86_64 hosts build the packager from source with `make host`.
