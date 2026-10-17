#include "utils/perf_trace.h"

#ifdef GUNBROS_ENABLE_PERF_TRACE
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
#include <malloc.h>
#include <psp2/kernel/sysmem.h>

#ifndef GUNBROS_LOADING_LOG_PATH
#define GUNBROS_LOADING_LOG_PATH "ux0:data/gunbros/loading_trace.log"
#endif
#ifndef GUNBROS_LOADING_LOG_LIMIT
#define GUNBROS_LOADING_LOG_LIMIT (4u * 1024u * 1024u)
#endif

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *log_file;
static int log_attempted, log_capped;
static size_t log_bytes;
static uint64_t last_flush;
static char log_buffer[16384];

static void loading_log_open(void) {
    if (log_attempted) return;
    log_attempted = 1;
    /* Preserve the preceding session, then start a bounded new trace. */
    remove(GUNBROS_LOADING_LOG_PATH ".previous");
    rename(GUNBROS_LOADING_LOG_PATH, GUNBROS_LOADING_LOG_PATH ".previous");
    log_file = fopen(GUNBROS_LOADING_LOG_PATH, "wb");
    if (!log_file) return;
    setvbuf(log_file, log_buffer, _IOFBF, sizeof(log_buffer));
    log_bytes = fprintf(log_file,
        "Gun Bros loading trace v3; timestamps=process-us; durations=inclusive\n"
        "Limits: 4 MiB/session, one previous session. Memory samples are not peak measurements.\n");
    fflush(log_file);
}

void gunbros_perf_log(const char *format, ...) {
    char message[1536];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (n < 0) return;
    pthread_mutex_lock(&log_lock);
    loading_log_open();
    if (log_file && !log_capped) {
        /* Reserve enough space for the final marker and timestamp. */
        if (log_bytes + sizeof(message) + 128 >= GUNBROS_LOADING_LOG_LIMIT) {
            fputs("[TRACE-LIMIT] log limit reached; restart to capture another session\n", log_file);
            log_capped = 1;
            fflush(log_file);
        } else {
            uint64_t now = gunbros_perf_now();
            int written = fprintf(log_file, "t_us=%llu %s%s",
                (unsigned long long)now, message,
                n >= (int)sizeof(message) ? " [truncated]\n" : "");
            if (written < 0) {
                fclose(log_file);
                log_file = NULL;
                pthread_mutex_unlock(&log_lock);
                return;
            }
            if (written > 0) log_bytes += written;
            if (now - last_flush >= 1000000u) {
                if (fflush(log_file) != 0) {
                    fclose(log_file);
                    log_file = NULL;
                }
                last_flush = now;
            }
        }
    }
    pthread_mutex_unlock(&log_lock);
}

void gunbros_perf_log_flush(void) {
    pthread_mutex_lock(&log_lock);
    if (log_file && fflush(log_file) != 0) {
        fclose(log_file);
        log_file = NULL;
    }
    pthread_mutex_unlock(&log_lock);
}

void gunbros_perf_log_memory(const char *reason) {
    SceKernelFreeMemorySizeInfo free_mem = {0};
    free_mem.size = sizeof(free_mem);
    int result = sceKernelGetFreeMemorySize(&free_mem);
    struct mallinfo heap = mallinfo();
    GUNBROS_PERF_LOG("[PERF-MEMORY] reason=%s result=%d free_user=%d free_cdram=%d free_phycont=%d libc_used=%u libc_free=%u\n",
        reason, result, free_mem.size_user, free_mem.size_cdram, free_mem.size_phycont,
        (unsigned)heap.uordblks, (unsigned)heap.fordblks);
}
#endif
