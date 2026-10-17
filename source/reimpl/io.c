/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/io.h"
#include "utils/perf_trace.h"
#include "utils/read_window.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#endif

#include "utils/logger.h"
#include "utils/utils.h"

// Includes the following inline utilities:
// int oflags_musl_to_newlib(int flags);
// dirent64_bionic * dirent_newlib_to_bionic(struct dirent* dirent_newlib);
// void stat_newlib_to_bionic(struct stat * src, stat64_bionic * dst);
#include "reimpl/bits/_struct_converters.c"

#define TRACKED_FD_MAX 32

typedef struct TrackedFdPath {
    int used;
    int fd;
    char path[PATH_MAX];
} TrackedFdPath;

static TrackedFdPath g_tracked_fds[TRACKED_FD_MAX];

/* When USE_SCELIBC_IO is enabled, every FILE* the game gets is normally
 * expected to be an opaque handle owned by Sony's real SceLibc module
 * (sceLibcBridge_*). But that module can refuse to open some perfectly
 * valid files (seen in practice for data placed outside the title's own
 * sandboxed folder), and NULL is not an option since the game doesn't
 * check for it. In that situation we fall back to a plain libc FILE*
 * instead - but every subsequent fread/fseek/fclose/etc. call on that
 * pointer needs to route to the *matching* backend, or we'll crash trying
 * to interpret a foreign struct. This little registry lets the stdio
 * wrappers below tell the two kinds of FILE* apart. */
#define NATIVE_FILE_MAX 128

static FILE *g_native_files[NATIVE_FILE_MAX];
static char g_native_file_paths[NATIVE_FILE_MAX][PATH_MAX];

static int native_file_slot(FILE *f) {
    if (!f) return -1;
    for (int i = 0; i < NATIVE_FILE_MAX; ++i) {
        if (g_native_files[i] == f) {
            return i;
        }
    }

    return -1;
}

static int mark_native_file(FILE *f, const char *path) {
    if (!f) return 0;

    int slot = native_file_slot(f);
    if (slot >= 0) {
        if (path && path[0] != '\0') {
            snprintf(g_native_file_paths[slot], sizeof(g_native_file_paths[slot]), "%s", path);
        }
        return 1;
    }

    for (int i = 0; i < NATIVE_FILE_MAX; ++i) {
        if (g_native_files[i] == NULL) {
            g_native_files[i] = f;
            if (path && path[0] != '\0') {
                snprintf(g_native_file_paths[i], sizeof(g_native_file_paths[i]), "%s", path);
            } else {
                g_native_file_paths[i][0] = '\0';
            }
            return 1;
        }
    }

    sceClibPrintf("[FDMAP] native file registry full, can't track %p\n", f);
    return 0;
}

static int is_native_file(FILE *f) {
    if (!f) return 0;
    for (int i = 0; i < NATIVE_FILE_MAX; ++i) {
        if (g_native_files[i] == f) return 1;
    }
    return 0;
}

static void unmark_native_file(FILE *f) {
    for (int i = 0; i < NATIVE_FILE_MAX; ++i) {
        if (g_native_files[i] == f) {
            g_native_files[i] = NULL;
            g_native_file_paths[i][0] = '\0';
            return;
        }
    }
}

static const char *native_file_path(FILE *f) {
    int slot = native_file_slot(f);

    if (slot >= 0 && g_native_file_paths[slot][0] != '\0') {
        return g_native_file_paths[slot];
    }

    return "(unknown)";
}

static int asset_io_log_allow(int *counter, int max) {
    if (*counter < max) {
        (*counter)++;
        return 1;
    }

    return 0;
}

static int mode_reads(const char *mode) {
    return !mode || strchr(mode, 'r') != NULL || strchr(mode, '+') != NULL;
}

static int resource_stream_path(const char *path) {
    size_t len;

    if (!path) {
        return 0;
    }
    len = strlen(path);
    return (len >= 4 && strcmp(path + len - 4, ".big") == 0) ||
           strstr(path, "packTOC_") != NULL;
}

static void tune_resource_stream(FILE *stream, const char *path, const char *mode) {
#ifdef USE_SCELIBC_IO
    if (stream && mode && mode[0] == 'r' && !strchr(mode, '+') &&
        resource_stream_path(path) && is_native_file(stream)) {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode) &&
            gunbros_window_attach(stream, st.st_size) == 0) {
            /* The shared window replaces per-file read-ahead. Avoid a second
             * stdio buffer which would otherwise be discarded on every seek. */
            (void)setvbuf(stream, NULL, _IONBF, 0);
            GUNBROS_PERF_LOG("[PERF-IO-WINDOW-OPEN] file=%p path=%s length=%llu shared_budget=262144\n",
                stream, path, (unsigned long long)st.st_size);
            return;
        }
    }
