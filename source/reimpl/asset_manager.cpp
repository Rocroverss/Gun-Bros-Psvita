#include "reimpl/asset_manager.h"
#include "utils/perf_trace.h"
#include "utils/data_paths.h"
#include "utils/logger.h"

#include <pthread.h>
#include <malloc.h>
#include <cstring>
#include <cstdio>
#include <libc_bridge/libc_bridge.h>
#include <fcntl.h>
#include <sys/stat.h>

typedef struct assetManager {
    int dummy = 0; // TODO: mb we will need to store something here in future
    pthread_mutex_t mLock;
} assetManager;

typedef struct aAsset {
    char * filename;
    FILE* f;
    size_t bytesRead;
    size_t fileSize;
    bool opened = false;
} asset;

static AAssetManager * g_AAssetManager = nullptr;

enum { ASSET_PATH_CACHE_MAX = 64 };
typedef struct AssetPathCacheEntry {
    bool used;
    char request[256];
    char resolved[512];
    off_t size;
} AssetPathCacheEntry;

static AssetPathCacheEntry g_asset_path_cache[ASSET_PATH_CACHE_MAX];
static unsigned int g_asset_path_cache_replace;
static pthread_mutex_t g_asset_path_cache_lock = PTHREAD_MUTEX_INITIALIZER;

static bool resolve_asset_path_cached(char *dst, size_t dst_size,
                                      const char *filename, off_t *out_size) {
    bool cacheable;
    bool found;
    struct stat st;
    size_t i;

    if (!dst || dst_size == 0 || !filename) {
        return false;
    }

    cacheable = strlen(filename) < sizeof(g_asset_path_cache[0].request);
    if (cacheable) {
        pthread_mutex_lock(&g_asset_path_cache_lock);
        for (i = 0; i < ASSET_PATH_CACHE_MAX; ++i) {
            const AssetPathCacheEntry *entry = &g_asset_path_cache[i];
            if (entry->used && strcmp(entry->request, filename) == 0) {
                snprintf(dst, dst_size, "%s", entry->resolved);
                if (out_size) {
                    *out_size = entry->size;
                }
                pthread_mutex_unlock(&g_asset_path_cache_lock);
                return true;
            }
        }
        pthread_mutex_unlock(&g_asset_path_cache_lock);
    }

    memset(&st, 0, sizeof(st));
    found = gunbros_resolve_data_path_ex(dst, dst_size, filename, false,
                                         &st, nullptr);
    if (out_size) {
        *out_size = found ? (off_t)st.st_size : 0;
    }

    /* Only successful read-only installation assets are cached. A missing
     * file is allowed to appear later and will be resolved again. */
    if (found && cacheable) {
        AssetPathCacheEntry *entry = nullptr;
        pthread_mutex_lock(&g_asset_path_cache_lock);
        for (i = 0; i < ASSET_PATH_CACHE_MAX; ++i) {
            if (!g_asset_path_cache[i].used) {
                entry = &g_asset_path_cache[i];
                break;
            }
        }
        if (!entry) {
            entry = &g_asset_path_cache[
                g_asset_path_cache_replace++ % ASSET_PATH_CACHE_MAX];
        }
        entry->used = true;
        snprintf(entry->request, sizeof(entry->request), "%s", filename);
        snprintf(entry->resolved, sizeof(entry->resolved), "%s", dst);
        entry->size = (off_t)st.st_size;
        pthread_mutex_unlock(&g_asset_path_cache_lock);
    }
    return found;
}

AAssetManager * AAssetManager_create() {
    if (g_AAssetManager) return g_AAssetManager;

    g_AAssetManager = (AAssetManager *) malloc(sizeof(assetManager));
    if (!g_AAssetManager) return nullptr;

    memset(g_AAssetManager, 0, sizeof(assetManager));
    pthread_mutex_init(&((assetManager *)g_AAssetManager)->mLock, nullptr);

    return g_AAssetManager;
}

