#ifndef SOBUNDLE_H
#define SOBUNDLE_H

/*
 * sobundle.h -- OFS (Object File Stream) container layout and shared types.
 *
 * The format is intentionally simple and alignment friendly:
 *
 *   [ofs_header_t][ofs_entry_t * file_count][padding][payload 0][padding]...
 *
 * Every payload starts at an address that is a multiple of OFS_ALIGNMENT.
 * The raw bytes of each input ELF shared object are copied verbatim.
 */

#include <stdint.h>
#include <stddef.h>

#define OFS_MAGIC_BYTES "OFS1"
#define OFS_MAGIC 0x4F465331u

#define OFS_NAME_MAX 64
#define OFS_ALIGNMENT 16384u

#define OFS_FLAG_ALIGN_16K 0x00000001u

typedef struct __attribute__((packed)) {
    char magic[4];
    uint32_t file_count;
    uint32_t reserved_flags;
} ofs_header_t;

typedef struct __attribute__((packed)) {
    char name[OFS_NAME_MAX];
    uint64_t offset;
    uint64_t size;
    uint32_t elf_flags;
    uint32_t hash; /* CRC-32 of the payload bytes */
} ofs_entry_t;

/* Runtime helper used by the JNI bootstrap loader. */
typedef struct {
    int fd;
    void *mapping;
    size_t mapping_size;
    const ofs_header_t *header;
    const ofs_entry_t *entries;
} ofs_container_t;

#endif /* SOBUNDLE_H */

/* Runtime API implemented by libloader.so. */
int ofs_bootstrap(void);
void *ofs_resolve_symbol(const char *library, const char *symbol);
int ofs_container_count(void);
const char *ofs_container_path(void);