#endif
    if (stream && mode_reads(mode) && resource_stream_path(path)) {
        /* BIG readers perform many small fread/fseek operations. Retain only
         * a 128 KiB window per open pack; the existing FIOS filter remains
         * the shared 8 MiB block cache underneath it. */
        (void)setvbuf_soloader(stream, NULL, _IOFBF, 128 * 1024);
    }
}

static void asset_io_log_open(const char *op, const char *path, const char *mode,
                              FILE *file, int stat_res, const struct stat *st,
                              int err, const char *backend) {
    static int open_logs = 0;
    int suspicious = 0;

    if (!file) {
        suspicious = 1;
    } else if (mode_reads(mode)) {
        suspicious = stat_res < 0 || !S_ISREG(st->st_mode) || st->st_size <= 0;
    }

    if (suspicious || backend) {
        if (!asset_io_log_allow(&open_logs, 160)) {
            return;
        }

        sceClibPrintf("[ASSET-IO] %s path='%s' mode='%s' backend=%s file=%p stat=%d size=%lld errno=%d%s\n",
                      op ? op : "open",
                      path ? path : "(null)",
                      mode ? mode : "(null)",
                      backend ? backend : "default",
                      file,
                      stat_res,
                      stat_res == 0 ? (long long)st->st_size : -1LL,
                      err,
                      suspicious ? " suspicious" : "");
    }
}

static int tracked_fd_slot(int fd) {
    int free_slot = -1;

    for (int i = 0; i < TRACKED_FD_MAX; ++i) {
        if (g_tracked_fds[i].used && g_tracked_fds[i].fd == fd) {
            return i;
        }
        if (!g_tracked_fds[i].used && free_slot < 0) {
            free_slot = i;
        }
    }

    return free_slot;
}

static const char *tracked_fd_path(int fd) {
    for (int i = 0; i < TRACKED_FD_MAX; ++i) {
        if (g_tracked_fds[i].used && g_tracked_fds[i].fd == fd) {
            return g_tracked_fds[i].path;
        }
    }

    return NULL;
}

void gunbros_track_fd_path(int fd, const char *path) {
    int slot;

    if (fd < 0 || !path || path[0] == '\0') {
        return;
    }

    slot = tracked_fd_slot(fd);
    if (slot < 0) {
        sceClibPrintf("[FDMAP] track fd=%d failed: table full for '%s'\n", fd, path);
        return;
    }

    g_tracked_fds[slot].used = 1;
    g_tracked_fds[slot].fd = fd;
    snprintf(g_tracked_fds[slot].path, sizeof(g_tracked_fds[slot].path), "%s", path);
    sceClibPrintf("[FDMAP] track fd=%d -> '%s'\n", fd, g_tracked_fds[slot].path);
}

void gunbros_forget_fd_path(int fd) {
    for (int i = 0; i < TRACKED_FD_MAX; ++i) {
        if (g_tracked_fds[i].used && g_tracked_fds[i].fd == fd) {
            sceClibPrintf("[FDMAP] forget fd=%d ('%s')\n", fd, g_tracked_fds[i].path);
            g_tracked_fds[i].used = 0;
            g_tracked_fds[i].fd = -1;
            g_tracked_fds[i].path[0] = '\0';
            return;
        }
    }
}

static void consume_fdopen_source_fd(int fd, const char *why) {
    if (fd < 0) {
        return;
    }

    if (close(fd) == 0) {
        gunbros_forget_fd_path(fd);
        sceClibPrintf("[FDMAP] fdopen(%d): consumed duplicate fd after %s\n",
                      fd, why ? why : "path reopen");
    }
}

