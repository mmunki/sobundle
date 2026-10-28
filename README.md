# Sobundle

Sobundle packs multiple compiled Android native shared libraries (`.so`) for one
CPU architecture into a single aligned `libraries.ofs` container and provides a
tiny JNI bootstrap loader, `libloader.so`.

## Build

Host packager:

```sh
make
```

Android ARM64 loader:

```sh
make android
```

Both:

```sh
make all
```

The Android NDK is located through `ANDROID_NDK`. Override it if needed:

```sh
make android ANDROID_NDK=/path/to/ndk ANDROID_API=24
```

## Usage

```sh
./build/sobundle <input_so_dir> <output_ofs_path>
```

Example:

```sh
./build/sobundle lib/arm64-v8a build/libraries.ofs
```

The resulting layout for an APK is:

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
