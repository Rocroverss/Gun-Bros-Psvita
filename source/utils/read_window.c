#include "utils/read_window.h"
#include "utils/perf_trace.h"
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

enum { CURSORS = 64, WINDOWS = 4, WINDOW_BYTES = 64 * 1024 };
typedef struct Cursor {
    FILE *file;
    int64_t length, position, physical;
    int eof;
    uint64_t seeks, physical_seeks, hits, misses;
} Cursor;
typedef struct Window {
    FILE *owner;
    int64_t start;
    size_t length;
    uint64_t age;
    unsigned char bytes[WINDOW_BYTES];
} Window;
static Cursor cursors[CURSORS];
static Window windows[WINDOWS];
static uint64_t age;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static Cursor *find(FILE *file) {
    if (!file) return NULL;
    for (int i = 0; i < CURSORS; ++i)
        if (cursors[i].file == file) return &cursors[i];
    return NULL;
}
static void release(Cursor *c) {
    for (int i = 0; i < WINDOWS; ++i)
        if (windows[i].owner == c->file) windows[i].owner = NULL;
    memset(c, 0, sizeof(*c));
}
static void report(const Cursor *c) {
    (void)c;
    GUNBROS_PERF_LOG("[PERF-IO-WINDOW] file=%p seeks=%llu physical_seeks=%llu hits=%llu misses=%llu budget_bytes=%u\n",
        c->file, (unsigned long long)c->seeks, (unsigned long long)c->physical_seeks,
        (unsigned long long)c->hits, (unsigned long long)c->misses, WINDOWS * WINDOW_BYTES);
}
int gunbros_window_attach(FILE *file, int64_t length) {
    if (!file || length < 0 || length > LONG_MAX) return -1;
    pthread_mutex_lock(&lock);
    Cursor *c = find(file);
    if (c) { pthread_mutex_unlock(&lock); return 0; }
    for (int i = 0; i < CURSORS; ++i) {
        if (!cursors[i].file) {
            long pos = ftell(file);
            if (pos >= 0) {
                c = &cursors[i];
                c->file = file; c->length = length;
                c->position = c->physical = pos;
                c->eof = feof(file) != 0;
            }
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    return c ? 0 : -1;
}
int gunbros_window_active(FILE *file) {
    pthread_mutex_lock(&lock);
    int result = file && find(file) != NULL;
    pthread_mutex_unlock(&lock);
    return result;
}
size_t gunbros_window_read(void *dst, size_t size, size_t count, FILE *file) {
    if (!size || !count) return 0;
    if (count > SIZE_MAX / size) { errno = EOVERFLOW; return 0; }
    pthread_mutex_lock(&lock);
    Cursor *c = find(file);
    if (!c) { pthread_mutex_unlock(&lock); return fread(dst, size, count, file); }
    size_t needed = size * count, copied = 0;
    while (copied < needed) {
        if (c->position >= c->length) { c->eof = 1; break; }
        Window *w = NULL;
        for (int i = 0; i < WINDOWS; ++i) {
            Window *candidate = &windows[i];
            if (candidate->owner == file && c->position >= candidate->start &&
                c->position - candidate->start < (int64_t)candidate->length) {
                w = candidate; break;
            }
        }
        if (w) {
            ++c->hits;
        } else {
            ++c->misses;
            int64_t start = (c->position / WINDOW_BYTES) * WINDOW_BYTES;
            if (start != c->physical) {
                ++c->physical_seeks;
                if (fseek(file, (long)start, SEEK_SET) != 0) break;
                c->physical = start;
            }
            w = &windows[0];
            for (int i = 0; i < WINDOWS; ++i) {
                if (!windows[i].owner) { w = &windows[i]; break; }
                if (windows[i].age < w->age) w = &windows[i];
            }
            /* Invalidate before reading: an error must not expose old bytes. */
            w->owner = file; w->start = start; w->length = 0;
            int64_t remaining = c->length - start;
            size_t request = remaining < WINDOW_BYTES ? (size_t)remaining : WINDOW_BYTES;
            w->length = fread(w->bytes, 1, request, file);
            c->physical += w->length;
            if (w->length <= (size_t)(c->position - start)) {
                c->eof = feof(file) != 0;
                break;
            }
        }
        w->age = ++age;
        size_t offset = (size_t)(c->position - w->start);
        size_t amount = w->length - offset;
        if (amount > needed - copied) amount = needed - copied;
        memcpy((unsigned char *)dst + copied, w->bytes + offset, amount);
        copied += amount; c->position += amount;
    }
    pthread_mutex_unlock(&lock);
    return copied / size;
}
int gunbros_window_seek(FILE *file, int64_t offset, int origin) {
    pthread_mutex_lock(&lock);
    Cursor *c = find(file);
    if (!c) { pthread_mutex_unlock(&lock); return fseek(file, (long)offset, origin); }
    int64_t base;
    switch (origin) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = c->position; break;
        case SEEK_END: base = c->length; break;
        default: errno = EINVAL; pthread_mutex_unlock(&lock); return -1;
    }
    if (offset < -base) { errno = EINVAL; pthread_mutex_unlock(&lock); return -1; }
    if (offset > LONG_MAX - base) { errno = EOVERFLOW; pthread_mutex_unlock(&lock); return -1; }
    c->position = base + offset;
    c->eof = 0;
    ++c->seeks;
    if ((c->seeks % 512) == 0) report(c);
    pthread_mutex_unlock(&lock);
    return 0;
}
long gunbros_window_tell(FILE *file) {
    pthread_mutex_lock(&lock);
    Cursor *c = find(file);
    long result = c ? (long)c->position : ftell(file);
    pthread_mutex_unlock(&lock);
    return result;
}
int gunbros_window_eof(FILE *file) {
    pthread_mutex_lock(&lock);
    Cursor *c = find(file);
    int result = c ? c->eof : feof(file);
    pthread_mutex_unlock(&lock);
    return result;
}
int gunbros_window_error(FILE *file) { return ferror(file); }
int gunbros_window_detach(FILE *file) {
    pthread_mutex_lock(&lock);
    Cursor *c = find(file);
    if (c) {
        if (c->physical != c->position || (feof(file) != 0) != c->eof) {
            if (fseek(file, (long)c->position, SEEK_SET) != 0) {
                pthread_mutex_unlock(&lock); return -1;
            }
            if (c->eof) (void)fgetc(file); /* Preserve logical EOF on handoff. */
        }
        report(c);
        release(c);
    }
    pthread_mutex_unlock(&lock);
    return 0;
}
void gunbros_window_drop(FILE *file) {
    pthread_mutex_lock(&lock);
    Cursor *c = find(file);
    if (c) { report(c); release(c); }
    pthread_mutex_unlock(&lock);
}
void gunbros_window_rewind(FILE *file) {
    pthread_mutex_lock(&lock);
    Cursor *c = find(file);
    if (c) { c->position = 0; c->eof = 0; clearerr(file); }
    else rewind(file);
    pthread_mutex_unlock(&lock);
}