FILE * fopen_soloader(const char * filename, const char * mode) {
    struct stat st;
    int stat_res;
    int saved_errno = 0;

    if (!filename) {
        asset_io_log_open("fopen", NULL, mode, NULL, -1, &st, EINVAL, "failed");
        return NULL;
    }

    if (strcmp(filename, "/proc/cpuinfo") == 0) {
        return fopen_soloader("app0:/cpuinfo", mode);
    } else if (strcmp(filename, "/proc/meminfo") == 0) {
        return fopen_soloader("app0:/meminfo", mode);
    }

    stat_res = filename ? stat(filename, &st) : -1;
    if (stat_res < 0) {
        saved_errno = errno;
    }

#ifdef USE_SCELIBC_IO
    FILE* ret = sceLibcBridge_fopen(filename, mode);
    if (!ret) {
        /* Same fallback as fdopen_soloader below: SceLibcBridge can
         * refuse a perfectly valid path. Try plain libc fopen and, if
         * that works, remember this FILE* is "native" so fread/fseek/
         * fclose/etc. know to route around SceLibcBridge for it. */
        ret = fopen(filename, mode);
        saved_errno = ret ? 0 : errno;
        if (ret) {
            mark_native_file(ret, filename);
            sceClibPrintf("[FDMAP] fopen(%s, %s): SceLibcBridge refused, native fallback -> %p\n",
                          filename, mode, ret);
        }
        asset_io_log_open("fopen", filename, mode, ret, stat_res, &st,
                          saved_errno, ret ? "native-fallback" : "failed");
    }
#else
    FILE* ret = fopen(filename, mode);
    saved_errno = ret ? 0 : errno;
    asset_io_log_open("fopen", filename, mode, ret, stat_res, &st,
                      saved_errno, ret ? NULL : "failed");
#endif

    tune_resource_stream(ret, filename, mode);

    if (ret)
        l_debug("fopen(%s, %s): %p", filename, mode, ret);
    else
        l_warn("fopen(%s, %s): %p", filename, mode, ret);

    return ret;
}

FILE * fdopen_soloader(int fd, const char *mode) {
    FILE *ret;
    const char *tracked_path;

    if (!mode) {
        mode = "rb";
    }

#ifdef USE_SCELIBC_IO
    tracked_path = tracked_fd_path(fd);
    if (tracked_path) {
        ret = sceLibcBridge_fopen(tracked_path, mode);
        sceClibPrintf("[FDMAP] fdopen(%d, %s): reopen '%s' via SceLibcBridge -> %p\n",
                      fd, mode, tracked_path, ret);
        if (ret) {
            tune_resource_stream(ret, tracked_path, mode);
            consume_fdopen_source_fd(fd, "SceLibcBridge path reopen");
            return ret;
        }

        /* SceLibcBridge refused the path. Do NOT fall back to opening
         * fd itself (via sceLibcBridge_fdopen or plain fdopen): fd here
         * is very likely the result of dup(), and on this toolchain
         * dup() does not reliably produce a fully-backed duplicate
         * descriptor for sceIo-based files - both the bridge and plain
         * newlib fail to fdopen() it (confirmed: "newlib fallback -> 0x0"
         * in the logs even though fopen-by-path on the very same file
         * works fine elsewhere). Instead, open a brand new handle
         * straight from the known-good path, bypassing the suspect fd
         * entirely. */
        ret = fopen(tracked_path, mode);
        if (ret) {
            mark_native_file(ret, tracked_path);
            tune_resource_stream(ret, tracked_path, mode);
        }
        sceClibPrintf("[FDMAP] fdopen(%d, %s): SceLibcBridge refused '%s', "
                      "opening fresh by path via native fopen -> %p\n",
                      fd, mode, tracked_path, ret);
        {
            struct stat st;
            int stat_res = stat(tracked_path, &st);
            asset_io_log_open("fdopen-reopen", tracked_path, mode, ret, stat_res, &st,
                              ret ? 0 : errno, ret ? "native-fallback" : "failed");
        }
        if (ret) {
            consume_fdopen_source_fd(fd, "native path reopen");
        }
        return ret;
    }

    /* No known path for this fd - nothing to reopen by path with, so try
     * to wrap the fd itself as a last resort. */
    ret = sceLibcBridge_fdopen(fd, mode);
    sceClibPrintf("[FDMAP] fdopen(%d, %s): bridge fallback -> %p\n", fd, mode, ret);
    if (ret) {
        return ret;
    }

    ret = fdopen(fd, mode);
    if (ret) {
        mark_native_file(ret, NULL);
    }
    sceClibPrintf("[FDMAP] fdopen(%d, %s): newlib fallback -> %p\n", fd, mode, ret);
#else
    ret = fdopen(fd, mode);
    sceClibPrintf("[FDMAP] fdopen(%d, %s): newlib -> %p\n", fd, mode, ret);
#endif

    return ret;
}