AAsset* AAssetManager_open(AAssetManager* mgr, const char* filename, int mode) {
    char realp[512];
    off_t resolved_size = 0;
    bool have_size;

    (void)mgr;
    if (!filename) {
        return nullptr;
    }

    GUNBROS_PERF_BEGIN(open_begin);
    have_size = resolve_asset_path_cached(realp, sizeof(realp), filename, &resolved_size);
    if (!have_size) {
        gunbros_format_data_path(realp, sizeof(realp), "assets/", filename);
    }

    auto * a = new aAsset;
    a->filename = (char *) malloc(strlen(realp) + 1);
    if (!a->filename) {
        delete a;
        return nullptr;
    }
    strcpy(a->filename, realp);
    a->bytesRead = 0;

#ifdef USE_SCELIBC_IO
    a->f = sceLibcBridge_fopen((const char *)a->filename, "rb");
#else
    a->f = fopen((const char *)a->filename, "rb");
#endif

    if (!a->f) {
        free(a->filename);
        delete a;
        a = nullptr;
    } else {
        /* Large assets and BIG files are often consumed through many small
         * reads. A larger stdio window complements the existing 8 MB FIOS
         * block cache without retaining whole packs in RAM. */
        if (resolved_size >= 256 * 1024) {
#ifdef USE_SCELIBC_IO
            (void)sceLibcBridge_setvbuf(a->f, nullptr, _IOFBF, 64 * 1024);
#else
            (void)setvbuf(a->f, nullptr, _IOFBF, 64 * 1024);
#endif
        }
        a->fileSize = resolved_size > 0 ? (size_t)resolved_size : 0;
        /* Installation assets are immutable while the process runs. Path
         * resolution already obtained their size; avoid EOF seeks per open.
         * Keep the fallback for files opened without successful resolution. */
        if (!have_size) {
#ifdef USE_SCELIBC_IO
            if (sceLibcBridge_fseek(a->f, 0, SEEK_END) == 0) {
                long end = sceLibcBridge_ftell(a->f);
                if (end >= 0) {
                    a->fileSize = (size_t)end;
                }
            }
            (void)sceLibcBridge_fseek(a->f, 0, SEEK_SET);
#else
            if (fseek(a->f, 0, SEEK_END) == 0) {
                long end = ftell(a->f);
                if (end >= 0) {
                    a->fileSize = (size_t)end;
                }
            }
            (void)fseek(a->f, 0, SEEK_SET);
#endif
        }
        a->opened = true;
    }

    GUNBROS_PERF_END(GUNBROS_STAGE_ASSET_OPEN, open_begin, 0);
#ifdef GUNBROS_ENABLE_PERF_TRACE
    uint64_t open_elapsed = gunbros_perf_now() - open_begin;
    if (open_elapsed >= 20000u)
        GUNBROS_PERF_LOG("[PERF-ASSET-OPEN] path=%s elapsed_us=%llu size=%llu ok=%d\n",
            realp, (unsigned long long)open_elapsed, (unsigned long long)resolved_size, a != nullptr);
#endif
    l_debug("AAssetManager_open<%p>(%p, %s, %i): %p", __builtin_return_address(0), mgr, realp, mode, a);
    return (AAsset *) a;
}

void AAsset_close(AAsset* asset) {
    l_debug("AAsset_close<%p>(%p)", __builtin_return_address(0), asset);

    if (asset) {
        auto * a = (aAsset *) asset;
        free(a->filename);
        if (a->opened) {
#ifdef USE_SCELIBC_IO
            sceLibcBridge_fclose(a->f);
#else
            fclose(a->f);
#endif
        }
        delete a;
    }
}

