#define _GNU_SOURCE

#include "sobundle.h"

#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define LOG_TAG "sobundle-loader"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static JavaVM *g_vm = NULL;
static ofs_container_t g_container;
static int g_container_open = 0;
static int g_bootstrapped = 0;
static int g_bootstrap_result = -1;

static char g_ofs_path[PATH_MAX];
static char g_cache_dir[PATH_MAX];

typedef struct {
    char name[OFS_NAME_MAX];
    void *handle;
} loaded_lib_t;

static loaded_lib_t *g_loaded = NULL;
static size_t g_loaded_count = 0;

static void safe_copy(char *dst, size_t dst_size, const char *src) {
    if (dst_size == 0u) {
        return;
    }
    size_t n = strlen(src);
    if (n >= dst_size) {
        n = dst_size - 1u;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int path_join(char *dst, size_t dst_size, const char *a, const char *b) {
    int n = snprintf(dst, dst_size, "%s/%s", a, b);
    return n >= 0 && (size_t)n < dst_size;
}

static int ensure_dir(const char *path) {
    if (mkdir(path, 0700) == 0) {
        return 0;
    }
    return errno == EEXIST ? 0 : -1;
}

static int name_is_safe(const char *name) {
    if (name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 0;
    }
    for (const char *p = name; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            return 0;
        }
    }
    return 1;
}

static int find_ofs_path(void) {
    const char *env_path = getenv("SOBUNDLE_OFS_PATH");
    if (env_path != NULL && env_path[0] != '\0') {
        if (access(env_path, R_OK) == 0) {
            safe_copy(g_ofs_path, sizeof g_ofs_path, env_path);
            return 0;
        }
    }

    Dl_info info;
    memset(&info, 0, sizeof info);
    if (dladdr((void *)find_ofs_path, &info) != 0 && info.dli_fname != NULL) {
        char dir[PATH_MAX];
        safe_copy(dir, sizeof dir, info.dli_fname);
        char *slash = strrchr(dir, '/');
        if (slash != NULL) {
            *slash = '\0';
            char candidate[PATH_MAX];
            if (path_join(candidate, sizeof candidate, dir, "libraries.ofs") &&
                access(candidate, R_OK) == 0) {
                safe_copy(g_ofs_path, sizeof g_ofs_path, candidate);
                return 0;
            }
        }
    }

    const char *candidates[] = {
        "libraries.ofs",
        "/data/local/tmp/libraries.ofs",
        "/sdcard/libraries.ofs"
    };
    for (size_t i = 0; i < sizeof candidates / sizeof candidates[0]; i++) {
        if (access(candidates[i], R_OK) == 0) {
            safe_copy(g_ofs_path, sizeof g_ofs_path, candidates[i]);
            return 0;
        }
    }

    return -1;
}

static void determine_cache_dir(void) {
    const char *env_dir = getenv("SOBUNDLE_CACHE_DIR");
    if (env_dir != NULL && env_dir[0] != '\0') {
        safe_copy(g_cache_dir, sizeof g_cache_dir, env_dir);
    } else {
        const char *tmp = getenv("TMPDIR");
        if (tmp != NULL && tmp[0] != '\0') {
            snprintf(g_cache_dir, sizeof g_cache_dir, "%s/sobundle-cache", tmp);
        } else {
            safe_copy(g_cache_dir, sizeof g_cache_dir, "/data/local/tmp/sobundle-cache");
        }
    }
    (void)ensure_dir(g_cache_dir);
}

static int ofs_open_container(void) {
    if (g_container_open) {
        return 0;
    }
    if (g_ofs_path[0] == '\0' && find_ofs_path() != 0) {
        LOGE("libraries.ofs was not found");
        return -1;
    }

    int fd = open(g_ofs_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        LOGE("cannot open %s: %s", g_ofs_path, strerror(errno));
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return -1;
    }
    if (st.st_size < (off_t)sizeof(ofs_header_t)) {
        close(fd);
        LOGE("container is too small");
        return -1;
    }

    void *mapping = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mapping == MAP_FAILED) {
        LOGE("cannot mmap container: %s", strerror(errno));
        return -1;
    }

    const unsigned char *bytes = mapping;
    if (bytes[0] != 'O' || bytes[1] != 'F' || bytes[2] != 'S' || bytes[3] != '1') {
        munmap(mapping, (size_t)st.st_size);
        LOGE("bad OFS magic");
        return -1;
    }

    const ofs_header_t *header = (const ofs_header_t *)bytes;
    if (header->file_count > 1000000u) {
        munmap(mapping, (size_t)st.st_size);
        LOGE("implausible file count");
        return -1;
    }

    size_t index_size = (size_t)header->file_count * sizeof(ofs_entry_t);
    if (sizeof(ofs_header_t) + index_size > (size_t)st.st_size ||
        sizeof(ofs_header_t) + index_size < sizeof(ofs_header_t)) {
        munmap(mapping, (size_t)st.st_size);
        LOGE("truncated OFS index");
        return -1;
    }

    g_container.fd = -1;
    g_container.mapping = mapping;
    g_container.mapping_size = (size_t)st.st_size;
    g_container.header = header;
    g_container.entries = (const ofs_entry_t *)(bytes + sizeof(ofs_header_t));
    g_container_open = 1;
    return 0;
}