int dup_soloader(int fd) {
    int newfd = dup(fd);
    if (newfd >= 0) {
        const char *path = tracked_fd_path(fd);
        if (path) {
            gunbros_track_fd_path(newfd, path);
        }
    }
    sceClibPrintf("[FDMAP] dup(%d) -> %d\n", fd, newfd);
    return newfd;
}

int open_soloader(const char * path, int oflag, ...) {
    struct stat st;
    int stat_res;
    int original_oflag = oflag;

    if (!path) {
        asset_io_log_open("open", NULL, "fd-read", NULL, -1, &st, EINVAL, "failed");
        return -1;
    }

    if (strcmp(path, "/proc/cpuinfo") == 0) {
        return open_soloader("app0:/cpuinfo", oflag);
    } else if (strcmp(path, "/proc/meminfo") == 0) {
        return open_soloader("app0:/meminfo", oflag);
    } else if (strcmp(path, "/dev/urandom") == 0) {
        return open_soloader("app0:/urandom", oflag);
    }

    mode_t mode = 0666;
    if (((oflag & BIONIC_O_CREAT) == BIONIC_O_CREAT) ||
        ((oflag & BIONIC_O_TMPFILE) == BIONIC_O_TMPFILE)) {
        va_list args;
        va_start(args, oflag);
        mode = (mode_t)(va_arg(args, int));
        va_end(args);
    }

    oflag = oflags_bionic_to_newlib(oflag);
    stat_res = path ? stat(path, &st) : -1;
    int ret = open(path, oflag, mode);
    if (ret >= 0) {
        gunbros_track_fd_path(ret, path);
        l_debug("open(%s, %x): %i", path, oflag, ret);
    } else {
        if ((original_oflag & (BIONIC_O_WRONLY | BIONIC_O_RDWR)) == 0) {
            FILE *no_file = NULL;
            asset_io_log_open("open", path, "fd-read", no_file, stat_res, &st,
                              errno, "failed");
        }
        l_warn("open(%s, %x): %i", path, oflag, ret);
    }
    return ret;
}

int fstat_soloader(int fd, stat64_bionic * buf) {
    struct stat st;
    int res = fstat(fd, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("fstat(%i): %i", fd, res);
    return res;
}

int stat_soloader(const char * path, stat64_bionic * buf) {
    struct stat st;
    int res = stat(path, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("stat(%s): %i", path, res);
    return res;
}

int fclose_soloader(FILE * f) {
    gunbros_window_drop(f);
#ifdef USE_SCELIBC_IO
    int ret;
    if (is_native_file(f)) {
        ret = fclose(f);
        unmark_native_file(f);
    } else {
        ret = sceLibcBridge_fclose(f);
    }
#else
    int ret = fclose(f);
#endif

    l_debug("fclose(%p): %i", f, ret);
    return ret;
}

int vfprintf_soloader(FILE *stream, const char *format, va_list args) {
    if (gunbros_window_detach(stream) != 0) return -1;
#ifdef USE_SCELIBC_IO
    return is_native_file(stream) ? vfprintf(stream, format, args)
                                  : sceLibcBridge_vfprintf(stream, format, args);
#else
    return vfprintf(stream, format, args);
#endif
}

int fprintf_soloader(FILE *stream, const char *format, ...) {
    int ret;
    va_list args;

    va_start(args, format);
    ret = vfprintf_soloader(stream, format, args);
    va_end(args);

    return ret;
}

void rewind_soloader(FILE *stream) {
#ifdef USE_SCELIBC_IO
    if (is_native_file(stream)) {
        gunbros_window_rewind(stream);
    } else {
        (void)sceLibcBridge_fseek(stream, 0, SEEK_SET);
    }
#else
    rewind(stream);
#endif
}

int fseeko_soloader(FILE *stream, off_t offset, int whence) {
    GUNBROS_PERF_BEGIN(seek_begin);
#ifdef USE_SCELIBC_IO
    int result = is_native_file(stream) ?
        (gunbros_window_active(stream) ? gunbros_window_seek(stream, offset, whence) : fseeko(stream, offset, whence))
                                  : sceLibcBridge_fseek(stream, (long int)offset, whence);
#else
    int result = fseeko(stream, offset, whence);
#endif
    GUNBROS_PERF_END(GUNBROS_STAGE_SEEK, seek_begin, 0);
    return result;
}

off_t ftello_soloader(FILE *stream) {
#ifdef USE_SCELIBC_IO
    return is_native_file(stream) ? (off_t)gunbros_window_tell(stream) : (off_t)sceLibcBridge_ftell(stream);
#else
    return ftello(stream);
#endif
}

int ungetc_soloader(int c, FILE *stream) {
    if (gunbros_window_detach(stream) != 0) return EOF;
#ifdef USE_SCELIBC_IO
    return is_native_file(stream) ? ungetc(c, stream) : sceLibcBridge_ungetc(c, stream);
#else
    return ungetc(c, stream);
#endif
}

int setvbuf_soloader(FILE *stream, char *buffer, int mode, size_t size) {
    if (gunbros_window_detach(stream) != 0) return -1;
#ifdef USE_SCELIBC_IO
    return is_native_file(stream) ? setvbuf(stream, buffer, mode, size)
                                  : sceLibcBridge_setvbuf(stream, buffer, mode, size);
#else
    return setvbuf(stream, buffer, mode, size);
#endif
}

int close_soloader(int fd) {
    int ret = close(fd);
    if (ret == 0) {
        gunbros_forget_fd_path(fd);
    }
    l_debug("close(%i): %i", fd, ret);
    return ret;
}

DIR* opendir_soloader(char* _pathname) {
    DIR* ret = opendir(_pathname);
    l_debug("opendir(\"%s\"): %p", _pathname, ret);
    return ret;
}

struct dirent64_bionic * readdir_soloader(DIR * dir) {
    static struct dirent64_bionic dirent_tmp;

    struct dirent* ret = readdir(dir);
    l_debug("readdir(%p): %p", dir, ret);

    if (ret) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(ret);
        memcpy(&dirent_tmp, entry_tmp, sizeof(dirent64_bionic));
        free(entry_tmp);
        return &dirent_tmp;
    }

    return NULL;
}

int readdir_r_soloader(DIR * dirp, dirent64_bionic * entry,
                       dirent64_bionic ** result) {
    struct dirent dirent_tmp;
    struct dirent * pdirent_tmp;

    int ret = readdir_r(dirp, &dirent_tmp, &pdirent_tmp);

    if (ret == 0) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(&dirent_tmp);
        memcpy(entry, entry_tmp, sizeof(dirent64_bionic));
        *result = (pdirent_tmp != NULL) ? entry : NULL;
        free(entry_tmp);
    }

    l_debug("readdir_r(%p, %p, %p): %i", dirp, entry, result, ret);
    return ret;
}