int AAsset_read(AAsset* asset, void* buf, size_t count) {
    l_debug("AAsset_read<%p>(%p, %p, %i)", __builtin_return_address(0), asset, buf, count);

    if (!asset) {
        return -1;
    }

    auto * a = (aAsset *) asset;

    if (!a->opened) {
        return -1;
    }

    GUNBROS_PERF_BEGIN(read_begin);
#ifdef USE_SCELIBC_IO
    size_t ret = sceLibcBridge_fread(buf, 1, count, a->f);
#else
    size_t ret = fread(buf, 1, count, a->f);
#endif

    GUNBROS_PERF_END(GUNBROS_STAGE_READ, read_begin, ret);
#ifdef GUNBROS_ENABLE_PERF_TRACE
    uint64_t read_us = gunbros_perf_now() - read_begin;
    if (read_us >= 20000u)
        GUNBROS_PERF_LOG("[PERF-READ] backend=asset path=%s bytes=%llu elapsed_us=%llu\n",
            a->filename, (unsigned long long)ret, (unsigned long long)read_us);
#endif
    if (ret > 0) {
        a->bytesRead += ret;
        return (int) ret;
    } else {
#ifdef USE_SCELIBC_IO
        if (sceLibcBridge_feof(a->f)) {
#else
        if (feof(a->f)) {
#endif
            return 0;
        } else {
            return -1;
        }
    }
}

off_t AAsset_seek(AAsset* asset, off_t offset, int whence) {
    l_debug("AAsset_seek(%p, %d, %i)", asset, offset, whence);

    if (!asset) {
        return (off_t) -1;
    }

    auto * a = (aAsset *) asset;

    if (!a->opened) {
        return -1;
    }

#ifdef USE_SCELIBC_IO
    int ret = sceLibcBridge_fseek(a->f, offset, whence);
    off_t position = ret == 0 ? (off_t)sceLibcBridge_ftell(a->f) : (off_t)-1;
#else
    int ret = fseek(a->f, offset, whence);
    off_t position = ret == 0 ? (off_t)ftell(a->f) : (off_t)-1;
#endif

    /* Android's AAsset_seek returns the resulting absolute position, not the
     * stdio success code. Keep remaining-length accounting aligned with that
     * position so seek-heavy pack readers do not re-read or overrun assets. */
    if (position >= 0) {
        a->bytesRead = (size_t)position;
    }
    return position;
}

off_t AAsset_getRemainingLength(AAsset* asset) {
    l_debug("AAsset_getRemainingLength");
    if (!asset) {
        return (off_t) -1;
    }

    auto * a = (aAsset *) asset;

    if (!a->opened) {
        return -1;
    }

    if (a->bytesRead >= a->fileSize) {
        return 0;
    }
    return (off_t)(a->fileSize - a->bytesRead);
}

off_t AAsset_getLength(AAsset* asset) {
    l_debug("AAsset_getLength");
    if (!asset) {
        return (off_t) -1;
    }

    auto * a = (aAsset *) asset;

    return (off_t)a->fileSize;
}

AAssetDir* AAssetManager_openDir(AAssetManager* mgr, const char* dirName) {
    l_error("UNIMPLEMENTED: AAssetManager_openDir: %s", dirName);
    return (AAssetDir *)strdup("dummy");
}

const char* AAssetDir_getNextFileName(AAssetDir* assetDir) {
    l_error("UNIMPLEMENTED: AAssetDir_getNextFileName: %p", assetDir);
    return "";
}

void AAssetDir_close(AAssetDir* assetDir) {
    l_error("UNIMPLEMENTED: AAssetDir_close");
    free(assetDir);
}

int AAsset_openFileDescriptor(AAsset* asset, off_t* outStart, off_t* outLength) {
    if (!asset) {
        l_warn("AAsset_openFileDescriptor(%p, %p, %p): asset is null", asset, outStart, outLength);
        return -1;
    }
    auto * a = (aAsset *) asset;
    if (outStart) *outStart = 0;
    if (outLength) *outLength = a->fileSize;
    if (a->opened) {
        if (a->opened) {
#ifdef USE_SCELIBC_IO
            sceLibcBridge_fclose(a->f);
#else
            fclose(a->f);
#endif
        }
        a->opened = false;
    }
    int ret = open(a->filename, O_RDONLY);
    l_debug("AAsset_openFileDescriptor(%p/\"%s\", %p, %p): ret %i", asset, a->filename, outStart, outLength, ret);
    return ret;
}