static int extract_entry(const ofs_entry_t *entry, char *out_path, size_t out_size) {
    if (!name_is_safe(entry->name)) {
        LOGE("unsafe entry name in container");
        return -1;
    }
    if (!path_join(out_path, out_size, g_cache_dir, entry->name)) {
        LOGE("cache path is too long");
        return -1;
    }

    struct stat st;
    if (stat(out_path, &st) == 0 && (uint64_t)st.st_size == entry->size) {
        return 0;
    }

    if (entry->offset > g_container.mapping_size ||
        entry->size > g_container.mapping_size - entry->offset) {
        LOGE("entry %s is outside the container", entry->name);
        return -1;
    }

    char temp_path[PATH_MAX];
    snprintf(temp_path, sizeof temp_path, "%s/%s.tmp", g_cache_dir, entry->name);

    int fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0700);
    if (fd < 0) {
        LOGE("cannot create %s: %s", temp_path, strerror(errno));
        return -1;
    }

    const unsigned char *src = (const unsigned char *)g_container.mapping + entry->offset;
    size_t remaining = (size_t)entry->size;
    size_t done = 0;
    while (remaining > 0u) {
        size_t chunk = remaining > (1u << 20) ? (1u << 20) : remaining;
        ssize_t written = write(fd, src + done, chunk);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            unlink(temp_path);
            return -1;
        }
        done += (size_t)written;
        remaining -= (size_t)written;
    }

    if (fsync(fd) != 0) {
        close(fd);
        unlink(temp_path);
        return -1;
    }
    if (close(fd) != 0) {
        unlink(temp_path);
        return -1;
    }
    if (rename(temp_path, out_path) != 0) {
        unlink(temp_path);
        return -1;
    }
    return 0;
}