int closedir_soloader(DIR * dir) {
    int ret = closedir(dir);
    l_debug("closedir(%p): %i", dir, ret);
    return ret;
}

int fcntl_soloader(int fd, int cmd, ...) {
    l_warn("fcntl(%i, %i, ...): not implemented", fd, cmd);
    return 0;
}

int ioctl_soloader(int fd, int request, ...) {
    l_warn("ioctl(%i, %i, ...): not implemented", fd, request);
    return 0;
}

#ifdef USE_SCELIBC_IO
size_t fread_soloader(void *ptr, size_t size, size_t count, FILE *stream) {
    GUNBROS_PERF_BEGIN(read_begin);
    int native = is_native_file(stream);
    size_t ret = native ? gunbros_window_read(ptr, size, count, stream)
                        : sceLibcBridge_fread(ptr, size, count, stream);
    GUNBROS_PERF_END(GUNBROS_STAGE_READ, read_begin, (uint64_t)ret * size);
#ifdef GUNBROS_ENABLE_PERF_TRACE
    uint64_t read_us = gunbros_perf_now() - read_begin;
    if (read_us >= 20000u)
        GUNBROS_PERF_LOG("[PERF-READ] backend=%s path=%s bytes=%llu elapsed_us=%llu\n",
            native ? "native" : "bridge", native ? native_file_path(stream) : "(pack identity in PERF-REQUEST)",
            (unsigned long long)((uint64_t)ret * size), (unsigned long long)read_us);
#endif

    if (size != 0 && count != 0 && ret < count) {
        static int read_logs = 0;

        if (asset_io_log_allow(&read_logs, 128)) {
            int eof = native ? gunbros_window_eof(stream) : sceLibcBridge_feof(stream);
            int err = native ? ferror(stream) : sceLibcBridge_ferror(stream);
            long pos = native ? gunbros_window_tell(stream) : sceLibcBridge_ftell(stream);

            sceClibPrintf("[ASSET-IO] fread short file=%p path='%s' backend=%s size=%u count=%u got=%u bytes=%u/%u pos=%ld eof=%d err=%d\n",
                          stream,
                          native ? native_file_path(stream) : "(SceLibcBridge)",
                          native ? "native" : "bridge",
                          (unsigned int)size,
                          (unsigned int)count,
                          (unsigned int)ret,
                          (unsigned int)(ret * size),
                          (unsigned int)(count * size),
                          pos,
                          eof,
                          err);
        }
    }

    return ret;
}