int ofs_bootstrap(void) {
    if (g_bootstrapped) {
        return g_bootstrap_result;
    }
    g_bootstrapped = 1;
    determine_cache_dir();

    if (ofs_open_container() != 0) {
        g_bootstrap_result = -1;
        return g_bootstrap_result;
    }

    uint32_t count = g_container.header->file_count;
    g_loaded_count = count;
    if (count > 0u) {
        g_loaded = calloc(count, sizeof *g_loaded);
        if (g_loaded == NULL) {
            g_bootstrap_result = -1;
            return g_bootstrap_result;
        }
    }

    for (uint32_t i = 0; i < count; i++) {
        const ofs_entry_t *entry = &g_container.entries[i];
        char path[PATH_MAX];
        if (extract_entry(entry, path, sizeof path) != 0) {
            LOGE("failed to extract %s", entry->name);
            safe_copy(g_loaded[i].name, sizeof g_loaded[i].name, entry->name);
            g_loaded[i].handle = NULL;
            continue;
        }

        safe_copy(g_loaded[i].name, sizeof g_loaded[i].name, entry->name);
        g_loaded[i].handle = dlopen(path, RTLD_LAZY | RTLD_GLOBAL);
        if (g_loaded[i].handle == NULL) {
            LOGE("dlopen(%s) failed: %s", path, dlerror());
        } else {
            LOGI("loaded %s", entry->name);
        }
    }

    g_bootstrap_result = 0;
    return g_bootstrap_result;
}

void *ofs_resolve_symbol(const char *library, const char *symbol) {
    if (symbol == NULL) {
        return NULL;
    }
    if (!g_bootstrapped) {
        ofs_bootstrap();
    }

    if (library != NULL) {
        for (size_t i = 0; i < g_loaded_count; i++) {
            if (g_loaded[i].handle != NULL && strcmp(g_loaded[i].name, library) == 0) {
                void *value = dlsym(g_loaded[i].handle, symbol);
                if (value != NULL) {
                    return value;
                }
            }
        }
    } else {
        for (size_t i = 0; i < g_loaded_count; i++) {
            if (g_loaded[i].handle != NULL) {
                void *value = dlsym(g_loaded[i].handle, symbol);
                if (value != NULL) {
                    return value;
                }
            }
        }
    }

    return dlsym(RTLD_DEFAULT, symbol);
}

int ofs_container_count(void) {
    if (!g_bootstrapped) {
        ofs_bootstrap();
    }
    return g_container_open ? (int)g_container.header->file_count : -1;
}

const char *ofs_container_path(void) {
    if (g_ofs_path[0] == '\0') {
        find_ofs_path();
    }
    return g_ofs_path;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    g_vm = vm;
    LOGI("JNI_OnLoad");
    ofs_bootstrap();
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved) {
    (void)vm;
    (void)reserved;
}

JNIEXPORT jboolean JNICALL Java_com_sobundle_Loader_init(JNIEnv *env, jclass clazz, jobject context) {
    (void)clazz;
    if (context == NULL) {
        return JNI_FALSE;
    }

    jclass context_class = (*env)->GetObjectClass(env, context);
    if (context_class == NULL) {
        return JNI_FALSE;
    }
    jmethodID get_cache_dir = (*env)->GetMethodID(env, context_class, "getCacheDir", "()Ljava/io/File;");
    if (get_cache_dir == NULL) {
        (*env)->ExceptionClear(env);
        return JNI_FALSE;
    }
    jobject cache_file = (*env)->CallObjectMethod(env, context, get_cache_dir);
    if (cache_file == NULL) {
        return JNI_FALSE;
    }
    jclass file_class = (*env)->GetObjectClass(env, cache_file);
    jmethodID get_path = (*env)->GetMethodID(env, file_class, "getAbsolutePath", "()Ljava/lang/String;");
    if (get_path == NULL) {
        (*env)->ExceptionClear(env);
        return JNI_FALSE;
    }
    jstring path_string = (jstring)(*env)->CallObjectMethod(env, cache_file, get_path);
    if (path_string == NULL) {
        return JNI_FALSE;
    }
    const char *path = (*env)->GetStringUTFChars(env, path_string, NULL);
    if (path == NULL) {
        return JNI_FALSE;
    }
    snprintf(g_cache_dir, sizeof g_cache_dir, "%s/sobundle", path);
    (*env)->ReleaseStringUTFChars(env, path_string, path);
    (void)ensure_dir(g_cache_dir);
    return ofs_bootstrap() == 0 ? JNI_TRUE : JNI_FALSE;
}