size_t fwrite_soloader(const void *ptr, size_t size, size_t count, FILE *stream) {
    if (gunbros_window_detach(stream) != 0) return 0;
    return is_native_file(stream) ? fwrite(ptr, size, count, stream)
                                   : sceLibcBridge_fwrite(ptr, size, count, stream);
}

int fseek_soloader(FILE *stream, long int offset, int origin) {
    GUNBROS_PERF_BEGIN(seek_begin);
    int native = is_native_file(stream);
    int ret = native ? gunbros_window_seek(stream, offset, origin)
                     : sceLibcBridge_fseek(stream, offset, origin);
    GUNBROS_PERF_END(GUNBROS_STAGE_SEEK, seek_begin, 0);

    if (ret != 0) {
        static int seek_logs = 0;

        if (asset_io_log_allow(&seek_logs, 96)) {
            sceClibPrintf("[ASSET-IO] fseek failed file=%p path='%s' backend=%s offset=%ld origin=%d ret=%d\n",
                          stream,
                          native ? native_file_path(stream) : "(SceLibcBridge)",
                          native ? "native" : "bridge",
                          offset,
                          origin,
                          ret);
        }
    }

    return ret;
}

long int ftell_soloader(FILE *stream) {
    int native = is_native_file(stream);
    long int ret = native ? gunbros_window_tell(stream) : sceLibcBridge_ftell(stream);

    if (ret < 0) {
        static int tell_logs = 0;

        if (asset_io_log_allow(&tell_logs, 96)) {
            sceClibPrintf("[ASSET-IO] ftell failed file=%p path='%s' backend=%s ret=%ld\n",
                          stream,
                          native ? native_file_path(stream) : "(SceLibcBridge)",
                          native ? "native" : "bridge",
                          ret);
        }
    }

    return ret;
}

int feof_soloader(FILE *stream) {
    return is_native_file(stream) ? gunbros_window_eof(stream) : sceLibcBridge_feof(stream);
}

int ferror_soloader(FILE *stream) {
    return is_native_file(stream) ? ferror(stream) : sceLibcBridge_ferror(stream);
}

int fflush_soloader(FILE *stream) {
    if (gunbros_window_detach(stream) != 0) return EOF;
    return is_native_file(stream) ? fflush(stream) : sceLibcBridge_fflush(stream);
}

char *fgets_soloader(char *s, int n, FILE *stream) {
    if (gunbros_window_detach(stream) != 0) return NULL;
    return is_native_file(stream) ? fgets(s, n, stream) : sceLibcBridge_fgets(s, n, stream);
}

int fgetc_soloader(FILE *stream) {
    if (gunbros_window_detach(stream) != 0) return EOF;
    return is_native_file(stream) ? fgetc(stream) : sceLibcBridge_fgetc(stream);
}

int getc_soloader(FILE *stream) {
    if (gunbros_window_detach(stream) != 0) return EOF;
    return is_native_file(stream) ? getc(stream) : sceLibcBridge_getc(stream);
}

int fileno_soloader(FILE *stream) {
    if (gunbros_window_detach(stream) != 0) return -1;
    return is_native_file(stream) ? fileno(stream) : sceLibcBridge_fileno(stream);
}

int fgetpos_soloader(FILE *stream, fpos_t *pos) {
    if (gunbros_window_detach(stream) != 0) return -1;
    return is_native_file(stream) ? fgetpos(stream, pos) : sceLibcBridge_fgetpos(stream, pos);
}

int fsetpos_soloader(FILE *stream, const fpos_t *pos) {
    if (gunbros_window_detach(stream) != 0) return -1;
    return is_native_file(stream) ? fsetpos(stream, pos) : sceLibcBridge_fsetpos(stream, pos);
}

FILE *freopen_soloader(const char *path, const char *mode, FILE *stream) {
    int native = is_native_file(stream);
    gunbros_window_drop(stream);
    if (native) unmark_native_file(stream);
    FILE *result = native ? freopen(path, mode, stream) : sceLibcBridge_freopen(path, mode, stream);
    if (native && result) mark_native_file(result, path);
    tune_resource_stream(result, path, mode);
    return result;
}
#endif

int fsync_soloader(int fd) {
    int ret = fsync(fd);
    l_debug("fsync(%i): %i", fd, ret);
    return ret;
}
